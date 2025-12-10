#pragma once

#include "../core/common.h"
#include "../core/config.h"
#include "../core/profiler.h"
#include "../core/profiling_manager.h"
#include "../core/profiler_adapter.h"
#include "../sdk/claude_sdk.h"
#include "../agent/tool_system.h"
#include "../agent/event_bus.h"
#include "../analysis/memory_tool.h"
#include "database_manager.h"
#include "agent_spawner.h"
#include "tool_call_tracker.h"
#include "merge_manager.h"
#include "nogo_zone_manager.h"
#include "auto_decompile_manager.h"
#include "lldb_manager.h"
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
    friend class AutoDecompileManager;  // Needs access to agents_ and completed_agents_ for health checks

public:
    Orchestrator(const Config& config, const std::string& main_db_path, bool show_ui = true);
    ~Orchestrator();

    // Initialize all subsystems
    bool initialize();

    // Initialize for MCP mode (no UI)
    bool initialize_mcp_mode(const std::string& session_id,
                           const std::string& session_dir);

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

    // Auto-decompile
    void start_auto_decompile();
    void stop_auto_decompile();

    // LLDB revalidation (called when configuration changes)
    void revalidate_lldb();

    // Check if conversation is active
    bool is_conversation_active() const { return conversation_active_; }

    // Get list of active IRC channels
    std::vector<std::string> get_irc_channels() const;

    // Get last response text (for MCP mode)
    std::string get_last_response() const { return last_response_text_; }

    // Get binary name
    const std::string& get_binary_name() const { return binary_name_; }

private:
    // Core components
    const Config& config_;
    std::string main_database_path_;
    std::string binary_name_;
    std::unique_ptr<DatabaseManager> db_manager_;
    std::unique_ptr<AgentSpawner> agent_spawner_;
    std::unique_ptr<ToolCallTracker> tool_tracker_;
    std::unique_ptr<MergeManager> merge_manager_;
    std::unique_ptr<NoGoZoneManager> nogo_zone_manager_;
    std::unique_ptr<AutoDecompileManager> auto_decompile_manager_;
    std::unique_ptr<LLDBSessionManager> lldb_manager_;
    std::unique_ptr<irc::IRCServer> irc_server_;
    std::unique_ptr<irc::IRCClient> irc_client_;
    
    // Claude API for orchestrator
    std::unique_ptr<claude::Client> api_client_;
    ProfilerAdapter profiler_adapter_;                            // Metrics adapter for profiling
    // Note: OAuth manager no longer needed - Client uses global pool
    claude::tools::ToolRegistry tool_registry_;
    std::unique_ptr<MemoryToolHandler> memory_handler_;  // Orchestrator's persistent memory
    
    // Agent management
    std::map<std::string, AgentInfo> agents_;
    std::set<std::string> completed_agents_;  // Track agents that have sent results
    std::map<std::string, std::string> agent_results_;  // Store agent results
    mutable std::mutex agent_state_mutex_;  // Protects completed_agents_ and agent_results_
    int next_agent_id_ = 1;
    
    // State
    bool initialized_ = false;
    bool shutting_down_ = false;
    std::string current_user_task_;
    int allocated_irc_port_ = 0;  // Dynamically allocated port for this orchestrator
    bool lldb_validation_passed_ = false;  // LLDB connectivity validated at init

    // Token tracking
    claude::usage::TokenStats token_stats_;
    
    // Context management for orchestrator
    std::vector<claude::messages::Message> conversation_history_;
    bool conversation_active_ = false;  // Track if we're in an ongoing conversation
    
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

    // Conflict channel monitoring thread
    std::thread conflict_monitor_thread_;
    std::atomic<bool> conflict_monitor_should_stop_{false};

    // MCP mode support (pipe-based communication)
    bool show_ui_ = true;
    std::string mcp_session_id_;
    std::string last_response_text_;
    std::string mcp_session_dir_;        // Session directory path
    std::string mcp_request_pipe_;       // request.pipe (MCP → Orchestrator)
    std::string mcp_response_pipe_;      // response.pipe (Orchestrator → MCP)
    int mcp_request_fd_ = -1;            // File descriptor for reading requests
    int mcp_response_fd_ = -1;           // File descriptor for writing responses
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

    // Real-time tool call monitoring
    void handle_tool_call_event(const AgentEvent& event);
    void broadcast_no_go_zone(const NoGoZone& zone);
    void replicate_patch_to_agents(const std::string& source_agent, const ToolCall& call);
    std::string event_bus_subscription_id_;
    
    // Token usage tracking (per-iteration for context, cumulative for totals)
    void log_token_usage(const claude::TokenUsage& per_iteration_usage, const claude::TokenUsage& cumulative_usage);
    
    // Send request to Claude API
    claude::ChatResponse send_orchestrator_request(const std::string& user_input);
    claude::ChatResponse send_continuation_request();
    claude::ChatResponse send_request_with_memory(claude::ChatRequestBuilder& builder);

    // Execute orchestrator tool calls
    std::vector<claude::messages::Message> process_orchestrator_tools(const claude::messages::Message& msg);

    // Wait for specific agents to complete
    void wait_for_agents_completion(const std::vector<std::string>& agent_ids);
    
    // Handle IRC messages (for agent results)
    void handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message);

    // Handle LLDB IRC messages (from agents requesting debugging)
    void handle_lldb_message(const std::string& channel, const std::string& sender, const std::string& message);

    // LLDB connectivity validation
    bool validate_lldb_connectivity();

    // Consensus value extraction and enforcement
    json extract_consensus_tool_call(const ConflictSession& session);
    void enforce_consensus_tool_execution(const std::string& channel, const json& tool_call, const std::set<std::string>& agents);
    void handle_manual_tool_result(const std::string& message);
    bool verify_consensus_applied(const std::set<std::string>& agents, ea_t address);

    // Agent workspace cleanup
    // Deletes agent's workspace directory if the agent performed no write operations
    // Returns true if directory was deleted, false if it had writes or deletion failed
    bool cleanup_agent_directory_if_no_writes(const std::string& agent_id);

    // System prompts
    /*
    - Plan how agents should collaborate

    - Communicate with each other over IRC
    - Ask each other questions
    - Share findings and insights

    - Provide context about other agents if collaboration would help
    - Agents can see what other agents are working on
     */
    static constexpr const char* ORCHESTRATOR_SYSTEM_PROMPT = R"(You are the Orchestrator for a multi-agent reverse engineering system. You are the ONLY entity that communicates with the user.

You are a program running that is capable of spawning agents (spawning IDA Pro reverse engineering agents).

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
- Anticipate potential conflicts and how to resolve them
- Consider the order of operations
- Think about dependencies between tasks

AGENT CAPABILITIES:
The agents you spawn are EXTRAORDINARILY capable. They can:
- Perform deep binary analysis with various strategies
- Deliberate conflicts through discussion
- Work in ways you might not fully predict

If you want to patch the binary, use the agents to do so! Do not create separate patcher scripts using write_file unless the user wants you to do so

When crafting prompts for agents, remember:
- Be specific about the goal but allow flexibility in approach
- The quality of your prompt directly impacts agent effectiveness

TOOLS AVAILABLE:
- spawn_agent: Create a new agent with a specific task (their findings and IDA database updates are automatically merged when they complete)
- write_file: Create implementation files and other file outputs

Before finishing your spawn_agent call, make sure you provided enough information to the agent.
The agent **only** knows what ever you tell them, this program *does not do* any additional handling.

If you want to spawn multiple agents in parallel, you have to respond with all your spawn_agent tool calls in one response.

NOTE: If you spawn multiple agents in ONE RESPONSE, they *will RUN IN PARALLEL*!
If you want to spawn one agent, and then spawn another to verify that agents work you must spawn the first agent, and then once you see its results spawn the next agent.
If you want to have agents run in parallel, you must use multiple spawn_agent calls in one response before ending your turn.

Remember: Do NOT get confused, YOUR AGENTS CAN NOT analyze multiple binaries. You are ONLY analyzing the ONE binary listed by "Current binary being analyzed" below. The agents are analyzing that binary, YOU DO NOT HAVE THE ABILITY TO INTERACT WITH OTHER BINARIES.
You are NOT capable of reverse engineering other binaries. If you find that your investigation can't be completed because the current binary doesn't have the information you need (you can NOT just go and claim this, you must have spent significant time having agents exploring the binary before you can even THINK you can claim this. If you claim this and are wrong, you WILL screw up the users reverse engineering FOREVER because YOU AREN'T SUPPOSED TO MAKE MISTAKES, and the user will then spend time reverse engineering something else) then you can tell the user this, but ONLY if you are 100% sure.

IMPORTANT: You cannot directly interact with the binary. All binary analysis must be done through agents.

Think deeply. Plan carefully. Orchestrate intelligently.)";  // "this program *does not do* any additional handling." is not true, it provides past agent results which i will get rid of eventually once i add back in agent collaboration and get it working better

    /*
     *
     *5. How should agents collaborate on this?
     *
     */
    static constexpr const char* DEEP_THINKING_PROMPT = R"(You need to think EXTREMELY deeply about this task. Consider:

1. What is the user really asking for?
2. What are the different components of this task?
3. What strategies could be employed?
4. What kinds of specialized agents would be most effective?
5. What order should tasks be done in?
6. What conflicts might arise?
7. What context does each agent need?
8. How can agents build on each other's work?
9. What's the optimal decomposition of this task?

Think step by step through the entire problem before deciding on your approach.)";
};

} // namespace llm_re::orchestrator