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

// On macOS, environ needs explicit declaration
extern char **environ;

namespace fs = std::filesystem;

namespace llm_re::mcp {

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

    // Setup session directory and files
    session->session_dir = (fs::path(sessions_root_dir_) / session_id).string();
    session->state_file = (fs::path(session->session_dir) / "state.json").string();
    session->request_file = (fs::path(session->session_dir) / "request.json").string();
    session->response_file = (fs::path(session->session_dir) / "response.json").string();
    session->request_seq_file = (fs::path(session->session_dir) / "request_seq").string();
    session->response_seq_file = (fs::path(session->session_dir) / "response_seq").string();
    session->request_seq = 0;
    session->response_seq = 0;

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

    if (!send_json_to_orchestrator(session.get(), init_msg)) {
        kill_orchestrator(session->orchestrator_pid);
        cleanup_session_directory(session_id);
        throw std::runtime_error("Failed to send initial task to orchestrator");
    }

    // Store session and track binary
    sessions_[session_id] = std::move(session);
    binary_to_session_[binary_path] = session_id;

    return session_id;
}

json SessionManager::send_message(const std::string& session_id, const std::string& message, bool wait_for_response) {
    Session* session_ptr = nullptr;

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
        session_ptr->last_activity = std::chrono::steady_clock::now();

        // Check for pending message - enforce single message at a time
        if (session_ptr->has_pending_message) {
            json error_response;
            error_response["error"] = "Cannot send message: session is still processing previous message: \"" +
                                     session_ptr->pending_message_text + "\"";
            return error_response;
        }

        // Mark message as pending
        session_ptr->has_pending_message = true;
        session_ptr->pending_message_text = message;

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
    if (!send_json_to_orchestrator(session_ptr, msg)) {
        // Decrement usage count before returning error
        // Also clear pending flag since send failed
        {
            std::lock_guard<std::mutex> lock(session_ptr->queue_mutex);
            session_ptr->has_pending_message = false;
            session_ptr->pending_message_text.clear();
        }

        if (--session_ptr->usage_count == 0) {
            session_ptr->usage_cv.notify_all();
        }

        json error_response;
        error_response["error"] = "Failed to send message to orchestrator";
        return error_response;
    }

    // If background mode (no wait), return success immediately
    if (!wait_for_response) {
        // Decrement usage count
        if (--session_ptr->usage_count == 0) {
            session_ptr->usage_cv.notify_all();
        }

        json success_response;
        success_response["success"] = true;
        success_response["message"] = "Message sent, response pending";
        return success_response;
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

    // Only hard kill if process is still alive after graceful timeout
    if (is_orchestrator_alive(session->orchestrator_pid)) {
        std::cerr << "MCP Server: Graceful exit timeout, using hard kill as last resort..." << std::endl;
        kill_orchestrator(session->orchestrator_pid);
    } else {
        std::cerr << "MCP Server: Process exited gracefully" << std::endl;
    }

    // Cleanup session directory
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

            // Kill any remaining processes
            if (is_orchestrator_alive(session->orchestrator_pid)) {
                std::cerr << "MCP Server: Force-killing session " << session_id
                          << " (PID " << session->orchestrator_pid << ")" << std::endl;
                kill_orchestrator(session->orchestrator_pid);
            }

            // Cleanup session directory
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

        if (is_orchestrator_alive(session->orchestrator_pid)) {
            std::cerr << "MCP Server: Force-killing session " << session_id
                      << " (PID " << session->orchestrator_pid << ")" << std::endl;
            kill(session->orchestrator_pid, SIGKILL);
        }

        // Detach reader thread instead of joining (we're in a hurry)
        if (session->reader_thread && session->reader_thread->joinable()) {
            session->reader_thread->detach();
        }

        // Cleanup session directory
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

json SessionManager::get_session_messages(const std::string& session_id, int max_messages) {
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

    // Get accumulated messages without blocking
    json result;
    {
        std::lock_guard<std::mutex> lock(session_ptr->queue_mutex);

        if (session_ptr->accumulated_responses.empty()) {
            // No messages available yet
            result["messages"] = json::array();
            result["has_pending"] = session_ptr->has_pending_message;
            if (session_ptr->has_pending_message) {
                result["pending_message"] = session_ptr->pending_message_text;
            }
        } else {
            // Return accumulated messages
            int count = max_messages > 0 ? std::min(max_messages, (int)session_ptr->accumulated_responses.size())
                                         : session_ptr->accumulated_responses.size();

            json messages = json::array();
            for (int i = 0; i < count; i++) {
                messages.push_back(session_ptr->accumulated_responses[i]);
            }

            // Remove returned messages
            session_ptr->accumulated_responses.erase(
                session_ptr->accumulated_responses.begin(),
                session_ptr->accumulated_responses.begin() + count
            );

            // If we've consumed all messages, clear pending flag
            if (session_ptr->accumulated_responses.empty()) {
                session_ptr->has_pending_message = false;
                session_ptr->pending_message_text.clear();
            }

            result["messages"] = messages;
            result["has_pending"] = session_ptr->has_pending_message;
        }
    }

    // Decrement usage count
    if (--session_ptr->usage_count == 0) {
        session_ptr->usage_cv.notify_all();
    }

    return result;
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

    // Wait for response
    json result;
    {
        std::unique_lock<std::mutex> lock(session_ptr->queue_mutex);

        // Check if response already available
        if (!session_ptr->accumulated_responses.empty()) {
            // Return first message
            result = session_ptr->accumulated_responses[0];
            session_ptr->accumulated_responses.erase(session_ptr->accumulated_responses.begin());

            // Clear pending flag if no more messages
            if (session_ptr->accumulated_responses.empty()) {
                session_ptr->has_pending_message = false;
                session_ptr->pending_message_text.clear();
            }
        } else {
            // Wait for response
            if (timeout_ms > 0) {
                bool received = session_ptr->response_cv.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                    [session_ptr]() {
                        return !session_ptr->accumulated_responses.empty();
                    });

                if (!received) {
                    result["error"] = "Timeout waiting for response";
                } else {
                    result = session_ptr->accumulated_responses[0];
                    session_ptr->accumulated_responses.erase(session_ptr->accumulated_responses.begin());

                    if (session_ptr->accumulated_responses.empty()) {
                        session_ptr->has_pending_message = false;
                        session_ptr->pending_message_text.clear();
                    }
                }
            } else {
                // Wait indefinitely
                session_ptr->response_cv.wait(lock, [session_ptr]() {
                    return !session_ptr->accumulated_responses.empty();
                });

                result = session_ptr->accumulated_responses[0];
                session_ptr->accumulated_responses.erase(session_ptr->accumulated_responses.begin());

                if (session_ptr->accumulated_responses.empty()) {
                    session_ptr->has_pending_message = false;
                    session_ptr->pending_message_text.clear();
                }
            }
        }
    }

    // Decrement usage count
    if (--session_ptr->usage_count == 0) {
        session_ptr->usage_cv.notify_all();
    }

    return result;
}

int SessionManager::spawn_orchestrator(const std::string& binary_path, const std::string& session_id,
                                      Session* session) {
    // Create MCP config file in the same directory as the binary
    fs::path binary_dir = fs::path(binary_path).parent_path();
    fs::path config_path = binary_dir / "mcp_orchestrator_config.json";

    // Create the config JSON with session directory
    json mcp_config;
    mcp_config["session_id"] = session_id;
    mcp_config["session_dir"] = session->session_dir;

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

    std::cerr << "Successfully spawned IDA with PID: " << pid << std::endl;
    return pid;
}

bool SessionManager::send_json_to_orchestrator(Session* session, const json& msg) {
    if (!session) return false;

    try {
        // Write to temporary file first
        std::string temp_file = session->request_file + ".tmp";
        std::ofstream out(temp_file);
        if (!out.is_open()) {
            std::cerr << "Failed to open request file for writing: " << temp_file << std::endl;
            return false;
        }

        out << msg.dump();
        out.close();

        // Atomically rename to actual request file
        fs::rename(temp_file, session->request_file);

        // Increment request sequence to signal orchestrator
        session->request_seq++;
        if (!write_seq_file(session->request_seq_file, session->request_seq)) {
            std::cerr << "Failed to update request sequence file" << std::endl;
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception in send_json_to_orchestrator: " << e.what() << std::endl;
        return false;
    }
}

json SessionManager::read_json_from_orchestrator(Session* session, int timeout_ms) {
    if (!session) {
        json error;
        error["error"] = "Invalid session";
        return error;
    }

    auto start = std::chrono::steady_clock::now();
    uint64_t last_seq = session->response_seq;

    while (true) {
        // Poll the response sequence file
        uint64_t current_seq = read_seq_file(session->response_seq_file);

        if (current_seq > last_seq) {
            // New response available
            try {
                std::ifstream in(session->response_file);
                if (!in.is_open()) {
                    json error;
                    error["error"] = "Failed to open response file";
                    return error;
                }

                std::string content((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
                in.close();

                json result = json::parse(content);
                session->response_seq = current_seq;
                return result;
            } catch (const json::exception& e) {
                json error;
                error["error"] = std::string("JSON parse error: ") + e.what();
                return error;
            } catch (const std::exception& e) {
                json error;
                error["error"] = std::string("Error reading response: ") + e.what();
                return error;
            }
        }

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

        // Sleep briefly to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void SessionManager::orchestrator_reader_thread(Session* session) {
    std::cerr << "MCP Server: Starting reader thread for session " << session->session_id
              << " (polling " << session->response_file << ")" << std::endl;

    int consecutive_errors = 0;
    const int max_consecutive_errors = 5;

    while (!session->reader_should_stop) {
        json response = read_json_from_orchestrator(session, 1000); // 1s timeout

        if (response.contains("error")) {
            std::string error_msg = response["error"].get<std::string>();

            if (error_msg == "Timeout reading from orchestrator") {
                // Timeout is normal, reset error counter and continue
                consecutive_errors = 0;
                continue;
            }

            // Real error occurred
            consecutive_errors++;
            std::cerr << "MCP Server: Reader thread error (" << consecutive_errors << "/" << max_consecutive_errors << "): "
                      << error_msg << std::endl;

            // Only exit if we get too many consecutive errors (indicates permanent failure)
            if (consecutive_errors >= max_consecutive_errors) {
                std::cerr << "MCP Server: Too many consecutive errors, reader thread exiting" << std::endl;
                break;
            }

            // For transient errors, wait a bit and retry
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        // Successfully read a response - reset error counter
        consecutive_errors = 0;

        // Add response to both queue (for blocking wait) and accumulated (for background retrieval)
        std::cerr << "MCP Server: Received response from orchestrator: "
                  << response.dump().substr(0, 200) << "..." << std::endl;
        {
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->response_queue.push(response);
            session->accumulated_responses.push_back(response);  // Also accumulate for get_session_messages
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

bool SessionManager::create_session_directory(Session* session) {
    if (!session) return false;

    try {
        // Create session directory
        fs::create_directories(session->session_dir);

        // Create empty request and response files
        std::ofstream(session->request_file).close();
        std::ofstream(session->response_file).close();

        // Initialize sequence files to 0
        if (!write_seq_file(session->request_seq_file, 0)) {
            std::cerr << "Failed to create request_seq file" << std::endl;
            return false;
        }
        if (!write_seq_file(session->response_seq_file, 0)) {
            std::cerr << "Failed to create response_seq file" << std::endl;
            return false;
        }

        std::cerr << "Created session directory: " << session->session_dir << std::endl;
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
        state["active"] = session->active;
        state["created_at"] = std::chrono::duration_cast<std::chrono::seconds>(
            session->created_at.time_since_epoch()).count();
        state["last_activity"] = std::chrono::duration_cast<std::chrono::seconds>(
            session->last_activity.time_since_epoch()).count();

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

uint64_t SessionManager::read_seq_file(const std::string& filepath) {
    try {
        if (!fs::exists(filepath)) {
            return 0;
        }

        std::ifstream in(filepath);
        if (!in.is_open()) {
            return 0;
        }

        uint64_t seq = 0;
        in >> seq;
        return seq;
    } catch (...) {
        return 0;
    }
}

bool SessionManager::write_seq_file(const std::string& filepath, uint64_t seq) {
    try {
        std::ofstream out(filepath);
        if (!out.is_open()) {
            return false;
        }

        out << seq;
        out.close();
        return true;
    } catch (...) {
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