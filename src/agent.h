//
// Created by user on 6/29/25.
//

#ifndef AGENT_H
#define AGENT_H

#include "common.h"
#include "memory.h"
#include "actions.h"
#include "anthropic_client.h"
#include <pro.h>  // For IDA threading APIs

namespace llm_re {

    class REAgent {
    private:
        std::shared_ptr<BinaryMemory> memory;
        std::shared_ptr<ActionExecutor> executor;
        std::shared_ptr<AnthropicClient> anthropic;

        // Thread management using IDA's API
        qthread_t worker_thread;
        std::atomic<bool> running;
        std::atomic<bool> stop_requested;
        qmutex_t task_mutex;
        qsemaphore_t task_semaphore;

        // Current task
        std::string current_task;
        std::string api_key;

        // Agent state
        std::vector<AnthropicClient::ChatMessage> conversation_history;

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

        // Worker thread function (needs to be public for thread callback)
        void worker_loop();
    };

} // namespace llm_re

#endif //AGENT_H