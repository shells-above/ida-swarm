#pragma once

#include "../core/common_base.h"
#include <string>

namespace llm_re::orchestrator {

/**
 * Configuration for remote debugging connection
 */
struct RemoteConfig {
    std::string host;              // Remote host IP/hostname
    int ssh_port;                  // SSH port (typically 22)
    std::string ssh_user;          // SSH username (e.g., "root" or "mobile". root recommended so you can update .app bundles in place)
    int debugserver_port;          // debugserver port

    RemoteConfig() : ssh_port(22), debugserver_port(0) {}
};

/**
 * Result of connectivity validation
 */
struct ValidationResult {
    bool ssh_reachable;           // SSH connection successful
    std::string error_message;    // Error details if validation failed

    ValidationResult() : ssh_reachable(false) {}

    bool is_valid() const {
        return ssh_reachable;
    }
};

/**
 * Manages remote file synchronization and connectivity validation for LLDB debugging
 *
 * Provides SSH/SFTP operations for syncing agent binaries to remote iOS devices
 * and validating connectivity before debugging attempts.
 *
 * NOTE: Currently only jailbroken iOS devices are supported for remote debugging.
 * The sync functionality itself is platform-agnostic (SSH/SFTP), but the overall
 * debugging workflow requires iOS-specific components (debugserver, code signing).
 */
class RemoteSyncManager {
public:
    /**
     * Validate remote connectivity (SSH + debugserver)
     *
     * @param config Remote configuration
     * @return Validation result with status and error details
     */
    static ValidationResult validate_connectivity(const RemoteConfig& config);

    /**
     * Sync local binary to remote device via SFTP
     *
     * @param local_path Path to agent's patched binary
     * @param remote_path Destination path on remote device
     * @param config Remote configuration
     * @param error_msg Output parameter for error details
     * @return true if sync succeeded, false otherwise
     */
    static bool sync_binary(
        const std::string& local_path,
        const std::string& remote_path,
        const RemoteConfig& config,
        std::string& error_msg
    );

private:
    /**
     * Test SSH connection
     *
     * @param config Remote configuration
     * @param error Output parameter for error details
     * @return true if SSH connection succeeded
     */
    static bool test_ssh_connection(const RemoteConfig& config, std::string& error);

    /**
     * Upload file via SFTP using libssh2
     *
     * @param local_path Source file
     * @param remote_path Destination path
     * @param config Remote configuration
     * @param error Output parameter for error details
     * @return true if upload succeeded
     */
    static bool sftp_upload(
        const std::string& local_path,
        const std::string& remote_path,
        const RemoteConfig& config,
        std::string& error
    );
};

} // namespace llm_re::orchestrator
