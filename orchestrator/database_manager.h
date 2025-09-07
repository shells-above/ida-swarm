#pragma once

#include "../core/common.h"
#include <filesystem>
#include <map>
#include <mutex>

namespace llm_re::orchestrator {

// Manages IDA database operations for the swarm
class DatabaseManager {
public:
    DatabaseManager(const std::string& main_db_path, const std::string& binary_name);
    ~DatabaseManager();
    
    // Initialize the manager
    bool initialize();
    
    // Save and pack the current database
    bool save_current_database();
    
    // Create a copy of the database for an agent
    std::string create_agent_database(const std::string& agent_id);
    
    // Get path to agent's database
    std::string get_agent_database(const std::string& agent_id) const;
    
    // Clean up agent database
    bool cleanup_agent_database(const std::string& agent_id);
    
    // Clean up all agent databases
    void cleanup_all_agent_databases();
    
    // Get workspace directory
    std::string get_workspace_directory() const { return workspace_dir_; }
    
    // Check if an agent is dormant (completed but preserved)
    bool is_agent_dormant(const std::string& agent_id) const;
    
    // Get list of all dormant agents
    std::vector<std::string> get_dormant_agents() const;
    
    // Restore a dormant agent's database
    std::string restore_dormant_agent(const std::string& agent_id);

private:
    std::string main_database_path_;
    std::string workspace_dir_;
    std::map<std::string, std::string> agent_databases_;  // agent_id -> db_path
    mutable std::mutex mutex_;
    
    // Create workspace directory structure
    bool create_workspace();
    
    // Copy database files
    bool copy_database_files(const std::string& source, const std::string& dest);
    
    // Get all database-related files (idb, i64, til, etc.)
    std::vector<std::filesystem::path> get_database_files(const std::string& base_path) const;
};

} // namespace llm_re::orchestrator