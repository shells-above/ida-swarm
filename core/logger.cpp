#include "logger.h"
#include <chrono>
#include <ctime>
#include <vector>
#include <filesystem>
// IDA SDK headers - must include pro.h before other IDA headers
#include <pro.h>
#include <kernwin.hpp>

namespace llm_re {

// Global logger instance
Logger g_logger;

std::string Logger::get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    char time_buffer[32];
    std::strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", std::localtime(&time_t));

    // Build timestamp string
    std::string result(time_buffer);
    result += ".";

    // Add milliseconds with padding
    int ms_val = static_cast<int>(ms.count());
    if (ms_val < 10) {
        result += "00";
    } else if (ms_val < 100) {
        result += "0";
    }
    result += std::to_string(ms_val);

    return result;
}

std::string Logger::level_to_string(claude::LogLevel level) {
    switch (level) {
        case claude::LogLevel::DEBUG:   return "DEBUG";
        case claude::LogLevel::INFO:    return "INFO ";
        case claude::LogLevel::WARNING: return "WARN ";
        case claude::LogLevel::ERROR:   return "ERROR";
        default:                        return "?????";
    }
}

Logger::~Logger() {
    if (log_file_.is_open()) {
        std::lock_guard<std::mutex> lock(mutex_);
        log_file_ << "[" << get_timestamp() << "] === Session Ended ===\n";
        log_file_.close();
    }
}

bool Logger::initialize(const std::string& log_path, const std::string& context) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        return true;
    }

    context_ = context;

    // Create parent directory if needed
    std::filesystem::path path(log_path);
    try {
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
    } catch (const std::exception& e) {
        ::msg("Logger: Failed to create directory for %s: %s\n", log_path.c_str(), e.what());
        return false;
    }

    // Open log file with truncate mode to clear previous session
    log_file_.open(log_path, std::ios::out | std::ios::trunc);

    if (!log_file_.is_open()) {
        ::msg("Logger: Failed to open log file at %s\n", log_path.c_str());
        return false;
    }

    initialized_ = true;
    log_file_ << "\n=== Session Started at " << get_timestamp()
              << " (Context: " << context_ << ") ===\n";
    log_file_.flush();

    ::msg("Logger: Initialized for '%s' at %s\n", context_.c_str(), log_path.c_str());
    return true;
}

void Logger::log(claude::LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);

    // First pass to get length
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    // Allocate buffer and format
    std::vector<char> buffer(len + 1);
    vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);

    std::string message(buffer.data());
    std::string timestamp = get_timestamp();
    std::string level_str = level_to_string(level);

    // Write to file with timestamp and level
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (log_file_.is_open()) {
            log_file_ << "[" << timestamp << "] [" << level_str << "] [" << context_ << "] " << message;
            log_file_.flush();  // Immediate flush to ensure data is written
        }
    }

    // Also write to IDA console
    ::msg("%s", message.c_str());
}

void Logger::log(const char* format, ...) {
    va_list args;
    va_start(args, format);

    // First pass to get length
    va_list args_copy;
    va_copy(args_copy, args);
    int len = vsnprintf(nullptr, 0, format, args_copy);
    va_end(args_copy);

    // Allocate buffer and format
    std::vector<char> buffer(len + 1);
    vsnprintf(buffer.data(), buffer.size(), format, args);
    va_end(args);

    std::string message(buffer.data());
    std::string timestamp = get_timestamp();

    // Write to file with timestamp (INFO level by default)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (log_file_.is_open()) {
            log_file_ << "[" << timestamp << "] [INFO ] [" << context_ << "] " << message;
            log_file_.flush();
        }
    }

    // Also write to IDA console
    ::msg("%s", message.c_str());
}

} // namespace llm_re
