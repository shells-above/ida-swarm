#include "database_manager.h"
#include "orchestrator_logger.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <fstream>

namespace fs = std::filesystem;

namespace llm_re::orchestrator {

DatabaseManager::DatabaseManager(const std::string& main_db_path, const std::string& binary_name)
    : main_database_path_(main_db_path) {
    
    // Set workspace directory with binary name
    // The base path should be passed from orchestrator config
    workspace_dir_ = "/tmp/ida_swarm_workspace/" + binary_name;
}

DatabaseManager::~DatabaseManager() {
    // No cleanup on destruction
}

bool DatabaseManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Create workspace directory
    if (!create_workspace()) {
        ORCH_LOG("DatabaseManager: Failed to create workspace\n");
        return false;
    }

    // Check if main database exists, save it if not
    if (!fs::exists(main_database_path_)) {
        ORCH_LOG("DatabaseManager: Main database does not exist: %s\n", main_database_path_.c_str());
        ORCH_LOG("DatabaseManager: Attempting to save database for first time...\n");

        // Need to unlock mutex before calling save_current_database to avoid deadlock
        mutex_.unlock();
        bool save_result = save_current_database();
        mutex_.lock();

        if (!save_result) {
            ORCH_LOG("DatabaseManager: Failed to save database\n");
            return false;
        }

        // Verify the database file was created
        if (!fs::exists(main_database_path_)) {
            ORCH_LOG("DatabaseManager: Database file still doesn't exist after save: %s\n",
                     main_database_path_.c_str());
            return false;
        }

        ORCH_LOG("DatabaseManager: Successfully saved database: %s\n", main_database_path_.c_str());
    }

    ORCH_LOG("DatabaseManager: Initialized with workspace: %s\n", workspace_dir_.c_str());
    return true;
}

bool DatabaseManager::create_workspace() {
    try {
        // Create main workspace directory
        if (!fs::exists(workspace_dir_)) {
            fs::create_directories(workspace_dir_);
        }
        
        // Create subdirectories
        fs::create_directories(fs::path(workspace_dir_) / "agents");
        fs::create_directories(fs::path(workspace_dir_) / "configs");
        
        return true;
    } catch (const fs::filesystem_error& e) {
        ORCH_LOG("DatabaseManager: Filesystem error: %s\n", e.what());
        return false;
    }
}

bool DatabaseManager::save_current_database() {
    ORCH_LOG("DatabaseManager: About to call save_database()\n");
    
    // Create request to execute save_database on main thread
    struct SaveDatabaseRequest : exec_request_t {
        bool result = false;
        virtual ssize_t idaapi execute() override {
            result = save_database();
            return 0;
        }
    };
    
    SaveDatabaseRequest req;
    execute_sync(req, MFF_WRITE);  // MFF_WRITE for database modification
    
    ORCH_LOG("DatabaseManager: save_database() returned %d\n", req.result ? 1 : 0);
    ORCH_LOG("DatabaseManager: Saved main database\n");
    
    return req.result;
}

std::string DatabaseManager::create_agent_database(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ORCH_LOG("DatabaseManager: create_agent_database called for %s\n", agent_id.c_str());

    ORCH_LOG("DatabaseManager: Calling save_current_database()\n");
    if (!save_current_database()) {
        ORCH_LOG("DatabaseManager: Failed to save current database\n");
        return "";
    }
    ORCH_LOG("DatabaseManager: save_current_database() completed successfully\n");

    // Create agent directory
    fs::path agent_dir = fs::path(workspace_dir_) / "agents" / agent_id;
    ORCH_LOG("DatabaseManager: Creating directory for agent at %s\n", agent_dir.string().c_str());
    
    try {
        fs::create_directories(agent_dir);
        
        // Get all database files
        auto db_files = get_database_files(main_database_path_);
        
        // Copy each file
        for (const auto& file : db_files) {
            fs::path dest = agent_dir / file.filename();
            fs::copy_file(file, dest, fs::copy_options::overwrite_existing);
            ORCH_LOG("DatabaseManager: Copied %s to %s\n", 
                file.filename().string().c_str(), 
                dest.string().c_str());
        }
        
        // Get the main database file in the agent directory
        fs::path base_name = fs::path(main_database_path_).filename();
        fs::path agent_db = agent_dir / base_name;
        
        // Track this agent's database
        agent_databases_[agent_id] = agent_db.string();
        
        ORCH_LOG("DatabaseManager: Created agent database for %s at %s\n", 
            agent_id.c_str(), agent_db.string().c_str());
        
        return agent_db.string();
        
    } catch (const fs::filesystem_error& e) {
        ORCH_LOG("DatabaseManager: Failed to create agent database: %s\n", e.what());
        return "";
    }
}

std::string DatabaseManager::get_agent_database(const std::string& agent_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = agent_databases_.find(agent_id);
    if (it != agent_databases_.end()) {
        return it->second;
    }
    
    return "";
}


std::vector<fs::path> DatabaseManager::get_database_files(const std::string& base_path) const {
    std::vector<fs::path> files;
    
    fs::path base(base_path);
    fs::path dir = base.parent_path();
    std::string stem = base.stem().string();
    
    // IDA 9.0+ only uses .i64 format - only copy the packed database
    // IDA will automatically unpack it when opening
    fs::path i64_file = dir / (stem + ".i64");
    
    if (fs::exists(i64_file)) {
        files.push_back(i64_file);
        ORCH_LOG("DatabaseManager: Will copy packed database %s\n", i64_file.string().c_str());
    } else {
        ORCH_LOG("DatabaseManager: Warning - no .i64 file found for %s\n", base_path.c_str());
    }
    
    return files;
}

bool DatabaseManager::copy_database_files(const std::string& source, const std::string& dest) {
    try {
        auto files = get_database_files(source);
        
        fs::path dest_dir = fs::path(dest).parent_path();
        if (!fs::exists(dest_dir)) {
            fs::create_directories(dest_dir);
        }
        
        for (const auto& file : files) {
            fs::path dest_file = dest_dir / file.filename();
            fs::copy_file(file, dest_file, fs::copy_options::overwrite_existing);
        }
        
        return true;
        
    } catch (const fs::filesystem_error& e) {
        ORCH_LOG("DatabaseManager: Copy failed: %s\n", e.what());
        return false;
    }
}


} // namespace llm_re::orchestrator