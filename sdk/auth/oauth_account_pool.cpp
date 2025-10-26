#include "oauth_account_pool.h"
#include <algorithm>
#include <sstream>
#include <thread>

namespace claude::auth {

OAuthAccountPool::OAuthAccountPool() {
}

bool OAuthAccountPool::add_account(const OAuthCredentials& creds, int priority) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if account already exists
    auto it = find_account_by_uuid(creds.account_uuid);
    if (it != accounts_.end()) {
        // Update existing account
        it->credentials = creds;
        it->priority = priority;
    } else {
        // Add new account
        accounts_.emplace_back(creds, priority);
    }

    // Sort accounts by priority
    std::sort(accounts_.begin(), accounts_.end());

    return true;
}

bool OAuthAccountPool::remove_account(const std::string& account_uuid) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = find_account_by_uuid(account_uuid);
    if (it == accounts_.end()) {
        return false;
    }

    accounts_.erase(it);

    // Remove from rate limits
    rate_limits_.erase(account_uuid);

    return true;
}

size_t OAuthAccountPool::account_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return accounts_.size();
}

std::shared_ptr<OAuthCredentials> OAuthAccountPool::get_best_available_account() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (accounts_.empty()) {
        last_error_ = "No OAuth accounts available";
        return nullptr;
    }

    // Iterate through accounts in priority order (already sorted)
    for (const auto& account : accounts_) {
        if (is_account_available(account.credentials.account_uuid)) {
            return std::make_shared<OAuthCredentials>(account.credentials);
        }
    }

    // No available accounts - check if any will become available soon
    auto soonest = get_soonest_available_account();
    if (soonest) {
        int wait_seconds = soonest->seconds_until_available();
        if (wait_seconds > 0 && wait_seconds < 3600) {  // Wait up to 1 hour
            // Temporarily release mutex while waiting
            mutex_.unlock();
            std::this_thread::sleep_for(std::chrono::seconds(wait_seconds));
            mutex_.lock();

            // Try to get the account that should now be available
            if (is_account_available(soonest->account_uuid)) {
                auto it = find_account_by_uuid(soonest->account_uuid);
                if (it != accounts_.end()) {
                    return std::make_shared<OAuthCredentials>(it->credentials);
                }
            }
        }
    }

    last_error_ = "No OAuth accounts available (all rate limited or expired)";
    return nullptr;
}

void OAuthAccountPool::mark_rate_limited(const std::string& account_uuid, int retry_after_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto now = std::chrono::system_clock::now();
    auto rate_limit_until = now + std::chrono::seconds(retry_after_seconds);

    RateLimitInfo info;
    info.account_uuid = account_uuid;
    info.rate_limited_until = rate_limit_until;
    info.retry_after_seconds = retry_after_seconds;

    rate_limits_[account_uuid] = info;
}

bool OAuthAccountPool::is_account_available(const std::string& account_uuid) const {
    // Check if account exists
    auto it = find_account_by_uuid(account_uuid);
    if (it == accounts_.end()) {
        return false;
    }

    // Check if account token is expired AND has no refresh token
    // Expired tokens with refresh tokens are considered "available" because they can be refreshed by the caller
    if (it->credentials.is_expired(300) && it->credentials.refresh_token.empty()) {
        return false;  // No refresh token, truly unavailable
    }

    // Check if account is rate limited
    auto rate_it = rate_limits_.find(account_uuid);
    if (rate_it != rate_limits_.end() && rate_it->second.is_rate_limited()) {
        return false;
    }

    return true;
}

// Note: Assumes mutex_ is already held by caller
std::optional<RateLimitInfo> OAuthAccountPool::get_soonest_available_account() const {
    std::optional<RateLimitInfo> soonest;

    for (const auto& [uuid, info] : rate_limits_) {
        if (info.is_rate_limited()) {
            if (!soonest || info.rate_limited_until < soonest->rate_limited_until) {
                soonest = info;
            }
        }
    }

    return soonest;
}

bool OAuthAccountPool::load_from_json(const json& j) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        if (!j.contains("version")) {
            last_error_ = "Missing version field in credentials";
            return false;
        }

        int version = j["version"];

        if (version != 2) {
            last_error_ = "Unsupported credentials version: " + std::to_string(version);
            return false;
        }

        // Multi-account format
        if (!j.contains("accounts") || !j["accounts"].is_array()) {
            last_error_ = "Missing or invalid accounts array";
            return false;
        }

        accounts_.clear();

        for (const auto& account_json : j["accounts"]) {
            OAuthCredentials creds;
            creds.access_token = account_json["access_token"];
            creds.refresh_token = account_json.value("refresh_token", "");
            creds.expires_at = account_json.value("expires_at", 0.0);
            creds.account_uuid = account_json.value("account_uuid", "");

            int priority = account_json.value("priority", 0);

            accounts_.emplace_back(creds, priority);
        }

        // Sort by priority
        std::sort(accounts_.begin(), accounts_.end());

        return true;

    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to parse credentials JSON: ") + e.what();
        return false;
    }
}

json OAuthAccountPool::save_to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);

    json accounts_array = json::array();

    for (const auto& account : accounts_) {
        json account_json;
        account_json["priority"] = account.priority;
        account_json["access_token"] = account.credentials.access_token;
        account_json["refresh_token"] = account.credentials.refresh_token;
        account_json["expires_at"] = account.credentials.expires_at;
        account_json["account_uuid"] = account.credentials.account_uuid;
        account_json["provider"] = "claude_ai";  // Default provider

        accounts_array.push_back(account_json);
    }

    json result;
    result["version"] = 2;
    result["accounts"] = accounts_array;

    return result;
}

bool OAuthAccountPool::update_account_credentials(const std::string& account_uuid,
                                                   const OAuthCredentials& new_creds) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = find_account_by_uuid(account_uuid);
    if (it == accounts_.end()) {
        return false;
    }

    it->credentials = new_creds;
    return true;
}

std::vector<OAuthAccount> OAuthAccountPool::get_all_accounts() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return accounts_;
}

void OAuthAccountPool::clear_rate_limits() {
    std::lock_guard<std::mutex> lock(mutex_);
    rate_limits_.clear();
}

bool OAuthAccountPool::swap_priorities(const std::string& uuid1, const std::string& uuid2) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it1 = find_account_by_uuid(uuid1);
    auto it2 = find_account_by_uuid(uuid2);

    if (it1 == accounts_.end() || it2 == accounts_.end()) {
        return false;
    }

    // Swap priorities
    std::swap(it1->priority, it2->priority);

    // Re-sort accounts by priority
    std::sort(accounts_.begin(), accounts_.end());

    return true;
}

std::vector<OAuthAccountPool::AccountInfo> OAuthAccountPool::get_all_accounts_info() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<AccountInfo> result;

    for (const auto& account : accounts_) {
        AccountInfo info;
        info.priority = account.priority;
        info.account_uuid = account.credentials.account_uuid;
        info.expires_at = account.credentials.expires_at;

        // Check if rate limited
        auto rate_it = rate_limits_.find(account.credentials.account_uuid);
        if (rate_it != rate_limits_.end() && rate_it->second.is_rate_limited()) {
            info.is_rate_limited = true;
            info.seconds_until_available = rate_it->second.seconds_until_available();
        } else {
            info.is_rate_limited = false;
            info.seconds_until_available = 0;
        }

        // Check if expires soon (< 5 minutes)
        auto now = std::chrono::system_clock::now();
        auto now_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        int seconds_left = static_cast<int>(account.credentials.expires_at - now_timestamp);
        info.expires_soon = (seconds_left > 0 && seconds_left < 300);

        result.push_back(info);
    }

    return result;
}

// Private helper methods

std::vector<OAuthAccount>::iterator OAuthAccountPool::find_account_by_uuid(const std::string& account_uuid) {
    return std::find_if(accounts_.begin(), accounts_.end(),
                       [&account_uuid](const OAuthAccount& acc) {
                           return acc.credentials.account_uuid == account_uuid;
                       });
}

std::vector<OAuthAccount>::const_iterator OAuthAccountPool::find_account_by_uuid(const std::string& account_uuid) const {
    return std::find_if(accounts_.begin(), accounts_.end(),
                       [&account_uuid](const OAuthAccount& acc) {
                           return acc.credentials.account_uuid == account_uuid;
                       });
}

} // namespace claude::auth
