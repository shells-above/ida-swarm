#pragma once

#include "../core/common.h"
#include "../agent/event_bus.h"
#include <sqlite3.h>
#include <memory>
#include <mutex>
#include <vector>
#include <thread>
#include <atomic>

namespace llm_re::orchestrator {

// Information about a tool call
struct ToolCall {
    int id;
    std::string agent_id;
    std::string tool_name;
    ea_t address;
    json parameters;
    std::chrono::system_clock::time_point timestamp;
    bool is_write_operation;
};

// Conflict information
struct ToolConflict {
    ToolCall first_call;
    ToolCall second_call;
    std::string conflict_type;  // "name", "comment", "prototype", etc.
};

// Statistics for an agent
struct AgentToolStats {
    int total_calls = 0;
    int write_calls = 0;
    int read_calls = 0;
    int conflicts = 0;
};

// Tracks all tool calls across all agents
class ToolCallTracker {
public:
    explicit ToolCallTracker(const std::string& binary_name, EventBus* event_bus = nullptr);
    ~ToolCallTracker();
    
    // Initialize the database
    bool initialize();
    
    // Record a tool call
    bool record_tool_call(const std::string& agent_id,
                         const std::string& tool_name,
                         ea_t address,
                         const json& parameters);
    
    // Check for conflicts before a write operation
    std::vector<ToolConflict> check_for_conflicts(const std::string& agent_id,
                                                  const std::string& tool_name,
                                                  ea_t address);
    
    // Get all tool calls for an agent
    std::vector<ToolCall> get_agent_tool_calls(const std::string& agent_id);
    
    // Get all tool calls at an address
    std::vector<ToolCall> get_address_tool_calls(ea_t address);
    
    // Get agent statistics
    AgentToolStats get_agent_stats(const std::string& agent_id);
    
    // Get all write operations for merging
    std::vector<ToolCall> get_agent_write_operations(const std::string& agent_id);
    
    // Clear all data for an agent
    bool clear_agent_data(const std::string& agent_id);
    
    // Check if tool is a write operation
    static bool is_write_tool(const std::string& tool_name);
    
    // Start/stop monitoring for new tool calls
    void start_monitoring();
    void stop_monitoring();
    
private:
    sqlite3* db_ = nullptr;
    std::string binary_name_;
    EventBus* event_bus_;
    mutable std::mutex mutex_;
    
    // Monitoring thread
    std::thread monitor_thread_;
    std::atomic<bool> monitoring_{false};
    int64_t last_seen_id_ = 0;
    
    // Database operations
    bool create_tables();
    bool prepare_statements();
    void finalize_statements();
    
    // Prepared statements
    sqlite3_stmt* insert_stmt_ = nullptr;
    sqlite3_stmt* select_by_agent_stmt_ = nullptr;
    sqlite3_stmt* select_by_address_stmt_ = nullptr;
    sqlite3_stmt* select_conflicts_stmt_ = nullptr;
    sqlite3_stmt* delete_by_agent_stmt_ = nullptr;
    
    // Helper to execute a simple SQL statement
    bool execute_sql(const char* sql);
    
    // Convert tool call to/from database row
    ToolCall row_to_tool_call(sqlite3_stmt* stmt);
    
    // Monitoring thread function
    void monitor_loop();
    
    // List of write tools
    static const std::vector<std::string> WRITE_TOOLS;
};

} // namespace llm_re::orchestrator