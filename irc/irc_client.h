#pragma once

#include <string>
#include <thread>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace llm_re::irc {

// Simple IRC client for agents
class IRCClient {
private:
    std::string nick_;
    std::string server_;
    int port_;
    int socket_fd_;
    bool connected_;
    bool running_;
    
    std::thread recv_thread_;
    std::queue<std::string> message_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    
    // Callback for received messages
    std::function<void(const std::string& channel, const std::string& nick, const std::string& message)> message_callback_;
    
    void receive_loop();
    bool send_raw(const std::string& data);
    
public:
    IRCClient(const std::string& nick, const std::string& server = "127.0.0.1", int port = 6667);
    ~IRCClient();
    
    // Connect to server
    bool connect();
    void disconnect();
    bool is_connected() const { return connected_; }
    
    // IRC operations
    bool join_channel(const std::string& channel);
    bool send_message(const std::string& channel, const std::string& message);
    bool leave_channel(const std::string& channel);
    
    // Set callback for incoming messages
    void set_message_callback(std::function<void(const std::string&, const std::string&, const std::string&)> callback) {
        message_callback_ = callback;
    }
    
    // Get next message from queue (for polling)
    std::string get_next_message(int timeout_ms = 1000);
};

} // namespace llm_re::irc