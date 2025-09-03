#include "swarm_agent.h"
#include "swarm_logger.h"  // For SWARM_LOG macro
#include "agent_irc_tools.h"
#include "../sdk/messages/types.h"  // For ContentExtractor
#include "../analysis/actions.h"  // For ActionExecutor::parse_single_address_value
#include <format>
#include <thread>
#include <chrono>

namespace llm_re::agent {

SwarmAgent::SwarmAgent(const Config& config, const std::string& agent_id)
    : Agent(config, agent_id), agent_id_(agent_id) {
    
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
    
    // Create conflict detector now that we have binary_name
    conflict_detector_ = std::make_unique<ConflictDetector>(agent_id_, binary_name_);
    
    // Set up console adapter to display agent messages
    console_adapter_ = std::make_unique<ConsoleAdapter>();
    console_adapter_->start();
    
    // Extract IRC configuration - use provided values or fall back to config defaults
    irc_server_ = swarm_config.value("irc_server", config_.irc.server);
    irc_port_ = swarm_config.value("irc_port", config_.irc.port);
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
    
    // Register additional swarm tools
    SWARM_LOG("SwarmAgent: Registering swarm-specific tools\n");
    register_swarm_tools();
    
    // Start the base agent
    SWARM_LOG("SwarmAgent: Starting base agent worker thread\n");
    start();
    
    SWARM_LOG("SwarmAgent: Agent %s initialization complete\n", agent_id_.c_str());
    emit_log(LogLevel::INFO, std::format("SwarmAgent {} initialized", agent_id_));
    return true;
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
    
    // Announce our presence with task
    SWARM_LOG("SwarmAgent: Announcing presence to swarm\n");
    announce_presence();
    
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

void SwarmAgent::register_swarm_tools() {
    // Register simplified IRC tools (no conflict detector needed)
    register_agent_irc_tools(tool_registry_, this);
}

void SwarmAgent::start_task(const std::string& orchestrator_prompt) {
    set_task(orchestrator_prompt);
    SWARM_LOG("SwarmAgent: Agent is now processing\n");
}

std::vector<claude::messages::Message> SwarmAgent::process_tool_calls(const claude::messages::Message& message, int iteration) {
    // First, extract tool uses to record them in our database
    auto tool_uses = claude::messages::ContentExtractor::extract_tool_uses(message);
    
    if (!tool_uses.empty()) {
        SWARM_LOG("SwarmAgent: Recording %zu tool calls to database\n", tool_uses.size());
    }
    
    // Record each tool call and check for conflicts BEFORE execution
    for (const auto* tool_use : tool_uses) {
        if (!tool_use) continue;
        
        SWARM_LOG("SwarmAgent: Processing tool: %s\n", tool_use->name.c_str());
        
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
        SWARM_LOG("SwarmAgent: Recording tool call %s at 0x%llx in database\n",
            tool_use->name.c_str(), address);
        bool recorded = conflict_detector_->record_tool_call(tool_use->name, address, tool_use->input);
        if (!recorded) {
            SWARM_LOG("SwarmAgent: WARNING - Failed to record tool call in database\n");
        }
        
        // Check for conflicts if it's a write operation
        if (orchestrator::ToolCallTracker::is_write_tool(tool_use->name) && address != 0) {
            SWARM_LOG("SwarmAgent: Checking for conflicts for write operation %s at 0x%llx\n",
                tool_use->name.c_str(), address);
            
            std::vector<ToolConflict> conflicts = conflict_detector_->check_conflict(tool_use->name, address);
            
            if (!conflicts.empty()) {
                SWARM_LOG("SwarmAgent: CONFLICT DETECTED - %zu conflicts found\n", conflicts.size());
                emit_log(LogLevel::WARNING, std::format("Conflict detected for {} at 0x{:x}", 
                    tool_use->name, address));
                
                // Handle each conflict
                for (const ToolConflict& conflict : conflicts) {
                    SWARM_LOG("SwarmAgent: Handling conflict with agent %s\n",
                        conflict.first_call.agent_id.c_str());
                    handle_conflict_notification(conflict);
                }
            }
        }
    }
    
    // Now call the base class implementation to actually execute the tools
    // This handles tracking, messaging, and execution
    return Agent::process_tool_calls(message, iteration);
}

void SwarmAgent::handle_conflict_notification(const ToolConflict& conflict) {
    emit_log(LogLevel::INFO, "Handling conflict notification");
    
    // Generate channel name for this conflict using configured format
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
    std::string channel = format;
    
    // Store conflict info
    active_conflicts_[channel] = conflict;
    
    // Join the conflict channel
    join_irc_channel(channel);
    
    // Force the other agent to join by sending them a special CONFLICT_FORCE message
    if (irc_client_ && irc_connected_) {
        // Send a message that will trigger the other agent to join
        std::string force_msg = std::format("CONFLICT_FORCE:{}:{}", 
            conflict.first_call.agent_id, channel);
        irc_client_->send_message("#agents", force_msg);
    }
    
    // Start deliberation with the channel we already created
    initiate_conflict_deliberation(conflict, channel);
}

void SwarmAgent::initiate_conflict_deliberation(const ToolConflict& conflict, const std::string& channel) {
    // Inject conflict into agent's conversation
    std::string conflict_prompt = std::format(R"(
CONFLICT DETECTED - IRC DELIBERATION REQUIRED

Another agent has applied different metadata at address 0x{:x}:
- {} set {} to: {}
- You want to set it to: {}

You are now in IRC channel {} to discuss this conflict.
Present your reasoning and evidence. Work with the other agent to determine the most accurate value.

Use the send_irc_message tool to communicate in the channel.
)", 
        conflict.first_call.address,
        conflict.first_call.agent_id,
        conflict.conflict_type,
        conflict.first_call.parameters.dump(),
        conflict.second_call.parameters.dump(),
        channel
    );
    
    inject_user_message(conflict_prompt);
}

void SwarmAgent::handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message) {
    if (sender == agent_id_) {
        // Ignore own messages
        return;
    }
    
    SWARM_LOG("SwarmAgent: IRC message in %s from %s: %.100s...\n",
        channel.c_str(), sender.c_str(), message.c_str());
    
    // Emit IRC message event for UI
    event_bus_.publish(AgentEvent(AgentEvent::MESSAGE, sender, {
        {"channel", channel},
        {"message", message}
    }));
    
    // Handle system messages from the server
    if (sender == "SYSTEM") {
        // Parse system notifications
        if (message.find("AGENT_JOIN:") == 0) {
            // Extract agent_id and task
            std::string info = message.substr(11); // Skip "AGENT_JOIN:"
            size_t pipe_pos = info.find('|');
            if (pipe_pos != std::string::npos) {
                std::string new_agent_id = info.substr(0, pipe_pos);
                std::string task = info.substr(pipe_pos + 1);
                
                // Update peer list
                AgentPeerInfo peer_info;
                peer_info.agent_id = new_agent_id;
                peer_info.task = task;
                peer_info.discovered_at = std::chrono::steady_clock::now();
                known_peers_[new_agent_id] = peer_info;
                
                // Inject notification for LLM
                inject_user_message(std::format(
                    "📢 New agent joined: {} (working on: {})",
                    new_agent_id, task
                ));
            }
        } else if (message.find("AGENT_LEAVE:") == 0) {
            std::string departed_id = message.substr(12); // Skip "AGENT_LEAVE:"
            handle_peer_departure(departed_id);
        }
        return;
    }
    
    // Handle conflict forcing from other agents
    if (message.find("CONFLICT_FORCE:") == 0 && message.find(agent_id_) != std::string::npos) {
        // Parse CONFLICT_FORCE:agent_id:channel
        size_t first_colon = message.find(':', 14);
        size_t second_colon = message.find(':', first_colon + 1);
        if (second_colon != std::string::npos) {
            std::string target_agent = message.substr(14, first_colon - 14);
            std::string conflict_channel = message.substr(second_colon + 1);
            
            if (target_agent == agent_id_) {
                // We're being forced into a conflict channel
                current_private_channel_ = conflict_channel;
                irc_client_->join_channel(conflict_channel);
                
                // Inject urgent notification
                inject_user_message(std::format(  // todo make use force_join_conflict_channel
                    "⚠️ CONFLICT DETECTED - IMMEDIATE DELIBERATION REQUIRED\n"
                    "You've been added to {} for mandatory conflict resolution.\n"
                    "Another agent has conflicting changes. Please discuss and reach consensus.\n"
                    "You have full access to all analysis tools during deliberation.\n"
                    "Use leave_private_channel when consensus is reached.",
                    conflict_channel
                ));
                
                emit_log(LogLevel::INFO, "Force-joined conflict channel: " + conflict_channel);
            }
        }
        return;
    }
    
    // Regular messages - inject with context
    if (channel == "#agents") {
        // General broadcast channel
        inject_user_message("[broadcast] " + sender + ": " + message);
    } else if (channel.find("#private_") == 0) {
        // Private conversation
        inject_user_message("[private] " + sender + ": " + message);
    } else if (channel.find("#conflict_") == 0) {
        // Conflict deliberation
        inject_user_message("[conflict] " + sender + ": " + message);
    } else {
        // Other channels
        inject_user_message("[" + channel + "] " + sender + ": " + message);
    }
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

void SwarmAgent::start_private_conversation(const std::string& target_agent, const std::string& initial_message) {
    if (!irc_client_ || !irc_connected_) {
        emit_log(LogLevel::WARNING, "Cannot start private conversation - not connected to IRC");
        return;
    }
    
    // Create private channel name
    std::string channel = "#private_" + agent_id_ + "_" + target_agent;
    current_private_channel_ = channel;
    
    // Join the channel
    irc_client_->join_channel(channel);
    
    // Notify the target agent to join
    irc_client_->send_message("#agents", "@" + target_agent + " JOIN " + channel);
    
    // Send initial message
    irc_client_->send_message(channel, initial_message);
    
    // Inject notification into our conversation
    inject_user_message("💬 Started private conversation with " + target_agent + " in " + channel);
    emit_log(LogLevel::INFO, "Started private conversation with " + target_agent);
}

std::string SwarmAgent::leave_private_channel() {
    if (current_private_channel_.empty()) {
        return "";
    }
    
    std::string channel = current_private_channel_;
    
    // Leave the channel
    if (irc_client_ && irc_connected_) {
        irc_client_->leave_channel(channel);
    }
    
    current_private_channel_.clear();
    
    // Inject notification
    inject_user_message("👋 Left " + channel + " and returned to main task");
    emit_log(LogLevel::INFO, "Left private channel: " + channel);
    
    return channel;
}

void SwarmAgent::force_join_conflict_channel(const std::string& channel, const ToolConflict& conflict) {
    if (!irc_client_ || !irc_connected_) {
        emit_log(LogLevel::WARNING, "Cannot join conflict channel - not connected to IRC");
        return;
    }
    
    // Join the conflict channel
    current_private_channel_ = channel;
    irc_client_->join_channel(channel);
    
    // Inject urgent notification into agent's conversation
    std::string notification = std::format(
        "⚠️ CONFLICT DETECTED - IMMEDIATE DELIBERATION REQUIRED\n"
        "You've been added to {} for mandatory conflict resolution.\n"
        "Another agent modified the same location (0x{:x}).\n"
        "Their change: {} used {}({})\n"
        "Your attempted change: {} used {}({})\n"
        "You must discuss and reach consensus. You have full access to all analysis tools during deliberation.\n"
        "Use leave_private_channel when consensus is reached.",
        channel,
        conflict.first_call.address,
        conflict.first_call.agent_id,
        conflict.first_call.tool_name,
        conflict.first_call.parameters.dump(),
        conflict.second_call.agent_id,
        conflict.second_call.tool_name,
        conflict.second_call.parameters.dump()
    );
    
    inject_user_message(notification);
    emit_log(LogLevel::INFO, "Force-joined conflict channel: " + channel);
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


void SwarmAgent::announce_presence() {
    // Send our task info so server can broadcast it
    if (irc_client_ && irc_connected_) {
        std::string task = swarm_config_.value("task", "analyzing binary");
        // Truncate if too long
        if (task.length() > 100) {
            task = task.substr(0, 100) + "...";
        }
        // Send task info as a simple message
        irc_client_->send_message("#agents", "MY_TASK: " + task);
    }
    SWARM_LOG("SwarmAgent: Announced task to swarm\n");
    emit_log(LogLevel::INFO, "Joined swarm");
}

void SwarmAgent::handle_peer_departure(const std::string& departed_agent_id) {
    auto it = known_peers_.find(departed_agent_id);
    if (it != known_peers_.end()) {
        std::string task = it->second.task;  // Save task before erasing
        known_peers_.erase(it);
        emit_log(LogLevel::INFO, std::format("Agent {} departed", departed_agent_id));
        
        // Notify the LLM agent about the departure
        inject_user_message(std::format(
            "[Swarm Update] Agent {} has left the swarm (was working on: {})",
            departed_agent_id, task
        ));
    }
}


} // namespace llm_re::agent