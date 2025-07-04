//
// Created by user on 6/30/25.
//

#ifndef AGENT_H
#define AGENT_H

#include "common.h"
#include "message_types.h"
#include "tool_system.h"
#include "anthropic_api.h"
#include "memory.h"
#include "actions.h"
#include "qt_widgets.h"
#include "deep_analysis.h"

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
        mutex_ = qmutex_create();  // Initialize in constructor
    }

    ~AgentState() {
        if (mutex_) {
            qmutex_free(mutex_);
        }
    }

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

// Conversation state management
class ConversationState {
    std::vector<messages::Message> messages_;
    std::map<std::string, int> tool_call_iterations_;     // tool_id -> iteration
    std::map<std::string, std::string> tool_call_names_;  // tool_id -> tool_name
    mutable qmutex_t mutex_;

public:
    ConversationState() {
        mutex_ = qmutex_create();
    }

    ~ConversationState() {
        if (mutex_) {
            qmutex_free(mutex_);
        }
    }

    void add_message(const messages::Message& msg) {
        qmutex_locker_t lock(mutex_);
        messages_.push_back(msg);
    }

    void add_messages(const std::vector<messages::Message>& msgs) {
        qmutex_locker_t lock(mutex_);
        messages_.insert(messages_.end(), msgs.begin(), msgs.end());
    }

    std::vector<messages::Message> get_messages() const {
        qmutex_locker_t lock(mutex_);
        return messages_;
    }

    void track_tool_call(const std::string& tool_id, const std::string& tool_name, int iteration) {
        qmutex_locker_t lock(mutex_);
        tool_call_iterations_[tool_id] = iteration;
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

    void clear() {
        qmutex_locker_t lock(mutex_);
        messages_.clear();
        tool_call_iterations_.clear();
        tool_call_names_.clear();
    }

    size_t message_count() const {
        qmutex_locker_t lock(mutex_);
        return messages_.size();
    }

    json to_json() const {
        qmutex_locker_t lock(mutex_);
        json j;
        j["message_count"] = messages_.size();
        j["tool_calls"] = tool_call_iterations_.size();
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
class REAgent {
private:
    // Core components
    std::shared_ptr<BinaryMemory> memory_;                        // memory that can be scripted by the LLM
    std::shared_ptr<ActionExecutor> executor_;                    // action executor, actual integration with IDA
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager_;  // manages deep analysis tasks
    tools::ToolRegistry tool_registry_;         // registry of tools that use the action executor
    api::AnthropicClient api_client_;           // api client

    // State management
    AgentState state_;
    ConversationState conversation_;  // stores all Message's with when tools were used
    const Config& config_;
    std::string last_error_;

    // Token tracking
    api::TokenTracker token_tracker_;
    std::vector<api::TokenTracker> tracker_sessions_;  // we add to this the previous token_tracker when we hit context limit

    static constexpr int CONTEXT_LIMIT_TOKENS = 150000;  // Trigger consolidation at 150k

    // Cache performance tracking
    struct CacheStats {
        int total_cache_hits = 0;      // Total cached tokens read
        int total_cache_misses = 0;    // Total non-cached input tokens
        int total_cache_writes = 0;    // Number of cache write operations
        double total_cache_savings = 0.0;

        double get_hit_rate() const {
            int total = total_cache_hits + total_cache_misses;
            return total > 0 ? (double)total_cache_hits / total : 0.0;
        }

        json to_json() const {
            return {
                {"cache_hit_tokens", total_cache_hits},
                {"cache_miss_tokens", total_cache_misses},
                {"cache_writes", total_cache_writes},
                {"hit_rate", get_hit_rate()},
                {"estimated_savings", total_cache_savings}
            };
        }
    } cache_stats_;

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

    // Saved state for resume
    struct SavedState {
        api::ChatRequest request;
        int iteration = 0;
        bool valid = false;
        std::chrono::steady_clock::time_point saved_at;
    } saved_state_;

    // Callbacks
    std::function<void(LogLevel, const std::string&)> log_callback_;
    std::function<void(const std::string&, const json&, int)> message_log_callback_;
    std::function<void(const std::string&)> final_report_callback_;
    std::function<void(const std::string&, const std::string&, const json&)> tool_started_callback_;
    std::function<void(const std::string&, const std::string&, const json&, const json&)> tool_callback_;
    std::function<void(AgentState::Status)> state_change_callback_;

    // System prompt
    static constexpr const char* SYSTEM_PROMPT = R"(You are an advanced reverse engineering agent working inside IDA Pro. Your goal is to analyze binaries and answer specific questions about their functionality.

You have access to various tools to examine the binary:
- Cross-reference tools to trace function calls and data usage
- Decompilation and disassembly tools to understand code
- Pattern matching to find specific code constructs
- String analysis to understand functionality
- Memory tools to save and query your findings

Guidelines:
1. Start by understanding the overall structure of what you're analyzing
2. Use cross-references to trace how functions and data are connected
3. Decompile functions to understand their logic
4. Look for strings and imports to understand functionality
5. Save important findings and speculation to memory for later reference
6. Save confident information in the IDA database through function name setting and adding comments
7. When you have gathered enough information, submit your final report
8. Do NOT use decimal to represent your function / data addresses, STICK TO HEXADECIMAL!

When reverse engineering complicated functions (or where exact understanding of a function is exceedingly important), request the function disassembly and analyze it in that message METICULOUSLY! You will not be able to revisit the disassembly later as it is an expensive action.
Note that disassembly is expensive! Only use it when you need a comprehensive understanding of a function, or when the decompiled result appears incorrect and you want to analyze manually.
Be systematic and thorough. Build your understanding incrementally.

Remember that you can execute multiple tool calls at once, in fact I encourage it!
If you realize you need information from multiple tool calls, don't wait to do it in multiple messages.
Do it all in one! But do NOT go crazy, *only do the tool calls you need*.)";


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

What's your next step to complete the task?)";



public:
    REAgent(const Config& config)
        : config_(config),
          api_client_(config.api.api_key, config.api.base_url),
          memory_(std::make_shared<BinaryMemory>()),
          executor_(std::make_shared<ActionExecutor>(memory_)),
          deep_analysis_manager_(std::make_shared<DeepAnalysisManager>(memory_, config.api.api_key)) {

        queue_mutex_ = qmutex_create();
        task_semaphore_ = qsem_create(nullptr, 0);

        // Register all tools
        tool_registry_.register_all_tools(memory_, executor_, config.agent.enable_deep_analysis, deep_analysis_manager_);

        // Set up API client logging
        api_client_.set_general_logger([this](LogLevel level, const std::string& msg) {
            log(level, msg);
        });
    }

    ~REAgent() {
        stop();
        if (task_semaphore_) {
            qsem_free(task_semaphore_);
        }
        if (queue_mutex_) {
            qmutex_free(queue_mutex_);
        }
    }

    // Start/stop agent
    void start() {
        if (worker_thread_) return;

        stop_requested_ = false;
        worker_thread_ = qthread_create([](void* ud) -> int {
            static_cast<REAgent*>(ud)->worker_loop();
            return 0;
        }, this);
    }

    void stop() {
        if (!worker_thread_) return;

        stop_requested_ = true;

        // Wake up worker thread so it can shut down
        if (task_semaphore_) {
            qsem_post(task_semaphore_);
        }

        // Wait for thread to finish, might be waiting on API response
        qthread_join(worker_thread_);
        qthread_free(worker_thread_);
        worker_thread_ = nullptr;
    }

    // Task management
    void set_task(const std::string& task) {
        {
            qmutex_locker_t lock(queue_mutex_);
            // Clear any pending tasks
            while (!task_queue_.empty()) {
                task_queue_.pop();
            }
            task_queue_.push(AgentTask::new_task(task));
        }

        // Clear saved state for new task
        saved_state_.valid = false;

        // Update state
        state_.set_task(task);
        change_state(AgentState::Status::Running);

        // Signal worker
        qsem_post(task_semaphore_);
    }

    void resume() {
        if (!state_.is_paused() || !saved_state_.valid) {
            log(LogLevel::WARNING, "Cannot resume - agent is not paused or no saved state");
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
            log(LogLevel::WARNING, "Cannot continue - agent must be completed or idle");
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

    // Callbacks
    void set_log_callback(std::function<void(LogLevel, const std::string&)> callback) {
        log_callback_ = callback;
    }

    void set_message_log_callback(std::function<void(const std::string&, const json&, int)> callback) {
        message_log_callback_ = callback;
        api_client_.set_message_logger(callback);
    }

    void set_final_report_callback(std::function<void(const std::string&)> callback) {
        final_report_callback_ = callback;
    }

    void set_tool_started_callback(std::function<void(const std::string&, const std::string&, const json&)> callback) {
        tool_started_callback_ = callback;
    }

    void set_tool_callback(std::function<void(const std::string&, const std::string&, const json&, const json&)> callback) {
        tool_callback_ = callback;
    }

    void set_state_change_callback(std::function<void(AgentState::Status)> callback) {
        state_change_callback_ = callback;
    }

    // State queries
    AgentState::Status get_status() const { return state_.get_status(); }
    bool is_idle() const { return state_.is_idle(); }
    bool is_running() const { return state_.is_running(); }
    bool is_paused() const { return state_.is_paused(); }
    bool is_completed() const { return state_.is_completed(); }

    // Get current state as JSON
    json get_state_json() const {
        json j;
        j["status"] = static_cast<int>(state_.get_status());
        j["current_task"] = state_.get_task();
        j["conversation"] = conversation_.to_json();
        j["tokens"] = token_tracker_.to_json();
        j["cache_stats"] = cache_stats_.to_json();
        j["memory"] = memory_->export_memory_snapshot();
        j["has_saved_state"] = saved_state_.valid;
        if (saved_state_.valid) {
            auto elapsed = std::chrono::steady_clock::now() - saved_state_.saved_at;
            j["saved_state_age_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        }

        j["context_management"] = {
            {"consolidation_count", context_state_.consolidation_count},
            {"consolidation_in_progress", context_state_.consolidation_in_progress}
        };

        if (context_state_.consolidation_count > 0) {
            auto elapsed = std::chrono::steady_clock::now() - context_state_.last_consolidation;
            j["context_management"]["minutes_since_last_consolidation"] =
                std::chrono::duration_cast<std::chrono::minutes>(elapsed).count();
        }

        // Add estimated tokens for current conversation
        if (saved_state_.valid) {
            j["context_management"]["estimated_current_tokens"] = estimate_request_tokens(saved_state_.request);
        }
        return j;
    }

    json get_thinking_stats() const {
        json stats;
        stats["thinking_enabled"] = config_.agent.enable_thinking;
        stats["interleaved_thinking_possible"] = tool_registry_.has_tools();

        // Get thinking block count from conversation
        int total_thinking_blocks = 0;
        int total_redacted_blocks = 0;

        std::vector<messages::Message> messages = conversation_.get_messages();
        for (const messages::Message& msg: messages) {
            if (msg.role() == messages::Role::Assistant) {
                std::vector<const messages::ThinkingContent*> thinking = messages::ContentExtractor::extract_thinking_blocks(msg);
                std::vector<const messages::RedactedThinkingContent*> redacted = messages::ContentExtractor::extract_redacted_thinking_blocks(msg);
                total_thinking_blocks += thinking.size();
                total_redacted_blocks += redacted.size();
            }
        }

        stats["total_thinking_blocks"] = total_thinking_blocks;
        stats["total_redacted_blocks"] = total_redacted_blocks;
        stats["max_thinking_budget"] = config_.api.max_thinking_tokens;

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
            log(LogLevel::INFO, "Memory saved to " + filename);
        } else {
            log(LogLevel::ERROR, "Failed to save memory to " + filename);
        }
    }

    void load_memory(const std::string& filename) {
        std::ifstream file(filename);
        if (file.is_open()) {
            json snapshot;
            file >> snapshot;
            memory_->import_memory_snapshot(snapshot);
            file.close();
            log(LogLevel::INFO, "Memory loaded from " + filename);
        } else {
            log(LogLevel::ERROR, "Failed to load memory from " + filename);
        }
    }

    // Token statistics
    api::TokenUsage get_token_usage() const {
        return token_tracker_.get_total();
    }

    void reset_token_usage() {
        token_tracker_.reset();
    }

private:
    // Worker thread main loop
    void worker_loop() {
        while (!stop_requested_) {
            AgentTask task;

            // Get next task
            {
                qmutex_locker_t lock(queue_mutex_);
                if (task_queue_.empty()) {
                    // Wait for task with timeout
                    qsem_wait(task_semaphore_, 100);
                    continue;
                }

                task = task_queue_.front();
                task_queue_.pop();
            }

            log(LogLevel::INFO, "worker got task");

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
                log(LogLevel::ERROR, "Exception in worker loop: " + std::string(e.what()));
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
        // Clear conversation for new task
        conversation_.clear();
        api_client_.set_iteration(0);

        // Build initial request with cache control on tools and system
        api::ChatRequestBuilder builder;
        builder.with_model(config_.api.model)
               .with_system_prompt(SYSTEM_PROMPT)
               .with_max_tokens(config_.api.max_tokens)
               .with_max_thinking_tokens(config_.api.max_thinking_tokens)
               .with_temperature(config_.api.temperature)
               .enable_thinking(config_.agent.enable_thinking)
               .enable_interleaved_thinking(config_.agent.enable_interleaved_thinking);

        // Add tools with caching enabled (cache control added automatically in with_tools())
        if (tool_registry_.has_tools()) {
            builder.with_tools(tool_registry_);
        }

        // Add initial user message
        builder.add_message(messages::Message::user_text("Please analyze the binary to answer: " + task));

        api::ChatRequest request = builder.build();

        // Save initial state
        saved_state_.request = request;
        saved_state_.iteration = 0;
        saved_state_.valid = true;
        saved_state_.saved_at = std::chrono::steady_clock::now();

        // Run analysis loop
        run_analysis_loop();
    }



    // Process resume
    void process_resume() {
        if (!saved_state_.valid) {
            log(LogLevel::ERROR, "No valid saved state to resume from");
            change_state(AgentState::Status::Idle);
            return;
        }

        log(LogLevel::INFO, "Resuming from saved state at iteration " + std::to_string(saved_state_.iteration));

        // Continue from saved state
        run_analysis_loop();
    }

    // Process continue
    void process_continue(const std::string& additional) {
        log(LogLevel::INFO, "Continuing with additional instructions: " + additional);

        if (!saved_state_.valid) {
            log(LogLevel::WARNING, "No saved state found while continuing");
            change_state(AgentState::Status::Idle);
            return;
        }

        // Check if we should re-enable or adjust thinking for continuation
        bool had_thinking = saved_state_.request.enable_thinking;
        bool should_use_interleaved = had_thinking && tool_registry_.has_tools();

        // Update thinking settings if needed
        if (should_use_interleaved && !saved_state_.request.enable_interleaved_thinking) {
            saved_state_.request.enable_interleaved_thinking = true;
            log(LogLevel::INFO, "Enabling interleaved thinking for continuation");
        }

        // Add user message to saved request
        messages::Message continue_msg = messages::Message::user_text(additional);

        // Check for cache invalidation scenario
        if (saved_state_.request.enable_thinking && has_non_tool_result_content(continue_msg)) {
            log(LogLevel::WARNING, "Non-tool-result user message will invalidate thinking block cache.");
        }

        saved_state_.request.messages.push_back(continue_msg);
        conversation_.add_message(saved_state_.request.messages.back());

        // Continue analysis
        run_analysis_loop();
    }

    // Helper to check if a message contains non-tool-result content
    bool has_non_tool_result_content(const messages::Message& msg) const {
        if (msg.role() != messages::Role::User) return false;

        for (const auto& content : msg.contents()) {
            if (!dynamic_cast<messages::ToolResultContent*>(content.get())) {
                return true;  // Found non-tool-result content
            }
        }
        return false;
    }

    // Helper to check if a message contains tool results
    bool has_tool_results(const messages::Message& msg) const {
        if (msg.role() != messages::Role::User) return false;

        for (const auto& content : msg.contents()) {
            if (dynamic_cast<messages::ToolResultContent*>(content.get())) {
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

    // Estimate tokens for a request (rough but sufficient for our needs)
    int estimate_request_tokens(const api::ChatRequest& request) const {
        // Convert to JSON and estimate
        // Rough approximation: ~0.75 tokens per character for JSON
        json request_json = request.to_json();
        std::string json_str = request_json.dump();

        // Add some buffer for response
        return static_cast<int>(json_str.length() * 0.75);
    }

    // Check if we need to consolidate context
    bool should_consolidate_context(const api::ChatRequest& request) {
        if (context_state_.consolidation_in_progress) {
            return false;  // Already consolidating
        }

        api::TokenUsage usage = token_tracker_.get_last_usage();
        int total_tokens = usage.input_tokens + usage.output_tokens + usage.cache_read_tokens + usage.cache_creation_tokens;
        return total_tokens > CONTEXT_LIMIT_TOKENS;
    }

    // Trigger context consolidation
    void trigger_context_consolidation() {
        log(LogLevel::WARNING, "Context limit reached. Initiating memory consolidation...");

        context_state_.consolidation_in_progress = true;
        context_state_.consolidation_count++;
        context_state_.last_consolidation = std::chrono::steady_clock::now();

        // Add consolidation message
        messages::Message consolidation_msg = messages::Message::user_text(CONSOLIDATION_PROMPT);
        saved_state_.request.messages.push_back(consolidation_msg);
        conversation_.add_message(consolidation_msg);

        // Mark that we're in consolidation mode
        saved_state_.valid = true;
    }

    ConsolidationResult process_consolidation_response(const messages::Message& response_msg, const std::vector<messages::Message>& tool_results) {
        ConsolidationResult result;

        // Extract stored keys from tool calls
        std::vector<const messages::ToolUseContent*> tool_uses = messages::ContentExtractor::extract_tool_uses(response_msg);
        for (const messages::ToolUseContent *tool_use: tool_uses) {
            if (tool_use->name == "store_analysis" && tool_use->input.contains("key")) {
                result.stored_keys.push_back(tool_use->input["key"]);
            }
        }

        // Extract summary text
        std::optional<std::string> text_content = messages::ContentExtractor::extract_text(response_msg);
        if (text_content) {
            result.summary = *text_content;
            result.success = true;
        } else {
            log(LogLevel::WARNING, "After attempting consolidation, the LLM did not provide a summary");

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
        log(LogLevel::INFO, "Rebuilding conversation with consolidated memory...");

        // Save current task and token stats
        std::string original_task = state_.get_task();
        api::TokenUsage total_usage_before = token_tracker_.get_total();

        // store old tracker session
        tracker_sessions_.emplace_back(std::move(token_tracker_));

        // Clear conversation
        conversation_.clear();

        // Reset token tracker
        reset_token_usage();

        // Build new request with consolidated context
        api::ChatRequestBuilder builder;
        builder.with_model(config_.api.model)
               .with_system_prompt(SYSTEM_PROMPT)
               .with_max_tokens(config_.api.max_tokens)
               .with_max_thinking_tokens(config_.api.max_thinking_tokens)
               .with_temperature(config_.api.temperature)
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

        builder.add_message(messages::Message::user_text(continuation_prompt));

        // Reset saved state with new request
        saved_state_.request = builder.build();
        saved_state_.iteration = 0;  // Reset iteration count
        saved_state_.valid = true;
        saved_state_.saved_at = std::chrono::steady_clock::now();

        // Add initial message to conversation
        conversation_.add_message(saved_state_.request.messages[0]);

        // Log consolidation stats
        log(LogLevel::INFO, std::format(
            "Context consolidated. Stored {} keys. Token usage before: {} in, {} out. Cost so far: ${:.4f}",
            consolidation.stored_keys.size(),
            total_usage_before.input_tokens,
            total_usage_before.output_tokens,
            total_usage_before.estimated_cost()
        ));

        // Mark consolidation complete
        context_state_.consolidation_in_progress = false;
    }

    void apply_incremental_caching() {
        if (saved_state_.request.messages.size() < 2) {
            return;
        }

        // IMPORTANT: We can only have 4 cache breakpoints total
        // We already use 2 for tools and system prompt, so we can only add 2 more

        // First, remove any existing cache controls from messages to avoid exceeding limit
        for (auto& msg : saved_state_.request.messages) {
            messages::Message new_msg(msg.role());
            for (const auto& content : msg.contents()) {
                // Clone content without cache control
                if (auto* text = dynamic_cast<messages::TextContent*>(content.get())) {
                    new_msg.add_content(std::make_unique<messages::TextContent>(text->text));
                } else if (auto* tool_result = dynamic_cast<messages::ToolResultContent*>(content.get())) {
                    new_msg.add_content(std::make_unique<messages::ToolResultContent>(
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
        for (int i = saved_state_.request.messages.size() - 1; i >= 0; i--) {
            const messages::Message& msg = saved_state_.request.messages[i];

            // Find the last user message with tool results
            if (msg.role() == messages::Role::User && has_tool_results(msg)) {
                cache_position = i;
                break;
            }
        }

        if (cache_position >= 0) {
            // Create a new message with cache control on the last content block
            messages::Message& msg_to_modify = saved_state_.request.messages[cache_position];

            // Clone the message and add cache control to the last tool result
            messages::Message new_msg(msg_to_modify.role());
            auto& contents = msg_to_modify.contents();

            for (size_t i = 0; i < contents.size(); i++) {
                if (i == contents.size() - 1) {
                    // Last content block - add cache control
                    if (auto* tool_result = dynamic_cast<messages::ToolResultContent*>(contents[i].get())) {
                        new_msg.add_content(std::make_unique<messages::ToolResultContent>(
                            tool_result->tool_use_id,
                            tool_result->content,
                            tool_result->is_error,
                            messages::CacheControl{messages::CacheControl::Type::Ephemeral}
                        ));
                    } else {
                        new_msg.add_content(contents[i]->clone());
                    }
                } else {
                    new_msg.add_content(contents[i]->clone());
                }
            }

            // Replace the message
            saved_state_.request.messages[cache_position] = std::move(new_msg);
        }
    }

    // Main analysis loop
    void run_analysis_loop() {
        int iteration = saved_state_.iteration;
        bool task_complete = false;

        while (iteration < config_.agent.max_iterations && !stop_requested_ && !task_complete && state_.is_running()) {
            if (stop_requested_) {
                log(LogLevel::INFO, "Analysis interrupted by stop request");
                break;
            }

            iteration++;
            saved_state_.iteration = iteration;
            api_client_.set_iteration(iteration);

            log(LogLevel::INFO, "Iteration " + std::to_string(iteration));

            // Apply caching for continuation
            if (iteration > 1) {
                apply_incremental_caching();
            }

            // Create request for this iteration
            api::ChatRequest current_request = saved_state_.request;

            // Check if we need to consolidate context BEFORE sending
            if (should_consolidate_context(current_request) && !context_state_.consolidation_in_progress) {
                trigger_context_consolidation();
                continue;  // Loop will create consolidation request next iteration
            }

            // Send request
            api::ChatResponse response = api_client_.send_request(current_request);

            if (!response.success) {
                handle_api_error(response);
                break;
            }

            // Log thinking information
            if (response.has_thinking()) {
                std::vector<const messages::ThinkingContent*> thinking_blocks = response.get_thinking_blocks();
                std::vector<const messages::RedactedThinkingContent*> redacted_blocks = response.get_redacted_thinking_blocks();

                log(LogLevel::INFO, std::format("Response contains {} thinking blocks and {} redacted blocks",
                    thinking_blocks.size(), redacted_blocks.size()));

                // Log thinking summary if available
                if (!thinking_blocks.empty()) {
                    log(LogLevel::DEBUG, "Thinking: " + thinking_blocks[0]->thinking);
                }
            }

            validate_thinking_preservation(response);

            // Track token + cache usage
            token_tracker_.add_usage(response.usage);
            log_token_usage(response.usage, iteration);
            update_cache_stats(response.usage);

            // Add response to conversation and saved state
            // IMPORTANT: We must preserve the entire message including thinking blocks
            saved_state_.request.messages.push_back(response.message);
            conversation_.add_message(response.message);

            // Process tool calls
            std::vector<messages::Message> tool_results = process_tool_calls(response.message, iteration);

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
                messages::Message combined_results = messages::Message(messages::Role::User);

                for (const messages::Message& result : tool_results) {
                    for (const std::unique_ptr<messages::Content>& content: result.contents()) {
                        combined_results.add_content(content->clone());
                    }
                }

                saved_state_.request.messages.push_back(combined_results);
                conversation_.add_message(combined_results);
            }

            // Check if task is complete
            task_complete = check_task_complete(response.message);

            // Handle end turn without tools
            if (response.stop_reason == api::StopReason::EndTurn && !response.has_tool_calls() && !task_complete) {
                if (iteration > 1 && !context_state_.consolidation_in_progress) {
                    // Add a user message to continue
                    messages::Message continue_msg = messages::Message::user_text(
                        "Please continue your analysis and use tools to gather more information or submit your final report."
                    );
                    saved_state_.request.messages.push_back(continue_msg);
                    conversation_.add_message(continue_msg);
                }
            }
        }

        if (iteration >= config_.agent.max_iterations && !task_complete) {
            log(LogLevel::WARNING, "Reached maximum iterations without completing task");
            change_state(AgentState::Status::Completed);
        } else if (task_complete) {
            change_state(AgentState::Status::Completed);
        }
    }

    // Update cache statistics
    void update_cache_stats(const api::TokenUsage& usage) {
        if (usage.cache_read_tokens > 0 || usage.cache_creation_tokens > 0 || usage.input_tokens > 0) {
            cache_stats_.total_cache_hits += usage.cache_read_tokens;
            cache_stats_.total_cache_misses += usage.input_tokens;

            if (usage.cache_creation_tokens > 0) {
                cache_stats_.total_cache_writes++;
            }

            // Estimate savings based on the difference in pricing
            if (usage.cache_read_tokens > 0) {
                double saved = usage.cache_read_tokens / 1000000.0 * (get_input_price(usage.model) - get_cache_read_price(usage.model));
                cache_stats_.total_cache_savings += saved;
            }
        }
    }

    double get_input_price(api::Model model) const {
        switch (model) {
            case api::Model::Opus4: return 15.0;
            case api::Model::Sonnet4:
            case api::Model::Sonnet37: return 3.0;
            case api::Model::Haiku35: return 0.8;
        }
        return 0.0;
    }

    double get_cache_read_price(api::Model model) const {
        switch (model) {
            case api::Model::Opus4: return 1.5;
            case api::Model::Sonnet4:
            case api::Model::Sonnet37: return 0.30;
            case api::Model::Haiku35: return 0.08;
        }
        return 0.0;
    }

    // Process tool calls from assistant message
    std::vector<messages::Message> process_tool_calls(const messages::Message& msg, int iteration) {
        std::vector<messages::Message> results;

        std::vector<const messages::ToolUseContent*> tool_calls = messages::ContentExtractor::extract_tool_uses(msg);

        for (const messages::ToolUseContent* tool_use: tool_calls) {
            log(LogLevel::INFO, std::format("Executing tool: {} with input: {}", tool_use->name, tool_use->input.dump()));

            // Track tool call
            conversation_.track_tool_call(tool_use->id, tool_use->name, iteration);

            // Call tool started callback
            if (tool_started_callback_) {
                tool_started_callback_(tool_use->id, tool_use->name, tool_use->input);
            }

            // Execute tool
            messages::Message result_msg = tool_registry_.execute_tool_call(*tool_use);
            results.push_back(result_msg);

            // Call the tool callback
            if (tool_callback_) {
                // Extract result content from the message
                json result_json;
                for (const std::unique_ptr<messages::Content>& content: result_msg.contents()) {
                    if (auto tool_result = dynamic_cast<const messages::ToolResultContent*>(content.get())) {
                        try {
                            result_json = json::parse(tool_result->content);
                        } catch (...) {
                            result_json = {{"content", tool_result->content}};
                        }
                        break;
                    }
                }
                tool_callback_(tool_use->id, tool_use->name, tool_use->input, result_json);
            }

            // Special handling for final report
            if (tool_use->name == "submit_final_report") {
                handle_final_report(tool_use->input["report"]);
            }
        }

        return results;
    }

    // Check if task is complete
    bool check_task_complete(const messages::Message& msg) {
        std::vector<const messages::ToolUseContent*> tool_calls = messages::ContentExtractor::extract_tool_uses(msg);

        for (const messages::ToolUseContent *tool_use: tool_calls) {
            if (tool_use->name == "submit_final_report") {
                return true;
            }
        }

        return false;
    }

    // Handle API errors
    void handle_api_error(const api::ChatResponse& response) {
        if (!response.error) {
            log(LogLevel::ERROR, "Unknown API error");
            change_state(AgentState::Status::Idle);
            last_error_ = "Unknown API error";
            return;
        }

        std::string error_msg = *response.error;

        // Check for thinking-specific errors
        if (error_msg.find("thinking") != std::string::npos ||
            error_msg.find("budget_tokens") != std::string::npos) {
            log(LogLevel::ERROR, "Thinking-related error: " + error_msg);
            log(LogLevel::INFO, "Consider adjusting thinking budget or disabling thinking");
            }

        if (api::AnthropicClient::is_recoverable_error(response)) {
            log(LogLevel::INFO, "You can resume the analysis");
            change_state(AgentState::Status::Paused);
            saved_state_.valid = true;
            last_error_ = "API Error (recoverable): " + error_msg;
        } else {
            log(LogLevel::ERROR, "Unrecoverable API error: " + error_msg);
            change_state(AgentState::Status::Idle);
            saved_state_.valid = false;
            last_error_ = "API Error: " + error_msg;
        }
    }

    void validate_thinking_preservation(const api::ChatResponse& response) {
        if (!response.has_thinking() || !response.has_tool_calls()) {
            return;  // No validation needed
        }

        // Ensure the message being saved includes thinking blocks
        std::vector<const messages::ThinkingContent*> thinking_blocks = response.get_thinking_blocks();
        std::vector<const messages::RedactedThinkingContent*> redacted_blocks = response.get_redacted_thinking_blocks();

        if (thinking_blocks.empty() && redacted_blocks.empty()) {
            log(LogLevel::WARNING, "Tool calls present but no thinking blocks found - this might indicate an issue");
        } else {
            log(LogLevel::DEBUG, std::format("Preserving {} thinking blocks with tool calls",
                thinking_blocks.size() + redacted_blocks.size()));
        }
    }

    // Handle final report
    void handle_final_report(const std::string& report) {
        log(LogLevel::INFO, "=== FINAL REPORT ===");
        log(LogLevel::INFO, report);
        log(LogLevel::INFO, "===================");

        if (final_report_callback_) {
            final_report_callback_(report);
        }

        log(LogLevel::INFO, "Task completed. You can continue with additional instructions");
    }

    // changes and notifies ui of agent state
    void change_state(AgentState::Status new_status) {
        state_.set_status(new_status);

        if (state_change_callback_) {
            log(LogLevel::INFO, "State changed to: " + std::to_string(static_cast<int>(new_status)));
            state_change_callback_(new_status);
        }
    }

    void log(LogLevel level, const std::string& message) {
        if (log_callback_) {
            log_callback_(level, message);
        }
    }

    void log_token_usage(const api::TokenUsage& usage, int iteration) {
        api::TokenUsage total = token_tracker_.get_total();

        // // Debug logging for cache token tracking
        // if (usage.cache_read_tokens > 0 || usage.cache_creation_tokens > 0) {
        //     log(LogLevel::DEBUG, std::format("Cache tokens detected - Read: {}, Write: {}, Model: {}",
        //         usage.cache_read_tokens, usage.cache_creation_tokens, api::model_to_string(usage.model)));
        // }
        //
        // // Debug cost calculation
        // log(LogLevel::DEBUG, std::format("Cost breakdown - Input: ${:.4f}, Output: ${:.4f}, Cache Read: ${:.4f}, Cache Write: ${:.4f}, Total Model: {}",
        //     usage.input_tokens / 1000000.0 * (usage.model == api::Model::Sonnet4 ? 3.0 : 0.0),
        //     usage.output_tokens / 1000000.0 * (usage.model == api::Model::Sonnet4 ? 15.0 : 0.0),
        //     usage.cache_read_tokens / 1000000.0 * (usage.model == api::Model::Sonnet4 ? 0.30 : 0.0),
        //     usage.cache_creation_tokens / 1000000.0 * (usage.model == api::Model::Sonnet4 ? 3.75 : 0.0),
        //     api::model_to_string(total.model)));
        // log(LogLevel::DEBUG, std::format("Total cost: ${:.4f} (Input: {}, Output: {}, Cache Read: {}, Cache Write: {})",
        //     total.estimated_cost(), total.input_tokens, total.output_tokens, total.cache_read_tokens, total.cache_creation_tokens));

        std::stringstream ss;
        ss << "[Iteration " << iteration << "] ";
        ss << "Tokens: " << usage.input_tokens << " in, " << usage.output_tokens << " out";

        ss << " [" << usage.cache_read_tokens << " cache read, " << usage.cache_creation_tokens << " cache write]";

        ss << " | Total: " << total.input_tokens << " in, " << total.output_tokens << " out";
        ss << " [" << total.cache_read_tokens << " cache read, " << total.cache_creation_tokens << " cache write]";
        ss << " | Est. Cost: $" << std::fixed << std::setprecision(4) << total.estimated_cost();

        // Add overall cache statistics
        if (cache_stats_.total_cache_hits + cache_stats_.total_cache_misses > 0) {
            ss << " | Overall Cache: " << std::fixed << std::setprecision(1)
               << (cache_stats_.get_hit_rate() * 100) << "% hit rate";
            ss << ", $" << std::fixed << std::setprecision(4)
               << cache_stats_.total_cache_savings << " saved";
        }

        log(LogLevel::INFO, ss.str());
    }

public:
    // Tool registry access (for testing/extension)
    tools::ToolRegistry& get_tool_registry() { return tool_registry_; }
    const tools::ToolRegistry& get_tool_registry() const { return tool_registry_; }

    // Cache statistics access
    CacheStats get_cache_stats() const { return cache_stats_; }
    void reset_cache_stats() { cache_stats_ = CacheStats{}; }
};

} // namespace llm_re

#endif //AGENT_H