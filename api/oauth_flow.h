#pragma once

#include "api/api_common.h"
#include "anthropic_api.h"
#include <string>
#include <optional>
#include <chrono>
#include <map>

namespace llm_re {

// OAuth flow implementation for token refresh
class OAuthFlow {
public:
    // Constructor
    OAuthFlow();
    ~OAuthFlow();
    
    // Refresh an OAuth token
    // Returns updated credentials with new access token and updated expiry
    // Throws std::runtime_error on failure
    api::OAuthCredentials refresh_token(const std::string& refresh_token, const std::optional<std::string>& account_uuid = std::nullopt);
    
    // Check if credentials are expired or will expire soon
    static bool needs_refresh(const api::OAuthCredentials& creds, int buffer_seconds = 300);
    
    // Get last error message
    std::string get_last_error() const { return last_error_; }
    
    // Rate limiting for refresh attempts
    bool can_refresh() const;
    void record_refresh_attempt();
    
private:
    // Error tracking
    mutable std::string last_error_;
    
    // Rate limiting
    std::chrono::steady_clock::time_point last_refresh_attempt_;
    static constexpr int REFRESH_COOLDOWN_SECONDS = 30;
    
    // CURL helpers
    static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp);
    
    // Perform the actual token refresh HTTP request
    json perform_refresh_request(const std::string& refresh_token);
    
    // Parse refresh response and build credentials
    api::OAuthCredentials parse_refresh_response(const json& response,
                                                const std::string& original_refresh_token,
                                                const std::optional<std::string>& account_uuid);
};

} // namespace llm_re