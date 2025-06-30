//
// Created by user on 6/29/25.
//

#ifndef AGENT_H
#define AGENT_H

#include "common.h"
#include "memory.h"
#include "actions.h"
#include "anthropic_client.h"

namespace llm_re {

    class REAgent {
    public:
        enum class AgentState {
            IDLE,       // No task
            RUNNING,    // Currently executing
            PAUSED,     // Paused due to error
            COMPLETED   // Task completed
        };

    private:
        std::shared_ptr<BinaryMemory> memory;
        std::shared_ptr<ActionExecutor> executor;
        std::shared_ptr<AnthropicClient> anthropic;

        // Thread management using IDA's API
        qthread_t worker_thread;
        std::atomic<bool> running;
        std::atomic<bool> stop_requested;
        std::atomic<bool> pause_requested;
        std::atomic<AgentState> agent_state{AgentState::IDLE};
        qmutex_t task_mutex;
        qsemaphore_t task_semaphore;

        // Current task
        std::string current_task;
        std::string api_key;

        // Saved state for resume/continue
        AnthropicClient::ChatRequest saved_request;
        int saved_iteration = 0;
        bool has_saved_state = false;

        // Agent state
        std::vector<AnthropicClient::ChatMessage> conversation_history;

        // Token tracking
        struct TokenStats {
            int total_input_tokens = 0;
            int total_output_tokens = 0;
            int total_cache_creation_tokens = 0;
            int total_cache_read_tokens = 0;
            int request_count = 0;
            std::time_t session_start;
            
            TokenStats() : session_start(std::time(nullptr)) {}
            
            int get_total_tokens() const {
                return total_input_tokens + total_output_tokens;
            }
            
            double get_estimated_cost() const {
                // approximate because cache reads are a different price

                // todo track usage per model and switch pricing calc for each

                // Claude Opus 4 pricing (approximate)
                // double input_cost = (total_input_tokens / 1000.0) * 0.015;  // $15 per 1M input tokens
                // double output_cost = (total_output_tokens / 1000.0) * 0.075; // $75 per 1M output tokens

                // Claude Sonnet 4 pricing (approximate)
                double input_cost = (total_input_tokens / 1000.0) * 0.003;  // $3 per 1M input tokens
                double output_cost = (total_output_tokens / 1000.0) * 0.015; // $15 per 1M output tokens

                // Claude Haiku 3.5 pricing (approximate)
                // double input_cost = (total_input_tokens / 1000.0) * 0.0008;  // $0.8 per 1M input tokens
                // double output_cost = (total_output_tokens / 1000.0) * 0.004; // $4 per 1M output tokens

                double cache_write_cost = (total_cache_creation_tokens / 1000.0) * 0.00375; // 1.25x input
                return input_cost + output_cost + cache_write_cost;
            }
        } token_stats;

        // Tool definitions
        std::vector<AnthropicClient::Tool> define_tools() const;

        // LLM interaction
        std::string build_system_prompt() const;

        void log_token_usage(const AnthropicClient::ChatResponse& response, int iteration);

        // UI callback
        std::function<void(const std::string&)> log_callback;
        std::function<void(const std::string&, const json&, int)> llm_message_callback;


    public:
        REAgent(const std::string& anthropic_api_key);
        ~REAgent();

        // Start/stop agent
        void start();
        void stop();

        // Set task
        void set_task(const std::string& task);

        // Resume/continue functionality
        void resume();
        void continue_with_task(const std::string& additional_task);

        // State queries
        bool is_idle() const { return agent_state == AgentState::IDLE; }
        bool is_running() const { return agent_state == AgentState::RUNNING; }
        bool is_paused() const { return agent_state == AgentState::PAUSED; }
        bool is_completed() const { return agent_state == AgentState::COMPLETED; }
        AgentState get_state() const { return agent_state.load(); }

        // Set logging callback
        void set_log_callback(std::function<void(const std::string&)> callback);
        void set_llm_message_callback(std::function<void(const std::string&, const json&, int)> callback);

        // Get current state
        std::string get_current_state() const;
        
        // Token usage
        TokenStats get_token_stats() const { return token_stats; }
        void reset_token_stats() { token_stats = TokenStats(); }

        // Save/load memory
        void save_memory(const std::string& filename);
        void load_memory(const std::string& filename);

        // Worker thread function (needs to be public for thread callback)
        void worker_loop();
    };

} // namespace llm_re

#endif //AGENT_H