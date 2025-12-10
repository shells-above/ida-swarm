#include "oauth_account_pool.h"
#include <algorithm>
#include <sstream>
#include <thread>
#include <fstream>
#include <cstdlib>
#include <sys/stat.h>

namespace claude::auth {

// ============================================================================
// CONSTRUCTOR AND INITIALIZATION
// ============================================================================

OAuthAccountPool::OAuthAccountPool(const std::filesystem::path& config_dir) {
    if (config_dir.empty()) {
        config_dir_ = get_default_config_dir();
    } else {
        config_dir_ = config_dir;
    }

    initialize_file_paths();

    // Ensure config directory exists
    if (!std::filesystem::exists(config_dir_)) {
        std::filesystem::create_directories(config_dir_);
    }
}

std::filesystem::path OAuthAccountPool::get_default_config_dir() {
    const char* home_env = std::getenv("HOME");
    if (!home_env) {
        throw std::runtime_error("HOME environment variable not set");
    }
    return std::filesystem::path(home_env) / ".claude_cpp_sdk";
}

void OAuthAccountPool::initialize_file_paths() {
    credentials_file_ = config_dir_ / "credentials.json";
    credentials_file_tmp_ = config_dir_ / "credentials.json.tmp";
    lock_file_ = config_dir_ / ".credentials.lock";
}

// ============================================================================
// FILE LOCKING (moved from CredentialStore)
// ============================================================================

OAuthAccountPool::FileLock::FileLock(const std::filesystem::path& lock_file_path)
    : lock_file_path_(lock_file_path), locked_(false), lock_type_(LockType::SHARED)
#ifdef _WIN32
    , handle_(INVALID_HANDLE_VALUE)
#else
    , fd_(-1)
#endif
{
}

OAuthAccountPool::FileLock::~FileLock() {
    if (locked_) {
        release();
    }
}

bool OAuthAccountPool::FileLock::acquire(LockType type, int timeout_seconds) {
    if (locked_) {
        return true;  // Already locked
    }

    lock_type_ = type;

    // Create lock file if it doesn't exist
    std::ofstream lock_file(lock_file_path_, std::ios::app);
    lock_file.close();

#ifdef _WIN32
    // Windows file locking implementation
    DWORD flags = (type == LockType::EXCLUSIVE) ? LOCKFILE_EXCLUSIVE_LOCK : 0;
    flags |= LOCKFILE_FAIL_IMMEDIATELY;

    handle_ = CreateFileW(
        lock_file_path_.wstring().c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );

    if (handle_ == INVALID_HANDLE_VALUE) {
        return false;
    }

    auto start = std::chrono::steady_clock::now();
    while (true) {
        OVERLAPPED overlapped = {0};
        if (LockFileEx(handle_, flags, 0, MAXDWORD, MAXDWORD, &overlapped)) {
            locked_ = true;
            return true;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();

        if (elapsed >= timeout_seconds) {
            CloseHandle(handle_);
            handle_ = INVALID_HANDLE_VALUE;
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
#else
    // Unix file locking implementation
    fd_ = open(lock_file_path_.c_str(), O_RDWR | O_CREAT, 0600);
    if (fd_ < 0) {
        return false;
    }

    int lock_operation = (type == LockType::EXCLUSIVE) ? LOCK_EX : LOCK_SH;

    // Try with exponential backoff
    auto start = std::chrono::steady_clock::now();
    int backoff_ms = 10;

    while (true) {
        // Try non-blocking lock
        if (flock(fd_, lock_operation | LOCK_NB) == 0) {
            locked_ = true;
            return true;
        }

        // Check timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start).count();

        if (elapsed >= timeout_seconds) {
            close(fd_);
            fd_ = -1;
            return false;
        }

        // Exponential backoff with cap
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff_ms));
        backoff_ms = std::min(backoff_ms * 2, 1000);  // Max 1 second
    }
#endif
}

void OAuthAccountPool::FileLock::release() {
    if (!locked_) {
        return;
    }

#ifdef _WIN32
    if (handle_ != INVALID_HANDLE_VALUE) {
        UnlockFile(handle_, 0, 0, MAXDWORD, MAXDWORD);
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
#else
    if (fd_ >= 0) {
        flock(fd_, LOCK_UN);
        close(fd_);
        fd_ = -1;
    }
#endif

    locked_ = false;
}

// ============================================================================
// FILE I/O OPERATIONS (merged from CredentialStore)
// ============================================================================

bool OAuthAccountPool::credentials_exist() const {
    return std::filesystem::exists(credentials_file_);
}

bool OAuthAccountPool::load_from_disk() {
    // Acquire shared lock for reading
    FileLock lock(lock_file_);
    if (!lock.acquire(FileLock::LockType::SHARED, LOCK_TIMEOUT_SECONDS)) {
        last_error_ = "Failed to acquire lock for reading credentials";
        return false;
    }

    // Check if file exists
    if (!credentials_exist()) {
        last_error_ = "Credentials file does not exist";
        return false;
    }

    // Read file contents
    auto file_contents = read_file(credentials_file_);
    if (!file_contents) {
        last_error_ = "Failed to read credentials file";
        return false;
    }

    // Parse JSON
    try {
        json creds_json = json::parse(*file_contents);
        return from_json(creds_json);
    } catch (const std::exception& e) {
        last_error_ = std::string("Failed to parse credentials JSON: ") + e.what();
        return false;
    }
}

bool OAuthAccountPool::save_to_disk() {
    // Acquire exclusive lock for writing
    FileLock lock(lock_file_);
    if (!lock.acquire(FileLock::LockType::EXCLUSIVE, LOCK_TIMEOUT_SECONDS)) {
        last_error_ = "Failed to acquire lock for writing credentials";
        return false;
    }

    // Serialize to JSON
    json creds_json = to_json();
    std::string json_str = creds_json.dump(2);  // Pretty print with 2-space indent

    // Write atomically (temp file + rename)
    if (!write_file_atomic(credentials_file_, json_str)) {
        last_error_ = "Failed to write credentials file";
        return false;
    }

    return true;
}

bool OAuthAccountPool::update_on_disk(std::function<bool(void)> modify_callback) {
    // Acquire exclusive lock for atomic read-modify-write
    FileLock lock(lock_file_);
    if (!lock.acquire(FileLock::LockType::EXCLUSIVE, LOCK_TIMEOUT_SECONDS)) {
        last_error_ = "Failed to acquire lock for updating credentials";
        return false;
    }

    // Load current state
    if (credentials_exist()) {
        auto file_contents = read_file(credentials_file_);
        if (file_contents) {
            try {
                json creds_json = json::parse(*file_contents);
                from_json(creds_json);
            } catch (...) {
                // If parse fails, continue with empty state
            }
        }
    }

    // Execute modification callback
    bool should_save = modify_callback();
    if (!should_save) {
        return false;
    }

    // Save modified state
    json creds_json = to_json();
    std::string json_str = creds_json.dump(2);

    if (!write_file_atomic(credentials_file_, json_str)) {
        last_error_ = "Failed to write updated credentials";
        return false;
    }

    return true;
}

// ============================================================================
// FILE I/O HELPERS
// ============================================================================

std::optional<std::string> OAuthAccountPool::read_file(const std::filesystem::path& path) const {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

bool OAuthAccountPool::write_file_atomic(const std::filesystem::path& path, const std::string& data) {
    // Write to temp file
    std::ofstream tmp_file(credentials_file_tmp_);
    if (!tmp_file.is_open()) {
        return false;
    }

    tmp_file << data;
    tmp_file.close();

    if (!tmp_file.good()) {
        std::filesystem::remove(credentials_file_tmp_);
        return false;
    }

    // Set restrictive permissions (owner read/write only)
    chmod(credentials_file_tmp_.c_str(), 0600);

    // Atomic rename
    try {
        std::filesystem::rename(credentials_file_tmp_, path);
        return true;
    } catch (const std::exception& e) {
        std::filesystem::remove(credentials_file_tmp_);
        return false;
    }
}

// ============================================================================
// ACCOUNT MANAGEMENT
// ============================================================================

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

    // SIMPLIFIED: No waiting logic - just return nullptr
    // Let the CALLER (Client) handle waiting and retrying
    // This prevents the dangerous mutex_.unlock()/mutex_.lock() pattern that could cause:
    // - Invalid iterators (if accounts_ modified while unlocked)
    // - Segmentation faults
    // - Race conditions

    // Check if any will become available soon (for error message)
    auto soonest = get_soonest_available_account();
    if (soonest) {
        int wait_seconds = soonest->seconds_until_available();
        last_error_ = "No OAuth accounts available (all rate limited, retry in " +
                     std::to_string(wait_seconds) + " seconds)";
    } else {
        last_error_ = "No OAuth accounts available (all rate limited or expired)";
    }

    return nullptr;
}

void OAuthAccountPool::mark_rate_limited(const std::string& account_uuid, int retry_after_seconds) {
    // CRITICAL: Must save to disk so OTHER PROCESSES see the rate limit!
    // This prevents all processes from hammering the same rate-limited account.

    auto now = std::chrono::system_clock::now();
    auto now_timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    double rate_limited_until_timestamp = now_timestamp + retry_after_seconds;

    // Atomic update on disk
    update_on_disk([&]() {
        std::lock_guard<std::mutex> lock(mutex_);

        // Find account and update rate limit
        auto it = find_account_by_uuid(account_uuid);
        if (it != accounts_.end()) {
            it->credentials.rate_limited_until = rate_limited_until_timestamp;
            return true;  // Save to disk
        }
        return false;  // Account not found, don't save
    });
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

    // Check if account is rate limited (uses persisted field from disk - shared across processes!)
    if (it->credentials.is_rate_limited()) {
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

// ============================================================================
// SERIALIZATION (private helpers - renamed from load_from_json/save_to_json)
// ============================================================================

bool OAuthAccountPool::from_json(const json& j) {
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
            creds.rate_limited_until = account_json.value("rate_limited_until", 0.0);  // Load rate limits from disk!

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

json OAuthAccountPool::to_json() const {
    std::lock_guard<std::mutex> lock(mutex_);

    json accounts_array = json::array();

    for (const auto& account : accounts_) {
        json account_json;
        account_json["priority"] = account.priority;
        account_json["access_token"] = account.credentials.access_token;
        account_json["refresh_token"] = account.credentials.refresh_token;
        account_json["expires_at"] = account.credentials.expires_at;
        account_json["account_uuid"] = account.credentials.account_uuid;
        account_json["rate_limited_until"] = account.credentials.rate_limited_until;  // Persist rate limits!
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

        // Check if rate limited (from persisted field on disk)
        info.is_rate_limited = account.credentials.is_rate_limited();
        if (info.is_rate_limited) {
            auto now = std::chrono::system_clock::now();
            auto now_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                now.time_since_epoch()).count();
            info.seconds_until_available = static_cast<int>(
                account.credentials.rate_limited_until - now_timestamp);
            if (info.seconds_until_available < 0) {
                info.seconds_until_available = 0;
            }
        } else {
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
