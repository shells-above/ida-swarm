#pragma once

#include "../core/common_base.h"
#include <string>
#include <optional>
#include <chrono>

// Forward declarations for libssh2
struct _LIBSSH2_SESSION;
struct _LIBSSH2_CHANNEL;
typedef struct _LIBSSH2_SESSION LIBSSH2_SESSION;
typedef struct _LIBSSH2_CHANNEL LIBSSH2_CHANNEL;

namespace llm_re::orchestrator {

/**
 * Information about a remote iOS device that can be auto-discovered
 *
 * NOTE: Currently only jailbroken iOS devices are supported for remote debugging.
 * This struct stores device identification info fetched via SSH.
 */
struct DeviceInfo {
    std::string udid;               // Unique device identifier
    std::string model;              // Device model (e.g., "iPad13,8")
    std::string ios_version;        // iOS version (e.g., "16.5")
    std::string name;               // Device name (default: Model - Version, editable by user)
    std::chrono::system_clock::time_point last_connected;

    DeviceInfo() = default;
};

/**
 * RAII wrapper for libssh2 SSH session
 * Ensures proper cleanup of SSH resources on destruction
 */
class SSH2SessionGuard {
public:
    SSH2SessionGuard();
    ~SSH2SessionGuard();

    // Disable copy/move
    SSH2SessionGuard(const SSH2SessionGuard&) = delete;
    SSH2SessionGuard& operator=(const SSH2SessionGuard&) = delete;

    // Connect to remote host
    bool connect(const std::string& host, int port, const std::string& user, std::string& error);

    // Execute command and return output
    std::string exec(const std::string& command, std::string& error);

    // Check if session is active
    bool is_active() const { return session_ != nullptr; }

private:
    LIBSSH2_SESSION* session_;
    LIBSSH2_CHANNEL* channel_;
    int socket_;
    bool libssh2_initialized_;
#ifdef _WIN32
    bool wsa_initialized_;
#endif
};

/**
 * Fetches device information from remote devices via SSH
 *
 * This class provides methods to auto-discover device identity by
 * SSHing into the device and running system commands to extract:
 * - Device name/hostname
 * - UDID (unique device identifier)
 * - Model identifier
 * - iOS version
 */
class RemoteDeviceInfoFetcher {
public:
    /**
     * Fetch comprehensive device information via SSH
     *
     * @param host Remote host IP/hostname
     * @param ssh_port SSH port (typically 22)
     * @param ssh_user SSH username (typically "root" for iOS)
     * @param error Output parameter for error details
     * @return DeviceInfo if successful, std::nullopt on failure
     *
     * This method establishes an SSH connection using the system's SSH keys
     * (managed by SSHKeyManager) and runs several commands to gather device info.
     */
    static std::optional<DeviceInfo> fetch_device_info(
        const std::string& host,
        int ssh_port,
        const std::string& ssh_user,
        std::string& error
    );

private:
    /**
     * Execute a single command via SSH and return output
     *
     * @param host Remote host
     * @param ssh_port SSH port
     * @param ssh_user SSH username
     * @param command Command to execute
     * @param error Output parameter for error details
     * @return Command output (stdout) or empty string on failure
     */
    static std::string ssh_exec_command(
        const std::string& host,
        int ssh_port,
        const std::string& ssh_user,
        const std::string& command,
        std::string& error
    );

    /**
     * Parse UDID from ioreg command output
     *
     * @param output Output from ioreg command
     * @return UDID if found, empty string otherwise
     */
    static std::string parse_udid_from_ioreg(const std::string& output);

    /**
     * Trim whitespace from string
     */
    static std::string trim(const std::string& str);
};

} // namespace llm_re::orchestrator
