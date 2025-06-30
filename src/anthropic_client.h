//
// Created by user on 6/29/25.
//

#ifndef ANTHROPIC_CLIENT_H
#define ANTHROPIC_CLIENT_H

#include "common.h"
#include <curl/curl.h>

namespace llm_re {

    class AnthropicClient {
    private:
        std::string api_key;
        std::string api_url;

        static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp);

    public:
        AnthropicClient(const std::string& key);
        ~AnthropicClient();

        struct ChatMessage {
            std::string role;  // "user" or "assistant"
            std::string content;
        };

        struct ChatRequest {
            std::string model_opus = "claude-opus-4-20250514";
            std::string model_sonnet = "claude-sonnet-4-20250514";
            std::vector<ChatMessage> messages;
            int max_tokens = 4096;
            double temperature = 0.0;
            std::string system_prompt;
        };

        struct ChatResponse {
            bool success;
            std::string content;
            std::string error;
            std::string stop_reason;
        };

        ChatResponse send_chat_request(const ChatRequest& request);
    };

} // namespace llm_re


#endif //ANTHROPIC_CLIENT_H
