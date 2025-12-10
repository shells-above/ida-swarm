#pragma once

#include "../core/common_base.h"
#include "remote_device_info.h"
#include <string>
#include <map>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <vector>
#include <chrono>

namespace llm_re {

// Forward declarations
class IRCClient;

namespace orchestrator {
class DatabaseManager;
}
using orchestrator::DatabaseManager;
using orchestrator::DeviceInfo;

/**
 * Connection health status of a remote device
 */
enum class ConnectionHealth {
    HEALTHY,      // Device online and working normally
    ERROR,        // Cannot connect to device
    DISABLED      // Device disabled in configuration
};

/**
 * Represents a remote device in the debugger pool
 */
struct RemoteDevice {
    std::string id;                   // Unique identifier (UDID or user-provided)
    std::string name;                 // Human-readable name (hostname or user-provided)
    std::string host;                 // IP address or hostname
    int ssh_port;                     // SSH port
    std::string ssh_user;             // SSH username
    int debugserver_port;             // Debugserver port
    std::string remote_binary_path;   // Path to binary on device
    bool enabled;                     // Whether device is enabled

    // Auto-discovered information
    std::optional<DeviceInfo> device_info;

    // Runtime state
    bool is_available;                // Device not currently in use
    std::string current_agent_id;     // Agent using this device (empty if available)
    std::chrono::system_clock::time_point session_start_time;  // When current session started
    ConnectionHealth health_status;   // Connection health

    // Platform detection (initialized on first use)
    bool initialized;                 // Has device been initialized?
    std::string signing_tool;         // Code signing tool: empty (no signing), "ldid", or "jtool"

    RemoteDevice()
        : ssh_port(22), debugserver_port(0), enabled(true),
          is_available(true), health_status(ConnectionHealth::HEALTHY),
          initialized(false) {}
};

/**
 * Represents an active LLDB debugging session
 */
struct LLDBSession {
    std::string session_id;        // Unique session identifier
    std::string agent_id;          // Agent that owns this session
    std::string device_id;         // Device this session is using
    int lldb_pid;                  // LLDB process ID
    int pty_master_fd;             // PTY master file descriptor (Unix) or handle (Windows)
    std::string target_binary;     // Binary being debugged
    std::string remote_host;       // Remote debugserver host (if remote)
    int remote_port;               // Remote debugserver port (if remote)
    bool is_remote;                // Whether this is a remote debug session
    int remote_debugserver_pid;    // PID of debugserver process on remote device
    int remote_debugged_pid;       // PID of debugged process on remote device

    LLDBSession() : lldb_pid(-1), pty_master_fd(-1), remote_port(0), is_remote(false),
                    remote_debugserver_pid(-1), remote_debugged_pid(-1) {}
};

/**
 * Manages LLDB debugging sessions with pool-based device allocation
 *
 * PLATFORM SUPPORT: Currently iOS only (jailbroken devices)
 * =========================================================
 * This implementation ONLY supports jailbroken iOS devices because:
 * 1. Uses Apple's 'debugserver' (iOS/macOS specific)
 * 2. Requires iOS code signing tools (ldid/jtool)
 * 3. Uses debugserver-specific connection protocol
 *
 * TODO: Future platform support
 * - Linux:   Would need lldb-server or gdbserver, no code signing
 * - Android: Would need lldb-server for Android, different protocol
 * - macOS:   debugserver works but needs different setup (no jailbreak)
 * - Windows: Would need Windows remote debugging protocol
 *
 * ARCHITECTURE:
 * Multiple agents can debug simultaneously by using different devices from the pool.
 * When all devices are busy, agents wait in a FIFO queue until a device becomes available.
 * The manager handles device allocation, session lifecycle, and crash recovery.
 */
class LLDBSessionManager {
public:
    LLDBSessionManager(const std::string& lldb_path,
                      const std::string& workspace_path,
                      DatabaseManager* db_manager,
                      int irc_port);
    ~LLDBSessionManager();

    // Disable copy/move
    LLDBSessionManager(const LLDBSessionManager&) = delete;
    LLDBSessionManager& operator=(const LLDBSessionManager&) = delete;

    /**
     * Handle start session request from an agent
     *
     * @param agent_id Agent requesting debugging session
     * @param request_id Unique request ID for response correlation
     * @param timeout_ms Maximum time to wait in queue (milliseconds)
     * @return JSON response with session_id on success or error message
     *
     * This method BLOCKS if another agent is currently debugging. The agent
     * will wait in a FIFO queue until it's their turn. When the current debugger
     * releases their session, the next agent in queue is notified. If the timeout
     * expires before reaching the front of the queue, an error is returned.
     */
    json handle_start_session(const std::string& agent_id, const std::string& request_id, int timeout_ms);

    /**
     * Handle send command request
     *
     * @param session_id Session to send command to
     * @param agent_id Agent sending the command (for validation)
     * @param command Raw LLDB command string
     * @param request_id Request ID for response
     * @return JSON response with command output or error
     */
    json handle_send_command(const std::string& session_id,
                            const std::string& agent_id,
                            const std::string& command,
                            const std::string& request_id);

    /**
     * Handle address conversion request
     *
     * @param session_id Session to query for runtime base
     * @param agent_id Agent requesting conversion (for validation)
     * @param ida_address IDA virtual address to convert
     * @param request_id Request ID for response
     * @return JSON response with runtime address or error
     */
    json handle_convert_address(const std::string& session_id,
                                const std::string& agent_id,
                                uint64_t ida_address,
                                const std::string& request_id);

    /**
     * Handle stop session request
     *
     * @param session_id Session to stop
     * @param agent_id Agent stopping the session (for validation)
     * @param request_id Request ID for response
     * @return JSON response with success or error
     */
    json handle_stop_session(const std::string& session_id,
                            const std::string& agent_id,
                            const std::string& request_id);

    /**
     * Cleanup all sessions owned by an agent (called on agent crash)
     *
     * @param agent_id Agent that crashed
     *
     * This terminates the agent's LLDB session, releases the queue lock,
     * and notifies the next agent waiting in queue.
     */
    void cleanup_agent_sessions(const std::string& agent_id);

private:
    std::string lldb_path_;                              // Path to LLDB executable
    std::string workspace_path_;                         // Workspace directory for config
    DatabaseManager* db_manager_;                        // For getting agent binary paths
    int irc_port_;                                       // IRC server port (used to auto-assign debugserver ports)

    // Device pool
    std::vector<RemoteDevice> devices_;                  // Pool of remote debugging devices

    // Session management
    std::map<std::string, LLDBSession> active_sessions_; // session_id -> session
    mutable std::mutex active_sessions_mutex_;           // Protects active_sessions_ map

    // Queue management (global queue for any available device)
    struct QueueEntry {
        std::string agent_id;
        std::string request_id;
        std::shared_ptr<std::condition_variable> cv;     // Per-request condition variable
        bool notified;                                    // Whether this entry has been notified

        QueueEntry() : cv(std::make_shared<std::condition_variable>()), notified(false) {}
    };
    std::queue<QueueEntry> global_queue_;                // FIFO queue waiting for any device
    std::mutex queue_mutex_;                             // Protects queue and device pool

    // Helper methods
    std::string generate_session_id() const;
    json load_lldb_config() const;
    bool validate_lldb_config(const json& config, std::string& error_msg) const;

    // LLDB path detection
    static std::string auto_detect_lldb_path();
    static bool is_valid_lldb_executable(const std::string& path);

    // Device pool management
    RemoteDevice* find_available_device();  // Find first available enabled healthy device
    RemoteDevice* find_device_by_id(const std::string& device_id);
    void discover_and_update_device(RemoteDevice& device);  // Fetch device info and update config
    bool initialize_remote_device(RemoteDevice& device, std::string& error);  // Initialize device platform detection
    void save_lldb_config() const;  // Save current device pool to config file

    // Platform-specific LLDB spawning
    int spawn_lldb_with_pty(RemoteDevice& device, int* pty_master_fd);

    // LLDB I/O
    bool write_to_lldb(int pty_fd, const std::string& command);
    std::string read_from_lldb_until_prompt(int pty_fd, int timeout_ms = 30000);
    std::string read_lldb_until_connect_complete(int pty_fd, int timeout_ms = 30000);
    std::string strip_ansi_codes(const std::string& input) const;

    // Process cleanup
    void terminate_lldb_process(int lldb_pid, int pty_fd);

    // Debugserver lifecycle management
    bool start_remote_debugserver(RemoteDevice& device,
                                  const std::string& binary_path,
                                  int& out_debugserver_pid,
                                  int& out_debugged_pid,
                                  std::string& error);

    bool stop_remote_processes(const std::string& host,
                              int ssh_port,
                              const std::string& ssh_user,
                              int debugserver_pid,
                              int debugged_pid);

    // Address parsing
    std::optional<uint64_t> parse_image_base_from_lldb_output(const std::string& output) const;
    uint64_t get_ida_imagebase() const;

    // Session validation
    bool validate_session_ownership(const std::string& session_id,
                                   const std::string& agent_id,
                                   std::string& error_msg) const;
};

} // namespace llm_re
