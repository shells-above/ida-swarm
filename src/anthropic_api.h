//
// Created by user on 6/30/25.
//

#ifndef ANTHROPIC_API_H
#define ANTHROPIC_API_H

#include "common.h"
#include "message_types.h"

// Forward declaration
namespace llm_re::tools {
    class ToolRegistry;
}

namespace llm_re::api {

// Model selection
enum class Model {
    Opus4,
    Sonnet4,
    Sonnet37,
    Haiku35
};

inline std::string model_to_string(Model model) {
    switch (model) {
        case Model::Opus4: return "claude-opus-4-20250514";
        case Model::Sonnet4: return "claude-sonnet-4-20250514";
        case Model::Sonnet37: return "claude-3-7-sonnet-latest";
        case Model::Haiku35: return "claude-3-5-haiku-latest";
    }
    return "";
}

inline Model model_from_string(const std::string& s) {
    if (s == "claude-opus-4-20250514") return Model::Opus4;
    if (s == "claude-sonnet-4-20250514") return Model::Sonnet4;
    if (s == "claude-3-7-sonnet-latest") return Model::Sonnet37;
    if (s == "claude-3-5-haiku-latest") return Model::Haiku35;
    throw std::runtime_error("Unknown model: " + s);
}

// Stop reason enum
enum class StopReason {
    EndTurn,
    MaxTokens,
    StopSequence,
    ToolUse,
    Unknown
};

inline StopReason stop_reason_from_string(const std::string& s) {
    if (s == "end_turn") return StopReason::EndTurn;
    if (s == "max_tokens") return StopReason::MaxTokens;
    if (s == "stop_sequence") return StopReason::StopSequence;
    if (s == "tool_use") return StopReason::ToolUse;
    return StopReason::Unknown;
}

// Token usage tracking
struct TokenUsage {
    int input_tokens = 0;
    int output_tokens = 0;
    int cache_creation_tokens = 0;
    int cache_read_tokens = 0;
    Model model;

    /**
     * sums 2 TokenUsage objects and returns a new object
     * @param other other instance of TokenUsage
     * @return returns new summed TokenUsage
     */
    TokenUsage operator+(const TokenUsage& other) const {
        TokenUsage result = {
            input_tokens + other.input_tokens,
            output_tokens + other.output_tokens,
            cache_creation_tokens + other.cache_creation_tokens,
            cache_read_tokens + other.cache_read_tokens,
            model  // Preserve model from this instance
        };
        return result;
    }

    /**
     * adds another TokneUsage object to this one
     * @param other other instance of TokenUsage
     * @return returns this
     */
    TokenUsage& operator+=(const TokenUsage& other) {
        // Set model from the first non-zero usage (preserve model context)
        bool was_empty = (input_tokens == 0 && output_tokens == 0 && cache_creation_tokens == 0 && cache_read_tokens == 0);
        
        input_tokens += other.input_tokens;
        output_tokens += other.output_tokens;
        cache_creation_tokens += other.cache_creation_tokens;
        cache_read_tokens += other.cache_read_tokens;
        
        // If this was empty, adopt the model from the other usage
        if (was_empty) {
            model = other.model;
        }
        return *this;
    }

    double estimated_cost() const {
        double price_input, price_output, price_cache_write, price_cache_read;

        switch (model) {
            case Model::Opus4:
                price_input = 15.0;
                price_output = 75.0;
                price_cache_write = 18.75;
                price_cache_read = 1.5;
                break;
            case Model::Sonnet4:
            case Model::Sonnet37:
                price_input = 3.0;
                price_output = 15.0;
                price_cache_write = 3.75;
                price_cache_read = 0.30;
                break;
            case Model::Haiku35:
                price_input = 0.8;
                price_output = 4.0;
                price_cache_write = 1.0;
                price_cache_read = 0.08;
                break;
        }

        return (input_tokens / 1000000.0 * price_input) +
               (output_tokens / 1000000.0 * price_output) +
               (cache_creation_tokens / 1000000.0 * price_cache_write) +
               (cache_read_tokens / 1000000.0 * price_cache_read);
    }

    static TokenUsage from_json(const json& j) {
        TokenUsage usage;
        if (j.contains("input_tokens")) usage.input_tokens = j["input_tokens"];
        if (j.contains("output_tokens")) usage.output_tokens = j["output_tokens"];
        if (j.contains("cache_creation_input_tokens")) usage.cache_creation_tokens = j["cache_creation_input_tokens"];
        if (j.contains("cache_read_input_tokens")) usage.cache_read_tokens = j["cache_read_input_tokens"];
        if (j.contains("model")) usage.model = model_from_string(j["model"]);
        return usage;
    }

    // Fallback method for cases where model isn't in JSON
    static TokenUsage from_json(const json& j, Model model) {
        TokenUsage usage = from_json(j);
        if (!j.contains("model")) {
            usage.model = model;
        }
        return usage;
    }

    json to_json() const {
        return {
            {"input_tokens", input_tokens},
            {"output_tokens", output_tokens},
            {"cache_creation_input_tokens", cache_creation_tokens},
            {"cache_read_input_tokens", cache_read_tokens},
            {"model", model_to_string(model)}
        };
    }
};

// System prompt with cache control
struct SystemPrompt {
    std::string text;

    json to_json() const {
        if (text.empty()) return json();

        json system_array = json::array();
        json system_obj = {
            {"type", "text"},
            {"text", text}
        };

        system_obj["cache_control"] = {{"type", "ephemeral"}};

        system_array.push_back(system_obj);
        return system_array;
    }
};

// Structured chat request
class ChatRequest {
public:
    Model model = Model::Sonnet4;
    SystemPrompt system_prompt;
    json multiple_system_prompts;  // for multiple cache breakpoints
    std::vector<messages::Message> messages;
    std::vector<json> tool_definitions;
    int max_tokens = 8192;
    int max_thinking_tokens = 2048;
    double temperature = 0.0;
    bool enable_thinking = false;
    bool enable_interleaved_thinking = false;
    std::vector<std::string> stop_sequences;


    /**
     * performs basic validation of the current settings + message history
     */
    void validate() const {
        if (messages.empty()) {
            throw std::runtime_error("ChatRequest must have at least one message");
        }

        // Check for proper role alternation
        messages::Role last_role = messages::Role::System;
        for (const messages::Message &msg: messages) {
            if (msg.role() == last_role && last_role != messages::Role::System) {
                throw std::runtime_error("Adjacent messages with same role detected");
            }
            last_role = msg.role();
        }

        if (max_tokens <= 0 || max_tokens > 200000) {
            throw std::runtime_error("max_tokens must be between 1 and 200000");
        }

        if (temperature < 0.0 || temperature > 1.0) {
            throw std::runtime_error("temperature must be between 0.0 and 1.0");
        }

        if (enable_thinking) {
            if (max_thinking_tokens < 1024) {
                throw std::runtime_error("max_thinking_tokens must be at least 1024 when thinking is enabled");
            }
            if (max_thinking_tokens > max_tokens) {
                throw std::runtime_error("max_thinking_tokens cannot exceed max_tokens");
            }
            // Check model compatibility
            if (model == Model::Haiku35) {
                throw std::runtime_error("Extended thinking is not supported on Haiku 3.5 model");
            }
            // Temperature restrictions with thinking
            if (temperature != 1.0) {
                throw std::runtime_error("temperature must be 1.0 when thinking is enabled (temperature and top_k are not compatible with thinking)");
            }
        }

        if (enable_interleaved_thinking) {
            if (!enable_thinking) {
                throw std::runtime_error("enable_interleaved_thinking requires enable_thinking to be true");
            }
            if (model == Model::Sonnet37) {
                throw std::runtime_error("Interleaved thinking is only supported on Claude 4 models (Opus 4, Sonnet 4)");
            }
            if (model == Model::Haiku35) {
                throw std::runtime_error("Interleaved thinking is only supported on Claude 4 models (Opus 4, Sonnet 4)");
            }
        }
    }

    json to_json() const {
        json j;
        j["model"] = model_to_string(model);
        j["max_tokens"] = max_tokens;
        j["temperature"] = temperature;

        // if using prompt caching (you should be)
        // order is very important, should be tools -> system -> messages

        // tools
        if (!tool_definitions.empty()) {
            json tools = tool_definitions;
            // Add cache control to the last tool
            tools.back()["cache_control"] = {{"type", "ephemeral"}};
            j["tools"] = tools;
        }

        // system
        if (!multiple_system_prompts.is_null() && !multiple_system_prompts.empty()) {
            j["system"] = multiple_system_prompts;
        } else {
            json system_json = system_prompt.to_json();
            if (!system_json.is_null()) {
                j["system"] = system_json;
            }
        }

        // messages
        json messages_array = json::array();
        for (const messages::Message &msg: messages) {
            messages_array.push_back(msg.to_json());
        }
        j["messages"] = messages_array;

        // Optional parameters
        if (!stop_sequences.empty()) {
            j["stop_sequences"] = stop_sequences;
        }

        if (enable_thinking) {
            // Enable thinking/reasoning
            // Interleaved thinking is automatically enabled via beta header when tools are used
            j["thinking"] = {
                {"type", "enabled"},
                {"budget_tokens", max_thinking_tokens}
            };
        }

        return j;
    }
};

// Structured chat response
struct ChatResponse {
    bool success = false;
    std::optional<std::string> error;
    StopReason stop_reason = StopReason::Unknown;
    messages::Message message{messages::Role::Assistant};
    TokenUsage usage;
    std::string model_used;
    std::string response_id;

    // Helper methods
    bool has_tool_calls() const {
        return message.has_tool_calls();
    }

    std::vector<const messages::ToolUseContent*> get_tool_calls() const {
        return messages::ContentExtractor::extract_tool_uses(message);
    }

    std::optional<std::string> get_text() const {
        return messages::ContentExtractor::extract_text(message);
    }

    std::vector<const messages::ThinkingContent*> get_thinking_blocks() const {
        return messages::ContentExtractor::extract_thinking_blocks(message);
    }

    std::vector<const messages::RedactedThinkingContent*> get_redacted_thinking_blocks() const {
        return messages::ContentExtractor::extract_redacted_thinking_blocks(message);
    }

    bool has_thinking() const {
        auto thinking_blocks = get_thinking_blocks();
        auto redacted_blocks = get_redacted_thinking_blocks();
        return !thinking_blocks.empty() || !redacted_blocks.empty();
    }

    // Get combined thinking text (for backward compatibility)
    std::optional<std::string> get_thinking_text() const {
        auto thinking_blocks = get_thinking_blocks();
        if (!thinking_blocks.empty()) {
            std::string combined;
            for (const auto* block : thinking_blocks) {
                if (!combined.empty()) combined += "\n\n";
                combined += block->thinking;
            }
            return combined;
        }
        return std::nullopt;
    }

    // Create an assistant message with preserved thinking blocks for tool use continuation
    // This is ESSENTIAL when using tools with thinking enabled - the thinking blocks must be
    // passed back to the API to maintain reasoning continuity
    messages::Message to_assistant_message() const {
        return message;  // The message already contains all content blocks including thinking
    }

    static ChatResponse from_json(const json& response_json) {
        ChatResponse response;

        if (response_json.contains("error")) {
            response.success = false;
            if (response_json["error"].is_object()) {
                response.error = response_json["error"]["message"];
            } else {
                response.error = response_json["error"];
            }
            return response;
        }

        response.success = true;

        if (response_json.contains("id")) {
            response.response_id = response_json["id"];
        }

        if (response_json.contains("model")) {
            response.model_used = response_json["model"];
        }

        if (response_json.contains("stop_reason")) {
            response.stop_reason = stop_reason_from_string(response_json["stop_reason"]);
        }

        if (response_json.contains("usage")) {
            response.usage = TokenUsage::from_json(response_json["usage"], model_from_string(response.model_used));
        }

        // Parse content into message
        if (response_json.contains("content")) {
            for (const auto& content_item : response_json["content"]) {
                if (!content_item.contains("type")) continue;

                std::string type = content_item["type"];
                if (type == "text") {
                    response.message.add_content(std::make_unique<messages::TextContent>(content_item["text"]));
                } else if (type == "tool_use") {
                    response.message.add_content(std::make_unique<messages::ToolUseContent>(
                        content_item["id"],
                        content_item["name"],
                        content_item["input"]
                    ));
                } else if (type == "thinking") {
                    std::string thinking_text = content_item["thinking"];
                    if (content_item.contains("signature")) {
                        response.message.add_content(std::make_unique<messages::ThinkingContent>(
                            thinking_text, content_item["signature"]
                        ));
                    } else {
                        response.message.add_content(std::make_unique<messages::ThinkingContent>(thinking_text));
                    }
                } else if (type == "redacted_thinking") {
                    response.message.add_content(std::make_unique<messages::RedactedThinkingContent>(
                        content_item["data"]
                    ));
                }
            }
        }

        return response;
    }
};

// Request builder for fluent API
class ChatRequestBuilder {
    ChatRequest request;

public:
    ChatRequestBuilder& with_model(Model model) {
        request.model = model;
        return *this;
    }

    ChatRequestBuilder& with_system_prompt(const std::string& prompt) {
        request.system_prompt = {prompt};
        return *this;
    }

    ChatRequestBuilder& add_message(const messages::Message& msg) {
        request.messages.push_back(msg);
        return *this;
    }

    ChatRequestBuilder& add_messages(const std::vector<messages::Message>& msgs) {
        request.messages.insert(request.messages.end(), msgs.begin(), msgs.end());
        return *this;
    }

    ChatRequestBuilder& with_tools(const tools::ToolRegistry& registry);

    ChatRequestBuilder& with_max_tokens(int tokens) {
        request.max_tokens = tokens;
        return *this;
    }

    ChatRequestBuilder& with_max_thinking_tokens(int tokens) {
        request.max_thinking_tokens = tokens;
        return *this;
    }

    ChatRequestBuilder& with_temperature(double temp) {
        request.temperature = temp;
        return *this;
    }

    ChatRequestBuilder& with_stop_sequences(const std::vector<std::string>& sequences) {
        request.stop_sequences = sequences;
        return *this;
    }

    ChatRequestBuilder& enable_thinking(bool enable = true) {
        request.enable_thinking = enable;
        return *this;
    }

    ChatRequestBuilder& enable_interleaved_thinking(bool enable = true) {
        request.enable_interleaved_thinking = enable;
        return *this;
    }

    ChatRequest build() {
        request.validate();
        return request;
    }
};

// Error types for better error handling
enum class ErrorType {
    NetworkError,
    RateLimitError,
    ServerError,
    AuthenticationError,
    InvalidRequestError,
    ParseError,
    Unknown
};

struct ApiError {
    ErrorType type;
    std::string message;
    std::optional<int> status_code;
    std::optional<int> retry_after_seconds;

    bool is_recoverable() const {
        return type == ErrorType::RateLimitError ||
               type == ErrorType::ServerError ||
               (type == ErrorType::NetworkError && message.find("timeout") != std::string::npos);
    }

    static ApiError from_response(const std::string& error_msg, int status_code = 0, const std::map<std::string, std::string>& headers = {}) {
        ApiError error;
        error.message = error_msg;
        error.status_code = status_code;

        // Detect error type from message and status code
        if (status_code == 429 || error_msg.find("rate limit") != std::string::npos) {
            error.type = ErrorType::RateLimitError;

            // Extract retry-after from headers if available
            auto retry_after_it = headers.find("retry-after");
            if (retry_after_it != headers.end()) {
                try {
                    error.retry_after_seconds = std::stoi(retry_after_it->second);
                } catch (const std::exception&) {
                    error.retry_after_seconds = 60; // Default fallback
                }
            } else {
                error.retry_after_seconds = 60; // Default fallback
            }
        } else if (error_msg.find("Overloaded") != std::string::npos) {
            error.type = ErrorType::ServerError;
        } else if (status_code == 401) {
            error.type = ErrorType::AuthenticationError;
        } else if (status_code >= 500) {
            error.type = ErrorType::ServerError;
        } else if (status_code >= 400) {
            error.type = ErrorType::InvalidRequestError;
        } else if (error_msg.find("CURL error") != std::string::npos) {
            error.type = ErrorType::NetworkError;
        } else if (error_msg.find("JSON parse error") != std::string::npos) {
            error.type = ErrorType::ParseError;
        } else {
            error.type = ErrorType::Unknown;
        }

        return error;
    }
};

// Clean API client
class AnthropicClient {
    std::string api_key;
    std::string api_url = "https://api.anthropic.com/v1/messages";

    // Logging
    std::function<void(const std::string&, const json&, int)> message_logger;
    std::function<void(LogLevel, const std::string&)> general_logger;
    int current_iteration = 0;

    // Request tracking
    struct RequestStats {
        int total_requests = 0;
        int successful_requests = 0;
        int failed_requests = 0;
        std::chrono::steady_clock::time_point last_request_time;
        TokenUsage total_usage;
    } stats;

    // CURL callbacks
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        size_t totalSize = size * nmemb;
        userp->append((char*)contents, totalSize);
        return totalSize;
    }

    static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, std::map<std::string, std::string>* headers) {
        size_t totalSize = size * nitems;
        std::string header(buffer, totalSize);

        // Find the colon separator
        size_t colonPos = header.find(':');
        if (colonPos != std::string::npos) {
            std::string key = header.substr(0, colonPos);
            std::string value = header.substr(colonPos + 1);

            // Trim whitespace
            key.erase(0, key.find_first_not_of(" \t\r\n"));
            key.erase(key.find_last_not_of(" \t\r\n") + 1);
            value.erase(0, value.find_first_not_of(" \t\r\n"));
            value.erase(value.find_last_not_of(" \t\r\n") + 1);

            // Convert key to lowercase for case-insensitive comparison
            std::transform(key.begin(), key.end(), key.begin(), ::tolower);

            (*headers)[key] = value;
        }

        return totalSize;
    }

    void log(LogLevel level, const std::string& message) const {
        if (general_logger) {
            general_logger(level, message);
        }
    }

    // Helper to sanitize logs
    json sanitize_for_logging(const json& j, int max_depth = 3) const {
        if (max_depth <= 0) return "[truncated]";

        if (j.is_object()) {
            json result;
            for (auto& [key, value] : j.items()) {
                if (key == "system" && value.is_array() && !value.empty()) {
                    // Truncate system prompt for logging
                    result[key] = "[System prompt - " + std::to_string(value[0]["text"].get<std::string>().length()) + " chars]";
                } else if (key == "tools" && value.is_array()) {
                    result[key] = "[" + std::to_string(value.size()) + " tools defined]";
                } else if (key == "messages" && value.is_array() && value.size() > 5) {
                    // Only show recent messages
                    json recent;
                    for (size_t i = value.size() - 3; i < value.size(); ++i) {
                        recent.push_back(sanitize_for_logging(value[i], max_depth - 1));
                    }
                    result[key] = recent;
                    result["_message_count"] = value.size();
                } else if (key == "content" && value.is_string() && value.get<std::string>().length() > 1000) {
                    result[key] = value.get<std::string>().substr(0, 1000) + "... [truncated]";
                } else {
                    result[key] = sanitize_for_logging(value, max_depth - 1);
                }
            }
            return result;
        } else if (j.is_array() && j.size() > 10) {
            json result = json::array();
            for (size_t i = 0; i < 5; ++i) {
                result.push_back(sanitize_for_logging(j[i], max_depth - 1));
            }
            result.push_back("... " + std::to_string(j.size() - 5) + " more items");
            return result;
        }

        return j;
    }

public:
    explicit AnthropicClient(const std::string& key, const std::string& base_url = "https://api.anthropic.com/v1/messages")
        : api_key(key), api_url(base_url) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~AnthropicClient() {
        curl_global_cleanup();
    }

    void set_message_logger(std::function<void(const std::string&, const json&, int)> logger) {
        message_logger = logger;
    }

    void set_general_logger(std::function<void(LogLevel, const std::string&)> logger) {
        general_logger = logger;
    }

    void set_iteration(int iter) {
        current_iteration = iter;
    }

    RequestStats get_stats() const {
        return stats;
    }

    ChatResponse send_request(const ChatRequest& request) {
        stats.total_requests++;
        stats.last_request_time = std::chrono::steady_clock::now();

        json request_json = request.to_json();

        // Log the request
        if (message_logger) {
            json log_json = sanitize_for_logging(request_json);
            log_json["_iteration"] = current_iteration;
            message_logger("REQUEST", log_json, current_iteration);
        }

        // Perform HTTP request
        CURL* curl = curl_easy_init();
        if (!curl) {
            stats.failed_requests++;
            ChatResponse response;
            response.success = false;
            response.error = "Failed to initialize CURL";
            return response;
        }

        std::string request_body = request_json.dump();
        std::string response_body;
        std::map<std::string, std::string> response_headers;
        long http_code = 0;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, ("x-api-key: " + api_key).c_str());
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");

        // Add interleaved thinking beta header if enabled and tools are being used
        if (request.enable_interleaved_thinking && request.enable_thinking && !request.tool_definitions.empty()) {
            headers = curl_slist_append(headers, "anthropic-beta: interleaved-thinking-2025-05-14");
        }

        curl_easy_setopt(curl, CURLOPT_URL, api_url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        // curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300L);  // 5 minute timeout

        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        ChatResponse response;

        if (res != CURLE_OK) {
            stats.failed_requests++;
            response.success = false;
            response.error = "CURL error: " + std::string(curl_easy_strerror(res));

            if (message_logger) {
                json error_log;
                error_log["error"] = response.error.value();
                error_log["curl_code"] = res;
                message_logger("ERROR", error_log, current_iteration);
            }

            return response;
        }

        // Parse response
        try {
            json response_json = json::parse(response_body);

            if (message_logger) {
                json log_json = sanitize_for_logging(response_json);
                log_json["_iteration"] = current_iteration;
                log_json["_http_code"] = http_code;
                message_logger("RESPONSE", log_json, current_iteration);
            }

            response = ChatResponse::from_json(response_json);

            if (response.success) {
                stats.successful_requests++;
                stats.total_usage += response.usage;
            } else {
                stats.failed_requests++;

                // Enhance error information
                ApiError api_error = ApiError::from_response(
                    response.error.value_or("Unknown error"),
                    static_cast<int>(http_code),
                    response_headers
                );

                if (api_error.is_recoverable()) {
                    std::string log_message = "Recoverable API error: " + api_error.message;
                    if (api_error.type == ErrorType::RateLimitError && api_error.retry_after_seconds) {
                        log_message += " (retry after " + std::to_string(*api_error.retry_after_seconds) + " seconds)";
                    }
                    log(LogLevel::WARNING, log_message);
                } else {
                    log(LogLevel::ERROR, "API error: " + api_error.message);
                }
            }

        } catch (const std::exception& e) {
            stats.failed_requests++;
            response.success = false;
            response.error = "JSON parse error: " + std::string(e.what());

            if (message_logger) {
                json error_log;
                error_log["error"] = response.error.value();
                error_log["raw_response"] = response_body.substr(0, 500);
                error_log["http_code"] = http_code;
                message_logger("PARSE_ERROR", error_log, current_iteration);
            }
        }

        return response;
    }

    // Convenience method to check if an error is recoverable
    static bool is_recoverable_error(const ChatResponse& response) {
        if (response.success) return false;
        if (!response.error) return false;

        ApiError api_error = ApiError::from_response(*response.error);
        return api_error.is_recoverable();
    }
};

// Session token tracker
class TokenTracker {
    TokenUsage session_total;
    std::chrono::steady_clock::time_point session_start;
    std::vector<std::pair<std::chrono::steady_clock::time_point, TokenUsage>> history;

public:
    TokenTracker() : session_start(std::chrono::steady_clock::now()) {}

    void add_usage(const TokenUsage& usage) {
        session_total += usage;
        history.emplace_back(std::chrono::steady_clock::now(), usage);
    }

    TokenUsage get_total() const {
        return session_total;
    }

    TokenUsage get_last_usage() const {
        return history.back().second;
    }

    double get_session_duration_minutes() const {
        auto now = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::minutes>(now - session_start);
        return duration.count();
    }

    json to_json() const {
        json j;
        j["session_total"] = session_total.to_json();
        j["session_duration_minutes"] = get_session_duration_minutes();
        j["request_count"] = history.size();
        return j;
    }

    void reset() {
        session_total = TokenUsage{};
        session_start = std::chrono::steady_clock::now();
        history.clear();
    }
};

} // namespace llm_re::api

#endif //ANTHROPIC_API_H