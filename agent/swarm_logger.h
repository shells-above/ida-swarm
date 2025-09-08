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

namespace llm_re::agent {

class SwarmLogger {
private:
    std::ofstream log_file_;
    std::mutex mutex_;
    std::string log_path_;
    std::string agent_id_;
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
    SwarmLogger() = default;
    
    ~SwarmLogger() {
        if (log_file_.is_open()) {
            log_file_ << "[" << get_timestamp() << "] === Agent Session Ended ===\n";
            log_file_.close();
        }
    }
    
    bool initialize(const std::string& binary_name, const std::string& agent_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (initialized_) return true;
        
        agent_id_ = agent_id;
        
        // Create log directory for this agent
        std::filesystem::path log_dir = std::filesystem::path("/tmp/ida_swarm_workspace") 
                                       / binary_name 
                                       / "agents" 
                                       / agent_id;
        try {
            std::filesystem::create_directories(log_dir);
        } catch (const std::exception& e) {
            ::msg("SwarmLogger: Failed to create log directory: %s\n", e.what());
            return false;
        }
        
        // Open log file with append mode
        log_path_ = (log_dir / "agent.log").string();
        log_file_.open(log_path_, std::ios::out | std::ios::app);
        
        if (log_file_.is_open()) {
            initialized_ = true;
            log_file_ << "\n=== SwarmAgent Session Started at " << get_timestamp() 
                     << " (Agent: " << agent_id << ") ===\n";
            log_file_.flush();
            
            // Also log to IDA console that we've initialized the logger
            ::msg("SwarmLogger: Logging to %s\n", log_path_.c_str());
            return true;
        }
        
        ::msg("SwarmLogger: Failed to open log file at %s\n", log_path_.c_str());
        return false;
    }
    
    void log(const char* format, ...) {
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
        
        // Get timestamp once
        std::string timestamp = get_timestamp();
        
        // Write to file with timestamp and immediate flush
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (log_file_.is_open()) {
                log_file_ << "[" << timestamp << "] [" << agent_id_ << "] " << buffer.data();
                log_file_.flush();  // CRITICAL: Immediate flush to ensure data is written before any crash
            }
        }
        
        // Also write to IDA console (best effort, might not flush before crash)
        ::msg("%s", buffer.data());
    }
    
    // Convenience wrapper for cleaner syntax
    template<typename... Args>
    void logf(const char* format, Args... args) {
        log(format, args...);
    }
    
    // Get log file path for debugging
    const std::string& get_log_path() const { return log_path_; }
};

// Global logger instance for each swarm agent (each agent process has its own)
extern SwarmLogger g_swarm_logger;

// Macro to replace msg() calls in SwarmAgent
#define SWARM_LOG(...) g_swarm_logger.log(__VA_ARGS__)

} // namespace llm_re::agent