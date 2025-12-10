/*
 * IMPORTANT: IDA Binary File Path Metadata
 *
 * IDA stores the original binary file location in the database metadata when the database
 * is first created. This path comes from get_input_file_path() and represents where the
 * binary was located at analysis time.
 *
 * If you move or delete the binary after creating the database, IDA will still reference
 * the original location. This causes failures for operations that need the binary file:
 * - Binary patching (both IDA DB + file)
 * - Code injection / segment injection
 * - LLDB debugging with modified binaries
 *
 * If the binary has moved, you must either:
 * 1. Move it back to the original location
 * 2. Update the path in IDA (File -> Load file -> Reload the binary)
 * 3. Recreate the database from the new binary location
 */

#include "database_manager.h"
#include "../core/logger.h"
#include <filesystem>
#include <chrono>
#include <thread>
#include <fstream>
#include <fcntl.h>      // For open()
#include <sys/file.h>   // For flock()
#include <cerrno>       // For errno
#include <cstring>      // For strerror()
#include <unistd.h>     // For close()
#include <nalt.hpp>     // For get_input_file_path

namespace fs = std::filesystem;

namespace llm_re::orchestrator {

DatabaseManager::DatabaseManager(const std::string& main_db_path, const std::string& binary_name) : main_database_path_(main_db_path) {
    workspace_dir_ = "/tmp/ida_swarm_workspace/" + binary_name;

    // Get binary path from IDA metadata (see file header comment)
    char input_path[MAXSTR];
    if (get_input_file_path(input_path, sizeof(input_path)) > 0) {
        binary_file_path_ = input_path;
    }
    // Validation happens in initialize()
}

DatabaseManager::~DatabaseManager() {
    // No cleanup on destruction
}

bool DatabaseManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Validate binary file exists - required for patching and code injection
    auto validation_error = validate_binary_path();
    if (validation_error.has_value()) {
        LOG_INFO("DatabaseManager: ERROR - Binary validation failed\n");
        LOG_INFO("DatabaseManager: %s\n", validation_error.value().c_str());
        return false;
    }
    LOG_INFO("DatabaseManager: Binary file validated: %s\n", binary_file_path_.c_str());

    // Create workspace directory
    if (!create_workspace()) {
        LOG_INFO("DatabaseManager: Failed to create workspace\n");
        return false;
    }

    // Check if main database exists, save it if not
    if (!fs::exists(main_database_path_)) {
        LOG_INFO("DatabaseManager: Main database does not exist: %s\n", main_database_path_.c_str());
        LOG_INFO("DatabaseManager: Attempting to save database for first time...\n");

        // Need to unlock mutex before calling save_current_database to avoid deadlock
        mutex_.unlock();
        bool save_result = save_current_database();
        mutex_.lock();

        if (!save_result) {
            LOG_INFO("DatabaseManager: Failed to save database\n");
            return false;
        }

        // Verify the database file was created
        if (!fs::exists(main_database_path_)) {
            LOG_INFO("DatabaseManager: Database file still doesn't exist after save: %s\n",
                     main_database_path_.c_str());
            return false;
        }

        LOG_INFO("DatabaseManager: Successfully saved database: %s\n", main_database_path_.c_str());
    }

    LOG_INFO("DatabaseManager: Initialized with workspace: %s\n", workspace_dir_.c_str());
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
        LOG_INFO("DatabaseManager: Filesystem error: %s\n", e.what());
        return false;
    }
}

bool DatabaseManager::save_current_database() {
    LOG_INFO("DatabaseManager: Acquiring lock for save_database()...\n");

    // there's a weird bug in IDA (not totally sure where)
    // but if you are using the MCP server + spawn multiple sessions at the same time for some reason all the orchestrators
    // besides the most recently spawned one will crash right after calling save_database, even though they are all operating on different files

    // Create/open lock file for inter-process synchronization
    std::string lock_file = "/tmp/ida_swarm_save_db.lock";
    int fd = open(lock_file.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd == -1) {
        LOG_INFO("DatabaseManager: WARNING - Failed to open lock file: %s\n", strerror(errno));
        LOG_INFO("DatabaseManager: Continuing without lock (unsafe but better than failing)\n");
        // Continue anyway - better to try than fail completely
    } else {
        // Acquire exclusive lock (blocks until available)
        LOG_INFO("DatabaseManager: Waiting for lock...\n");
        if (flock(fd, LOCK_EX) != 0) {
            LOG_INFO("DatabaseManager: WARNING - Failed to acquire lock: %s\n", strerror(errno));
        } else {
            LOG_INFO("DatabaseManager: Lock acquired successfully\n");
        }
    }

    LOG_INFO("DatabaseManager: About to call save_database()\n");

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

    LOG_INFO("DatabaseManager: save_database() returned %d\n", req.result ? 1 : 0);

    // Release lock
    if (fd != -1) {
        flock(fd, LOCK_UN);
        close(fd);
        LOG_INFO("DatabaseManager: Lock released\n");
    }

    LOG_INFO("DatabaseManager: Saved main database\n");

    return req.result;
}

std::string DatabaseManager::create_agent_database(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    LOG_INFO("DatabaseManager: create_agent_database called for %s\n", agent_id.c_str());

    LOG_INFO("DatabaseManager: Calling save_current_database()\n");
    if (!save_current_database()) {
        LOG_INFO("DatabaseManager: Failed to save current database\n");
        return "";
    }
    LOG_INFO("DatabaseManager: save_current_database() completed successfully\n");

    // Create agent directory
    fs::path agent_dir = fs::path(workspace_dir_) / "agents" / agent_id;
    LOG_INFO("DatabaseManager: Creating directory for agent at %s\n", agent_dir.string().c_str());

    try {
        fs::create_directories(agent_dir);

        // Get all database files
        auto db_files = get_database_files(main_database_path_);

        // Copy each file
        for (const auto& file : db_files) {
            fs::path dest = agent_dir / file.filename();
            fs::copy_file(file, dest, fs::copy_options::overwrite_existing);
            LOG_INFO("DatabaseManager: Copied %s to %s\n",
                file.filename().string().c_str(),
                dest.string().c_str());
        }

        // Copy the binary file (validation already confirmed it exists)
        fs::path binary_source(binary_file_path_);
        fs::path binary_dest = agent_dir / (agent_id + "_" + binary_source.filename().string());

        try {
            fs::copy_file(binary_source, binary_dest, fs::copy_options::overwrite_existing);
            LOG_INFO("DatabaseManager: Copied binary %s to %s\n",
                binary_source.filename().string().c_str(),
                binary_dest.string().c_str());

            // Ad-hoc sign the binary for iOS compatibility
            // iOS requires binaries to be signed, even if just with an ad-hoc signature
            std::string codesign_cmd = "codesign -s - -f \"" + binary_dest.string() + "\" 2>&1";
            int status = system(codesign_cmd.c_str());
            if (status == 0) {
                LOG_INFO("DatabaseManager: Ad-hoc signed binary for iOS\n");
            } else {
                LOG_INFO("DatabaseManager: WARNING - codesign failed with status %d\n", status);
            }

            // Store the binary path for this agent
            agent_binaries_[agent_id] = binary_dest.string();
        } catch (const fs::filesystem_error& e) {
            LOG_INFO("DatabaseManager: ERROR - Failed to copy binary: %s\n", e.what());
            return "";  // Fail agent creation if binary copy fails
        }

        // Get the main database file in the agent directory
        fs::path base_name = fs::path(main_database_path_).filename();
        fs::path agent_db = agent_dir / base_name;

        // Track this agent's database
        agent_databases_[agent_id] = agent_db.string();

        LOG_INFO("DatabaseManager: Created agent database for %s at %s\n",
            agent_id.c_str(), agent_db.string().c_str());

        return agent_db.string();

    } catch (const fs::filesystem_error& e) {
        LOG_INFO("DatabaseManager: Failed to create agent database: %s\n", e.what());
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
    fs::path i64_file = dir / (stem + ".i64");
    
    if (fs::exists(i64_file)) {
        files.push_back(i64_file);
        LOG_INFO("DatabaseManager: Will copy packed database %s\n", i64_file.string().c_str());
    } else {
        LOG_INFO("DatabaseManager: Warning - no .i64 file found for %s\n", base_path.c_str());
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
        LOG_INFO("DatabaseManager: Copy failed: %s\n", e.what());
        return false;
    }
}

std::optional<std::string> DatabaseManager::validate_binary_path() const {
    // Check if we retrieved a path from IDA
    if (binary_file_path_.empty()) {
        return "Could not retrieve binary file path from IDA database metadata. "
               "The database may be corrupted or was created without a binary reference.";
    }

    // Check if the file exists at the stored location
    if (!fs::exists(binary_file_path_)) {
        return std::string("Binary file not found at path stored in IDA metadata: ") + binary_file_path_ + "\n" +
               "This path is from when the database was created. If the binary has been moved, "
               "please move it back, update the path in IDA, or recreate the database.";
    }

    // Verify it's actually a file, not a directory
    if (!fs::is_regular_file(binary_file_path_)) {
        return std::string("Path exists but is not a regular file: ") + binary_file_path_;
    }

    return std::nullopt;  // Success
}


} // namespace llm_re::orchestrator