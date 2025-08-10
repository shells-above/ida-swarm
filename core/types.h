#pragma once

#include "common_base.h"
#include "api/api_common.h"  // For LogLevel enum
#include "api/message_types.h"

namespace llm_re {

// Session information for tracking agent runs
struct SessionInfo {
    std::string id;
    std::string task;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    api::TokenUsage token_usage;
    int tool_calls = 0;
    int message_count = 0;
    bool success = true;
    std::string error_message;
    long duration_ms = 0;
};

// Log entry structure
struct LogEntry {
    std::chrono::system_clock::time_point timestamp;
    LogLevel level;
    std::string message;
    std::string source;

    static std::string level_to_string(LogLevel l) {
        switch (l) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARNING: return "WARNING";
            case LogLevel::ERROR: return "ERROR";
        }
        return "UNKNOWN";
    }
};

} // namespace llm_re