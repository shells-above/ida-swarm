#pragma once

#include "agent.h"
#include "conflict_detector.h"
#include "message_adapter.h"
#include "../irc/irc_client.h"
#include "../orchestrator/tool_call_tracker.h"
#include <memory>
#include <set>
#include <atomic>

namespace llm_re::agent {

// Import ToolConflict from orchestrator namespace
using orchestrator::ToolConflict;

// Agent peer information
struct AgentPeerInfo {
    std::string agent_id;
    std::string task;
    std::chrono::steady_clock::time_point discovered_at;
};

// Simple conflict state for tracking discussions
struct SimpleConflictState {
    std::string channel;                        // IRC channel for discussion

    // Turn-based discussion tracking
    bool my_turn = false;                       // Whether it's this agent's turn to speak
    bool consensus_reached = false;             // Whether consensus has been reached (orchestrator confirmed)
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
    
    // Override graceful shutdown to save additional swarm state
    void request_graceful_shutdown() override;
    
    // Get known peers (for ListActiveAgents tool)
    std::map<std::string, AgentPeerInfo> get_known_peers() const { return known_peers_; }
    
    // IRC message handlers
    void handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message);
    void handle_conflict_notification(const ToolConflict& conflict);
    
    // Send message to IRC
    void send_irc_message(const std::string& channel, const std::string& message);
    
    // Join IRC channel for discussion
    void join_irc_channel(const std::string& channel);
    
    // Restore conversation history for resurrection
    void restore_conversation_history(const json& saved_conversation);
    
    // Inject a user message into the conversation
    void inject_user_message(const std::string& message) override {
        Agent::inject_user_message(message);
    }


    // Helper methods for MarkConsensusReachedTool and conflict management
    bool has_active_conflict() const { return !active_conflicts_.empty(); }

    // Get any active conflict channel (used by tools)
    std::string get_conflict_channel() const;

    std::string get_agent_id() const { return agent_id_; }

    // Get specific conflict by channel
    SimpleConflictState* get_conflict_by_channel(const std::string& channel);

    // Check if we have any conflict waiting for response
    bool has_waiting_conflict() const;

    // Get the channel of a conflict that's waiting for response
    std::string get_waiting_conflict_channel() const;

    // Remove completed/resolved conflicts from the map
    void remove_completed_conflicts();

    // Add a conflict state (for resurrection)
    void add_conflict_state(const std::string& channel, bool my_turn = false) {
        SimpleConflictState state;
        state.channel = channel;
        state.my_turn = my_turn;
        state.consensus_reached = false;
        active_conflicts_[channel] = state;
    }

protected:
    // Override tool processing to add conflict detection for ALL tools
    std::vector<claude::messages::Message> process_tool_calls(const claude::messages::Message& msg, int iteration) override;

private:
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
    std::map<std::string, SimpleConflictState> active_conflicts_;  // Channel -> conflict state mapping


    // Connect to IRC server
    bool connect_to_irc();

    // No-go zone and patch replication handlers
    void handle_no_go_zone_message(const std::string& message);
    void handle_patch_replication_message(const std::string& message);
    std::string generate_conflict_channel(const ToolConflict& conflict) const;
    
    // Manual tool execution support
    void handle_manual_tool_execution(const std::string& channel, const std::string& message);
    static bool parse_manual_tool_message(const std::string& message, std::string& target_agent,
                                   std::string& tool_name, json& parameters);
    void send_manual_tool_result(const std::string& channel, bool success, const json& result);
    
    // Message adapters
    std::unique_ptr<ConsoleAdapter> console_adapter_;
    std::unique_ptr<IRCAdapter> irc_adapter_;

    // No-go zones tracking
    std::vector<orchestrator::NoGoZone> collected_no_go_zones_;
};

} // namespace llm_re::agent