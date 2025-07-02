//
// Created by user on 6/30/25.
//

#ifndef TOOL_SYSTEM_H
#define TOOL_SYSTEM_H

#include "common.h"
#include "message_types.h"
#include "memory.h"
#include "actions.h"
#include "deep_analysis.h"

namespace llm_re::tools {

// Base tool result type
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
        // Merge data fields into top level for backward compatibility
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

// Base tool interface
class Tool {
protected:
    std::shared_ptr<BinaryMemory> memory;
    std::shared_ptr<ActionExecutor> executor;

public:
    Tool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec)
        : memory(mem), executor(exec) {}

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

// Type-safe parameter builder
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

// Unified search functions tool
class SearchFunctionsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "search_functions";
    }

    std::string description() const override {
        return "Search for functions by name pattern. Can filter to only named functions and limit results. Returns address, name, and whether it's user-named.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("pattern", "Search pattern (substring match, case-insensitive). Empty for all functions", false)
            .add_boolean("named_only", "Only return user-named functions (exclude auto-generated names)", false)
            .add_integer("max_results", "Maximum number of results to return (defaults to 100)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string pattern = input.value("pattern", "");
            bool named_only = input.value("named_only", true);
            int max_results = input.value("max_results", 100);

            return ToolResult::success(executor->search_functions(pattern, named_only, max_results));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Unified search globals tool
class SearchGlobalsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "search_globals";
    }

    std::string description() const override {
        return "Search for global variables/data by name pattern. Returns address, name, value preview, and type. Excludes auto-generated names by default.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("pattern", "Search pattern (substring match, case-insensitive). Empty for all globals", false)
            .add_integer("max_results", "Maximum number of results to return (defaults to 100)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string pattern = input.value("pattern", "");
            int max_results = input.value("max_results", 100);

            return ToolResult::success(executor->search_globals(pattern, max_results));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Unified search strings tool
class SearchStringsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "search_strings";
    }

    std::string description() const override {
        return "Search for strings in the binary. Can filter by content pattern and minimum length. Returns address and content.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("pattern", "Search pattern (substring match, case-insensitive). Empty for all strings", false)
            .add_integer("min_length", "Minimum string length (defaults to 5)", false)
            .add_integer("max_results", "Maximum number of results to return (defaults to 100)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string pattern = input.value("pattern", "");
            int min_length = input.value("min_length", 5);
            int max_results = input.value("max_results", 100);

            return ToolResult::success(executor->search_strings(pattern, min_length, max_results));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Get comprehensive function info tool
class GetFunctionInfoTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_function_info";
    }

    std::string description() const override {
        return "Get comprehensive information about a function including name, bounds, cross-references counts, and reference counts. Fast overview without disassembly/decompilation.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the function")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_function_info(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Get comprehensive data info tool
class GetDataInfoTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_data_info";
    }

    std::string description() const override {
        return "Get comprehensive information about data including name, value, type, and cross-references. Provides complete data context.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the data")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_data_info(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Analyze function tool (replaces decompilation/disassembly tools)
class AnalyzeFunctionTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "analyze_function";
    }

    std::string description() const override {
        return "Analyze a function with optional disassembly and decompilation. Includes cross-references, strings, data refs, and code. Use this for deep function understanding.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the function")
            .add_boolean("include_disasm", "Include disassembly (defaults to false)", false)
            .add_boolean("include_decomp", "Include decompilation (defaults to true)", false)
            .add_integer("max_xrefs", "Maximum cross-references to include (defaults to 20)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            bool include_disasm = input.value("include_disasm", false);
            bool include_decomp = input.value("include_decomp", true);
            int max_xrefs = input.value("max_xrefs", 20);

            return ToolResult::success(executor->analyze_function(address, include_disasm, include_decomp, max_xrefs));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Store analysis tool (unified knowledge storage)
class StoreAnalysisTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "store_analysis";
    }

    std::string description() const override {
        return "Store analysis findings, notes, or insights. Can be associated with addresses or kept as global notes.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("key", "Unique key for this analysis")
            .add_string("content", "The analysis content")
            .add_integer("address", "Associated address (optional)", false)
            .add_string("type", "Type of analysis: note, finding, hypothesis, question, analysis (defaults to note)", false)
            .add_array("related_addresses", "integer", "Additional related addresses", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string key = input.at("key");
            std::string content = input.at("content");
            std::optional<ea_t> address;
            if (input.contains("address")) {
                address = ActionExecutor::parse_single_address_value(input.at("address"));
            }
            std::string type = input.value("type", "note");
            std::vector<ea_t> related_addresses;
            if (input.contains("related_addresses")) {
                related_addresses = ActionExecutor::parse_list_address_param(input, "related_addresses");
            }

            return ToolResult::success(executor->store_analysis(key, content, address, type, related_addresses));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Get analysis tool
class GetAnalysisTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_analysis";
    }

    std::string description() const override {
        return "Retrieve stored analysis by key, address, type, or search pattern.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("key", "Specific key to retrieve", false)
            .add_integer("address", "Find analysis related to this address", false)
            .add_string("type", "Filter by type (note, finding, hypothesis, question, analysis)", false)
            .add_string("pattern", "Search pattern in content", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string key = input.value("key", "");
            std::optional<ea_t> address;
            if (input.contains("address")) {
                address = ActionExecutor::parse_single_address_value(input.at("address"));
            }
            std::string type = input.value("type", "");
            std::string pattern = input.value("pattern", "");

            return ToolResult::success(executor->get_analysis(key, address, type, pattern));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Batch analyze functions tool
class AnalyzeFunctionsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "analyze_functions";
    }

    std::string description() const override {
        return "Analyze multiple functions as a batch. Efficient for analyzing related function groups. Returns analysis for each function.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_array("addresses", "integer", "List of function addresses to analyze")
            .add_integer("level", "Analysis detail level (0=basic info, 1=with decompilation, 2=full with disasm. Defaults to 1)", false)
            .add_string("group_name", "Optional name for this group of functions", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::vector<ea_t> addresses = ActionExecutor::parse_list_address_param(input, "addresses");
            int level = input.value("level", 1);
            std::string group_name = input.value("group_name", "");

            return ToolResult::success(executor->analyze_functions(addresses, level, group_name));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Get analysis context tool (replaces multiple context tools)
class GetAnalysisContextTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_analysis_context";
    }

    std::string description() const override {
        return "Get comprehensive analysis context including nearby functions, analysis queue, exploration frontier, and relationships. Centers around current focus or specified address.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "Center context around this address (uses current focus if not specified)", false)
            .add_integer("radius", "How many functions away to include (defaults to 2)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::optional<ea_t> address;
            if (input.contains("address")) {
                address = ActionExecutor::parse_single_address_value(input.at("address"));
            }
            int radius = input.value("radius", 2);

            return ToolResult::success(executor->get_analysis_context(address, radius));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Cross-reference tool
class GetXrefsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_xrefs";
    }

    std::string description() const override {
        return "Get cross-references to AND from an address. Shows what calls this and what this calls. Essential for understanding code relationships.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address to get xrefs for")
            .add_integer("max_results", "Maximum xrefs per direction (defaults to 100)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            int max_results = input.value("max_results", 100);

            return ToolResult::success(executor->get_xrefs(address, max_results));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Set name tool (functions + data)
class SetNameTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "set_name";
    }

    std::string description() const override {
        return "Set a custom name for a function or data at the given address. Works for both code and data locations.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address to name")
            .add_string("name", "The new name")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string name = input.at("name");

            return ToolResult::success(executor->set_name(address, name));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Comment tool
class SetCommentTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "set_comment";
    }

    std::string description() const override {
        return "Set or clear a comment at the given address. Empty comment clears existing. Adds to both disassembly and decompilation views.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address for the comment")
            .add_string("comment", "The comment text (empty to clear)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string comment = input.value("comment", "");

            return ToolResult::success(executor->set_comment(address, comment));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Get imports tool
class GetImportsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_imports";
    }

    std::string description() const override {
        return "Get all imported functions and libraries. Shows external dependencies of the binary.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("max_results", "Maximum imports to return (defaults to 100)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            int max_results = input.value("max_results", 100);
            return ToolResult::success(executor->get_imports(max_results));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Get entry points tool (kept as is)
class GetEntryPointsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_entry_points";
    }

    std::string description() const override {
        return "Get all entry points of the binary (main entry, exports, TLS callbacks). Shows where execution can begin.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("max_count", "Max number of entry points to return")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            return ToolResult::success(executor->get_entry_points(input.at("max_count")));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Mark for analysis tool
class MarkForAnalysisTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "mark_for_analysis";
    }

    std::string description() const override {
        return "Mark a function for future analysis with a reason and priority. Helps you organize your analysis workflow.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address to mark for analysis")
            .add_string("reason", "The reason for analysis")
            .add_integer("priority", "Priority level (1-10, defaults to 5)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string reason = input.at("reason");
            int priority = input.value("priority", 5);

            return ToolResult::success(executor->mark_for_analysis(address, reason, priority));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Set focus tool
class SetCurrentFocusTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "set_current_focus";
    }

    std::string description() const override {
        return "Set the current analysis focus to the given address. Centers memory context around this location.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address to focus on")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->set_current_focus(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Final report submission tool
class SubmitFinalReportTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "submit_final_report";
    }

    std::string description() const override {
        return "Submit your final analysis report when you have gathered enough information to answer the user's task. This completes the analysis.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("report", "Your complete analysis report")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string report = input.at("report");

            json result;
            result["success"] = true;
            result["report_received"] = true;
            result["message"] = "Report submitted successfully";

            return ToolResult::success(result);

        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Deep analysis collection tools (kept as is)
class StartDeepAnalysisCollectionTool : public Tool {
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager;

public:
    StartDeepAnalysisCollectionTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<DeepAnalysisManager> dam) : Tool(mem, exec), deep_analysis_manager(dam) {}

    std::string name() const override {
        return "start_deep_analysis_collection";
    }

    std::string description() const override {
        return "EXPENSIVE OPERATION - Start collecting information for an extremely complex reverse engineering task that requires deep expert analysis. "
               "Use this ONLY when you encounter a system so complex that normal analysis tools are insufficient. "
               "The flow for performing deep analysis is recognizing a complex task that warrants this process and calling start_deep_analysis_collection. "
               "Then explore the binary further looking for more information and provide it using the add_to_deep_analysis call. "
               "Once you have collected enough information, call request_deep_analysis. "
               "Remember! The result can *only be as good as the information provided*, so your information gathering stage with add_to_deep_analysis is of the utmost importance. "
               "This will delegate to the more powerful Opus 4 model at SIGNIFICANT cost.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("topic", "A descriptive name for the complex system/task being analyzed")
            .add_string("description", "Detailed description of what makes this task complex and why deep analysis is needed")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string topic = input.at("topic");
            std::string description = input.at("description");

            deep_analysis_manager->start_collection(topic, description);

            json result;
            result["success"] = true;
            result["message"] = "Started deep analysis collection for: " + topic;
            result["warning"] = "Remember to add relevant functions and observations (add_to_deep_analysis) before requesting analysis";

            return ToolResult::success(result);
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class AddToDeepAnalysisTool : public Tool {
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager;

public:
    AddToDeepAnalysisTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<DeepAnalysisManager> dam) : Tool(mem, exec), deep_analysis_manager(dam) {}

    std::string name() const override {
        return "add_to_deep_analysis";
    }

    std::string description() const override {
        return "Add observations, findings, or function addresses to the current deep analysis collection. "
               "Call this as you discover relevant information about the complex system you're analyzing.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("key", "A descriptive key for this piece of information")
            .add_string("value", "The observation, finding, or analysis to store", false)
            .add_integer("function_address", "Address of a related function to include in deep analysis. Expected to be formatted as: [ADDR, ADDR] or plainly as ADDR. Do NOT wrap the square brackets with quotes.", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            if (!deep_analysis_manager->has_active_collection()) {
                return ToolResult::failure("No active deep analysis collection. Call start_deep_analysis_collection first.");
            }

            std::string key = input.at("key");

            if (input.contains("value")) {
                std::string value = input.at("value");
                deep_analysis_manager->add_to_collection(key, value);
            }

            if (input.contains("function_address")) {
                std::vector<ea_t> addrs = ActionExecutor::parse_list_address_param(input, "function_address");
                for (ea_t addr : addrs)
                    deep_analysis_manager->add_function_to_collection(addr);
            }

            json result;
            result["success"] = true;
            result["message"] = "Added to deep analysis collection";

            return ToolResult::success(result);
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class RequestDeepAnalysisTool : public Tool {
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager;

public:
    RequestDeepAnalysisTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<DeepAnalysisManager> dam) : Tool(mem, exec), deep_analysis_manager(dam) {}

    std::string name() const override {
        return "request_deep_analysis";
    }

    std::string description() const override {
        return "VERY EXPENSIVE - Send the collected information to Opus 4 for deep expert analysis. "
               "This will include all collected data, memory contents, and full decompilations. "
               "Only use after collecting sufficient information. Each analysis is VERY expensive. "
               "The analysis will be stored and can be retrieved later with get_deep_analysis.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("task", "Specific analysis task or questions for Opus 4 to address")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string task = input.at("task");

            if (!deep_analysis_manager->has_active_collection()) {
                return ToolResult::failure("No active deep analysis collection to analyze");
            }

            // Get collection info for cost estimate
            DeepAnalysisCollection collection = deep_analysis_manager->get_current_collection();

            // Execute the analysis
            DeepAnalysisResult result = deep_analysis_manager->execute_deep_analysis(
                task,
                executor,
                [](const std::string &msg) {
                    /* Progress callback if needed */
                }
            );

            json response;
            response["success"] = true;
            response["analysis_key"] = result.key;
            response["topic"] = result.topic;
            response["message"] = "Deep analysis completed. Use get_deep_analysis with key: " + result.key;

            return ToolResult::success(response);
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class ListDeepAnalysesTool : public Tool {
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager;

public:
    ListDeepAnalysesTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<DeepAnalysisManager> dam) : Tool(mem, exec), deep_analysis_manager(dam) {}

    std::string name() const override {
        return "list_deep_analyses";
    }

    std::string description() const override {
        return "List all completed deep analyses with their keys and descriptions. "
               "Use this to see what complex systems have been analyzed by Opus 4.";
    }

    json parameters_schema() const override {
        return ParameterBuilder().build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::vector<std::pair<std::string, std::string>> analyses = deep_analysis_manager->list_analyses();

            json result;
            result["success"] = true;
            result["analyses"] = json::array();

            for (const auto& [key, description] : analyses) {
                json analysis_info;
                analysis_info["key"] = key;
                analysis_info["description"] = description;
                result["analyses"].push_back(analysis_info);
            }

            result["count"] = analyses.size();

            return ToolResult::success(result);
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class GetDeepAnalysisTool : public Tool {
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager;

public:
    GetDeepAnalysisTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<DeepAnalysisManager> dam) : Tool(mem, exec), deep_analysis_manager(dam) {}

    std::string name() const override {
        return "get_deep_analysis";
    }

    std::string description() const override {
        return "Retrieve a completed deep analysis by its key. "
               "Returns the full expert analysis from Opus 4 for the specified complex system.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("key", "The analysis key (from list_deep_analyses or request_deep_analysis)")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string key = input.at("key");

            std::optional<DeepAnalysisResult> analysis_opt = deep_analysis_manager->get_analysis(key);
            if (!analysis_opt) {
                return ToolResult::failure("Deep analysis not found with key: " + key);
            }

            DeepAnalysisResult& analysis = *analysis_opt;

            json result;
            result["success"] = true;
            result["key"] = analysis.key;
            result["topic"] = analysis.topic;
            result["task"] = analysis.task_description;
            result["analysis"] = analysis.analysis;

            return ToolResult::success(result);
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Tool registry with type safety
class ToolRegistry {
    std::unordered_map<std::string, std::unique_ptr<Tool>> tools;
    std::vector<std::string> tool_order;  // Maintain registration order

public:
    void register_tool(std::unique_ptr<Tool> tool) {
        std::string name = tool->name();
        if (tools.find(name) == tools.end()) {
            tool_order.push_back(name);
        }
        tools[name] = std::move(tool);
    }

    template<typename ToolType, typename... Args>
    void register_tool_type(Args&&... args) {
        register_tool(std::make_unique<ToolType>(std::forward<Args>(args)...));
    }

    void register_all_tools(std::shared_ptr<BinaryMemory> memory, std::shared_ptr<ActionExecutor> executor, bool enable_deep_analysis, std::shared_ptr<DeepAnalysisManager> deep_analysis_manager = nullptr) {
        // Core navigation and info tools
        register_tool_type<GetXrefsTool>(memory, executor);
        register_tool_type<GetFunctionInfoTool>(memory, executor);
        register_tool_type<GetDataInfoTool>(memory, executor);
        register_tool_type<AnalyzeFunctionTool>(memory, executor);

        // Search tools
        register_tool_type<SearchFunctionsTool>(memory, executor);
        register_tool_type<SearchGlobalsTool>(memory, executor);
        register_tool_type<SearchStringsTool>(memory, executor);

        // Modification tools
        register_tool_type<SetNameTool>(memory, executor);
        register_tool_type<SetCommentTool>(memory, executor);

        // Analysis tools
        register_tool_type<StoreAnalysisTool>(memory, executor);
        register_tool_type<GetAnalysisTool>(memory, executor);
        register_tool_type<AnalyzeFunctionsTool>(memory, executor);
        register_tool_type<GetAnalysisContextTool>(memory, executor);

        // Workflow tools
        register_tool_type<MarkForAnalysisTool>(memory, executor);
        register_tool_type<SetCurrentFocusTool>(memory, executor);

        // Binary info tools
        register_tool_type<GetImportsTool>(memory, executor);
        register_tool_type<GetEntryPointsTool>(memory, executor);

        // Special tools
        register_tool_type<SubmitFinalReportTool>(memory, executor);

        // Deep analysis
        if (enable_deep_analysis) {
            register_tool_type<StartDeepAnalysisCollectionTool>(memory, executor, deep_analysis_manager);
            register_tool_type<AddToDeepAnalysisTool>(memory, executor, deep_analysis_manager);
            register_tool_type<RequestDeepAnalysisTool>(memory, executor, deep_analysis_manager);
            register_tool_type<ListDeepAnalysesTool>(memory, executor, deep_analysis_manager);
            register_tool_type<GetDeepAnalysisTool>(memory, executor, deep_analysis_manager);
        }
    }

    Tool* get_tool(const std::string& name) {
        auto it = tools.find(name);
        return it != tools.end() ? it->second.get() : nullptr;
    }

    std::vector<json> get_api_definitions() const {
        std::vector<json> defs;
        // Use ordered list to maintain consistent tool order
        // necessary for prompt caching
        for (const std::string &name: tool_order) {
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

        ToolResult result = tool->execute(tool_use.input);
        return messages::Message::tool_result(tool_use.id, result.to_json().dump());
    }

    // Get tool names for logging
    std::vector<std::string> get_tool_names() const {
        return tool_order;
    }
};

} // namespace llm_re::tools

#endif //TOOL_SYSTEM_H