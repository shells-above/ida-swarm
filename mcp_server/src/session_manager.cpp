#include "../include/session_manager.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <filesystem>
#include <cstring>
#include <spawn.h>
#include <algorithm>
#include <cctype>

// On macOS, environ needs explicit declaration
extern char **environ;

namespace fs = std::filesystem;

namespace llm_re::mcp {

SessionManager::SessionManager() {
    // Create directory for named pipes if it doesn't exist
    fs::path pipe_dir = "/tmp/ida_mcp_pipes";
    if (!fs::exists(pipe_dir)) {
        fs::create_directories(pipe_dir);
    }
}

SessionManager::~SessionManager() {
    close_all_sessions();
}

std::string SessionManager::get_active_session_for_binary(const std::string& binary_path) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    auto binary_it = binary_to_session_.find(binary_path);
    if (binary_it != binary_to_session_.end()) {
        // Verify the session is still active
        auto session_it = sessions_.find(binary_it->second);
        if (session_it != sessions_.end() && session_it->second->active) {
            return binary_it->second;
        }
    }
    return "";  // No active session for this binary
}

std::string SessionManager::generate_session_id() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << "session_" << time_t << "_" << std::setfill('0') << std::setw(3) << next_session_num_++;
    return ss.str();
}

std::string SessionManager::create_session(const std::string& binary_path, const std::string& initial_task) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    // Check if binary already has an active session
    auto binary_it = binary_to_session_.find(binary_path);
    if (binary_it != binary_to_session_.end()) {
        // Verify the session is still active
        auto session_it = sessions_.find(binary_it->second);
        if (session_it != sessions_.end() && session_it->second->active) {
            throw std::runtime_error("Binary already being analyzed. Use close_session for session " + binary_it->second);
        }
        // If session is not active, remove the stale mapping
        binary_to_session_.erase(binary_it);
    }

    // Check if we've reached max sessions
    if (sessions_.size() >= max_sessions_) {
        throw std::runtime_error("Maximum number of sessions reached");
    }

    // Generate new session ID
    std::string session_id = generate_session_id();

    // Create new session
    std::unique_ptr<Session> session = std::make_unique<Session>();
    session->session_id = session_id;
    session->binary_path = binary_path;
    session->created_at = std::chrono::steady_clock::now();
    session->last_activity = session->created_at;
    session->active = true;

    // Spawn orchestrator process
    session->orchestrator_pid = spawn_orchestrator(binary_path, session_id,
                                                  session->input_fd, session->output_fd);

    if (session->orchestrator_pid <= 0) {
        throw std::runtime_error("Failed to spawn orchestrator process");
    }

    // Start reader thread for this session
    session->reader_thread = std::make_unique<std::thread>(
        &SessionManager::orchestrator_reader_thread, this, session.get()
    );

    // Send initial task
    json init_msg;
    init_msg["type"] = "request";
    init_msg["id"] = "init_" + session_id;
    init_msg["method"] = "start_task";
    init_msg["params"]["task"] = initial_task;

    if (!send_json_to_orchestrator(session->input_fd, init_msg)) {
        kill_orchestrator(session->orchestrator_pid);
        throw std::runtime_error("Failed to send initial task to orchestrator");
    }

    // Store session and track binary
    sessions_[session_id] = std::move(session);
    binary_to_session_[binary_path] = session_id;

    return session_id;
}

json SessionManager::send_message(const std::string& session_id, const std::string& message) {
    Session* session_ptr = nullptr;
    int input_fd = -1;

    // Acquire lock only to validate session and increment usage count
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        auto it = sessions_.find(session_id);
        if (it == sessions_.end() || !it->second->active) {
            json error_response;
            error_response["error"] = "Session not found or inactive";
            return error_response;
        }

        session_ptr = it->second.get();
        input_fd = session_ptr->input_fd;
        session_ptr->last_activity = std::chrono::steady_clock::now();

        // Increment usage count to prevent session deletion while we're using it
        session_ptr->usage_count++;
    } // sessions_mutex_ is released here - other sessions can now send messages concurrently

    // Create message for orchestrator
    json msg;
    msg["type"] = "request";
    msg["id"] = "msg_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    msg["method"] = "process_input";
    msg["params"]["input"] = message;

    // Send to orchestrator (no longer holding sessions_mutex_!)
    if (!send_json_to_orchestrator(input_fd, msg)) {
        // Decrement usage count before returning error
        if (--session_ptr->usage_count == 0) {
            session_ptr->usage_cv.notify_all();
        }

        json error_response;
        error_response["error"] = "Failed to send message to orchestrator";
        return error_response;
    }

    // Wait for response (no longer holding sessions_mutex_!)
    json response;
    {
        std::unique_lock<std::mutex> queue_lock(session_ptr->queue_mutex);
        session_ptr->response_cv.wait(queue_lock, [session_ptr]() {
            return !session_ptr->response_queue.empty();
        });

        response = session_ptr->response_queue.front();
        session_ptr->response_queue.pop();
    }

    // Decrement usage count and notify if this was the last user
    if (--session_ptr->usage_count == 0) {
        session_ptr->usage_cv.notify_all();
    }

    return response;
}

bool SessionManager::close_session(const std::string& session_id) {
    Session* session_raw = nullptr;

    // Mark session as inactive to prevent new operations, then wait for active ones to finish
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            return false;
        }

        session_raw = it->second.get();
        // Mark as inactive so new send_message calls will be rejected
        session_raw->active = false;
    } // Release sessions_mutex_ to avoid deadlock

    // Wait for all active send_message calls to complete before closing
    {
        std::unique_lock<std::mutex> usage_lock(session_raw->usage_mutex);
        session_raw->usage_cv.wait(usage_lock, [session_raw]() {
            return session_raw->usage_count == 0;
        });
    }
    std::cerr << "MCP Server: All active operations completed for session " << session_id << std::endl;

    // Re-acquire sessions_mutex_ for the actual close operations
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    // Get session again (it should still exist since we marked it inactive)
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        return false;  // Session was removed by another thread (shouldn't happen)
    }

    std::unique_ptr<Session>& session = it->second;

    // Send shutdown message to orchestrator
    json shutdown_msg;
    shutdown_msg["type"] = "request";
    shutdown_msg["id"] = "shutdown_" + session_id;
    shutdown_msg["method"] = "shutdown";

    std::cerr << "MCP Server: Sending shutdown message to orchestrator..." << std::endl;
    send_json_to_orchestrator(session->input_fd, shutdown_msg);

    // Give IDA time to gracefully save database and exit (60 seconds)
    std::cerr << "MCP Server: Waiting for graceful IDA exit (60s timeout)..." << std::endl;
    int wait_seconds = 0;
    const int max_wait_seconds = 60;

    while (wait_seconds < max_wait_seconds && is_orchestrator_alive(session->orchestrator_pid)) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        wait_seconds++;
        if (wait_seconds % 10 == 0) {
            std::cerr << "MCP Server: Still waiting for graceful exit (" << wait_seconds << "s)..." << std::endl;
        }
    }

    // Stop reader thread
    session->reader_should_stop = true;
    if (session->reader_thread && session->reader_thread->joinable()) {
        session->reader_thread->join();
    }

    // Only hard kill if process is still alive after graceful timeout
    if (is_orchestrator_alive(session->orchestrator_pid)) {
        std::cerr << "MCP Server: Graceful exit timeout, using hard kill as last resort..." << std::endl;
        kill_orchestrator(session->orchestrator_pid);
    } else {
        std::cerr << "MCP Server: Process exited gracefully" << std::endl;
    }

    // Close file descriptors
    if (session->input_fd >= 0) close(session->input_fd);
    if (session->output_fd >= 0) close(session->output_fd);

    // Cleanup pipes
    cleanup_pipes(session_id);

    // Remove binary tracking
    std::string binary_path = session->binary_path;
    auto binary_it = binary_to_session_.find(binary_path);
    if (binary_it != binary_to_session_.end() && binary_it->second == session_id) {
        binary_to_session_.erase(binary_it);
    }

    // Remove session
    sessions_.erase(it);

    return true;
}

void SessionManager::close_all_sessions() {
    std::vector<Session*> sessions_to_close;

    // First pass: mark all sessions as inactive and collect pointers
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& [session_id, session] : sessions_) {
            session->active = false;  // Reject new operations
            sessions_to_close.push_back(session.get());
        }
    } // Release sessions_mutex_

    // Second pass: wait for all active operations to complete
    for (Session* session : sessions_to_close) {
        std::unique_lock<std::mutex> usage_lock(session->usage_mutex);
        session->usage_cv.wait(usage_lock, [session]() {
            return session->usage_count == 0;
        });
    }
    std::cerr << "MCP Server: All active operations completed for all sessions" << std::endl;

    // Third pass: acquire lock and do the actual cleanup
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    for (auto& [session_id, session] : sessions_) {
        // Send shutdown message
        json shutdown_msg;
        shutdown_msg["type"] = "request";
        shutdown_msg["id"] = "shutdown_all";
        shutdown_msg["method"] = "shutdown";

        send_json_to_orchestrator(session->input_fd, shutdown_msg);

        // Stop reader thread
        session->reader_should_stop = true;
        if (session->reader_thread && session->reader_thread->joinable()) {
            session->reader_thread->join();
        }

        // Kill process
        if (is_orchestrator_alive(session->orchestrator_pid)) {
            kill_orchestrator(session->orchestrator_pid);
        }

        // Close FDs
        if (session->input_fd >= 0) close(session->input_fd);
        if (session->output_fd >= 0) close(session->output_fd);

        // Cleanup pipes
        cleanup_pipes(session_id);
    }

    sessions_.clear();
    binary_to_session_.clear();
}

json SessionManager::get_session_status(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    json status;
    auto it = sessions_.find(session_id);
    if (it == sessions_.end()) {
        status["exists"] = false;
        return status;
    }

    const auto& session = it->second;
    status["exists"] = true;
    status["session_id"] = session_id;
    status["binary_path"] = session->binary_path;
    status["active"] = session->active;
    status["pid"] = session->orchestrator_pid;
    status["process_alive"] = is_orchestrator_alive(session->orchestrator_pid);

    auto now = std::chrono::steady_clock::now();
    auto created_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now - session->created_at).count();
    auto last_activity_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        now - session->last_activity).count();

    status["created_seconds_ago"] = created_seconds;
    status["last_activity_seconds_ago"] = last_activity_seconds;

    return status;
}


json SessionManager::wait_for_initial_response(const std::string& session_id, int timeout_ms) {
    std::cerr << "MCP Server: Waiting for initial response from session " << session_id << " (no timeout)" << std::endl;

    Session* session_ptr = nullptr;

    // Acquire lock to safely access sessions map and increment usage count
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        auto it = sessions_.find(session_id);
        if (it == sessions_.end()) {
            json error;
            error["error"] = "Session not found";
            return error;
        }

        session_ptr = it->second.get();
        // Increment usage count to prevent session deletion while we're waiting
        session_ptr->usage_count++;
    } // sessions_mutex_ released here

    // Wait for response without holding sessions_mutex_
    json response;
    {
        std::unique_lock<std::mutex> lock(session_ptr->queue_mutex);
        // Block indefinitely until response arrives - no timeout
        session_ptr->response_cv.wait(lock, [session_ptr]() {
            return !session_ptr->response_queue.empty();
        });

        response = session_ptr->response_queue.front();
        session_ptr->response_queue.pop();
    }

    std::cerr << "MCP Server: Got initial response from session " << session_id << std::endl;

    // Decrement usage count and notify if this was the last user
    if (--session_ptr->usage_count == 0) {
        session_ptr->usage_cv.notify_all();
    }

    return response;
}

int SessionManager::spawn_orchestrator(const std::string& binary_path, const std::string& session_id,
                                      int& input_fd, int& output_fd) {
    // Create named pipes (FIFOs only, don't open them yet)
    if (!create_pipes(session_id)) {
        return -1;
    }

    // Create MCP config file in the same directory as the binary
    fs::path binary_dir = fs::path(binary_path).parent_path();
    fs::path config_path = binary_dir / "mcp_orchestrator_config.json";

    // Create the config JSON
    json mcp_config;
    mcp_config["session_id"] = session_id;
    mcp_config["input_pipe"] = "/tmp/ida_mcp_pipes/" + session_id + "_in";
    mcp_config["output_pipe"] = "/tmp/ida_mcp_pipes/" + session_id + "_out";

    // Write config file
    try {
        std::ofstream config_file(config_path);
        if (!config_file.is_open()) {
            std::cerr << "Failed to create MCP config file at: " << config_path << std::endl;
            return -1;
        }
        config_file << mcp_config.dump(2);
        config_file.close();
        std::cerr << "Created MCP config file at: " << config_path << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error writing MCP config file: " << e.what() << std::endl;
        return -1;
    }

#ifdef __APPLE__
    // On macOS, IDA crashes when launched directly from command line
    // We need to use launchd to provide the proper launch context
    // This is similar to how the Dock launches applications

    std::cerr << "Spawning IDA on macOS via launchd:" << std::endl;
    std::cerr << "  Binary: " << binary_path << std::endl;
    std::cerr << "  Session: " << session_id << std::endl;

    // Create a unique launchd job label
    std::string job_label = "com.ida.mcp." + session_id;

    // Create a temporary plist file for launchd
    std::string plist_path = "/tmp/" + job_label + ".plist";
    std::ofstream plist(plist_path);
    if (!plist.is_open()) {
        std::cerr << "spawn_orchestrator: Failed to create plist file at: " << plist_path << std::endl;
        return -1;
    }

    // Write the launchd plist
    plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    plist << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
    plist << "<plist version=\"1.0\">\n";
    plist << "<dict>\n";
    plist << "    <key>Label</key>\n";
    plist << "    <string>" << job_label << "</string>\n";
    plist << "    <key>ProgramArguments</key>\n";
    plist << "    <array>\n";
    plist << "        <string>/Applications/IDA Professional 9.0.app/Contents/MacOS/ida64</string>\n";
    plist << "        <string>-A</string>\n";   // autonomous mode (no dialogs)

    // Detect if we need -T flag for Fat Mach-O ARM64 slice selection
    std::string type_flag = detect_type_flag(binary_path);
    if (!type_flag.empty()) {
        plist << "        <string>" << type_flag << "</string>\n";
    }

    plist << "        <string>" << binary_path << "</string>\n";
    plist << "    </array>\n";
    plist << "    <key>RunAtLoad</key>\n";
    plist << "    <false/>\n";
    plist << "    <key>KeepAlive</key>\n";
    plist << "    <false/>\n";
    plist << "    <key>StandardOutPath</key>\n";
    plist << "    <string>/tmp/" << job_label << ".out</string>\n";
    plist << "    <key>StandardErrorPath</key>\n";
    plist << "    <string>/tmp/" << job_label << ".err</string>\n";
    plist << "    <key>EnvironmentVariables</key>\n";
    plist << "    <dict>\n";
    plist << "        <key>__CFBundleIdentifier</key>\n";
    plist << "        <string>com.hexrays.ida64</string>\n";
    plist << "    </dict>\n";
    plist << "</dict>\n";
    plist << "</plist>\n";
    plist.close();

    // Load the launchd job
    std::string load_cmd = "launchctl load " + plist_path + " 2>&1";
    FILE* fp = popen(load_cmd.c_str(), "r");
    if (!fp) {
        std::cerr << "Failed to load launchd job" << std::endl;
        std::remove(plist_path.c_str());
        return -1;
    }
    pclose(fp);

    // Start the job
    std::string start_cmd = "launchctl start " + job_label;
    system(start_cmd.c_str());

    // Give IDA time to start
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // Find the PID of the launched IDA process
    std::string ps_cmd = "ps aux | grep ida64 | grep '" + binary_path + "' | grep -v grep | awk '{print $2}'";

    fp = popen(ps_cmd.c_str(), "r");
    if (!fp) {
        std::cerr << "spawn_orchestrator: Failed to execute ps command" << std::endl;
        // Clean up
        system(("launchctl unload " + plist_path + " 2>/dev/null").c_str());
        std::remove(plist_path.c_str());
        return -1;
    }

    char pid_str[32];
    pid_t pid = -1;
    if (fgets(pid_str, sizeof(pid_str), fp) != nullptr) {
        pid = atoi(pid_str);
        std::cerr << "spawn_orchestrator: Found IDA process with PID: " << pid << std::endl;
    } else {
        std::cerr << "spawn_orchestrator: No IDA process found matching criteria" << std::endl;
    }
    pclose(fp);

    // Don't unload the job - just remove the plist file
    // Unloading kills the running process
    std::remove(plist_path.c_str());

    if (pid <= 0) {
        std::cerr << "spawn_orchestrator: Could not find IDA process after launch, error code: " << pid << std::endl;

        // Try to check if launchctl job is running
        std::string status_cmd = "launchctl list | grep " + job_label + " 2>&1";
        FILE* status_fp = popen(status_cmd.c_str(), "r");
        if (status_fp) {
            char status_output[256];
            std::string status_result;
            while (fgets(status_output, sizeof(status_output), status_fp) != nullptr) {
                status_result += status_output;
            }
            pclose(status_fp);
            if (!status_result.empty()) {
                std::cerr << "spawn_orchestrator: launchctl job status: " << status_result << std::endl;
            } else {
                std::cerr << "spawn_orchestrator: launchctl job not found in list" << std::endl;
            }
        }

        return -1;
    }

    std::cerr << "Successfully spawned IDA with PID: " << pid << std::endl;

    // Wait a moment for IDA window to appear
    std::this_thread::sleep_for(std::chrono::seconds(3));

    // Minimize the IDA window using AppleScript
    std::string minimize_cmd = "osascript -e 'tell application \"System Events\" to tell (first process whose unix id is "
                              + std::to_string(pid) + ") to set value of attribute \"AXMinimized\" of window 1 to true' 2>/dev/null";

    std::cerr << "Attempting to minimize IDA window with PID " << pid << std::endl;
    int result = system(minimize_cmd.c_str());

    if (result == 0) {
        std::cerr << "Successfully minimized IDA window" << std::endl;
    } else {
        // Fallback: Try using window title
        // std::cerr << "First minimize attempt failed, trying alternate method..." << std::endl;
        // std::string alt_minimize_cmd = "osascript -e 'tell application \"IDA\" to set miniaturized of window 1 to true' 2>/dev/null";
        // result = system(alt_minimize_cmd.c_str());
        //
        // if (result == 0) {
        //     std::cerr << "Successfully minimized IDA window using alternate method" << std::endl;
        // } else {
        std::cerr << "Could not minimize IDA window automatically" << std::endl;
        // }
    }

#else
    // Non-macOS platforms can use posix_spawn directly
    std::string ida_exe = "/Applications/IDA Professional 9.0.app/Contents/MacOS/ida64";
    if (!fs::exists(ida_exe)) {
        std::cerr << "IDA executable not found at: " << ida_exe << std::endl;
        return -1;
    }

    // Build stable argv array - need to keep strings alive during posix_spawn
    std::vector<std::string> stable_args;
    stable_args.push_back(ida_exe);           // argv[0] is the program name
    stable_args.push_back("-A");              // Automatic mode FIRST

    // Detect if we need -T flag for Fat Mach-O ARM64 slice selection
    std::string type_flag = detect_type_flag(binary_path);
    if (!type_flag.empty()) {
        std::cerr << "Adding type flag: " << type_flag << std::endl;
        stable_args.push_back(type_flag);
    }

    stable_args.push_back(binary_path);       // Database/binary path LAST

    // Build argv array from stable strings
    std::vector<char*> argv;
    for (auto& arg : stable_args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    std::cerr << "Spawning IDA with posix_spawn:" << std::endl;
    std::cerr << "  IDA exe: " << ida_exe << std::endl;
    std::cerr << "  Binary: " << binary_path << std::endl;
    std::cerr << "  Session: " << session_id << std::endl;

    pid_t pid;
    int result = posix_spawn(&pid, ida_exe.c_str(), nullptr, nullptr, argv.data(), environ);

    if (result != 0) {
        std::cerr << "posix_spawn failed: " << strerror(result) << std::endl;
        return -1;
    }
#endif

    // Parent process - now open the pipes after IDA has been spawned
    std::cerr << "Parent: Spawned IDA with PID " << pid << ", opening pipes..." << std::endl;

    // Block until pipes are opened - this will wait until IDA is fully initialized
    if (!open_pipes(session_id, input_fd, output_fd)) {
        std::cerr << "Failed to open pipes to orchestrator" << std::endl;
        // This should never happen now since open_pipes blocks indefinitely
        cleanup_pipes(session_id);
        return -1;
    }

    return pid;
}

bool SessionManager::send_json_to_orchestrator(int fd, const json& msg) {
    if (fd < 0) return false;

    std::string serialized = msg.dump() + "\n";
    ssize_t written = write(fd, serialized.c_str(), serialized.length());

    if (written != static_cast<ssize_t>(serialized.length())) {
        std::cerr << "Failed to write complete message to orchestrator" << std::endl;
        return false;
    }

    return true;
}

json SessionManager::read_json_from_orchestrator(int fd, int timeout_ms) {
    if (fd < 0) {
        json error;
        error["error"] = "Invalid file descriptor";
        return error;
    }

    // Set non-blocking mode if timeout specified
    if (timeout_ms > 0) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    std::string buffer;
    char read_buf[4096];
    auto start = std::chrono::steady_clock::now();

    while (true) {
        ssize_t bytes = read(fd, read_buf, sizeof(read_buf) - 1);

        if (bytes > 0) {
            read_buf[bytes] = '\0';
            buffer += read_buf;

            // Check for complete JSON message (newline-delimited)
            size_t newline_pos = buffer.find('\n');
            if (newline_pos != std::string::npos) {
                std::string json_str = buffer.substr(0, newline_pos);

                try {
                    return json::parse(json_str);
                } catch (const json::exception& e) {
                    json error;
                    error["error"] = std::string("JSON parse error: ") + e.what();
                    return error;
                }
            }
        } else if (bytes == 0) {
            // EOF
            json error;
            error["error"] = "Orchestrator closed connection";
            return error;
        } else {
            // Check timeout
            if (timeout_ms > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                if (elapsed >= timeout_ms) {
                    json error;
                    error["error"] = "Timeout reading from orchestrator";
                    return error;
                }
            }

            // Small sleep to avoid busy waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void SessionManager::orchestrator_reader_thread(Session* session) {
    std::cerr << "MCP Server: Starting reader thread for session " << session->session_id << std::endl;
    while (!session->reader_should_stop) {
        json response = read_json_from_orchestrator(session->output_fd, 1000); // Increased timeout to 1s

        if (response.contains("error")) {
            if (response["error"] != "Timeout reading from orchestrator") {
                // Real error, not just timeout
                std::cerr << "MCP Server: Reader thread error: " << response["error"] << std::endl;
                break;
            }
            // Timeout is normal, continue
            continue;
        }

        // Add response to queue
        std::cerr << "MCP Server: Received response from orchestrator: " << response.dump().substr(0, 200) << "..." << std::endl;
        {
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->response_queue.push(response);
        }
        session->response_cv.notify_one();
    }
    std::cerr << "MCP Server: Reader thread exiting for session " << session->session_id << std::endl;
}

bool SessionManager::is_orchestrator_alive(int pid) const {
    if (pid <= 0) return false;

    // Check if process exists
    int result = kill(pid, 0);
    return (result == 0);
}

void SessionManager::kill_orchestrator(int pid) {
    if (pid <= 0) return;

    // First try SIGTERM
    kill(pid, SIGTERM);

    // Wait a bit for graceful shutdown
    std::this_thread::sleep_for(std::chrono::seconds(20));

    // If still alive, use SIGKILL
    if (is_orchestrator_alive(pid)) {
        kill(pid, SIGKILL);
    }

    // Wait for process to be reaped
    int status;
    waitpid(pid, &status, 0);
}

bool SessionManager::create_pipes(const std::string& session_id) {
    std::string pipe_dir = "/tmp/ida_mcp_pipes";
    std::string input_pipe = pipe_dir + "/" + session_id + "_in";
    std::string output_pipe = pipe_dir + "/" + session_id + "_out";

    // Create named pipes (but don't open them yet)
    if (mkfifo(input_pipe.c_str(), 0666) != 0 && errno != EEXIST) {
        std::cerr << "Failed to create input pipe: " << strerror(errno) << std::endl;
        return false;
    }

    if (mkfifo(output_pipe.c_str(), 0666) != 0 && errno != EEXIST) {
        std::cerr << "Failed to create output pipe: " << strerror(errno) << std::endl;
        unlink(input_pipe.c_str());
        return false;
    }

    std::cerr << "Created FIFOs for session " << session_id << std::endl;
    return true;
}

bool SessionManager::open_pipes(const std::string& session_id, int& input_fd, int& output_fd) {
    std::string pipe_dir = "/tmp/ida_mcp_pipes";
    std::string input_pipe = pipe_dir + "/" + session_id + "_in";
    std::string output_pipe = pipe_dir + "/" + session_id + "_out";

    std::cerr << "Opening pipes for session " << session_id << "..." << std::endl;

    // Open pipes - now IDA should be starting and will open the other ends
    // We write to input_pipe (IDA reads from it)
    // We read from output_pipe (IDA writes to it)

    // For the input pipe (we write, IDA reads), try non-blocking first
    input_fd = open(input_pipe.c_str(), O_WRONLY | O_NONBLOCK);
    if (input_fd < 0) {
        if (errno == ENXIO) {
            // No reader yet, try blocking mode with a timeout approach
            std::cerr << "Waiting for IDA to open read end of input pipe..." << std::endl;

            // No timeout - IDA can take a long time to load large databases
            std::cerr << "Opening input pipe (this may take a while for large databases)..." << std::endl;
            input_fd = open(input_pipe.c_str(), O_WRONLY);

            if (input_fd < 0) {
                std::cerr << "Failed to open input pipe: " << strerror(errno) << std::endl;
                return false;
            }
        } else {
            std::cerr << "Failed to open input pipe: " << strerror(errno) << std::endl;
            return false;
        }
    }

    // For the output pipe (we read, IDA writes), similar approach
    output_fd = open(output_pipe.c_str(), O_RDONLY | O_NONBLOCK);
    if (output_fd < 0) {
        if (errno == ENXIO) {
            std::cerr << "Waiting for IDA to open write end of output pipe..." << std::endl;

            // No timeout - IDA can take a long time to load large databases
            std::cerr << "Opening output pipe (this may take a while for large databases)..." << std::endl;
            output_fd = open(output_pipe.c_str(), O_RDONLY);

            if (output_fd < 0) {
                std::cerr << "Failed to open output pipe: " << strerror(errno) << std::endl;
                close(input_fd);
                return false;
            }
        } else {
            std::cerr << "Failed to open output pipe: " << strerror(errno) << std::endl;
            close(input_fd);
            return false;
        }
    }

    std::cerr << "Successfully opened pipes to IDA orchestrator" << std::endl;
    return true;
}

void SessionManager::cleanup_pipes(const std::string& session_id) {
    std::string pipe_dir = "/tmp/ida_mcp_pipes";
    std::string input_pipe = pipe_dir + "/" + session_id + "_in";
    std::string output_pipe = pipe_dir + "/" + session_id + "_out";

    unlink(input_pipe.c_str());
    unlink(output_pipe.c_str());
}

std::string SessionManager::detect_type_flag(const std::string& binary_path) {
    // Use file command to detect binary type
    std::string file_cmd = "file '" + binary_path + "' 2>/dev/null";

    FILE* fp = popen(file_cmd.c_str(), "r");
    if (!fp) {
        std::cerr << "Failed to run file command, using auto-detect" << std::endl;
        return "";  // Empty string means no flag (auto-detect)
    }

    char output[1024];
    std::string file_output;
    while (fgets(output, sizeof(output), fp) != nullptr) {
        file_output += output;
    }
    pclose(fp);

    // Convert to lowercase for easier matching
    std::string lower_output = file_output;
    std::transform(lower_output.begin(), lower_output.end(), lower_output.begin(), ::tolower);

    // Check for Universal/Fat Mach-O files with ARM64 slice
    // Note: file command returns "universal binary" not "fat"
    if ((lower_output.find("universal") != std::string::npos || lower_output.find("fat") != std::string::npos) &&
        lower_output.find("mach-o") != std::string::npos) {

        // Use lipo command to check for ARM64 slice
        std::string lipo_cmd = "lipo -archs '" + binary_path + "' 2>/dev/null";
        FILE* lipo_fp = popen(lipo_cmd.c_str(), "r");
        if (lipo_fp) {
            char lipo_output[256];
            std::string lipo_result;
            while (fgets(lipo_output, sizeof(lipo_output), lipo_fp) != nullptr) {
                lipo_result += lipo_output;
            }
            pclose(lipo_fp);

            // Convert to lowercase
            std::transform(lipo_result.begin(), lipo_result.end(), lipo_result.begin(), ::tolower);

            if (lipo_result.find("arm64") != std::string::npos) {
                return "-TFat Mach-O file, 2. ARM64";
            }
        }
    }

    return "";  // Empty string means no flag (auto-detect)
}

} // namespace llm_re::mcp