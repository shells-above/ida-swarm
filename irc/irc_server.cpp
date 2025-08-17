#include "irc_server.h"
#include <sstream>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <ctime>

namespace llm_re::irc {

// Message implementation
std::string Message::serialize() const {
    std::stringstream ss;
    if (!prefix.empty()) {
        ss << ":" << prefix << " ";
    }
    ss << command;
    for (size_t i = 0; i < params.size(); ++i) {
        ss << " ";
        if (i == params.size() - 1 && params[i].find(' ') != std::string::npos) {
            ss << ":" << params[i];
        } else {
            ss << params[i];
        }
    }
    ss << "\r\n";
    return ss.str();
}

Message Message::parse(const std::string& line) {
    Message msg;
    std::istringstream iss(line);
    std::string token;
    
    // Check for prefix
    if (line[0] == ':') {
        iss >> msg.prefix;
        msg.prefix = msg.prefix.substr(1); // Remove leading :
    }
    
    // Get command
    iss >> msg.command;
    
    // Get parameters
    while (iss >> token) {
        if (token[0] == ':') {
            // Trailing parameter - get rest of line
            std::string trailing;
            std::getline(iss, trailing);
            msg.params.push_back(token.substr(1) + trailing);
            break;
        } else {
            msg.params.push_back(token);
        }
    }
    
    return msg;
}

// Channel implementation
void Channel::add_client(int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.insert(client_fd);
}

void Channel::remove_client(int client_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.erase(client_fd);
}

void Channel::broadcast(const Message& msg, int sender_fd) {
    std::lock_guard<std::mutex> lock(mutex_);
    history_.push_back(msg);
    
    std::string data = msg.serialize();
    for (int fd : clients_) {
        if (fd != sender_fd) {  // Don't echo back to sender
            send(fd, data.c_str(), data.length(), 0);
        }
    }
}

std::vector<Message> Channel::get_history() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return history_;
}

bool Channel::has_clients() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !clients_.empty();
}

std::set<int> Channel::get_clients() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clients_;
}

// IRCServer implementation
IRCServer::IRCServer(int port, const std::string& binary_name) 
    : port_(port), server_fd_(-1), running_(false), db_(nullptr), binary_name_(binary_name) {}

IRCServer::~IRCServer() {
    stop();
    if (db_) {
        sqlite3_close(db_);
    }
}

void IRCServer::init_database() {
    std::string db_path = "/tmp/ida_swarm_workspace/" + binary_name_ + "/irc_deliberation.db";
    sqlite3_open(db_path.c_str(), &db_);
    
    const char* create_table = R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP,
            channel TEXT NOT NULL,
            nick TEXT NOT NULL,
            message TEXT NOT NULL
        );
        CREATE INDEX IF NOT EXISTS idx_channel ON messages(channel);
    )";
    
    char* err_msg = nullptr;
    sqlite3_exec(db_, create_table, nullptr, nullptr, &err_msg);
    if (err_msg) {
        sqlite3_free(err_msg);
    }
}

bool IRCServer::start() {
    if (running_) return false;
    
    // Initialize database
    init_database();
    
    // Create socket
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) return false;
    
    // Allow socket reuse
    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);
    
    if (bind(server_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(server_fd_);
        return false;
    }
    
    // Listen
    if (listen(server_fd_, 10) < 0) {
        close(server_fd_);
        return false;
    }
    
    running_ = true;
    accept_thread_ = std::thread(&IRCServer::accept_loop, this);
    
    std::cout << "IRC Server started on port " << port_ << std::endl;
    return true;
}

void IRCServer::stop() {
    if (!running_) return;
    
    running_ = false;
    close(server_fd_);
    
    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    
    // Close all client connections
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [fd, nick] : client_nicks_) {
        close(fd);
    }
    client_nicks_.clear();
    channels_.clear();
}

void IRCServer::accept_loop() {
    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        int client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;
        
        std::thread(&IRCServer::handle_client, this, client_fd).detach();
    }
}

void IRCServer::handle_client(int client_fd) {
    char buffer[4096];  // Increased buffer size for larger messages
    std::string line_buffer;
    
    while (running_) {
        ssize_t bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes <= 0) break;
        
        buffer[bytes] = '\0';
        line_buffer += buffer;
        
        // Process complete lines
        size_t pos;
        while ((pos = line_buffer.find("\r\n")) != std::string::npos) {
            std::string line = line_buffer.substr(0, pos);
            line_buffer.erase(0, pos + 2);
            
            if (!line.empty()) {
                Message msg = Message::parse(line);
                process_message(client_fd, msg);
            }
        }
    }
    
    // Client disconnected - remove from all channels and handle agent leave
    std::lock_guard<std::mutex> lock(mutex_);
    
    // If this was an agent, handle its departure
    handle_agent_leave(client_fd);
    
    for (auto& [name, channel] : channels_) {
        channel->remove_client(client_fd);
    }
    client_nicks_.erase(client_fd);
    close(client_fd);
}

void IRCServer::process_message(int client_fd, const Message& msg) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (msg.command == "NICK" && !msg.params.empty()) {
        // Set nickname
        client_nicks_[client_fd] = msg.params[0];
        
        // Send welcome
        Message welcome;
        welcome.prefix = "server";
        welcome.command = "001";
        welcome.params = {msg.params[0], "Welcome to the deliberation server"};
        std::string data = welcome.serialize();
        send(client_fd, data.c_str(), data.length(), 0);
        
    } else if (msg.command == "JOIN" && !msg.params.empty()) {
        // Join channel
        std::string channel_name = msg.params[0];
        if (channels_.find(channel_name) == channels_.end()) {
            channels_[channel_name] = std::make_unique<Channel>(channel_name);
        }
        
        channels_[channel_name]->add_client(client_fd);
        
        // Notify channel of join
        Message join_msg;
        join_msg.prefix = client_nicks_[client_fd];
        join_msg.command = "JOIN";
        join_msg.params = {channel_name};
        channels_[channel_name]->broadcast(join_msg);
        
        // If joining #agents channel and this is an agent, just track it
        if (channel_name == "#agents" && client_nicks_[client_fd].find("agent_") == 0) {
            // Just track the agent - don't broadcast yet
            std::string agent_id = client_nicks_[client_fd];
            
            // Create placeholder entry - will be updated when task is announced
            AgentInfo info;
            info.agent_id = agent_id;
            info.task = "";
            info.last_seen = std::chrono::steady_clock::now();
            info.client_fd = client_fd;
            active_agents_[agent_id] = info;
        }
        
        // Send channel history to new client
        for (const auto& hist_msg : channels_[channel_name]->get_history()) {
            std::string data = hist_msg.serialize();
            send(client_fd, data.c_str(), data.length(), 0);
        }
        
    } else if (msg.command == "PRIVMSG" && msg.params.size() >= 2) {
        // Send message to channel
        std::string channel_name = msg.params[0];
        std::string text = msg.params[1];
        
        // Unescape newlines and carriage returns
        std::string unescaped_text = text;
        size_t pos = 0;
        while ((pos = unescaped_text.find("\\n", pos)) != std::string::npos) {
            unescaped_text.replace(pos, 2, "\n");
            pos += 1;
        }
        pos = 0;
        while ((pos = unescaped_text.find("\\r", pos)) != std::string::npos) {
            unescaped_text.replace(pos, 2, "\r");
            pos += 1;
        }
        
        // Handle task announcement from agents
        if (text.find("MY_TASK: ") == 0) {
            std::string task = text.substr(9);
            std::string agent_id = client_nicks_[client_fd];
            
            // Update agent's task and broadcast join
            auto it = active_agents_.find(agent_id);
            if (it != active_agents_.end()) {
                bool is_new_agent = it->second.task.empty();
                it->second.task = task;
                
                // Only broadcast join if this is the first task announcement
                if (is_new_agent) {
                    broadcast_agent_join(agent_id);
                }
            }
            return; // Don't broadcast the MY_TASK message itself
        }
        
        if (channels_.find(channel_name) != channels_.end()) {
            Message privmsg;
            privmsg.prefix = client_nicks_[client_fd];
            privmsg.command = "PRIVMSG";
            // Keep escaped version for broadcasting to maintain protocol compliance
            privmsg.params = {channel_name, text};
            channels_[channel_name]->broadcast(privmsg);
            
            // Log unescaped version to database for proper storage
            log_to_db(channel_name, client_nicks_[client_fd], unescaped_text);
        }
        
    } else if (msg.command == "PART" && !msg.params.empty()) {
        // Leave channel
        std::string channel_name = msg.params[0];
        if (channels_.find(channel_name) != channels_.end()) {
            channels_[channel_name]->remove_client(client_fd);
            
            // Notify channel
            Message part_msg;
            part_msg.prefix = client_nicks_[client_fd];
            part_msg.command = "PART";
            part_msg.params = {channel_name};
            channels_[channel_name]->broadcast(part_msg);
        }
    }
}

void IRCServer::log_to_db(const std::string& channel, const std::string& nick, const std::string& message) {
    if (!db_) return;
    
    const char* sql = "INSERT INTO messages (channel, nick, message) VALUES (?, ?, ?)";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, channel.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, nick.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, message.c_str(), -1, SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<Message> IRCServer::get_channel_history(const std::string& channel_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = channels_.find(channel_name);
    if (it != channels_.end()) {
        return it->second->get_history();
    }
    
    // If channel doesn't exist, try to load from database
    std::vector<Message> history;
    if (db_) {
        const char* sql = "SELECT nick, message FROM messages WHERE channel = ? ORDER BY timestamp";
        sqlite3_stmt* stmt;
        
        if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(stmt, 1, channel_name.c_str(), -1, SQLITE_STATIC);
            
            while (sqlite3_step(stmt) == SQLITE_ROW) {
                Message msg;
                msg.prefix = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                msg.command = "PRIVMSG";
                msg.params = {channel_name, reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))};
                history.push_back(msg);
            }
            sqlite3_finalize(stmt);
        }
    }
    
    return history;
}

std::vector<std::string> IRCServer::list_channels() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [name, channel] : channels_) {
        names.push_back(name);
    }
    return names;
}

void IRCServer::broadcast_agent_join(const std::string& agent_id) {
    // Get agent's task
    std::string task = "";
    auto it = active_agents_.find(agent_id);
    if (it != active_agents_.end()) {
        task = it->second.task;
    }
    
    // Only broadcast if we have a task
    if (!task.empty() && channels_.find("#agents") != channels_.end()) {
        Message system_msg;
        system_msg.prefix = "SYSTEM";
        system_msg.command = "PRIVMSG";
        system_msg.params = {"#agents", "AGENT_JOIN: " + agent_id + "|" + task};
        channels_["#agents"]->broadcast(system_msg);
    }
}

void IRCServer::handle_agent_leave(int client_fd) {
    // Find and remove agent by client_fd
    for (auto it = active_agents_.begin(); it != active_agents_.end(); ++it) {
        if (it->second.client_fd == client_fd) {
            std::string agent_id = it->first;
            active_agents_.erase(it);
            
            // Broadcast SYSTEM message about agent departure
            if (channels_.find("#agents") != channels_.end()) {
                Message leave_msg;
                leave_msg.prefix = "SYSTEM";
                leave_msg.command = "PRIVMSG";
                leave_msg.params = {"#agents", "AGENT_LEAVE: " + agent_id};
                channels_["#agents"]->broadcast(leave_msg);
            }
            
            printf("IRC Server: Agent %s left\n", agent_id.c_str());
            break;
        }
    }
}
} // namespace llm_re::irc