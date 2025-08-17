#pragma once

#include "../core/common.h"
#include "../core/config.h"
#include "../sdk/claude_sdk.h"
#include "../agent/tool_system.h"
#include "database_manager.h"
#include "agent_spawner.h"
#include "tool_call_tracker.h"
#include "merge_manager.h"
#include "../irc/irc_server.h"
#include "../irc/irc_client.h"
#include <memory>
#include <vector>
#include <map>

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
    Orchestrator(const Config& config, const std::string& main_db_path);
    ~Orchestrator();
    
    // Initialize all subsystems
    bool initialize();
    
    // Start interactive session with user
    void start_interactive_session();
    
    // Shutdown all agents and cleanup
    void shutdown();
    
    // Tool implementations for the orchestrator
    json spawn_agent_async(const std::string& task, const std::string& context);
    json merge_database(const std::string& agent_id);
    
    // Helper to get agent result
    std::string get_agent_result(const std::string& agent_id) const;
    
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
    
    // Token tracking
    claude::usage::TokenStats token_stats_;

    // Generate prompt for agent
    std::string generate_agent_prompt(const std::string& task, const std::string& context);
    
    // Process user input
    void process_user_input(const std::string& input);
    
    // Send request to Claude API
    claude::ChatResponse send_orchestrator_request(const std::string& user_input);
    
    // Execute orchestrator tool calls
    std::vector<claude::messages::Message> process_orchestrator_tools(const claude::messages::Message& msg);

    // Wait for specific agents to complete
    void wait_for_agents_completion(const std::vector<std::string>& agent_ids);
    
    // Handle IRC messages (for agent results)
    void handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message);

    // System prompts
    static constexpr const char* ORCHESTRATOR_SYSTEM_PROMPT = R"(You are the Orchestrator for a multi-agent reverse engineering system. You are the ONLY entity that communicates with the user.

CRITICAL RESPONSIBILITIES:
1. You MUST think EXTREMELY deeply before taking any action
2. You decompose complex reverse engineering tasks into agent subtasks
3. You spawn specialized agents to work on isolated database copies
4. You manage the swarm of agents and their interactions
5. You merge agent findings back into the main database
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
- spawn_agent: Create a new agent with a specific task
- merge_database: Integrate an agent's findings into the main database

IMPORTANT: You cannot directly interact with the binary. All binary analysis must be done through agents.

Think deeply. Plan carefully. Orchestrate intelligently.)";
    
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