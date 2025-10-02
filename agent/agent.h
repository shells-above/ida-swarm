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
#include "analysis/actions.h"
#include "analysis/deep_analysis.h"
#include "analysis/memory_tool.h"
#include "patching/patch_manager.h"
#include "patching/code_injection_manager.h"
#include <nalt.hpp>  // For get_input_file_path
#include <fstream>
#include <filesystem>
#include <format>
#include <thread>
#include <chrono>

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
        Continue  // continuing processing on an old task with new instructions
    };

    Type type;
    std::string content;

    static AgentTask new_task(const std::string& task) {
        return {Type::NewTask, task};
    }


    static AgentTask continue_with(const std::string& additional) {
        return {Type::Continue, additional};
    }
};

// Main agent
class Agent {
protected:
    // Needs to be accessible to SwarmAgent for restoration
    AgentExecutionState execution_state_;  // Execution and conversation state

    // Protected getter for current task (for SwarmAgent status updates)
    std::string get_current_task() const { return state_.get_task(); }

private:
    // State management
    AgentState state_;
    std::string last_error_;

    // Token tracking
    claude::usage::TokenStats token_stats_;

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
                std::string log_filename = std::format("anthropic_requests_agent_{}.log", agent_id_);
                return claude::Client(config.api.api_key, config.api.base_url, log_filename);
            }
            
            // Pass the shared_ptr so all clients share the same credentials
            // Also pass oauth_manager_ so Client can handle token refresh automatically
            std::string log_filename = std::format("anthropic_requests_agent_{}.log", agent_id_);
            return claude::Client(oauth_creds, oauth_manager_, config.api.base_url, log_filename);
        }
        
        // Default to API key with log filename configured
        std::string log_filename = std::format("anthropic_requests_agent_{}.log", agent_id_);
        return claude::Client(config.api.api_key, config.api.base_url, log_filename);
    }

public:
    Agent(const Config& config, const std::string& agent_id = "agent")
        : config_(config),
          agent_id_(agent_id),
          event_bus_(get_event_bus()),
          executor_(std::make_shared<ActionExecutor>()),
          deep_analysis_manager_(config.agent.enable_deep_analysis ? std::make_shared<DeepAnalysisManager>(config) : nullptr),
          api_client_(create_api_client(config)),
          grader_(config.grader.enabled ? std::make_unique<AnalysisGrader>(config) : nullptr) {

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

        // Get the binary path for this agent (the copy being analyzed)
        std::string binary_path;
        char input_path[MAXSTR];
        if (get_input_file_path(input_path, sizeof(input_path)) > 0) {
            binary_path = input_path;
            emit_log(LogLevel::DEBUG, "Agent binary path: " + binary_path);
        } else {
            emit_log(LogLevel::WARNING, "Could not get binary path for dual patching");
        }

        // Set binary path on patch manager for dual patching
        if (!binary_path.empty() && patch_manager_) {
            patch_manager_->set_binary_path(binary_path);
            emit_log(LogLevel::DEBUG, "Enabled dual patching (IDA DB + file)");

            // Create code injection manager
            code_injection_manager_ = std::make_shared<CodeInjectionManager>(patch_manager_.get(), binary_path);
            if (code_injection_manager_->initialize()) {
                // Connect patch manager to code injection manager
                patch_manager_->set_code_injection_manager(code_injection_manager_.get());
                emit_log(LogLevel::DEBUG, "Code injection manager initialized");
            } else {
                emit_log(LogLevel::WARNING, "Failed to initialize code injection manager");
                code_injection_manager_ = nullptr;
            }
        }

        // Register tools
        tools::register_ida_tools(tool_registry_, executor_, deep_analysis_manager_, patch_manager_, code_injection_manager_, config_);

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

    // Memory tool management
    void set_memory_directory(const std::string& memory_dir) {
        memory_handler_ = std::make_unique<MemoryToolHandler>(memory_dir);
    }

    // Token statistics
    claude::TokenUsage get_token_usage() const {
        return token_stats_.get_total();
    }

    void reset_token_usage() {
        token_stats_.reset();
    }

protected:  // Changed to protected so SwarmAgent can access
    // Configuration
    const Config& config_;

    // Core components
    std::shared_ptr<ActionExecutor> executor_;                       // action executor, actual integration with IDA
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager_;     // manages deep analysis tasks
    std::shared_ptr<PatchManager> patch_manager_;                    // patch manager
    std::shared_ptr<CodeInjectionManager> code_injection_manager_;   // code injection manager
    std::unique_ptr<AnalysisGrader> grader_;                         // quality evaluator for agent work
    std::unique_ptr<MemoryToolHandler> memory_handler_;              // memory tool handler for /memories filesystem
    claude::tools::ToolRegistry tool_registry_;                      // registry for tools
    std::shared_ptr<claude::auth::OAuthManager> oauth_manager_;      // OAuth manager for this agent instance
    claude::Client api_client_;                                      // agent api client
    
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

            // Execute tool (intercept memory tool calls)
            claude::messages::Message result_msg = [&]() {
                if (tool_use->name == "memory") {
                    json result = memory_handler_
                        ? memory_handler_->execute_command(tool_use->input)
                        : json{{"success", false}, {"error", "Memory system not initialized"}};
                    return claude::messages::Message::tool_result(tool_use->id, result.dump());
                } else {
                    return tool_registry_.execute_tool_call(*tool_use);
                }
            }();
            
            // Check and trim tool result if it's too large for context
            claude::messages::Message trimmed_result = check_and_trim_tool_result(result_msg);
            results.push_back(trimmed_result);

            // Extract result content (from trimmed result for event logging)
            json result_json;
            for (const std::unique_ptr<claude::messages::Content>& content: trimmed_result.contents()) {
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
               .enable_interleaved_thinking(config_.agent.enable_interleaved_thinking)
               .enable_auto_context_clearing(100000, 5, {"memory"});

        // Add tools with caching enabled (cache control added automatically in with_tools())
        if (tool_registry_.has_tools()) {
            builder.with_tools(tool_registry_);
        }

        // Add initial user message with database context
        std::string initial_message;
        const char* idb_path = get_path(PATH_TYPE_IDB);
        if (idb_path) {
            initial_message = "You are analyzing the IDA database: " + std::string(idb_path) + "\n\n";
        }
        initial_message += "Please analyze the binary to answer: " + task;
        builder.add_message(claude::messages::Message::user_text(initial_message));

        claude::ChatRequest request = builder.build();

        // Add memory tool FIRST (before IDA tools) so cache control stays on last IDA tool
        json memory_tool = {
            {"type", "memory_20250818"},
            {"name", "memory"}
        };
        request.tool_definitions.insert(request.tool_definitions.begin(), memory_tool);

        // Initialize execution state with the request
        execution_state_.reset_with_request(request);

        // Run analysis loop
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

    // Estimate current conversation token count
    int estimate_current_conversation_tokens() const {
        int total_tokens = 0;
        
        // Estimate tokens from execution state messages
        for (const claude::messages::Message& msg : execution_state_.get_messages()) {
            std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(msg);
            if (text) {
                total_tokens += text->length() / 2;  // Conservative token estimation (2 chars per token)
            }
            
            // Add tokens for tool calls and results
            auto tool_calls = claude::messages::ContentExtractor::extract_tool_uses(msg);
            for (const auto* tool : tool_calls) {
                total_tokens += tool->name.length() / 2;
                total_tokens += tool->input.dump().length() / 2;
            }
        }
        
        return total_tokens;
    }

    // Check and trim tool result if it would exceed context limits
    claude::messages::Message check_and_trim_tool_result(claude::messages::Message result_msg) {
        // Extract tool result content
        const std::vector<std::unique_ptr<claude::messages::Content>>& contents = result_msg.contents();
        if (contents.empty()) {
            return result_msg;  // No content to check
        }
        
        // Find the tool result content
        claude::messages::ToolResultContent* tool_result = nullptr;
        for (const std::unique_ptr<claude::messages::Content>& content: contents) {
            if (auto* tr = dynamic_cast<claude::messages::ToolResultContent*>(content.get())) {
                tool_result = tr;
                break;
            }
        }
        
        if (!tool_result) {
            return result_msg;  // No tool result found
        }
        
        // Estimate tokens in tool result
        int tool_result_tokens = tool_result->content.length() / 2;  // Conservative estimation for tool results
        int current_conversation_tokens = estimate_current_conversation_tokens();
        int available_tokens = config_.agent.context_limit - current_conversation_tokens - config_.agent.tool_result_trim_buffer;
        
        // Check if we need to trim
        if (tool_result_tokens <= available_tokens) {
            return result_msg;  // Fits within limits
        }
        
        // Calculate how much to keep (in characters)
        int chars_to_keep = available_tokens * 2;  // Convert tokens back to characters
        if (chars_to_keep < 100) {  // Always keep at least 100 characters
            chars_to_keep = 100;
        }
        
        // Trim the content and add truncation notice
        std::string original_content = tool_result->content;
        std::string trimmed_content;
        
        if (chars_to_keep < (int)original_content.length()) {
            trimmed_content = original_content.substr(0, chars_to_keep);
            int truncated_tokens = (original_content.length() - chars_to_keep) / 2;
            trimmed_content += std::format("\n\n[...TRIMMED: Tool result too long, truncated {} more tokens to fit context limits]", truncated_tokens);
            
            emit_log(LogLevel::WARNING, std::format("Trimmed tool result from {} to {} characters ({} tokens truncated)", 
                original_content.length(), trimmed_content.length(), truncated_tokens));
        } else {
            trimmed_content = original_content;
        }
        
        // Create new message with trimmed content
        claude::messages::Message trimmed_msg(result_msg.role());
        for (const auto& content : contents) {
            if (auto* tr = dynamic_cast<claude::messages::ToolResultContent*>(content.get())) {
                // Replace with trimmed version, preserving other attributes
                trimmed_msg.add_content(std::make_unique<claude::messages::ToolResultContent>(
                    tr->tool_use_id,
                    trimmed_content,
                    tr->is_error,
                    tr->cache_control
                ));
            } else {
                // Keep other content as-is
                trimmed_msg.add_content(content->clone());
            }
        }
        
        return trimmed_msg;
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
    // Virtual hook for subclasses to add per-iteration behavior
    virtual void on_iteration_start(int iteration) {
        // Default: do nothing
        // SwarmAgent can override to add status updates
    }

    void run_analysis_loop() {
        int iteration = execution_state_.get_iteration();
        bool grader_approved = false;

        while (!stop_requested_ && !grader_approved && state_.is_running()) {
            if (stop_requested_) {
                emit_log(LogLevel::INFO, "Analysis interrupted by stop request");
                break;
            }

            iteration++;
            execution_state_.set_iteration(iteration);
            api_client_.set_iteration(iteration);

            emit_log(LogLevel::INFO, "Iteration " + std::to_string(iteration));

            // Call hook for subclass-specific behavior
            on_iteration_start(iteration);

            // Apply caching for continuation
            if (iteration > 1) {
                apply_incremental_caching();
            }

            // Create request for this iteration
            claude::ChatRequest current_request = execution_state_.get_request();

            // Send request - Client now handles OAuth token refresh automatically
            claude::ChatResponse response = api_client_.send_request(current_request);

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

            // Log context clearing if it occurred
            if (response.context_management) {
                for (const auto& edit : response.context_management->applied_edits) {
                    emit_log(LogLevel::INFO, std::format(
                        "Context cleared: {} tool uses ({} tokens)",
                        edit.cleared_tool_uses, edit.cleared_input_tokens));
                }
            }

            // Add response to execution state
            // IMPORTANT: We must preserve the entire message including thinking blocks
            execution_state_.add_message(response.message);

            // Process tool calls
            std::vector<claude::messages::Message> tool_results = process_tool_calls(response.message, iteration);

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
                if (iteration > 1) {
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
                            
                            // Get the agent's actual findings (not the grader's assessment)
                            std::string final_findings = extract_last_assistant_message();
                            
                            // Emit final report event with agent's findings
                            event_bus_.publish(AgentEvent(AgentEvent::ANALYSIS_RESULT, agent_id_, {
                                {"report", final_findings}
                            }));
                            change_state(AgentState::Status::Completed);
                        } else {
                            emit_log(LogLevel::INFO, "Investigation needs more work - sending questions back to agent");
                            
                            // Add grader's questions as user message and continue
                            // Mark this as grader feedback with a special prefix we can filter
                            claude::messages::Message continue_msg = claude::messages::Message::user_text("__GRADER_FEEDBACK__: " + grade.response +
                                " [NOTE FROM SYSTEM]: The grader can be very strict, "
                                "it performs a meticulous analysis of your final message (your FINAL / LATEST MESSAGE ONLY!) "
                                "and determines if it meets the criteria of the users task. "
                                "If something *truly* is not possible, and you can't complete it you need to make that clear to the Grader, "
                                "or else it will keep rejecting you. If this is the case, you can not just say that you can not do it, "
                                "you must *teach* the Grader what you are capable of, and what the impossible request would require that you simply can not provide and *WHY*. "
                                "If you don't teach the grader, it will keep rejecting you. "
                                "Do NOT use this as a way to get out of analysis early, ONLY use this if you truly can not do something. Think deeply!");

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
    }


    // Handle API errors
    void handle_api_error(const claude::ChatResponse& response) {
        if (!response.error) {
            emit_log(LogLevel::ERROR, "Unknown API error");
            event_bus_.emit_error(agent_id_, "Unknown API error");
            change_state(AgentState::Status::Completed);
            last_error_ = "Unknown API error";
            // Graceful shutdown on unknown error
            request_graceful_shutdown();
            return;
        }

        std::string error_msg = *response.error;
        
        // Check for thinking-specific errors
        if (error_msg.find("thinking") != std::string::npos ||
            error_msg.find("budget_tokens") != std::string::npos) {
            emit_log(LogLevel::ERROR, "Thinking-related error: " + error_msg);
            emit_log(LogLevel::INFO, "Consider adjusting thinking budget or disabling thinking");
        }

        // SDK has already handled retries internally, so any error at this point is final
        emit_log(LogLevel::ERROR, "API error (after retries): " + error_msg);
        
        // Emit error to UI
        event_bus_.emit_error(agent_id_, error_msg);
        
        // Mark execution state as invalid and store error
        change_state(AgentState::Status::Completed);
        execution_state_.set_valid(false);
        last_error_ = "API Error: " + error_msg;
        
        // Request graceful shutdown
        request_graceful_shutdown();
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


protected:
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
        
        return "Failed to extract final report.";
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

        // Grade the analysis and return result
        return grader_->evaluate_analysis(context);
    }
    
    void log_token_usage(const claude::TokenUsage& usage, int iteration) {
        std::string summary = token_stats_.get_iteration_summary(usage, iteration);
        emit_log(LogLevel::INFO, summary);

        // Emit token update event with cumulative totals and per-iteration values
        claude::TokenUsage cumulative_total = token_stats_.get_total();
        // Use the 'usage' parameter which contains per-iteration values for context calculation
        event_bus_.publish(AgentEvent(AgentEvent::AGENT_TOKEN_UPDATE, agent_id_, {
            {"agent_id", agent_id_},
            {"tokens", {
                {"input_tokens", cumulative_total.input_tokens},
                {"output_tokens", cumulative_total.output_tokens},
                {"cache_read_tokens", cumulative_total.cache_read_tokens},
                {"cache_creation_tokens", cumulative_total.cache_creation_tokens},
                {"estimated_cost", cumulative_total.estimated_cost()},
                {"model", model_to_string(cumulative_total.model)}
            }},
            {"session_tokens", {
                {"input_tokens", usage.input_tokens},
                {"output_tokens", usage.output_tokens},
                {"cache_read_tokens", usage.cache_read_tokens},
                {"cache_creation_tokens", usage.cache_creation_tokens}
            }},
            {"iteration", iteration}
        }));
    }

    // Graceful shutdown methods
    virtual void request_graceful_shutdown() {
        emit_log(LogLevel::INFO, "Requesting graceful shutdown...");
        
        // Stop and clean up worker thread FIRST
        emit_log(LogLevel::INFO, "Stopping worker thread...");
        stop();
        cleanup_thread();
        emit_log(LogLevel::INFO, "Worker thread stopped");
        
        // Save the current IDA database
        if (!save_ida_database()) {
            emit_log(LogLevel::WARNING, "Failed to save IDA database");
        }
        
        // Save conversation state
        save_conversation_state();
        
        // Mark as completed if not already
        if (state_.get_status() != AgentState::Status::Completed) {
            change_state(AgentState::Status::Completed);
        }
        
        // Request IDA to exit gracefully
        request_ida_exit();
    }

protected:
    // Save the current IDA database
    bool save_ida_database() {
        emit_log(LogLevel::INFO, "Saving IDA database...");
        
        struct SaveDatabaseRequest : exec_request_t {
            bool result = false;
            virtual ssize_t idaapi execute() override {
                result = save_database();
                return 0;
            }
        };
        
        SaveDatabaseRequest req;
        execute_sync(req, MFF_WRITE);
        
        if (req.result) {
            emit_log(LogLevel::INFO, "IDA database saved successfully");
        }
        return req.result;
    }

    // Save conversation state to file
    void save_conversation_state() {
        emit_log(LogLevel::INFO, "Saving conversation state...");
        
        try {
            // Get the agent workspace directory
            std::filesystem::path workspace = get_agent_workspace_path();
            std::filesystem::path state_file = workspace / "conversation_state.json";
            
            // Create state JSON
            json state = {
                {"agent_id", get_agent_id()},
                {"task", state_.get_task()},
                {"status", static_cast<int>(state_.get_status())},
                {"conversation", serialize_conversation()},
                {"tool_history", serialize_tool_history()},
                {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()},
                {"iteration", execution_state_.get_iteration()},
                {"token_stats", serialize_token_stats()}
            };
            
            // Write to file
            std::ofstream file(state_file);
            if (file.is_open()) {
                file << state.dump(2);
                file.close();
                emit_log(LogLevel::INFO, "Conversation state saved to: " + state_file.string());
            } else {
                emit_log(LogLevel::ERROR, "Failed to open state file for writing");
            }
        } catch (const std::exception& e) {
            emit_log(LogLevel::ERROR, std::string("Failed to save conversation state: ") + e.what());
        }
    }
    
    // Get agent workspace path
    std::filesystem::path get_agent_workspace_path() const {
        // Extract from current database path
        const char* idb_path = get_path(PATH_TYPE_IDB);
        if (idb_path) {
            std::filesystem::path db_path(idb_path);
            return db_path.parent_path();  // Should be workspace/agents/agent_X/
        }
        // Fallback to temp directory
        return std::filesystem::temp_directory_path() / "ida_swarm_workspace" / "agents" / agent_id_;
    }
    
    // Serialize conversation to JSON
    json serialize_conversation() const {
        json messages_json = json::array();
        
        for (const claude::messages::Message& msg: execution_state_.get_messages()) {
            // Convert role enum to string
            std::string role_str = (msg.role() == claude::messages::Role::User) ? "user" : "assistant";
            
            json msg_json = {
                {"role", role_str},
                {"content", json::array()}
            };
            
            // Serialize all content blocks
            for (const std::unique_ptr<claude::messages::Content>& content: msg.contents()) {
                // Use dynamic_cast to check content type
                if (auto* text = dynamic_cast<const claude::messages::TextContent*>(content.get())) {
                    msg_json["content"].push_back({
                        {"type", "text"},
                        {"text", text->text}
                    });
                } else if (auto* tool_use = dynamic_cast<const claude::messages::ToolUseContent*>(content.get())) {
                    msg_json["content"].push_back({
                        {"type", "tool_use"},
                        {"id", tool_use->id},
                        {"name", tool_use->name},
                        {"input", tool_use->input}
                    });
                } else if (auto* tool_result = dynamic_cast<const claude::messages::ToolResultContent*>(content.get())) {
                    msg_json["content"].push_back({
                        {"type", "tool_result"},
                        {"tool_use_id", tool_result->tool_use_id},
                        {"content", tool_result->content},
                        {"is_error", tool_result->is_error}
                    });
                }
            }
            
            messages_json.push_back(msg_json);
        }
        
        return messages_json;
    }
    
    // Serialize tool history
    json serialize_tool_history() const {
        json tools = json::array();
        
        for (const auto& [tool_id, iteration] : execution_state_.get_tool_iterations()) {
            std::optional<std::string> tool_name = execution_state_.get_tool_name(tool_id);
            tools.push_back({
                {"id", tool_id},
                {"name", tool_name.value_or("unknown")},
                {"iteration", iteration}
            });
        }
        
        return tools;
    }
    
    // Serialize token statistics
    json serialize_token_stats() const {
        claude::TokenUsage total = token_stats_.get_total();
        return {
            {"total_input_tokens", total.input_tokens},
            {"total_output_tokens", total.output_tokens},
            {"total_cache_read_tokens", total.cache_read_tokens},
            {"total_cache_creation_tokens", total.cache_creation_tokens},
            {"total_estimated_cost", total.estimated_cost()}
        };
    }
    
    // Request IDA to exit gracefully
    void request_ida_exit() {
        emit_log(LogLevel::INFO, "Requesting IDA to exit gracefully...");
        
        struct ExitRequest : exec_request_t {
            virtual ssize_t idaapi execute() override {
                // Request IDA to exit gracefully
                qexit(0);
                return 0;
            }
        };
        
        ExitRequest req;
        execute_sync(req, MFF_WRITE);
    }
};

} // namespace llm_re

#endif //AGENT_H