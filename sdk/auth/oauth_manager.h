#pragma once

#include "../common.h"
#include "oauth_credentials.h"
#include "oauth_account_pool.h"
#include <filesystem>
#include <optional>
#include <chrono>
#include <string>
#include <memory>

namespace claude::auth {

// OAuth manager to read credentials storage with multi-account support
class OAuthManager {
public:
    // Constructor with optional config directory override
    explicit OAuthManager(const std::string& config_dir = "");

    // Destructor (must be defined in .cpp where OAuthAccountPool is complete)
    ~OAuthManager();

    // Check if OAuth credentials are available
    bool has_credentials() const;

    // Get OAuth credentials (returns best available account globally)
    std::shared_ptr<OAuthCredentials> get_credentials();

    // Save OAuth credentials (encrypts and stores)
    // For multi-account: adds to account pool with specified priority
    bool save_credentials(const OAuthCredentials& creds, int priority = 0);

    // Mark an account as rate limited
    void mark_account_rate_limited(const std::string& account_uuid, int retry_after_seconds);

    // Refresh OAuth tokens for a specific account
    // Returns updated credentials on success, nullptr on failure
    std::shared_ptr<OAuthCredentials> refresh_account(const std::string& account_uuid);


    // Get number of accounts
    size_t get_account_count() const;

    // Remove an account from the pool
    bool remove_account(const std::string& account_uuid);

    // Swap priorities of two accounts (for UI reordering)
    bool swap_account_priorities(const std::string& uuid1, const std::string& uuid2);

    // Get detailed account information for UI display
    std::vector<OAuthAccountPool::AccountInfo> get_all_accounts_info() const;

    // Get error message if last operation failed
    std::string get_last_error() const { return last_error; }

private:
    std::filesystem::path config_dir;
    std::filesystem::path credentials_file;
    std::filesystem::path key_file;

    // Multi-account pool
    std::unique_ptr<OAuthAccountPool> account_pool_;

    // Error tracking
    mutable std::string last_error;

    // Helper methods
    std::filesystem::path expand_home_directory(const std::string& path) const;
    std::optional<std::string> read_file(const std::filesystem::path& path) const;
    std::optional<std::string> decrypt_data(const std::string& encrypted_data, const std::string& key) const;
    std::optional<json> parse_credentials_json(const std::string& decrypted_data) const;

    // Load accounts from disk
    bool load_accounts_from_disk();

    // Save accounts to disk
    bool save_accounts_to_disk();

    // AES encryption/decryption helpers
    std::vector<uint8_t> derive_key(const std::string& password) const;
    std::string encrypt_data(const std::string& plaintext, const std::string& key_str) const;
};

} // namespace claude::auth