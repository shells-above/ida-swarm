#include "../include/stdio_mcp_server.h"
#include <iostream>
#include <string>

namespace llm_re::mcp {

StdioMCPServer::StdioMCPServer(const std::string& name, const std::string& version)
    : server_name_(name), server_version_(version) {
    std::cerr << "==============================================\n";
    std::cerr << "    " << name << " v" << version << "\n";
    std::cerr << "==============================================\n\n";
}

StdioMCPServer::~StdioMCPServer() {
    stop();
}

void StdioMCPServer::register_tool(const std::string& name,
                                   const std::string& description,
                                   const json& input_schema,
                                   ToolHandler handler) {
    Tool tool;
    tool.name = name;
    tool.description = description;
    tool.input_schema = input_schema;
    tool.handler = handler;
    tools_[name] = tool;
}

void StdioMCPServer::start() {
    while (!should_stop_) {
        try {
            // Read JSON-RPC message from stdin
            std::string line = read_line();
            if (line.empty() && std::cin.eof()) {
                break;  // EOF reached
            }

            // Parse JSON
            json message;
            try {
                message = json::parse(line);
            } catch (const json::exception& e) {
                // Invalid JSON, send parse error
                std::optional<json> no_id;
                write_json(create_error_response(no_id, -32700, "Parse error"));
                continue;
            }

            // Check for batch request (not allowed for certain methods)
            if (is_batch_request(message)) {
                // Process batch - but reject if it contains initialize
                bool has_initialize = false;
                for (const auto& req : message) {
                    if (req.contains("method") && req["method"] == "initialize") {
                        has_initialize = true;
                        break;
                    }
                }

                if (has_initialize) {
                    std::optional<json> no_id;
                    write_json(create_error_response(no_id, -32600,
                        "Invalid Request: initialize cannot be part of a batch"));
                    continue;
                }

                // Process each request in batch
                json responses = json::array();
                for (const auto& req : message) {
                    auto response = process_message(req);
                    if (response.has_value()) {
                        responses.push_back(response.value());
                    }
                }

                if (!responses.empty()) {
                    write_json(responses);
                }
            } else {
                // Process single message
                auto response = process_message(message);
                if (response.has_value()) {
                    write_json(response.value());
                }
            }

        } catch (const std::exception& e) {
            std::cerr << "Server error: " << e.what() << std::endl;
        }
    }
}

void StdioMCPServer::stop() {
    should_stop_ = true;
}

std::optional<json> StdioMCPServer::process_message(const json& message) {
    // Validate JSON-RPC structure
    if (!message.contains("jsonrpc") || message["jsonrpc"] != "2.0") {
        std::optional<json> id;
        if (message.contains("id")) {
            id = message["id"];
        }
        return create_error_response(id, -32600,
            "Invalid Request: Missing or invalid jsonrpc field");
    }

    if (!message.contains("method")) {
        std::optional<json> id;
        if (message.contains("id")) {
            id = message["id"];
        }
        return create_error_response(id, -32600,
            "Invalid Request: Missing method field");
    }

    std::string method = message["method"];
    json params = message.value("params", json::object());

    // Check if this is a notification (no id field)
    bool is_notif = is_notification(message);

    // Get id if present
    std::optional<json> id;
    if (!is_notif) {
        id = message["id"];
    }

    // Handle ping (allowed anytime)
    if (method == "ping") {
        if (is_notif) {
            return std::nullopt;  // No response for notification
        }
        return create_success_response(id.value(), handle_ping());
    }

    // Handle initialize
    if (method == "initialize") {
        if (is_notif) {
            // Initialize must be a request, not notification
            return std::nullopt;
        }

        if (state_ != State::UNINITIALIZED) {
            return create_error_response(id, -32600,
                "Invalid Request: Already initialized");
        }

        state_ = State::INITIALIZING;
        auto result = handle_initialize(params);
        state_ = State::INITIALIZING;  // Wait for initialized notification
        return create_success_response(id.value(), result);
    }

    // Handle initialized notification
    if (method == "notifications/initialized") {
        if (state_ == State::INITIALIZING) {
            handle_initialized_notification();
        }
        return std::nullopt;  // No response for notification
    }

    // All other methods require initialization
    if (state_ != State::INITIALIZED) {
        if (!is_notif) {
            return create_error_response(id, -32002,
                "Server not initialized",
                json{{"note", "Expected notifications/initialized"}});
        }
        return std::nullopt;
    }

    // Route to appropriate handler
    if (method == "tools/list") {
        if (is_notif) {
            return std::nullopt;
        }
        return create_success_response(id.value(), handle_list_tools(params));
    } else if (method == "tools/call") {
        if (is_notif) {
            return std::nullopt;
        }
        return create_success_response(id.value(), handle_call_tool(params));
    } else {
        if (!is_notif) {
            return create_error_response(id, -32601, "Method not found: " + method);
        }
        return std::nullopt;
    }
}

json StdioMCPServer::handle_initialize(const json& params) {
    json result;

    // Protocol version - MUST be 2025-03-26
    result["protocolVersion"] = "2025-03-26";

    // Server info
    json server_info;
    server_info["name"] = server_name_;
    server_info["version"] = server_version_;
    result["serverInfo"] = server_info;

    // Capabilities - simplified format per spec
    json capabilities;
    capabilities["tools"] = json::object();  // Just empty object, not listTools/callTool
    result["capabilities"] = capabilities;

    // Optional instructions
    result["instructions"] = "Use the available tools to interact with the IDA Swarm orchestrator";

    return result;
}

json StdioMCPServer::handle_list_tools(const json& params) {
    json result;
    json tools_array = json::array();

    for (const auto& [name, tool] : tools_) {
        json tool_obj;
        tool_obj["name"] = tool.name;
        tool_obj["description"] = tool.description;
        tool_obj["inputSchema"] = tool.input_schema;
        tools_array.push_back(tool_obj);
    }

    result["tools"] = tools_array;
    return result;
}

json StdioMCPServer::handle_call_tool(const json& params) {
    try {
        // Extract tool name and arguments
        if (!params.contains("name")) {
            json error_content;
            error_content["type"] = "text";
            error_content["text"] = "Missing 'name' parameter";

            json result;
            result["content"] = json::array({error_content});
            result["isError"] = true;
            return result;
        }

        std::string tool_name = params["name"];
        json tool_params = params.value("arguments", json::object());

        // Find the tool
        auto it = tools_.find(tool_name);
        if (it == tools_.end()) {
            json error_content;
            error_content["type"] = "text";
            error_content["text"] = "Tool not found: " + tool_name;

            json result;
            result["content"] = json::array({error_content});
            result["isError"] = true;
            return result;
        }

        // Call the tool handler
        json tool_result = it->second.handler(tool_params);

        // Format result according to MCP spec
        json result;
        json content_array = json::array();

        // Check if tool_result already has the correct format
        if (tool_result.contains("type") && tool_result["type"] == "text") {
            // Single content item
            content_array.push_back(tool_result);
        } else if (tool_result.is_array()) {
            // Already an array of content items
            content_array = tool_result;
        } else {
            // Wrap in text content type
            json content_item;
            content_item["type"] = "text";

            // If tool_result has a "text" field, use it, otherwise dump the whole result
            if (tool_result.contains("text")) {
                content_item["text"] = tool_result["text"];
            } else if (tool_result.is_string()) {
                content_item["text"] = tool_result;
            } else {
                content_item["text"] = tool_result.dump();
            }

            content_array.push_back(content_item);
        }

        result["content"] = content_array;
        return result;

    } catch (const std::exception& e) {
        // Tool execution error
        json error_content;
        error_content["type"] = "text";
        error_content["text"] = std::string("Tool execution error: ") + e.what();

        json result;
        result["content"] = json::array({error_content});
        result["isError"] = true;
        return result;
    }
}

json StdioMCPServer::handle_ping() {
    return json::object();  // Empty object for ping response
}

void StdioMCPServer::handle_initialized_notification() {
    state_ = State::INITIALIZED;
    std::cerr << "Server initialized and ready for tool calls" << std::endl;
}

std::string StdioMCPServer::read_line() {
    std::string line;
    std::getline(std::cin, line);
    return line;
}

void StdioMCPServer::write_json(const json& response) {
    // Only write non-empty responses
    if (!response.is_null() && !response.empty()) {
        std::cout << response.dump() << std::endl;
        std::cout.flush();
    }
}

json StdioMCPServer::create_error_response(const std::optional<json>& id, int code,
                                          const std::string& message,
                                          const std::optional<json>& data) {
    json response;
    response["jsonrpc"] = "2.0";

    json error;
    error["code"] = code;
    error["message"] = message;

    if (data.has_value()) {
        error["data"] = data.value();
    }

    response["error"] = error;

    // Only include id if it was provided (not for parse errors or notifications)
    if (id.has_value()) {
        response["id"] = id.value();
    }

    return response;
}

json StdioMCPServer::create_success_response(const json& id, const json& result) {
    json response;
    response["jsonrpc"] = "2.0";
    response["id"] = id;
    response["result"] = result;
    return response;
}

bool StdioMCPServer::is_notification(const json& message) const {
    return !message.contains("id");
}

bool StdioMCPServer::is_batch_request(const json& message) const {
    return message.is_array();
}

} // namespace llm_re::mcp