#pragma once

#include <string>
#include <chrono>

namespace claude {

// OAuth credentials structure
struct OAuthCredentials {
    std::string access_token;
    std::string refresh_token;
    double expires_at = 0;  // Unix timestamp
    std::string account_uuid;

    bool is_expired(int buffer_seconds = 300) const {
        auto now = std::chrono::system_clock::now();
        auto now_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        return now_timestamp + buffer_seconds >= expires_at;
    }
};

} // namespace claude
