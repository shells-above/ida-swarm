#pragma once

#include "api/api_common.h"
#include "anthropic_api.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <optional>
#include <map>

namespace llm_re {

// OAuth Authorizer - handles the complete OAuth flow
class OAuthAuthorizer {
public:
    OAuthAuthorizer();
    ~OAuthAuthorizer();
    
    // Start the authorization flow
    // Returns true if successful, false otherwise
    bool authorize();
    
    // Get last error message
    std::string getLastError() const { return last_error_; }
    
    // Check if authorization is in progress
    bool isAuthorizing() const { return is_authorizing_; }
    
private:
    // PKCE (Proof Key for Code Exchange) parameters
    struct PKCEParams {
        std::string code_verifier;
        std::string code_challenge;
        std::string state;
    };
    
    // OAuth configuration
    static constexpr int TIMEOUT_SECONDS = 300;
    
    // State
    std::atomic<bool> is_authorizing_{false};
    std::string last_error_;
    
    // Server state
    std::thread server_thread_;
    std::atomic<bool> server_running_{false};
    int server_socket_ = -1;
    
    // OAuth flow state
    PKCEParams pkce_params_;
    std::string auth_code_;
    std::mutex auth_mutex_;
    std::condition_variable auth_cv_;
    
    // PKCE generation
    std::string generateCodeVerifier();
    std::string generateCodeChallenge(const std::string& verifier);
    std::string generateState();
    std::string base64UrlEncode(const std::vector<uint8_t>& data);
    
    // Server management
    bool startCallbackServer();
    void stopCallbackServer();
    void runServer();
    void handleRequest(int client_socket);
    std::map<std::string, std::string> parseQueryString(const std::string& query);
    std::string waitForAuthCode();
    
    // OAuth flow
    std::string buildAuthorizationUrl(const PKCEParams& params);
    bool openBrowser(const std::string& url);
    std::optional<api::OAuthCredentials> exchangeCodeForTokens(const std::string& code);
    
    // Storage
    bool saveCredentials(const api::OAuthCredentials& creds);
    
    // URL encoding
    std::string urlEncode(const std::string& value);
};

} // namespace llm_re