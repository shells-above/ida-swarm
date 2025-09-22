#pragma once

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <optional>
#include <nlohmann/json.hpp>

namespace llm_re::mcp {

using json = nlohmann::json;

// Simple stdio-based MCP server implementation
class StdioMCPServer {
public:
    using ToolHandler = std::function<json(const json& params)>;

    struct Tool {
        std::string name;
        std::string description;
        json input_schema;
        ToolHandler handler;
    };

    // Server state machine
    enum class State {
        UNINITIALIZED,
        INITIALIZING,
        INITIALIZED
    };

    StdioMCPServer(const std::string& name, const std::string& version);
    ~StdioMCPServer();

    // Register a tool
    void register_tool(const std::string& name,
                      const std::string& description,
                      const json& input_schema,
                      ToolHandler handler);

    // Start the server (blocking)
    void start();

    // Stop the server
    void stop();

private:
    std::string server_name_;
    std::string server_version_;
    std::map<std::string, Tool> tools_;
    std::atomic<bool> should_stop_{false};
    State state_{State::UNINITIALIZED};

    // Process a JSON-RPC message - returns optional response
    std::optional<json> process_message(const json& message);

    // Handle specific methods
    json handle_initialize(const json& params);
    json handle_list_tools(const json& params);
    json handle_call_tool(const json& params);
    json handle_ping();
    void handle_initialized_notification();

    // Read a line from stdin
    std::string read_line();

    // Write JSON to stdout
    void write_json(const json& response);

    // Create error response - id is optional for proper handling
    json create_error_response(const std::optional<json>& id, int code,
                              const std::string& message,
                              const std::optional<json>& data = std::nullopt);

    // Create success response
    json create_success_response(const json& id, const json& result);

    // Validation helpers
    bool is_notification(const json& message) const;
    bool is_batch_request(const json& message) const;
};

} // namespace llm_re::mcp