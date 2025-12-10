#pragma once

#include "../common.h"
#include "oauth_credentials.h"
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <mutex>
#include <optional>
#include <memory>
#include <filesystem>
#include <functional>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/file.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace claude::auth {

// Rate limit tracking per account
struct RateLimitInfo {
    std::string account_uuid;
    std::chrono::system_clock::time_point rate_limited_until;
    int retry_after_seconds = 0;

    // Check if rate limit has expired
    bool is_rate_limited() const {
        auto now = std::chrono::system_clock::now();
        return now < rate_limited_until;
    }

    // Get seconds until rate limit expires (0 if not rate limited)
    int seconds_until_available() const {
        if (!is_rate_limited()) return 0;
        auto now = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            rate_limited_until - now);
        return static_cast<int>(duration.count());
    }
};

// OAuth account with priority
struct OAuthAccount {
    OAuthCredentials credentials;
    int priority = 0;  // 0 = highest priority (primary), 1 = secondary, etc.

    OAuthAccount() = default;
    OAuthAccount(const OAuthCredentials& creds, int prio)
        : credentials(creds), priority(prio) {}

    // Comparison for sorting by priority
    bool operator<(const OAuthAccount& other) const {
        return priority < other.priority;
    }
};

// Multi-account OAuth management with rate limit tracking
// NOW INCLUDES: File I/O, locking, and atomic operations (merged from CredentialStore)
class OAuthAccountPool {
public:
    // Constructor with config directory path
    // If config_dir is empty, uses default: ~/.claude_cpp_sdk
    explicit OAuthAccountPool(const std::filesystem::path& config_dir = "");
    ~OAuthAccountPool() = default;

    // Account management
    bool add_account(const OAuthCredentials& creds, int priority);
    bool remove_account(const std::string& account_uuid);
    size_t account_count() const;
    bool has_accounts() const { return !accounts_.empty(); }

    // Get best available account (highest priority non-rate-limited)
    // Returns nullptr if no accounts available (waits if all rate limited)
    std::shared_ptr<OAuthCredentials> get_best_available_account();

    // Mark an account as rate limited
    void mark_rate_limited(const std::string& account_uuid, int retry_after_seconds);

    // Check if an account is available (not rate limited and not expired)
    bool is_account_available(const std::string& account_uuid) const;

    // === FILE OPERATIONS (merged from CredentialStore) ===

    // Check if credentials file exists on disk
    bool credentials_exist() const;

    // Load accounts from disk (with file locking)
    // Returns false if file doesn't exist or can't be read
    bool load_from_disk();

    // Save accounts to disk (with file locking and atomic write)
    // Uses temp file + rename for atomicity
    bool save_to_disk();

    // Atomic read-modify-write operation
    // Callback receives mutable reference to this pool, modifies it, returns true to commit
    bool update_on_disk(std::function<bool(void)> modify_callback);

    // Update credentials for an account (after refresh)
    bool update_account_credentials(const std::string& account_uuid,
                                   const OAuthCredentials& new_creds);

    // Get all accounts (for debugging/inspection)
    std::vector<OAuthAccount> get_all_accounts() const;

    // Clear all rate limits (for testing)
    void clear_rate_limits();

    // Swap priorities of two accounts (for UI reordering)
    bool swap_priorities(const std::string& uuid1, const std::string& uuid2);

    // Get last error message
    std::string get_last_error() const { return last_error_; }

    // Get detailed account information for UI display
    struct AccountInfo {
        int priority;
        std::string account_uuid;
        bool is_rate_limited;
        int seconds_until_available;
        double expires_at;
        bool expires_soon;  // < 5 minutes

        std::string get_status_text() const {
            if (is_rate_limited) return "Rate Limited";
            if (expires_soon) return "Expiring Soon";
            return "Active";
        }

        std::string get_expires_in_text() const {
            if (is_rate_limited && seconds_until_available > 0) {
                int mins = seconds_until_available / 60;
                int secs = seconds_until_available % 60;
                return std::to_string(mins) + "m " + std::to_string(secs) + "s";
            }

            auto now = std::chrono::system_clock::now();
            auto now_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            int seconds_left = static_cast<int>(expires_at - now_timestamp);

            if (seconds_left < 0) return "Expired";

            int hours = seconds_left / 3600;
            int mins = (seconds_left % 3600) / 60;
            return std::to_string(hours) + "h " + std::to_string(mins) + "m";
        }
    };
    std::vector<AccountInfo> get_all_accounts_info() const;

private:
    // === ACCOUNT STORAGE ===

    // Accounts sorted by priority
    std::vector<OAuthAccount> accounts_;

    // Rate limit tracking per account UUID
    std::map<std::string, RateLimitInfo> rate_limits_;

    // === FILE MANAGEMENT (merged from CredentialStore) ===

    // File paths
    std::filesystem::path config_dir_;
    std::filesystem::path credentials_file_;
    std::filesystem::path credentials_file_tmp_;
    std::filesystem::path lock_file_;

    // Lock timeout in seconds
    static constexpr int LOCK_TIMEOUT_SECONDS = 90;

    // === FILE LOCKING (moved from CredentialStore) ===

    class FileLock {
    public:
        enum class LockType { SHARED, EXCLUSIVE };

        explicit FileLock(const std::filesystem::path& lock_file_path);
        ~FileLock();

        bool acquire(LockType type, int timeout_seconds);
        void release();
        bool is_locked() const { return locked_; }

        // Prevent copying
        FileLock(const FileLock&) = delete;
        FileLock& operator=(const FileLock&) = delete;

    private:
        std::filesystem::path lock_file_path_;
        bool locked_;
        LockType lock_type_;

#ifdef _WIN32
        HANDLE handle_;
#else
        int fd_;
#endif
    };

    // === SERIALIZATION (now private helpers) ===

    // Serialize accounts to JSON (version 2 format)
    json to_json() const;

    // Deserialize accounts from JSON (version 2 format)
    bool from_json(const json& j);

    // === FILE I/O HELPERS ===

    // Read file contents (simple helper)
    std::optional<std::string> read_file(const std::filesystem::path& path) const;

    // Write file atomically (temp + rename)
    bool write_file_atomic(const std::filesystem::path& path, const std::string& data);

    // === SYNCHRONIZATION ===

    // Thread safety
    mutable std::mutex mutex_;

    // Error tracking
    mutable std::string last_error_;

    // === ACCOUNT HELPERS ===

    // Find account by UUID
    std::vector<OAuthAccount>::iterator find_account_by_uuid(const std::string& account_uuid);
    std::vector<OAuthAccount>::const_iterator find_account_by_uuid(const std::string& account_uuid) const;

    // Get the account with the soonest expiry time (for waiting when all are limited)
    // Note: Assumes mutex_ is already held by caller
    std::optional<RateLimitInfo> get_soonest_available_account() const;

    // === INITIALIZATION ===

    // Initialize file paths based on config directory
    void initialize_file_paths();

    // Get default config directory
    static std::filesystem::path get_default_config_dir();
};

} // namespace claude::auth
