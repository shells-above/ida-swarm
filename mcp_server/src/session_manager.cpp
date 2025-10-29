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
#include <openssl/sha.h>
#include <errno.h>

// On macOS, environ needs explicit declaration
extern char **environ;

namespace fs = std::filesystem;

namespace llm_re::mcp {

// Helper function to read exactly N bytes from a file descriptor
// Handles partial reads and EINTR interrupts
static ssize_t read_exactly(int fd, void* buf, size_t count) {
    size_t total = 0;
    char* ptr = static_cast<char*>(buf);

    while (total < count) {
        ssize_t n = read(fd, ptr + total, count - total);
        if (n == 0) {
            // EOF reached before reading all bytes
            return (total > 0) ? total : 0;
        }
        if (n < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, retry
                continue;
            }
            // Real error
            return -1;
        }
        total += n;
    }
    return total;
}

SessionManager::SessionManager() {
    // Create root directory for session files if it doesn't exist
    fs::path sessions_dir = sessions_root_dir_;
    if (!fs::exists(sessions_dir)) {
        fs::create_directories(sessions_dir);
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

std::string SessionManager::hash_binary_path(const std::string& binary_path) {
    // Compute SHA256 hash of the absolute path
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(binary_path.c_str()),
           binary_path.length(), hash);

    // Convert to hex string, use first 16 chars (64 bits)
    std::stringstream ss;
    for (int i = 0; i < 8; ++i) {  // 8 bytes = 16 hex chars
        ss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
    }

    return ss.str();
}

std::string SessionManager::generate_session_id(const std::string& binary_path) {
    // Hash the absolute binary path to create deterministic session ID
    std::string hash = hash_binary_path(binary_path);
    std::string session_id = "session_" + hash;

    // Check for hash collision (ultra rare)
    fs::path session_dir = fs::path(sessions_root_dir_) / session_id;
    if (fs::exists(session_dir)) {
        // Read state.json to verify binary path
        fs::path state_file = session_dir / "state.json";
        if (fs::exists(state_file)) {
            try {
                std::ifstream in(state_file);
                json state;
                in >> state;
                in.close();

                std::string stored_path = state.value("binary_path", "");
                if (stored_path != binary_path) {
                    // Hash collision detected! Append suffix
                    std::cerr << "WARNING: Hash collision detected for " << binary_path << std::endl;
                    std::cerr << "  Collides with: " << stored_path << std::endl;

                    // Find unique suffix
                    int suffix = 2;
                    while (fs::exists(fs::path(sessions_root_dir_) / (session_id + "_" + std::to_string(suffix)))) {
                        suffix++;
                    }
                    session_id = session_id + "_" + std::to_string(suffix);
                    std::cerr << "  Using session ID: " << session_id << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "Warning: Could not read state file for collision check: " << e.what() << std::endl;
            }
        }
    }

    return session_id;
}

std::string SessionManager::create_session(const std::string& binary_path, const std::string& initial_task) {
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    // Generate session ID from binary path (deterministic)
    std::string session_id = generate_session_id(binary_path);
    fs::path session_dir = fs::path(sessions_root_dir_) / session_id;

    // Check if session directory already exists (crash recovery scenario)
    if (fs::exists(session_dir)) {
        fs::path state_file = session_dir / "state.json";
        if (fs::exists(state_file)) {
            try {
                std::ifstream in(state_file);
                json state;
                in >> state;
                in.close();

                int pid = state.value("orchestrator_pid", -1);
                std::string stored_path = state.value("binary_path", "");

                // Verify this is the same binary (not a collision)
                if (stored_path == binary_path) {
                    // Check if orchestrator is still alive
                    if (is_orchestrator_alive(pid)) {
                        // Session is alive! Check if already in our map
                        auto it = sessions_.find(session_id);
                        if (it != sessions_.end()) {
                            throw std::runtime_error("Binary already being analyzed in session " + session_id);
                        }
                        // Not in map but process alive - this is a recovery scenario
                        // For now, we'll error - in future could support reconnect
                        throw std::runtime_error("Existing session found for this binary (PID " +
                                                std::to_string(pid) + "). Please close it first.");
                    } else {
                        // Process is dead, cleanup stale session
                        std::cerr << "Found stale session " << session_id << " (PID " << pid
                                  << " is dead), cleaning up..." << std::endl;
                        cleanup_session_directory(session_id);
                    }
                }
                // If different path, this was handled in generate_session_id with suffix
            } catch (const std::exception& e) {
                std::cerr << "Warning: Error reading existing session state: " << e.what() << std::endl;
                // Continue with creating new session
            }
        }
    }

    // Check if binary already has an active session in memory
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

    // Create new session
    std::unique_ptr<Session> session = std::make_unique<Session>();
    session->session_id = session_id;
    session->binary_path = binary_path;
    session->created_at = std::chrono::steady_clock::now();
    session->last_activity = session->created_at;
    session->active = true;

    // Setup session directory and pipes
    session->session_dir = (fs::path(sessions_root_dir_) / session_id).string();
    session->state_file = (fs::path(session->session_dir) / "state.json").string();
    session->request_pipe = (fs::path(session->session_dir) / "request.pipe").string();
    session->response_pipe = (fs::path(session->session_dir) / "response.pipe").string();

    // Create session directory structure
    if (!create_session_directory(session.get())) {
        throw std::runtime_error("Failed to create session directory");
    }

    // Spawn orchestrator process
    session->orchestrator_pid = spawn_orchestrator(binary_path, session_id, session.get());

    if (session->orchestrator_pid <= 0) {
        cleanup_session_directory(session_id);
        throw std::runtime_error("Failed to spawn orchestrator process");
    }

    // Update state file with PID
    if (!update_state_file(session.get())) {
        kill_orchestrator(session->orchestrator_pid);
        cleanup_session_directory(session_id);
        throw std::runtime_error("Failed to write state file");
    }

    // Start reader thread for this session (will open response pipe)
    session->reader_thread = std::make_unique<std::thread>(
        &SessionManager::orchestrator_reader_thread, this, session.get()
    );

    // Open request pipe for writing (blocks until orchestrator opens for reading)
    // CRITICAL: Keep this FD open for the entire session lifetime
    std::cerr << "MCP Server: Opening request pipe (will block until orchestrator ready): "
              << session->request_pipe << std::endl;

    session->request_pipe_fd = open(session->request_pipe.c_str(), O_WRONLY);
    if (session->request_pipe_fd < 0) {
        std::cerr << "MCP Server: Failed to open request pipe: " << strerror(errno) << std::endl;
        session->reader_should_stop = true;
        if (session->reader_thread && session->reader_thread->joinable()) {
            session->reader_thread->join();
        }
        kill_orchestrator(session->orchestrator_pid);
        cleanup_session_directory(session_id);
        throw std::runtime_error("Failed to open request pipe: " + std::string(strerror(errno)));
    }

    std::cerr << "MCP Server: Request pipe opened (fd=" << session->request_pipe_fd << ")" << std::endl;

    // Send initial task
    json init_msg;
    init_msg["type"] = "request";
    init_msg["id"] = "init_" + session_id;
    init_msg["method"] = "start_task";
    init_msg["params"]["task"] = initial_task;

    if (!send_json_to_orchestrator(session.get(), init_msg)) {
        close(session->request_pipe_fd);
        session->request_pipe_fd = -1;
        session->reader_should_stop = true;
        if (session->reader_thread && session->reader_thread->joinable()) {
            session->reader_thread->join();
        }
        kill_orchestrator(session->orchestrator_pid);
        cleanup_session_directory(session_id);
        throw std::runtime_error("Failed to send initial task to orchestrator");
    }

    // Mark initial task as pending
    session->has_pending_request = true;
    session->pending_request_text = initial_task;

    // Store session and track binary
    sessions_[session_id] = std::move(session);
    binary_to_session_[binary_path] = session_id;

    return session_id;
}

json SessionManager::consume_all_responses(Session* session_ptr, int timeout_ms) {
    if (!session_ptr) {
        json error;
        error["error"] = "Invalid session pointer";
        return error;
    }

    std::unique_lock<std::mutex> lock(session_ptr->state_mutex);

    // Wait for response (orchestrator sends exactly one per request)
    if (session_ptr->response_buffer.empty()) {
        if (timeout_ms > 0) {
            bool received = session_ptr->response_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                [session_ptr]() {
                    return !session_ptr->response_buffer.empty();
                });

            if (!received) {
                json error;
                error["error"] = "Timeout waiting for response from orchestrator";
                return error;
            }
        } else {
            // Wait indefinitely for response
            session_ptr->response_cv.wait(lock, [session_ptr]() {
                return !session_ptr->response_buffer.empty();
            });
        }
    }

    // Get the response (only ever one)
    json response = session_ptr->response_buffer[0];
    session_ptr->response_buffer.clear();

    // Clear pending flag - ready for next request
    session_ptr->has_pending_request = false;
    session_ptr->pending_request_text.clear();

    std::cerr << "MCP Server: Consumed response from buffer, cleared pending flag" << std::endl;

    return response;
}

json SessionManager::send_message(const std::string& session_id, const std::string& message, bool wait_for_response) {
    Session* session_ptr = nullptr;

    // Acquire lock to validate session
    {
        std::lock_guard<std::mutex> sessions_lock(sessions_mutex_);

        auto it = sessions_.find(session_id);
        if (it == sessions_.end() || !it->second->active) {
            json error_response;
            error_response["error"] = "Session not found or inactive";
            return error_response;
        }

        session_ptr = it->second.get();
        session_ptr->last_activity = std::chrono::steady_clock::now();

        // Increment usage count to prevent session deletion while we're using it
        session_ptr->usage_count++;
    } // sessions_mutex_ is released here

    // Check for pending request and prepare for new request
    {
        std::lock_guard<std::mutex> state_lock(session_ptr->state_mutex);

        // CRITICAL: Enforce one-request-at-a-time to prevent lost/out-of-order responses
        if (session_ptr->has_pending_request) {
            // Decrement usage count before returning error
            {
                std::lock_guard<std::mutex> usage_lock(session_ptr->usage_mutex);
                if (--session_ptr->usage_count == 0) {
                    session_ptr->usage_cv.notify_all();
                }
            }

            json error_response;
            error_response["error"] = "Cannot send message: session has unconsumed response from previous request: \"" +
                                     session_ptr->pending_request_text + "\". Call wait_for_response() first.";
            return error_response;
        }

        // Clear response buffer and mark new request as pending
        session_ptr->response_buffer.clear();
        session_ptr->has_pending_request = true;
        session_ptr->pending_request_text = message;
    }

    // Create message for orchestrator
    json msg;
    msg["type"] = "request";
    msg["id"] = "msg_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    msg["method"] = "process_input";
    msg["params"]["input"] = message;

    // Send to orchestrator
    if (!send_json_to_orchestrator(session_ptr, msg)) {
        // Clear pending flag since send failed
        {
            std::lock_guard<std::mutex> state_lock(session_ptr->state_mutex);
            session_ptr->has_pending_request = false;
            session_ptr->pending_request_text.clear();
        }

        // Decrement usage count
        {
            std::lock_guard<std::mutex> usage_lock(session_ptr->usage_mutex);
            if (--session_ptr->usage_count == 0) {
                session_ptr->usage_cv.notify_all();
            }
        }

        json error_response;
        error_response["error"] = "Failed to send message to orchestrator";
        return error_response;
    }

    // If background mode (no wait), return success immediately
    if (!wait_for_response) {
        // Decrement usage count
        {
            std::lock_guard<std::mutex> usage_lock(session_ptr->usage_mutex);
            if (--session_ptr->usage_count == 0) {
                session_ptr->usage_cv.notify_all();
            }
        }

        json success_response;
        success_response["success"] = true;
        success_response["message"] = "Message sent, response pending";
        return success_response;
    }

    // Blocking mode: consume all responses (clears pending flag)
    json response = consume_all_responses(session_ptr);

    // Decrement usage count
    {
        std::lock_guard<std::mutex> usage_lock(session_ptr->usage_mutex);
        if (--session_ptr->usage_count == 0) {
            session_ptr->usage_cv.notify_all();
        }
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
    send_json_to_orchestrator(session.get(), shutdown_msg);

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

    // Close pipe file descriptors if still open
    if (session->request_pipe_fd >= 0) {
        close(session->request_pipe_fd);
        session->request_pipe_fd = -1;
    }
    if (session->response_pipe_fd >= 0) {
        close(session->response_pipe_fd);
        session->response_pipe_fd = -1;
    }

    // Only hard kill if process is still alive after graceful timeout
    if (is_orchestrator_alive(session->orchestrator_pid)) {
        std::cerr << "MCP Server: Graceful exit timeout, using hard kill as last resort..." << std::endl;
        kill_orchestrator(session->orchestrator_pid);
    } else {
        std::cerr << "MCP Server: Process exited gracefully" << std::endl;
    }

    // Cleanup pipes and session directory
    cleanup_session_directory(session_id);

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
    std::vector<int> pids_to_wait;
    std::vector<std::string> session_ids;

    // First pass: mark all sessions as inactive and collect pointers
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);
        for (auto& [session_id, session] : sessions_) {
            session->active = false;  // Reject new operations
            sessions_to_close.push_back(session.get());
            pids_to_wait.push_back(session->orchestrator_pid);
            session_ids.push_back(session_id);
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

    // Third pass: send shutdown messages to ALL sessions in parallel
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        std::cerr << "MCP Server: Sending shutdown messages to " << sessions_.size() << " session(s)..." << std::endl;

        for (auto& [session_id, session] : sessions_) {
            // Send shutdown message
            json shutdown_msg;
            shutdown_msg["type"] = "request";
            shutdown_msg["id"] = "shutdown_all";
            shutdown_msg["method"] = "shutdown";

            send_json_to_orchestrator(session.get(), shutdown_msg);

            // Stop reader thread
            session->reader_should_stop = true;
        }
    }

    // Fourth pass: wait for ALL processes to exit (in parallel, up to 60 seconds total)
    std::cerr << "MCP Server: Waiting for all IDA processes to exit gracefully (60s timeout)..." << std::endl;
    auto start_time = std::chrono::steady_clock::now();
    const int max_wait_seconds = 60;

    while (true) {
        // Check if all processes have exited
        bool all_exited = true;
        for (int pid : pids_to_wait) {
            if (is_orchestrator_alive(pid)) {
                all_exited = false;
                break;
            }
        }

        if (all_exited) {
            std::cerr << "MCP Server: All processes exited gracefully" << std::endl;
            break;
        }

        // Check timeout
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count();

        if (elapsed >= max_wait_seconds) {
            std::cerr << "MCP Server: Graceful exit timeout, using hard kill for remaining processes..." << std::endl;
            break;
        }

        // Progress update every 10 seconds
        if (elapsed > 0 && elapsed % 10 == 0) {
            int still_alive = 0;
            for (int pid : pids_to_wait) {
                if (is_orchestrator_alive(pid)) still_alive++;
            }
            std::cerr << "MCP Server: Still waiting for " << still_alive << " process(es) to exit ("
                      << elapsed << "s elapsed)..." << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Fifth pass: final cleanup - join reader threads and kill any remaining processes
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        for (auto& [session_id, session] : sessions_) {
            // Join reader thread
            if (session->reader_thread && session->reader_thread->joinable()) {
                session->reader_thread->join();
            }

            // Close pipe file descriptors if still open
            if (session->request_pipe_fd >= 0) {
                close(session->request_pipe_fd);
                session->request_pipe_fd = -1;
            }
            if (session->response_pipe_fd >= 0) {
                close(session->response_pipe_fd);
                session->response_pipe_fd = -1;
            }

            // Kill any remaining processes
            if (is_orchestrator_alive(session->orchestrator_pid)) {
                std::cerr << "MCP Server: Force-killing session " << session_id
                          << " (PID " << session->orchestrator_pid << ")" << std::endl;
                kill_orchestrator(session->orchestrator_pid);
            }

            // Cleanup pipes and session directory
            cleanup_session_directory(session_id);
        }

        sessions_.clear();
        binary_to_session_.clear();
    }

    std::cerr << "MCP Server: All sessions closed" << std::endl;
}

void SessionManager::force_kill_all_sessions() {
    std::lock_guard<std::mutex> lock(sessions_mutex_);

    std::cerr << "MCP Server: Force-killing all " << sessions_.size() << " session(s) immediately..." << std::endl;

    for (auto& [session_id, session] : sessions_) {
        session->active = false;
        session->reader_should_stop = true;

        // Close pipe file descriptors if still open
        if (session->request_pipe_fd >= 0) {
            close(session->request_pipe_fd);
            session->request_pipe_fd = -1;
        }
        if (session->response_pipe_fd >= 0) {
            close(session->response_pipe_fd);
            session->response_pipe_fd = -1;
        }

        if (is_orchestrator_alive(session->orchestrator_pid)) {
            std::cerr << "MCP Server: Force-killing session " << session_id
                      << " (PID " << session->orchestrator_pid << ")" << std::endl;
            kill(session->orchestrator_pid, SIGKILL);
        }

        // Detach reader thread instead of joining (we're in a hurry)
        if (session->reader_thread && session->reader_thread->joinable()) {
            session->reader_thread->detach();
        }

        // Cleanup pipes and session directory
        cleanup_session_directory(session_id);
    }

    sessions_.clear();
    binary_to_session_.clear();

    std::cerr << "MCP Server: All sessions force-killed" << std::endl;
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


json SessionManager::wait_for_response(const std::string& session_id, int timeout_ms) {
    Session* session_ptr = nullptr;

    // Acquire lock to validate session and increment usage count
    {
        std::lock_guard<std::mutex> lock(sessions_mutex_);

        auto it = sessions_.find(session_id);
        if (it == sessions_.end() || !it->second->active) {
            json error;
            error["error"] = "Session not found or inactive";
            return error;
        }

        session_ptr = it->second.get();
        session_ptr->usage_count++;
    } // sessions_mutex_ released here

    // Consume all responses (clears pending flag)
    json result = consume_all_responses(session_ptr, timeout_ms);

    // Decrement usage count
    {
        std::lock_guard<std::mutex> usage_lock(session_ptr->usage_mutex);
        if (--session_ptr->usage_count == 0) {
            session_ptr->usage_cv.notify_all();
        }
    }

    return result;
}

int SessionManager::spawn_orchestrator(const std::string& binary_path, const std::string& session_id,
                                      Session* session) {
    // Build custom environment with session info (avoids file-based race conditions)
    std::vector<std::string> env_storage;  // Keep strings alive
    std::vector<char*> custom_env;

    // Copy existing environment
    for (char** env = environ; *env != nullptr; env++) {
        env_storage.emplace_back(*env);
    }

    // Add MCP session variables
    env_storage.push_back("IDA_SWARM_MCP_SESSION_ID=" + session_id);
    env_storage.push_back("IDA_SWARM_MCP_SESSION_DIR=" + session->session_dir);

    // Build char* array for posix_spawn
    for (auto& env_str : env_storage) {
        custom_env.push_back(const_cast<char*>(env_str.c_str()));
    }
    custom_env.push_back(nullptr);

    std::cerr << "Set environment variables:" << std::endl;
    std::cerr << "  IDA_SWARM_MCP_SESSION_ID=" << session_id << std::endl;
    std::cerr << "  IDA_SWARM_MCP_SESSION_DIR=" << session->session_dir << std::endl;

    // Use posix_spawn to launch IDA directly
    std::string ida_exe = ida_path_;
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
    int result = posix_spawn(&pid, ida_exe.c_str(), nullptr, nullptr, argv.data(), custom_env.data());

    if (result != 0) {
        std::cerr << "posix_spawn failed: " << strerror(result) << std::endl;
        return -1;
    }

    std::cerr << "Successfully spawned IDA with PID: " << pid << std::endl;
    return pid;
}

bool SessionManager::send_json_to_orchestrator(Session* session, const json& msg) {
    if (!session || session->request_pipe_fd < 0) {
        std::cerr << "Invalid session or pipe not open" << std::endl;
        return false;
    }

    try {
        // Serialize JSON to string
        std::string json_str = msg.dump();
        uint32_t len = json_str.size();

        // Write length-prefixed message to the EXISTING open FD
        // CRITICAL: Do NOT close this FD - it stays open for the session lifetime
        ssize_t n = write(session->request_pipe_fd, &len, sizeof(len));
        if (n != sizeof(len)) {
            std::cerr << "Failed to write message length to pipe: " << strerror(errno) << std::endl;
            return false;
        }

        n = write(session->request_pipe_fd, json_str.c_str(), len);
        if (n != static_cast<ssize_t>(len)) {
            std::cerr << "Failed to write message body to pipe: " << strerror(errno) << std::endl;
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception in send_json_to_orchestrator: " << e.what() << std::endl;
        return false;
    }
}

void SessionManager::orchestrator_reader_thread(Session* session) {
    std::cerr << "MCP Server: Starting reader thread for session " << session->session_id << std::endl;
    std::cerr << "MCP Server: Opening response pipe: " << session->response_pipe << std::endl;

    // Open pipe for reading (blocks until orchestrator opens for writing)
    int fd = open(session->response_pipe.c_str(), O_RDONLY);
    if (fd < 0) {
        std::cerr << "MCP Server: Failed to open response pipe: " << strerror(errno) << std::endl;

        // Push error to buffer to wake up any waiters
        json error_response;
        error_response["error"] = "Failed to open response pipe: " + std::string(strerror(errno));
        {
            std::lock_guard<std::mutex> lock(session->state_mutex);
            session->response_buffer.push_back(error_response);
        }
        session->response_cv.notify_all();
        return;
    }

    // Store file descriptor for potential cleanup
    session->response_pipe_fd = fd;

    std::cerr << "MCP Server: Response pipe opened, waiting for messages..." << std::endl;

    while (!session->reader_should_stop) {
        // Read message length (blocks until data available)
        uint32_t len;
        ssize_t n = read_exactly(fd, &len, sizeof(len));

        if (n == 0) {
            // EOF - orchestrator closed pipe (clean shutdown or crash)
            std::cerr << "MCP Server: Orchestrator closed pipe (EOF detected)" << std::endl;

            // Check if process actually died
            if (!is_orchestrator_alive(session->orchestrator_pid)) {
                std::cerr << "MCP Server: Orchestrator process died (PID "
                          << session->orchestrator_pid << ")" << std::endl;

                json crash_response;
                crash_response["error"] = "Orchestrator process terminated (PID " +
                                          std::to_string(session->orchestrator_pid) + ")";
                {
                    std::lock_guard<std::mutex> lock(session->state_mutex);
                    session->response_buffer.push_back(crash_response);
                }
                session->response_cv.notify_all();
                session->active = false;
            }
            break;
        }

        if (n < 0) {
            // Error reading from pipe
            std::cerr << "MCP Server: Pipe read error: " << strerror(errno) << std::endl;

            json error_response;
            error_response["error"] = "Pipe read error: " + std::string(strerror(errno));
            {
                std::lock_guard<std::mutex> lock(session->state_mutex);
                session->response_buffer.push_back(error_response);
            }
            session->response_cv.notify_all();
            break;
        }

        // Validate message length (sanity check)
        if (len == 0 || len > 10 * 1024 * 1024) {  // Max 10MB
            std::cerr << "MCP Server: Invalid message length: " << len << std::endl;
            break;
        }

        // Read message body
        std::vector<char> buf(len);
        n = read_exactly(fd, buf.data(), len);

        if (n != static_cast<ssize_t>(len)) {
            std::cerr << "MCP Server: Failed to read complete message body (expected "
                      << len << " bytes, got " << n << ")" << std::endl;

            json error_response;
            error_response["error"] = "Incomplete message read";
            {
                std::lock_guard<std::mutex> lock(session->state_mutex);
                session->response_buffer.push_back(error_response);
            }
            session->response_cv.notify_all();
            break;
        }

        // Parse JSON
        try {
            json response = json::parse(buf.begin(), buf.end());

            std::cerr << "MCP Server: Received response from orchestrator: "
                      << response.dump().substr(0, 200) << "..." << std::endl;

            // Add to buffer
            {
                std::lock_guard<std::mutex> lock(session->state_mutex);
                session->response_buffer.push_back(response);
            }
            session->response_cv.notify_all();

        } catch (const json::exception& e) {
            std::cerr << "MCP Server: JSON parse error: " << e.what() << std::endl;

            json error_response;
            error_response["error"] = "JSON parse error: " + std::string(e.what());
            {
                std::lock_guard<std::mutex> lock(session->state_mutex);
                session->response_buffer.push_back(error_response);
            }
            session->response_cv.notify_all();
            break;
        }
    }

    close(fd);
    session->response_pipe_fd = -1;
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

bool SessionManager::create_session_directory(Session* session) {
    if (!session) return false;

    try {
        // Create session directory
        fs::create_directories(session->session_dir);

        // Create named pipes (FIFOs)
        if (mkfifo(session->request_pipe.c_str(), 0666) != 0) {
            std::cerr << "Failed to create request pipe: " << strerror(errno) << std::endl;
            return false;
        }

        if (mkfifo(session->response_pipe.c_str(), 0666) != 0) {
            std::cerr << "Failed to create response pipe: " << strerror(errno) << std::endl;
            unlink(session->request_pipe.c_str());  // Cleanup request pipe
            return false;
        }

        std::cerr << "Created session directory with pipes: " << session->session_dir << std::endl;
        std::cerr << "  Request pipe: " << session->request_pipe << std::endl;
        std::cerr << "  Response pipe: " << session->response_pipe << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create session directory: " << e.what() << std::endl;
        return false;
    }
}

void SessionManager::cleanup_session_directory(const std::string& session_id) {
    fs::path session_dir = fs::path(sessions_root_dir_) / session_id;
    try {
        if (fs::exists(session_dir)) {
            fs::remove_all(session_dir);
            std::cerr << "Cleaned up session directory: " << session_dir << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error cleaning up session directory: " << e.what() << std::endl;
    }
}

bool SessionManager::update_state_file(Session* session) {
    if (!session) return false;

    try {
        json state;
        state["session_id"] = session->session_id;
        state["binary_path"] = session->binary_path;
        state["orchestrator_pid"] = session->orchestrator_pid;

        std::ofstream out(session->state_file);
        if (!out.is_open()) {
            std::cerr << "Failed to open state file for writing" << std::endl;
            return false;
        }

        out << state.dump(2);
        out.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error updating state file: " << e.what() << std::endl;
        return false;
    }
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