#include "remote_device_info.h"
#include "../core/logger.h"
#include "../core/ssh_key_manager.h"
#include <libssh2.h>
#include <sstream>
#include <regex>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <netdb.h>
#endif

namespace llm_re::orchestrator {

// Helper to close socket
static void close_socket(int sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

// SSH2SessionGuard implementation

SSH2SessionGuard::SSH2SessionGuard()
    : session_(nullptr), channel_(nullptr), socket_(-1), libssh2_initialized_(false)
#ifdef _WIN32
    , wsa_initialized_(false)
#endif
{
}

SSH2SessionGuard::~SSH2SessionGuard() {
    // Cleanup in reverse order of initialization
    if (channel_) {
        libssh2_channel_free(channel_);
        channel_ = nullptr;
    }

    if (session_) {
        libssh2_session_disconnect(session_, "Normal shutdown");
        libssh2_session_free(session_);
        session_ = nullptr;
    }

    if (socket_ >= 0) {
        close_socket(socket_);
        socket_ = -1;
    }

    if (libssh2_initialized_) {
        libssh2_exit();
        libssh2_initialized_ = false;
    }

#ifdef _WIN32
    if (wsa_initialized_) {
        WSACleanup();
        wsa_initialized_ = false;
    }
#endif
}

bool SSH2SessionGuard::connect(const std::string& host, int port, const std::string& user, std::string& error) {
#ifdef _WIN32
    // Initialize Winsock
    WSADATA wsadata;
    if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0) {
        error = "WSAStartup failed";
        return false;
    }
    wsa_initialized_ = true;
#endif

    // Create socket
    socket_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_ < 0) {
        error = "Failed to create socket";
        return false;
    }

    // Resolve hostname
    struct hostent* host_entry = gethostbyname(host.c_str());
    if (!host_entry) {
        error = std::format("Failed to resolve hostname: {}", host);
        return false;
    }

    // Connect socket
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    memcpy(&server_addr.sin_addr, host_entry->h_addr_list[0], host_entry->h_length);

    if (::connect(socket_, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        error = std::format("Failed to connect to {}:{}", host, port);
        return false;
    }

    // Initialize libssh2
    if (libssh2_init(0) != 0) {
        error = "Failed to initialize libssh2";
        return false;
    }
    libssh2_initialized_ = true;

    // Create session
    session_ = libssh2_session_init();
    if (!session_) {
        error = "Failed to initialize libssh2 session";
        return false;
    }

    // SSH handshake
    if (libssh2_session_handshake(session_, socket_) != 0) {
        char* err_msg;
        libssh2_session_last_error(session_, &err_msg, nullptr, 0);
        error = std::format("SSH handshake failed: {}", err_msg);
        return false;
    }

    // Authenticate with public key
    std::string private_key = SSHKeyManager::get_private_key_path();
    std::string public_key = SSHKeyManager::get_public_key_path();

    if (libssh2_userauth_publickey_fromfile(
            session_,
            user.c_str(),
            public_key.c_str(),
            private_key.c_str(),
            nullptr) != 0) {
        char* err_msg;
        libssh2_session_last_error(session_, &err_msg, nullptr, 0);
        error = std::format("SSH authentication failed: {}", err_msg);
        return false;
    }

    return true;
}

// Helper function to escape single quotes in commands for shell wrapping
static std::string escape_single_quotes(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c == '\'') {
            result += "'\\''";  // End quote, escaped quote, start quote
        } else {
            result += c;
        }
    }
    return result;
}

std::string SSH2SessionGuard::exec(const std::string& command, std::string& error) {
    if (!session_) {
        error = "Session not connected";
        return "";
    }

    // Wrap command in login shell to source shell initialization files
    // This ensures PATH is properly set up (especially for jailbroken iOS)
    std::string wrapped_command = "exec zsh -l -c '" + escape_single_quotes(command) + "'";

    // Debug: log the actual command being executed
    LOG_INFO("SSH2SessionGuard: Executing command: %s\n", wrapped_command.c_str());

    // Close previous channel if any
    if (channel_) {
        libssh2_channel_free(channel_);
        channel_ = nullptr;
    }

    // Open channel for command execution
    channel_ = libssh2_channel_open_session(session_);
    if (!channel_) {
        char* err_msg;
        libssh2_session_last_error(session_, &err_msg, nullptr, 0);
        error = std::format("Failed to open channel: {}", err_msg);
        return "";
    }

    // Execute wrapped command
    if (libssh2_channel_exec(channel_, wrapped_command.c_str()) != 0) {
        char* err_msg;
        libssh2_session_last_error(session_, &err_msg, nullptr, 0);
        error = std::format("Failed to execute command: {}", err_msg);
        return "";
    }

    // Read output
    std::string output;
    char buffer[1024];
    ssize_t bytes_read;

    while ((bytes_read = libssh2_channel_read(channel_, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        output += buffer;
    }

    // Check for read errors
    if (bytes_read < 0 && bytes_read != LIBSSH2_ERROR_EAGAIN) {
        char* err_msg;
        libssh2_session_last_error(session_, &err_msg, nullptr, 0);
        error = std::format("Failed to read command output: {}", err_msg);
        return "";
    }

    // Wait for command to complete and get exit status
    int exit_status = 0;
    libssh2_channel_wait_closed(channel_);
    exit_status = libssh2_channel_get_exit_status(channel_);

    LOG_INFO("SSH2SessionGuard: Exit status: %d\n", exit_status);

    if (exit_status != 0) {
        error = std::format("Command exited with status: {}", exit_status);
        // Return the output anyway, might contain error message
    }

    return output;
}

std::optional<DeviceInfo> RemoteDeviceInfoFetcher::fetch_device_info(
    const std::string& host,
    int ssh_port,
    const std::string& ssh_user,
    std::string& error
) {
    LOG_INFO("RemoteDeviceInfoFetcher: Fetching device info from %s@%s:%d\n",
             ssh_user.c_str(), host.c_str(), ssh_port);

    DeviceInfo info;
    bool is_ios = false;

    // Try iOS-specific UDID fetch first
    std::string ioreg_cmd = "/usr/sbin/ioreg -rd1 -c IOPlatformExpertDevice | /var/jb/usr/bin/grep IOPlatformUUID | /var/jb/usr/bin/head -1";
    std::string ioreg_output = ssh_exec_command(host, ssh_port, ssh_user, ioreg_cmd, error);

    if (!ioreg_output.empty() && ioreg_output.find("not found") == std::string::npos) {
        // iOS device detected
        is_ios = true;
        info.udid = parse_udid_from_ioreg(ioreg_output);
        if (info.udid.empty()) {
            LOG_INFO("RemoteDeviceInfoFetcher: Could not parse UDID, using hostname-based ID\n");
            info.udid = "device_" + host;
        }
    } else {
        // Not iOS or UDID unavailable - use hostname as ID
        LOG_INFO("RemoteDeviceInfoFetcher: Not iOS device (or UDID unavailable), using hostname-based ID\n");
        std::string hostname_output = ssh_exec_command(host, ssh_port, ssh_user, "hostname", error);
        if (!hostname_output.empty()) {
            info.udid = "device_" + trim(hostname_output);
        } else {
            info.udid = "device_" + host;
        }
    }
    LOG_INFO("RemoteDeviceInfoFetcher: Device ID: %s\n", info.udid.c_str());

    // Try iOS version first, fallback to generic OS detection
    std::string version_output = ssh_exec_command(host, ssh_port, ssh_user, "/var/jb/usr/bin/sw_vers -productVersion", error);
    if (!version_output.empty() && version_output.find("not found") == std::string::npos) {
        info.ios_version = trim(version_output);
        is_ios = true;
    } else {
        // Fallback: Try generic uname for OS info
        std::string uname_output = ssh_exec_command(host, ssh_port, ssh_user, "uname -sr", error);
        if (!uname_output.empty()) {
            info.ios_version = trim(uname_output);  // Store OS info in ios_version field (generic OS field)
        } else {
            info.ios_version = "Unknown";
        }
    }
    LOG_INFO("RemoteDeviceInfoFetcher: OS Version: %s\n", info.ios_version.c_str());

    // Fetch architecture/model - try jailbreak path first, fallback to PATH
    std::string model_output = ssh_exec_command(host, ssh_port, ssh_user, "/var/jb/usr/bin/uname -m", error);
    if (model_output.empty() || model_output.find("not found") != std::string::npos) {
        // Fallback to standard uname
        model_output = ssh_exec_command(host, ssh_port, ssh_user, "uname -m", error);
    }
    if (!model_output.empty()) {
        info.model = trim(model_output);
    } else {
        info.model = "Unknown";
    }
    LOG_INFO("RemoteDeviceInfoFetcher: Architecture: %s\n", info.model.c_str());

    // Generate device name (platform-agnostic)
    if (is_ios) {
        info.name = info.model + " - iOS " + info.ios_version;
    } else {
        info.name = info.model + " - " + info.ios_version;
    }
    LOG_INFO("RemoteDeviceInfoFetcher: Generated name: %s\n", info.name.c_str());

    // Set last connected timestamp
    info.last_connected = std::chrono::system_clock::now();

    return info;
}

std::string RemoteDeviceInfoFetcher::ssh_exec_command(
    const std::string& host,
    int ssh_port,
    const std::string& ssh_user,
    const std::string& command,
    std::string& error
) {
    // Use RAII guard for automatic cleanup
    SSH2SessionGuard session;

    if (!session.connect(host, ssh_port, ssh_user, error)) {
        return "";
    }

    return session.exec(command, error);
}

std::string RemoteDeviceInfoFetcher::parse_udid_from_ioreg(const std::string& output) {
    // Output format examples:
    // "IOPlatformUUID" = "00008020-001234567890001E"
    // "IOPlatformSerialNumber" = <"C123456789">

    // Try to match UUID format first (most reliable)
    std::regex uuid_regex("\"([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4,12}-[0-9A-Fa-f]{4,16}-[0-9A-Fa-f]{4,16}-[0-9A-Fa-f]{4,12})\"");
    std::smatch match;
    if (std::regex_search(output, match, uuid_regex)) {
        return match[1].str();
    }

    // Try to extract any quoted alphanumeric string
    std::regex generic_regex("\"([A-Za-z0-9-]+)\"");
    if (std::regex_search(output, match, generic_regex)) {
        std::string result = match[1].str();
        // Only return if it looks like a UDID (has some length and variety)
        if (result.length() >= 8) {
            return result;
        }
    }

    return "";
}

std::string RemoteDeviceInfoFetcher::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

} // namespace llm_re::orchestrator
