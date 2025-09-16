#pragma once

#include "../core/common.h"
#include "../core/config.h"
#include "../sdk/claude_sdk.h"
#include "../sdk/auth/oauth_manager.h"
#include "../agent/tool_system.h"
#include "../agent/event_bus.h"
#include "database_manager.h"
#include "agent_spawner.h"
#include "tool_call_tracker.h"
#include "merge_manager.h"
#include "../irc/irc_server.h"
#include "../irc/irc_client.h"
#include <memory>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace llm_re::orchestrator {

// Information about a spawned agent
struct AgentInfo {
    std::string agent_id;
    std::string task;  // The task assigned by the orchestrator
    std::string database_path;
    int process_id;
};

// The Orchestrator - The ONLY entity that talks to the user
class Orchestrator {
public:
    Orchestrator(const Config& config, const std::string& main_db_path, bool show_ui = true);
    ~Orchestrator();

    // Initialize all subsystems
    bool initialize();

    // Initialize for MCP mode (no UI)
    bool initialize_mcp_mode(const std::string& session_id,
                           const std::string& input_pipe_path,
                           const std::string& output_pipe_path);

    // Start interactive session with user
    void start_interactive_session();

    // Start MCP IPC listener
    void start_mcp_listener();

    // Shutdown all agents and cleanup
    void shutdown();

    // Tool implementations for the orchestrator
    json spawn_agent_async(const std::string& task, const std::string& context);
    json merge_database(const std::string& agent_id);

    // Helper to get agent result
    std::string get_agent_result(const std::string& agent_id) const;

    // Process user input (public for UI access)
    void process_user_input(const std::string& input);

    // Process MCP request and return response
    json process_mcp_request(const json& request);

    // Clear conversation and start fresh
    void clear_conversation();

    // Check if conversation is active
    bool is_conversation_active() const { return conversation_active_; }

    // Get list of active IRC channels
    std::vector<std::string> get_irc_channels() const;

    // Get last response text (for MCP mode)
    std::string get_last_response() const { return last_response_text_; }

private:
    // Core components
    const Config& config_;
    std::string main_database_path_;
    std::string binary_name_;
    std::unique_ptr<DatabaseManager> db_manager_;
    std::unique_ptr<AgentSpawner> agent_spawner_;
    std::unique_ptr<ToolCallTracker> tool_tracker_;
    std::unique_ptr<MergeManager> merge_manager_;
    std::unique_ptr<irc::IRCServer> irc_server_;
    std::unique_ptr<irc::IRCClient> irc_client_;
    
    // Claude API for orchestrator
    std::unique_ptr<claude::Client> api_client_;
    std::shared_ptr<claude::auth::OAuthManager> oauth_manager_;
    claude::tools::ToolRegistry tool_registry_;
    
    // Agent management
    std::map<std::string, AgentInfo> agents_;
    std::set<std::string> completed_agents_;  // Track agents that have sent results
    std::map<std::string, std::string> agent_results_;  // Store agent results
    int next_agent_id_ = 1;
    
    // State
    bool initialized_ = false;
    bool shutting_down_ = false;
    std::string current_user_task_;
    int allocated_irc_port_ = 0;  // Dynamically allocated port for this orchestrator
    
    // Token tracking
    claude::usage::TokenStats token_stats_;
    
    // Context management for orchestrator
    std::vector<claude::messages::Message> conversation_history_;
    bool conversation_active_ = false;  // Track if we're in an ongoing conversation
    struct ConsolidationState {
        bool consolidation_in_progress = false;
        int consolidation_count = 0;
        std::chrono::steady_clock::time_point last_consolidation;
    } consolidation_state_;
    
    // Event bus for UI communication
    EventBus& event_bus_ = get_event_bus();
    
    // Conflict resolution tracking
    struct ConflictSession {
        std::string channel;
        std::set<std::string> participating_agents;
        std::map<std::string, std::string> agreements;  // agent_id -> agreement text (deprecated)
        std::map<std::string, std::string> consensus_statements;  // agent_id -> consensus text
        ToolConflict original_conflict;  // Store the original conflict for context
        bool grader_invoked = false;  // deprecated
        bool resolved = false;
        std::chrono::steady_clock::time_point started;
    };
    std::map<std::string, ConflictSession> active_conflicts_;  // channel -> session
    std::mutex conflicts_mutex_;
    
    // Manual tool execution tracking
    std::map<std::string, bool> manual_tool_responses_;  // agent_id -> responded
    std::mutex manual_tool_mutex_;

    // MCP mode support
    bool show_ui_ = true;
    std::string mcp_session_id_;
    std::string last_response_text_;
    int mcp_input_fd_ = -1;   // Read requests from MCP server
    int mcp_output_fd_ = -1;  // Send responses to MCP server
    std::thread mcp_listener_thread_;
    std::atomic<bool> mcp_listener_should_stop_{false};

    // Task completion signaling for MCP mode
    std::mutex task_completion_mutex_;
    std::condition_variable task_completion_cv_;
    bool task_completed_ = false;

    void signal_task_completion();
    void wait_for_task_completion();
    void reset_task_completion();

    // Generate prompt for agent
    std::string generate_agent_prompt(const std::string& task, const std::string& context);
    
    // Port allocation for IRC server
    int allocate_unique_port();
    
    // OAuth token management
    bool refresh_oauth_if_needed();
    
    // Token usage tracking (per-iteration for context, cumulative for totals)
    void log_token_usage(const claude::TokenUsage& per_iteration_usage, const claude::TokenUsage& cumulative_usage);
    
    // Send request to Claude API
    claude::ChatResponse send_orchestrator_request(const std::string& user_input);
    claude::ChatResponse send_continuation_request();
    
    // Execute orchestrator tool calls
    std::vector<claude::messages::Message> process_orchestrator_tools(const claude::messages::Message& msg);

    // Wait for specific agents to complete
    void wait_for_agents_completion(const std::vector<std::string>& agent_ids);
    
    // Handle IRC messages (for agent results)
    void handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message);
    
    // Conflict resolution management
    void handle_conflict_message(const std::string& channel, const std::string& sender, const std::string& message);

    // Consensus value extraction and enforcement
    json extract_consensus_tool_call(const std::string& channel, const ConflictSession& session);
    void enforce_consensus_tool_execution(const std::string& channel, const json& tool_call,
                                         const std::set<std::string>& agents);
    void handle_manual_tool_result(const std::string& channel, const std::string& sender, const std::string& message);
    bool verify_consensus_applied(const std::set<std::string>& agents, ea_t address);
    
    // Context consolidation for orchestrator
    bool should_consolidate_context() const;
    void consolidate_conversation_context();
    std::string create_orchestrator_consolidation_summary(const std::vector<claude::messages::Message>& conversation) const;

    // System prompts
    static constexpr const char* ORCHESTRATOR_SYSTEM_PROMPT = R"(You are the Orchestrator for a multi-agent reverse engineering system. You are the ONLY entity that communicates with the user.

CRITICAL RESPONSIBILITIES:
1. You MUST think EXTREMELY deeply before taking any action
2. You decompose complex reverse engineering tasks into agent subtasks
3. You spawn specialized agents to work on isolated database copies
4. You manage the swarm of agents and their interactions
5. Agent findings are automatically merged back into the main database when they complete
6. You synthesize agent work into coherent responses for the user

DEEP THINKING REQUIREMENTS:
Before spawning ANY agent, you MUST:
- Analyze the task thoroughly to understand what's really being asked
- Consider multiple approaches and strategies
- Think about what types of agents would be most effective
- Plan how agents should collaborate
- Anticipate potential conflicts and how to resolve them
- Consider the order of operations
- Think about dependencies between tasks

AGENT CAPABILITIES:
The agents you spawn are EXTRAORDINARILY capable. They can:
- Perform deep binary analysis with various strategies
- Communicate with each other over IRC
- Deliberate conflicts through discussion
- Ask each other questions
- Share findings and insights
- Work in ways you might not fully predict

When crafting prompts for agents, remember:
- Be specific about the goal but allow flexibility in approach
- Provide context about other agents if collaboration would help
- Agents can see what other agents are working on
- The quality of your prompt directly impacts agent effectiveness

TOOLS AVAILABLE:
- spawn_agent: Create a new agent with a specific task (their findings are automatically merged when they complete)
- write_file: Create implementation files and other file outputs

Before finishing your spawn_agent call, make sure you provided enough information to the agent.
The agent **only** knows what ever you tell them, this program *does not do* any additional handling.

If you want to spawn multiple agents in parallel, you have to respond with all your spawn_agent tool calls in one response.

IMPORTANT: You cannot directly interact with the binary. All binary analysis must be done through agents.

Think deeply. Plan carefully. Orchestrate intelligently.)";
    
    // Orchestrator consolidation prompts
    static constexpr const char* ORCHESTRATOR_CONSOLIDATION_PROMPT = R"(CONTEXT CONSOLIDATION REQUIRED:

Our orchestration conversation has grown too long and we need to consolidate it to continue effectively.

Please provide a comprehensive summary of our coordination session that includes:

1. **Original User Task**: What the user asked us to investigate
2. **Agents Spawned**: List all agents created and their specific tasks
3. **Key Agent Findings**: Important discoveries from agent reports
4. **Coordination Decisions**: Major orchestration choices made
5. **Current Progress**: What has been completed vs what remains
6. **Active Context**: Any ongoing agent work or pending results

Make this summary comprehensive but concise - it will replace our entire conversation history.
Focus on preserving the essential context needed to continue coordinating agents effectively.)";

    static constexpr const char* DEEP_THINKING_PROMPT = R"(You need to think EXTREMELY deeply about this task. Consider:

1. What is the user really asking for?
2. What are the different components of this task?
3. What strategies could be employed?
4. What kinds of specialized agents would be most effective?
5. How should agents collaborate on this?
6. What order should tasks be done in?
7. What conflicts might arise?
8. What context does each agent need?
9. How can agents build on each other's work?
10. What's the optimal decomposition of this task?

Think step by step through the entire problem before deciding on your approach.)";
};

} // namespace llm_re::orchestrator