#include "remote_sync_manager.h"
#include "../core/logger.h"
#include "../core/ssh_key_manager.h"
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <fstream>
#include <filesystem>
#include <format>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <errno.h>
#endif

namespace llm_re::orchestrator {

// RAII wrapper for libssh2 session
class SSH2Session {
public:
    SSH2Session() {
#ifdef _WIN32
        WSADATA wsadata;
        WSAStartup(MAKEWORD(2, 0), &wsadata);
#endif
        libssh2_init(0);
        session_ = libssh2_session_init();
    }

    ~SSH2Session() {
        if (session_) {
            libssh2_session_disconnect(session_, "Normal shutdown");
            libssh2_session_free(session_);
        }
        libssh2_exit();
#ifdef _WIN32
        WSACleanup();
#endif
    }

    LIBSSH2_SESSION* get() { return session_; }
    operator bool() const { return session_ != nullptr; }

private:
    LIBSSH2_SESSION* session_ = nullptr;
};

// Helper to close socket properly
static void close_socket(int sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

ValidationResult RemoteSyncManager::validate_connectivity(const RemoteConfig& config) {
    ValidationResult result;

    LOG_INFO("RemoteSyncManager: Validating connectivity to %s\n", config.host.c_str());

    // Test SSH connection only - debugserver is started on-demand per session
    std::string ssh_error;
    result.ssh_reachable = test_ssh_connection(config, ssh_error);

    if (!result.ssh_reachable) {
        result.error_message = "SSH connection failed: " + ssh_error;
        LOG_INFO("RemoteSyncManager: %s\n", result.error_message.c_str());
    } else {
        LOG_INFO("RemoteSyncManager: SSH connection successful\n");
    }

    return result;
}

bool RemoteSyncManager::sync_binary(
    const std::string& local_path,
    const std::string& remote_path,
    const RemoteConfig& config,
    std::string& error_msg
) {
    LOG_INFO("RemoteSyncManager: Syncing %s to %s@%s:%s\n",
             local_path.c_str(), config.ssh_user.c_str(),
             config.host.c_str(), remote_path.c_str());

    // Verify local file exists
    if (!std::filesystem::exists(local_path)) {
        error_msg = "Local binary not found: " + local_path;
        return false;
    }

    // Upload via SFTP
    return sftp_upload(local_path, remote_path, config, error_msg);
}

bool RemoteSyncManager::test_ssh_connection(const RemoteConfig& config, std::string& error) {
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        error = "Failed to create socket";
        return false;
    }

    // Resolve hostname
    struct hostent* host = gethostbyname(config.host.c_str());
    if (!host) {
        error = std::format("Failed to resolve hostname: {}", config.host);
        close_socket(sock);
        return false;
    }

    // Connect to SSH port
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(config.ssh_port);
    memcpy(&sin.sin_addr, host->h_addr, host->h_length);

    if (connect(sock, (struct sockaddr*)(&sin), sizeof(sin)) != 0) {
        error = std::format("Failed to connect to {}:{}", config.host, config.ssh_port);
        close_socket(sock);
        return false;
    }

    // Initialize libssh2 session
    SSH2Session ssh_session;
    if (!ssh_session) {
        error = "Failed to initialize libssh2 session";
        close_socket(sock);
        return false;
    }

    // Start SSH handshake
    if (libssh2_session_handshake(ssh_session.get(), sock) != 0) {
        char* err_msg;
        libssh2_session_last_error(ssh_session.get(), &err_msg, nullptr, 0);
        error = std::format("SSH handshake failed: {}", err_msg);
        close_socket(sock);
        return false;
    }

    // Try public key authentication
    std::string private_key = SSHKeyManager::get_private_key_path();
    std::string public_key = SSHKeyManager::get_public_key_path();

    int auth_result = libssh2_userauth_publickey_fromfile(
        ssh_session.get(),
        config.ssh_user.c_str(),
        public_key.c_str(),
        private_key.c_str(),
        nullptr  // No passphrase
    );

    close_socket(sock);

    if (auth_result != 0) {
        char* err_msg;
        libssh2_session_last_error(ssh_session.get(), &err_msg, nullptr, 0);
        error = std::format("SSH authentication failed: {}. "
                          "Have you copied the public key to the remote device's authorized_keys?",
                          err_msg);
        return false;
    }

    return true;
}

bool RemoteSyncManager::sftp_upload(
    const std::string& local_path,
    const std::string& remote_path,
    const RemoteConfig& config,
    std::string& error
) {
    // Create socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        error = "Failed to create socket";
        return false;
    }

    // Resolve hostname
    struct hostent* host = gethostbyname(config.host.c_str());
    if (!host) {
        error = std::format("Failed to resolve hostname: {}", config.host);
        close_socket(sock);
        return false;
    }

    // Connect to SSH port
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(config.ssh_port);
    memcpy(&sin.sin_addr, host->h_addr, host->h_length);

    if (connect(sock, (struct sockaddr*)(&sin), sizeof(sin)) != 0) {
        error = std::format("Failed to connect to {}:{}", config.host, config.ssh_port);
        close_socket(sock);
        return false;
    }

    // Initialize SSH session
    SSH2Session ssh_session;
    if (!ssh_session) {
        error = "Failed to initialize SSH session";
        close_socket(sock);
        return false;
    }

    // SSH handshake
    if (libssh2_session_handshake(ssh_session.get(), sock) != 0) {
        char* err_msg;
        libssh2_session_last_error(ssh_session.get(), &err_msg, nullptr, 0);
        error = std::format("SSH handshake failed: {}", err_msg);
        close_socket(sock);
        return false;
    }

    // Authenticate
    std::string private_key = SSHKeyManager::get_private_key_path();
    std::string public_key = SSHKeyManager::get_public_key_path();

    if (libssh2_userauth_publickey_fromfile(
            ssh_session.get(),
            config.ssh_user.c_str(),
            public_key.c_str(),
            private_key.c_str(),
            nullptr) != 0) {
        char* err_msg;
        libssh2_session_last_error(ssh_session.get(), &err_msg, nullptr, 0);
        error = std::format("SSH authentication failed: {}", err_msg);
        close_socket(sock);
        return false;
    }

    // Initialize SFTP
    LIBSSH2_SFTP* sftp = libssh2_sftp_init(ssh_session.get());
    if (!sftp) {
        char* err_msg;
        libssh2_session_last_error(ssh_session.get(), &err_msg, nullptr, 0);
        error = std::format("SFTP initialization failed: {}", err_msg);
        close_socket(sock);
        return false;
    }

    // Open remote file for writing
    LIBSSH2_SFTP_HANDLE* sftp_handle = libssh2_sftp_open(
        sftp,
        remote_path.c_str(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR | LIBSSH2_SFTP_S_IXUSR |  // User: rwx
        LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IXGRP |                          // Group: r-x
        LIBSSH2_SFTP_S_IROTH | LIBSSH2_SFTP_S_IXOTH                            // Other: r-x (0755)
    );

    if (!sftp_handle) {
        char* err_msg;
        libssh2_session_last_error(ssh_session.get(), &err_msg, nullptr, 0);
        error = std::format("Failed to open remote file: {}", err_msg);
        libssh2_sftp_shutdown(sftp);
        close_socket(sock);
        return false;
    }

    // Open local file
    std::ifstream local_file(local_path, std::ios::binary);
    if (!local_file) {
        error = "Failed to open local file";
        libssh2_sftp_close(sftp_handle);
        libssh2_sftp_shutdown(sftp);
        close_socket(sock);
        return false;
    }

    // Upload file in chunks
    char buffer[4096];
    size_t total_written = 0;

    while (local_file.read(buffer, sizeof(buffer)) || local_file.gcount() > 0) {
        size_t bytes_read = local_file.gcount();
        size_t bytes_sent = 0;

        // Keep writing until all bytes from this read are sent
        // (libssh2_sftp_write can return fewer bytes than requested)
        while (bytes_sent < bytes_read) {
            ssize_t bytes_written = libssh2_sftp_write(sftp_handle, buffer + bytes_sent, bytes_read - bytes_sent);

            if (bytes_written < 0) {
                error = std::format("SFTP write failed at offset {}", total_written + bytes_sent);
                libssh2_sftp_close(sftp_handle);
                libssh2_sftp_shutdown(sftp);
                close_socket(sock);
                return false;
            }

            bytes_sent += bytes_written;
        }

        total_written += bytes_sent;
    }

    LOG_INFO("RemoteSyncManager: Uploaded %zu bytes to %s\n", total_written, remote_path.c_str());

    // Cleanup
    libssh2_sftp_close(sftp_handle);
    libssh2_sftp_shutdown(sftp);
    close_socket(sock);

    return true;
}

} // namespace llm_re::orchestrator
