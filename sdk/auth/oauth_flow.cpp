#include "oauth_flow.h"
#include <curl/curl.h>
#include <sstream>
#include <stdexcept>

namespace claude::auth {

OAuthFlow::OAuthFlow() 
    : last_refresh_attempt_(std::chrono::steady_clock::time_point{}) {
}

OAuthFlow::~OAuthFlow() = default;

size_t OAuthFlow::write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

bool OAuthFlow::needs_refresh(const OAuthCredentials& creds, int buffer_seconds) {
    return creds.is_expired(buffer_seconds);
}

bool OAuthFlow::can_refresh() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_refresh_attempt_).count();
    return elapsed >= REFRESH_COOLDOWN_SECONDS;
}

void OAuthFlow::record_refresh_attempt() {
    last_refresh_attempt_ = std::chrono::steady_clock::now();
}

json OAuthFlow::perform_refresh_request(const std::string& refresh_token) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }
    
    // Prepare request data
    json request_data = {
        {"grant_type", "refresh_token"},
        {"refresh_token", refresh_token},
        {"client_id", OAUTH_CLIENT_ID}
    };
    
    std::string request_body = request_data.dump();
    std::string response_body;
    
    // Set up headers
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, ("User-Agent: " + std::string(USER_AGENT)).c_str());
    
    // Configure CURL
    curl_easy_setopt(curl, CURLOPT_URL, OAUTH_TOKEN_URL);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request_body.length());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

    // Force HTTP/1.1 to avoid libcurl HTTP/2 connection reuse bugs
    curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);

    // Connection management
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 60L);
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L);

    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    if (res == CURLE_OK) {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    }
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    // Check for errors
    if (res != CURLE_OK) {
        throw std::runtime_error("CURL request failed: " + std::string(curl_easy_strerror(res)));
    }
    
    if (http_code != 200) {
        std::stringstream error_msg;
        error_msg << "Token refresh failed with HTTP " << http_code;
        if (!response_body.empty()) {
            try {
                json error_json = json::parse(response_body);
                if (error_json.contains("error")) {
                    error_msg << ": " << error_json["error"];
                }
            } catch (...) {
                error_msg << ": " << response_body;
            }
        }
        throw std::runtime_error(error_msg.str());
    }
    
    // Parse response
    try {
        return json::parse(response_body);
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to parse refresh response: " + std::string(e.what()));
    }
}

OAuthCredentials OAuthFlow::parse_refresh_response(const json& response,
                                                        const std::string& original_refresh_token,
                                                        const std::optional<std::string>& account_uuid) {
    OAuthCredentials creds;
    
    // Extract access token (required)
    if (!response.contains("access_token") || !response["access_token"].is_string()) {
        throw std::runtime_error("Missing access_token in refresh response");
    }
    creds.access_token = response["access_token"];
    
    // Extract refresh token (may be rotated or preserved)
    if (response.contains("refresh_token") && response["refresh_token"].is_string()) {
        creds.refresh_token = response["refresh_token"];
    } else {
        // Keep the original refresh token if not rotated
        creds.refresh_token = original_refresh_token;
    }
    
    // Calculate expiry time
    if (response.contains("expires_in") && response["expires_in"].is_number()) {
        auto now = std::chrono::system_clock::now();
        auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        creds.expires_at = static_cast<double>(now_seconds + response["expires_in"].get<int>());
    } else {
        // Default to 1 hour if not specified
        auto now = std::chrono::system_clock::now();
        auto now_seconds = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        creds.expires_at = static_cast<double>(now_seconds + 3600);
    }
    
    // Preserve account UUID
    if (account_uuid.has_value()) {
        creds.account_uuid = account_uuid.value();
    }
    
    return creds;
}

OAuthCredentials OAuthFlow::refresh_token(const std::string& refresh_token,
                                              const std::optional<std::string>& account_uuid) {
    // Check rate limiting
    if (!can_refresh()) {
        throw std::runtime_error("Token refresh attempted too frequently (cooldown active)");
    }
    
    // Record attempt
    record_refresh_attempt();
    
    try {
        // Perform refresh request
        json response = perform_refresh_request(refresh_token);
        
        // Parse and return credentials
        return parse_refresh_response(response, refresh_token, account_uuid);
        
    } catch (const std::exception& e) {
        last_error_ = e.what();
        throw;
    }
}

} // namespace claude::auth