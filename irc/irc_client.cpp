#include "irc_client.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <chrono>

namespace llm_re::irc {

IRCClient::IRCClient(const std::string& nick, const std::string& server, int port)
    : nick_(nick), server_(server), port_(port), socket_fd_(-1), 
      connected_(false), running_(false) {}

IRCClient::~IRCClient() {
    disconnect();
}

bool IRCClient::connect() {
    if (connected_) return true;
    
    // Create socket
    socket_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd_ < 0) return false;
    
    // Connect to server
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    
    // Handle localhost specially, otherwise try as IP address
    if (server_ == "localhost") {
        inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr);
    } else {
        // Try as IP address
        if (inet_pton(AF_INET, server_.c_str(), &server_addr.sin_addr) != 1) {
            // Failed to parse as IP, could add hostname resolution here
            close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    }
    
    if (::connect(socket_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // Send NICK command
    std::string nick_cmd = "NICK " + nick_ + "\r\n";
    if (!send_raw(nick_cmd)) {
        close(socket_fd_);
        socket_fd_ = -1;
        return false;
    }
    
    // Send USER command (required by IRC protocol)
    std::string user_cmd = "USER " + nick_ + " 0 * :Agent " + nick_ + "\r\n";
    send_raw(user_cmd);
    
    connected_ = true;
    running_ = true;
    
    // Start receive thread
    recv_thread_ = std::thread(&IRCClient::receive_loop, this);
    
    return true;
}

void IRCClient::disconnect() {
    if (!connected_) return;

    running_ = false;
    connected_ = false;

    // Try to send quit message (non-blocking)
    if (socket_fd_ >= 0) {
        // Ignore errors, we're shutting down anyway
        send_raw("QUIT :Disconnecting\r\n");

        // Close socket to unblock recv() in receive_loop
        close(socket_fd_);
        socket_fd_ = -1;
    }

    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }
}

bool IRCClient::join_channel(const std::string& channel) {
    if (!connected_) return false;
    return send_raw("JOIN " + channel + "\r\n");
}

bool IRCClient::send_message(const std::string& channel, const std::string& message) {
    if (!connected_) return false;
    
    // Escape newlines to prevent message truncation
    std::string escaped_message = message;
    size_t pos = 0;
    while ((pos = escaped_message.find('\n', pos)) != std::string::npos) {
        escaped_message.replace(pos, 1, "\\n");
        pos += 2; // Move past the escaped sequence
    }
    
    // Also escape carriage returns
    pos = 0;
    while ((pos = escaped_message.find('\r', pos)) != std::string::npos) {
        escaped_message.replace(pos, 1, "\\r");
        pos += 2;
    }
    
    return send_raw("PRIVMSG " + channel + " :" + escaped_message + "\r\n");
}

bool IRCClient::leave_channel(const std::string& channel) {
    if (!connected_) return false;
    return send_raw("PART " + channel + "\r\n");
}

bool IRCClient::send_raw(const std::string& data) {
    if (socket_fd_ < 0) return false;
    ssize_t sent = send(socket_fd_, data.c_str(), data.length(), 0);
    return sent == static_cast<ssize_t>(data.length());
}

void IRCClient::receive_loop() {
    char buffer[4096];  // Increased buffer size for larger messages
    std::string line_buffer;
    
    while (running_ && socket_fd_ >= 0) {
        ssize_t bytes = recv(socket_fd_, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        
        buffer[bytes] = '\0';
        line_buffer += buffer;
        
        // Process complete lines
        size_t pos;
        while ((pos = line_buffer.find("\r\n")) != std::string::npos) {
            std::string line = line_buffer.substr(0, pos);
            line_buffer.erase(0, pos + 2);
            
            // Parse IRC message
            if (!line.empty()) {
                // Handle PING (keep-alive)
                if (line.substr(0, 4) == "PING") {
                    send_raw("PONG" + line.substr(4) + "\r\n");
                    continue;
                }
                
                // Parse PRIVMSG for callback
                size_t prefix_end = line.find(' ');
                if (prefix_end != std::string::npos) {
                    std::string prefix = line.substr(0, prefix_end);
                    size_t cmd_end = line.find(' ', prefix_end + 1);
                    if (cmd_end != std::string::npos) {
                        std::string cmd = line.substr(prefix_end + 1, cmd_end - prefix_end - 1);
                        
                        if (cmd == "PRIVMSG") {
                            // Extract channel and message
                            size_t chan_end = line.find(' ', cmd_end + 1);
                            if (chan_end != std::string::npos) {
                                std::string channel = line.substr(cmd_end + 1, chan_end - cmd_end - 1);
                                // Find the colon that marks the start of the message
                                size_t msg_start = line.find(':', chan_end);
                                std::string msg = (msg_start != std::string::npos) ? 
                                    line.substr(msg_start + 1) : line.substr(chan_end + 1);
                                
                                // Extract sender nick from prefix
                                std::string sender;
                                if (prefix[0] == ':') {
                                    size_t nick_end = prefix.find('!');
                                    sender = prefix.substr(1, nick_end - 1);
                                }
                                
                                // Callback
                                if (message_callback_) {
                                    message_callback_(channel, sender, msg);
                                }
                                
                                // Also queue the message
                                {
                                    std::lock_guard<std::mutex> lock(queue_mutex_);
                                    message_queue_.push(channel + "|" + sender + "|" + msg);
                                    queue_cv_.notify_one();
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

std::string IRCClient::get_next_message(int timeout_ms) {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    
    if (queue_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                           [this] { return !message_queue_.empty(); })) {
        std::string msg = message_queue_.front();
        message_queue_.pop();
        return msg;
    }
    
    return "";
}

} // namespace llm_re::irc