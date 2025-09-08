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
    
    // Connect to IRC server
    bool connect_to_irc();

    // Save swarm-specific state
    void save_swarm_state();
    
    // Simplified conflict handling
    void check_if_all_participating_agents_agreed();
    void process_grader_result(const std::string& result_json);
    std::string generate_conflict_channel(const ToolConflict& conflict) const;
    
    // Message adapters
    std::unique_ptr<ConsoleAdapter> console_adapter_;
    std::unique_ptr<IRCAdapter> irc_adapter_;
};

} // namespace llm_re::agent