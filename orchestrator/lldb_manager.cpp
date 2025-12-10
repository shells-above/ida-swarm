#include "lldb_manager.h"
#include "database_manager.h"
#include "remote_sync_manager.h"
#include "remote_device_info.h"
#include "../core/logger.h"
#include "../core/config.h"
#include <fstream>
#include <sstream>
#include <regex>
#include <chrono>
#include <random>
#include <format>

// Platform-specific includes
#ifdef _WIN32
    #include <windows.h>
    #include <process.h>
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/wait.h>
    #include <sys/select.h>
    #include <sys/ioctl.h>
    #include <signal.h>
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #ifdef __APPLE__
        #include <util.h>
    #else
        #include <pty.h>
    #endif
#endif

#include <cstdio>  // For popen/pclose

namespace llm_re {

bool LLDBSessionManager::is_valid_lldb_executable(const std::string& path) {
    if (path.empty()) {
        return false;
    }

    // Check if file exists and is executable
#ifdef _WIN32
    // Windows: check if file exists (executability check is complex)
    DWORD attr = GetFileAttributesA(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
#else
    // Unix: use access() to check if executable
    return access(path.c_str(), X_OK) == 0;
#endif
}

std::string LLDBSessionManager::auto_detect_lldb_path() {
    LOG("LLDB: Auto-detecting LLDB executable path...\n");

    // List of common LLDB locations to try
    std::vector<std::string> candidates = {
        "/usr/bin/lldb",
        "/usr/local/bin/lldb",
#ifdef __APPLE__
        "/Applications/Xcode.app/Contents/Developer/usr/bin/lldb",
        "/Library/Developer/CommandLineTools/usr/bin/lldb",
#endif
        "/opt/homebrew/bin/lldb",
        "/opt/local/bin/lldb",
    };

    // Try each candidate
    for (const auto& candidate : candidates) {
        if (is_valid_lldb_executable(candidate)) {
            LOG("LLDB: Found LLDB at %s\n", candidate.c_str());
            return candidate;
        }
    }

    // Try using 'which lldb' command
    LOG("LLDB: Trying 'which lldb' command...\n");
#ifdef _WIN32
    FILE* pipe = _popen("where lldb", "r");
#else
    FILE* pipe = popen("which lldb 2>/dev/null", "r");
#endif

    if (pipe) {
        char buffer[256];
        std::string result;

#undef fgets
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            result += buffer;
        }
#define fgets dont_use_fgets

#ifdef _WIN32
        _pclose(pipe);
#else
        pclose(pipe);
#endif

        // Trim whitespace
        result.erase(result.find_last_not_of(" \n\r\t") + 1);

        if (!result.empty() && is_valid_lldb_executable(result)) {
            LOG("LLDB: Found LLDB via 'which' at %s\n", result.c_str());
            return result;
        }
    }

    LOG("LLDB: Failed to auto-detect LLDB path\n");
    return "";
}

LLDBSessionManager::LLDBSessionManager(const std::string& lldb_path,
                                       const std::string& workspace_path,
                                       DatabaseManager* db_manager,
                                       int irc_port)
    : workspace_path_(workspace_path), db_manager_(db_manager), irc_port_(irc_port) {

    // Validate or auto-detect LLDB path
    if (is_valid_lldb_executable(lldb_path)) {
        lldb_path_ = lldb_path;
        LOG("LLDB: Using provided LLDB path: %s\n", lldb_path_.c_str());
    } else {
        if (!lldb_path.empty()) {
            LOG("LLDB: Warning - provided LLDB path is invalid: %s\n", lldb_path.c_str());
        }

        lldb_path_ = auto_detect_lldb_path();

        if (lldb_path_.empty()) {
            LOG("LLDB: ERROR - Could not find LLDB executable!\n");
            LOG("LLDB: Please install LLDB or specify the correct path in preferences.\n");
            throw std::runtime_error("LLDB executable not found. Please install LLDB or configure the path in preferences.");
        }
    }

    LOG("LLDB: Session manager initialized (lldb_path=%s, workspace=%s)\n",
        lldb_path_.c_str(), workspace_path.c_str());

    // Load device pool from configuration
    try {
        json config = load_lldb_config();
        // Config loaded and devices_ populated
        LOG("LLDB: Loaded %zu devices from configuration\n", devices_.size());
    } catch (const std::exception& e) {
        LOG("LLDB: Warning - failed to load device configuration: %s\n", e.what());
        // Continue with empty device pool - can be configured later
    }
}

LLDBSessionManager::~LLDBSessionManager() {
    // Cleanup all active sessions
    // Copy sessions first to avoid holding lock during cleanup
    std::vector<LLDBSession> sessions_to_cleanup;
    {
        std::lock_guard<std::mutex> lock(active_sessions_mutex_);
        for (auto& [session_id, session] : active_sessions_) {
            sessions_to_cleanup.push_back(session);
        }
    }

    // Cleanup outside the lock
    for (const auto& session : sessions_to_cleanup) {
        if (session.lldb_pid > 0) {
            LOG("LLDB: Terminating session %s (pid=%d) during shutdown\n",
                session.session_id.c_str(), session.lldb_pid);
            terminate_lldb_process(session.lldb_pid, session.pty_master_fd);
        }

        // CRITICAL: Also cleanup remote debugserver processes
        if (session.is_remote && (session.remote_debugserver_pid > 0 || session.remote_debugged_pid > 0)) {
            RemoteDevice* device = nullptr;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                device = find_device_by_id(session.device_id);
            }

            if (device) {
                LOG("LLDB: Cleaning up remote processes during shutdown (debugserver=%d, debugged=%d)\n",
                    session.remote_debugserver_pid, session.remote_debugged_pid);
                stop_remote_processes(
                    device->host,
                    device->ssh_port,
                    device->ssh_user,
                    session.remote_debugserver_pid,
                    session.remote_debugged_pid
                );
            }
        }
    }
}

json LLDBSessionManager::handle_start_session(const std::string& agent_id, const std::string& request_id, int timeout_ms) {
    try {
        LOG("LLDB: Agent %s requesting debug session (request_id=%s, timeout=%dms)\n",
            agent_id.c_str(), request_id.c_str(), timeout_ms);

        RemoteDevice* allocated_device = nullptr;

        // Try to find an available device
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            allocated_device = find_available_device();

            if (!allocated_device) {
                // No devices available - add to queue and wait
                LOG("LLDB: All devices busy, adding agent %s to queue\n", agent_id.c_str());

                QueueEntry entry;
                entry.agent_id = agent_id;
                entry.request_id = request_id;
                auto cv = entry.cv;

                global_queue_.push(entry);
                size_t queue_position = global_queue_.size();
                LOG("LLDB: Agent %s added to queue at position %zu\n", agent_id.c_str(), queue_position);

                // Wait for device to become available AND for us to be at front of queue
                // The second check prevents spurious wakeups from causing queue violations:
                // Without it, if agent B at position 2 has a spurious wakeup while a device
                // is free, it could incorrectly proceed and pop agent A's entry from the queue.
                bool device_available = cv->wait_for(lock, std::chrono::milliseconds(timeout_ms), [this, &agent_id]() {
                    return !global_queue_.empty() &&
                           global_queue_.front().agent_id == agent_id &&
                           find_available_device() != nullptr;
                });

                if (!device_available) {
                    // Timeout - remove from queue
                    LOG("LLDB: Agent %s timeout waiting for available device\n", agent_id.c_str());

                    std::queue<QueueEntry> new_queue;
                    while (!global_queue_.empty()) {
                        if (global_queue_.front().agent_id != agent_id) {
                            new_queue.push(global_queue_.front());
                        }
                        global_queue_.pop();
                    }
                    global_queue_ = std::move(new_queue);

                    return {
                        {"status", "error"},
                        {"error", std::format("Timeout waiting for available device ({} ms)", timeout_ms)},
                        {"request_id", request_id}
                    };
                }

                // Device became available - allocate it BEFORE popping from queue
                allocated_device = find_available_device();

                if (!allocated_device) {
                    // Race condition - device was taken by another thread
                    // This should not happen since we're holding the lock, but check anyway
                    LOG("LLDB: ERROR - Race condition detected in device allocation\n");
                    return {
                        {"status", "error"},
                        {"error", "Device allocation race condition"},
                        {"request_id", request_id}
                    };
                }

                // Atomically allocate device and remove from queue
                allocated_device->is_available = false;
                allocated_device->current_agent_id = agent_id;
                allocated_device->session_start_time = std::chrono::system_clock::now();
                global_queue_.pop();  // Remove this agent from queue only after successful allocation
            } else {
                // Device available immediately - allocate it
                allocated_device->is_available = false;
                allocated_device->current_agent_id = agent_id;
                allocated_device->session_start_time = std::chrono::system_clock::now();
            }

            LOG("LLDB: Allocated device %s (%s) to agent %s\n",
                allocated_device->name.c_str(), allocated_device->id.c_str(), agent_id.c_str());
        }
        // Mutex unlocked - can now do time-consuming operations

        // Discover device info if not already fetched
        if (!allocated_device->device_info) {
            LOG("LLDB: Device info not cached, discovering...\n");
            discover_and_update_device(*allocated_device);
        }

        // Initialize device platform detection on first use
        if (!allocated_device->initialized) {
            LOG("LLDB: Device not initialized, detecting platform...\n");
            std::string init_error;
            if (!initialize_remote_device(*allocated_device, init_error)) {
                LOG("LLDB: Failed to initialize device: %s\n", init_error.c_str());

                // Release device on error
                std::unique_lock<std::mutex> lock(queue_mutex_);
                allocated_device->is_available = true;
                allocated_device->current_agent_id = "";
                if (!global_queue_.empty() && !global_queue_.front().notified) {
                    global_queue_.front().notified = true;
                    global_queue_.front().cv->notify_one();
                }

                return {
                    {"status", "error"},
                    {"error", "Failed to initialize device: " + init_error},
                    {"request_id", request_id}
                };
            }
        }

        // Get agent's patched binary
        std::string agent_binary_path = db_manager_->get_agent_binary(agent_id);
        if (agent_binary_path.empty()) {
            LOG("LLDB: Agent binary not found for %s\n", agent_id.c_str());

            // Release device on error
            std::unique_lock<std::mutex> lock(queue_mutex_);
            allocated_device->is_available = true;
            allocated_device->current_agent_id = "";
            if (!global_queue_.empty() && !global_queue_.front().notified) {
                global_queue_.front().notified = true;
                global_queue_.front().cv->notify_one();
            }

            return {
                {"status", "error"},
                {"error", "Agent binary not found in workspace"},
                {"request_id", request_id}
            };
        }

        LOG("LLDB: Found agent binary at %s\n", agent_binary_path.c_str());

        // Build remote config for syncing from allocated device
        orchestrator::RemoteConfig remote_cfg;
        remote_cfg.host = allocated_device->host;
        remote_cfg.ssh_port = allocated_device->ssh_port;
        remote_cfg.ssh_user = allocated_device->ssh_user;
        remote_cfg.debugserver_port = allocated_device->debugserver_port;

        std::string remote_path = allocated_device->remote_binary_path;

        // Sync agent's patched binary to remote device
        std::string sync_error;
        bool synced = orchestrator::RemoteSyncManager::sync_binary(
            agent_binary_path,
            remote_path,
            remote_cfg,
            sync_error
        );

        if (!synced) {
            LOG("LLDB: Failed to sync binary: %s\n", sync_error.c_str());

            // Release device on error
            std::unique_lock<std::mutex> lock(queue_mutex_);
            allocated_device->is_available = true;
            allocated_device->current_agent_id = "";
            if (!global_queue_.empty() && !global_queue_.front().notified) {
                global_queue_.front().notified = true;
                global_queue_.front().cv->notify_one();
            }

            return {
                {"status", "error"},
                {"error", "Failed to sync binary to remote: " + sync_error},
                {"request_id", request_id}
            };
        }

        LOG("LLDB: Successfully synced agent binary to remote\n");

        // Auto-sign binary if device is iOS (requires code signing)
        if (!allocated_device->signing_tool.empty()) {
            LOG("LLDB: Auto-signing binary with %s...\n", allocated_device->signing_tool.c_str());

            orchestrator::SSH2SessionGuard ssh;
            std::string sign_error;
            if (!ssh.connect(allocated_device->host, allocated_device->ssh_port, allocated_device->ssh_user, sign_error)) {
                LOG("LLDB: ERROR - Failed to connect for signing: %s\n", sign_error.c_str());
                sign_error = "Failed to connect for code signing: " + sign_error;
            } else {
                // Sign the binary (ad-hoc signature)
                std::string sign_cmd = std::format("{} -S \"{}\"", allocated_device->signing_tool, remote_path);
                std::string sign_output = ssh.exec(sign_cmd, sign_error);

                if (!sign_error.empty()) {
                    LOG("LLDB: ERROR - Auto-signing failed: %s\n", sign_error.c_str());
                    sign_error = "Code signing failed (required for iOS): " + sign_error;
                } else {
                    LOG("LLDB: Binary successfully signed with %s\n", allocated_device->signing_tool.c_str());
                    sign_error.clear();  // Success
                }
            }

            // CRITICAL: If signing failed on iOS, fail hard - debugserver will not work without signature
            if (!sign_error.empty()) {
                LOG("LLDB: FATAL - Cannot proceed without code signature on iOS device\n");

                // Release device on error
                std::unique_lock<std::mutex> lock(queue_mutex_);
                allocated_device->is_available = true;
                allocated_device->current_agent_id = "";
                if (!global_queue_.empty() && !global_queue_.front().notified) {
                    global_queue_.front().notified = true;
                    global_queue_.front().cv->notify_one();
                }

                return {
                    {"status", "error"},
                    {"error", sign_error},
                    {"request_id", request_id}
                };
            }
        }

        // Start debugserver on remote device
        int debugserver_pid = -1;
        int debugged_pid = -1;
        std::string debugserver_error;

        if (!start_remote_debugserver(
                *allocated_device,
                remote_path,
                debugserver_pid,
                debugged_pid,
                debugserver_error)) {

            LOG("LLDB: ERROR - Failed to start debugserver: %s\n", debugserver_error.c_str());

            // Release device and notify queue
            std::unique_lock<std::mutex> lock(queue_mutex_);
            allocated_device->is_available = true;
            allocated_device->current_agent_id = "";
            if (!global_queue_.empty() && !global_queue_.front().notified) {
                global_queue_.front().notified = true;
                global_queue_.front().cv->notify_one();
            }

            return {
                {"status", "error"},
                {"error", "Failed to start remote debugserver: " + debugserver_error},
                {"request_id", request_id}
            };
        }

        LOG("LLDB: Debugserver ready (PID=%d, debugged=%d)\n", debugserver_pid, debugged_pid);

        // Spawn LLDB with PTY
        int pty_fd = -1;
        int lldb_pid = spawn_lldb_with_pty(*allocated_device, &pty_fd);

        if (lldb_pid < 0 || pty_fd < 0) {
            // CRITICAL: Cleanup debugserver that was already started
            LOG("LLDB: Cleaning up debugserver after LLDB spawn failure\n");
            stop_remote_processes(
                allocated_device->host,
                allocated_device->ssh_port,
                allocated_device->ssh_user,
                debugserver_pid,
                debugged_pid
            );

            // Release device on error
            std::unique_lock<std::mutex> lock(queue_mutex_);
            allocated_device->is_available = true;
            allocated_device->current_agent_id = "";
            if (!global_queue_.empty() && !global_queue_.front().notified) {
                global_queue_.front().notified = true;
                global_queue_.front().cv->notify_one();
            }

            return {
                {"status", "error"},
                {"error", "Failed to spawn LLDB process"},
                {"request_id", request_id}
            };
        }

        LOG("LLDB: Spawned LLDB process (pid=%d, pty_fd=%d)\n", lldb_pid, pty_fd);

        // Read and discard LLDB's initial startup output/prompt
        // This is critical - if we don't clear this, our command gets interleaved
        std::string initial_output = read_from_lldb_until_prompt(pty_fd, 10000);
        LOG("LLDB: Initial LLDB output (%zu bytes): %s\n", initial_output.size(), initial_output.c_str());

        // Set LLDB to synchronous mode - ensures process connect waits for connection to complete
        // In async mode, commands return immediately and output comes later asynchronously
        if (!write_to_lldb(pty_fd, "settings set target.async false")) {
            LOG("LLDB: WARNING - Failed to write async mode setting\n");
        }
        std::string async_output = read_from_lldb_until_prompt(pty_fd, 5000);
        LOG("LLDB: Async mode setting output: %s\n", async_output.c_str());

        // Connect to remote debugserver
        std::string connect_cmd = std::format("process connect connect://{}:{}", allocated_device->host, allocated_device->debugserver_port);

        LOG("LLDB: Connecting to remote debugserver at %s:%d\n", allocated_device->host.c_str(), allocated_device->debugserver_port);

        if (!write_to_lldb(pty_fd, connect_cmd)) {
            terminate_lldb_process(lldb_pid, pty_fd);

            // CRITICAL: Cleanup debugserver that was already started
            LOG("LLDB: Cleaning up debugserver after connect write failure\n");
            stop_remote_processes(
                allocated_device->host,
                allocated_device->ssh_port,
                allocated_device->ssh_user,
                debugserver_pid,
                debugged_pid
            );

            std::unique_lock<std::mutex> lock(queue_mutex_);
            allocated_device->is_available = true;
            allocated_device->current_agent_id = "";
            if (!global_queue_.empty() && !global_queue_.front().notified) {
                global_queue_.front().notified = true;
                global_queue_.front().cv->notify_one();
            }

            return {
                {"status", "error"},
                {"error", "Failed to write connect command to LLDB"},
                {"request_id", request_id}
            };
        }

        // Use longer timeout for process connect - network connections can take time
        // Also wait specifically for "Process" or "error:" since async mode may return
        // to prompt before connection completes
        std::string connect_output = read_lldb_until_connect_complete(pty_fd, 30000);  // 30 second timeout
        LOG("LLDB: Connect output: %s\n", connect_output.c_str());

        // Check for connection errors
        if (connect_output.find("error:") != std::string::npos ||
            connect_output.find("failed") != std::string::npos) {
            terminate_lldb_process(lldb_pid, pty_fd);

            // CRITICAL: Cleanup debugserver that was already started
            LOG("LLDB: Cleaning up debugserver after connection error\n");
            stop_remote_processes(
                allocated_device->host,
                allocated_device->ssh_port,
                allocated_device->ssh_user,
                debugserver_pid,
                debugged_pid
            );

            std::unique_lock<std::mutex> lock(queue_mutex_);
            allocated_device->is_available = true;
            allocated_device->current_agent_id = "";
            if (!global_queue_.empty() && !global_queue_.front().notified) {
                global_queue_.front().notified = true;
                global_queue_.front().cv->notify_one();
            }

            return {
                {"status", "error"},
                {"error", "Failed to connect to remote debugserver: " + connect_output},
                {"request_id", request_id}
            };
        }

        // =========================================================================
        // NO process launch command needed!
        // =========================================================================
        // When debugserver is started with: debugserver host:port "/path/to/binary"
        // The binary is AUTOMATICALLY launched when LLDB connects via `process connect`.
        // The process is stopped at _dyld_start (before main() execution).
        //
        // Calling `process launch` would prompt "there's a running process, kill it [y/n]"
        // which hangs because there's no user input in automated mode.
        //
        // The agent can now:
        // 1. Set breakpoints using converted addresses (convert_ida_address tool)
        // 2. Continue execution with `c` or `continue` command
        // 3. Inspect memory, registers, etc.
        // =========================================================================

        // Verify process is attached and stopped
        if (connect_output.find("stopped") == std::string::npos) {
            LOG("LLDB: WARNING - Process state unclear after connect. Output: %s\n",
                connect_output.c_str());
        } else {
            LOG("LLDB: Process launched and stopped at _dyld_start (debugserver auto-launch)\n");
        }

        // Create session
        std::string session_id = generate_session_id();
        LLDBSession session;
        session.session_id = session_id;
        session.agent_id = agent_id;
        session.device_id = allocated_device->id;
        session.lldb_pid = lldb_pid;
        session.pty_master_fd = pty_fd;
        session.target_binary = allocated_device->remote_binary_path;
        session.remote_host = allocated_device->host;
        session.remote_port = allocated_device->debugserver_port;
        session.is_remote = true;
        session.remote_debugserver_pid = debugserver_pid;
        session.remote_debugged_pid = debugged_pid;

        {
            std::lock_guard<std::mutex> lock(active_sessions_mutex_);
            active_sessions_[session_id] = session;
        }

        LOG("LLDB: Session %s created for agent %s\n", session_id.c_str(), agent_id.c_str());

        return {
            {"status", "success"},
            {"session_id", session_id},
            {"lldb_cheatsheet", "PLACEHOLDER - Use send_lldb_command with raw LLDB commands"},
            {"request_id", request_id}
        };

    } catch (const std::exception& e) {
        LOG("LLDB: Exception in handle_start_session: %s\n", e.what());

        // TODO: Add proper cleanup based on what was allocated
        // This exception handler currently leaks resources if exception occurs after:
        // 1. Device allocation (allocated_device)
        // 2. Debugserver start (debugserver_pid, debugged_pid)
        // 3. LLDB spawn (lldb_pid, pty_fd)
        //
        // Required refactoring:
        // - Move device/pid tracking to outer scope
        // - Add cleanup logic here based on what was allocated
        // - Example:
        //   if (debugserver_pid > 0) stop_remote_processes(...)
        //   if (lldb_pid > 0) terminate_lldb_process(...)
        //   if (allocated_device) release device and notify queue
        //
        // Current state: PARTIAL MITIGATION - main error paths now cleanup properly,
        // but unexpected exceptions still leak resources.

        return {
            {"status", "error"},
            {"error", std::string("Exception: ") + e.what()},
            {"request_id", request_id}
        };
    }
}

json LLDBSessionManager::handle_send_command(const std::string& session_id,
                                              const std::string& agent_id,
                                              const std::string& command,
                                              const std::string& request_id) {
    try {
        LOG("LLDB: Agent %s sending command to session %s: %s\n",
            agent_id.c_str(), session_id.c_str(), command.c_str());

        // Block "platform shell" commands - they run on the LOCAL machine, not the
        // remote iOS device! This causes confusion as agents think they're running
        // commands on the device but actually run them on the host Mac.
        if (command.find("platform shell") != std::string::npos ||
            command.find("platform sh") != std::string::npos) {
            return {
                {"status", "error"},
                {"error", "BLOCKED: 'platform shell' runs on the LOCAL machine, not the remote iOS device. "
                          "Use LLDB debugging commands (memory read, register read, x, etc.) to inspect the remote process."},
                {"request_id", request_id}
            };
        }

        std::string error_msg;
        if (!validate_session_ownership(session_id, agent_id, error_msg)) {
            return {
                {"status", "error"},
                {"error", error_msg},
                {"request_id", request_id}
            };
        }

        int pty_fd;
        {
            std::lock_guard<std::mutex> lock(active_sessions_mutex_);
            LLDBSession& session = active_sessions_.at(session_id);
            pty_fd = session.pty_master_fd;
        }

        if (!write_to_lldb(pty_fd, command)) {
            return {
                {"status", "error"},
                {"error", "Failed to write command to LLDB"},
                {"request_id", request_id}
            };
        }

        std::string output = read_from_lldb_until_prompt(pty_fd);

        LOG("LLDB: Command output (%zu bytes)\n", output.size());

        return {
            {"status", "success"},
            {"output", output},
            {"request_id", request_id}
        };

    } catch (const std::exception& e) {
        LOG("LLDB: Exception in handle_send_command: %s\n", e.what());
        return {
            {"status", "error"},
            {"error", std::string("Exception: ") + e.what()},
            {"request_id", request_id}
        };
    }
}

json LLDBSessionManager::handle_convert_address(const std::string& session_id,
                                                 const std::string& agent_id,
                                                 uint64_t ida_address,
                                                 const std::string& request_id) {
    try {
        LOG("LLDB: Agent %s converting address 0x%llx in session %s\n",
            agent_id.c_str(), (unsigned long long)ida_address, session_id.c_str());

        std::string error_msg;
        if (!validate_session_ownership(session_id, agent_id, error_msg)) {
            return {
                {"status", "error"},
                {"error", error_msg},
                {"request_id", request_id}
            };
        }

        int pty_fd;
        {
            std::lock_guard<std::mutex> lock(active_sessions_mutex_);
            LLDBSession& session = active_sessions_.at(session_id);
            pty_fd = session.pty_master_fd;
        }

        // Query LLDB for image list to get runtime base
        if (!write_to_lldb(pty_fd, "image list")) {
            return {
                {"status", "error"},
                {"error", "Failed to query LLDB for image list"},
                {"request_id", request_id}
            };
        }

        std::string output = read_from_lldb_until_prompt(pty_fd);
        auto runtime_base_opt = parse_image_base_from_lldb_output(output);

        if (!runtime_base_opt.has_value()) {
            return {
                {"status", "error"},
                {"error", "Failed to parse runtime base address from LLDB output"},
                {"request_id", request_id}
            };
        }

        uint64_t runtime_base = runtime_base_opt.value();

        // Get IDA base address
        uint64_t ida_base = get_ida_imagebase();

        // Calculate runtime address
        uint64_t offset = ida_address - ida_base;
        uint64_t runtime_address = runtime_base + offset;

        LOG("LLDB: Address conversion: IDA 0x%llx -> Runtime 0x%llx (base: IDA=0x%llx, runtime=0x%llx, offset=0x%llx)\n",
            (unsigned long long)ida_address,
            (unsigned long long)runtime_address,
            (unsigned long long)ida_base,
            (unsigned long long)runtime_base,
            (unsigned long long)offset);

        return {
            {"status", "success"},
            {"ida_address", ida_address},
            {"runtime_address", runtime_address},
            {"ida_base", ida_base},
            {"runtime_base", runtime_base},
            {"offset", offset},
            {"request_id", request_id}
        };

    } catch (const std::exception& e) {
        LOG("LLDB: Exception in handle_convert_address: %s\n", e.what());
        return {
            {"status", "error"},
            {"error", std::string("Exception: ") + e.what()},
            {"request_id", request_id}
        };
    }
}

json LLDBSessionManager::handle_stop_session(const std::string& session_id,
                                              const std::string& agent_id,
                                              const std::string& request_id) {
    try {
        LOG("LLDB: Agent %s stopping session %s\n", agent_id.c_str(), session_id.c_str());

        std::string error_msg;
        if (!validate_session_ownership(session_id, agent_id, error_msg)) {
            return {
                {"status", "error"},
                {"error", error_msg},
                {"request_id", request_id}
            };
        }

        // Copy session data and remove from map before cleanup
        int pty_fd;
        int lldb_pid;
        std::string device_id;
        int debugserver_pid;
        int debugged_pid;
        {
            std::lock_guard<std::mutex> lock(active_sessions_mutex_);
            LLDBSession& session = active_sessions_.at(session_id);
            pty_fd = session.pty_master_fd;
            lldb_pid = session.lldb_pid;
            device_id = session.device_id;
            debugserver_pid = session.remote_debugserver_pid;
            debugged_pid = session.remote_debugged_pid;
            // Remove session from map immediately
            active_sessions_.erase(session_id);
        }

        // Terminate LLDB process properly
        terminate_lldb_process(lldb_pid, pty_fd);

        // Stop remote debugserver and debugged process
        RemoteDevice* cleanup_device = nullptr;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cleanup_device = find_device_by_id(device_id);
        }

        if (cleanup_device) {
            LOG("LLDB: Cleaning up remote processes (debugserver=%d, debugged=%d)\n",
                debugserver_pid, debugged_pid);

            stop_remote_processes(
                cleanup_device->host,
                cleanup_device->ssh_port,
                cleanup_device->ssh_user,
                debugserver_pid,
                debugged_pid
            );
        } else {
            LOG("LLDB: WARNING - Device not found for remote cleanup: %s\n", device_id.c_str());
        }

        LOG("LLDB: Session %s terminated\n", session_id.c_str());

        // Free device and notify next agent in queue
        std::unique_lock<std::mutex> lock(queue_mutex_);

        RemoteDevice* device = find_device_by_id(device_id);
        if (device) {
            device->is_available = true;
            device->current_agent_id = "";
            LOG("LLDB: Freed device %s (%s)\n", device->name.c_str(), device->id.c_str());
        }

        if (!global_queue_.empty() && !global_queue_.front().notified) {
            LOG("LLDB: Notifying next agent in queue (%s)\n", global_queue_.front().agent_id.c_str());
            global_queue_.front().notified = true;
            global_queue_.front().cv->notify_one();
        }

        return {
            {"status", "success"},
            {"request_id", request_id}
        };

    } catch (const std::exception& e) {
        LOG("LLDB: Exception in handle_stop_session: %s\n", e.what());
        return {
            {"status", "error"},
            {"error", std::string("Exception: ") + e.what()},
            {"request_id", request_id}
        };
    }
}

void LLDBSessionManager::cleanup_agent_sessions(const std::string& agent_id) {
    LOG("LLDB: Cleaning up sessions for crashed agent %s\n", agent_id.c_str());

    // Find and copy sessions owned by this agent, then remove from map
    std::vector<LLDBSession> sessions_to_cleanup;
    std::vector<std::string> freed_devices;
    {
        std::lock_guard<std::mutex> lock(active_sessions_mutex_);
        for (auto it = active_sessions_.begin(); it != active_sessions_.end();) {
            if (it->second.agent_id == agent_id) {
                LOG("LLDB: Terminating session %s owned by crashed agent %s\n",
                    it->first.c_str(), agent_id.c_str());

                sessions_to_cleanup.push_back(it->second);
                freed_devices.push_back(it->second.device_id);
                it = active_sessions_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Cleanup processes outside the lock
    for (const auto& session : sessions_to_cleanup) {
        terminate_lldb_process(session.lldb_pid, session.pty_master_fd);

        // Also cleanup remote debugserver and debugged process
        RemoteDevice* device = nullptr;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            device = find_device_by_id(session.device_id);
        }

        if (device) {
            LOG("LLDB: Cleaning up remote processes for crashed agent (debugserver=%d, debugged=%d)\n",
                session.remote_debugserver_pid, session.remote_debugged_pid);

            stop_remote_processes(
                device->host,
                device->ssh_port,
                device->ssh_user,
                session.remote_debugserver_pid,
                session.remote_debugged_pid
            );
        }
    }

    // Free devices and update queue with proper locking
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        // Free devices that were in use by crashed agent
        for (const auto& device_id : freed_devices) {
            RemoteDevice* device = find_device_by_id(device_id);
            if (device) {
                device->is_available = true;
                device->current_agent_id = "";
                LOG("LLDB: Freed device %s (%s) from crashed agent\n",
                    device->name.c_str(), device->id.c_str());
            }
        }

        // Also check if agent was holding a device without active session (rare edge case)
        for (auto& device : devices_) {
            if (device.current_agent_id == agent_id && device.is_available == false) {
                device.is_available = true;
                device.current_agent_id = "";
                LOG("LLDB: Freed orphaned device %s (%s) from crashed agent\n",
                    device.name.c_str(), device.id.c_str());
            }
        }

        // Remove agent from queue
        std::queue<QueueEntry> new_queue;
        while (!global_queue_.empty()) {
            auto entry = global_queue_.front();
            global_queue_.pop();
            if (entry.agent_id != agent_id) {
                new_queue.push(entry);
            } else {
                LOG("LLDB: Removed crashed agent %s from queue\n", agent_id.c_str());
            }
        }
        global_queue_ = std::move(new_queue);

        // Notify next agent in queue (device may have been freed)
        if (!global_queue_.empty() && !freed_devices.empty() && !global_queue_.front().notified) {
            LOG("LLDB: Notifying next agent in queue after crash cleanup (%s)\n",
                global_queue_.front().agent_id.c_str());
            global_queue_.front().notified = true;
            global_queue_.front().cv->notify_one();
        }
    }
}

// Helper methods

std::string LLDBSessionManager::generate_session_id() const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);

    std::stringstream ss;
    ss << "lldb_";
    for (int i = 0; i < 16; i++) {
        ss << std::hex << dis(gen);
    }
    return ss.str();
}

json LLDBSessionManager::load_lldb_config() const {
    LOG("LLDB: Loading device pool configuration\n");

    // Clear existing devices (const_cast needed since this logically modifies state)
    auto& devices = const_cast<std::vector<RemoteDevice>&>(const_cast<LLDBSessionManager*>(this)->devices_);
    devices.clear();

    // Step 1: Load global device registry from config
    const Config& global_config = Config::instance();
    if (global_config.lldb.devices.empty()) {
        LOG("LLDB: Warning - No devices configured in global config\n");
        // Continue anyway - will throw error at end if still empty
    }

    // Step 2: Load workspace overrides (enabled + remote_binary_path per device)
    std::string config_path = workspace_path_ + "/lldb_config.json";
    json workspace_overrides;

    std::ifstream file(config_path);
    if (file) {
        try {
            file >> workspace_overrides;
        } catch (const std::exception& e) {
            LOG("LLDB: Warning - Failed to parse workspace config: %s\n", e.what());
        }
    } else {
        LOG("LLDB: No workspace config found at %s - all devices disabled by default\n", config_path.c_str());
    }

    // Step 3: Merge global devices with workspace overrides
    for (const auto& global_device : global_config.lldb.devices) {
        RemoteDevice device;

        // Copy from global registry
        device.id = global_device.id;
        device.name = global_device.name;
        device.host = global_device.host;
        device.ssh_port = global_device.ssh_port;
        device.ssh_user = global_device.ssh_user;
        device.debugserver_port = irc_port_;

        // TODO: (port conflicts)
        // currently the orchestrator WILL break if multiple binaries have been spawned with the same name (ida_swarm_workspace/<bin_name>)
        // and since we use the irc port (just convenience) this will too
        // however this also has another more important issue. the irc port means nothing on the remote debugger. that port could have already been taken by another process
        // we just hope that it hasn't been. i will fix this eventually (probably when i make rewrite everything to use idalib)

        LOG_INFO("LLDB: Device '%s' auto-assigned debugserver port %d (from IRC port)\n",
                 device.name.c_str(), device.debugserver_port);

        // Copy cached device info
        if (global_device.device_info) {
            DeviceInfo info;
            info.udid = global_device.device_info->udid;
            info.model = global_device.device_info->model;
            info.ios_version = global_device.device_info->ios_version;
            info.name = global_device.device_info->name;
            device.device_info = info;
        }

        // Apply per-workspace overrides (enabled + remote_binary_path)
        // we use 2 levels for lldb config:
        // 1: global (inside the main config file) which contains device information that doesn't change. it's the global registry for possible remote debuggers
        // 2: per-workspace (ida_swarm_workspace/<bin_name>/lldb_config.json) which contains for the given binary which remote debuggers are available for it
        // we use a separate per-workspace config because we expect the user to handle getting the remote setup. for example for .app bundles we expect that the user has already synced the full .app bundle so the app can run normally
        // then all we have to do is sync over the agent binary (could be patched, we sync and overwrite the previous on the remote whenever an agent starts a session) and run it
        // i will be writing a skill for claude code so that it can perform all the initial setup needed (that the user would normally do) for remote debugging

        device.enabled = false;
        device.remote_binary_path = "";

        if (workspace_overrides.contains("device_overrides") &&
            workspace_overrides["device_overrides"].contains(device.id)) {
            const auto& override = workspace_overrides["device_overrides"][device.id];
            device.enabled = override.value("enabled", false);
            device.remote_binary_path = override.value("remote_binary_path", "");
        }

        // Initialize runtime state
        device.is_available = true;
        device.current_agent_id = "";
        device.health_status = device.enabled ? ConnectionHealth::HEALTHY : ConnectionHealth::DISABLED;

        devices.push_back(device);
        LOG("LLDB: Loaded device: %s (%s) at %s [%s]\n",
            device.name.c_str(), device.id.c_str(), device.host.c_str(),
            device.enabled ? "enabled" : "disabled");
    }

    if (devices.empty()) {
        throw std::runtime_error("No devices configured in global config. Please add devices in Preferences -> LLDB.");
    }

    // Return workspace overrides for potential use
    return workspace_overrides;
}

bool LLDBSessionManager::validate_lldb_config(const json& config, std::string& error_msg) const {
    if (!config.contains("remote_host") || config["remote_host"].get<std::string>().empty()) {
        error_msg = "lldb_config.json missing or empty 'remote_host' field";
        return false;
    }

    if (!config.contains("remote_port") || config["remote_port"].get<int>() <= 0) {
        error_msg = "lldb_config.json missing or invalid 'remote_port' field";
        return false;
    }

    if (!config.contains("remote_binary_path") || config["remote_binary_path"].get<std::string>().empty()) {
        error_msg = "lldb_config.json missing or empty 'remote_binary_path' field";
        return false;
    }

    return true;
}

int LLDBSessionManager::spawn_lldb_with_pty(RemoteDevice& device, int* pty_master_fd) {
#ifdef _WIN32
    // TODO: Windows PTY support using CreatePseudoConsole (Windows 10+)
    LOG("LLDB: Windows PTY support not implemented yet\n");
    return -1;
#else
    int master_fd, slave_fd;
    char slave_name[256];

    // Create PTY with default settings
    // We use NULL for termios and winsize to get default PTY behavior
    // This avoids terminal echo interfering with LLDB output reading
    if (openpty(&master_fd, &slave_fd, nullptr, nullptr, nullptr) < 0) {
        LOG("LLDB: Failed to create PTY: %s\n", strerror(errno));
        return -1;
    }

    // Set master to non-blocking for reads
    int flags = fcntl(master_fd, F_GETFL, 0);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);

    pid_t pid = fork();
    if (pid < 0) {
        LOG("LLDB: Fork failed: %s\n", strerror(errno));
        close(master_fd);
        close(slave_fd);
        return -1;
    }

    if (pid == 0) {
        // Child process
        close(master_fd);

        // Make slave the controlling terminal
        setsid();
#undef ioctl
        ioctl(slave_fd, TIOCSCTTY, 0);
#define ioctl dont_use_ioctl

        // Redirect stdin/stdout/stderr to slave
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);

        if (slave_fd > 2) {
            close(slave_fd);
        }

        // Execute LLDB with --no-lldbinit to avoid loading user plugins that may crash
        // This prevents issues like libtoolsuite.dylib crashing during PluginInitialize
        execl(lldb_path_.c_str(), lldb_path_.c_str(), "--no-lldbinit", nullptr);

        // If exec fails
        exit(1);
    }

    // Parent process
    close(slave_fd);
    *pty_master_fd = master_fd;

    LOG("LLDB: Spawned LLDB process (pid=%d, master_fd=%d)\n", pid, master_fd);

    // Wait for initial prompt
    sleep(1);  // Give LLDB time to start

    return pid;
#endif
}

bool LLDBSessionManager::write_to_lldb(int pty_fd, const std::string& command) {
#ifdef _WIN32
    // TODO: Windows write support
    return false;
#else
    std::string cmd_with_newline = command + "\n";
    ssize_t written = write(pty_fd, cmd_with_newline.c_str(), cmd_with_newline.size());
    return written == (ssize_t)cmd_with_newline.size();
#endif
}

// Specialized read function for 'process connect' command
// Waits specifically for connection result (Process stopped, or error)
// rather than just any prompt, because async mode may show prompt early
std::string LLDBSessionManager::read_lldb_until_connect_complete(int pty_fd, int timeout_ms) {
#ifdef _WIN32
    return "";
#else
    std::string output;
    char buffer[4096];
    auto start_time = std::chrono::steady_clock::now();
    int consecutive_idle_cycles = 0;
    const int required_idle_cycles = 3;  // 300ms of no data after seeing result

    // For process connect, successful output looks like:
    // (lldb) process connect connect://...
    // Process 26818 stopped
    // * thread #1, stop reason = signal SIGSTOP
    //     frame #0: 0x0000000100e91000
    // dyld`_dyld_start:
    // ->  0x100e91000 <+0>:  mov    x28, sp
    //     0x100e91004 <+4>:  and    sp, x28, #0xfffffffffffffff0
    // (lldb)    <-- sometimes this prompt doesn't appear!
    //
    // The "->" marker in disassembly is the clearest indicator of success -
    // it only appears when the process is stopped at an instruction.
    auto is_connect_complete = [](const std::string& s) -> bool {
        // Success: we see "-> " followed by "0x" (current instruction in disassembly)
        bool has_current_instruction = s.find("->") != std::string::npos &&
                                       s.find("0x") != std::string::npos;

        // Failure: error message
        bool has_error = s.find("error:") != std::string::npos;

        return has_current_instruction || has_error;
    };

    while (true) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        if (elapsed >= timeout_ms) {
            LOG("LLDB: Connect read timeout after %lld ms\n", (long long)elapsed);
            break;
        }

        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(pty_fd, &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms

        int result = select(pty_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (result < 0) {
            if (errno == EINTR) continue;
            LOG("LLDB: Select error in connect read: %s\n", strerror(errno));
            break;
        }

        if (result == 0) {
            // No data - check if we have complete connect output
            if (is_connect_complete(output)) {
                consecutive_idle_cycles++;
                if (consecutive_idle_cycles >= required_idle_cycles) {
                    break;  // 300ms of silence after seeing result
                }
            }
            continue;
        }

        // Data available - reset idle counter
        consecutive_idle_cycles = 0;

        ssize_t bytes_read = read(pty_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) break;

        buffer[bytes_read] = '\0';
        output += buffer;
    }

    return strip_ansi_codes(output);
#endif
}

std::string LLDBSessionManager::read_from_lldb_until_prompt(int pty_fd, int timeout_ms) {
#ifdef _WIN32
    // TODO: Windows read support
    return "";
#else
    std::string output;
    char buffer[4096];
    auto start_time = std::chrono::steady_clock::now();
    int consecutive_idle_cycles = 0;
    const int required_idle_cycles = 3;  // 300ms of silence after seeing prompt

    while (true) {
        // Check timeout
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        if (elapsed >= timeout_ms) {
            LOG("LLDB: Read timeout after %lld ms\n", (long long)elapsed);
            break;
        }

        // Use select with timeout
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(pty_fd, &read_fds);

        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms

        int result = select(pty_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            LOG("LLDB: Select error: %s\n", strerror(errno));
            break;
        }

        if (result == 0) {
            // No data - check if we have a prompt and can exit
            bool has_prompt = output.find("(lldb)") != std::string::npos;
            if (has_prompt) {
                consecutive_idle_cycles++;
                if (consecutive_idle_cycles >= required_idle_cycles) {
                    // 300ms of silence after seeing prompt - done
                    break;
                }
            }
            continue;
        }

        // Data available - reset idle counter
        consecutive_idle_cycles = 0;

        ssize_t bytes_read = read(pty_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) {
            break;
        }

        buffer[bytes_read] = '\0';
        output += buffer;
    }

    return strip_ansi_codes(output);
#endif
}

std::string LLDBSessionManager::strip_ansi_codes(const std::string& input) const {
    // Remove ANSI escape sequences: \x1b[...m
    std::regex ansi_regex("\x1b\\[[0-9;]*m");
    return std::regex_replace(input, ansi_regex, "");
}

std::optional<uint64_t> LLDBSessionManager::parse_image_base_from_lldb_output(const std::string& output) const {
    // Parse output like:
    // [  0] 8A6E4F2A-0000-0000-0000-000000000000 0x000000010abcd000 /path/to/binary
    std::regex base_regex(R"(\[\s*0\]\s+[0-9A-Fa-f-]+\s+(0x[0-9A-Fa-f]+))");
    std::smatch match;

    if (std::regex_search(output, match, base_regex)) {
        std::string base_str = match[1].str();
        try {
            return std::stoull(base_str, nullptr, 16);
        } catch (const std::exception& e) {
            LOG("LLDB: Failed to parse address '%s': %s\n", base_str.c_str(), e.what());
            return std::nullopt;
        }
    }

    LOG("LLDB: Failed to parse image base from output: %s\n", output.c_str());
    return std::nullopt;
}

uint64_t LLDBSessionManager::get_ida_imagebase() const {
    return get_imagebase();
}

bool LLDBSessionManager::validate_session_ownership(const std::string& session_id,
                                                     const std::string& agent_id,
                                                     std::string& error_msg) const {
    std::lock_guard<std::mutex> lock(active_sessions_mutex_);
    auto it = active_sessions_.find(session_id);
    if (it == active_sessions_.end()) {
        error_msg = "Session not found: " + session_id;
        return false;
    }

    if (it->second.agent_id != agent_id) {
        error_msg = "Session " + session_id + " is owned by different agent";
        return false;
    }

    return true;
}

// Device pool management methods

RemoteDevice* LLDBSessionManager::find_available_device() {
    // Find first enabled, healthy, available device
    for (auto& device : devices_) {
        if (device.enabled &&
            device.is_available &&
            device.health_status == ConnectionHealth::HEALTHY) {
            return &device;
        }
    }
    return nullptr;
}

RemoteDevice* LLDBSessionManager::find_device_by_id(const std::string& device_id) {
    for (auto& device : devices_) {
        if (device.id == device_id) {
            return &device;
        }
    }
    return nullptr;
}

void LLDBSessionManager::discover_and_update_device(RemoteDevice& device) {
    LOG_INFO("LLDB: Discovering device info for %s\n", device.host.c_str());

    std::string error;
    auto info = orchestrator::RemoteDeviceInfoFetcher::fetch_device_info(
        device.host,
        device.ssh_port,
        device.ssh_user,
        error
    );

    if (info) {
        // Update device with discovered info
        if (device.id.empty() || device.id.find("legacy") != std::string::npos) {
            device.id = info->udid;
        }
        if (device.name.empty() || device.name == device.host) {
            device.name = info->name;
        }
        device.device_info = *info;

        LOG_INFO("LLDB: Discovered device: %s (UDID: %s, iOS: %s)\n", device.name.c_str(), info->udid.c_str(), info->ios_version.c_str());

        // Save updated config
        save_lldb_config();
    } else {
        LOG_INFO("LLDB: Failed to discover device info: %s\n", error.c_str());
    }
}

bool LLDBSessionManager::initialize_remote_device(RemoteDevice& device, std::string& error) {
    LOG("LLDB: Initializing remote device %s (%s)\n", device.name.c_str(), device.host.c_str());

    // Connect via SSH
    orchestrator::SSH2SessionGuard ssh;
    if (!ssh.connect(device.host, device.ssh_port, device.ssh_user, error)) {
        error = "Failed to connect via SSH: " + error;
        return false;
    }

    // =========================================================================
    // PLATFORM DETECTION - CURRENTLY iOS ONLY
    // =========================================================================
    //
    // This remote debugging implementation ONLY supports jailbroken iOS devices.
    //
    // Why iOS-only:
    // 1. Uses Apple's 'debugserver' which is iOS/macOS specific
    // 2. Requires code signing tools (ldid/jtool) for iOS code signature
    // 3. Uses 'process connect connect://host:port' protocol specific to debugserver
    //
    // TODO: Future platform support would require:
    // - Linux:   lldb-server or gdbserver, no code signing
    // - Android: lldb-server for Android, different connection protocol
    // - macOS:   debugserver works but needs different setup (no jailbreak)
    // - Windows: Windows remote debugging protocol
    //
    // Each platform would need:
    // 1. Platform detection logic
    // 2. Appropriate debug server binary/command
    // 3. Platform-specific connection protocol in spawn_lldb_with_pty
    // 4. Platform-specific code signing (or none)
    // =========================================================================

    // Detect if iOS by checking for jailbreak directory
    // We check BOTH /var/jb (modern jailbreaks like Dopamine) and /var/lib/dpkg (legacy + Cydia)
    std::string jb_check_error;
    std::string jb_check = ssh.exec("[ -d /var/jb ] || [ -d /var/lib/dpkg ] && echo YES || echo NO", jb_check_error);
    bool is_jailbroken_ios = (jb_check.find("YES") != std::string::npos);

    // Also verify debugserver is available (the core requirement)
    std::string debugserver_check_error;
    std::string debugserver_check = ssh.exec("command -v debugserver >/dev/null 2>&1 && echo YES || echo NO", debugserver_check_error);
    bool has_debugserver = (debugserver_check.find("YES") != std::string::npos);

    if (!is_jailbroken_ios) {
        error = "Remote debugging currently only supports jailbroken iOS devices. "
                "Device does not appear to be jailbroken (no /var/jb or /var/lib/dpkg). "
                "Future versions may support Linux, Android, and other platforms.";
        LOG("LLDB: ERROR - %s\n", error.c_str());
        return false;
    }

    if (!has_debugserver) {
        error = "debugserver not found on iOS device. "
                "Please ensure debugserver is installed (usually comes with developer tools or can be extracted from Xcode).";
        LOG("LLDB: ERROR - %s\n", error.c_str());
        return false;
    }

    LOG("LLDB: Device is jailbroken iOS with debugserver available\n");

    // iOS requires code signing - check for signing tools
    LOG("LLDB: Checking for code signing tools...\n");

    // Check for ldid (preferred)
    std::string ldid_check_error;
    std::string ldid_check = ssh.exec("command -v ldid >/dev/null 2>&1 && echo YES || echo NO", ldid_check_error);
    bool has_ldid = (ldid_check.find("YES") != std::string::npos);

    if (has_ldid) {
        device.signing_tool = "ldid";
        LOG("LLDB: Found ldid for code signing\n");
    } else {
        // Check for jtool (alternative)
        std::string jtool_check_error;
        std::string jtool_check = ssh.exec("command -v jtool >/dev/null 2>&1 && echo YES || echo NO", jtool_check_error);
        bool has_jtool = (jtool_check.find("YES") != std::string::npos);

        if (has_jtool) {
            device.signing_tool = "jtool";
            LOG("LLDB: Found jtool for code signing\n");
        } else {
            error = "iOS device requires either 'ldid' or 'jtool' for code signing. "
                    "Please install one of them on the device (e.g., 'apt install ldid').";
            LOG("LLDB: ERROR - %s\n", error.c_str());
            return false;
        }
    }

    device.initialized = true;
    LOG("LLDB: iOS device initialization complete (signing_tool: %s)\n", device.signing_tool.c_str());
    return true;
}

void LLDBSessionManager::save_lldb_config() const {
    std::string config_path = workspace_path_ + "/lldb_config.json";
    LOG_INFO("LLDB: Saving workspace config to %s\n", config_path.c_str());

    // Save workspace overrides only (enabled + remote_binary_path per device)
    json config;
    json device_overrides = json::object();

    for (const auto& device : devices_) {
        json override;
        override["enabled"] = device.enabled;
        override["remote_binary_path"] = device.remote_binary_path;
        device_overrides[device.id] = override;
    }

    config["device_overrides"] = device_overrides;

    // Write to file
    std::ofstream file(config_path);
    if (file) {
        file << config.dump(4);
        LOG_INFO("LLDB: Workspace configuration saved successfully\n");
    } else {
        LOG_INFO("LLDB: Failed to save workspace configuration\n");
    }
}

void LLDBSessionManager::terminate_lldb_process(int lldb_pid, int pty_fd) {
#ifdef _WIN32
    // Windows: Send quit command, then terminate
    if (pty_fd >= 0) {
        write_to_lldb(pty_fd, "quit");
    }
    TerminateProcess((HANDLE)lldb_pid, 0);
    if (pty_fd >= 0) {
        CloseHandle((HANDLE)pty_fd);
    }
#else
    // Unix: Proper shutdown sequence

    // Step 1: Send quit command (may already be sent by caller)
    if (pty_fd >= 0) {
        write_to_lldb(pty_fd, "quit");
    }

    // Step 2: Wait up to 2 seconds for graceful exit
    bool exited = false;
    for (int i = 0; i < 20; i++) {  // 20 * 100ms = 2 seconds
        int status;
#undef waitpid
        int result = waitpid(lldb_pid, &status, WNOHANG);
#define waitpid dont_use_waitpid
        if (result == lldb_pid) {
            exited = true;
            LOG("LLDB: Process %d exited gracefully\n", lldb_pid);
            break;
        } else if (result == -1) {
            // Process doesn't exist
            exited = true;
            break;
        }
        usleep(100000);  // 100ms
    }

    // Step 3: If still alive, send SIGTERM
    if (!exited) {
        LOG("LLDB: Process %d did not exit gracefully, sending SIGTERM\n", lldb_pid);
        kill(lldb_pid, SIGTERM);

        // Wait up to 3 seconds for SIGTERM
        for (int i = 0; i < 30; i++) {  // 30 * 100ms = 3 seconds
            int status;
#undef waitpid
            int result = waitpid(lldb_pid, &status, WNOHANG);
#define waitpid dont_use_waitpid
            if (result == lldb_pid) {
                exited = true;
                LOG("LLDB: Process %d terminated with SIGTERM\n", lldb_pid);
                break;
            } else if (result == -1) {
                exited = true;
                break;
            }
            usleep(100000);  // 100ms
        }
    }

    // Step 4: If STILL alive, send SIGKILL
    if (!exited) {
        LOG("LLDB: Process %d still alive, sending SIGKILL\n", lldb_pid);
        kill(lldb_pid, SIGKILL);

        // Final blocking wait
        int status;
#undef waitpid
        waitpid(lldb_pid, &status, 0);  // Blocking wait
#define waitpid dont_use_waitpid
        LOG("LLDB: Process %d killed with SIGKILL\n", lldb_pid);
    }

    // Step 5: Close PTY (check for errors)
    if (pty_fd >= 0) {
        if (close(pty_fd) < 0) {
            LOG("LLDB: Warning - failed to close PTY fd %d: %s\n", pty_fd, strerror(errno));
        }
    }
#endif
}


bool LLDBSessionManager::stop_remote_processes(const std::string& host,
                                               int ssh_port,
                                               const std::string& ssh_user,
                                               int debugserver_pid,
                                               int debugged_pid) {
    if (debugserver_pid <= 0 && debugged_pid <= 0) {
        return true;  // Nothing to stop
    }

    LOG("LLDB: Stopping remote processes on %s (debugserver=%d, debugged=%d)\n",
        host.c_str(), debugserver_pid, debugged_pid);

    // Build kill command for both PIDs
    std::string kill_pids;
    if (debugserver_pid > 0 && debugged_pid > 0) {
        kill_pids = std::format("{} {}", debugserver_pid, debugged_pid);
    } else if (debugserver_pid > 0) {
        kill_pids = std::to_string(debugserver_pid);
    } else {
        kill_pids = std::to_string(debugged_pid);
    }

    std::string cmd = std::format("kill -9 {}", kill_pids);

    // Execute via SSH
    std::string error;
    orchestrator::SSH2SessionGuard ssh;
    if (!ssh.connect(host, ssh_port, ssh_user, error)) {
        LOG("LLDB: WARNING - Failed to connect for cleanup: %s\n", error.c_str());
        return false;
    }

    ssh.exec(cmd, error);
    if (!error.empty()) {
        LOG("LLDB: WARNING - Failed to kill remote processes: %s\n", error.c_str());
        return false;
    }

    LOG("LLDB: Successfully stopped remote processes\n");
    return true;
}

bool LLDBSessionManager::start_remote_debugserver(RemoteDevice& device,
                                                  const std::string& binary_path,
                                                  int& out_debugserver_pid,
                                                  int& out_debugged_pid,
                                                  std::string& error) {
    LOG("LLDB: Starting debugserver on %s:%d for binary %s\n",
        device.host.c_str(), device.debugserver_port, binary_path.c_str());

    // Execute via SSH to start debugserver
    orchestrator::SSH2SessionGuard ssh;
    if (!ssh.connect(device.host, device.ssh_port, device.ssh_user, error)) {
        error = "Failed to connect via SSH: " + error;
        return false;
    }

    // DIAGNOSTIC: Check PATH and debugserver location
    std::string diag_error;
    std::string path_check = ssh.exec("echo PATH=$PATH; which debugserver 2>&1; which nohup 2>&1", diag_error);
    LOG("LLDB: Diagnostic - %s\n", path_check.c_str());

    // DIAGNOSTIC: Verify binary exists and is executable
    std::string binary_check = ssh.exec(std::format("ls -la \"{}\" 2>&1", binary_path), diag_error);
    LOG("LLDB: Binary check - %s\n", binary_check.c_str());

    // Build command to start debugserver in background
    // Format: nohup debugserver 0.0.0.0:PORT "/path/to/binary" > /tmp/debugserver.log 2>&1 & echo $!
    // NOTE: Binary path is quoted to handle spaces in app names (e.g., "Krispy Kreme.app/Krispy Kreme")
    // NOTE: PATH is set up via login shell wrapper in SSH2SessionGuard::exec()
    std::string cmd = std::format(
        "nohup debugserver 0.0.0.0:{} \"{}\" > /tmp/debugserver.log 2>&1 & echo $!",
        device.debugserver_port,
        binary_path
    );

    std::string output = ssh.exec(cmd, error);
    if (!error.empty()) {
        error = "Failed to execute debugserver command: " + error;
        return false;
    }

    // Parse debugserver PID from output
    try {
        // Trim whitespace
        output.erase(output.find_last_not_of(" \n\r\t") + 1);
        out_debugserver_pid = std::stoi(output);
        LOG("LLDB: Debugserver started with PID %d\n", out_debugserver_pid);
    } catch (const std::exception& e) {
        error = std::format("Failed to parse debugserver PID from output: '{}'", output);
        return false;
    }

    // Verify debugserver process actually exists and is running
    std::string verify_cmd = std::format("ps -p {} -o comm=", out_debugserver_pid);
    std::string verify_output = ssh.exec(verify_cmd, error);
    if (!error.empty() || verify_output.find("debugserver") == std::string::npos) {
        // Debugserver crashed - fetch log for diagnostics
        std::string log_error;
        std::string debugserver_log = ssh.exec("tail -20 /tmp/debugserver.log 2>&1", log_error);
        if (!log_error.empty() || debugserver_log.empty()) {
            LOG("LLDB: Failed to fetch debugserver log: %s\n", log_error.c_str());
        } else {
            LOG("LLDB: Debugserver log (last 20 lines):\n%s\n", debugserver_log.c_str());
        }

        error = std::format("Debugserver process {} not running (may have exited immediately)", out_debugserver_pid);
        LOG("LLDB: ERROR - %s\n", error.c_str());
        return false;
    }
    LOG("LLDB: Verified debugserver process is running\n");

    // Wait a moment for debugserver to launch the binary
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Find the debugged process PID by looking for child of debugserver
    // SECURITY: Use parent PID matching instead of binary name to avoid shell injection
    // The debugged process will be a child of the debugserver process
    // NOTE: Using grep+sed since awk is not available on all jailbroken iOS devices
    std::string ps_cmd = std::format(
        "ps -o pid,ppid | grep ' {}$' | sed 's/^[[:space:]]*\\([0-9]*\\).*/\\1/'",
        out_debugserver_pid
    );

    std::string ps_output = ssh.exec(ps_cmd, error);
    if (!error.empty()) {
        // Don't fail - debugged process PID is optional
        LOG("LLDB: WARNING - Failed to get debugged process PID: %s\n", error.c_str());
        out_debugged_pid = -1;
        error.clear();  // Clear error since this is non-fatal
    } else {
        try {
            ps_output.erase(ps_output.find_last_not_of(" \n\r\t") + 1);
            if (!ps_output.empty()) {
                out_debugged_pid = std::stoi(ps_output);
                LOG("LLDB: Debugged process PID %d\n", out_debugged_pid);
            } else {
                LOG("LLDB: Debugged process not yet started (will start on LLDB connect)\n");
                out_debugged_pid = -1;
            }
        } catch (const std::exception& e) {
            LOG("LLDB: WARNING - Failed to parse debugged PID from: '%s'\n", ps_output.c_str());
            out_debugged_pid = -1;
        }
    }

    // Wait for debugserver port to be ready
    // CRITICAL: We must check via SSH on the remote device, NOT by connecting from local machine!
    // Debugserver only accepts ONE client connection. If we TCP connect from here to test,
    // we consume that one-shot connection and LLDB will fail with "Failed to connect port".
    {
        auto start = std::chrono::steady_clock::now();
        const int timeout_ms = 5000;
        bool port_ready = false;

        std::string check_cmd = std::format(
            "netstat -an 2>/dev/null | grep -q '[:.]{}.*LISTEN' && echo LISTENING || echo NOT_LISTENING",
            device.debugserver_port);

        while (!port_ready) {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();

            if (elapsed >= timeout_ms) {
                LOG("LLDB: Timeout waiting for port %d to be listening\n", device.debugserver_port);
                break;
            }

            std::string check_error;
            std::string result = ssh.exec(check_cmd, check_error);
            if (result.find("LISTENING") != std::string::npos) {
                port_ready = true;
                LOG("LLDB: Port %d is listening on remote device\n", device.debugserver_port);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            }
        }

        if (!port_ready) {
            // Fetch debugserver log for diagnostics
            std::string log_error;
            std::string debugserver_log = ssh.exec("tail -20 /tmp/debugserver.log 2>&1", log_error);
            if (!log_error.empty() || debugserver_log.empty()) {
                LOG("LLDB: Failed to fetch debugserver log: %s\n", log_error.c_str());
            } else {
                LOG("LLDB: Debugserver log (last 20 lines):\n%s\n", debugserver_log.c_str());
            }

            error = "Debugserver started but port not ready within timeout";
            // Cleanup: kill the debugserver we just started
            stop_remote_processes(device.host, device.ssh_port, device.ssh_user, out_debugserver_pid, -1);
            return false;
        }
    }

    LOG("LLDB: Debugserver ready and listening on port %d\n", device.debugserver_port);
    return true;
}

} // namespace llm_re
