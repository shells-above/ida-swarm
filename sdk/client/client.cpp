//
// Created by user on 7/1/25.
//

#include "client.h"
#include "../auth/oauth_authorizer.h"

namespace claude {

// Static member definitions
std::mutex Client::global_oauth_mutex_;
std::shared_ptr<auth::OAuthAccountPool> Client::global_oauth_pool_;

ChatRequestBuilder& ChatRequestBuilder::with_tools(const tools::ToolRegistry& registry) {
    request.tool_definitions = registry.get_api_definitions();

    // Add cache control to the last tool definition for prompt caching
    if (!request.tool_definitions.empty()) {
        request.tool_definitions.back()["cache_control"] = {{"type", "ephemeral"}};
    }

    return *this;
}

Client::Client(
    AuthMethod method,
    const std::string& credential,
    const std::string& base_url,
    const std::string& log_filename
) : auth_method(method), api_url(base_url) {

    request_log_filename = log_filename.empty() ? generate_timestamp_log_filename() : log_filename;

    if (method == AuthMethod::API_KEY) {
        if (credential.empty()) {
            throw std::runtime_error("API key required for API_KEY auth method");
        }
        api_key = credential;
    }
    else if (method == AuthMethod::OAUTH) {
        // OAuth credentials will be fetched fresh from disk on each request
        // If no credentials exist, requests will fail gracefully with proper error messages
    }
    else {
        throw std::runtime_error("Invalid authentication method");
    }
}

// === STATIC HELPER METHODS ===

std::filesystem::path Client::get_default_config_dir() {
    const char* home_env = std::getenv("HOME");
    if (!home_env) {
        throw std::runtime_error("HOME environment variable not set");
    }
    return std::filesystem::path(home_env) / ".claude_cpp_sdk";
}

std::shared_ptr<auth::OAuthAccountPool> Client::get_global_oauth_pool() {
    std::lock_guard<std::mutex> lock(global_oauth_mutex_);

    if (!global_oauth_pool_) {
        // Lazy initialization (just creates pool object with file paths)
        std::filesystem::path config_dir = get_default_config_dir();
        global_oauth_pool_ = std::make_shared<auth::OAuthAccountPool>(config_dir);
    }

    // CRITICAL: ALWAYS reload from disk to get fresh credentials!
    // NO CACHING - this ensures we see updates from other processes:
    // - Token refreshes from other processes
    // - New accounts added via OAuth flow
    // - Rate limit updates
    // - Account removal or priority changes
    if (!global_oauth_pool_->credentials_exist()) {
        return nullptr;
    }

    if (!global_oauth_pool_->load_from_disk()) {
        return nullptr;  // Failed to load
    }

    return global_oauth_pool_;
}

// === OAUTH STATIC METHODS ===

bool Client::authorize_new_account() {
    // Use OAuthAuthorizer to run browser flow
    auth::OAuthAuthorizer authorizer;

    if (!authorizer.authorize()) {
        // Authorization failed
        return false;
    }

    // Credentials were saved by authorizer
    // Reset global pool to force reload on next access
    {
        std::lock_guard<std::mutex> lock(global_oauth_mutex_);
        global_oauth_pool_.reset();
    }

    return true;
}

bool Client::refresh_account_tokens(const std::string& account_uuid) {
    auto pool = get_global_oauth_pool();
    if (!pool) {
        return false;
    }

    // Get current credentials for this account
    pool->load_from_disk();  // Reload to get latest
    auto accounts = pool->get_all_accounts();

    std::shared_ptr<OAuthCredentials> creds_to_refresh = nullptr;
    for (const auto& account : accounts) {
        if (account.credentials.account_uuid == account_uuid) {
            creds_to_refresh = std::make_shared<OAuthCredentials>(account.credentials);
            break;
        }
    }

    if (!creds_to_refresh) {
        return false;  // Account not found
    }

    // Use OAuthFlow to refresh
    auth::OAuthFlow flow;
    try {
        auto new_creds = flow.refresh_token(creds_to_refresh->refresh_token, account_uuid);

        // Update credentials in pool and save to disk atomically
        bool success = pool->update_on_disk([&]() {
            return pool->update_account_credentials(account_uuid, new_creds);
        });

        return success;
    } catch (const std::exception& e) {
        return false;
    }
}

std::vector<auth::OAuthAccountPool::AccountInfo> Client::get_accounts_info() {
    auto pool = get_global_oauth_pool();
    if (!pool) {
        return {};
    }

    return pool->get_all_accounts_info();
}

bool Client::has_oauth_credentials() {
    std::filesystem::path config_dir = get_default_config_dir();
    std::filesystem::path creds_file = config_dir / "credentials.json";
    return std::filesystem::exists(creds_file);
}

} // namespace claude