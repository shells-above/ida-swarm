#include "tool_call_tracker.h"
#include "../core/logger.h"
#include <chrono>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <fcntl.h>      // For open()
#include <sys/file.h>   // For flock()
#include <cerrno>       // For errno
#include <unistd.h>     // For close()

namespace llm_re::orchestrator {

const std::vector<std::string> ToolCallTracker::WRITE_TOOLS = {
    "set_name",
    "set_comment",
    "set_function_prototype",
    "set_variable",
    "set_local_type",
    "patch_bytes",
    "patch_assembly",
    "start_semantic_patch",
    "compile_replacement",
    "preview_semantic_patch",
    "finalize_semantic_patch"
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

    LOG_INFO("ToolCallTracker: Acquiring inter-process lock for database initialization...\n");

    // Create workspace directory structure first (needed for per-binary lock file)
    std::filesystem::path workspace_dir = std::filesystem::path("/tmp/ida_swarm_workspace") / binary_name_;
    try {
        std::filesystem::create_directories(workspace_dir);
        LOG_INFO("ToolCallTracker: Created workspace directory: %s\n", workspace_dir.string().c_str());
    } catch (const std::exception& e) {
        LOG_INFO("ToolCallTracker: Failed to create workspace directory: %s\n", e.what());
        return false;
    }

    // Create/open lock file for inter-process synchronization
    // This is PER-BINARY so different analysis sessions don't block each other
    std::filesystem::path lock_file = workspace_dir / "tool_tracker.lock";
    int fd = open(lock_file.string().c_str(), O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        LOG_INFO("ToolCallTracker: WARNING - Failed to open lock file: %s\n", strerror(errno));
        LOG_INFO("ToolCallTracker: Continuing without lock (unsafe but better than failing)\n");
    } else {
        // Acquire exclusive lock (blocks until available)
        LOG_INFO("ToolCallTracker: Waiting for initialization lock...\n");
        if (flock(fd, LOCK_EX) != 0) {
            LOG_INFO("ToolCallTracker: WARNING - Failed to acquire lock: %s\n", strerror(errno));
        } else {
            LOG_INFO("ToolCallTracker: Lock acquired successfully\n");
        }
    }

    // Open database with binary-specific path (workspace_dir already created above)
    std::filesystem::path db_path = workspace_dir / "tool_calls.db";
    int rc = sqlite3_open(db_path.string().c_str(), &db_);

    if (rc != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: Failed to open database: %s\n", sqlite3_errmsg(db_));
        if (fd != -1) {
            flock(fd, LOCK_UN);
            close(fd);
        }
        return false;
    }

    // Enable WAL mode for better concurrent access from multiple processes
    char* err_msg = nullptr;
    rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: WARNING - Failed to enable WAL mode: %s\n", err_msg);
        sqlite3_free(err_msg);
        // Continue anyway - WAL is optional but recommended
    } else {
        LOG_INFO("ToolCallTracker: Enabled WAL mode for concurrent access\n");
    }

    // Set busy timeout to 5 seconds to handle concurrent access
    sqlite3_busy_timeout(db_, 5000);
    LOG_INFO("ToolCallTracker: Set busy timeout to 5 seconds\n");

    // Create tables (only first process will actually create them)
    if (!create_tables()) {
        LOG_INFO("ToolCallTracker: Failed to create tables\n");
        sqlite3_close(db_);
        db_ = nullptr;
        if (fd != -1) {
            flock(fd, LOCK_UN);
            close(fd);
        }
        return false;
    }

    // Prepare statements
    if (!prepare_statements()) {
        LOG_INFO("ToolCallTracker: Failed to prepare statements\n");
        sqlite3_close(db_);
        db_ = nullptr;
        if (fd != -1) {
            flock(fd, LOCK_UN);
            close(fd);
        }
        return false;
    }

    // Release lock
    if (fd != -1) {
        flock(fd, LOCK_UN);
        close(fd);
        LOG_INFO("ToolCallTracker: Lock released\n");
    }

    LOG_INFO("ToolCallTracker: Initialized with database at %s\n", db_path.string().c_str());
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
            is_write INTEGER NOT NULL,
            is_manual INTEGER DEFAULT 0
        );
        
        CREATE INDEX IF NOT EXISTS idx_agent ON tool_calls(agent_id);
        CREATE INDEX IF NOT EXISTS idx_address ON tool_calls(address);
        CREATE INDEX IF NOT EXISTS idx_tool ON tool_calls(tool_name);
        CREATE INDEX IF NOT EXISTS idx_agent_tool ON tool_calls(agent_id, tool_name);
        CREATE INDEX IF NOT EXISTS idx_address_write ON tool_calls(address, is_write);
        CREATE INDEX IF NOT EXISTS idx_manual ON tool_calls(is_manual);
    )";
    
    // Check if is_manual column exists and add it if not (for existing databases)
    const char* check_column = "PRAGMA table_info(tool_calls)";
    sqlite3_stmt* stmt;
    bool has_manual_column = false;
    
    if (sqlite3_prepare_v2(db_, check_column, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* col_name = (const char*)sqlite3_column_text(stmt, 1);
            if (col_name && strcmp(col_name, "is_manual") == 0) {
                has_manual_column = true;
                break;
            }
        }
        sqlite3_finalize(stmt);
    }
    
    // Add column if it doesn't exist
    if (!has_manual_column) {
        const char* add_column = "ALTER TABLE tool_calls ADD COLUMN is_manual INTEGER DEFAULT 0";
        if (!execute_sql(add_column)) {
            LOG_INFO("ToolCallTracker: Warning - Could not add is_manual column (may already exist)\n");
        }
    }
    
    return execute_sql(sql);
}

bool ToolCallTracker::prepare_statements() {
    // Insert statement
    const char* insert_sql = 
        "INSERT INTO tool_calls (agent_id, tool_name, address, parameters, timestamp, is_write, is_manual) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)";
    
    if (sqlite3_prepare_v2(db_, insert_sql, -1, &insert_stmt_, nullptr) != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: Failed to prepare insert statement: %s\n", sqlite3_errmsg(db_));
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
    
    LOG_INFO("ToolCallTracker: Recording call - agent=%s, tool=%s, addr=0x%llx\n", 
        agent_id.c_str(), tool_name.c_str(), address);
    
    if (!db_) {
        LOG_INFO("ToolCallTracker: ERROR - Database not initialized\n");
        return false;
    }
    
    if (!insert_stmt_) {
        LOG_INFO("ToolCallTracker: ERROR - Insert statement not prepared\n");
        return false;
    }
    
    sqlite3_reset(insert_stmt_);
    sqlite3_clear_bindings(insert_stmt_);
    
    // Bind parameters
    int rc = sqlite3_bind_text(insert_stmt_, 1, agent_id.c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: Failed to bind agent_id: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    rc = sqlite3_bind_text(insert_stmt_, 2, tool_name.c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: Failed to bind tool_name: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    rc = sqlite3_bind_int64(insert_stmt_, 3, address);
    if (rc != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: Failed to bind address: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    std::string params_str = parameters.dump();
    rc = sqlite3_bind_text(insert_stmt_, 4, params_str.c_str(), -1, SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: Failed to bind parameters: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    rc = sqlite3_bind_int64(insert_stmt_, 5, timestamp);
    if (rc != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: Failed to bind timestamp: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    rc = sqlite3_bind_int(insert_stmt_, 6, is_write_tool(tool_name) ? 1 : 0);
    if (rc != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: Failed to bind is_write: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    // Check if this is a manual execution (look for __is_manual in parameters)
    bool is_manual = parameters.contains("__is_manual") && parameters["__is_manual"];
    rc = sqlite3_bind_int(insert_stmt_, 7, is_manual ? 1 : 0);
    if (rc != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: Failed to bind is_manual: %s\n", sqlite3_errmsg(db_));
        return false;
    }
    
    // Execute
    rc = sqlite3_step(insert_stmt_);
    
    if (rc != SQLITE_DONE) {
        LOG_INFO("ToolCallTracker: Failed to insert tool call: %s (code=%d)\n", 
            sqlite3_errmsg(db_), rc);
        return false;
    }
    
    LOG_INFO("ToolCallTracker: Successfully recorded tool call (rowid=%lld)\n", 
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

std::vector<ToolCall> ToolCallTracker::get_manual_tool_calls(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ToolCall> calls;
    
    const char* sql = agent_id.empty() ?
        "SELECT * FROM tool_calls WHERE is_manual = 1 ORDER BY timestamp DESC" :
        "SELECT * FROM tool_calls WHERE agent_id = ? AND is_manual = 1 ORDER BY timestamp DESC";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: Failed to prepare manual calls query: %s\n", sqlite3_errmsg(db_));
        return calls;
    }
    
    if (!agent_id.empty()) {
        sqlite3_bind_text(stmt, 1, agent_id.c_str(), -1, SQLITE_STATIC);
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        calls.push_back(row_to_tool_call(stmt));
    }
    
    sqlite3_finalize(stmt);
    
    LOG_INFO("ToolCallTracker: Found %zu manual tool calls%s\n", 
             calls.size(), agent_id.empty() ? "" : (" for agent " + agent_id).c_str());
    
    return calls;
}

bool ToolCallTracker::execute_sql(const char* sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err_msg);
    
    if (rc != SQLITE_OK) {
        LOG_INFO("ToolCallTracker: SQL error: %s\n", err_msg);
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
    LOG_INFO("ToolCallTracker: Started monitoring thread\n");
}

void ToolCallTracker::stop_monitoring() {
    if (!monitoring_) {
        return;  // Not monitoring
    }
    
    monitoring_ = false;
    if (monitor_thread_.joinable()) {
        monitor_thread_.join();
    }
    LOG_INFO("ToolCallTracker: Stopped monitoring thread\n");
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
                LOG_INFO("ToolCallTracker: Failed to prepare monitor query: %s\n", sqlite3_errmsg(db_));
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
                
                LOG_INFO("ToolCallTracker: Emitted TOOL_CALL event for %s - %s at 0x%llx\n",
                    call.agent_id.c_str(), call.tool_name.c_str(), call.address);
            }
            
            sqlite3_finalize(stmt);
        }
        
        // Sleep before next poll
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

} // namespace llm_re::orchestrator