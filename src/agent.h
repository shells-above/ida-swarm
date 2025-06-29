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
    private:
        std::shared_ptr<BinaryMemory> memory;
        std::shared_ptr<ActionExecutor> executor;
        std::shared_ptr<AnthropicClient> anthropic;

        // Thread management
        std::thread worker_thread;
        std::atomic<bool> running;
        std::atomic<bool> stop_requested;
        std::mutex task_mutex;
        std::condition_variable task_cv;

        // Current task
        std::string current_task;
        std::string api_key;

        // Agent state
        std::vector<AnthropicClient::ChatMessage> conversation_history;

        // Worker thread function
        void worker_loop();

        // LLM interaction
        std::string build_system_prompt() const;
        json parse_llm_action(const std::string& response) const;
        std::string format_action_result(const json& result) const;

        // UI callback
        std::function<void(const std::string&)> log_callback;

    public:
        REAgent(const std::string& anthropic_api_key);
        ~REAgent();

        // Start/stop agent
        void start();
        void stop();

        // Set task
        void set_task(const std::string& task);

        // Set logging callback
        void set_log_callback(std::function<void(const std::string&)> callback);

        // Get current state
        std::string get_current_state() const;

        // Save/load memory
        void save_memory(const std::string& filename);
        void load_memory(const std::string& filename);
    };

} // namespace llm_re



#endif //AGENT_H
