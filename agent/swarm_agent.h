#pragma once

#include "agent.h"
#include "conflict_detector.h"
#include "message_adapter.h"
#include "../irc/irc_client.h"
#include "../orchestrator/tool_call_tracker.h"
#include <memory>

namespace llm_re::agent {

// Import ToolConflict from orchestrator namespace
using orchestrator::ToolConflict;

// Agent peer information
struct AgentPeerInfo {
    std::string agent_id;
    std::string task;
    std::chrono::steady_clock::time_point discovered_at;
};

// Extended agent for swarm operation
class SwarmAgent : public Agent {
public:
    SwarmAgent(const Config& config, const std::string& agent_id);
    virtual ~SwarmAgent();
    
    // Initialize with swarm configuration
    bool initialize(const json& swarm_config);
    
    // Start working with the orchestrator's prompt
    void start_task(const std::string& orchestrator_prompt);
    
    // Shutdown agent
    void shutdown();
    
    // Override trigger_shutdown for IRC cleanup  
    void trigger_shutdown() override;
    
    // Get agent ID
    std::string get_agent_id() const { return agent_id_; }
    
    // Get known peers (for ListActiveAgents tool)
    std::map<std::string, AgentPeerInfo> get_known_peers() const { return known_peers_; }
    
    // IRC message handlers
    void handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message);
    void handle_conflict_notification(const ToolConflict& conflict);
    
    // Send message to IRC
    void send_irc_message(const std::string& channel, const std::string& message);
    
    // Start private conversation with another agent
    void start_private_conversation(const std::string& target_agent, const std::string& initial_message);
    
    // Leave current private channel
    std::string leave_private_channel();
    
    // Force join a conflict channel (called by conflict detector)
    void force_join_conflict_channel(const std::string& channel, const ToolConflict& conflict);
    
    // Join IRC channel for discussion
    void join_irc_channel(const std::string& channel);
    
protected:
    // Override tool processing to add conflict detection for ALL tools
    std::vector<claude::messages::Message> process_tool_calls(const claude::messages::Message& msg, int iteration) override;
    
    // Override tool execution to add conflict detection
    virtual json execute_tool_with_conflict_check(const std::string& tool_name, const json& input);
    
    // Handle conflict through IRC deliberation
    void initiate_conflict_deliberation(const ToolConflict& conflict, const std::string& channel);
    
private:
    std::string agent_id_;
    std::string binary_name_;
    json swarm_config_;
    std::unique_ptr<ConflictDetector> conflict_detector_;
    std::unique_ptr<irc::IRCClient> irc_client_;
    
    // IRC connection info
    std::string irc_server_;
    int irc_port_;
    bool irc_connected_ = false;
    
    // Dynamic peer tracking
    std::map<std::string, AgentPeerInfo> known_peers_;
    
    // Conflict handling state
    std::map<std::string, ToolConflict> active_conflicts_;  // channel -> conflict
    
    // Private conversation tracking
    std::string current_private_channel_;  // Current private/conflict channel we're in
    
    // Connect to IRC server
    bool connect_to_irc();
    
    // Register additional swarm tools
    void register_swarm_tools();

    // Agent discovery methods
    void announce_presence();
    void request_peer_list();
    void handle_peer_departure(const std::string& agent_id);
    
    // Message adapters
    std::unique_ptr<ConsoleAdapter> console_adapter_;
    std::unique_ptr<IRCAdapter> irc_adapter_;
};

} // namespace llm_re::agent