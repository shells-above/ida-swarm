//
// Created by user on 6/29/25.
//

#ifndef ANTHROPIC_CLIENT_H
#define ANTHROPIC_CLIENT_H

#include "common.h"

namespace llm_re {

    class AnthropicClient {
    private:
        std::string api_key;
        std::string api_url;

        std::function<void(const std::string&, const json&, int)> message_logger;
        int current_iteration = 0;

        static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);

    public:
        AnthropicClient(const std::string& key);
        ~AnthropicClient();

        // Add setters for logging
        void set_message_logger(std::function<void(const std::string&, const json&, int)> logger) {
            message_logger = logger;
        }
        void set_iteration(int iter) { current_iteration = iter; }

        struct ChatMessage {
            std::string role;  // "user", "assistant", or "tool"
            std::string content;

            // For assistant messages with tool use
            std::vector<json> tool_calls;

            // For tool messages
            std::string tool_call_id;

            ChatMessage(const std::string& r, const std::string& c)
                : role(r), content(c) {}

            explicit ChatMessage(const std::string& tool_id, const json& result)
                : role("tool"), tool_call_id(tool_id) {
                content = result.dump();
            }
        };

        struct Tool {
            std::string name;
            std::string description;
            json parameters;  // JSON Schema for parameters
        };

        struct ChatRequest {
            std::string model_opus = "claude-opus-4-20250514";
            std::string model_sonnet = "claude-sonnet-4-20250514";
            std::vector<ChatMessage> messages;
            std::vector<Tool> tools;
            int max_tokens = 8192;
            double temperature = 0.0;
            std::string system_prompt;
            bool enable_thinking = false;  // todo add support for this with interleaved thinking tool calls (in beta)
        };

        struct ChatResponse {
            bool success;
            std::string content;
            std::string thinking;
            std::string error;
            std::string stop_reason;
            std::vector<json> tool_calls;  // Contains tool use blocks
            int input_tokens = 0;
            int output_tokens = 0;
            int cache_creation_input_tokens = 0;
            int cache_read_input_tokens = 0;
        };

        ChatResponse send_chat_request(const ChatRequest& request);
    };

} // namespace llm_re

#endif //ANTHROPIC_CLIENT_H