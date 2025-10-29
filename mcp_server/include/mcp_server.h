#pragma once

#include "session_manager.h"
#include "stdio_mcp_server.h"
#include <memory>
#include <string>
#include <atomic>
#include <thread>

namespace llm_re::mcp {

class MCPServer {
public:
    MCPServer();
    ~MCPServer();

    // Initialize and configure the server
    bool initialize();

    // Start the MCP server (blocking)
    void start();

    // Shutdown the server (graceful, waits for IDA to exit)
    void shutdown();

    // Fast shutdown (force kills all sessions, for external termination)
    void fast_shutdown();

private:
    std::unique_ptr<SessionManager> session_manager_;
    std::unique_ptr<StdioMCPServer> mcp_server_;
    std::atomic<bool> should_shutdown_{false};

    // Register MCP tools
    void register_tools();

    // Tool handlers
    nlohmann::json handle_start_analysis_session(const nlohmann::json& params);
    nlohmann::json handle_send_message(const nlohmann::json& params);
    nlohmann::json handle_close_session(const nlohmann::json& params);
    nlohmann::json handle_wait_for_response(const nlohmann::json& params);


    // Configuration
    void load_configuration();

    // Server configuration
    struct Config {
        int max_sessions = 25;
        std::string ida_path = "/Applications/IDA Professional 9.0.app/Contents/MacOS/ida64";
    } config_;
};

} // namespace llm_re::mcp