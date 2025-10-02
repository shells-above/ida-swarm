#include "swarm_agent.h"
#include "swarm_logger.h"
#include "agent_irc_tools.h"
#include "../sdk/messages/types.h"
#include <format>
#include <thread>
#include <chrono>

#include "orchestrator/nogo_zone_manager.h"

namespace llm_re::agent {

SwarmAgent::SwarmAgent(const Config& config, const std::string& agent_id)
    : Agent(config, agent_id) {
    
    // ConflictDetector will be created in initialize() when we have binary_name
}

SwarmAgent::~SwarmAgent() {
    shutdown();
}

bool SwarmAgent::initialize(const json& swarm_config) {
    // First msg before logger is initialized goes to console only
    msg("SwarmAgent: Starting initialization for agent %s\n", agent_id_.c_str());
    swarm_config_ = swarm_config;
    
    // Extract binary name from config
    if (swarm_config.contains("binary_name")) {
        binary_name_ = swarm_config["binary_name"].get<std::string>();
        msg("SwarmAgent: Binary name: %s\n", binary_name_.c_str());
    } else {
        msg("SwarmAgent: WARNING - No binary_name in config, using default\n");
        binary_name_ = "unknown_binary";
    }
    
    // Initialize the logger ASAP so we capture all subsequent logs
    if (!g_swarm_logger.initialize(binary_name_, agent_id_)) {
        msg("SwarmAgent: ERROR - Failed to initialize logger for %s\n", agent_id_.c_str());
        // Continue anyway, but logs will only go to console
    }
    
    // From here on, use SWARM_LOG for all logging
    SWARM_LOG("SwarmAgent: Initializing agent %s with binary %s\n", agent_id_.c_str(), binary_name_.c_str());
    
    // Update API client log filename to include binary name
    std::string log_filename = std::format("anthropic_requests_{}_{}.log", binary_name_, agent_id_);
    api_client_.set_request_log_filename(log_filename);
    SWARM_LOG("SwarmAgent: Set API request log to /tmp/%s\n", log_filename.c_str());
    
    // Create conflict detector now that we have binary_name
    conflict_detector_ = std::make_unique<ConflictDetector>(agent_id_, binary_name_);
    
    // Set up console adapter to display agent messages
    console_adapter_ = std::make_unique<ConsoleAdapter>();
    console_adapter_->start();
    
    // Extract IRC configuration - use provided values or fall back to defaults
    irc_server_ = swarm_config.value("irc_server", config_.irc.server);
    irc_port_ = swarm_config.value("irc_port", 0);  // Default to 0 - orchestrator must provide port
    SWARM_LOG("SwarmAgent: IRC config - server: %s, port: %d\n", irc_server_.c_str(), irc_port_);

    // Initialize agent memory handler
    if (swarm_config.contains("memory_directory")) {
        std::string memory_dir = swarm_config["memory_directory"].get<std::string>();
        set_memory_directory(memory_dir);
        SWARM_LOG("SwarmAgent: Memory handler initialized at %s\n", memory_dir.c_str());
    } else {
        SWARM_LOG("SwarmAgent: WARNING - No memory_directory in config, memory tool will not work\n");
    }

    // Log task if present
    if (swarm_config.contains("task")) {
        SWARM_LOG("SwarmAgent: Task: %s\n", swarm_config["task"].get<std::string>().c_str());
    }

    // Initialize conflict detector
    SWARM_LOG("SwarmAgent: Initializing conflict detector\n");
    if (!conflict_detector_->initialize()) {
        SWARM_LOG("SwarmAgent: ERROR - Failed to initialize conflict detector\n");
        emit_log(LogLevel::ERROR, "Failed to initialize conflict detector");
        return false;
    }
    SWARM_LOG("SwarmAgent: Conflict detector initialized successfully\n");
    
    // Connect to IRC
    SWARM_LOG("SwarmAgent: Attempting to connect to IRC server\n");
    if (!connect_to_irc()) {
        SWARM_LOG("SwarmAgent: WARNING - Failed to connect to IRC, continuing without collaboration\n");
        emit_log(LogLevel::WARNING, "Failed to connect to IRC - continuing without collaboration");
        // Don't fail completely, agent can still work
    }

    // Subscribe to analysis result events to log grader responses
    event_bus_.subscribe([this](const AgentEvent& event) {
        if (event.type == AgentEvent::ANALYSIS_RESULT && event.source == agent_id_) {
            if (event.payload.contains("report")) {
                std::string report = event.payload["report"];
                SWARM_LOG("SwarmAgent: Final grader report: %s\n", report.c_str());
            }
        }
    }, {AgentEvent::ANALYSIS_RESULT});
    
    // Register SwarmAgent-specific IRC tools
    SWARM_LOG("SwarmAgent: Registering IRC communication tools\n");
    register_swarm_irc_tools(tool_registry_, this);

    // Start the base agent
    SWARM_LOG("SwarmAgent: Starting base agent worker thread\n");
    start();

    // Apply any collected no-go zones to the CodeInjectionManager if it exists
    if (code_injection_manager_ && !collected_no_go_zones_.empty()) {
        code_injection_manager_->set_no_go_zones(collected_no_go_zones_);
        SWARM_LOG("SwarmAgent: Applied %zu collected no-go zones to CodeInjectionManager\n",
                  collected_no_go_zones_.size());
    }

    SWARM_LOG("SwarmAgent: Agent %s initialization complete\n", agent_id_.c_str());
    emit_log(LogLevel::INFO, std::format("SwarmAgent {} initialized", agent_id_));
    return true;
}

void SwarmAgent::start_task(const std::string& orchestrator_prompt) {
    set_task(orchestrator_prompt);
    SWARM_LOG("SwarmAgent: Agent is now processing\n");
}

void SwarmAgent::shutdown() {
    SWARM_LOG("SwarmAgent: Shutting down agent %s\n", agent_id_.c_str());
    emit_log(LogLevel::INFO, "SwarmAgent shutting down");

    // Server will automatically broadcast departure
    if (irc_client_ && irc_connected_) {
        SWARM_LOG("SwarmAgent: Disconnecting from IRC\n");
        irc_client_->disconnect();
    }

    // Stop base agent
    SWARM_LOG("SwarmAgent: Stopping base agent\n");
    stop();

    // Clean up
    SWARM_LOG("SwarmAgent: Cleaning up resources\n");
    if (console_adapter_) {
        console_adapter_->stop();
        console_adapter_.reset();
    }
    if (irc_adapter_) {
        irc_adapter_->stop();
        irc_adapter_.reset();
    }
    conflict_detector_.reset();
    irc_client_.reset();

    SWARM_LOG("SwarmAgent: Shutdown complete\n");
}

void SwarmAgent::trigger_shutdown() {
    SWARM_LOG("SwarmAgent: Trigger shutdown for agent %s\n", agent_id_.c_str());
    emit_log(LogLevel::INFO, "SwarmAgent trigger shutdown - sending IRC logout");

    // Server will automatically broadcast departure
    if (irc_client_ && irc_connected_) {
        SWARM_LOG("SwarmAgent: Disconnecting from IRC\n");
        irc_client_->disconnect();
        irc_connected_ = false;
    }

    SWARM_LOG("SwarmAgent: IRC cleanup complete\n");
}

void SwarmAgent::request_graceful_shutdown() {
    SWARM_LOG("SwarmAgent: Graceful shutdown requested for agent %s\n", agent_id_.c_str());
    emit_log(LogLevel::INFO, "SwarmAgent graceful shutdown initiated");

    // The base Agent class will publish ANALYSIS_RESULT event which MessageAdapter
    // will convert to AGENT_RESULT and send to #results channel.
    // We don't need to send it here to avoid duplicates.
    
    // Just disconnect from IRC
    if (irc_client_ && irc_connected_) {
        // Give time for any pending messages to be sent
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        // Disconnect from IRC
        irc_client_->disconnect();
        irc_connected_ = false;
    }

    // Call base class graceful shutdown
    Agent::request_graceful_shutdown();
}


void SwarmAgent::handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message) {
    if (sender == agent_id_) {
        // Ignore own messages
        return;
    }
    
    // Ignore certain messages in #agents channel - they're only for orchestrator
    // todo: make separate channels for talking -> orchestrator and receiving commands and do proper packet handling
    // this system got more complicated than i thought it would
    if (channel == "#agents") {
        if (message.find("AGENT_TOKEN_UPDATE | ") == 0) {
            return;
        }

        if (message.find("MARKED_CONSENSUS|") == 0) {
            // Don't inject consensus marks into agent conversation
            // Orchestrator will handle these
            return;
        }

        if (message.find("JOIN_CONFLICT|") == 0) {
            return;
        }
    }
    
    // Check for manual tool execution messages from orchestrator
    if (message.find("MANUAL_TOOL_EXEC|") == 0) {
        handle_manual_tool_execution(channel, message);
        return;  // Don't inject as user message
    }

    // Check for no-go zone messages
    if (message.find("NOGO|") == 0) {
        handle_no_go_zone_message(message);
        return;  // Don't inject as user message
    }

    // Check for patch replication messages
    if (message.find("PATCH|") == 0) {
        handle_patch_replication_message(message);
        return;  // Don't inject as user message
    }
    
    SWARM_LOG("SwarmAgent: IRC message in %s from %s: %s\n", channel.c_str(), sender.c_str(), message.c_str());
    
    // Emit IRC message event for UI
    event_bus_.publish(AgentEvent(AgentEvent::MESSAGE, sender, {
                                      {"channel", channel},
                                      {"message", message}
                                  }));

    // Check if this is one of our active conflict channels
    SimpleConflictState* conflict = get_conflict_by_channel(channel);
    if (conflict && sender != agent_id_) {
        // If we're waiting for consensus complete, don't process any messages except CONSENSUS_COMPLETE
        if (conflict->waiting_for_consensus_complete) {
            // Just inject the message for context but don't trigger any action
            inject_user_message(std::format("[{}] {}: {}", channel, sender, message));
            SWARM_LOG("SwarmAgent: Received message while waiting for consensus complete, not updating turn\n");
        }
        // Skip turn updates for CONFLICT DETAILS messages
        else if (message.find("CONFLICT DETAILS:") == 0) {
            // Inject as context without triggering turn change
            inject_user_message(std::format("[{}] {}: {}", channel, sender, message));
            SWARM_LOG("SwarmAgent: Received CONFLICT DETAILS message, not updating turn\n");
        } else {
            // Turn-based discussion - update turn for this specific conflict
            conflict->my_turn = true;  // Other agent spoke, now our turn
            SWARM_LOG("SwarmAgent: Received message from %s in channel %s, now our turn\n", sender.c_str(), channel.c_str());

            // Just show the message and remind to respond
            std::string response_prompt = std::format(
                "{} said: {}\n\n"
                "Your turn to respond. Use send_irc_message with channel='{}' to continue the discussion.\n"
                "If you both agree, use the 'mark_consensus_reached' tool (both agents must call it).",
                sender, message, channel
            );

            inject_user_message(response_prompt);
        }
    }

    // Handle CONFLICT_INVITE messages for running agents
    // this invites an already running agent to a conflict channel
    if (message.find("CONFLICT_INVITE|") == 0 && channel == "#agents") {
        // Format: CONFLICT_INVITE|target|channel
        std::string parts = message.substr(16);  // Skip "CONFLICT_INVITE|"
        size_t pipe = parts.find('|');
        if (pipe != std::string::npos) {
            std::string target = parts.substr(0, pipe);
            std::string conflict_channel = parts.substr(pipe + 1);

            if (target == agent_id_) {
                SWARM_LOG("SwarmAgent: Invited to join conflict channel %s\n", conflict_channel.c_str());

                // Create basic conflict state for tracking
                SimpleConflictState state;
                state.channel = conflict_channel;
                state.my_turn = false;  // Wait for our turn in discussion
                state.consensus_reached = false;

                // Add to conflicts map
                active_conflicts_[conflict_channel] = state;
                SWARM_LOG("SwarmAgent: Added conflict for channel %s (total active: %zu)\n",
                          conflict_channel.c_str(), active_conflicts_.size());

                // Join the conflict channel
                join_irc_channel(conflict_channel);

                // Simple notification - IRC history replay will show conflict details
                inject_user_message(std::format(
                    "Joining conflict discussion in channel {}.\n"
                    "The conflict details will appear in the channel history.",
                    conflict_channel
                ));
            }
        }
        return;
    }

    // Handle CONSENSUS_COMPLETE notifications in conflict channels
    if (message == "CONSENSUS_COMPLETE" && channel.find("#conflict_") == 0) {
        SimpleConflictState* conflict = get_conflict_by_channel(channel);
        if (conflict) {
            conflict->consensus_reached = true;
            SWARM_LOG("SwarmAgent: Received CONSENSUS_COMPLETE for %s, marking consensus reached\n", channel.c_str());
            inject_user_message("[SYSTEM] Consensus has been reached and applied by the system. Conflict resolution complete.");

            // Leave the conflict channel
            if (irc_client_ && irc_connected_) {
                irc_client_->leave_channel(channel);
                SWARM_LOG("SwarmAgent: Left conflict channel %s after consensus complete\n", channel.c_str());
            }

            // Clean up completed conflicts
            remove_completed_conflicts();
        }
        return;
    }

    // Inject messages for the agent to see (except #agents channel and conflict channels which are handled above)
    if (channel != "#agents" && !get_conflict_by_channel(channel)) {
        inject_user_message("[" + channel + "] " + sender + ": " + message);
    }
}

void SwarmAgent::handle_conflict_notification(const ToolConflict& conflict) {
    emit_log(LogLevel::INFO, "Handling conflict notification");
    SWARM_LOG("SwarmAgent: Conflict detected, setting up discussion channel\n");

    // Generate unique channel name with timestamp to prevent collisions
    std::string base_channel = generate_conflict_channel(conflict);
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    std::string channel = std::format("{}_{}", base_channel, timestamp % 100000);  // Add 5-digit timestamp

    // Create simple conflict state
    SimpleConflictState state;
    state.channel = channel;
    state.consensus_reached = false;
    state.my_turn = true;  // Initiator goes first

    // Add to conflicts map (don't overwrite existing conflicts)
    active_conflicts_[channel] = state;
    SWARM_LOG("SwarmAgent: Created conflict channel %s (total active: %zu)\n",
              channel.c_str(), active_conflicts_.size());

    // Join the conflict channel
    join_irc_channel(channel);

    // Identify the other agent and our position
    std::string other_agent = (conflict.first_call.agent_id == agent_id_) ?
                                  conflict.second_call.agent_id :
                                  conflict.first_call.agent_id;

    json our_params = (conflict.first_call.agent_id == agent_id_) ?
                          conflict.first_call.parameters :
                          conflict.second_call.parameters;

    json their_params = (conflict.first_call.agent_id == agent_id_) ?
                          conflict.second_call.parameters :
                          conflict.first_call.parameters;

    // Send request for other agent to join conflict discussion
    // if agent is dead, orchestrator will resurrect
    if (irc_client_ && irc_connected_) {
        // Format: JOIN_CONFLICT|target|channel
        std::string join_msg = std::format("JOIN_CONFLICT|{}|{}", other_agent, channel);
        irc_client_->send_message("#agents", join_msg);
        SWARM_LOG("SwarmAgent: Sent request for agent %s to join conflict channel %s\n",
                  other_agent.c_str(), channel.c_str());
    }

    // Post conflict details to the channel for the other agent to see
    if (irc_client_ && irc_connected_) {
        std::string conflict_details = std::format(
            "CONFLICT DETAILS:\n"
            "Tool: {}\n"
            "Address: 0x{:x}\n"
            "Type: {}\n\n"
            "{} attempted: {}\n"
            "{} attempted: {}\n\n"
            "Let's discuss and reach consensus.",
            conflict.first_call.tool_name,
            conflict.first_call.address,
            conflict.conflict_type,
            agent_id_, our_params.dump(2),
            other_agent, their_params.dump(2)
        );
        irc_client_->send_message(channel, conflict_details);
        SWARM_LOG("SwarmAgent: Posted conflict details to channel %s\n", channel.c_str());
    }

    // Prompt for the initiating agent
    std::string conflict_prompt = std::format(
        "Conflict detected at address 0x{:x}.\n\n"
        "You're now in channel {} to discuss with {}.\n\n"
        "Start by stating your position using send_irc_message.\n"
        "Then wait for their response and continue the discussion.\n\n"
        "When you BOTH agree on the solution:\n"
        "1. Use the 'mark_consensus_reached' tool with the complete agreed solution\n"
        "2. Include ALL details: exact address, tool name, and ALL parameters\n"
        "3. Both agents MUST call this tool for consensus to be valid\n\n"
        "Remember: Use send_irc_message with channel='{}' for discussion.",
        conflict.first_call.address,
        channel,
        other_agent,
        channel
    );

    // Set turn for this specific conflict
    active_conflicts_[channel].my_turn = true;  // Initiator goes first

    inject_user_message(conflict_prompt);
    emit_log(LogLevel::INFO, std::format("Entered conflict discussion in channel {}", channel));
}

void SwarmAgent::send_irc_message(const std::string& channel, const std::string& message) {
    if (irc_client_ && irc_connected_) {
        irc_client_->send_message(channel, message);
        emit_log(LogLevel::INFO, std::format("Sent to {}: {}", channel, message));

        // Emit our own message to the event bus for UI
        event_bus_.publish(AgentEvent(AgentEvent::MESSAGE, agent_id_, {
                                          {"channel", channel},
                                          {"message", message}
                                      }));

        // Update turn tracking for the specific conflict channel
        SimpleConflictState* conflict = get_conflict_by_channel(channel);
        if (conflict && !conflict->consensus_reached) {
            conflict->my_turn = false;  // After sending, it's no longer our turn
            SWARM_LOG("SwarmAgent: Sent message to conflict channel %s, now waiting for response (%s)\n",
                      channel.c_str(), message.c_str());
        }
    } else {
        emit_log(LogLevel::WARNING, "Not connected to IRC");
    }
}


void SwarmAgent::join_irc_channel(const std::string& channel) {
    if (irc_client_ && irc_connected_) {
        irc_client_->join_channel(channel);
        emit_log(LogLevel::INFO, std::format("Joined IRC channel: {}", channel));
    }
}

void SwarmAgent::restore_conversation_history(const json& saved_conversation) {
    SWARM_LOG("SwarmAgent: Restoring conversation history\n");
    emit_log(LogLevel::INFO, "Restoring conversation history from saved state");

    try {
        // Clear current conversation
        execution_state_.clear();

        // Restore each message with 100% FULL content preservation
        for (const auto& msg_json : saved_conversation) {
            std::string role = msg_json["role"];

            // Create message with appropriate role
            claude::messages::Role msg_role = (role == "user")
                                                  ? claude::messages::Role::User
                                                  : (role == "assistant")
                                                        ? claude::messages::Role::Assistant
                                                        : claude::messages::Role::System;

            claude::messages::Message message(msg_role);

            // Restore ALL content blocks with 100% fidelity
            if (msg_json.contains("content")) {
                for (const auto& content_json : msg_json["content"]) {
                    if (!content_json.contains("type")) continue;

                    std::string type = content_json["type"];

                    if (type == "text") {
                        // Restore text content with cache control if present
                        auto text_content = claude::messages::TextContent::from_json(content_json);
                        if (text_content) {
                            message.add_content(std::move(text_content));
                        }

                    } else if (type == "tool_use") {
                        // Restore tool use content exactly
                        auto tool_content = claude::messages::ToolUseContent::from_json(content_json);
                        if (tool_content) {
                            message.add_content(std::move(tool_content));
                        }

                    } else if (type == "tool_result") {
                        // Restore tool result content with error flag and cache control
                        auto result_content = claude::messages::ToolResultContent::from_json(content_json);
                        if (result_content) {
                            message.add_content(std::move(result_content));
                        }

                    } else if (type == "thinking") {
                        // Restore thinking blocks if present
                        auto thinking_content = claude::messages::ThinkingContent::from_json(content_json);
                        if (thinking_content) {
                            message.add_content(std::move(thinking_content));
                        }

                    } else if (type == "redacted_thinking") {
                        // Restore redacted thinking blocks if present
                        auto redacted_content = claude::messages::RedactedThinkingContent::from_json(content_json);
                        if (redacted_content) {
                            message.add_content(std::move(redacted_content));
                        }
                    }
                    // Unknown types are skipped but don't fail the restoration
                }
            }

            // Add fully restored message to execution state
            execution_state_.add_message(std::move(message));
        }

        SWARM_LOG("SwarmAgent: Restored %zu messages\n",
                  execution_state_.message_count());
        emit_log(LogLevel::INFO, std::format("Restored {} messages from saved state with full content preservation",
                                             execution_state_.message_count()));

        // Mark state as valid for continuation
        execution_state_.set_valid(true);

    } catch (const std::exception& e) {
        SWARM_LOG("SwarmAgent: Failed to restore conversation: %s\n", e.what());
        emit_log(LogLevel::ERROR, std::format("Failed to restore conversation: {}", e.what()));
    }
}

std::vector<claude::messages::Message> SwarmAgent::process_tool_calls(const claude::messages::Message& message, int iteration) {
    // Log assistant's text response if present
    std::optional<std::string> assistant_text = claude::messages::ContentExtractor::extract_text(message);
    if (assistant_text.has_value() && !assistant_text->empty()) {
        SWARM_LOG("SwarmAgent: Assistant response: %s\n", assistant_text->c_str());
    }
    
    // First, extract tool uses to record them in our database
    std::vector<const claude::messages::ToolUseContent*> tool_uses = claude::messages::ContentExtractor::extract_tool_uses(message);

    if (!tool_uses.empty()) {
        SWARM_LOG("SwarmAgent: Recording %zu tool calls to database\n", tool_uses.size());
    }

    // Record each tool call and check for conflicts BEFORE execution
    for (const claude::messages::ToolUseContent* tool_use: tool_uses) {
        if (!tool_use) continue;

        SWARM_LOG("SwarmAgent: Processing tool: %s\n", tool_use->name.c_str());

        // Check turn enforcement for send_irc_message during conflicts
        // this should never happen
        if (tool_use->name == "send_irc_message") {
            if (tool_use->input.contains("channel")) {
                std::string target_channel = tool_use->input["channel"];
                auto* conflict = get_conflict_by_channel(target_channel);
                if (conflict) {
                    // Check if it's our turn in this specific conflict
                    if (!conflict->my_turn && !conflict->consensus_reached) {
                        SWARM_LOG("SwarmAgent: Blocking send_irc_message for channel %s - not our turn\n",
                                  target_channel.c_str());
                        // Return error to make agent wait
                        return {claude::messages::Message::tool_result(
                            tool_use->id,
                            json{
                                {"success", false},
                                {"error", "Please wait for the other agent to respond before sending another message."}
                            }.dump(),
                            true  // is_error
                        )};
                    }
                    // After sending, it's no longer our turn (will be handled after actual send)
                }
            }
        }

        // Extract address if present
        ea_t address = 0;
        if (tool_use->input.contains("address")) {
            try {
                if (tool_use->input["address"].is_string()) {
                    address = std::stoull(tool_use->input["address"].get<std::string>(), nullptr, 0);
                } else if (tool_use->input["address"].is_number()) {
                    address = tool_use->input["address"].get<ea_t>();
                }
            } catch (...) {
                SWARM_LOG("SwarmAgent: Could not parse address from tool input\n");
            }
        }

        // Check for conflicts BEFORE recording if it's a write operation
        if (orchestrator::ToolCallTracker::is_write_tool(tool_use->name) && address != 0) {
            SWARM_LOG("SwarmAgent: Checking for conflicts for write operation %s at 0x%llx\n", tool_use->name.c_str(), address);

            std::vector<ToolConflict> conflicts = conflict_detector_->check_conflict(tool_use->name, address);

            if (!conflicts.empty()) {
                SWARM_LOG("SwarmAgent: CONFLICT DETECTED - %zu conflicts found\n", conflicts.size());
                emit_log(LogLevel::WARNING, std::format("Conflict detected for {} at 0x{:x}", tool_use->name, address));

                // Handle each conflict
                for (const ToolConflict& conflict : conflicts) {
                    SWARM_LOG("SwarmAgent: Handling conflict with agent %s\n", conflict.first_call.agent_id.c_str());
                    handle_conflict_notification(conflict);
                }

                // CRITICAL: Don't record or execute the tools if there's a conflict!
                // Return error results for each tool instead
                SWARM_LOG("SwarmAgent: Preventing tool execution due to conflict (not recording in database)\n");
                std::vector<claude::messages::Message> error_results;
                for (const auto* tool_use : tool_uses) {
                    if (tool_use) {
                        error_results.push_back(claude::messages::Message::tool_result(
                            tool_use->id,
                            json{
                                {"success", false},
                                {"error", "Tool execution prevented due to conflict. Entering discussion phase to reach consensus."}
                            }.dump()
                        ));
                    }
                }
                return error_results;
            }
        }

        // Only record the tool call if there's no conflict (or it's not a write operation)
        SWARM_LOG("SwarmAgent: Recording tool call %s at 0x%llx in database\n", tool_use->name.c_str(), address);
        bool recorded = conflict_detector_->record_tool_call(tool_use->name, address, tool_use->input);
        if (!recorded) {
            SWARM_LOG("SwarmAgent: WARNING - Failed to record tool call in database\n");
        }
    }

    // Now call the base class implementation to actually execute the tools
    // This handles tracking, messaging, and execution
    std::vector<claude::messages::Message> results = Agent::process_tool_calls(message, iteration);

    // After sending a message in a conflict channel, wait for responses
    // Check if we have any conflicts where we're waiting for a response
    while (has_waiting_conflict()) {
        std::string waiting_channel = get_waiting_conflict_channel();
        if (waiting_channel.empty()) {
            break;  // No conflicts waiting
        }

        SimpleConflictState* conflict = get_conflict_by_channel(waiting_channel);
        if (!conflict) {
            break;  // Conflict was removed
        }

        SWARM_LOG("SwarmAgent: Waiting for response in conflict channel %s\n", waiting_channel.c_str());
        emit_log(LogLevel::INFO, std::format("Waiting for other agent's response in {}", waiting_channel));

        // Wait for this specific conflict to update
        int wait_iterations = 0;
        const int max_wait_iterations = 1200;  // 120 seconds timeout
        while (conflict && (!conflict->my_turn || conflict->waiting_for_consensus_complete) && !conflict->consensus_reached) {
            // Check if conflict still exists and is valid
            conflict = get_conflict_by_channel(waiting_channel);
            if (!conflict) {
                SWARM_LOG("SwarmAgent: Conflict %s was removed, continuing\n", waiting_channel.c_str());
                break;
            }

            // Check for timeout
            if (++wait_iterations > max_wait_iterations) {
                SWARM_LOG("SwarmAgent: Timeout waiting for response in %s, abandoning conflict\n",
                          waiting_channel.c_str());
                conflict->consensus_reached = true;  // Mark as completed to exit
                inject_user_message(std::format("[SYSTEM] Conflict resolution timed out for {}. Proceeding.",
                                               waiting_channel));
                break;
            }

            // Small sleep to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (conflict && conflict->my_turn) {
            SWARM_LOG("SwarmAgent: It's now our turn in %s, continuing\n", waiting_channel.c_str());
        }

        // Clean up any completed conflicts
        remove_completed_conflicts();
    }

    return results;
}

bool SwarmAgent::connect_to_irc() {
    SWARM_LOG("SwarmAgent: Creating IRC client for %s\n", agent_id_.c_str());
    irc_client_ = std::make_unique<irc::IRCClient>(agent_id_, irc_server_, irc_port_);

    SWARM_LOG("SwarmAgent: Connecting to IRC %s:%d\n", irc_server_.c_str(), irc_port_);
    if (!irc_client_->connect()) {
        SWARM_LOG("SwarmAgent: Failed to connect to IRC server\n");
        emit_log(LogLevel::ERROR, "Failed to connect to IRC server");
        return false;
    }
    SWARM_LOG("SwarmAgent: Successfully connected to IRC\n");

    // Set up message callback
    SWARM_LOG("SwarmAgent: Setting up IRC message callback\n");
    irc_client_->set_message_callback(
        [this](const std::string& channel, const std::string& sender, const std::string& message) {
            handle_irc_message(channel, sender, message);
        }
    );

    // Join the standard agent coordination channel
    SWARM_LOG("SwarmAgent: Joining #agents channel\n");
    irc_client_->join_channel("#agents");

    irc_connected_ = true;
    SWARM_LOG("SwarmAgent: IRC setup complete\n");
    emit_log(LogLevel::INFO, "Connected to IRC server");

    // Set up IRC adapter for event-based communication
    irc_adapter_ = std::make_unique<IRCAdapter>(
        "#agents",
        [this](const std::string& channel, const std::string& message) {
            if (irc_client_ && irc_connected_) {
                irc_client_->send_message(channel, message);
            }
        }
    );
    irc_adapter_->start();

    return true;
}

std::string SwarmAgent::generate_conflict_channel(const ToolConflict& conflict) {
    std::string format = "#conflict_{address}_{type}";

    // Replace placeholders
    size_t pos = format.find("{address}");
    if (pos != std::string::npos) {
        format.replace(pos, 9, std::format("{:x}", conflict.first_call.address));
    }

    pos = format.find("{type}");
    if (pos != std::string::npos) {
        format.replace(pos, 6, conflict.conflict_type);
    }

    return format;
}

bool SwarmAgent::parse_manual_tool_message(const std::string& message, std::string& target_agent, std::string& tool_name, json& parameters) {
    // Format: MANUAL_TOOL_EXEC|<agent_id>|<tool_name>|<json_parameters>
    if (message.find("MANUAL_TOOL_EXEC|") != 0) {
        return false;
    }
    
    std::string content = message.substr(17); // Skip "MANUAL_TOOL_EXEC|"
    
    // Find first delimiter
    size_t first_delim = content.find('|');
    if (first_delim == std::string::npos) {
        return false;
    }
    
    // Find second delimiter
    size_t second_delim = content.find('|', first_delim + 1);
    if (second_delim == std::string::npos) {
        return false;
    }
    
    // Extract components
    target_agent = content.substr(0, first_delim);
    tool_name = content.substr(first_delim + 1, second_delim - first_delim - 1);
    std::string params_str = content.substr(second_delim + 1);
    
    // Parse JSON parameters
    try {
        parameters = json::parse(params_str);
        return true;
    } catch (const json::exception& e) {
        SWARM_LOG("SwarmAgent: Failed to parse tool parameters: %s\n", e.what());
        return false;
    }
}

void SwarmAgent::handle_manual_tool_execution(const std::string& channel, const std::string& message) {
    std::string target_agent;
    std::string tool_name;
    json parameters;
    
    if (!parse_manual_tool_message(message, target_agent, tool_name, parameters)) {
        SWARM_LOG("SwarmAgent: Invalid manual tool execution message format\n");
        return;
    }
    
    // Check if this message is for us
    if (target_agent != agent_id_ && target_agent != "*") {
        // Not for us, ignore
        return;
    }
    
    SWARM_LOG("SwarmAgent: Executing manual tool call: %s with params: %s\n", 
              tool_name.c_str(), parameters.dump().c_str());
    emit_log(LogLevel::INFO, std::format("Executing consensus-enforced tool: {}", tool_name));
    
    // Execute the tool using base class method
    json result = execute_manual_tool(tool_name, parameters);
    
    // Record in conflict detector with manual flag
    if (result["success"]) {
        // Extract address if present for recording
        ea_t address = 0;
        if (parameters.contains("address")) {
            try {
                if (parameters["address"].is_string()) {
                    address = std::stoull(parameters["address"].get<std::string>(), nullptr, 0);
                } else if (parameters["address"].is_number()) {
                    address = parameters["address"].get<ea_t>();
                }
            } catch (...) {
                SWARM_LOG("SwarmAgent: Could not parse address from parameters\n");
            }
        }
        
        // Record as manual execution
        json params_with_manual = parameters;
        params_with_manual["__is_manual"] = true;
        params_with_manual["__enforced_by"] = "orchestrator_consensus";
        
        bool recorded = conflict_detector_->record_tool_call(tool_name, address, params_with_manual);
        if (!recorded) {
            SWARM_LOG("SwarmAgent: WARNING - Failed to record manual tool call in database\n");
        }
        
        // Inject user message to inform the agent
        std::string notification = std::format(
            "[SYSTEM] Consensus enforcement executed: {} with parameters: {}\n"
            "This action was applied to ensure all agents have identical data after reaching consensus.",
            tool_name, parameters.dump(2)
        );
        inject_user_message(notification);
    }
    
    // Send result back via IRC
    send_manual_tool_result(channel, result["success"], result);
}

void SwarmAgent::send_manual_tool_result(const std::string& channel, bool success, const json& result) {
    // Format: MANUAL_TOOL_RESULT | <agent_id>|<success/failure>|<result_json>
    std::string status = success ? "success" : "failure";
    std::string message = std::format("MANUAL_TOOL_RESULT | {}|{}|{}",
                                      agent_id_, status, result.dump());
    
    send_irc_message(channel, message);
    
    SWARM_LOG("SwarmAgent: Sent manual tool result: %s\n", status.c_str());
    emit_log(LogLevel::INFO, std::format("Manual tool execution {}", status));
}

void SwarmAgent::handle_no_go_zone_message(const std::string& message) {
    // Parse format: NOGO|TYPE|agent_id|start_addr|end_addr
    std::optional<orchestrator::NoGoZone> zone_opt = orchestrator::NoGoZoneManager::deserialize_zone(message);

    if (!zone_opt.has_value()) {
        SWARM_LOG("SwarmAgent: Failed to parse no-go zone message: %s\n", message.c_str());
        return;
    }

    const orchestrator::NoGoZone &zone = zone_opt.value();
    const char* type_str = (zone.type == orchestrator::NoGoZoneType::TEMP_SEGMENT) ? "TEMP_SEGMENT" : "CODE_CAVE";

    SWARM_LOG("SwarmAgent: Received no-go zone from %s: %s at 0x%llX-0x%llX\n",
        zone.agent_id.c_str(), type_str,
        (uint64_t)zone.start_address, (uint64_t)zone.end_address);

    // Add to our collection
    collected_no_go_zones_.push_back(zone);

    // Pass to CodeInjectionManager if it exists
    if (code_injection_manager_) {
        code_injection_manager_->set_no_go_zones(collected_no_go_zones_);
        SWARM_LOG("SwarmAgent: Updated CodeInjectionManager with %zu no-go zones\n",
                  collected_no_go_zones_.size());
    } else {
        SWARM_LOG("SwarmAgent: CodeInjectionManager not available, stored no-go zone for later\n");
    }

    emit_log(LogLevel::DEBUG, std::format("Received no-go zone from {}: {:#x}-{:#x}",
        zone.agent_id, zone.start_address, zone.end_address));
}

void SwarmAgent::handle_patch_replication_message(const std::string& message) {
    // Parse format: PATCH|tool_name|agent_id|address|parameters_json
    std::stringstream ss(message);
    std::string token;
    std::vector<std::string> tokens;

    while (std::getline(ss, token, '|')) {
        tokens.push_back(token);
    }

    if (tokens.size() != 5 || tokens[0] != "PATCH") {
        SWARM_LOG("SwarmAgent: Invalid patch replication message format\n");
        return;
    }

    std::string tool_name = tokens[1];
    std::string source_agent = tokens[2];

    // Parse address (handle hex format)
    ea_t address = BADADDR;
    try {
        address = std::stoull(tokens[3], nullptr, 0);
    } catch (...) {
        SWARM_LOG("SwarmAgent: Failed to parse address in patch message\n");
        return;
    }

    // Parse parameters
    json parameters;
    try {
        parameters = json::parse(tokens[4]);
    } catch (...) {
        SWARM_LOG("SwarmAgent: Failed to parse parameters in patch message\n");
        return;
    }

    SWARM_LOG("SwarmAgent: Received patch replication from %s: %s at 0x%llX\n",
        source_agent.c_str(), tool_name.c_str(), (uint64_t)address);

    // Execute the tool locally
    // Create a ToolUseContent for the tool call
    claude::messages::ToolUseContent tool_use(
        std::format("replicated_{}", source_agent),
        tool_name,
        parameters
    );

    // Execute through our tool registry
    try {
        claude::messages::Message result = tool_registry_.execute_tool_call(tool_use);

        // Log the result
        SWARM_LOG("SwarmAgent: Successfully replicated %s from %s\n",
            tool_name.c_str(), source_agent.c_str());

        // Don't inject this into the conversation - it's background synchronization
        emit_log(LogLevel::DEBUG, std::format("Replicated {} from {} at {:#x}",
            tool_name, source_agent, address));

    } catch (const std::exception& e) {
        SWARM_LOG("SwarmAgent: Failed to replicate patch: %s\n", e.what());
        emit_log(LogLevel::WARNING, std::format("Failed to replicate {} from {}: {}",
            tool_name, source_agent, e.what()));
    }
}

// Helper method implementations for multiple conflict support

std::string SwarmAgent::get_conflict_channel() const {
    // Return the first active (non-completed) conflict channel
    for (const auto& [channel, state] : active_conflicts_) {
        if (!state.consensus_reached) {
            return channel;
        }
    }
    return "";
}


SimpleConflictState* SwarmAgent::get_conflict_by_channel(const std::string& channel) {
    auto it = active_conflicts_.find(channel);
    return (it != active_conflicts_.end()) ? &it->second : nullptr;
}

bool SwarmAgent::has_waiting_conflict() const {
    for (const auto& [channel, state] : active_conflicts_) {
        if (!state.my_turn && !state.consensus_reached) {
            return true;
        }
    }
    return false;
}

std::string SwarmAgent::get_waiting_conflict_channel() const {
    for (const auto& [channel, state] : active_conflicts_) {
        if (!state.my_turn && !state.consensus_reached) {
            return channel;
        }
    }
    return "";
}

void SwarmAgent::remove_completed_conflicts() {
    std::vector<std::string> to_remove;
    for (const auto& [channel, state] : active_conflicts_) {
        if (state.consensus_reached) {
            to_remove.push_back(channel);
        }
    }
    for (const std::string& channel : to_remove) {
        SWARM_LOG("SwarmAgent: Removing completed conflict from channel %s\n", channel.c_str());
        active_conflicts_.erase(channel);
    }
}

void SwarmAgent::on_iteration_start(int iteration) {
    // Send status update on first iteration and every 10 iterations
    status_update_counter_++;
    if ((status_update_counter_ == 1 || status_update_counter_ % 10 == 0) && irc_connected_) {
        generate_and_send_status_update();
    }
}

void SwarmAgent::generate_and_send_status_update() {
    // Get last 10 assistant messages
    std::vector<claude::messages::Message> messages = execution_state_.get_messages();
    std::vector<std::string> recent_assistant_content;

    // Add current task
    std::string current_task = get_current_task();
    if (!current_task.empty()) {
        recent_assistant_content.push_back(std::format("[CURRENT TASK]: {}", current_task));
    }

    // Add previous status if exists
    if (!last_status_sent_.empty()) {
        recent_assistant_content.push_back(std::format("[PREVIOUS STATUS]: {}", last_status_sent_));
    }

    int count = 0;
    for (auto it = messages.rbegin(); it != messages.rend() && count < 10; ++it) {
        if (it->role() == claude::messages::Role::Assistant) {
            // Extract thinking blocks
            auto thinking_blocks = claude::messages::ContentExtractor::extract_thinking_blocks(*it);
            for (const auto* block : thinking_blocks) {
                if (block && !block->thinking.empty()) {
                    recent_assistant_content.push_back(std::format("[THINKING]: {}", block->thinking));
                }
            }

            // Extract text content
            std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(*it);
            if (text && !text->empty()) {
                recent_assistant_content.push_back(std::format("[RESPONSE]: {}", *text));
            }

            count++;
        }
    }

    if (recent_assistant_content.empty()) {
        // No recent content to generate status from
        return;
    }

    // Prepare prompt for Haiku
    std::string status_prompt = R"(Based on the following recent agent activity, generate a brief status update.

Recent activity from agent (newest first):
)";

    for (const auto& content : recent_assistant_content) {
        status_prompt += content + "\n";
    }

    status_prompt += R"(
Generate a JSON response with EXACTLY this format:
{
  "reasoning": "Brief reasoning about what the agent is doing",
  "current_status": "A concise status message. max 100 chars",
  "emoji": "A single emoji that represents the current activity"
}

The status should be informative and specific about what the agent is currently analyzing or doing.
Choose an emoji that best represents the activity (e.g., 🔍 for searching, 🐛 for debugging, 📊 for analyzing data, 🔧 for fixing, etc.)

Respond ONLY with the JSON, no other text.)";

    // Create request for Haiku
    claude::ChatRequestBuilder builder;
    builder.with_model(claude::Model::Haiku35)
           .with_max_tokens(500)
           .with_temperature(0.3)
           .enable_thinking(false);

    builder.add_message(claude::messages::Message::user_text(status_prompt));

    claude::ChatRequest request = builder.build();

    // Send to API
    claude::ChatResponse response = api_client_.send_request(request);

    if (!response.success) {
        SWARM_LOG("SwarmAgent: Failed to generate status update\n");
        return;
    }

    // Extract and parse JSON response
    std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(response.message);
    if (!text) {
        SWARM_LOG("SwarmAgent: No text in status update response\n");
        return;
    }

    try {
        json status_data = json::parse(*text);

        // Validate required fields
        if (!status_data.contains("current_status") || !status_data.contains("emoji")) {
            SWARM_LOG("SwarmAgent: Status update missing required fields\n");
            return;
        }

        // Save the current status for next time
        last_status_sent_ = status_data["current_status"].get<std::string>();

        // Send to IRC #status channel
        std::string status_message = status_data.dump();
        send_irc_message("#status", status_message);

        SWARM_LOG("SwarmAgent: Sent status update: %s - %s\n",
                  status_data["emoji"].get<std::string>().c_str(),
                  status_data["current_status"].get<std::string>().c_str());

    } catch (const json::exception& e) {
        SWARM_LOG("SwarmAgent: Failed to parse status update JSON: %s\n", e.what());
    }
}

} // namespace llm_re::agent