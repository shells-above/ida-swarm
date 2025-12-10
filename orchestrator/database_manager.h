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
    
    // Get workspace directory
    std::string get_workspace_directory() const { return workspace_dir_; }

    // Get agent's binary path
    std::string get_agent_binary(const std::string& agent_id) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = agent_binaries_.find(agent_id);
        if (it != agent_binaries_.end()) {
            return it->second;
        }
        return "";
    }

private:
    std::string main_database_path_;
    std::string workspace_dir_;
    std::string binary_file_path_;  // The original binary being analyzed
    std::map<std::string, std::string> agent_databases_;  // agent_id -> db_path
    std::map<std::string, std::string> agent_binaries_;   // agent_id -> binary_path
    mutable std::mutex mutex_;

    // Validate that binary file exists and is accessible
    // Returns empty optional on success, error message on failure
    std::optional<std::string> validate_binary_path() const;

    // Create workspace directory structure
    bool create_workspace();

    // Copy database files
    bool copy_database_files(const std::string& source, const std::string& dest);

    // Get all database-related files (idb, i64, til, etc.)
    std::vector<std::filesystem::path> get_database_files(const std::string& base_path) const;
};

} // namespace llm_re::orchestrator