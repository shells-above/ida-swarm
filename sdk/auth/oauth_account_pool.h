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
class OAuthAccountPool {
public:
    OAuthAccountPool();
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

    // Load accounts from JSON (version 2 format)
    bool load_from_json(const json& j);

    // Save accounts to JSON (version 2 format)
    json save_to_json() const;

    // Update credentials for an account (after refresh)
    bool update_account_credentials(const std::string& account_uuid,
                                   const OAuthCredentials& new_creds);

    // Get all accounts (for debugging/inspection)
    std::vector<OAuthAccount> get_all_accounts() const;

    // Clear all rate limits (for testing)
    void clear_rate_limits();

    // Swap priorities of two accounts (for UI reordering)
    bool swap_priorities(const std::string& uuid1, const std::string& uuid2);

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
    // Accounts sorted by priority
    std::vector<OAuthAccount> accounts_;

    // Rate limit tracking per account UUID
    std::map<std::string, RateLimitInfo> rate_limits_;

    // Thread safety
    mutable std::mutex mutex_;

    // Error tracking
    mutable std::string last_error_;

    // Helper: Find account by UUID
    std::vector<OAuthAccount>::iterator find_account_by_uuid(const std::string& account_uuid);
    std::vector<OAuthAccount>::const_iterator find_account_by_uuid(const std::string& account_uuid) const;

    // Helper: Get the account with the soonest expiry time (for waiting when all are limited)
    // Note: Assumes mutex_ is already held by caller
    std::optional<RateLimitInfo> get_soonest_available_account() const;
};

} // namespace claude::auth
