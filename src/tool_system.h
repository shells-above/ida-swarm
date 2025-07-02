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

// Cross-reference tool
class GetXrefsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_xrefs_to";
    }

    std::string description() const override {
        return "Find what calls or references this address. Returns caller addresses with names. Essential for understanding how functions are used.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address to find references to")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_xrefs_to(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Get xrefs from
class GetXrefsFromTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_xrefs_from";
    }

    std::string description() const override {
        return "Find what this address calls or references. Returns called addresses with names.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address to find references from")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_xrefs_from(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Function disassembly and decompilation tools
class GetFunctionDisassemblyTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_function_disassembly";
    }

    std::string description() const override {
        return "Get the disassembly (assembly code) for a function at the given address. Shows the low-level assembly instructions. This is expensive, so only use when you need to really understand a complicated function, or if the decompilation is not making sense.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the function to disassemble")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_function_disassembly(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class GetFunctionDecompilationTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_function_decompilation";
    }

    std::string description() const override {
        return "Get the decompiled pseudocode for a function at the given address. Shows high-level C-like representation.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the function to decompile")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_function_decompilation(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Function name tools
class GetFunctionAddressTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_function_address";
    }

    std::string description() const override {
        return "Find the address of a function by its name. Returns the address where the function is located.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("name", "The name of the function to find")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string name = input.at("name");
            return ToolResult::success(executor->get_function_address(name));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class GetFunctionNameTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_function_name";
    }

    std::string description() const override {
        return "Get the name of the function at the given address. Returns the symbol name or generated name.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the function")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_function_name(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class SetFunctionNameTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "set_function_name";
    }

    std::string description() const override {
        return "Set a custom name for the function at the given address. Useful for organizing and understanding code.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the function")
            .add_string("name", "The new name for the function")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string name = input.at("name");
            return ToolResult::success(executor->set_function_name(address, name));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Function reference tools
class GetFunctionStringRefsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_function_string_refs";
    }

    std::string description() const override {
        return "Get all string references used within a function. Auto-updates memory. Helps understand function behavior.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the function")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_function_string_refs(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class GetFunctionDataRefsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_function_data_refs";
    }

    std::string description() const override {
        return "Get all data references used within a function. Auto-updates memory. Shows what data the function accesses.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the function")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_function_data_refs(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Data tools
class GetDataNameTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_data_name";
    }

    std::string description() const override {
        return "Get the name of the data variable or symbol at the given address.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the data")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_data_name(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class SetDataNameTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "set_data_name";
    }

    std::string description() const override {
        return "Set a custom name for the data variable or symbol at the given address.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the data")
            .add_string("name", "The new name for the data")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string name = input.at("name");
            return ToolResult::success(executor->set_data_name(address, name));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class GetDataTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_data";
    }

    std::string description() const override {
        return "Get the value and type information for data at the given address. Returns both the data value and its type.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the data")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->get_data(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Comment tools
class AddCommentTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "add_comment";
    }

    std::string description() const override {
        return "Add a comment to the disassembly and pseudocode at the given address. Helps document your analysis findings.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address to add the comment to")
            .add_string("comment", "The comment text")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string comment = input.at("comment");

            return ToolResult::success(executor->add_comment(address, comment));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class ClearCommentTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "clear_comment";
    }

    std::string description() const override {
        return "Clear/remove the disassembly and pseudocode comment at the given address.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address to clear the comment from")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return ToolResult::success(executor->clear_comment(address));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Import tools
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
            .add_integer("max_count", "Max number of imports to return")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            return ToolResult::success(executor->get_imports(input.at("max_count")));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// String search tool
class SearchStringsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "search_strings";
    }

    std::string description() const override {
        return "Search for strings containing the given text. Case sensitivity is optional. Useful for finding specific functionality.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("text", "The text to search for in strings")
            .add_boolean("is_case_sensitive", "Whether the search is case sensitive", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string text = input.at("text");
            bool is_case_sensitive = input.value("is_case_sensitive", false);
            return ToolResult::success(executor->search_strings(text, is_case_sensitive));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Named functions tool
class GetNamedFunctionsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_named_functions";
    }

    std::string description() const override {
        return "Get all functions with user-defined or meaningful names (excludes auto-generated names like sub_*)";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("max_count", "Maximum number of functions to return")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            int max_count = input.value("max_count", -1); // -1 means unlimited
            return ToolResult::success(executor->get_named_functions(max_count));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Search named functions tool
class SearchNamedFunctionsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "search_named_functions";
    }

    std::string description() const override {
        return "Search for named functions containing the given text in their names. Case sensitivity is optional.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("text", "The text to search for in function names")
            .add_integer("max_count", "Maximum number of functions to return")
            .add_boolean("is_case_sensitive", "Whether the search is case sensitive", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string text = input.at("text");
            bool is_case_sensitive = input.value("is_case_sensitive", false);
            int max_count = input.value("max_count", -1); // -1 means unlimited
            return ToolResult::success(executor->search_named_functions(text, is_case_sensitive, max_count));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Search named globals tool
class SearchNamedGlobalsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "search_named_globals";
    }

    std::string description() const override {
        return "Search for global variables/data by name pattern. Supports regex";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("pattern", "The pattern to search for in global names (supports regex)")
            .add_boolean("is_regex", "Whether to use regex matching (defaults to substring search)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string pattern = input.at("pattern");
            bool is_regex = input.value("is_regex", false);
            return ToolResult::success(executor->search_named_globals(pattern, is_regex));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Get named globals tool
class GetNamedGlobalsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_named_globals";
    }

    std::string description() const override {
        return "Get all global variables and data with user-defined names";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("max_count", "Max number of globals to return")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            return ToolResult::success(executor->get_named_globals(input.at("max_count")));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Get strings tool
class GetStringsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_strings";
    }

    std::string description() const override {
        return "Get all strings in the binary. Returns addresses and content. Useful for understanding program functionality.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("min_length", "Minimum string length to include (defaults to 5)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            int min_length = input.value("min_length", 5);
            return ToolResult::success(executor->get_strings(min_length));
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
        return "Get all entry points of the binary (main entry, exports, etc). Shows where execution can begin.";
    }

    json parameters_schema() const override {
        return ParameterBuilder().build();
    }

    ToolResult execute(const json& input) override {
        try {
            return ToolResult::success(executor->get_entry_points());
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Global note tools
class SetGlobalNoteTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "set_global_note";
    }

    std::string description() const override {
        return "Store a global note with a key for later retrieval. Use for persistent analysis findings.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("key", "The key/name for the note")
            .add_string("content", "The content of the note")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string key = input.at("key");
            std::string content = input.at("content");
            return ToolResult::success(executor->set_global_note(key, content));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class GetGlobalNoteTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_global_note";
    }

    std::string description() const override {
        return "Retrieve a global note by its key. Access your previously stored analysis findings.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("key", "The key/name of the note to retrieve")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string key = input.at("key");
            return ToolResult::success(executor->get_global_note(key));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class ListGlobalNotesTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "list_global_notes";
    }

    std::string description() const override {
        return "List all available global note keys. See what analysis findings you have stored.";
    }

    json parameters_schema() const override {
        return ParameterBuilder().build();
    }

    ToolResult execute(const json& input) override {
        try {
            return ToolResult::success(executor->list_global_notes());
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class SearchNotesTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "search_notes";
    }

    std::string description() const override {
        return "Search through all global notes for the given query text. Find relevant analysis findings.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("query", "The text to search for in notes")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string query = input.at("query");
            return ToolResult::success(executor->search_notes(query));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Function analysis tools
class SetFunctionAnalysisTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "set_function_analysis";
    }

    std::string description() const override {
        return "Store detailed analysis for a function at a specific detail level. Builds persistent knowledge.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the function")
            .add_integer("level", "The detail level (0=basic, 1=detailed, 2=comprehensive)")
            .add_string("analysis", "The analysis content")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            int level = input.at("level");
            std::string analysis = input.at("analysis");
            return ToolResult::success(executor->set_function_analysis(address, level, analysis));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class GetFunctionAnalysisTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_function_analysis";
    }

    std::string description() const override {
        return "Retrieve stored analysis for a function at a specific detail level. Access your previous findings.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The address of the function")
            .add_integer("level", "The detail level to retrieve (defaults to 0)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            int level = input.value("level", 0);
            return ToolResult::success(executor->get_function_analysis(address, level));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Memory context tool
class GetMemoryContextTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_memory_context";
    }

    std::string description() const override {
        return "Get memory context around an address - nearby functions, relationships, and LLM memory. Essential for understanding code structure.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "The anchor address")
            .add_integer("radius", "Search radius for nearby functions (defaults to 2)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            int radius = input.value("radius", 2);
            return ToolResult::success(executor->get_memory_context(address, radius));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Analysis tracking tools
class GetAnalyzedFunctionsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_analyzed_functions";
    }

    std::string description() const override {
        return "Get list of all functions that have been analyzed, with their names and analysis levels.";
    }

    json parameters_schema() const override {
        return ParameterBuilder().build();
    }

    ToolResult execute(const json& input) override {
        try {
            return ToolResult::success(executor->get_analyzed_functions());
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class FindFunctionsByPatternTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "find_functions_by_pattern";
    }

    std::string description() const override {
        return "Find functions matching a pattern in their name or analysis. Use regex patterns to locate similar functions.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("pattern", "The pattern to search for (supports regex)")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string pattern = input.at("pattern");
            return ToolResult::success(executor->find_functions_by_pattern(pattern));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Exploration tools
class GetExplorationFrontierTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_exploration_frontier";
    }

    std::string description() const override {
        return "Get the current exploration frontier - functions that should be analyzed next based on relationships.";
    }

    json parameters_schema() const override {
        return ParameterBuilder().build();
    }

    ToolResult execute(const json& input) override {
        try {
            return ToolResult::success(executor->get_exploration_frontier());
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class MarkForAnalysisTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "mark_for_analysis";
    }

    std::string description() const override {
        return "Mark a function for future analysis with a reason and priority. Helps organize analysis workflow.";
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

class GetAnalysisQueueTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_analysis_queue";
    }

    std::string description() const override {
        return "Get the current analysis queue with priorities and reasons. See what's pending analysis.";
    }

    json parameters_schema() const override {
        return ParameterBuilder().build();
    }

    ToolResult execute(const json& input) override {
        try {
            return ToolResult::success(executor->get_analysis_queue());
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Focus tool
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

// Insight tools
class AddInsightTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "add_insight";
    }

    std::string description() const override {
        return "Add an insight/finding with type, description, and related addresses. Build knowledge base of discoveries.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("type", "The type/category of insight (valid types: Pattern, Hypothesis, Question, Finding)")
            .add_string("description", "Description of the insight")
            .add_array("related_addresses", "integer", "List of related addresses")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string type = input.at("type");
            std::string description = input.at("description");
            std::vector<ea_t> related_addresses = ActionExecutor::parse_list_address_param(input, "related_addresses");
            return ToolResult::success(executor->add_insight(type, description, related_addresses));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class GetInsightsTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_insights";
    }

    std::string description() const override {
        return "Get insights by type (or all if type is empty). Access your accumulated knowledge and findings.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("type", "Filter by insight type (empty for all)", false)
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string type = input.value("type", "");
            return ToolResult::success(executor->get_insights(type));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Cluster analysis tools
class AnalyzeClusterTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "analyze_cluster";
    }

    std::string description() const override {
        return "Analyze a cluster of related functions together. Groups related functionality for comprehensive understanding.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_array("addresses", "integer", "List of function addresses to analyze as a cluster")
            .add_string("cluster_name", "Name for this cluster")
            .add_integer("initial_level", "Initial analysis detail level")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::vector<ea_t> addresses = input.at("addresses").get<std::vector<ea_t>>();
            std::string cluster_name = input.at("cluster_name");
            int initial_level = input.at("initial_level");
            return ToolResult::success(executor->analyze_cluster(addresses, cluster_name, initial_level));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

class GetClusterAnalysisTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "get_cluster_analysis";
    }

    std::string description() const override {
        return "Get the analysis results for a named cluster. Access comprehensive cluster findings.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_string("cluster_name", "Name of the cluster to retrieve")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            std::string cluster_name = input.at("cluster_name");
            return ToolResult::success(executor->get_cluster_analysis(cluster_name));
        } catch (const std::exception& e) {
            return ToolResult::failure(e.what());
        }
    }
};

// Region summary tool
class SummarizeRegionTool : public Tool {
public:
    using Tool::Tool;

    std::string name() const override {
        return "summarize_region";
    }

    std::string description() const override {
        return "Generate a summary of a memory region between two addresses. Understand larger code sections.";
    }

    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("start_addr", "Starting address of the region")
            .add_integer("end_addr", "Ending address of the region")
            .build();
    }

    ToolResult execute(const json& input) override {
        try {
            ea_t start_addr = ActionExecutor::parse_single_address_value(input.at("start_addr"));
            ea_t end_addr = ActionExecutor::parse_single_address_value(input.at("end_addr"));
            return ToolResult::success(executor->summarize_region(start_addr, end_addr));
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
            std::vector<std::pair<std::string, std::string> > analyses = deep_analysis_manager->list_analyses();

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

    void register_all_tools(std::shared_ptr<BinaryMemory> memory, std::shared_ptr<ActionExecutor> executor, bool enable_deep_analysis, std::shared_ptr<DeepAnalysisManager> deep_analysis_manager = nullptr ) {
        // Cross-reference tools
        register_tool_type<GetXrefsTool>(memory, executor);
        register_tool_type<GetXrefsFromTool>(memory, executor);

        // Function disassembly and decompilation tools
        register_tool_type<GetFunctionDisassemblyTool>(memory, executor);
        register_tool_type<GetFunctionDecompilationTool>(memory, executor);

        // Function name tools
        register_tool_type<GetFunctionAddressTool>(memory, executor);
        register_tool_type<GetFunctionNameTool>(memory, executor);
        register_tool_type<SetFunctionNameTool>(memory, executor);

        // Function reference tools
        register_tool_type<GetFunctionStringRefsTool>(memory, executor);
        register_tool_type<GetFunctionDataRefsTool>(memory, executor);

        // Data tools
        register_tool_type<GetDataNameTool>(memory, executor);
        register_tool_type<SetDataNameTool>(memory, executor);
        register_tool_type<GetDataTool>(memory, executor);

        // Comment tools
        register_tool_type<AddCommentTool>(memory, executor);
        register_tool_type<ClearCommentTool>(memory, executor);

        // Import/Export tools
        register_tool_type<GetImportsTool>(memory, executor);

        // String search tool
        register_tool_type<SearchStringsTool>(memory, executor);

        register_tool_type<GetNamedFunctionsTool>(memory, executor);
        register_tool_type<SearchNamedFunctionsTool>(memory, executor);
        register_tool_type<SearchNamedGlobalsTool>(memory, executor);
        register_tool_type<GetNamedGlobalsTool>(memory, executor);
        register_tool_type<GetStringsTool>(memory, executor);
        register_tool_type<GetEntryPointsTool>(memory, executor);



        // memory tools

        // Global note tools
        register_tool_type<SetGlobalNoteTool>(memory, executor);
        register_tool_type<GetGlobalNoteTool>(memory, executor);
        register_tool_type<ListGlobalNotesTool>(memory, executor);
        register_tool_type<SearchNotesTool>(memory, executor);

        // Function analysis tools
        register_tool_type<SetFunctionAnalysisTool>(memory, executor);
        register_tool_type<GetFunctionAnalysisTool>(memory, executor);

        // Memory context tool
        register_tool_type<GetMemoryContextTool>(memory, executor);

        // Analysis tracking tools
        register_tool_type<GetAnalyzedFunctionsTool>(memory, executor);
        register_tool_type<FindFunctionsByPatternTool>(memory, executor);

        // Exploration tools
        register_tool_type<GetExplorationFrontierTool>(memory, executor);
        register_tool_type<MarkForAnalysisTool>(memory, executor);
        register_tool_type<GetAnalysisQueueTool>(memory, executor);

        // Focus tool
        register_tool_type<SetCurrentFocusTool>(memory, executor);

        // Insight tools
        register_tool_type<AddInsightTool>(memory, executor);
        register_tool_type<GetInsightsTool>(memory, executor);

        // Cluster analysis tools
        register_tool_type<AnalyzeClusterTool>(memory, executor);
        register_tool_type<GetClusterAnalysisTool>(memory, executor);

        // Region summary tool
        register_tool_type<SummarizeRegionTool>(memory, executor);



        // special tools

        // Final report tool
        register_tool_type<SubmitFinalReportTool>(memory, executor);

        // Deep analysis
        if (enable_deep_analysis) {
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
