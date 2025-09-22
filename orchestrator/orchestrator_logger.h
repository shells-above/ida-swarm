#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <cstdarg>
#include <ctime>
#include <vector>

#include <pro.h>
#include <kernwin.hpp>
#include "../agent/event_bus.h"
#include "../sdk/common.h"

using llm_re::EventBus;
using llm_re::AgentEvent;
using llm_re::get_event_bus;
using claude::LogLevel;

namespace llm_re::orchestrator {

class OrchestratorLogger {
private:
    std::ofstream log_file_;
    std::mutex mutex_;
    std::string log_path_;
    bool initialized_ = false;

    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        char time_buffer[32];
        std::strftime(time_buffer, sizeof(time_buffer), "%H:%M:%S", std::localtime(&time_t));
        
        // Build timestamp string manually to avoid format issues
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

public:
    OrchestratorLogger() = default;
    
    ~OrchestratorLogger() {
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }
    
    bool initialize(const std::string& binary_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (initialized_) return true;
        
        // Create log directory
        std::filesystem::path log_dir = std::filesystem::path("/tmp/ida_swarm_workspace") / binary_name;
        try {
            std::filesystem::create_directories(log_dir);
        } catch (const std::exception& e) {
            return false;
        }
        
        // Open log file with truncate mode to clear previous session
        log_path_ = (log_dir / "orchestrator.log").string();
        log_file_.open(log_path_, std::ios::out | std::ios::trunc);
        
        if (log_file_.is_open()) {
            initialized_ = true;
            log_file_ << "\n=== Orchestrator Session Started at " << get_timestamp() << " ===\n";
            log_file_.flush();
            return true;
        }
        
        return false;
    }
    
    void log(claude::LogLevel level, const char* format, ...) {
        // Format the message using va_list
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
        
        // Get timestamp once
        std::string timestamp = get_timestamp();
        
        // Write to file with timestamp
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (log_file_.is_open()) {
                log_file_ << "[" << timestamp << "] " << "[" << level_to_string(level) << "] " << message;
                log_file_.flush();  // Immediate flush to ensure data is written
            }
        }
        
        // Emit EventBus event for UI logging
        EventBus& bus = get_event_bus();
        bus.emit_log("orchestrator", static_cast<LogLevel>(level), message);
        
        // Also try to write to IDA console (best effort, might not work if UI is hung)
        ::msg("%s", buffer.data());
    }
    
    // Overload for backward compatibility (defaults to INFO level)
    void log(const char* format, ...) {
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
        log_internal(claude::LogLevel::INFO, message);
    }

private:
    void log_internal(claude::LogLevel level, const std::string& message) {
        // Get timestamp once
        std::string timestamp = get_timestamp();
        
        // Write to file with timestamp
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (log_file_.is_open()) {
                log_file_ << "[" << timestamp << "] " << "[" << level_to_string(level) << "] " << message;
                log_file_.flush();
            }
        }
        
        // Emit EventBus event for UI logging
        EventBus& bus = get_event_bus();
        bus.emit_log("orchestrator", static_cast<LogLevel>(level), message);
        
        // Also try to write to IDA console
        ::msg("%s", message.c_str());
    }
    
    std::string level_to_string(claude::LogLevel level) {
        switch (level) {
            case claude::LogLevel::DEBUG:   return "DEBUG";
            case claude::LogLevel::INFO:    return "INFO ";
            case claude::LogLevel::WARNING: return "WARN ";
            case claude::LogLevel::ERROR:   return "ERROR";
            default:                        return "?????";
        }
    }

public:
    
    // Convenience wrapper for cleaner syntax
    template<typename... Args>
    void logf(const char* format, Args... args) {
        log(format, args...);
    }
};

// Global logger instance for the orchestrator system
extern OrchestratorLogger g_orch_logger;

// Macros for different log levels
#define ORCH_LOG(...) g_orch_logger.log(__VA_ARGS__)
#define ORCH_LOG_DEBUG(...) g_orch_logger.log(claude::LogLevel::DEBUG, __VA_ARGS__)
#define ORCH_LOG_INFO(...) g_orch_logger.log(claude::LogLevel::INFO, __VA_ARGS__)
#define ORCH_LOG_WARNING(...) g_orch_logger.log(claude::LogLevel::WARNING, __VA_ARGS__)
#define ORCH_LOG_ERROR(...) g_orch_logger.log(claude::LogLevel::ERROR, __VA_ARGS__)

} // namespace llm_re::orchestrator