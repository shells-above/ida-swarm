//
// Created by user on 6/30/25.
//

#ifndef CLAUDE_CLIENT_H
#define CLAUDE_CLIENT_H

#include "../common.h"
#include "../messages/types.h"
#include "../tools/registry.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <format>
#include <thread>
#include <curl/curl.h>

namespace claude {

// Authentication methods
enum class AuthMethod {
    API_KEY,
    OAUTH
};

// OAuth constants
constexpr int OAUTH_REDIRECT_PORT = 54545;
constexpr const char* OAUTH_CLIENT_ID = "9d1c250a-e61b-44d9-88ed-5944d1962f5e";
constexpr const char* OAUTH_AUTH_URL = "https://claude.ai/oauth/authorize";
constexpr const char* OAUTH_TOKEN_URL = "https://console.anthropic.com/v1/oauth/token";
constexpr const char* OAUTH_SUCCESS_URL = "https://console.anthropic.com/oauth/code/success";
constexpr const char* CLAUDE_CODE_SYSTEM_PROMPT = "You are Claude Code, Anthropic's official CLI for Claude.";
constexpr const char* CLAUDE_CODE_BETA_HEADER = "claude-code-20250219";
constexpr const char* OAUTH_BETA_HEADER = "oauth-2025-04-20";

// Stainless SDK headers
// you can send whatever data you want but i prefer at least keeping it kind of real
constexpr const char* USER_AGENT = "claude-cli/1.0.64 (external, cli)";
constexpr const char* STAINLESS_PACKAGE_VERSION = "0.55.1";
#ifdef __APPLE__
constexpr const char* STAINLESS_OS = "MacOS";
#elif _WIN32
constexpr const char* STAINLESS_OS = "Windows";
#else
constexpr const char* STAINLESS_OS = "Linux";
#endif

#ifdef __aarch64__
constexpr const char* STAINLESS_ARCH = "arm64";
#elif __x86_64__
constexpr const char* STAINLESS_ARCH = "x64";
#else
constexpr const char* STAINLESS_ARCH = "unknown";
#endif

// Model selection
enum class Model {
    Opus41,
    Sonnet4,
    Haiku35
};

inline std::string model_to_string(Model model) {
    switch (model) {
        case Model::Opus41: return "claude-opus-4-1-20250805";
        case Model::Sonnet4: return "claude-sonnet-4-20250514";
        case Model::Haiku35: return "claude-3-5-haiku-latest";
    }
    return "";
}

inline Model model_from_string(const std::string& s) {
    if (s.starts_with("claude-opus-4-1-")) return Model::Opus41;
    if (s.starts_with("claude-sonnet-4-")) return Model::Sonnet4;
    if (s.starts_with("claude-3-5-haiku-")) return Model::Haiku35;
    throw std::runtime_error("Unknown model: " + s);
}

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

    // Delegate to PricingModel for cost calculation
    double estimated_cost() const;  // Implementation moved to pricing.h to avoid circular dependency

    static TokenUsage from_json(const json& j, Model model = Model::Sonnet4) {
        TokenUsage usage;
        if (j.contains("input_tokens")) usage.input_tokens = j["input_tokens"];
        if (j.contains("output_tokens")) usage.output_tokens = j["output_tokens"];
        if (j.contains("cache_creation_input_tokens")) usage.cache_creation_tokens = j["cache_creation_input_tokens"];
        if (j.contains("cache_read_input_tokens")) usage.cache_read_tokens = j["cache_read_input_tokens"];
        
        // Use model from JSON if available, otherwise use provided default
        if (j.contains("model")) {
            usage.model = model_from_string(j["model"]);
        } else {
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
    bool enable_thinking = true;
    bool enable_interleaved_thinking = false;
    std::vector<std::string> stop_sequences;


    /**
     * performs basic validation of the current settings + message history
     */
    void validate() const {
        if (messages.empty()) {
            throw std::runtime_error("ChatRequest must have at least one message");
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
                    std::string text = content_item["text"];
                    if (!text.empty()) {
                        response.message.add_content(std::make_unique<messages::TextContent>(text));
                    }
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
                // Try to parse retry-after from error message
                // Look for pattern like "retry after 12010 seconds"
                size_t retry_pos = error_msg.find("retry after ");
                if (retry_pos != std::string::npos) {
                    size_t num_start = retry_pos + 12; // length of "retry after "
                    size_t num_end = error_msg.find(" seconds", num_start);
                    if (num_end != std::string::npos) {
                        try {
                            int seconds = std::stoi(error_msg.substr(num_start, num_end - num_start));
                            error.retry_after_seconds = seconds;
                        } catch (const std::exception&) {
                            error.retry_after_seconds = 60; // Default fallback
                        }
                    } else {
                        error.retry_after_seconds = 60; // Default fallback
                    }
                } else {
                    error.retry_after_seconds = 60; // Default fallback
                }
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

// OAuth credentials structure
struct OAuthCredentials {
    std::string access_token;
    std::string refresh_token;
    double expires_at = 0;  // Unix timestamp
    std::string account_uuid;
    
    bool is_expired(int buffer_seconds = 300) const {
        auto now = std::chrono::system_clock::now();
        auto now_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        return now_timestamp + buffer_seconds >= expires_at;
    }
};

// Clean API client
class Client {
    // Authentication
    AuthMethod auth_method = AuthMethod::API_KEY;
    std::string api_key;
    std::shared_ptr<OAuthCredentials> oauth_creds;
    
    std::string api_url = "https://api.anthropic.com/v1/messages";
    std::string request_log_filename;  // Optional filename for request logging (will be in /tmp/)
    bool first_log_write = true;  // Track if this is the first write to clear the log file

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

    // Generate a timestamped log filename
    static std::string generate_timestamp_log_filename() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << "anthropic_requests_"
           << std::put_time(std::localtime(&time_t), "%Y%m%d_%H%M%S")
           << ".log";
        return ss.str();
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
    // Constructor for API key authentication
    explicit Client(const std::string& key, const std::string& base_url = "https://api.anthropic.com/v1/messages", const std::string& log_filename = "")
        : auth_method(AuthMethod::API_KEY), api_key(key), api_url(base_url) {
        request_log_filename = log_filename.empty() ? generate_timestamp_log_filename() : log_filename;
    }
    
    // Constructor for OAuth authentication
    Client(std::shared_ptr<OAuthCredentials> creds, const std::string& base_url = "https://api.anthropic.com/v1/messages", const std::string& log_filename = "")
        : auth_method(AuthMethod::OAUTH), oauth_creds(creds), api_url(base_url) {
        request_log_filename = log_filename.empty() ? generate_timestamp_log_filename() : log_filename;
    }

    ~Client() {
    }
    
    // Set authentication method and credentials
    void set_api_key(const std::string& key) {
        auth_method = AuthMethod::API_KEY;
        api_key = key;
    }
    
    void set_oauth_credentials(std::shared_ptr<OAuthCredentials> creds) {
        auth_method = AuthMethod::OAUTH;
        oauth_creds = creds;
    }
    
    AuthMethod get_auth_method() const {
        return auth_method;
    }
    
    // Set the request log filename (will be in /tmp/)
    void set_request_log_filename(const std::string& filename) {
        request_log_filename = filename;
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

    ChatResponse send_request_with_retry(ChatRequest request) {
        const int MAX_RETRIES = 5;
        const int BASE_DELAY_MS = 1000;  // Start with 1 second
        
        for (int attempt = 0; attempt <= MAX_RETRIES; attempt++) {
            ChatResponse response = send_request_internal(request);
            
            // Success or non-recoverable error - return immediately
            if (response.success || !is_recoverable_error(response)) {
                return response;
            }
            
            // Check if we've exhausted retries
            if (attempt == MAX_RETRIES) {
                log(LogLevel::ERROR, std::format("Max retries ({}) reached for API request", MAX_RETRIES));
                return response;
            }
            
            // Recoverable error - prepare for retry
            ApiError api_error = ApiError::from_response(
                response.error.value_or("Unknown error"),
                0,  // status_code is already embedded in the error
                {}
            );
            
            // Calculate delay with exponential backoff
            int delay_ms = BASE_DELAY_MS * (1 << attempt);  // 1s, 2s, 4s, 8s, 16s

            // If we have a retry-after value from the API, use that instead
            // For rate limits, we should respect the full duration requested by the API
            if (api_error.retry_after_seconds.has_value()) {
                delay_ms = api_error.retry_after_seconds.value() * 1000;
            }
            
            log(LogLevel::WARNING, std::format(
                "Retrying request (attempt {}/{}) after {} ms due to: {}",
                attempt + 1, MAX_RETRIES, delay_ms, response.error.value_or("Unknown error")
            ));
            
            // Sleep before retry
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
        
        // Should not reach here, but return a failed response just in case
        ChatResponse failed_response;
        failed_response.success = false;
        failed_response.error = "Unexpected error in retry logic";
        return failed_response;
    }
    
    // Renamed original method to internal, keeping it for backwards compatibility
    ChatResponse send_request(ChatRequest request) {
        return send_request_with_retry(request);
    }
    
    ChatResponse send_request_internal(ChatRequest request) {
        stats.total_requests++;
        stats.last_request_time = std::chrono::steady_clock::now();
        
        // If using OAuth, prepend Claude Code system prompt as separate blocks
        if (auth_method == AuthMethod::OAUTH) {
            std::string original_prompt = request.system_prompt.text;
            
            // Create multiple system prompts array
            json system_array = json::array();
            
            // Add Claude Code system prompt as first block
            json claude_code_block = {
                {"type", "text"},
                {"text", CLAUDE_CODE_SYSTEM_PROMPT}
            };
            system_array.push_back(claude_code_block);
            
            // Add original system prompt as second block if it exists
            if (!original_prompt.empty()) {
                json user_block = {
                    {"type", "text"},
                    {"text", original_prompt},
                    {"cache_control", {{"type", "ephemeral"}}}
                };
                system_array.push_back(user_block);
            }
            
            // Set the multiple_system_prompts field
            request.multiple_system_prompts = system_array;
            // Clear the single system_prompt to avoid conflicts
            request.system_prompt.text = "";
        }

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
        
        // Temporary file logging for debugging
        {
            // Use trunc on first write to clear old sessions, then append
            std::ios_base::openmode mode = first_log_write ? std::ios::trunc : std::ios::app;
            std::ofstream log_file("/tmp/" + request_log_filename, mode);
            if (first_log_write && log_file.is_open()) {
                first_log_write = false;
            }
            if (log_file.is_open()) {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                log_file << "=== REQUEST at " << std::ctime(&time_t);
                log_file << "Iteration: " << current_iteration << "\n";
                log_file << "Request Body:\n" << request_json.dump(2) << "\n\n";
                log_file.close();
            }
        }
        
        std::string response_body;
        std::map<std::string, std::string> response_headers;
        long http_code = 0;

        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
        
        // Authentication header
        if (auth_method == AuthMethod::API_KEY) {
            headers = curl_slist_append(headers, ("x-api-key: " + api_key).c_str());
        } else {
            // OAuth authentication
            if (!oauth_creds) {
                curl_slist_free_all(headers);
                curl_easy_cleanup(curl);
                throw std::runtime_error("OAuth credentials not set");
            }
            headers = curl_slist_append(headers, ("Authorization: Bearer " + oauth_creds->access_token).c_str());
            
            // Add Stainless SDK headers for OAuth
            headers = curl_slist_append(headers, ("User-Agent: " + std::string(USER_AGENT)).c_str());
            headers = curl_slist_append(headers, "anthropic-dangerous-direct-browser-access: true");
            headers = curl_slist_append(headers, ("X-Stainless-Lang: js"));
            headers = curl_slist_append(headers, ("X-Stainless-Package-Version: " + std::string(STAINLESS_PACKAGE_VERSION)).c_str());
            headers = curl_slist_append(headers, ("X-Stainless-OS: " + std::string(STAINLESS_OS)).c_str());
            headers = curl_slist_append(headers, ("X-Stainless-Arch: " + std::string(STAINLESS_ARCH)).c_str());
            headers = curl_slist_append(headers, "X-Stainless-Runtime: node");
            headers = curl_slist_append(headers, "X-Stainless-Runtime-Version: v23.11.0");
        }

        // Build beta headers
        std::string beta_header;
        if (auth_method == AuthMethod::OAUTH) {
            // OAuth requires these beta headers
            beta_header = std::string(CLAUDE_CODE_BETA_HEADER) + "," + OAUTH_BETA_HEADER;
        }
        
        // Add interleaved thinking beta header if enabled and tools are being used
        if (request.enable_interleaved_thinking && request.enable_thinking && !request.tool_definitions.empty()) {
            if (!beta_header.empty()) beta_header += ",";
            beta_header += "interleaved-thinking-2025-05-14";
        }
        
        if (!beta_header.empty()) {
            headers = curl_slist_append(headers, ("anthropic-beta: " + beta_header).c_str());
        }

        // Build final URL - add ?beta=true for OAuth
        std::string final_url = api_url;
        if (auth_method == AuthMethod::OAUTH) {
            // Add beta=true query parameter for OAuth
            if (final_url.find('?') != std::string::npos) {
                final_url += "&beta=true";
            } else {
                final_url += "?beta=true";
            }
        }

        curl_easy_setopt(curl, CURLOPT_URL, final_url.c_str());
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
            // First check if response looks like JSON
            if (response_body.empty() || (response_body[0] != '{' && response_body[0] != '[')) {
                // Log non-JSON response to debug file
                {
                    std::ofstream log_file("/tmp/" + request_log_filename, std::ios::app);
                    if (log_file.is_open()) {
                        log_file << "=== NON-JSON RESPONSE for iteration " << current_iteration << "\n";
                        log_file << "HTTP Code: " << http_code << "\n";
                        log_file << "Full response:\n" << response_body << "\n";
                        log_file << "----------------------------------------\n\n";
                        log_file.close();
                    }
                }
                
                // Check if this is a 50X server error with non-JSON response
                if (http_code >= 500 && http_code < 600) {
                    // This is a server error, treat it as recoverable
                    stats.failed_requests++;
                    response.success = false;
                    response.error = std::format("Server error (HTTP {}): Non-JSON response - {}",
                        http_code,
                        response_body.empty() ? "empty response" : response_body.substr(0, 200));
                    
                    // Create an ApiError to ensure it's marked as recoverable
                    ApiError api_error = ApiError::from_response(
                        response.error.value(),
                        static_cast<int>(http_code),
                        response_headers
                    );
                    
                    if (message_logger) {
                        json error_log;
                        error_log["error"] = response.error.value();
                        error_log["http_code"] = http_code;
                        error_log["error_type"] = "NON_JSON_SERVER_ERROR";
                        error_log["is_recoverable"] = api_error.is_recoverable();
                        message_logger("SERVER_ERROR", error_log, current_iteration);
                    }
                    
                    log(LogLevel::WARNING, std::format("Recoverable server error (HTTP {}): Non-JSON response", http_code));
                    return response;
                }
                
                // Not a 50X error, throw as before
                throw std::runtime_error("Response is not valid JSON. First char: '" + 
                    (response_body.empty() ? "empty" : std::string(1, response_body[0])) + "'");
            }
            
            json response_json = json::parse(response_body);

            // Temporary file logging for debugging - log response too
            {
                std::ofstream log_file("/tmp/" + request_log_filename, std::ios::app);
                if (log_file.is_open()) {
                    log_file << "=== RESPONSE for iteration " << current_iteration << "\n";
                    log_file << "HTTP Code: " << http_code << "\n";
                    log_file << "Response Body:\n" << response_json.dump(2) << "\n";
                    log_file << "----------------------------------------\n\n";
                    log_file.close();
                }
            }

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

        // Try to extract HTTP status code from error message
        int status_code = 0;
        std::string error_msg = *response.error;
        
        // Look for patterns like "HTTP 503" or "(HTTP 503)" in the error message
        size_t http_pos = error_msg.find("HTTP ");
        if (http_pos != std::string::npos) {
            // Try to parse the number after "HTTP "
            size_t num_start = http_pos + 5;
            size_t num_end = num_start;
            while (num_end < error_msg.length() && std::isdigit(error_msg[num_end])) {
                num_end++;
            }
            if (num_end > num_start) {
                try {
                    status_code = std::stoi(error_msg.substr(num_start, num_end - num_start));
                } catch (...) {
                    // Ignore parse errors, status_code remains 0
                }
            }
        }

        ApiError api_error = ApiError::from_response(error_msg, status_code);
        return api_error.is_recoverable();
    }
};

// Session token tracker
// TokenTracker removed - replaced by TokenStats in token_stats.h

} // namespace claude

#endif //CLAUDE_CLIENT_H