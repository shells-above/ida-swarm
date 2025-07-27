//
// Created by user on 6/30/25.
//

#ifndef TOOL_SYSTEM_H
#define TOOL_SYSTEM_H

#include "core/common.h"
#include "api/message_types.h"
#include "analysis/memory.h"
#include "analysis/actions.h"
#include "analysis/deep_analysis.h"
#include "patching/patch_manager.h"

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
            .add_boolean("named_only", "Only return user-named functions (exclude auto-generated names. defaults to true)", false)
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
        return "Search for global variables/data by name pattern. Does NOT return defined structures / types. Returns address, name, value preview, and their type name. Excludes auto-generated names by default.";
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
            .add_integer("max_xrefs", "Maximum cross-references to return (defaults to 20)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            int max_xrefs = input.value("max_xrefs", 20);
            return ToolResult::success(executor->get_data_info(address, max_xrefs));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Dump data tool
class DumpDataTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "dump_data";
    }

    std::string description() const override {
        return "Dump memory data at the given address in hexadecimal format. Use this if get_data_info isn't returning the expected information for a global due to it lacking a type. Returns hex dump with ASCII representation.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The starting address to dump")
            .add_integer("size", "Number of bytes to dump (max 65536)")
            .add_integer("bytes_per_line", "Bytes per line in the dump (defaults to 16)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            int size = input.at("size");
            int bytes_per_line = input.value("bytes_per_line", 16);

            if (size <= 0 || size > 65536) {
                return ToolResult::failure("Size must be between 1 and 65536 bytes");
            }

            if (bytes_per_line <= 0 || bytes_per_line > 32) {
                return ToolResult::failure("Bytes per line must be between 1 and 32");
            }

            return ToolResult::success(executor->dump_data(address, size, bytes_per_line));
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
        return "Deep dive into a function with optional disassembly and decompilation (Includes cross-references, strings, data refs as well). "
               "This is your primary tool for understanding code. As you analyze, consider: "
               "What would make this function clear to another reverse engineer? "
               "What names, types, and comments would tell its story?";
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
        return "Your detective's notebook for the investigation. Store hypotheses, patterns, questions, "
               "observations that might make sense later, TODOs, and connections you're still exploring. "
               "This is your thinking space - for permanent findings, use IDA's annotation tools "
               "(set_name for functions/data, set_comment for logic/discoveries, set_local_type for structures).";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("key", "Unique key for this analysis")
            .add_string("content", "The analysis content")
            .add_string("type", "Type of analysis: note, finding, hypothesis, question, analysis (analysis is for analyzing a specific function)")
            .add_integer("address", "Associated address (optional)", false)
            .add_array("related_addresses", "integer", "Additional related addresses", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string key = input.at("key");
            std::string content = input.at("content");
            std::string type = input.at("type");

            std::optional<ea_t> address;
            if (input.contains("address")) {
                address = ActionExecutor::parse_single_address_value(input.at("address"));
            }

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
        return "Retrieve your investigation notes - hypotheses, patterns, questions, and connections "
               "you've stored while building understanding. These notes complement the permanent "
               "annotations you've made in IDA's database. Use this to revisit earlier thoughts "
               "that might make more sense with new context.";
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
        return "Give a function or data a meaningful name in the IDA database. This transforms the entire "
               "codebase - every reference will now use this name. Even preliminary names like "
               "'NetworkHandler_401000' are valuable. As understanding improves, update names to be more specific. "
               "Good names are the foundation of readable reverse engineering.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address to name")
            .add_string("name", "The new name. Do not provide reserved names such as word_401000.")
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
        return "Add permanent explanatory comments visible in both disassembly and decompilation. "
               "Use for non-obvious logic, important discoveries, protocol details, or algorithm explanations. "
               "Comments are breadcrumbs for your future self and other reverse engineers. "
               "They make complex code understandable.";
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

// Get entry points tool
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

// Function prototype tools
class GetFunctionPrototypeTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_function_prototype";
    }

    std::string description() const override {
        return "Get the function prototype including return type, name, and parameters. Shows the current decompiled signature.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The function address")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_function_prototype(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class SetFunctionPrototypeTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "set_function_prototype";
    }

    std::string description() const override {
        return "Set the complete function signature including return type, calling convention, and parameters. "
               "Use this when you need to change the overall function type or multiple parameters at once. "
               "For individual parameter/variable updates, use set_variable instead. "
               "Use standard C declaration syntax (e.g., 'int __stdcall ProcessData(void *, int);'). "
               "Note: This does NOT validate the size of the types in the prototype, be EXTREMELY SURE you are providing the right sizes. You'll break decompilation if you don't";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The function address")
            .add_string("prototype", "Full C-style prototype. Note DO NOT PROVIDE ARGUMENT NAMES, only their types! This may seem strange, but it is important!")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string prototype = input.at("prototype");
            return ToolResult::success(executor->set_function_prototype(address, prototype));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Local type tools
class SearchLocalTypesTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "search_local_types";
    }

    std::string description() const override {
        return "Discover existing type definitions in the database. Essential before creating new types - "
               "previous analysis may have already identified structures you're seeing. "
               "Search by pattern to find candidates that match your current understanding. "
               "Building on existing types preserves and extends previous work.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("pattern", "Search pattern (substring match, case-insensitive). Empty for all types", false)
            .add_string("type_kind", "Filter by kind: struct, union, enum, typedef, any (defaults to any)", false)
            .add_integer("max_results", "Maximum results (defaults to 50)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string pattern = input.value("pattern", "");
            std::string type_kind = input.value("type_kind", "any");
            int max_results = input.value("max_results", 50);
            return ToolResult::success(executor->search_local_types(pattern, type_kind, max_results));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class GetLocalTypeTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_local_type";
    }

    std::string description() const override {
        return "Get the full C definition of a local type by name. Shows the complete struct/union/enum declaration.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("type_name", "Name of the type to retrieve")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string type_name = input.at("type_name");
            return ToolResult::success(executor->get_local_type(type_name));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class SetLocalTypeTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "set_local_type";
    }

    std::string description() const override {
        return "Define structures that unlock understanding across the entire binary. "
                  "One good struct definition can transform dozens of functions from cryptic to clear. "
                  "Remember to work iteratively on these types, your definition may not be perfect now, but you can iterate on it as you learn more. "
                  "Make sure to chain these tool calls correctly if creating types that depend on one another (the order in which you supply tool calls is respected). "
                  "Always search existing types first - build on previous work.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("definition", "C-style type definition (e.g., 'struct Point { int x; int y; };'). Only define one struct per set_local_type tool call")
            .add_boolean("replace_existing", "Replace if type already exists (defaults to true)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string definition = input.at("definition");
            bool replace_existing = input.value("replace_existing", true);
            return ToolResult::success(executor->set_local_type(definition, replace_existing));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Local variable tools
class GetVariablesTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_variables";
    }

    std::string description() const override {
        return "Get all variables in a function - both arguments and locals. "
               "Shows their current names, types, and locations (stack offset or register). "
               "Use this to see what variables need better names or correct types.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The function address")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_variables(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class SetVariableTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "set_variable";
    }

    std::string description() const override {
        return "Update any variable in a function - arguments or locals. Give them meaningful names and/or correct types. "
               "Transform 'v1' into 'packetLength', 'a2' into 'clientSocket'. "
               "Works for both function arguments (a1, a2, etc.) and local variables (v1, v2, etc.). "
               "Well-named variables make function logic self-documenting. ";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The function address")
            .add_string("variable_name", "Current variable name (e.g., 'v1', 'a2', or existing name)")
            .add_string("new_name", "New variable name", false)
            .add_string("new_type", "New type (e.g., 'SOCKET', 'char*', 'MY_STRUCT'). Be very careful with the size of the types", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string variable_name = input.at("variable_name");
            std::string new_name = input.value("new_name", "");
            std::string new_type = input.value("new_type", "");
            return ToolResult::success(executor->set_variable(address, variable_name, new_name, new_type));
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
        return "Submit your final report on this binary once you have FULLY completed your comprehensive reverse engineering. DO NOT CALL THIS EARLY. "
                  "Note this will end your analysis, so ONLY use this once you have FULLY reversed EVERYTHING to the specs provided by the system AND the user.";
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

// Deep analysis collection tools
class StartDeepAnalysisCollectionTool : public Tool {
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager;

public:
    StartDeepAnalysisCollectionTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<DeepAnalysisManager> dam) : Tool(mem, exec), deep_analysis_manager(dam) {}

    std::string name() const override {
        return "start_deep_analysis_collection";
    }

    std::string description() const override {
        return "EXPENSIVE OPERATION - Start collecting information for an extremely complex reverse engineering task that requires deep expert analysis. "
               "Use this ONLY when you encounter a system so complex that normal analysis tools are insufficient. (you should have attempted the problem before, and only use this if you can't figure it out)"
               "The flow for performing deep analysis is recognizing a complex task that warrants this process and calling start_deep_analysis_collection. "
               "Then explore the binary further looking for more information and provide it using the add_to_deep_analysis call. "
               "Once you have collected enough information, call request_deep_analysis. "
               "Remember! The result can *only be as good as the information provided*, so your information gathering stage with add_to_deep_analysis is of the utmost importance. "
               "This will delegate to the Opus 4 model at SIGNIFICANT cost.";  // not necessarily opus 4, but we can let the model think that
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
               "Call this as you discover relevant information about the complex system you're analyzing. "
               "It is ABSOLUTELY CRITICAL to add relevant functions using the function_address parameter. "  // LLM doesn't like adding to this param, but it references the functions inside whatever text it provides here
               "Opus 4 will only receive function information for functions that you explicitly provide in this parameter. ";
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
        return "VERY EXPENSIVE - Send the collected information to Opus 4 for deep expert analysis. "  // not necessarily opus 4, but we can let the model think that
               "This will include all collected data, memory contents, and full decompilations. "
               "Only use after collecting sufficient information. Each analysis is VERY expensive. "
               "The analysis will be stored and can be retrieved later with get_deep_analysis.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("task", "Specific analysis task or questions for Opus 4 to address")  // not necessarily opus 4, but we can let the model think that
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
               "Use this to see what complex systems have been analyzed by Opus 4.";  // not necessarily opus 4, but we can let the model think that
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
               "Returns the full expert analysis from Opus 4 for the specified complex system.";  // not necessarily opus 4, but we can let the model think that
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

// Patch bytes tool - requires original bytes verification
class PatchBytesTool : public Tool {
    std::shared_ptr<PatchManager> patch_manager_;
    
public:
    PatchBytesTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<PatchManager> pm) : Tool(mem, exec), patch_manager_(pm) {}
    
    std::string name() const override {
        return "patch_bytes";
    }
    
    std::string description() const override {
        return "Patch raw bytes at a specific address with mandatory verification. "
               "REQUIRES original bytes for safety verification before patching. "
               "Checks instruction boundaries and validates all inputs.";
    }
    
    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "Target address to patch")
            .add_string("original_bytes", "Original bytes in hex format for verification (e.g., '90 90 90' or '909090')")
            .add_string("new_bytes", "New bytes to write in hex format")
            .add_string("description", "REQUIRED: Description of why this patch is being applied (for audit trail)")
            .build();
    }
    
    ToolResult execute(const json& input) override {
        if (!patch_manager_) {
            return ToolResult::failure("Patch manager not initialized");
        }
        
        try {
            // Parse and validate parameters
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string original_hex = input.at("original_bytes");
            std::string new_hex = input.at("new_bytes");
            std::string description = input.at("description");  // Required for audit trail
            
            if (description.empty()) {
                return ToolResult::failure("Description is required for audit trail");
            }
            
            // Apply the byte patch with verification (thread safety handled in PatchManager)
            auto patch_result = patch_manager_->apply_byte_patch(address, original_hex, new_hex, description);
            
            if (patch_result.success) {
                json data;
                data["address"] = HexAddress(address);
                data["original_bytes"] = original_hex;
                data["new_bytes"] = new_hex;
                data["bytes_patched"] = patch_result.bytes_patched;
                data["description"] = description;
                data["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                return ToolResult::success(data);
            } else {
                return ToolResult::failure(patch_result.error_message);
            }
            
        } catch (const std::exception& e) {
            return ToolResult::failure(std::string("Exception: ") + e.what());
        }
    }
};

// Patch assembly tool - requires original assembly verification
class PatchAssemblyTool : public Tool {
    std::shared_ptr<PatchManager> patch_manager_;
    
public:
    PatchAssemblyTool(std::shared_ptr<BinaryMemory> mem,
                      std::shared_ptr<ActionExecutor> exec,
                      std::shared_ptr<PatchManager> pm)
        : Tool(mem, exec), patch_manager_(pm) {}
    
    std::string name() const override {
        return "patch_assembly";
    }
    
    std::string description() const override {
        return "Patch assembly instructions at a specific address with mandatory verification. "
               "REQUIRES original assembly for safety verification before patching. "
               "Automatically handles NOP padding if new instruction is smaller.";
    }
    
    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "Target address to patch")
            .add_string("original_asm", "Original assembly instruction(s) for verification")
            .add_string("new_asm", "New assembly instruction(s) to write")
            .add_string("description", "REQUIRED: Description of why this patch is being applied (for audit trail)")
            .build();
    }
    
    ToolResult execute(const json& input) override {
        if (!patch_manager_) {
            return ToolResult::failure("Patch manager not initialized");
        }
        
        try {
            // Parse and validate parameters
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string original_asm = input.at("original_asm");
            std::string new_asm = input.at("new_asm");
            std::string description = input.at("description");  // Required for audit trail
            
            if (description.empty()) {
                return ToolResult::failure("Description is required for audit trail");
            }
            
            // Apply the assembly patch with verification (thread safety handled in PatchManager)
            auto patch_result = patch_manager_->apply_assembly_patch(
                address, original_asm, new_asm, description);
            
            if (patch_result.success) {
                json data;
                data["address"] = HexAddress(address);
                data["original_asm"] = original_asm;
                data["new_asm"] = new_asm;
                data["bytes_patched"] = patch_result.bytes_patched;
                data["description"] = description;
                data["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                if (patch_result.nops_added > 0) {
                    data["nops_added"] = patch_result.nops_added;
                }
                return ToolResult::success(data);
            } else {
                return ToolResult::failure(patch_result.error_message);
            }
            
        } catch (const std::exception& e) {
            return ToolResult::failure(std::string("Exception: ") + e.what());
        }
    }
};

// Revert patches tool
class RevertPatchTool : public Tool {
    std::shared_ptr<PatchManager> patch_manager_;
    
public:
    RevertPatchTool(std::shared_ptr<BinaryMemory> mem,
                    std::shared_ptr<ActionExecutor> exec,
                    std::shared_ptr<PatchManager> pm)
        : Tool(mem, exec), patch_manager_(pm) {}
    
    std::string name() const override {
        return "revert_patch";
    }
    
    std::string description() const override {
        return "Revert a previously applied patch at a specific address or revert all patches. "
               "Restores original bytes from before the patch was applied.";
    }
    
    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "Address of patch to revert", false)
            .add_boolean("revert_all", "Revert all patches", false)
            .build();
    }
    
    ToolResult execute(const json& input) override {
        if (!patch_manager_) {
            return ToolResult::failure("Patch manager not initialized");
        }
        
        try {
            json data;
            bool success = false;
            
            if (input.value("revert_all", false)) {
                // Revert all patches
                success = patch_manager_->revert_all();
                if (success) {
                    data["reverted"] = "all";
                    data["message"] = "All patches reverted successfully";
                }
            } else if (input.contains("address")) {
                // Revert single patch
                ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
                success = patch_manager_->revert_patch(address);
                if (success) {
                    data["address"] = HexAddress(address);
                    data["message"] = "Patch reverted successfully";
                }
            } else {
                return ToolResult::failure("Must specify address or revert_all");
            }
            
            if (!success) {
                return ToolResult::failure("No patch found at specified address");
            }
            
            return ToolResult::success(data);
            
        } catch (const std::exception& e) {
            return ToolResult::failure(std::string("Exception: ") + e.what());
        }
    }
};

// List patches tool
class ListPatchesTool : public Tool {
    std::shared_ptr<PatchManager> patch_manager_;
    
public:
    ListPatchesTool(std::shared_ptr<BinaryMemory> mem,
                    std::shared_ptr<ActionExecutor> exec,
                    std::shared_ptr<PatchManager> pm)
        : Tool(mem, exec), patch_manager_(pm) {}
    
    std::string name() const override {
        return "list_patches";
    }
    
    std::string description() const override {
        return "List all applied patches with their descriptions, timestamps, and original/new bytes. "
               "Shows the complete audit trail of all modifications.";
    }
    
    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "List only patch at specific address", false)
            .build();
    }
    
    ToolResult execute(const json& input) override {
        if (!patch_manager_) {
            return ToolResult::failure("Patch manager not initialized");
        }
        
        try {
            json data;
            json patches_json = json::array();
            
            if (input.contains("address")) {
                // Get single patch
                ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
                auto patch_info = patch_manager_->get_patch_info(address);
                
                if (patch_info.has_value()) {
                    const auto& patch = patch_info.value();
                    json patch_json;
                    patch_json["address"] = HexAddress(patch.address);
                    patch_json["original_bytes"] = patch.original_bytes_hex;
                    patch_json["patched_bytes"] = patch.patched_bytes_hex;
                    patch_json["description"] = patch.description;
                    patch_json["timestamp"] = std::chrono::system_clock::to_time_t(patch.timestamp);
                    patch_json["is_assembly_patch"] = patch.is_assembly_patch;
                    
                    if (patch.is_assembly_patch) {
                        patch_json["original_asm"] = patch.original_asm;
                        patch_json["patched_asm"] = patch.patched_asm;
                    }
                    
                    patches_json.push_back(patch_json);
                }
            } else {
                // List all patches
                auto all_patches = patch_manager_->list_patches();
                
                for (const auto& patch : all_patches) {
                    json patch_json;
                    patch_json["address"] = HexAddress(patch.address);
                    patch_json["original_bytes"] = patch.original_bytes_hex;
                    patch_json["patched_bytes"] = patch.patched_bytes_hex;
                    patch_json["description"] = patch.description;
                    patch_json["timestamp"] = std::chrono::system_clock::to_time_t(patch.timestamp);
                    patch_json["is_assembly_patch"] = patch.is_assembly_patch;
                    
                    if (patch.is_assembly_patch) {
                        patch_json["original_asm"] = patch.original_asm;
                        patch_json["patched_asm"] = patch.patched_asm;
                    }
                    
                    patches_json.push_back(patch_json);
                }
            }
            
            data["patches"] = patches_json;
            data["count"] = patches_json.size();
            
            // Add statistics
            auto stats = patch_manager_->get_statistics();
            data["statistics"]["total_patches"] = stats.total_patches;
            data["statistics"]["assembly_patches"] = stats.assembly_patches;
            data["statistics"]["byte_patches"] = stats.byte_patches;
            data["statistics"]["total_bytes_patched"] = stats.total_bytes_patched;
            
            return ToolResult::success(data);
            
        } catch (const std::exception& e) {
            return ToolResult::failure(std::string("Exception: ") + e.what());
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

    void register_all_tools(std::shared_ptr<BinaryMemory> memory, std::shared_ptr<ActionExecutor> executor, bool enable_deep_analysis, std::shared_ptr<DeepAnalysisManager> deep_analysis_manager = nullptr, std::shared_ptr<PatchManager> patch_manager = nullptr) {
        // Core navigation and info tools
        register_tool_type<GetXrefsTool>(memory, executor);
        register_tool_type<GetFunctionInfoTool>(memory, executor);
        register_tool_type<GetDataInfoTool>(memory, executor);
        register_tool_type<DumpDataTool>(memory, executor);
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

        // Binary info tools
        register_tool_type<GetImportsTool>(memory, executor);
        register_tool_type<GetEntryPointsTool>(memory, executor);

        // Updating decompilation tools
        register_tool_type<GetFunctionPrototypeTool>(memory, executor);
        register_tool_type<SetFunctionPrototypeTool>(memory, executor);
        register_tool_type<GetVariablesTool>(memory, executor);
        register_tool_type<SetVariableTool>(memory, executor);

        // Local type tools
        register_tool_type<SearchLocalTypesTool>(memory, executor);
        register_tool_type<GetLocalTypeTool>(memory, executor);
        register_tool_type<SetLocalTypeTool>(memory, executor);

        // Patch tools
        if (patch_manager) {
            register_tool_type<PatchBytesTool>(memory, executor, patch_manager);
            register_tool_type<PatchAssemblyTool>(memory, executor, patch_manager);
            register_tool_type<RevertPatchTool>(memory, executor, patch_manager);
            register_tool_type<ListPatchesTool>(memory, executor, patch_manager);
        }

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

    bool has_tools() const {
        return !tools.empty();
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