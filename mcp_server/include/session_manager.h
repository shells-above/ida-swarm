#pragma once

#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <queue>
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

        // IPC communication channels
        int input_fd;   // Write to orchestrator
        int output_fd;  // Read from orchestrator

        // Response queue for async processing
        std::queue<json> response_queue;
        std::mutex queue_mutex;
        std::condition_variable response_cv;

        // Thread for reading orchestrator output
        std::unique_ptr<std::thread> reader_thread;
        bool reader_should_stop = false;
    };

    SessionManager();
    ~SessionManager();

    // Create new session with orchestrator
    std::string create_session(const std::string& binary_path, const std::string& initial_task);

    // Send message to existing session
    json send_message(const std::string& session_id, const std::string& message);

    // Close and cleanup session
    bool close_session(const std::string& session_id);

    // Close all sessions (for cleanup)
    void close_all_sessions();

    // Get session status
    json get_session_status(const std::string& session_id) const;


    // Wait for initial response from orchestrator
    json wait_for_initial_response(const std::string& session_id, int timeout_ms = 600000);

    // Check if a binary already has an active session
    std::string get_active_session_for_binary(const std::string& binary_path) const;

private:
    std::map<std::string, std::unique_ptr<Session>> sessions_;
    std::map<std::string, std::string> binary_to_session_;  // Maps binary paths to session IDs
    mutable std::mutex sessions_mutex_;
    int next_session_num_ = 1;

    // Configuration
    int max_sessions_ = 5;

    // Helper to generate unique session ID
    std::string generate_session_id();

    // Spawn orchestrator process
    int spawn_orchestrator(const std::string& binary_path, const std::string& session_id,
                          int& input_fd, int& output_fd);

    // Send JSON message to orchestrator
    bool send_json_to_orchestrator(int fd, const json& msg);

    // Read JSON response from orchestrator
    json read_json_from_orchestrator(int fd, int timeout_ms = -1);

    // Background thread to read orchestrator output
    void orchestrator_reader_thread(Session* session);

    // Check if orchestrator process is still running
    bool is_orchestrator_alive(int pid) const;

    // Kill orchestrator process
    void kill_orchestrator(int pid);

    // Create named pipes for IPC (just FIFOs, not opened)
    bool create_pipes(const std::string& session_id);

    // Open named pipes for IPC
    bool open_pipes(const std::string& session_id, int& input_fd, int& output_fd);

    // Cleanup pipes
    void cleanup_pipes(const std::string& session_id);
};

} // namespace llm_re::mcp