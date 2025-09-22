#pragma once

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <thread>
#include <mutex>
#include <sqlite3.h>
#include <sys/socket.h>
#include <netinet/in.h>

namespace llm_re::irc {

// Simple IRC message
struct Message {
    std::string prefix;     // :nick!user@host
    std::string command;    // JOIN, PRIVMSG, etc
    std::vector<std::string> params;
    
    std::string serialize() const;
    static Message parse(const std::string& line);
};

// IRC channel with history
class Channel {
private:
    std::string name_;
    std::set<int> clients_;  // client socket fds
    std::vector<Message> history_;
    mutable std::mutex mutex_;
    
public:
    explicit Channel(const std::string& name) : name_(name) {}
    
    void add_client(int client_fd);
    void remove_client(int client_fd);
    void broadcast(const Message& msg, int sender_fd = -1);
    std::vector<Message> get_history() const;
    bool has_clients() const;
    std::set<int> get_clients() const;
};

// Simple IRC server
class IRCServer {
private:
    int port_;
    int server_fd_;
    bool running_;
    std::thread accept_thread_;
    std::map<int, std::string> client_nicks_;  // fd -> nickname
    std::map<std::string, std::unique_ptr<Channel>> channels_;
    sqlite3* db_;
    std::string binary_name_;
    mutable std::mutex mutex_;
    
    void accept_loop();
    void handle_client(int client_fd);
    void process_message(int client_fd, const Message& msg);
    void log_to_db(const std::string& channel, const std::string& nick, const std::string& message);
    void init_database();

public:
    IRCServer(int port, const std::string& binary_name);
    ~IRCServer();
    
    // Test if a port is available for binding
    static bool is_port_available(int port);
    
    bool start();
    void stop();
    bool is_running() const { return running_; }
    
    // Get channel history for snapshot/resume
    std::vector<Message> get_channel_history(const std::string& channel_name) const;
    std::vector<std::string> list_channels() const;
};

} // namespace llm_re::irc