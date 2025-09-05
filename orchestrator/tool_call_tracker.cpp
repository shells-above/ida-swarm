#include "tool_call_tracker.h"
#include "orchestrator_logger.h"
#include <chrono>
#include <sstream>
#include <filesystem>

namespace llm_re::orchestrator {

const std::vector<std::string> ToolCallTracker::WRITE_TOOLS = {
    "set_name",
    "set_comment",
    "set_function_prototype",
    "set_variable",
    "set_local_type",
    "patch_bytes",
    "patch_assembly"
};

ToolCallTracker::ToolCallTracker(const std::string& binary_name, EventBus* event_bus) 
    : binary_name_(binary_name), event_bus_(event_bus) {
}

ToolCallTracker::~ToolCallTracker() {
    stop_monitoring();
    finalize_statements();
    if (db_) {
        sqlite3_close(db_);
    }
}

bool ToolCallTracker::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Create workspace directory structure
    std::filesystem::path workspace_dir = std::filesystem::path("/tmp/ida_swarm_workspace") / binary_name_;
    try {
        std::filesystem::create_directories(workspace_dir);
        ORCH_LOG("ToolCallTracker: Created workspace directory: %s\n", workspace_dir.string().c_str());
    } catch (const std::exception& e) {
        ORCH_LOG("ToolCallTracker: Failed to create workspace directory: %s\n", e.what());
        return false;
    }
    
    // Open database with binary-specific path
    std::filesystem::path db_path = workspace_dir / "tool_calls.db";
    int rc = sqlite3_open(db_path.string().c_str(), &db_);
    
    if (rc != SQLITE_OK) {
        ORCH_LOG("ToolCallTracker: Failed to open database: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    // Create tables
    if (!create_tables()) {
        ORCH_LOG("ToolCallTracker: Failed to create tables\n");
        return false;
    }
    
    // Prepare statements
    if (!prepare_statements()) {
        ORCH_LOG("ToolCallTracker: Failed to prepare statements\n");
        return false;
    }
    
    ORCH_LOG("ToolCallTracker: Initialized with database at %s\n", db_path.string().c_str());
    return true;
}

bool ToolCallTracker::create_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS tool_calls (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            agent_id TEXT NOT NULL,
            tool_name TEXT NOT NULL,
            address INTEGER NOT NULL,
            parameters TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            is_write INTEGER NOT NULL
        );
        
        CREATE INDEX IF NOT EXISTS idx_agent ON tool_calls(agent_id);
        CREATE INDEX IF NOT EXISTS idx_address ON tool_calls(address);
        CREATE INDEX IF NOT EXISTS idx_tool ON tool_calls(tool_name);
        CREATE INDEX IF NOT EXISTS idx_agent_tool ON tool_calls(agent_id, tool_name);
        CREATE INDEX IF NOT EXISTS idx_address_write ON tool_calls(address, is_write);
    )";
    
    return execute_sql(sql);
}

bool ToolCallTracker::prepare_statements() {
    // Insert statement
    const char* insert_sql = 
        "INSERT INTO tool_calls (agent_id, tool_name, address, parameters, timestamp, is_write) "
        "VALUES (?, ?, ?, ?, ?, ?)";
    
    if (sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt_, nullptr) != SQLITE_OK) {
        ORCH_LOG("ToolCallTracker: Failed to prepare insert statement: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    // Select by agent
    const char* select_agent_sql = 
        "SELECT * FROM tool_calls WHERE agent_id = ? ORDER BY timestamp";
    
    if (sqlite3_prepare_v2(db_, select_agent_sql, -1, &select_by_agent_stmt_, nullptr) != SQLITE_OK) {
        return false;
    }
    
    // Select by address
    const char* select_address_sql = 
        "SELECT * FROM tool_calls WHERE address = ? ORDER BY timestamp";
    
    if (sqlite3_prepare_v2(db_, select_address_sql, -1, &select_by_address_stmt_, nullptr) != SQLITE_OK) {
        return false;
    }
    
    // Select conflicts
    const char* select_conflicts_sql = 
        "SELECT * FROM tool_calls WHERE address = ? AND is_write = 1 AND agent_id != ? "
        "ORDER BY timestamp";
    
    if (sqlite3_prepare_v2(db_, select_conflicts_sql, -1, &select_conflicts_stmt_, nullptr) != SQLITE_OK) {
        return false;
    }
    
    // Delete by agent
    const char* delete_agent_sql = 
        "DELETE FROM tool_calls WHERE agent_id = ?";
    
    if (sqlite3_prepare_v2(db_, delete_agent_sql, -1, &delete_by_agent_stmt_, nullptr) != SQLITE_OK) {
        return false;
    }
    
    return true;
}

void ToolCallTracker::finalize_statements() {
    if (insert_stmt_) sqlite3_finalize(insert_stmt_);
    if (select_by_agent_stmt_) sqlite3_finalize(select_by_agent_stmt_);
    if (select_by_address_stmt_) sqlite3_finalize(select_by_address_stmt_);
    if (select_conflicts_stmt_) sqlite3_finalize(select_conflicts_stmt_);
    if (delete_by_agent_stmt_) sqlite3_finalize(delete_by_agent_stmt_);
}

bool ToolCallTracker::record_tool_call(const std::string& agent_id,
                                      const std::string& tool_name,
                                      ea_t address,
                                      const json& parameters) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ORCH_LOG("ToolCallTracker: Recording call - agent=%s, tool=%s, addr=0x%llx\n", 
        agent_id.c_str(), tool_name.c_str(), address);
    
    if (!db_) {
        ORCH_LOG("ToolCallTracker: ERROR - Database not initialized\n");
        return false;
    }
    
    if (!insert_stmt_) {
        ORCH_LOG("ToolCallTracker: ERROR - Insert statement not prepared\n");
        return false;
    }
    
    sqlite3_reset(insert_stmt_);
    sqlite3_clear_bindings(insert_stmt_);
    
    // Bind parameters
    int rc = sqlite3_bind_text(insert_stmt_, 1, agent_id.c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        ORCH_LOG("ToolCallTracker: Failed to bind agent_id: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    rc = sqlite3_bind_text(insert_stmt_, 2, tool_name.c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        ORCH_LOG("ToolCallTracker: Failed to bind tool_name: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    rc = sqlite3_bind_int64(insert_stmt_, 3, address);
    if (rc != SQLITE_OK) {
        ORCH_LOG("ToolCallTracker: Failed to bind address: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    std::string params_str = parameters.dump();
    rc = sqlite3_bind_text(insert_stmt_, 4, params_str.c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        ORCH_LOG("ToolCallTracker: Failed to bind parameters: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    rc = sqlite3_bind_int64(insert_stmt_, 5, timestamp);
    if (rc != SQLITE_OK) {
        ORCH_LOG("ToolCallTracker: Failed to bind timestamp: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    rc = sqlite3_bind_int(insert_stmt_, 6, is_write_tool(tool_name) ? 1 : 0);
    if (rc != SQLITE_OK) {
        ORCH_LOG("ToolCallTracker: Failed to bind is_write: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    // Execute
    rc = sqlite3_step(insert_stmt_);
    
    if (rc != SQLITE_DONE) {
        ORCH_LOG("ToolCallTracker: Failed to insert tool call: %s (code=%d)\n", 
            sqlite3_errmsg(db_), rc);
        return false;
    }
    
    ORCH_LOG("ToolCallTracker: Successfully recorded tool call (rowid=%lld)\n", 
        sqlite3_last_insert_rowid(db_));
    
    return true;
}

std::vector<ToolConflict> ToolCallTracker::check_for_conflicts(const std::string& agent_id,
                                                               const std::string& tool_name,
                                                               ea_t address) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<ToolConflict> conflicts;
    
    // Only check for write operations
    if (!is_write_tool(tool_name)) {
        return conflicts;
    }
    
    sqlite3_reset(select_conflicts_stmt_);
    sqlite3_bind_int64(select_conflicts_stmt_, 1, address);
    sqlite3_bind_text(select_conflicts_stmt_, 2, agent_id.c_str(), -1, SQLITE_STATIC);
    
    // Create a dummy current call for comparison
    ToolCall current_call;
    current_call.agent_id = agent_id;
    current_call.tool_name = tool_name;
    current_call.address = address;
    current_call.timestamp = std::chrono::system_clock::now();
    
    while (sqlite3_step(select_conflicts_stmt_) == SQLITE_ROW) {
        ToolCall existing = row_to_tool_call(select_conflicts_stmt_);
        
        // Check if it's the same type of operation
        if (existing.tool_name == tool_name) {
            ToolConflict conflict;
            conflict.first_call = existing;
            conflict.second_call = current_call;
            conflict.conflict_type = tool_name;
            conflicts.push_back(conflict);
        }
    }
    
    return conflicts;
}

std::vector<ToolCall> ToolCallTracker::get_agent_tool_calls(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<ToolCall> calls;
    
    sqlite3_reset(select_by_agent_stmt_);
    sqlite3_bind_text(select_by_agent_stmt_, 1, agent_id.c_str(), -1, SQLITE_STATIC);
    
    while (sqlite3_step(select_by_agent_stmt_) == SQLITE_ROW) {
        calls.push_back(row_to_tool_call(select_by_agent_stmt_));
    }
    
    return calls;
}

std::vector<ToolCall> ToolCallTracker::get_address_tool_calls(ea_t address) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<ToolCall> calls;
    
    sqlite3_reset(select_by_address_stmt_);
    sqlite3_bind_int64(select_by_address_stmt_, 1, address);
    
    while (sqlite3_step(select_by_address_stmt_) == SQLITE_ROW) {
        calls.push_back(row_to_tool_call(select_by_address_stmt_));
    }
    
    return calls;
}

std::vector<ToolCall> ToolCallTracker::get_agent_write_operations(const std::string& agent_id) {
    auto all_calls = get_agent_tool_calls(agent_id);
    
    std::vector<ToolCall> write_calls;
    for (const auto& call : all_calls) {
        if (call.is_write_operation) {
            write_calls.push_back(call);
        }
    }
    
    return write_calls;
}

AgentToolStats ToolCallTracker::get_agent_stats(const std::string& agent_id) {
    auto calls = get_agent_tool_calls(agent_id);
    
    AgentToolStats stats;
    stats.total_calls = calls.size();
    
    for (const auto& call : calls) {
        if (call.is_write_operation) {
            stats.write_calls++;
        } else {
            stats.read_calls++;
        }
    }
    
    // Count conflicts (simplified - would need more complex query)
    std::set<ea_t> addresses;
    for (const auto& call : calls) {
        if (call.is_write_operation) {
            addresses.insert(call.address);
        }
    }
    
    for (ea_t addr : addresses) {
        auto conflicts = check_for_conflicts(agent_id, "check", addr);
        stats.conflicts += conflicts.size();
    }
    
    return stats;
}

bool ToolCallTracker::clear_agent_data(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    sqlite3_reset(delete_by_agent_stmt_);
    sqlite3_bind_text(delete_by_agent_stmt_, 1, agent_id.c_str(), -1, SQLITE_STATIC);
    
    int rc = sqlite3_step(delete_by_agent_stmt_);
    return rc == SQLITE_DONE;
}

bool ToolCallTracker::is_write_tool(const std::string& tool_name) {
    return std::find(WRITE_TOOLS.begin(), WRITE_TOOLS.end(), tool_name) != WRITE_TOOLS.end();
}

bool ToolCallTracker::execute_sql(const char* sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    
    if (rc != SQLITE_OK) {
        ORCH_LOG("ToolCallTracker: SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        return false;
    }
    
    return true;
}

ToolCall ToolCallTracker::row_to_tool_call(sqlite3_stmt* stmt) {
    ToolCall call;
    
    call.id = sqlite3_column_int(stmt, 0);
    call.agent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    call.tool_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    call.address = sqlite3_column_int64(stmt, 3);
    
    const char* params_str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    if (params_str) {
        call.parameters = json::parse(params_str);
    }
    
    auto timestamp_sec = sqlite3_column_int64(stmt, 5);
    call.timestamp = std::chrono::system_clock::time_point(std::chrono::seconds(timestamp_sec));
    
    call.is_write_operation = sqlite3_column_int(stmt, 6) != 0;
    
    return call;
}

void ToolCallTracker::start_monitoring() {
    if (monitoring_ || !event_bus_) {
        return;  // Already monitoring or no event bus
    }
    
    monitoring_ = true;
    monitor_thread_ = std::thread(&ToolCallTracker::monitor_loop, this);
    ORCH_LOG("ToolCallTracker: Started monitoring thread\n");
}

void ToolCallTracker::stop_monitoring() {
    if (!monitoring_) {
        return;  // Not monitoring
    }
    
    monitoring_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    ORCH_LOG("ToolCallTracker: Stopped monitoring thread\n");
}

void ToolCallTracker::monitor_loop() {
    while (monitoring_) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            
            if (!db_) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            
            // Query for new tool calls
            const char* query = "SELECT id, agent_id, tool_name, address, parameters, timestamp, is_write "
                               "FROM tool_calls WHERE id > ? ORDER BY id";
            
            sqlite3_stmt* stmt = nullptr;
            int rc = sqlite3_prepare_v2(db_, query, -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                ORCH_LOG("ToolCallTracker: Failed to prepare monitor query: %s\n", sqlite3_errmsg(db_));
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            
            sqlite3_bind_int64(stmt, 1, last_seen_id_);
            
            // Process new tool calls
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                ToolCall call = row_to_tool_call(stmt);
                
                // Emit event for this tool call
                json tool_data = {
                    {"tool_name", call.tool_name},
                    {"address", call.address},
                    {"parameters", call.parameters},
                    {"is_write", call.is_write_operation},
                    {"timestamp", std::chrono::duration_cast<std::chrono::seconds>(
                        call.timestamp.time_since_epoch()).count()}
                };
                
                if (event_bus_) {
                    event_bus_->publish(AgentEvent(AgentEvent::TOOL_CALL, call.agent_id, tool_data));
                }
                
                // Update last seen ID
                last_seen_id_ = call.id;
                
                ORCH_LOG("ToolCallTracker: Emitted TOOL_CALL event for %s - %s at 0x%llx\n",
                    call.agent_id.c_str(), call.tool_name.c_str(), call.address);
            }
            
            sqlite3_finalize(stmt);
        }
        
        // Sleep before next poll
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace llm_re::orchestrator