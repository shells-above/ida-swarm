//
// Created by user on 6/29/25.
//

#include "anthropic_client.h"

namespace llm_re {

AnthropicClient::AnthropicClient(const std::string& key)
    : api_key(key), api_url("https://api.anthropic.com/v1/messages") {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

AnthropicClient::~AnthropicClient() {
    curl_global_cleanup();
}

size_t AnthropicClient::WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t totalSize = size * nmemb;
    userp->append((char*)contents, totalSize);
    return totalSize;
}

AnthropicClient::ChatResponse AnthropicClient::send_chat_request(const ChatRequest& request) {
    ChatResponse response;
    CURL* curl = curl_easy_init();

    if (!curl) {
        response.success = false;
        response.error = "Failed to initialize CURL";
        return response;
    }

    // Build request JSON
    json request_json;
    request_json["model"] = request.model_sonnet;
    request_json["max_tokens"] = request.max_tokens;
    request_json["temperature"] = request.temperature;
    
    if (request.enable_thinking) {
        // request_json["thinking"] = {
        //     "budget_tokens":
        //
        // };
    }

    if (!request.system_prompt.empty()) {
        // Wrap system prompt in cache control for prompt caching
        request_json["system"] = json::array({
            {
                {"type", "text"},
                {"text", request.system_prompt},
                {"cache_control", {{"type", "ephemeral"}}}  // This enables caching!
            }
        });
    }

    // Build messages array
    json messages = json::array();
    for (const auto& msg : request.messages) {
        json msg_json;

        if (msg.role == "tool") {  // there isn't actually a "tool" role, i store it internally because it makes more sense
            // Tool results must be sent as user messages with tool_result content
            msg_json["role"] = "user";
            json content_array = json::array();

            // Always keep content as string - Anthropic expects string or content blocks
            content_array.push_back({
                {"type", "tool_result"},
                {"tool_use_id", msg.tool_call_id},
                {"content", msg.content}  // This is already a string from result.dump()
            });
            msg_json["content"] = content_array;
        }
        else if (msg.role == "assistant" && !msg.tool_calls.empty()) {
            // Assistant message with tool calls
            msg_json["role"] = msg.role;
            json content_array = json::array();

            // Add text content if present
            if (!msg.content.empty()) {
                content_array.push_back({
                    {"type", "text"},
                    {"text", msg.content}
                });
            }

            // Add tool calls
            for (const auto& tool_call : msg.tool_calls) {
                content_array.push_back(tool_call);
            }

            msg_json["content"] = content_array;
        } else {
            // Regular text message
            msg_json["role"] = msg.role;
            msg_json["content"] = msg.content;
        }

        messages.push_back(msg_json);
    }
    request_json["messages"] = messages;


    // Add tools if provided
    if (!request.tools.empty()) {
        json tools_json = json::array();
        for (const auto& tool : request.tools) {
            json tool_json;
            tool_json["name"] = tool.name;
            tool_json["description"] = tool.description;
            tool_json["input_schema"] = tool.parameters;
            tools_json.push_back(tool_json);
        }
        request_json["tools"] = tools_json;
    }

    // Log the request
    if (message_logger) {
        // Create a simplified version for logging (truncate system prompt after first iteration)
        json log_request = request_json;
        if (current_iteration > 1 && request_json.contains("system") &&
            request_json["system"].get<std::string>().length() > 500) {
            log_request["system"] = "[System prompt truncated - " +
                std::to_string(request_json["system"].get<std::string>().length()) +
                " chars]";
        }
        message_logger("REQUEST", log_request, current_iteration);
    }

    std::string request_body = request_json.dump();
    std::string response_body;

    // Set up CURL
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("x-api-key: " + api_key).c_str());
    headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

    curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    // Perform request
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        response.success = false;
        response.error = "CURL error: " + std::string(curl_easy_strerror(res));

        // Log error
        if (message_logger) {
            json error_log;
            error_log["error"] = response.error;
            message_logger("ERROR", error_log, current_iteration);
        }
    } else {
        // Parse response
        try {
            json response_json = json::parse(response_body);

            if (response_json.contains("error")) {
                response.success = false;
                response.error = response_json["error"]["message"];
            } else {
                response.success = true;
                response.stop_reason = response_json["stop_reason"];

                // Log the response
                if (message_logger) {
                    // Optionally truncate very long content
                    json log_response = response_json;
                    if (log_response.contains("content")) {
                        for (auto& content_item : log_response["content"]) {
                            if (content_item.contains("text") &&
                                content_item["text"].get<std::string>().length() > 1000) {
                                std::string text = content_item["text"];
                                content_item["text"] = text.substr(0, 997) + "...";
                            }
                        }
                    }
                    message_logger("RESPONSE", log_response, current_iteration);
                }

                // Extract thinking if present
                if (response_json.contains("thinking") && !response_json["thinking"].is_null()) {
                    response.thinking = response_json["thinking"];
                }

                // Extract content (can be text or tool calls)
                const auto& content_array = response_json["content"];
                for (const auto& content_item : content_array) {
                    if (content_item["type"] == "text") {
                        response.content = content_item["text"];
                    } else if (content_item["type"] == "tool_use") {
                        response.tool_calls.push_back(content_item);
                    }
                }

                // Extract token usage information
                if (response_json.contains("usage")) {
                    const auto& usage = response_json["usage"];
                    if (usage.contains("input_tokens")) {
                        response.input_tokens = usage["input_tokens"];
                    }
                    if (usage.contains("output_tokens")) {
                        response.output_tokens = usage["output_tokens"];
                    }
                    if (usage.contains("cache_creation_input_tokens")) {
                        response.cache_creation_input_tokens = usage["cache_creation_input_tokens"];
                    }
                    if (usage.contains("cache_read_input_tokens")) {
                        response.cache_read_input_tokens = usage["cache_read_input_tokens"];
                    }
                }
            }
        } catch (const std::exception& e) {
            response.success = false;
            response.error = "JSON parse error: " + std::string(e.what());

            // Log parse error with raw response
            if (message_logger) {
                json error_log;
                error_log["error"] = response.error;
                error_log["raw_response"] = response_body.substr(0, 500);
                message_logger("PARSE_ERROR", error_log, current_iteration);
            }
        }
    }

    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

} // namespace llm_re