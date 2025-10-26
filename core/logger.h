#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <cstdarg>
#include "../sdk/common.h"

namespace llm_re {

/**
 * Simple unified logger for both orchestrator and swarm agents.
 *
 * Logs to:
 * - File (with timestamp and log level)
 * - IDA console
 *
 * Thread-safe with mutex protection.
 * Initialize once at startup with log path and context name.
 */
class Logger {
private:
    std::ofstream log_file_;
    std::mutex mutex_;
    std::string context_;      // "orchestrator" or agent ID like "agent_1"
    bool initialized_ = false;

    std::string get_timestamp();
    std::string level_to_string(claude::LogLevel level);

public:
    Logger() = default;
    ~Logger();

    /**
     * Initialize the logger with log file path and context name.
     *
     * @param log_path Absolute path to log file (e.g., "/tmp/ida_swarm_workspace/binary/orchestrator.log")
     * @param context Context name for log prefix (e.g., "orchestrator" or "agent_1")
     * @return true if initialization succeeded
     */
    bool initialize(const std::string& log_path, const std::string& context);

    /**
     * Log a message with specified level.
     *
     * @param level Log level (DEBUG, INFO, WARNING, ERROR)
     * @param format Printf-style format string
     */
    void log(claude::LogLevel level, const char* format, ...);

    /**
     * Log a message at INFO level (convenience method).
     *
     * @param format Printf-style format string
     */
    void log(const char* format, ...);

    /**
     * Check if logger has been initialized.
     */
    bool is_initialized() const { return initialized_; }

    /**
     * Get the context name (for debugging).
     */
    const std::string& get_context() const { return context_; }
};

// Global logger instance
extern Logger g_logger;

} // namespace llm_re

// Unified logging macros - use these throughout the codebase
#define LOG(...) llm_re::g_logger.log(__VA_ARGS__)
#define LOG_DEBUG(...) llm_re::g_logger.log(claude::LogLevel::DEBUG, __VA_ARGS__)
#define LOG_INFO(...) llm_re::g_logger.log(claude::LogLevel::INFO, __VA_ARGS__)
#define LOG_WARNING(...) llm_re::g_logger.log(claude::LogLevel::WARNING, __VA_ARGS__)
#define LOG_ERROR(...) llm_re::g_logger.log(claude::LogLevel::ERROR, __VA_ARGS__)
