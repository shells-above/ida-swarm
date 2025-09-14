#include "swarm_agent.h"
#include "swarm_logger.h"
#include "agent_irc_tools.h"
#include "../sdk/messages/types.h"
#include <format>
#include <thread>
#include <chrono>

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

    // Save swarm-specific state
    save_swarm_state();

    // Call base class graceful shutdown
    Agent::request_graceful_shutdown();
}


void SwarmAgent::handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message) {
    if (sender == agent_id_) {
        // Ignore own messages
        return;
    }
    
    // Ignore certain messages in #agents channel - they're only for orchestrator
    if (channel == "#agents") {
        if (message.find("AGENT_TOKEN_UPDATE | ") == 0) {
            return;
        }
        if (message.find("MARKED_CONSENSUS|") == 0) {
            // Don't inject consensus marks into agent conversation
            // Orchestrator will handle these
            return;
        }
    }
    
    // Check for manual tool execution messages
    if (message.find("MANUAL_TOOL_EXEC|") == 0) {
        handle_manual_tool_execution(channel, message);
        return;  // Don't inject as user message
    }
    
    SWARM_LOG("SwarmAgent: IRC message in %s from %s: %.100s...\n",
              channel.c_str(), sender.c_str(), message.c_str());
    
    // Emit IRC message event for UI
    event_bus_.publish(AgentEvent(AgentEvent::MESSAGE, sender, {
                                      {"channel", channel},
                                      {"message", message}
                                  }));

    // Check if this is our active conflict channel
    if (active_conflict_ && channel == active_conflict_->channel) {
        // Turn-based discussion - only process if from other agent
        if (sender != agent_id_) {
            active_conflict_->my_turn = true;  // Other agent spoke, now our turn
            SWARM_LOG("SwarmAgent: Received message from %s, now our turn\n", sender.c_str());

            // Simple injection - just show the message and remind to respond
            std::string response_prompt = std::format(
                "{} said: {}\n\n"
                "Your turn to respond. Use send_irc_message with channel='{}' to continue the discussion.\n"
                "If you both agree, use the 'mark_consensus_reached' tool (both agents must call it).",
                sender, message, channel
            );

            inject_user_message(response_prompt);
        }
    }
    
    // Handle simplified resurrection messages
    if (message.find("RESURRECT|") == 0 && channel == "#agents") {
        // Parse: RESURRECT|agent_id|channel
        std::string parts = message.substr(10);  // Skip "RESURRECT|"
        size_t pipe = parts.find('|');
        if (pipe != std::string::npos) {
            std::string target = parts.substr(0, pipe);
            std::string conflict_channel = parts.substr(pipe + 1);

            if (target == agent_id_) {
                // We're being resurrected for conflict
                SWARM_LOG("SwarmAgent: Being resurrected for conflict in channel %s\n", conflict_channel.c_str());

                SimpleConflictState state;
                state.channel = conflict_channel;
                state.my_turn = false;  // Wait for initiator to speak first
                state.resolved = false;
                active_conflict_ = state;

                // Join the conflict channel
                join_irc_channel(conflict_channel);

                // Simple prompt for resurrected agent
                inject_user_message(std::format(
                    "You've been called to resolve a conflict in channel {}.\n\n"
                    "Wait for the other agent's opening statement about the conflict.\n"
                    "Then discuss using send_irc_message with channel='{}'.\n\n"
                    "When you BOTH agree on the solution:\n"
                    "1. Use the 'mark_consensus_reached' tool with the complete agreed solution\n"
                    "2. Include ALL details: exact address, tool name, and ALL parameters\n"
                    "3. Both agents MUST call this tool for consensus to be valid",
                    conflict_channel,
                    conflict_channel
                ));

                // If we were waiting for conflict details, now start the task
                if (waiting_for_conflict_details_) {
                    waiting_for_conflict_details_ = false;
                    std::string task = "Participate in conflict resolution discussion";
                    SWARM_LOG("SwarmAgent: Starting conflict resolution task\n");
                    start_task(task);
                }
            }
        }
        return;
    }
    
    // Inject messages for the agent to see (except #agents channel and conflict channels which are handled above)
    if (channel != "#agents" && (!active_conflict_ || channel != active_conflict_->channel)) {
        inject_user_message("[" + channel + "] " + sender + ": " + message);
    }
}

void SwarmAgent::handle_conflict_notification(const ToolConflict& conflict) {
    emit_log(LogLevel::INFO, "Handling conflict notification");
    SWARM_LOG("SwarmAgent: Conflict detected, setting up discussion channel\n");

    // Create simple conflict state
    SimpleConflictState state;
    state.initial_conflict = conflict;
    state.channel = generate_conflict_channel(conflict);
    state.participating_agents.insert(conflict.first_call.agent_id);
    state.participating_agents.insert(conflict.second_call.agent_id);
    state.resolved = false;
    state.my_turn = true;  // Initiator goes first

    active_conflict_ = state;

    std::string channel = active_conflict_->channel;
    SWARM_LOG("SwarmAgent: Created conflict channel %s\n", channel.c_str());

    // Join the conflict channel
    join_irc_channel(channel);

    // Small delay to ensure orchestrator has time to join and set up session
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Identify the other agent and our position
    std::string other_agent = (conflict.first_call.agent_id == agent_id_) ?
                                  conflict.second_call.agent_id :
                                  conflict.first_call.agent_id;

    json our_params = (conflict.first_call.agent_id == agent_id_) ?
                          conflict.first_call.parameters :
                          conflict.second_call.parameters;

    // Send resurrection request if needed
    if (irc_client_ && irc_connected_) {
        std::string resurrect_msg = std::format("RESURRECT|{}|{}",
                                                other_agent, channel);
        irc_client_->send_message("#agents", resurrect_msg);
        SWARM_LOG("SwarmAgent: Sent resurrection request for agent %s\n", other_agent.c_str());
    }

    // Simple, clear prompt
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

    active_conflict_->my_turn = true;  // Initiator goes first

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

        // Update turn tracking for conflict channels
        if (active_conflict_ && channel == active_conflict_->channel && !active_conflict_->consensus_reached) {
            active_conflict_->my_turn = false;  // After sending, it's no longer our turn
            SWARM_LOG("SwarmAgent: Sent message to conflict channel, now waiting for response\n");
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
        if (tool_use->name == "send_irc_message" && active_conflict_) {
            if (tool_use->input.contains("channel")) {
                std::string target_channel = tool_use->input["channel"];
                if (target_channel == active_conflict_->channel) {
                    // Check if it's our turn
                    if (!active_conflict_->my_turn && !active_conflict_->consensus_reached) {
                        SWARM_LOG("SwarmAgent: Blocking send_irc_message - not our turn\n");
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

        // Record the tool call in the database
        SWARM_LOG("SwarmAgent: Recording tool call %s at 0x%llx in database\n", tool_use->name.c_str(), address);
        bool recorded = conflict_detector_->record_tool_call(tool_use->name, address, tool_use->input);
        if (!recorded) {
            SWARM_LOG("SwarmAgent: WARNING - Failed to record tool call in database\n");
        }

        // Check for conflicts if it's a write operation
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
                
                // CRITICAL: Don't execute the tools if there's a conflict!
                // Return error results for each tool instead
                SWARM_LOG("SwarmAgent: Preventing tool execution due to conflict\n");
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
    }

    // Now call the base class implementation to actually execute the tools
    // This handles tracking, messaging, and execution
    std::vector<claude::messages::Message> results = Agent::process_tool_calls(message, iteration);

    // After sending a message in a conflict channel, wait for the other agent's response
    if (active_conflict_ && !active_conflict_->my_turn && !active_conflict_->consensus_reached) {
        SWARM_LOG("SwarmAgent: Not our turn in conflict, waiting for response...\n");
        emit_log(LogLevel::INFO, "Waiting for other agent's response in conflict discussion");

        // Set a flag to indicate we're waiting
        conflict_waiting_for_response_ = true;

        // Wait for a message to arrive from the other agent
        while (conflict_waiting_for_response_) {
            // Check if it's now our turn (set by handle_irc_message)
            if (active_conflict_->my_turn) {
                SWARM_LOG("SwarmAgent: It's now our turn, continuing\n");
                conflict_waiting_for_response_ = false;
                break;
            }

            // Check if consensus was reached
            if (active_conflict_->consensus_reached) {
                SWARM_LOG("SwarmAgent: Consensus reached, continuing\n");
                conflict_waiting_for_response_ = false;
                break;
            }

            // Small sleep to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
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

void SwarmAgent::save_swarm_state() {
    SWARM_LOG("SwarmAgent: Saving swarm-specific state\n");

    try {
        std::filesystem::path workspace = get_agent_workspace_path();
        std::filesystem::path swarm_state_file = workspace / "swarm_state.json";

        json swarm_state = {
            {"agent_id", agent_id_},
            {"binary_name", binary_name_},
            {"swarm_config", swarm_config_},
            {"known_peers", json::array()},
            {"conflict_count", conflict_detector_ ? conflict_detector_->get_conflict_count() : 0},
            {"irc_server", irc_server_},
            {"irc_port", irc_port_}
        };

        // Save known peers
        for (const auto& [peer_id, info] : known_peers_) {
            swarm_state["known_peers"].push_back({
                {"agent_id", peer_id},
                {"task", info.task}
            });
        }

        // Save active conflict if any
        if (active_conflict_) {
            swarm_state["active_conflict"] = {
                {"channel", active_conflict_->channel},
                {"conflict_type", active_conflict_->initial_conflict.conflict_type},
                {"address", active_conflict_->initial_conflict.first_call.address},
                {"participating_agents", json::array()},
                {"agreements", json::object()},
                {"resolved", active_conflict_->resolved}
            };

            for (const auto& agent : active_conflict_->participating_agents) {
                swarm_state["active_conflict"]["participating_agents"].push_back(agent);
            }

            for (const auto& [agent, agreement] : active_conflict_->agreements) {
                swarm_state["active_conflict"]["agreements"][agent] = agreement;
            }
        }

        std::ofstream file(swarm_state_file);
        if (file.is_open()) {
            file << swarm_state.dump(2);
            file.close();
            SWARM_LOG("SwarmAgent: Swarm state saved to: %s\n", swarm_state_file.string().c_str());
        }
    } catch (const std::exception& e) {
        SWARM_LOG("SwarmAgent: Failed to save swarm state: %s\n", e.what());
    }
}

// Simplified - no longer using grader system
void SwarmAgent::check_if_all_participating_agents_agreed() {
    // This function is deprecated in the simplified system
    // Consensus is now detected through "CONSENSUS:" prefix in messages
    SWARM_LOG("SwarmAgent: check_if_all_participating_agents_agreed called (deprecated)\n");
}

void SwarmAgent::process_grader_result(const std::string& result_json) {
    // This function is deprecated in the simplified system
    // We no longer use a separate grader
    SWARM_LOG("SwarmAgent: process_grader_result called (deprecated)\n");
}

std::string SwarmAgent::generate_conflict_channel(const ToolConflict& conflict) const {
    // Use the configured format or a default
    std::string format = config_.irc.conflict_channel_format;

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

bool SwarmAgent::parse_manual_tool_message(const std::string& message, std::string& target_agent, 
                                          std::string& tool_name, json& parameters) {
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
    // Format: MANUAL_TOOL_RESULT|<agent_id>|<success/failure>|<result_json>
    std::string status = success ? "success" : "failure";
    std::string message = std::format("MANUAL_TOOL_RESULT|{}|{}|{}", 
                                      agent_id_, status, result.dump());
    
    send_irc_message(channel, message);
    
    SWARM_LOG("SwarmAgent: Sent manual tool result: %s\n", status.c_str());
    emit_log(LogLevel::INFO, std::format("Manual tool execution {}", status));
}


} // namespace llm_re::agent