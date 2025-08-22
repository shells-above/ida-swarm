#pragma once

#include "../common.h"
#include "../client/client.h"
#include <filesystem>
#include <optional>
#include <chrono>

namespace claude::auth {

// OAuth manager to read credentials storage
class OAuthManager {
public:
    // Constructor with optional config directory override
    explicit OAuthManager(const std::string& config_dir = "");
    
    // Check if OAuth credentials are available
    bool has_credentials() const;
    
    // Get OAuth credentials (reads and decrypts from storage)
    std::shared_ptr<OAuthCredentials> get_credentials();
    
    // Get cached credentials without re-reading
    std::shared_ptr<OAuthCredentials> get_cached_credentials() const;
    
    // Clear cached credentials
    void clear_cache();
    
    // Save OAuth credentials (encrypts and stores)
    bool save_credentials(const OAuthCredentials& creds);
    
    // Refresh OAuth tokens if expired or about to expire
    // Returns updated credentials on success, nullptr on failure
    std::shared_ptr<OAuthCredentials> refresh_if_needed();
    
    // Force refresh OAuth tokens
    std::shared_ptr<OAuthCredentials> force_refresh();
    
    // Check if current credentials need refresh
    bool needs_refresh();
    
    // Get error message if last operation failed
    std::string get_last_error() const { return last_error; }
    
private:
    std::filesystem::path config_dir;
    std::filesystem::path credentials_file;
    std::filesystem::path key_file;
    
    // Cached credentials (shared between all clients)
    std::shared_ptr<OAuthCredentials> cached_credentials;
    std::chrono::steady_clock::time_point cache_time;
    static constexpr int CACHE_DURATION_SECONDS = 60;
    
    // Error tracking
    mutable std::string last_error;
    
    // Helper methods
    std::filesystem::path expand_home_directory(const std::string& path) const;
    std::optional<std::string> read_file(const std::filesystem::path& path) const;
    std::optional<std::string> decrypt_data(const std::string& encrypted_data, const std::string& key) const;
    std::optional<json> parse_credentials_json(const std::string& decrypted_data) const;
    OAuthCredentials extract_oauth_credentials(const json& creds_json) const;
    
    // AES encryption/decryption helpers
    std::vector<uint8_t> derive_key(const std::string& password) const;
    std::string encrypt_data(const std::string& plaintext, const std::string& key_str) const;
};

} // namespace claude::auth