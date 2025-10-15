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

        // File-based communication
        std::string session_dir;           // /tmp/ida_swarm_sessions/{session_id}/
        std::string state_file;            // state.json
        std::string request_file;          // request.json
        std::string response_file;         // response.json
        std::string request_seq_file;      // request_seq
        std::string response_seq_file;     // response_seq

        // Sequence tracking for communication
        uint64_t request_seq = 0;
        uint64_t response_seq = 0;

        // Response queue for async processing
        std::queue<json> response_queue;
        std::mutex queue_mutex;
        std::condition_variable response_cv;

        // Thread for polling orchestrator output
        std::unique_ptr<std::thread> reader_thread;
        bool reader_should_stop = false;

        // Usage tracking to prevent deletion while in use
        std::atomic<int> usage_count{0};
        std::mutex usage_mutex;
        std::condition_variable usage_cv;

        // Background execution support
        bool has_pending_message = false;
        std::string pending_message_text;
        std::vector<json> accumulated_responses;  // All responses (for background mode)
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


    // Wait for initial response from orchestrator
    json wait_for_initial_response(const std::string& session_id, int timeout_ms = 600000);

    // Check if a binary already has an active session
    std::string get_active_session_for_binary(const std::string& binary_path) const;

    // Background execution support
    // Get accumulated messages (non-blocking, returns immediately)
    json get_session_messages(const std::string& session_id, int max_messages = -1);

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

    // Send JSON message to orchestrator via file
    bool send_json_to_orchestrator(Session* session, const json& msg);

    // Read JSON response from orchestrator via file
    json read_json_from_orchestrator(Session* session, int timeout_ms = -1);

    // Background thread to poll orchestrator output
    void orchestrator_reader_thread(Session* session);

    // Check if orchestrator process is still running
    bool is_orchestrator_alive(int pid) const;

    // Kill orchestrator process
    void kill_orchestrator(int pid);

    // Create session directory and files
    bool create_session_directory(Session* session);

    // Cleanup session directory
    void cleanup_session_directory(const std::string& session_id);

    // Update session state file
    bool update_state_file(Session* session);

    // Read sequence number from file
    uint64_t read_seq_file(const std::string& filepath);

    // Write sequence number to file
    bool write_seq_file(const std::string& filepath, uint64_t seq);

    // Detect if binary needs -T flag for Fat Mach-O ARM64 slice selection
    std::string detect_type_flag(const std::string& binary_path);
};

} // namespace llm_re::mcp