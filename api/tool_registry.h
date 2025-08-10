//
// Tool framework for the API
// Provides portable tool interfaces and registry for managing tools
//

#ifndef API_TOOL_REGISTRY_H
#define API_TOOL_REGISTRY_H

#include "message_types.h"
#include <string>
#include <memory>
#include <optional>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <nlohmann/json.hpp>

namespace llm_re::tools {

using json = nlohmann::json;

// Base tool result type - generic success/failure wrapper
struct ToolResult {
    bool wasSuccess = true;
    std::optional<std::string> error;
    json data;

    json to_json() const {
        json j;
        j["success"] = wasSuccess;
        if (error) {
            j["error"] = *error;
        }
        // Merge data fields into top level
        for (auto& [key, value] : data.items()) {
            j[key] = value;
        }
        return j;
    }

    static ToolResult success(json data) {
        return {true, std::nullopt, std::move(data)};
    }

    static ToolResult failure(const std::string& error) {
        return {false, error, json::object()};
    }
};

// Type-safe parameter builder for creating JSON schemas
class ParameterBuilder {
    json schema;
    json properties;
    std::vector<std::string> required_fields;

public:
    ParameterBuilder() {
        schema["type"] = "object";
    }

    ParameterBuilder& add_integer(const std::string& name, const std::string& description = "", bool required = true) {
        properties[name] = {{"type", "integer"}};
        if (!description.empty()) {
            properties[name]["description"] = description;
        }
        if (required) required_fields.push_back(name);
        return *this;
    }

    ParameterBuilder& add_string(const std::string& name, const std::string& description = "", bool required = true) {
        properties[name] = {{"type", "string"}};
        if (!description.empty()) {
            properties[name]["description"] = description;
        }
        if (required) required_fields.push_back(name);
        return *this;
    }

    ParameterBuilder& add_boolean(const std::string& name, const std::string& description = "", bool required = true) {
        properties[name] = {{"type", "boolean"}};
        if (!description.empty()) {
            properties[name]["description"] = description;
        }
        if (required) required_fields.push_back(name);
        return *this;
    }

    ParameterBuilder& add_array(const std::string& name, const std::string& item_type, const std::string& description = "", bool required = true) {
        properties[name] = {
            {"type", "array"},
            {"items", {{"type", item_type}}}
        };
        if (!description.empty()) {
            properties[name]["description"] = description;
        }
        if (required) required_fields.push_back(name);
        return *this;
    }

    json build() const {
        json result = schema;
        result["properties"] = properties;
        if (!required_fields.empty()) {
            result["required"] = required_fields;
        }
        return result;
    }
};

// Abstract tool interface - no implementation dependencies
class Tool {
public:
    virtual ~Tool() = default;

    // Tool metadata
    virtual std::string name() const = 0;
    virtual std::string description() const = 0;
    virtual json parameters_schema() const = 0;

    // Execution
    virtual ToolResult execute(const json& input) = 0;

    // Helper to create tool definition for API
    json to_api_definition() const {
        return {
            {"name", name()},
            {"description", description()},
            {"input_schema", parameters_schema()}
        };
    }
};

// Tool registry interface - standard tool management
class ToolRegistry {
public:
    // Tool usage statistics
    struct ToolStats {
        int execution_count = 0;
        int success_count = 0;
        int failure_count = 0;
        double total_duration_ms = 0.0;
        std::chrono::steady_clock::time_point last_used;
    };

protected:
    std::unordered_map<std::string, std::unique_ptr<Tool>> tools;
    std::vector<std::string> tool_order;  // Maintain registration order for caching
    std::unordered_map<std::string, ToolStats> tool_stats;

public:
    virtual ~ToolRegistry() = default;

    // Tool registration
    void register_tool(std::unique_ptr<Tool> tool) {
        std::string name = tool->name();
        if (tools.find(name) == tools.end()) {
            tool_order.push_back(name);
        }
        tools[name] = std::move(tool);
    }

    // Template helper for registering tool types
    template<typename ToolType, typename... Args>
    void register_tool_type(Args&&... args) {
        register_tool(std::make_unique<ToolType>(std::forward<Args>(args)...));
    }

    // Tool access
    Tool* get_tool(const std::string& name) {
        auto it = tools.find(name);
        return it != tools.end() ? it->second.get() : nullptr;
    }

    bool has_tools() const {
        return !tools.empty();
    }

    // Get API definitions for all registered tools
    std::vector<json> get_api_definitions() const {
        std::vector<json> defs;
        // Use ordered list to maintain consistent tool order (necessary for prompt caching)
        for (const std::string& name : tool_order) {
            auto it = tools.find(name);
            if (it != tools.end()) {
                defs.push_back(it->second->to_api_definition());
            }
        }
        return defs;
    }

    // Execute tool and return formatted message
    messages::Message execute_tool_call(const messages::ToolUseContent& tool_use) {
        Tool* tool = get_tool(tool_use.name);
        if (!tool) {
            json error_result;
            error_result["success"] = false;
            error_result["error"] = "Unknown tool: " + tool_use.name;
            return messages::Message::tool_result(tool_use.id, error_result.dump());
        }

        // Track execution start time
        auto start_time = std::chrono::steady_clock::now();
        
        // Execute the tool
        ToolResult result = tool->execute(tool_use.input);
        
        // Track execution end time
        auto end_time = std::chrono::steady_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
        
        // Update statistics
        ToolStats& stats = tool_stats[tool_use.name];
        stats.execution_count++;
        stats.total_duration_ms += duration_ms;
        stats.last_used = end_time;
        
        if (result.wasSuccess) {
            stats.success_count++;
        } else {
            stats.failure_count++;
        }
        
        return messages::Message::tool_result(tool_use.id, result.to_json().dump());
    }

    // Get tool names for logging
    std::vector<std::string> get_tool_names() const {
        return tool_order;
    }
    
    // Direct access to tool statistics
    const std::unordered_map<std::string, ToolStats>& get_tool_stats() const {
        return tool_stats;
    }
    
    // Get tool usage statistics as JSON
    json get_tool_statistics() const {
        json stats = json::array();
        
        for (const auto& tool_name : tool_order) {
            auto it = tool_stats.find(tool_name);
            if (it != tool_stats.end() && it->second.execution_count > 0) {
                const ToolStats& tool_stat = it->second;
                
                json stat;
                stat["name"] = tool_name;
                stat["execution_count"] = tool_stat.execution_count;
                stat["success_count"] = tool_stat.success_count;
                stat["failure_count"] = tool_stat.failure_count;
                stat["success_rate"] = (tool_stat.execution_count > 0) 
                    ? (double)tool_stat.success_count / tool_stat.execution_count 
                    : 0.0;
                stat["average_duration_ms"] = (tool_stat.execution_count > 0)
                    ? tool_stat.total_duration_ms / tool_stat.execution_count
                    : 0.0;
                
                // Add time since last use
                if (tool_stat.last_used.time_since_epoch().count() > 0) {
                    auto now = std::chrono::steady_clock::now();
                    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - tool_stat.last_used);
                    stat["seconds_since_last_use"] = duration.count();
                }
                
                stats.push_back(stat);
            }
        }
        
        return stats;
    }
};

} // namespace llm_re::api

#endif // API_TOOL_REGISTRY_H