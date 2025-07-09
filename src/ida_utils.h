//
// Created by user on 6/29/25.
//

#ifndef IDA_UTILS_H
#define IDA_UTILS_H

#include "common.h"

namespace llm_re {

// Structures for returning comprehensive info
struct FunctionInfo {
    std::string name;
    ea_t start_ea;
    ea_t end_ea;
    size_t size;
    size_t xrefs_to_count;
    size_t xrefs_from_count;
    size_t string_refs_count;
    size_t data_refs_count;
    bool is_library;
    bool is_thunk;
};

struct DataInfo {
    std::string name;
    std::string value;
    std::string type;
    size_t size;
    std::vector<std::pair<ea_t, std::string>> xrefs_to;  // Configurable limit (default 20)
    bool xrefs_truncated = false;  // True if xrefs were truncated
    int xrefs_truncated_at = 0;    // The limit at which truncation occurred
};

// Function prototype information
struct FunctionParameter {
    int index;
    std::string type;
    std::string name;
};

struct FunctionPrototypeInfo {
    std::string full_prototype;
    std::string return_type;
    std::string calling_convention;
    std::string function_name;
    std::vector<FunctionParameter> parameters;
};

// Local type information
struct LocalTypeInfo {
    std::string name;
    std::string kind;  // "struct", "union", "enum", "typedef"
    size_t size;
};

struct LocalTypeDefinition {
    std::string name;
    std::string definition;
    std::string kind;
    size_t size;
};

struct SetLocalTypeResult {
    bool success;
    std::string type_name;
    std::string error_message;
};

// Local variable information
struct LocalVariableInfo {
    std::string name;
    std::string type;
    std::string location;  // "stack", "register"
    int stack_offset;      // if on stack
    std::string reg_name;  // if in register
};

struct FunctionArgument {
    std::string name;
    std::string type;
    int index;
};

struct FunctionLocalsInfo {
    std::vector<LocalVariableInfo> locals;
    std::vector<FunctionArgument> arguments;
};

// Utility class that bridges our actions to IDA API calls
// All methods here will be called from worker thread and use execute_sync
class IDAUtils {
private:
    template<typename Func>
    class exec_request_wrapper_t : public exec_request_t {
    private:
        Func func;
        using RetType = decltype(std::declval<Func>()());
        RetType* result_ptr;
        std::exception_ptr* exception_ptr;

    public:
        exec_request_wrapper_t(Func&& f, RetType* res_ptr, std::exception_ptr* exc_ptr)
            : func(std::forward<Func>(f)), result_ptr(res_ptr), exception_ptr(exc_ptr) {}

        virtual ssize_t idaapi execute() override {
            try {
                *result_ptr = func();
                return 0;
            } catch (...) {
                *exception_ptr = std::current_exception();
                return -1;
            }
        }
    };

    // Specialization for void return type
    template<typename Func>
    class exec_request_wrapper_void_t : public exec_request_t {
    private:
        Func func;
        std::exception_ptr* exception_ptr;

    public:
        exec_request_wrapper_void_t(Func&& f, std::exception_ptr* exc_ptr)
            : func(std::forward<Func>(f)), exception_ptr(exc_ptr) {}

        virtual ssize_t idaapi execute() override {
            try {
                func();
                return 0;
            } catch (...) {
                *exception_ptr = std::current_exception();
                return -1;
            }
        }
    };

    // Helper to detect void return type
    template<typename T>
    struct is_void_return : std::false_type {};

    template<typename... Args>
    struct is_void_return<void(Args...)> : std::true_type {};

public:
    // Helper to execute IDA operations synchronously with custom flags
    template<typename Func>
    static auto execute_sync_wrapper(Func&& func, int flags = MFF_READ) -> decltype(func()) {
        using RetType = decltype(func());

        // Handle void return type
        if constexpr (std::is_void_v<RetType>) {
            std::exception_ptr exc_ptr;
            exec_request_wrapper_void_t<Func> req(std::forward<Func>(func), &exc_ptr);
            execute_sync(req, flags);
            if (exc_ptr) {
                std::rethrow_exception(exc_ptr);
            }
        } else {
            // Handle non-void return type
            RetType result;
            std::exception_ptr exc_ptr;
            exec_request_wrapper_t<Func> req(std::forward<Func>(func), &result, &exc_ptr);
            execute_sync(req, flags);
            if (exc_ptr) {
                std::rethrow_exception(exc_ptr);
            }
            return result;
        }
    }

    // Helpers
    static ea_t get_name_address(const std::string& name);
    static bool is_function(ea_t address);

    // Consolidated search functions
    static std::vector<std::tuple<ea_t, std::string, bool>> search_functions(const std::string& pattern, bool named_only, int max_results);
    static std::vector<std::tuple<ea_t, std::string, std::string, std::string>> search_globals(const std::string& pattern, int max_results);
    static std::vector<std::pair<ea_t, std::string>> search_strings_unified(const std::string& pattern, int min_length, int max_results);

    // Comprehensive info functions
    static FunctionInfo get_function_info(ea_t address);
    static DataInfo get_data_info(ea_t address, int max_xrefs = 20);
    static std::string dump_data(ea_t address, size_t size, int bytes_per_line = 16);

    // Cross-reference operations (kept for detailed analysis)
    static std::vector<std::pair<ea_t, std::string>> get_xrefs_to_with_names(ea_t address, int max_count = -1);
    static std::vector<std::pair<ea_t, std::string>> get_xrefs_from_with_names(ea_t address, int max_count = -1);

    // Disassembly and decompilation (kept for analyze_function)
    static std::string get_function_disassembly(ea_t address);
    static std::string get_function_decompilation(ea_t address);

    // Function operations (simplified)
    static std::string get_function_name(ea_t address);
    static std::vector<std::string> get_function_string_refs(ea_t address, int max_count = -1);
    static std::vector<ea_t> get_function_data_refs(ea_t address, int max_count = -1);

    // Unified name setter
    static bool set_addr_name(ea_t address, const std::string& name);

    // Data operations (kept for compatibility)
    static std::pair<std::string, std::string> get_data(ea_t address);

    // Comment operations (kept as is)
    static bool add_disassembly_comment(ea_t address, const std::string& comment);
    static bool add_pseudocode_comment(ea_t address, const std::string& comment);
    static bool clear_disassembly_comment(ea_t address);
    static bool clear_pseudocode_comments(ea_t address);

    // Binary info operations (kept as is)
    static std::map<std::string, std::vector<std::string>> get_imports();
    static std::vector<std::tuple<ea_t, std::string, std::string>> get_entry_points();

    // Decompilation-related functions
    static FunctionPrototypeInfo get_function_prototype(ea_t address);
    static bool set_function_prototype(ea_t address, const std::string& prototype);
    static FunctionLocalsInfo get_function_locals(ea_t address);
    static bool set_variable(ea_t address, const std::string& variable_name, const std::string& new_name, const std::string& new_type);

    // Local types
    static std::vector<LocalTypeInfo> search_local_types(const std::string& pattern, const std::string& type_kind, int max_results);
    static LocalTypeDefinition get_local_type(const std::string& type_name);
    static SetLocalTypeResult set_local_type(const std::string& definition, bool replace_existing);
};

} // namespace llm_re

#endif //IDA_UTILS_H