//
// Created by user on 6/30/25.
//

#ifndef TOOL_SYSTEM_H
#define TOOL_SYSTEM_H

#include "analysis/memory.h"
#include "analysis/actions.h"
#include "analysis/deep_analysis.h"
#include "patching/patch_manager.h"
#include "patching/code_injection_manager.h"
#include "core/config.h"
#include <fstream>
#include <filesystem>
#include <random>
#include <chrono>
#include <sstream>
#ifdef __NT__
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char **environ;
#endif

namespace llm_re::tools {

// Helper function to format address as hex (defined early so it can be used by all tools)
inline std::string HexAddress(ea_t addr) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << addr;
    return ss.str();
}

// IDA-specific base tool that extends the api Tool interface
class IDAToolBase : public claude::tools::Tool {
protected:
    std::shared_ptr<BinaryMemory> memory;
    std::shared_ptr<ActionExecutor> executor;

public:
    IDAToolBase(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec)
        : memory(mem), executor(exec) {}

    virtual ~IDAToolBase() = default;
};

// Unified search functions tool
class SearchFunctionsTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "search_functions";
    }

    std::string description() const override {
        return "Search for functions by name pattern. Can filter to only named functions and limit results. Returns address, name, and whether it's user-named.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("pattern", "Search pattern (substring match, case-insensitive). Empty for all functions", false)
            .add_boolean("named_only", "Only return user-named functions (exclude auto-generated names. defaults to true)", false)
            .add_integer("max_results", "Maximum number of results to return (defaults to 100)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string pattern = input.value("pattern", "");
            bool named_only = input.value("named_only", true);
            int max_results = input.value("max_results", 100);

            return claude::tools::ToolResult::success(executor->search_functions(pattern, named_only, max_results));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Unified search globals tool
class SearchGlobalsTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "search_globals";
    }

    std::string description() const override {
        return "Search for global variables/data by name pattern. Does NOT return defined structures / types. Returns address, name, value preview, and their type name. Excludes auto-generated names by default.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("pattern", "Search pattern (substring match, case-insensitive). Empty for all globals", false)
            .add_integer("max_results", "Maximum number of results to return (defaults to 100)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string pattern = input.value("pattern", "");
            int max_results = input.value("max_results", 100);

            return claude::tools::ToolResult::success(executor->search_globals(pattern, max_results));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Unified search strings tool
class SearchStringsTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "search_strings";
    }

    std::string description() const override {
        return "Search for strings in the binary. Can filter by content pattern and minimum length. Returns address and content.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("pattern", "Search pattern (substring match, case-insensitive). Empty for all strings", false)
            .add_integer("min_length", "Minimum string length (defaults to 5)", false)
            .add_integer("max_results", "Maximum number of results to return (defaults to 100)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string pattern = input.value("pattern", "");
            int min_length = input.value("min_length", 5);
            int max_results = input.value("max_results", 100);

            return claude::tools::ToolResult::success(executor->search_strings(pattern, min_length, max_results));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Get comprehensive function info tool
class GetFunctionInfoTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "get_function_info";
    }

    std::string description() const override {
        return "Get comprehensive information about a function including name, bounds, cross-references counts, and reference counts. Fast overview without disassembly/decompilation.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "The address of the function")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return claude::tools::ToolResult::success(executor->get_function_info(address));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Get comprehensive data info tool
class GetDataInfoTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "get_data_info";
    }

    std::string description() const override {
        return "Get comprehensive information about data including name, value, type, and cross-references. Provides complete data context.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "The address of the data")
            .add_integer("max_xrefs", "Maximum cross-references to return (defaults to 20)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            int max_xrefs = input.value("max_xrefs", 20);
            return claude::tools::ToolResult::success(executor->get_data_info(address, max_xrefs));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Dump data tool
class DumpDataTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "dump_data";
    }

    std::string description() const override {
        return "Dump memory data at the given address in hexadecimal format. Use this if get_data_info isn't returning the expected information for a global due to it lacking a type. Returns hex dump with ASCII representation.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "The starting address to dump")
            .add_integer("size", "Number of bytes to dump (max 65536)")
            .add_integer("bytes_per_line", "Bytes per line in the dump (defaults to 16)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            int size = input.at("size");
            int bytes_per_line = input.value("bytes_per_line", 16);

            if (size <= 0 || size > 65536) {
                return claude::tools::ToolResult::failure("Size must be between 1 and 65536 bytes");
            }

            if (bytes_per_line <= 0 || bytes_per_line > 32) {
                return claude::tools::ToolResult::failure("Bytes per line must be between 1 and 32");
            }

            return claude::tools::ToolResult::success(executor->dump_data(address, size, bytes_per_line));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Analyze function tool (replaces decompilation/disassembly tools)
class AnalyzeFunctionTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "analyze_function";
    }

    std::string description() const override {
        return "Deep dive into a function with optional disassembly and decompilation (Includes cross-references, strings, data refs as well). "
               "Disassembly includes address prefixes (e.g., '0x401000: mov eax, [ebp+8]') for precise instruction identification. "
               "This is your primary tool for understanding code. As you analyze, consider: "
               "What would make this function clear to another reverse engineer? "
               "What names, types, and comments would tell its story? "
               "Note, decompilation can be incorrect! If something doesn't make sense (ex: decompilation is empty or appears incomplete), check the disassembly! "
               "Use the decompilation to get the idea for the function, and then use disassembly if you need the specifics. The disassembly has what is ACTUALLY happening, but is more expensive "
               "If the decompilation looks like a NOP, it PROBABLY IS NOT. CHECK WITH THE DISASSEMBLY!";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "The address of the function")
            .add_boolean("include_disasm", "Include disassembly (defaults to false)", false)
            .add_boolean("include_decomp", "Include decompilation (defaults to true)", false)
            .add_integer("max_xrefs", "Maximum cross-references to include (defaults to 20)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            bool include_disasm = input.value("include_disasm", false);
            bool include_decomp = input.value("include_decomp", true);
            int max_xrefs = input.value("max_xrefs", 20);

            return claude::tools::ToolResult::success(executor->analyze_function(address, include_disasm, include_decomp, max_xrefs));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Store analysis tool (unified knowledge storage)
class StoreAnalysisTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "store_analysis";
    }

    std::string description() const override {
        return "Your PRIVATE (will not be shown to ANYONE) detective's notebook for the investigation. Store hypotheses, patterns, questions, "  // analyses will be provided to grader also
               "observations that might make sense later, TODOs, and connections you're still exploring. "
               "This is your thinking space - for permanent findings, use IDA's annotation tools "
               "(set_name for functions/data, set_comment for logic/discoveries, set_local_type for structures).";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("key", "Unique key for this analysis")
            .add_string("content", "The analysis content")
            .add_string("type", "Type of analysis: note, finding, hypothesis, question, analysis (analysis is for analyzing a specific function)")
            .add_integer("address", "Associated address (optional)", false)
            .add_array("related_addresses", "integer", "Additional related addresses", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
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

            return claude::tools::ToolResult::success(executor->store_analysis(key, content, address, type, related_addresses));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Get analysis tool
class GetAnalysisTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

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
        return claude::tools::ParameterBuilder()
            .add_string("key", "Specific key to retrieve", false)
            .add_integer("address", "Find analysis related to this address", false)
            .add_string("type", "Filter by type (note, finding, hypothesis, question, analysis)", false)
            .add_string("pattern", "Search pattern in content", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string key = input.value("key", "");
            std::optional<ea_t> address;
            if (input.contains("address")) {
                address = ActionExecutor::parse_single_address_value(input.at("address"));
            }
            std::string type = input.value("type", "");
            std::string pattern = input.value("pattern", "");

            return claude::tools::ToolResult::success(executor->get_analysis(key, address, type, pattern));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Cross-reference tool
class GetXrefsTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "get_xrefs";
    }

    std::string description() const override {
        return "Get cross-references to AND from an address. Shows what calls this and what this calls. Essential for understanding code relationships.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "The address to get xrefs for")
            .add_integer("max_results", "Maximum xrefs per direction (defaults to 100)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            int max_results = input.value("max_results", 100);

            return claude::tools::ToolResult::success(executor->get_xrefs(address, max_results));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Set name tool (functions + data)
class SetNameTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

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
        return claude::tools::ParameterBuilder()
            .add_integer("address", "The address to name")
            .add_string("name", "The new name. Do not provide reserved names such as word_401000.")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string name = input.at("name");

            return claude::tools::ToolResult::success(executor->set_name(address, name));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Comment tool
class SetCommentTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

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
        return claude::tools::ParameterBuilder()
            .add_integer("address", "The address for the comment")
            .add_string("comment", "The comment text (empty to clear)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string comment = input.value("comment", "");

            return claude::tools::ToolResult::success(executor->set_comment(address, comment));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Get imports tool
class GetImportsTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "get_imports";
    }

    std::string description() const override {
        return "Get all imported functions and libraries. Shows external dependencies of the binary.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("max_results", "Maximum imports to return (defaults to 100)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            int max_results = input.value("max_results", 100);
            return claude::tools::ToolResult::success(executor->get_imports(max_results));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Get entry points tool
class GetEntryPointsTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "get_exports";
    }

    std::string description() const override {
        return "Get all exports of the binary (entry points, exports, TLS callbacks). Shows where execution can begin.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("max_count", "Max number of exports to return")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            return claude::tools::ToolResult::success(executor->get_exports(input.at("max_count")));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Function prototype tools
class GetFunctionPrototypeTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "get_function_prototype";
    }

    std::string description() const override {
        return "Get the function prototype including return type, name, and parameters. Shows the current decompiled signature.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "The function address")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return claude::tools::ToolResult::success(executor->get_function_prototype(address));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

class SetFunctionPrototypeTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "set_function_prototype";
    }

    std::string description() const override {
        return "Set the complete function signature including return type, calling convention, and parameters. "
               "Use this when you need to change the overall function type or multiple parameters at once. "
               "For individual parameter/variable updates, use set_variable instead. "
               "Accepts standard C declaration syntax (e.g., 'int __stdcall ProcessData(void *buffer, int size)' or 'BOOL func(HWND, UINT, WPARAM, LPARAM)'). "
               "Important: Ensure type sizes are correct for the target architecture to avoid decompilation issues.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "The function address")
            .add_string("prototype", "Full C-style function prototype with or without argument names")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string prototype = input.at("prototype");
            return claude::tools::ToolResult::success(executor->set_function_prototype(address, prototype));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Local type tools
class SearchLocalTypesTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

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
        return claude::tools::ParameterBuilder()
            .add_string("pattern", "Search pattern (substring match, case-insensitive). Empty for all types", false)
            .add_string("type_kind", "Filter by kind: struct, union, enum, typedef, any (defaults to any)", false)
            .add_integer("max_results", "Maximum results (defaults to 50)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string pattern = input.value("pattern", "");
            std::string type_kind = input.value("type_kind", "any");
            int max_results = input.value("max_results", 50);
            return claude::tools::ToolResult::success(executor->search_local_types(pattern, type_kind, max_results));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

class GetLocalTypeTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "get_local_type";
    }

    std::string description() const override {
        return "Get the full C definition of a local type by name. Shows the complete struct/union/enum declaration.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("type_name", "Name of the type to retrieve")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string type_name = input.at("type_name");
            return claude::tools::ToolResult::success(executor->get_local_type(type_name));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

class SetLocalTypeTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

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
        return claude::tools::ParameterBuilder()
            .add_string("definition", "C-style type definition (e.g., 'struct Point { int x; int y; };'). Only define one struct per set_local_type tool call")
            .add_boolean("replace_existing", "Replace if type already exists (defaults to true)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string definition = input.at("definition");
            bool replace_existing = input.value("replace_existing", true);
            return claude::tools::ToolResult::success(executor->set_local_type(definition, replace_existing));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Local variable tools
class GetVariablesTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "get_variables";
    }

    std::string description() const override {
        return "Get all variables in a function - both arguments and locals. "
               "Shows their current names, types, and locations (stack offset or register). "
               "Use this to see what variables need better names or correct types.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "The function address")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            return claude::tools::ToolResult::success(executor->get_variables(address));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

class SetVariableTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "set_variable";
    }

    std::string description() const override {
        return "Update local variables in a function. Give them meaningful names and/or correct types. "
               "Transform 'v1' into 'packetLength', 'v2' into 'responseBuffer'. "
               "IMPORTANT: This tool only works for local variables (v1, v2, etc.), NOT function arguments. "
               "To modify function arguments (a1, a2, etc.), use set_function_prototype instead. "
               "Well-named variables make function logic self-documenting.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "The function address")
            .add_string("variable_name", "Current local variable name (e.g., 'v1', 'v2', or existing local var name)")
            .add_string("new_name", "New variable name", false)
            .add_string("new_type", "New type (e.g., 'SOCKET', 'char*', 'MY_STRUCT')", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string variable_name = input.at("variable_name");
            std::string new_name = input.value("new_name", "");
            std::string new_type = input.value("new_type", "");
            return claude::tools::ToolResult::success(executor->set_variable(address, variable_name, new_name, new_type));
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};


// Deep analysis collection tools
class StartDeepAnalysisCollectionTool : public IDAToolBase {
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager;

public:
    StartDeepAnalysisCollectionTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<DeepAnalysisManager> dam) : IDAToolBase(mem, exec), deep_analysis_manager(dam) {}

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
               "This will delegate to the grader model at SIGNIFICANT cost.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("topic", "A descriptive name for the complex system/task being analyzed")
            .add_string("description", "Detailed description of what makes this task complex and why deep analysis is needed")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string topic = input.at("topic");
            std::string description = input.at("description");

            deep_analysis_manager->start_collection(topic, description);

            json result;
            result["success"] = true;
            result["message"] = "Started deep analysis collection for: " + topic;
            result["warning"] = "Remember to add relevant functions and observations (add_to_deep_analysis) before requesting analysis";

            return claude::tools::ToolResult::success(result);
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

class AddToDeepAnalysisTool : public IDAToolBase {
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager;

public:
    AddToDeepAnalysisTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<DeepAnalysisManager> dam) : IDAToolBase(mem, exec), deep_analysis_manager(dam) {}

    std::string name() const override {
        return "add_to_deep_analysis";
    }

    std::string description() const override {
        return "Add observations, findings, or function addresses to the current deep analysis collection. "
               "Call this as you discover relevant information about the complex system you're analyzing. "
               "It is ABSOLUTELY CRITICAL to add relevant functions using the function_address parameter. "
               "The grader model will only receive function information for functions that you explicitly provide in this parameter. ";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("key", "A descriptive key for this piece of information")
            .add_string("value", "The observation, finding, or analysis to store", false)
            .add_integer("function_address", "Address of a related function to include in deep analysis. Expected to be formatted as: [ADDR, ADDR] or plainly as ADDR. Do NOT wrap the square brackets with quotes.", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            if (!deep_analysis_manager->has_active_collection()) {
                return claude::tools::ToolResult::failure("No active deep analysis collection. Call start_deep_analysis_collection first.");
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

            return claude::tools::ToolResult::success(result);
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

class RequestDeepAnalysisTool : public IDAToolBase {
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager;

public:
    RequestDeepAnalysisTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<DeepAnalysisManager> dam) : IDAToolBase(mem, exec), deep_analysis_manager(dam) {}

    std::string name() const override {
        return "request_deep_analysis";
    }

    std::string description() const override {
        return "VERY EXPENSIVE - Send the collected information to Opus 4 for deep expert analysis. "  // not necessarily opus 4, but we can let the model think that
               "This will include all collected data, memory contents, and full decompilations. "
               "Only use after collecting sufficient information. Each analysis is expensive. "
               "The analysis will be stored and can be retrieved later with get_deep_analysis.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("task", "Specific analysis task or questions for the grader model to address")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string task = input.at("task");

            if (!deep_analysis_manager->has_active_collection()) {
                return claude::tools::ToolResult::failure("No active deep analysis collection to analyze");
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

            return claude::tools::ToolResult::success(response);
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

class ListDeepAnalysesTool : public IDAToolBase {
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager;

public:
    ListDeepAnalysesTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<DeepAnalysisManager> dam) : IDAToolBase(mem, exec), deep_analysis_manager(dam) {}

    std::string name() const override {
        return "list_deep_analyses";
    }

    std::string description() const override {
        return "List all completed deep analyses with their keys and descriptions. "
               "Use this to see what complex systems have been analyzed by the grader model.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder().build();
    }

    claude::tools::ToolResult execute(const json& input) override {
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

            return claude::tools::ToolResult::success(result);
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

class GetDeepAnalysisTool : public IDAToolBase {
    std::shared_ptr<DeepAnalysisManager> deep_analysis_manager;

public:
    GetDeepAnalysisTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<DeepAnalysisManager> dam) : IDAToolBase(mem, exec), deep_analysis_manager(dam) {}

    std::string name() const override {
        return "get_deep_analysis";
    }

    std::string description() const override {
        return "Retrieve a completed deep analysis by its key. "
               "Returns the full expert analysis from the grader model for the specified complex system.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("key", "The analysis key (from list_deep_analyses or request_deep_analysis)")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string key = input.at("key");

            std::optional<DeepAnalysisResult> analysis_opt = deep_analysis_manager->get_analysis(key);
            if (!analysis_opt) {
                return claude::tools::ToolResult::failure("Deep analysis not found with key: " + key);
            }

            DeepAnalysisResult& analysis = *analysis_opt;

            json result;
            result["success"] = true;
            result["key"] = analysis.key;
            result["topic"] = analysis.topic;
            result["task"] = analysis.task_description;
            result["analysis"] = analysis.analysis;

            return claude::tools::ToolResult::success(result);
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

class RunPythonTool : public IDAToolBase {
public:
    using IDAToolBase::IDAToolBase;

    std::string name() const override {
        return "run_python";
    }

    std::string description() const override {
        return "Execute Python code whatever task you deem necessary. Use this to perform computation you couldn't have done yourself. "
               "IMPORTANT: Use ONLY Python standard library - no external packages. "
               "EXTREMELY IMPORTANT: **this tool IS EXPENSIVE!!** ONLY USE THIS TOOL WHEN IT WILL GREATLY ENHANCE YOUR ABILITIES. Do NOT WASTE IT. "  // it's not expensive, but the LLM like to run python and have it print out its reasoning, which i don't want it doing
               "BE VERY CAREFUL WITH WHAT YOU DO HERE! If you aren't careful, it will flood your context window with useless information! Make sure you know EXACTLY what you are doing! "
               "NEVER perform network operations (not needed for RE tasks).";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("code", "Python code to execute (standard library only)")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string python_code = input.at("code");

            // Create temp directory if it doesn't exist
            std::filesystem::path temp_dir = "/tmp/agent_python";
            if (!std::filesystem::exists(temp_dir)) {
                std::filesystem::create_directories(temp_dir);
            }

            // Generate random filename
            auto now = std::chrono::system_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(1000, 9999);

            std::string filename = std::format("script_{}_{}.py", timestamp, dis(gen));
            std::filesystem::path script_path = temp_dir / filename;

            // Write Python code to file
            std::ofstream script_file(script_path);
            if (!script_file.is_open()) {
                return claude::tools::ToolResult::failure("Failed to create temporary Python file");
            }
            script_file << python_code;
            script_file.close();

            // Execute Python script and capture output
            std::string output = execute_python_script(script_path.string());

            // Clean up temp file
            std::filesystem::remove(script_path);

            json result;
            result["output"] = output;

            return claude::tools::ToolResult::success(result);

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(std::string("Exception: ") + e.what());
        }
    }

private:
    std::string execute_python_script(const std::string& script_path) {
#ifdef __NT__
        return execute_windows_python(script_path);
#else
        return execute_unix_python(script_path);
#endif
    }

#ifndef __NT__
    std::string execute_unix_python(const std::string& script_path) {
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            return "Error: Failed to create pipe";
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipefd[0]);
        posix_spawn_file_actions_addclose(&actions, pipefd[1]);

        const char* python_cmd = "python3";
        std::vector<char*> argv = {
            const_cast<char*>(python_cmd),
            const_cast<char*>(script_path.c_str()),
            nullptr
        };

        pid_t pid;
        int result = posix_spawn(&pid, "/usr/bin/python3", &actions, nullptr, argv.data(), environ);

        close(pipefd[1]);
        posix_spawn_file_actions_destroy(&actions);

        if (result != 0) {
            close(pipefd[0]);
            // Try with just "python" if python3 fails
            if (pipe(pipefd) == -1) {
                return "Error: Failed to create pipe";
            }

            posix_spawn_file_actions_init(&actions);
            posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
            posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDERR_FILENO);
            posix_spawn_file_actions_addclose(&actions, pipefd[0]);
            posix_spawn_file_actions_addclose(&actions, pipefd[1]);

            python_cmd = "python";
            argv[0] = const_cast<char*>(python_cmd);
            result = posix_spawn(&pid, "/usr/bin/python", &actions, nullptr, argv.data(), environ);

            close(pipefd[1]);
            posix_spawn_file_actions_destroy(&actions);

            if (result != 0) {
                close(pipefd[0]);
                return std::format("Error: Failed to execute Python (tried python3 and python): {}", strerror(result));
            }
        }

        // Read output with size limit
        std::string output;
        char buffer[4096];
        ssize_t bytes_read;
        const size_t MAX_OUTPUT_SIZE = 10000;

        while ((bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            output += buffer;

            // Check if output is getting too large
            if (output.size() > MAX_OUTPUT_SIZE) {
                // Consume remaining output to prevent pipe blocking
                while (read(pipefd[0], buffer, sizeof(buffer) - 1) > 0) {
                    // Just discard it
                }
                break;
            }
        }
        close(pipefd[0]);

        // Wait for process to complete using IDA's thread-safe qwait
        int status;
        qwait(&status, pid, 0);

        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
            output = "Python execution failed with exit code " + std::to_string(WEXITSTATUS(status)) + "\n" + output;
        }

        // Add trimming message if output was truncated
        if (output.size() >= MAX_OUTPUT_SIZE) {
            output += "\n\n[OUTPUT TRUNCATED: Output exceeded " + std::to_string(MAX_OUTPUT_SIZE) + " characters and was trimmed]";
        }

        return output.empty() ? "(no output)" : output;
    }
#else
    std::string execute_windows_python(const std::string& script_path) {
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        HANDLE hReadPipe, hWritePipe;
        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
            return "Error: Failed to create pipe";
        }

        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOA si = {0};
        si.cb = sizeof(STARTUPINFOA);
        si.hStdOutput = hWritePipe;
        si.hStdError = hWritePipe;
        si.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi = {0};

        std::string command = "python \"" + script_path + "\"";

        if (!CreateProcessA(NULL, const_cast<char*>(command.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
            CloseHandle(hWritePipe);
            CloseHandle(hReadPipe);

            // Try python3 if python fails
            command = "python3 \"" + script_path + "\"";
            if (!CreateProcessA(NULL, const_cast<char*>(command.c_str()), NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
                return "Error: Failed to execute Python (tried python and python3)";
            }
        }

        CloseHandle(hWritePipe);

        // Read output with size limit
        std::string output;
        char buffer[4096];
        DWORD bytes_read;
        const size_t MAX_OUTPUT_SIZE = 10000;

        while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
            buffer[bytes_read] = '\0';
            output += buffer;

            // Check if output is getting too large
            if (output.size() > MAX_OUTPUT_SIZE) {
                // Consume remaining output to prevent pipe blocking
                while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
                    // Just discard it
                }
                break;
            }
        }

        CloseHandle(hReadPipe);

        // Wait for process to complete
        WaitForSingleObject(pi.hProcess, INFINITE);

        DWORD exit_code;
        GetExitCodeProcess(pi.hProcess, &exit_code);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (exit_code != 0) {
            output = "Python execution failed with exit code " + std::to_string(exit_code) + "\n" + output;
        }

        // Add trimming message if output was truncated
        if (output.size() >= MAX_OUTPUT_SIZE) {
            output += "\n\n[OUTPUT TRUNCATED: Output exceeded " + std::to_string(MAX_OUTPUT_SIZE) + " characters and was trimmed]";
        }

        return output.empty() ? "(no output)" : output;
    }
#endif
};


// Patch bytes tool
class PatchBytesTool : public IDAToolBase {
    std::shared_ptr<PatchManager> patch_manager_;

public:
    PatchBytesTool(std::shared_ptr<BinaryMemory> mem, std::shared_ptr<ActionExecutor> exec, std::shared_ptr<PatchManager> pm) : IDAToolBase(mem, exec), patch_manager_(pm) {}

    std::string name() const override {
        return "patch_bytes";
    }

    std::string description() const override {
        return "⚠️ EXTREMELY DANGEROUS - Patch raw bytes at a specific address. Before using this, ask yourself, can you accomplish this with patch_assembly? If you can, use patch_assembly, if you can't, use patch_bytes. "
               "CRITICAL: You MUST be 100% certain about your patch before using this tool! "
               "MANDATORY: Verify original bytes match EXACTLY before patching. "
               "WARNING: If new_bytes length > original_bytes length, YOU WILL OVERWRITE adjacent data/code! "
               "DANGER: Overwriting beyond intended boundaries can corrupt instructions, data structures, or critical code. "
               "ALWAYS: 1) Check instruction boundaries, 2) Verify patch size, 3) Understand what follows the patch location. "
               "This tool modifies the binary permanently - mistakes can break the entire program!";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "Target address to patch - MUST be exact start of instruction/data")
            .add_string("original_bytes", "CRITICAL: Original bytes for verification - MUST match exactly or patch will fail (hex format)")
            .add_string("new_bytes", "⚠️ New bytes to write - WARNING: If longer than original, WILL OVERWRITE adjacent memory!")
            .add_string("description", "REQUIRED: Detailed explanation of patch purpose and why it's safe (for audit trail)")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        if (!patch_manager_) {
            return claude::tools::ToolResult::failure("Patch manager not initialized");
        }

        try {
            // Parse and validate parameters
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string original_hex = input.at("original_bytes");
            std::string new_hex = input.at("new_bytes");
            std::string description = input.at("description");  // Required for audit trail

            if (description.empty()) {
                return claude::tools::ToolResult::failure("Description is required for audit trail");
            }

            // Apply the byte patch with verification
            BytePatchResult patch_result = patch_manager_-> apply_byte_patch(address, original_hex, new_hex, description);

            if (patch_result.success) {
                json data;
                data["address"] = HexAddress(address);
                data["original_bytes"] = original_hex;
                data["new_bytes"] = new_hex;
                data["bytes_patched"] = patch_result.bytes_patched;
                data["description"] = description;
                data["timestamp"] = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                return claude::tools::ToolResult::success(data);
            } else {
                return claude::tools::ToolResult::failure(patch_result.error_message);
            }

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(std::string("Exception: ") + e.what());
        }
    }
};

// Patch assembly tool
class PatchAssemblyTool : public IDAToolBase {
    std::shared_ptr<PatchManager> patch_manager_;

public:
    PatchAssemblyTool(std::shared_ptr<BinaryMemory> mem,
                      std::shared_ptr<ActionExecutor> exec,
                      std::shared_ptr<PatchManager> pm)
        : IDAToolBase(mem, exec), patch_manager_(pm) {}

    std::string name() const override {
        return "patch_assembly";
    }

    std::string description() const override {
        return "⚠️ EXTREMELY DANGEROUS - Patch assembly instructions at a specific address. "
               "Use addresses from analyze_function's disassembly output (e.g., '0x401000: mov eax, [ebp+8]' means address 0x401000). "
               "CRITICAL: You MUST be 100% certain about your patch before using this tool! "
               "MANDATORY: Verify original assembly matches EXACTLY before patching. "
               "WARNING: If assembled bytes > original instruction size, YOU WILL OVERWRITE following instructions! "
               "DANGER: Overwriting adjacent instructions can break control flow, corrupt function logic, or crash the program. "
               "ALWAYS: 1) Analyze surrounding instructions, 2) Check assembled size vs original, 3) Understand code flow impact. "
               "NOTE: Tool adds NOPs only if new instruction is SMALLER - it will NOT prevent overwriting if larger! "
               "This tool modifies the binary permanently - incorrect patches can destroy program functionality!";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "Target instruction address - MUST be exact start of instruction")
            .add_string("original_asm", "CRITICAL: Original assembly for verification - MUST match exactly or patch will fail")
            .add_string("new_asm", "⚠️ New assembly - WARNING: If assembled size > original, WILL DESTROY following instructions!")
            .add_string("description", "REQUIRED: Detailed explanation of patch purpose and safety analysis (for audit trail)")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        if (!patch_manager_) {
            return claude::tools::ToolResult::failure("Patch manager not initialized");
        }

        try {
            // Parse and validate parameters
            ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
            std::string original_asm = input.at("original_asm");
            std::string new_asm = input.at("new_asm");
            std::string description = input.at("description");  // Required for audit trail

            if (description.empty()) {
                return claude::tools::ToolResult::failure("Description is required for audit trail");
            }

            // Apply the assembly patch with verification
            AssemblyPatchResult patch_result = patch_manager_->apply_assembly_patch(address, original_asm, new_asm, description);

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
                return claude::tools::ToolResult::success(data);
            } else {
                return claude::tools::ToolResult::failure(patch_result.error_message);
            }

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(std::string("Exception: ") + e.what());
        }
    }
};

// Revert patches tool
class RevertPatchTool : public IDAToolBase {
    std::shared_ptr<PatchManager> patch_manager_;

public:
    RevertPatchTool(std::shared_ptr<BinaryMemory> mem,
                    std::shared_ptr<ActionExecutor> exec,
                    std::shared_ptr<PatchManager> pm)
        : IDAToolBase(mem, exec), patch_manager_(pm) {}

    std::string name() const override {
        return "revert_patch";
    }

    std::string description() const override {
        return "Revert a previously applied patch at a specific address or revert all patches. "
               "Restores original bytes from before the patch was applied.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "Address of patch to revert", false)
            .add_boolean("revert_all", "Revert all patches", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        if (!patch_manager_) {
            return claude::tools::ToolResult::failure("Patch manager not initialized");
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
                return claude::tools::ToolResult::failure("Must specify address or revert_all");
            }

            if (!success) {
                return claude::tools::ToolResult::failure("No patch found at specified address");
            }

            return claude::tools::ToolResult::success(data);

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(std::string("Exception: ") + e.what());
        }
    }
};

// List patches tool
class ListPatchesTool : public IDAToolBase {
    std::shared_ptr<PatchManager> patch_manager_;

public:
    ListPatchesTool(std::shared_ptr<BinaryMemory> mem,
                    std::shared_ptr<ActionExecutor> exec,
                    std::shared_ptr<PatchManager> pm)
        : IDAToolBase(mem, exec), patch_manager_(pm) {}

    std::string name() const override {
        return "list_patches";
    }

    std::string description() const override {
        return "List all applied patches with their descriptions, timestamps, and original/new bytes. "
               "Shows the complete audit trail of all modifications.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("address", "List only patch at specific address", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        if (!patch_manager_) {
            return claude::tools::ToolResult::failure("Patch manager not initialized");
        }

        try {
            json data;
            json patches_json = json::array();

            if (input.contains("address")) {
                // Get single patch
                ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
                std::optional<PatchInfo> patch_info = patch_manager_->get_patch_info(address);

                if (patch_info.has_value()) {
                    const PatchInfo& patch = patch_info.value();
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
                std::vector<PatchInfo> all_patches = patch_manager_->list_patches();

                for (const PatchInfo &patch: all_patches) {
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
            PatchStatistics stats = patch_manager_->get_statistics();
            data["statistics"]["total_patches"] = stats.total_patches;
            data["statistics"]["assembly_patches"] = stats.assembly_patches;
            data["statistics"]["byte_patches"] = stats.byte_patches;
            data["statistics"]["total_bytes_patched"] = stats.total_bytes_patched;

            return claude::tools::ToolResult::success(data);

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(std::string("Exception: ") + e.what());
        }
    }
};

class AllocateCodeWorkspaceTool : public IDAToolBase {
    std::shared_ptr<CodeInjectionManager> code_injection_manager_;

public:
    AllocateCodeWorkspaceTool(std::shared_ptr<BinaryMemory> mem,
                              std::shared_ptr<ActionExecutor> exec,
                              std::shared_ptr<CodeInjectionManager> cim)
        : IDAToolBase(mem, exec), code_injection_manager_(cim) {}

    std::string name() const override {
        return "allocate_code_workspace";
    }

    std::string description() const override {
        return "Allocate a TEMPORARY workspace in IDA for developing code injections. "
               "CRITICAL: This creates a segment that exists ONLY in the IDA database for development. "
               "The returned address (0xXXXXXXXX) is TEMPORARY and WILL NEED TO BE RELOCATED when finalized. "
               "You are using IDA as an IDE to iteratively develop your assembly code. "
               "IMPORTANT: Track ALL references to this temporary address - you MUST update them after relocation! "
               "Request 2x the size you think you need - it's better to overestimate. "
               "After developing your code (using patching), you MUST call preview_code_injection then finalize_code_injection.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("size_bytes", "Estimated size needed in bytes (will be increased by 50% automatically)")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        if (!code_injection_manager_) {
            return claude::tools::ToolResult::failure("Code injection manager not initialized");
        }

        try {
            size_t size = input.at("size_bytes");

            if (size == 0 || size > 0x100000) {  // Max 1MB
                return claude::tools::ToolResult::failure("Size must be between 1 and 1048576 bytes");
            }

            WorkspaceAllocation result = code_injection_manager_->allocate_code_workspace(size);

            if (result.success) {
                json data;
                data["success"] = true;
                data["temp_address"] = HexAddress(result.temp_segment_ea);
                data["allocated_size"] = result.allocated_size;
                data["segment_name"] = result.segment_name;
                data["warning"] = "REMEMBER: This address is TEMPORARY and will change! Track all references!";
                data["next_steps"] = "Use patch_bytes or patch_assembly to develop code at this address, "
                                    "then preview_code_injection and finalize_code_injection when done.";
                return claude::tools::ToolResult::success(data);
            } else {
                return claude::tools::ToolResult::failure(result.error_message);
            }

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(std::string("Exception: ") + e.what());
        }
    }
};

class PreviewCodeInjectionTool : public IDAToolBase {
    std::shared_ptr<CodeInjectionManager> code_injection_manager_;

public:
    PreviewCodeInjectionTool(std::shared_ptr<BinaryMemory> mem,
                            std::shared_ptr<ActionExecutor> exec,
                            std::shared_ptr<CodeInjectionManager> cim)
        : IDAToolBase(mem, exec), code_injection_manager_(cim) {}

    std::string name() const override {
        return "preview_code_injection";
    }

    std::string description() const override {
        return "⚠️ MANDATORY before finalization - Preview the code you've developed in your temporary workspace. "
               "This tool shows the final assembly that will be relocated and injected into the binary. "
               "CRITICAL: You MUST call this before finalize_code_injection or finalization will fail! "
               "Review the disassembly carefully - after finalization, this code becomes permanent. "
               "The preview validates that your code is complete and ready for relocation.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("start_address", "Start address of your code in the temp workspace")
            .add_integer("end_address", "End address (exclusive) of your code in the temp workspace")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        if (!code_injection_manager_) {
            return claude::tools::ToolResult::failure("Code injection manager not initialized");
        }

        try {
            ea_t start = ActionExecutor::parse_single_address_value(input.at("start_address"));
            ea_t end = ActionExecutor::parse_single_address_value(input.at("end_address"));

            CodePreviewResult result = code_injection_manager_->preview_code_injection(start, end);

            if (result.success) {
                json data;
                data["success"] = true;
                data["start_address"] = HexAddress(result.start_ea);
                data["end_address"] = HexAddress(result.end_ea);
                data["code_size"] = result.code_size;
                data["disassembly"] = result.disassembly;
                data["bytes_hex"] = bytes_to_hex_string(result.final_bytes);
                data["ready_to_finalize"] = true;
                data["next_step"] = "Call finalize_code_injection with the same start/end addresses";
                return claude::tools::ToolResult::success(data);
            } else {
                return claude::tools::ToolResult::failure(result.error_message);
            }

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(std::string("Exception: ") + e.what());
        }
    }

private:
    static std::string bytes_to_hex_string(const std::vector<uint8_t>& bytes) {
        std::stringstream ss;
        for (uint8_t b : bytes) {
            ss << std::hex << std::setfill('0') << std::setw(2) << (int)b;
        }
        return ss.str();
    }
};

class FinalizeCodeInjectionTool : public IDAToolBase {
    std::shared_ptr<CodeInjectionManager> code_injection_manager_;

public:
    FinalizeCodeInjectionTool(std::shared_ptr<BinaryMemory> mem,
                              std::shared_ptr<ActionExecutor> exec,
                              std::shared_ptr<CodeInjectionManager> cim)
        : IDAToolBase(mem, exec), code_injection_manager_(cim) {}

    std::string name() const override {
        return "finalize_code_injection";
    }

    std::string description() const override {
        return "⚠️ PERMANENT OPERATION - Finalize your code injection and relocate it to a permanent location. "
               "This will: 1) Find a code cave or create a new segment, 2) Copy your code there, "
               "3) Delete the temporary workspace, 4) Apply changes to the actual binary file. "
               "CRITICAL: You MUST have called preview_code_injection first with these exact addresses! "
               "IMPORTANT: After this succeeds, you MUST call list_patches and update ALL references to the old address! "
               "The tool will remind you to update ALL references - you must track and fix them! ";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("start_address", "Start address (must match preview)")
            .add_integer("end_address", "End address (must match preview)")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        if (!code_injection_manager_) {
            return claude::tools::ToolResult::failure("Code injection manager not initialized");
        }

        try {
            ea_t start = ActionExecutor::parse_single_address_value(input.at("start_address"));
            ea_t end = ActionExecutor::parse_single_address_value(input.at("end_address"));

            CodeFinalizationResult result = code_injection_manager_->finalize_code_injection(start, end);

            if (result.success) {
                json data;
                data["success"] = true;
                data["old_temp_address"] = HexAddress(result.old_temp_address);
                data["new_permanent_address"] = HexAddress(result.new_permanent_address);
                data["code_size"] = result.code_size;
                data["relocation_method"] = result.relocation_method;

                // Critical instructions for the LLM
                data["critical_action_required"] = "UPDATE ALL REFERENCES TO THE OLD ADDRESS!";

                // Build step strings before adding to array
                std::string step2 = std::string("2. Review each patch for any references to ") + HexAddress(result.old_temp_address);
                std::string step3 = std::string("3. Update any patches containing the old address ") + HexAddress(result.old_temp_address) +
                                   std::string(" to use the new address ") + HexAddress(result.new_permanent_address);

                data["next_steps"] = json::array();
                data["next_steps"].push_back("1. Call list_patches to see all your patches");
                data["next_steps"].push_back(step2);
                data["next_steps"].push_back(step3);
                data["next_steps"].push_back("4. This includes JMP, CALL, MOV, LEA or any instruction referencing the old address, or ANYTHING you patched to be offset from the old temporary address.");
                data["warning"] = "The code will NOT work correctly until you update all address references!";
                data["old_address_to_find"] = HexAddress(result.old_temp_address);
                data["new_address_to_use"] = HexAddress(result.new_permanent_address);

                data["message"] = "Code successfully relocated, examine the code and make sure it was done correctly or if you need to do anything to it. YOU MUST NOW UPDATE ALL PATCHES REFERENCING THE OLD ADDRESS OR THAT CODE *WILL CAUSE A CRASH*!";
                return claude::tools::ToolResult::success(data);
            } else {
                return claude::tools::ToolResult::failure(result.error_message);
            }

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(std::string("Exception: ") + e.what());
        }
    }
};

inline void register_ida_tools(claude::tools::ToolRegistry& registry,
                               std::shared_ptr<BinaryMemory> memory,
                               std::shared_ptr<ActionExecutor> executor,
                               std::shared_ptr<DeepAnalysisManager> deep_analysis_manager,
                               std::shared_ptr<PatchManager> patch_manager,
                               std::shared_ptr<CodeInjectionManager> code_injection_manager,
                               const Config& config) {
    // Core navigation and info tools
    registry.register_tool_type<GetXrefsTool>(memory, executor);
    registry.register_tool_type<GetFunctionInfoTool>(memory, executor);
    registry.register_tool_type<GetDataInfoTool>(memory, executor);
    registry.register_tool_type<DumpDataTool>(memory, executor);
    registry.register_tool_type<AnalyzeFunctionTool>(memory, executor);

    // Search tools
    registry.register_tool_type<SearchFunctionsTool>(memory, executor);
    registry.register_tool_type<SearchGlobalsTool>(memory, executor);
    registry.register_tool_type<SearchStringsTool>(memory, executor);

    // Modification tools
    registry.register_tool_type<SetNameTool>(memory, executor);
    registry.register_tool_type<SetCommentTool>(memory, executor);

    // Analysis tools
    registry.register_tool_type<StoreAnalysisTool>(memory, executor);
    registry.register_tool_type<GetAnalysisTool>(memory, executor);

    // Binary info tools
    registry.register_tool_type<GetImportsTool>(memory, executor);
    registry.register_tool_type<GetEntryPointsTool>(memory, executor);

    // Updating decompilation tools
    registry.register_tool_type<GetFunctionPrototypeTool>(memory, executor);
    registry.register_tool_type<SetFunctionPrototypeTool>(memory, executor);
    registry.register_tool_type<GetVariablesTool>(memory, executor);
    registry.register_tool_type<SetVariableTool>(memory, executor);

    // Local type tools
    registry.register_tool_type<SearchLocalTypesTool>(memory, executor);
    registry.register_tool_type<GetLocalTypeTool>(memory, executor);
    registry.register_tool_type<SetLocalTypeTool>(memory, executor);

    // Patch tools
    if (patch_manager) {
        registry.register_tool_type<PatchBytesTool>(memory, executor, patch_manager);
        registry.register_tool_type<PatchAssemblyTool>(memory, executor, patch_manager);
        registry.register_tool_type<RevertPatchTool>(memory, executor, patch_manager);
        registry.register_tool_type<ListPatchesTool>(memory, executor, patch_manager);
    }

    // Code injection tools
    if (code_injection_manager) {
        registry.register_tool_type<AllocateCodeWorkspaceTool>(memory, executor, code_injection_manager);
        registry.register_tool_type<PreviewCodeInjectionTool>(memory, executor, code_injection_manager);
        registry.register_tool_type<FinalizeCodeInjectionTool>(memory, executor, code_injection_manager);
    }

    // Python execution tool (only if enabled in config)
    if (config.agent.enable_python_tool) {
        registry.register_tool_type<RunPythonTool>(memory, executor);
    }

    // Deep analysis
    if (deep_analysis_manager) {
        registry.register_tool_type<StartDeepAnalysisCollectionTool>(memory, executor, deep_analysis_manager);
        registry.register_tool_type<AddToDeepAnalysisTool>(memory, executor, deep_analysis_manager);
        registry.register_tool_type<RequestDeepAnalysisTool>(memory, executor, deep_analysis_manager);
        registry.register_tool_type<ListDeepAnalysesTool>(memory, executor, deep_analysis_manager);
        registry.register_tool_type<GetDeepAnalysisTool>(memory, executor, deep_analysis_manager);
    }
}

} // namespace llm_re::tools

#endif //TOOL_SYSTEM_H
