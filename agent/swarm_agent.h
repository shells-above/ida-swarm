#pragma once

#include "agent.h"
#include "conflict_detector.h"
#include "message_adapter.h"
#include "../irc/irc_client.h"
#include "../orchestrator/tool_call_tracker.h"
#include <memory>
#include <set>

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
    ToolConflict initial_conflict;              // The original conflict that started this
    std::string channel;                        // IRC channel for discussion
    std::set<std::string> participating_agents; // Dynamically updated as agents join
    std::map<std::string, std::string> agreements; // agent_id -> what they said after "AGREE|"
    bool resolved = false;

    // Turn-based discussion tracking
    bool my_turn = false;                       // Whether it's this agent's turn to speak
    bool consensus_reached = false;              // Whether consensus has been reached
    std::string final_consensus;                // The final agreed-upon consensus text
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

    // Set whether agent is waiting for conflict details before starting
    void set_waiting_for_conflict_details(bool wait) {
        waiting_for_conflict_details_ = wait;
    }

    // Helper methods for MarkConsensusReachedTool
    bool has_active_conflict() const { return active_conflict_.has_value(); }
    std::string get_conflict_channel() const {
        return active_conflict_ ? active_conflict_->channel : "";
    }
    std::string get_agent_id() const { return agent_id_; }
    void mark_local_consensus(const std::string& consensus) {
        if (active_conflict_) {
            active_conflict_->consensus_reached = true;
            active_conflict_->final_consensus = consensus;
        }
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
    std::optional<SimpleConflictState> active_conflict_;  // Current active conflict (if any)

    // Flag for delayed task start when waiting for conflict details
    bool waiting_for_conflict_details_ = false;

    // Connect to IRC server
    bool connect_to_irc();

    // Save swarm-specific state
    void save_swarm_state();
    
    // Simplified conflict handling
    void check_if_all_participating_agents_agreed();
    void process_grader_result(const std::string& result_json);
    std::string generate_conflict_channel(const ToolConflict& conflict) const;
    
    // Manual tool execution support
    void handle_manual_tool_execution(const std::string& channel, const std::string& message);
    static bool parse_manual_tool_message(const std::string& message, std::string& target_agent,
                                   std::string& tool_name, json& parameters);
    void send_manual_tool_result(const std::string& channel, bool success, const json& result);
    
    // Message adapters
    std::unique_ptr<ConsoleAdapter> console_adapter_;
    std::unique_ptr<IRCAdapter> irc_adapter_;
};

} // namespace llm_re::agent