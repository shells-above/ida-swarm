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
    request_json["model"] = request.model;
    request_json["max_tokens"] = request.max_tokens;
    request_json["temperature"] = request.temperature;

    if (!request.system_prompt.empty()) {
        request_json["system"] = request.system_prompt;
    }

    json messages = json::array();
    for (const auto& msg : request.messages) {
        json msg_json;
        msg_json["role"] = msg.role;
        msg_json["content"] = msg.content;
        messages.push_back(msg_json);
    }
    request_json["messages"] = messages;

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
    } else {
        // Parse response
        try {
            json response_json = json::parse(response_body);

            if (response_json.contains("error")) {
                response.success = false;
                response.error = response_json["error"]["message"];
            } else {
                response.success = true;
                response.content = response_json["content"][0]["text"];
                response.stop_reason = response_json["stop_reason"];
            }
        } catch (const std::exception& e) {
            response.success = false;
            response.error = "JSON parse error: " + std::string(e.what());
        }
    }

    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return response;
}

} // namespace llm_re

