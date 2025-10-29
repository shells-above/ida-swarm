#pragma once

#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <queue>
#include <atomic>
#include <nlohmann/json.hpp>

namespace llm_re::mcp {

using json = nlohmann::json;

// Forward declaration
class OrchestratorBridge;

// Manages multiple orchestrator sessions for the MCP server
class SessionManager {
public:
    struct Session {
        std::string session_id;
        std::string binary_path;
        std::chrono::steady_clock::time_point created_at;
        std::chrono::steady_clock::time_point last_activity;
        bool active;
        int orchestrator_pid;

        // Pipe-based communication (blocking I/O, no polling)
        std::string session_dir;           // /tmp/ida_swarm_sessions/{session_id}/
        std::string state_file;            // state.json
        std::string request_pipe;          // request.pipe (MCP → Orchestrator)
        std::string response_pipe;         // response.pipe (Orchestrator → MCP)
        int request_pipe_fd = -1;          // File descriptor for writing requests (kept open)
        int response_pipe_fd = -1;         // File descriptor for reading responses (kept open)

        // Response buffer for wait_for_response()
        std::vector<json> response_buffer;       // Buffered responses from orchestrator

        // Request/response coordination (prevent multiple concurrent requests)
        bool has_pending_request = false;        // True if waiting for orchestrator response
        std::string pending_request_text;        // Text of pending request (for error messages)

        // Synchronization for response buffer and pending state
        std::mutex state_mutex;                  // Protects: response_buffer, has_pending_request, pending_request_text
        std::condition_variable response_cv;     // Signals when new response arrives

        // Thread for reading orchestrator output from pipe (blocking I/O)
        std::unique_ptr<std::thread> reader_thread;
        std::atomic<bool> reader_should_stop{false};

        // Usage tracking to prevent deletion while in use
        std::atomic<int> usage_count{0};
        std::mutex usage_mutex;
        std::condition_variable usage_cv;
    };

    SessionManager();
    ~SessionManager();

    // Configuration
    void set_max_sessions(int max_sessions) { max_sessions_ = max_sessions; }
    int get_max_sessions() const { return max_sessions_; }
    void set_ida_path(const std::string& ida_path) { ida_path_ = ida_path; }
    std::string get_ida_path() const { return ida_path_; }

    // Create new session with orchestrator
    std::string create_session(const std::string& binary_path, const std::string& initial_task);

    // Send message to existing session
    // If wait_for_response is false, returns immediately after sending (for background mode)
    json send_message(const std::string& session_id, const std::string& message, bool wait_for_response = true);

    // Close and cleanup session
    bool close_session(const std::string& session_id);

    // Close all sessions (for cleanup)
    void close_all_sessions();

    // Force kill all sessions immediately (used when externally terminated)
    void force_kill_all_sessions();

    // Get session status
    json get_session_status(const std::string& session_id) const;

    // Check if a binary already has an active session
    std::string get_active_session_for_binary(const std::string& binary_path) const;

    // Wait for response (blocking until response available)
    json wait_for_response(const std::string& session_id, int timeout_ms = -1);

private:
    std::map<std::string, std::unique_ptr<Session>> sessions_;
    std::map<std::string, std::string> binary_to_session_;  // Maps binary paths to session IDs
    mutable std::mutex sessions_mutex_;

    // Configuration
    int max_sessions_ = 25;
    std::string ida_path_ = "/Applications/IDA Professional 9.0.app/Contents/MacOS/ida64";
    std::string sessions_root_dir_ = "/tmp/ida_swarm_sessions";

    // Helper to generate unique session ID from binary path hash
    std::string generate_session_id(const std::string& binary_path);

    // Hash binary path to create deterministic session ID
    std::string hash_binary_path(const std::string& binary_path);

    // Spawn orchestrator process
    int spawn_orchestrator(const std::string& binary_path, const std::string& session_id,
                          Session* session);

    // Send JSON message to orchestrator via pipe (length-prefixed)
    bool send_json_to_orchestrator(Session* session, const json& msg);

    // Background thread to read orchestrator output from pipe (blocking I/O)
    void orchestrator_reader_thread(Session* session);

    // Consume all buffered responses (internal helper)
    // Waits for at least one response if buffer is empty, then returns buffered response
    json consume_all_responses(Session* session_ptr, int timeout_ms = -1);

    // Check if orchestrator process is still running
    bool is_orchestrator_alive(int pid) const;

    // Kill orchestrator process
    void kill_orchestrator(int pid);

    // Create session directory and pipes
    bool create_session_directory(Session* session);

    // Cleanup session directory
    void cleanup_session_directory(const std::string& session_id);

    // Update session state file
    bool update_state_file(Session* session);

    // Detect if binary needs -T flag for Fat Mach-O ARM64 slice selection
    std::string detect_type_flag(const std::string& binary_path);
};

} // namespace llm_re::mcp