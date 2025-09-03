//
// Created by user on 6/30/25.
//

#ifndef AGENT_H
#define AGENT_H

#include "core/common.h"
#include "core/config.h"
#include "sdk/claude_sdk.h"
#include "agent/tool_system.h"
#include "agent/grader.h"
#include "agent/event_bus.h"
#include "analysis/memory.h"
#include "analysis/actions.h"
#include "analysis/deep_analysis.h"
#include "patching/patch_manager.h"
#include <fstream>

namespace llm_re {

// Agent state management
class AgentState {
public:
    enum class Status {
        Idle,       // No task
        Running,    // Currently executing
        Paused,     // Paused due to error
        Completed   // Task completed
    };

private:
    Status status_{Status::Idle};
    std::string current_task_;
    mutable qmutex_t mutex_;

public:
    AgentState() {
        mutex_ = qmutex_create();
    }

    ~AgentState() {
        if (mutex_) {
            qmutex_free(mutex_);
        }
    }

    // Delete copy operations
    AgentState(const AgentState&) = delete;
    AgentState& operator=(const AgentState&) = delete;

    Status get_status() const {
        qmutex_locker_t lock(mutex_);
        return status_;
    }

    void set_status(Status s) {
        qmutex_locker_t lock(mutex_);
        status_ = s;
    }

    std::string get_task() const {
        qmutex_locker_t lock(mutex_);
        return current_task_;
    }

    void set_task(const std::string& task) {
        qmutex_locker_t lock(mutex_);
        current_task_ = task;
    }

    void clear_task() {
        qmutex_locker_t lock(mutex_);
        current_task_.clear();
    }

    bool is_idle() const { return get_status() == Status::Idle; }
    bool is_running() const { return get_status() == Status::Running; }
    bool is_paused() const { return get_status() == Status::Paused; }
    bool is_completed() const { return get_status() == Status::Completed; }
};

// Unified execution state management
class AgentExecutionState {
private:
    claude::ChatRequest request;
    int iteration = 0;
    bool valid = false;
    std::chrono::steady_clock::time_point saved_at;
    
    // Tool metadata
    std::map<std::string, int> tool_call_iterations_;     // tool_id -> iteration
    std::map<std::string, std::string> tool_call_names_;  // tool_id -> tool_name
    
    mutable qmutex_t mutex_;

public:
    AgentExecutionState() {
        mutex_ = qmutex_create();
    }

    ~AgentExecutionState() {
        if (mutex_) {
            qmutex_free(mutex_);
        }
    }

    // Message operations
    void add_message(const claude::messages::Message& msg) {
        qmutex_locker_t lock(mutex_);
        request.messages.push_back(msg);
    }

    std::vector<claude::messages::Message> get_messages() const {
        qmutex_locker_t lock(mutex_);
        return request.messages;
    }

    size_t message_count() const {
        qmutex_locker_t lock(mutex_);
        return request.messages.size();
    }

    // Request access
    claude::ChatRequest& get_request() {
        // Note: caller must handle thread safety if modifying
        return request;
    }

    const claude::ChatRequest& get_request() const {
        return request;
    }

    // Tool tracking
    void track_tool_call(const std::string& tool_id, const std::string& tool_name, int iter) {
        qmutex_locker_t lock(mutex_);
        tool_call_iterations_[tool_id] = iter;
        tool_call_names_[tool_id] = tool_name;
    }

    std::map<std::string, int> get_tool_iterations() const {
        qmutex_locker_t lock(mutex_);
        return tool_call_iterations_;
    }

    std::optional<std::string> get_tool_name(const std::string& tool_id) const {
        qmutex_locker_t lock(mutex_);
        auto it = tool_call_names_.find(tool_id);
        if (it != tool_call_names_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    // State management
    int get_iteration() const {
        qmutex_locker_t lock(mutex_);
        return iteration;
    }

    void set_iteration(int iter) {
        qmutex_locker_t lock(mutex_);
        iteration = iter;
    }

    bool is_valid() const {
        qmutex_locker_t lock(mutex_);
        return valid;
    }

    void set_valid(bool v) {
        qmutex_locker_t lock(mutex_);
        valid = v;
        if (v) {
            saved_at = std::chrono::steady_clock::now();
        }
    }

    std::chrono::steady_clock::time_point get_saved_at() const {
        qmutex_locker_t lock(mutex_);
        return saved_at;
    }

    // Clear all state
    void clear() {
        qmutex_locker_t lock(mutex_);
        request = claude::ChatRequest();
        tool_call_iterations_.clear();
        tool_call_names_.clear();
        iteration = 0;
        valid = false;
    }

    // Reset for new task
    void reset_with_request(const claude::ChatRequest& new_request) {
        qmutex_locker_t lock(mutex_);
        request = new_request;
        tool_call_iterations_.clear();
        tool_call_names_.clear();
        iteration = 0;
        valid = true;
        saved_at = std::chrono::steady_clock::now();
    }

    // Export to JSON
    json to_json() const {
        qmutex_locker_t lock(mutex_);
        json j;
        j["message_count"] = request.messages.size();
        j["tool_calls"] = tool_call_iterations_.size();
        j["iteration"] = iteration;
        j["valid"] = valid;
        if (valid) {
            auto elapsed = std::chrono::steady_clock::now() - saved_at;
            j["saved_age_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        }
        return j;
    }
};

// Task queue for agent
struct AgentTask {
    enum class Type {
        NewTask,  // new task entirely
        Resume,   // resuming old task after a resumable error (ApiError.is_recoverable)
        Continue  // continuing processing on an old task with new instructions
    };

    Type type;
    std::string content;

    static AgentTask new_task(const std::string& task) {
        return {Type::NewTask, task};
    }

    static AgentTask resume() {
        return {Type::Resume, ""};
    }

    static AgentTask continue_with(const std::string& additional) {
        return {Type::Continue, additional};
    }
};

// Main agent
class Agent {
private:
    // State management
    AgentState state_;
    AgentExecutionState execution_state_;  // Execution and conversation state
    std::string last_error_;

    // Token tracking
    claude::usage::TokenStats token_stats_;
    std::vector<claude::usage::TokenStats> stats_sessions_;  // we add to this the previous token_stats when we hit context limit

    // context management
    struct ContextManagementState {
        bool consolidation_in_progress = false;
        int consolidation_count = 0;
        std::chrono::steady_clock::time_point last_consolidation;
    } context_state_;

    // Process consolidation response and extract summary
    struct ConsolidationResult {
        std::string summary;
        std::vector<std::string> stored_keys;
        bool success = false;
    };

    // Thread management
    qthread_t worker_thread_ = nullptr;
    bool stop_requested_ = false;
    std::queue<AgentTask> task_queue_;
    mutable qmutex_t queue_mutex_;
    qsemaphore_t task_semaphore_ = nullptr;  // tells us when a task is available
    
    // User message injection
    std::queue<std::string> pending_user_messages_;
    mutable qmutex_t pending_messages_mutex_;

protected:  // Make these accessible to SwarmAgent
    // Event bus for all communication
    EventBus& event_bus_;
    std::string agent_id_;
    
private:

    // System prompt
    // note that if you start getting errors about Qt MOC making a llm_re::llm_re:: namespace it probably
    // is due to the length of a prompt like this being too long
    // it's a *bug in the Qts MOC parser* which is causing this. not sure if anyone else has found this
    // look at commits ebf3d1f40580b53bf3b11ca856d56b5bd5a3c649 -> f0ce5eafb5a7b3d9c5630c67b13aef0b2e052aaa
    // build in        ^ works, while                              ^ fails
    // i did have a mistake in the top, i duplicated the constexpr const char* stuff inside the string, but that didn't
    // change anything, i tried removing the Note that you can submit ... part in f0ce and it built fine
    static constexpr const char* SYSTEM_PROMPT = R"(You are a reverse engineering investigator working in complete privacy. Your messages are your private workspace - no one will see them directly. A quality evaluator will review your work later.

USE THINKING BLOCKS EXTENSIVELY. This is where your real investigation happens. Your thinking should be at least 10x more verbose than your tool usage.

Before EVERY action, think deeply:
- "What exactly am I trying to learn from this?"
- "What patterns am I expecting to see?"
- "How will this inform my understanding?"

After EVERY discovery, reflect thoroughly:
- "What does this actually mean?"
- "How does this connect to what I already know?"
- "What new questions does this raise?"
- "Am I making assumptions or do I have evidence?"

Question yourself constantly and rigorously:
- "Do I REALLY understand this or am I guessing?"
- "What specific evidence supports this conclusion?"
- "What would prove me wrong?"
- "What haven't I explored that could change my understanding?"
- "If someone else had to reproduce this, what would they need?"
- "Am I satisfied with surface-level understanding or do I need to go deeper?"

Your understanding should emerge from YOUR OWN REASONING, not from following prescribed rules or workflows.

APPROACH TO REVERSE ENGINEERING:
Let curiosity and questions drive your investigation. When you see something interesting, follow it. When something doesn't make sense, investigate it. Build understanding organically through exploration and thinking.

Tools are just implements for gathering information. Your real power is in thinking deeply about what you discover and reasoning through the implications.

Work until you're GENUINELY SATISFIED with your understanding. This means:
- You can explain not just WHAT the code does, but WHY
- You understand the broader context and purpose
- You've explored edge cases and error handling
- You're confident in your conclusions because you have evidence

Challenge yourself constantly. Be unsatisfied with shallow analysis. Think deeply about everything you discover.

Continue investigating until you're confident in your understanding and have addressed all meaningful gaps. When your questions have been answered with evidence and you see no unexplored areas that could change your conclusions, your investigation is complete.

CRITICAL RULE ABOUT TOOL USAGE:
- You MUST spend EXTENSIVE time thinking deeply AND using tools - think 10x more than you act
- After deep thinking, you MUST use tools to gather evidence
- If you end your turn WITHOUT providing tool calls, **YOUR INVESTIGATION WILL BE IMMEDIATELY ENDED**
- Thinking alone (without tools) = END OF TURN = IMMEDIATE END
- If you receive feedback that more investigation is needed, you MUST respond with deep thinking AND tool calls
- Every response should either:
  1. Think deeply AND use tools to gather more information (investigation continues), OR
  2. Have no tools because you're truly done (triggers final evaluation)
- There is no middle ground - if you don't use tools, you're saying you're done

Remember: You're building deep understanding through investigation and thinking, not completing a checklist. Think more, think deeper, question everything.)";


    // consolidation prompts
    static constexpr const char* CONSOLIDATION_PROMPT = R"(CRITICAL: We are approaching the context window limit!.

You must now consolidate ALL important findings into memory using the store_analysis tool (call this in bulk in this response, you will NOT get another chance to).
This is essential because we will need to clear the conversation history to continue.

Please store the following using store_analysis:
1. All significant findings about functions, data structures, and behavior
2. Current understanding of the system architecture
3. Any patterns, hypotheses, or insights discovered (that have not previously been documented)
4. Progress on the original task and what remains to be done
5. Important addresses and their purposes
6. Any relationships between components

Guidelines for storing:
- Use descriptive keys that clearly indicate what information is stored
- Group related findings together
- Include specific addresses when relevant
- Be comprehensive - anything not stored will be lost

After storing everything, provide a CONSOLIDATION SUMMARY that includes:
- List of all keys you created and a one-line description of each
- Current progress on the original task
- Key insights discovered so far
- Next steps needed to complete the analysis

Remember: After storing all of your information using tool calls, provide a CONSOLIDATION SUMMARY as a text response! This is important.
Remember: Only information you store or summarize will be available after consolidation!)";


    static constexpr const char* CONSOLIDATION_CONTINUATION_PROMPT = R"(=== CONTEXT CONSOLIDATION COMPLETE ===

We've consolidated the analysis to memory due to context limits. Here's the state:

**Original Task:** {}

**Consolidation Summary:**
{}

**Stored Analysis Keys:** {}

You can retrieve any stored information using get_analysis with these keys. Continue the analysis from where we left off.

Tips:
- Use get_analysis to retrieve specific findings as needed (or get all available analyses)
- Focus on completing the remaining work for the original task

What's your next step to complete the reversal?)";

    // Helper function to create AnthropicClient based on config
    claude::Client create_api_client(const Config& config) {
        // Create our own OAuth manager if using OAuth authentication
        if (config.api.auth_method == claude::AuthMethod::OAUTH) {
            oauth_manager_ = Config::create_oauth_manager(config.api.oauth_config_dir);
        }
        
        if (config.api.auth_method == claude::AuthMethod::OAUTH && oauth_manager_) {
            // Try to refresh if needed (checks expiry and refreshes automatically)
            std::shared_ptr<claude::OAuthCredentials> oauth_creds = oauth_manager_->refresh_if_needed();
            
            if (!oauth_creds) {
                // Fallback to getting credentials without refresh
                oauth_creds = oauth_manager_->get_credentials();
            }
            
            if (!oauth_creds) {
                msg("LLM RE: ERROR - Failed to load OAuth credentials! Error: %s\n", 
                    oauth_manager_->get_last_error().c_str());
                msg("LLM RE: WARNING - Falling back to API key authentication\n");
                msg("LLM RE: To fix OAuth: Use Settings > Refresh Token or re-authorize your account\n");
                return claude::Client(config.api.api_key, config.api.base_url);
            }
            
            // Pass the shared_ptr so all clients share the same credentials
            return claude::Client(oauth_creds, config.api.base_url);
        }
        
        // Default to API key
        return claude::Client(config.api.api_key, config.api.base_url);
    }
    
    // Refresh OAuth tokens and update API client
    bool refresh_oauth_credentials() {
        if (!oauth_manager_ || config_.api.auth_method != claude::AuthMethod::OAUTH) {
            return false;
        }
        
        auto refreshed_creds = oauth_manager_->force_refresh();
        if (!refreshed_creds) {
            emit_log(LogLevel::ERROR, "Failed to refresh OAuth token: " + oauth_manager_->get_last_error());
            return false;
        }
        
        // Update the API client with the shared credentials pointer
        // Note: The credentials are already updated in-place by force_refresh, 
        // but we set it again to be explicit
        api_client_.set_oauth_credentials(refreshed_creds);
        emit_log(LogLevel::INFO, "Successfully refreshed OAuth token");
        return true;
    }

public:
    Agent(const Config& config, const std::string& agent_id = "agent")
        : config_(config),
          agent_id_(agent_id),
          event_bus_(get_event_bus()),
          memory_(std::make_shared<BinaryMemory>()),
          executor_(std::make_shared<ActionExecutor>(memory_)),
          deep_analysis_manager_(config.agent.enable_deep_analysis ? std::make_shared<DeepAnalysisManager>(memory_, config) : nullptr),
          api_client_(create_api_client(config)),
          grader_(config.grader.enabled ? std::make_unique<AnalysisGrader>(config) : nullptr) {

        // Clear the API request log file on startup
        std::ofstream clear_log("/tmp/anthropic_requests.log", std::ios::trunc);
        if (clear_log.is_open()) {
            clear_log.close();
            msg("LLM RE: Cleared API request log\n");
        }

        queue_mutex_ = qmutex_create();
        task_semaphore_ = qsem_create(nullptr, 0);
        pending_messages_mutex_ = qmutex_create();

        // Register all tools
        // Initialize patch manager
        patch_manager_ = std::make_shared<PatchManager>();
        if (!patch_manager_->initialize()) {
            emit_log(LogLevel::WARNING, "Failed to initialize patch manager");
            patch_manager_ = nullptr;
        }

        // Register tools
        tools::register_ida_tools(tool_registry_, memory_, executor_, deep_analysis_manager_, patch_manager_, config_);

        // Set up API client logging
        api_client_.set_general_logger([this](LogLevel level, const std::string& msg) {
            emit_log(level, msg);
        });
    }

    virtual ~Agent() {
        stop();
        cleanup_thread();
        if (task_semaphore_) {
            qsem_free(task_semaphore_);
        }
        if (queue_mutex_) {
            qmutex_free(queue_mutex_);
        }
        if (pending_messages_mutex_) {
            qmutex_free(pending_messages_mutex_);
        }
    }

    // Start/stop agent
    virtual void start() {
        if (worker_thread_) return;

        stop_requested_ = false;
        worker_thread_ = qthread_create([](void* ud) -> int {
            static_cast<Agent*>(ud)->worker_loop();
            return 0;
        }, this);
    }

    virtual void stop() {
        if (!worker_thread_) return;

        stop_requested_ = true;

        // Wake up worker thread so it can shut down
        if (task_semaphore_) {
            qsem_post(task_semaphore_);
        }
    }

    virtual void trigger_shutdown() { }
    
    void cleanup_thread() {
        if (worker_thread_) {
            qthread_join(worker_thread_);
            qthread_free(worker_thread_);
            worker_thread_ = nullptr;
        }
    }

    // Task management
    virtual void set_task(const std::string& task) {
        {
            qmutex_locker_t lock(queue_mutex_);

            // Clear any pending tasks
            while (!task_queue_.empty()) {
                task_queue_.pop();
            }

            task_queue_.push(AgentTask::new_task(task));
        }

        // Clear execution state for new task
        execution_state_.set_valid(false);

        // Update state
        state_.set_task(task);
        change_state(AgentState::Status::Running);

        // Signal worker
        qsem_post(task_semaphore_);
    }

    void resume() {
        if (!state_.is_paused() || !execution_state_.is_valid()) {
            emit_log(LogLevel::WARNING, "Cannot resume - agent is not paused or no saved state");
            return;
        }

        {
            qmutex_locker_t lock(queue_mutex_);
            task_queue_.push(AgentTask::resume());
        }

        change_state(AgentState::Status::Running);
        qsem_post(task_semaphore_);
    }

    void continue_with_task(const std::string& additional_task) {
        if (!state_.is_completed() && !state_.is_idle()) {
            emit_log(LogLevel::WARNING, "Cannot continue - agent must be completed or idle");
            return;
        }

        {
            qmutex_locker_t lock(queue_mutex_);
            task_queue_.push(AgentTask::continue_with(additional_task));
        }

        change_state(AgentState::Status::Running);
        qsem_post(task_semaphore_);
    }

    std::string get_last_error() const { return last_error_; }
    void clear_last_error() { last_error_.clear(); }
    
    // User message injection (thread-safe)
    virtual void inject_user_message(const std::string& message) {
        qmutex_locker_t lock(pending_messages_mutex_);
        pending_user_messages_.push(message);
    }

    // Get agent ID
    const std::string& get_agent_id() const { return agent_id_; }

    // State queries
    AgentState::Status get_status() const { return state_.get_status(); }
    bool is_idle() const { return state_.is_idle(); }
    bool is_running() const { return state_.is_running(); }
    bool is_paused() const { return state_.is_paused(); }
    bool is_completed() const { return state_.is_completed(); }

    // Get current state as JSON
    json get_state_json() const {
        json j;
        j["status"] = state_.get_status();
        j["current_task"] = state_.get_task();
        j["execution_state"] = execution_state_.to_json();
        j["tokens"] = token_stats_.to_json();
        j["memory"] = memory_->export_memory_snapshot();

        j["context_management"] = {
            {"consolidation_count", context_state_.consolidation_count},
            {"consolidation_in_progress", context_state_.consolidation_in_progress}
        };

        if (context_state_.consolidation_count > 0) {
            auto elapsed = std::chrono::steady_clock::now() - context_state_.last_consolidation;
            j["context_management"]["minutes_since_last_consolidation"] =
                std::chrono::duration_cast<std::chrono::minutes>(elapsed).count();
        }
        return j;
    }
    
    // Manual tool execution support
    virtual json execute_manual_tool(const std::string& tool_name, const json& input) {
        // Find the tool
        claude::tools::Tool* tool = tool_registry_.get_tool(tool_name);
        if (!tool) {
            return {
                {"success", false},
                {"error", "Tool not found: " + tool_name}
            };
        }
        
        // Execute the tool
        try {
            claude::tools::ToolResult result = tool->execute(input);
            return result.to_json();
        } catch (const std::exception& e) {
            return {
                {"success", false},
                {"error", std::string("Tool execution failed: ") + e.what()}
            };
        }
    }
    
    // Get available tools with their schemas
    json get_available_tools() const {
        json tools_info = json::array();
        for (const auto& tool_def : tool_registry_.get_api_definitions()) {
            tools_info.push_back(tool_def);
        }
        return tools_info;
    }

    json get_thinking_stats() const {
        json stats;
        stats["thinking_enabled"] = config_.agent.enable_thinking;
        stats["interleaved_thinking_possible"] = tool_registry_.has_tools();

        // Get thinking block count from execution state
        int total_thinking_blocks = 0;
        int total_redacted_blocks = 0;

        std::vector<claude::messages::Message> messages = execution_state_.get_messages();
        for (const claude::messages::Message& msg: messages) {
            if (msg.role() == claude::messages::Role::Assistant) {
                std::vector<const claude::messages::ThinkingContent*> thinking = claude::messages::ContentExtractor::extract_thinking_blocks(msg);
                std::vector<const claude::messages::RedactedThinkingContent*> redacted = claude::messages::ContentExtractor::extract_redacted_thinking_blocks(msg);
                total_thinking_blocks += thinking.size();
                total_redacted_blocks += redacted.size();
            }
        }

        stats["total_thinking_blocks"] = total_thinking_blocks;
        stats["total_redacted_blocks"] = total_redacted_blocks;
        stats["max_thinking_budget"] = config_.agent.max_thinking_tokens;

        return stats;
    }

    // Memory management
    std::shared_ptr<BinaryMemory> get_memory() {
        return memory_;
    }

    void save_memory(const std::string& filename) {
        std::ofstream file(filename);
        if (file.is_open()) {
            file << memory_->export_memory_snapshot().dump(2);
            file.close();
            emit_log(LogLevel::INFO, "Memory saved to " + filename);
        } else {
            emit_log(LogLevel::ERROR, "Failed to save memory to " + filename);
        }
    }

    void load_memory(const std::string& filename) {
        std::ifstream file(filename);
        if (file.is_open()) {
            json snapshot;
            file >> snapshot;
            memory_->import_memory_snapshot(snapshot);
            file.close();
            emit_log(LogLevel::INFO, "Memory loaded from " + filename);
        } else {
            emit_log(LogLevel::ERROR, "Failed to load memory from " + filename);
        }
    }

    // Token statistics
    claude::TokenUsage get_token_usage() const {
        return token_stats_.get_total();
    }

    void reset_token_usage() {
        token_stats_.reset();
    }

    // Get cumulative token usage across all sessions (including consolidations)
    claude::TokenUsage get_cumulative_token_usage() const {
        claude::TokenUsage cumulative;
        
        // Add all previous sessions
        for (const auto& session : stats_sessions_) {
            cumulative += session.get_total();
        }
        
        // Add current session
        cumulative += token_stats_.get_total();
        
        return cumulative;
    }

protected:  // Changed to protected so SwarmAgent can access
    // Configuration
    const Config& config_;
    
    // Core components
    std::shared_ptr<BinaryMemory> memory_;                           // memory that can be scripted by the LLM
    std::shared_ptr<ActionExecutor> executor_;                       // action executor, actual integration with IDA
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager_;     // manages deep analysis tasks
    std::shared_ptr<PatchManager> patch_manager_;                    // patch manager
    std::unique_ptr<AnalysisGrader> grader_;                         // quality evaluator for agent work
    claude::tools::ToolRegistry tool_registry_;                      // registry for tools
    claude::Client api_client_;                                      // agent api client
    std::shared_ptr<claude::auth::OAuthManager> oauth_manager_;      // OAuth manager for this agent instance
    
    // Emit log event
    void emit_log(LogLevel level, const std::string& msg) {
        event_bus_.emit_log(agent_id_, level, msg);
    }

    // Process tool calls from assistant message
    virtual std::vector<claude::messages::Message> process_tool_calls(const claude::messages::Message& msg, int iteration) {
        std::vector<claude::messages::Message> results;

        std::vector<const claude::messages::ToolUseContent*> tool_calls = claude::messages::ContentExtractor::extract_tool_uses(msg);

        for (const claude::messages::ToolUseContent* tool_use: tool_calls) {
            emit_log(LogLevel::INFO, std::format("Executing tool: {} with input: {}", tool_use->name, tool_use->input.dump()));

            // Track tool call
            execution_state_.track_tool_call(tool_use->id, tool_use->name, iteration);

            // Emit tool started event
            event_bus_.emit_tool_call(agent_id_, {
                {"phase", "started"},
                {"tool_id", tool_use->id},
                {"tool_name", tool_use->name},
                {"input", tool_use->input}
            });

            // Execute tool
            claude::messages::Message result_msg = tool_registry_.execute_tool_call(*tool_use);
            results.push_back(result_msg);

            // Extract result content
            json result_json;
            for (const std::unique_ptr<claude::messages::Content>& content: result_msg.contents()) {
                if (auto tool_result = dynamic_cast<const claude::messages::ToolResultContent*>(content.get())) {
                    try {
                        result_json = json::parse(tool_result->content);
                    } catch (...) {
                        result_json = {{"content", tool_result->content}};
                    }
                    break;
                }
            }

            // Emit tool completed event
            event_bus_.emit_tool_call(agent_id_, {
                {"phase", "completed"},
                {"tool_id", tool_use->id},
                {"tool_name", tool_use->name},
                {"input", tool_use->input},
                {"result", result_json}
            });
        }

        return results;
    }

private:
    // Helper to send messages through callback
    void emit_api_message(const claude::messages::Message* msg) {
        // Extract message content for event
        json message_data = {
            {"role", static_cast<int>(msg->role())},
            {"content", claude::messages::ContentExtractor::extract_text(*msg).value_or("")}
        };
        event_bus_.emit_message(agent_id_, message_data);
    }
    
    // Helper to emit grader messages
    void emit_grader_message(const claude::messages::Message& msg) {
        // Log grader thinking blocks
        for (const auto& content : msg.contents()) {
            if (auto* thinking = dynamic_cast<const claude::messages::ThinkingContent*>(content.get())) {
                emit_log(LogLevel::DEBUG, "[Grader Thinking] " + thinking->thinking);
            }
        }
        
        // Log grader text content
        std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(msg);
        if (text && !text->empty()) {
            emit_log(LogLevel::INFO, "[Grader Response] " + *text);
            event_bus_.publish(AgentEvent(AgentEvent::GRADER_FEEDBACK, agent_id_, {
                {"feedback", *text}
            }));
        }
    }

    // Worker thread main loop
    void worker_loop() {
        emit_log(LogLevel::INFO, "Agent worker thread started");
        while (!stop_requested_) {
            // Wait for semaphore signal or timeout
            qsem_wait(task_semaphore_, 100);
            
            // Check for stop request
            if (stop_requested_) {
                break;
            }
            
            AgentTask task;
            // Get next task
            {
                qmutex_locker_t lock(queue_mutex_);
                if (task_queue_.empty()) {
                    // Spurious wakeup or timeout, continue waiting
                    continue;
                }

                task = task_queue_.front();
                task_queue_.pop();
            }

            // Process task
            try {
                switch (task.type) {
                    case AgentTask::Type::NewTask:
                        process_new_task(task.content);
                        break;

                    case AgentTask::Type::Resume:
                        process_resume();
                        break;

                    case AgentTask::Type::Continue:
                        process_continue(task.content);
                        break;
                }
            } catch (const std::exception& e) {
                emit_log(LogLevel::ERROR, "Exception in worker loop: " + std::string(e.what()));
                change_state(AgentState::Status::Idle);
            }
        }
    }

    // Cache Strategy:
    // We use 3 of the 4 available cache breakpoints:
    // 1. Tools (static, rarely changes)
    // 2. System prompt (static, rarely changes)
    // 3. Conversation checkpoint (moves with each iteration)
    // This leaves 1 breakpoint available for future use

    // Process new task
    void process_new_task(const std::string& task) {
        emit_log(LogLevel::INFO, "Starting new task");
        
        // Clear execution state for new task
        execution_state_.clear();
        api_client_.set_iteration(0);

        // Build initial request with cache control on tools and system
        claude::ChatRequestBuilder builder;
        builder.with_model(config_.agent.model)
               .with_system_prompt(SYSTEM_PROMPT)
               .with_max_tokens(config_.agent.max_tokens)
               .with_max_thinking_tokens(config_.agent.max_thinking_tokens)
               .with_temperature(config_.agent.enable_thinking ? 1.0 : config_.agent.temperature)
               .enable_thinking(config_.agent.enable_thinking)
               .enable_interleaved_thinking(config_.agent.enable_interleaved_thinking);

        // Add tools with caching enabled (cache control added automatically in with_tools())
        if (tool_registry_.has_tools()) {
            builder.with_tools(tool_registry_);
        }

        // Add initial user message
        builder.add_message(claude::messages::Message::user_text("Please analyze the binary to answer: " + task));

        claude::ChatRequest request = builder.build();

        // Initialize execution state with the request
        execution_state_.reset_with_request(request);

        // Run analysis loop
        run_analysis_loop();
    }



    // Process resume
    void process_resume() {
        if (!execution_state_.is_valid()) {
            emit_log(LogLevel::ERROR, "No valid saved state to resume from");
            change_state(AgentState::Status::Idle);
            return;
        }

        emit_log(LogLevel::INFO, "Resuming from saved state at iteration " + std::to_string(execution_state_.get_iteration()));

        // Continue from saved state
        run_analysis_loop();
    }

    // Process continue
    void process_continue(const std::string& additional) {
        emit_log(LogLevel::INFO, "Continuing with additional instructions: " + additional);

        if (!execution_state_.is_valid()) {
            emit_log(LogLevel::WARNING, "No saved state found while continuing");
            change_state(AgentState::Status::Idle);
            return;
        }

        // Add user message to execution state
        claude::messages::Message continue_msg = claude::messages::Message::user_text(additional);

        // will invalidate cache

        execution_state_.add_message(continue_msg);

        // Continue analysis
        run_analysis_loop();
    }

    // Helper to check if a message contains non-tool-result content
    bool has_non_tool_result_content(const claude::messages::Message& msg) const {
        if (msg.role() != claude::messages::Role::User) return false;

        for (const auto& content : msg.contents()) {
            if (!dynamic_cast<claude::messages::ToolResultContent*>(content.get())) {
                return true;  // Found non-tool-result content
            }
        }
        return false;
    }

    // Helper to check if a message contains tool results
    bool has_tool_results(const claude::messages::Message& msg) const {
        if (msg.role() != claude::messages::Role::User) return false;

        for (const auto& content : msg.contents()) {
            if (dynamic_cast<claude::messages::ToolResultContent*>(content.get())) {
                return true;
            }
        }
        return false;
    }

    template<typename Container>
    std::string join(const Container& container, const std::string& delimiter) {
        std::ostringstream oss;
        auto it = container.begin();
        if (it != container.end()) {
            oss << *it;
            ++it;
        }
        for (; it != container.end(); ++it) {
            oss << delimiter << *it;
        }
        return oss.str();
    }

    // Check if we need to consolidate context
    bool should_consolidate_context() {
        if (context_state_.consolidation_in_progress) {
            return false;  // Already consolidating
        }

        claude::TokenUsage usage = token_stats_.get_last_usage();
        int total_tokens = usage.input_tokens + usage.output_tokens + usage.cache_read_tokens + usage.cache_creation_tokens;
        return total_tokens > config_.agent.context_limit;
    }

    // Trigger context consolidation
    void trigger_context_consolidation() {
        emit_log(LogLevel::WARNING, "Context limit reached. Initiating memory consolidation...");
        event_bus_.publish(AgentEvent(AgentEvent::CONTEXT_CONSOLIDATION, agent_id_, {
            {"status", "starting"}
        }));

        context_state_.consolidation_in_progress = true;
        context_state_.consolidation_count++;
        context_state_.last_consolidation = std::chrono::steady_clock::now();

        // Add consolidation message
        claude::messages::Message consolidation_msg = claude::messages::Message::user_text(CONSOLIDATION_PROMPT);
        execution_state_.add_message(consolidation_msg);

        // Mark that we're in consolidation mode
        execution_state_.set_valid(true);
    }

    ConsolidationResult process_consolidation_response(const claude::messages::Message& response_msg, const std::vector<claude::messages::Message>& tool_results) {
        ConsolidationResult result;

        // Extract stored keys from tool calls
        std::vector<const claude::messages::ToolUseContent*> tool_uses = claude::messages::ContentExtractor::extract_tool_uses(response_msg);
        for (const claude::messages::ToolUseContent *tool_use: tool_uses) {
            if (tool_use->name == "store_analysis" && tool_use->input.contains("key")) {
                result.stored_keys.push_back(tool_use->input["key"]);
            }
        }

        // Extract summary text
        std::optional<std::string> text_content = claude::messages::ContentExtractor::extract_text(response_msg);
        if (text_content) {
            result.summary = *text_content;
            result.success = true;
        } else {
            emit_log(LogLevel::WARNING, "After attempting consolidation, the LLM did not provide a summary");

            // Fallback summary in case LLM didn't make one (which would be bad)
            result.summary = std::format("Consolidated {} findings to memory. Keys: {}",
                result.stored_keys.size(),
                join(result.stored_keys, ", "));
            result.success = true;
        }

        return result;
    }

    // Rebuild conversation after consolidation
    void rebuild_after_consolidation(const ConsolidationResult& consolidation) {
        emit_log(LogLevel::INFO, "Rebuilding conversation with consolidated memory...");

        // Save current task and token stats
        std::string original_task = state_.get_task();
        claude::TokenUsage total_usage_before = token_stats_.get_total();

        // store old tracker session
        stats_sessions_.emplace_back(std::move(token_stats_));

        // Clear execution state for rebuild
        execution_state_.clear();

        // Reset token tracker
        reset_token_usage();

        // Build new request with consolidated context
        claude::ChatRequestBuilder builder;
        builder.with_model(config_.agent.model)
               .with_system_prompt(SYSTEM_PROMPT)
               .with_max_tokens(config_.agent.max_tokens)
               .with_max_thinking_tokens(config_.agent.max_thinking_tokens)
               .with_temperature(config_.agent.enable_thinking ? 1.0 : config_.agent.temperature)
               .enable_thinking(config_.agent.enable_thinking)
               .enable_interleaved_thinking(config_.agent.enable_interleaved_thinking);

        // Add tools
        if (tool_registry_.has_tools()) {
            builder.with_tools(tool_registry_);
        }

        // Create continuation message
        std::string continuation_prompt = std::format(
            CONSOLIDATION_CONTINUATION_PROMPT,
            original_task,
            consolidation.summary,
            consolidation.stored_keys.empty() ? "(none)" : join(consolidation.stored_keys, ", ")
        );

        builder.add_message(claude::messages::Message::user_text(continuation_prompt));

        // Reset execution state with new request
        execution_state_.reset_with_request(builder.build());

        // Log consolidation stats
        emit_log(LogLevel::INFO, std::format(
            "Context consolidated. Stored {} keys. Token usage before: {} in, {} out, {} cache read, {} cache write. Cost so far: ${:.4f}",
            consolidation.stored_keys.size(),
            total_usage_before.input_tokens,
            total_usage_before.output_tokens,
            total_usage_before.cache_read_tokens,
            total_usage_before.cache_creation_tokens,
            total_usage_before.estimated_cost()
        ));

        // Mark consolidation complete
        context_state_.consolidation_in_progress = false;
    }

    void apply_incremental_caching() {
        claude::ChatRequest& request = execution_state_.get_request();
        if (request.messages.size() < 2) {
            return;
        }

        // IMPORTANT: We can only have 4 cache breakpoints total
        // We already use 2 for tools and system prompt, so we can only add 2 more

        // First, remove any existing cache controls from messages to avoid exceeding limit
        for (claude::messages::Message& msg: request.messages) {
            claude::messages::Message new_msg(msg.role());
            for (const std::unique_ptr<claude::messages::Content>& content: msg.contents()) {
                // Clone content without cache control
                if (auto* text = dynamic_cast<claude::messages::TextContent*>(content.get())) {
                    new_msg.add_content(std::make_unique<claude::messages::TextContent>(text->text));
                } else if (auto* tool_result = dynamic_cast<claude::messages::ToolResultContent*>(content.get())) {
                    new_msg.add_content(std::make_unique<claude::messages::ToolResultContent>(
                        tool_result->tool_use_id,
                        tool_result->content,
                        tool_result->is_error
                        // Explicitly no cache control
                    ));
                } else {
                    new_msg.add_content(content->clone());
                }
            }
            msg = std::move(new_msg);
        }

        // Now add cache control only to the LAST tool result message
        // This creates a single moving cache point for the conversation
        int cache_position = -1;
        for (int i = request.messages.size() - 1; i >= 0; i--) {
            const claude::messages::Message& msg = request.messages[i];

            // Find the last user message with tool results
            if (msg.role() == claude::messages::Role::User && has_tool_results(msg)) {
                cache_position = i;
                break;
            }
        }

        if (cache_position >= 0) {
            // Create a new message with cache control on the last content block
            claude::messages::Message& msg_to_modify = request.messages[cache_position];

            // Clone the message and add cache control to the last tool result
            claude::messages::Message new_msg(msg_to_modify.role());
            const std::vector<std::unique_ptr<claude::messages::Content>>& contents = msg_to_modify.contents();

            for (size_t i = 0; i < contents.size(); i++) {
                if (i == contents.size() - 1) {
                    // Last content block - add cache control
                    if (auto* tool_result = dynamic_cast<claude::messages::ToolResultContent*>(contents[i].get())) {
                        new_msg.add_content(std::make_unique<claude::messages::ToolResultContent>(
                            tool_result->tool_use_id,
                            tool_result->content,
                            tool_result->is_error,
                            claude::messages::CacheControl{claude::messages::CacheControl::Type::Ephemeral}
                        ));
                    } else {
                        new_msg.add_content(contents[i]->clone());
                    }
                } else {
                    new_msg.add_content(contents[i]->clone());
                }
            }

            // Replace the message
            request.messages[cache_position] = std::move(new_msg);
        }
    }

    // Main analysis loop
    void run_analysis_loop() {
        int iteration = execution_state_.get_iteration();
        bool grader_approved = false;

        while (iteration < config_.agent.max_iterations && !stop_requested_ && !grader_approved && state_.is_running()) {
            if (stop_requested_) {
                emit_log(LogLevel::INFO, "Analysis interrupted by stop request");
                break;
            }

            iteration++;
            execution_state_.set_iteration(iteration);
            api_client_.set_iteration(iteration);

            emit_log(LogLevel::INFO, "Iteration " + std::to_string(iteration));

            // Apply caching for continuation
            if (iteration > 1) {
                apply_incremental_caching();
            }

            // Create request for this iteration
            claude::ChatRequest current_request = execution_state_.get_request();

            // Check if we need to consolidate context BEFORE sending
            if (should_consolidate_context() && !context_state_.consolidation_in_progress) {
                trigger_context_consolidation();
                continue;  // Loop will create consolidation request next iteration
            }

            // Send request with retry on OAuth expiry
            claude::ChatResponse response = api_client_.send_request(current_request);

            // Check for OAuth token expiry (401 authentication error)
            if (!response.success && response.error && 
                response.error->find("OAuth token has expired") != std::string::npos) {
                
                emit_log(LogLevel::INFO, "OAuth token expired, attempting to refresh...");
                
                if (refresh_oauth_credentials()) {
                    // Retry the request with refreshed credentials
                    emit_log(LogLevel::INFO, "Retrying request with refreshed OAuth token...");
                    response = api_client_.send_request(current_request);
                } else {
                    emit_log(LogLevel::ERROR, "Failed to refresh OAuth token");
                }
            }

            if (!response.success) {
                handle_api_error(response);
                break;
            }

            // Send the response message directly to the UI!
            emit_api_message(&response.message);

            // Log thinking information
            if (response.has_thinking()) {
                std::vector<const claude::messages::ThinkingContent*> thinking_blocks = response.get_thinking_blocks();
                std::vector<const claude::messages::RedactedThinkingContent*> redacted_blocks = response.get_redacted_thinking_blocks();

                emit_log(LogLevel::INFO, std::format("Response contains {} thinking blocks and {} redacted blocks", thinking_blocks.size(), redacted_blocks.size()));
            }

            validate_thinking_preservation(response);

            // Track token + cache usage
            token_stats_.add_usage(response.usage);
            log_token_usage(response.usage, iteration);

            // Add response to execution state
            // IMPORTANT: We must preserve the entire message including thinking blocks
            execution_state_.add_message(response.message);

            // Process tool calls
            std::vector<claude::messages::Message> tool_results = process_tool_calls(response.message, iteration);

            // Check if this was a consolidation response
            if (context_state_.consolidation_in_progress) {
                ConsolidationResult consolidation = process_consolidation_response(response.message, tool_results);
                if (consolidation.success) {
                    rebuild_after_consolidation(consolidation);
                    iteration = 0;  // incremented on loop start
                    continue;
                }
            }

            // When adding tool results, combine them
            if (!tool_results.empty()) {
                claude::messages::Message combined_results = claude::messages::Message(claude::messages::Role::User);

                for (const claude::messages::Message& result : tool_results) {
                    for (const std::unique_ptr<claude::messages::Content>& content: result.contents()) {
                        // Skip empty text content blocks
                        if (auto* text = dynamic_cast<claude::messages::TextContent*>(content.get())) {
                            if (text->text.empty()) continue;
                        }
                        combined_results.add_content(content->clone());
                    }
                }

                execution_state_.add_message(combined_results);
            }
            
            // Check for pending user messages to inject
            {
                qmutex_locker_t lock(pending_messages_mutex_);
                if (!pending_user_messages_.empty()) {
                    // Process all pending messages
                    while (!pending_user_messages_.empty()) {
                        std::string user_msg = pending_user_messages_.front();
                        pending_user_messages_.pop();
                        
                        emit_log(LogLevel::INFO, "Injecting user guidance: " + user_msg);
                        event_bus_.publish(AgentEvent(AgentEvent::USER_MESSAGE, agent_id_, {
                            {"message", user_msg}
                        }));
                        
                        // If we just added tool results, append the user message to them
                        if (!tool_results.empty() && execution_state_.message_count() > 0) {
                            // Get the last message (should be the combined tool results)
                            claude::ChatRequest& req = execution_state_.get_request();
                            claude::messages::Message& last_msg = req.messages.back();
                            if (last_msg.role() == claude::messages::Role::User) {
                                // Add user text to the existing user message with tool results
                                last_msg.add_content(std::make_unique<claude::messages::TextContent>(user_msg));
                                
                                // Note: The conversation will be updated with the complete message on the next API call
                            } else {
                                // Create new user message if last wasn't user role
                                claude::messages::Message user_guidance = claude::messages::Message::user_text(user_msg);
                                execution_state_.add_message(user_guidance);
                            }
                        } else {
                            // No recent tool results, create standalone user message
                            claude::messages::Message user_guidance = claude::messages::Message::user_text(user_msg);
                            execution_state_.add_message(user_guidance);
                        }
                    }
                }
            }

            // Handle natural completion - agent stops when satisfied
            if (response.stop_reason == claude::StopReason::EndTurn && !response.has_tool_calls()) {
                if (iteration > 1 && !context_state_.consolidation_in_progress) {
                    // Agent has naturally stopped - they're satisfied with their understanding
                    emit_log(LogLevel::INFO, "Agent stopped investigation");

                    // Check with grader if enabled
                    if (grader_) {
                        AnalysisGrader::GradeResult grade = check_with_grader();
                        
                        // Log grader's full response (with thinking) to console
                        emit_grader_message(grade.fullMessage);
                        
                        if (grade.complete) {
                            emit_log(LogLevel::INFO, "Grader approved investigation");
                            grader_approved = true;
                            
                            // Emit final report event
                            event_bus_.publish(AgentEvent(AgentEvent::ANALYSIS_RESULT, agent_id_, {
                                {"report", grade.response}
                            }));
                            change_state(AgentState::Status::Completed);
                        } else {
                            emit_log(LogLevel::INFO, "Investigation needs more work - sending questions back to agent");
                            
                            // Add grader's questions as user message and continue
                            // Mark this as grader feedback with a special prefix we can filter
                            claude::messages::Message continue_msg = claude::messages::Message::user_text("__GRADER_FEEDBACK__: " + grade.response);
                            execution_state_.add_message(continue_msg);
                        }
                    } else {
                        // No grader - extract and send the actual findings
                        emit_log(LogLevel::INFO, "Grader disabled - extracting final findings");
                        grader_approved = true;
                        
                        // Get the last assistant message which contains the actual findings
                        std::string final_findings = extract_last_assistant_message();
                        
                        // Emit the actual findings as the final report
                        event_bus_.publish(AgentEvent(AgentEvent::ANALYSIS_RESULT, agent_id_, {
                            {"report", final_findings}
                        }));
                        change_state(AgentState::Status::Completed);
                    }
                }
            }
        }

        if (iteration >= config_.agent.max_iterations) {
            emit_log(LogLevel::WARNING, "Reached maximum iterations");
            change_state(AgentState::Status::Completed);
        }
    }


    // Handle API errors
    void handle_api_error(const claude::ChatResponse& response) {
        if (!response.error) {
            emit_log(LogLevel::ERROR, "Unknown API error");
            event_bus_.emit_error(agent_id_, "Unknown API error");
            change_state(AgentState::Status::Idle);
            last_error_ = "Unknown API error";
            return;
        }

        std::string error_msg = *response.error;

        // Check for thinking-specific errors
        if (error_msg.find("thinking") != std::string::npos ||
            error_msg.find("budget_tokens") != std::string::npos) {
            emit_log(LogLevel::ERROR, "Thinking-related error: " + error_msg);
            emit_log(LogLevel::INFO, "Consider adjusting thinking budget or disabling thinking");
            event_bus_.emit_error(agent_id_, error_msg);
        }

        if (claude::Client::is_recoverable_error(response)) {
            emit_log(LogLevel::INFO, "You can resume the analysis");
            change_state(AgentState::Status::Paused);
            execution_state_.set_valid(true);
            last_error_ = "API Error (recoverable): " + error_msg;
        } else {
            emit_log(LogLevel::ERROR, "Unrecoverable API error: " + error_msg);
            event_bus_.emit_error(agent_id_, error_msg);
            change_state(AgentState::Status::Idle);
            execution_state_.set_valid(false);
            last_error_ = "API Error: " + error_msg;
        }
    }

    void validate_thinking_preservation(const claude::ChatResponse& response) {
        if (!response.has_thinking() || !response.has_tool_calls()) {
            return;  // No validation needed
        }

        // Ensure the message being saved includes thinking blocks
        std::vector<const claude::messages::ThinkingContent*> thinking_blocks = response.get_thinking_blocks();
        std::vector<const claude::messages::RedactedThinkingContent*> redacted_blocks = response.get_redacted_thinking_blocks();

        if (thinking_blocks.empty() && redacted_blocks.empty()) {
            emit_log(LogLevel::WARNING, "Tool calls present but no thinking blocks found - this might indicate an issue");
        } else {
            emit_log(LogLevel::DEBUG, std::format("Preserving {} thinking blocks with tool calls",
                thinking_blocks.size() + redacted_blocks.size()));
        }
    }


    // Helper to extract the last assistant message from conversation
    std::string extract_last_assistant_message() {
        std::vector<claude::messages::Message> messages = execution_state_.get_messages();
        
        // Iterate in reverse to find last assistant message
        for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
            if (it->role() == claude::messages::Role::Assistant) {
                // Extract text content from the message
                auto text = claude::messages::ContentExtractor::extract_text(*it);
                if (text && !text->empty()) {
                    return *text;
                }
            }
        }
        
        return "Investigation complete (no findings extracted)";
    }
    
    // changes state and emits event
    void change_state(AgentState::Status new_status) {
        state_.set_status(new_status);
        event_bus_.emit_state(agent_id_, static_cast<int>(new_status));
    }

    // Check with grader to evaluate agent's work
    AnalysisGrader::GradeResult check_with_grader() {
        emit_log(LogLevel::INFO, "Evaluating analysis quality...");
        
        // Build grading context
        AnalysisGrader::GradingContext context;
        
        // Extract user messages, excluding grader feedback
        std::stringstream all_user_requests;
        bool first = true;
        
        for (const claude::messages::Message& msg: execution_state_.get_messages()) {
            if (msg.role() == claude::messages::Role::User) {
                std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(msg);
                if (text && !text->empty()) {
                    // Skip grader feedback messages
                    if (text->find("__GRADER_FEEDBACK__: ") == 0) {
                        continue;
                    }
                    
                    if (!first) {
                        all_user_requests << "\n\n---\n\n";
                    }
                    all_user_requests << *text;
                    first = false;
                }
            }
        }
        context.user_request = all_user_requests.str();
        context.agent_work = execution_state_.get_messages();
        context.stored_analyses = memory_->get_analysis();

        // Grade the analysis and return result
        return grader_->evaluate_analysis(context);
    }
    
    void log_token_usage(const claude::TokenUsage& usage, int iteration) {
        std::string summary;
        
        // If we have previous sessions (from consolidations), use cumulative totals
        if (!stats_sessions_.empty()) {
            claude::TokenUsage cumulative = get_cumulative_token_usage();
            summary = token_stats_.get_iteration_summary_with_cumulative(usage, iteration, cumulative);
        } else {
            // No consolidations yet, use regular summary
            summary = token_stats_.get_iteration_summary(usage, iteration);
        }
        
        emit_log(LogLevel::INFO, summary);
        
        // Emit metrics event with properly serialized token usage
        event_bus_.emit_metric(agent_id_, {
            {"tokens", {
                {"input_tokens", usage.input_tokens},
                {"output_tokens", usage.output_tokens},
                {"cache_read_tokens", usage.cache_read_tokens},
                {"cache_creation_tokens", usage.cache_creation_tokens},
                {"estimated_cost", usage.estimated_cost()}
            }},
            {"iteration", iteration}
        });
    }
};

} // namespace llm_re

#endif //AGENT_H