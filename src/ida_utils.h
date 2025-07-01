//
// Created by user on 6/29/25.
//

#ifndef IDA_UTILS_H
#define IDA_UTILS_H

#include "common.h"

namespace llm_re {

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


    // Cross-reference operations

    static std::vector<std::pair<ea_t, std::string>> get_xrefs_to_with_names(ea_t address);
    static std::vector<std::pair<ea_t, std::string>> get_xrefs_from_with_names(ea_t address);

    // Disassembly and decompilation

    /**
     * Get the disassembly for a function with comments
     * @param address Function start address
     * @return Disassembly text with comments
     */
    static std::string get_function_disassembly(ea_t address);

    /**
     * Get the decompilation for a function with comments
     * @param address Function start address
     * @return Decompiled code with comments
     */
    static std::string get_function_decompilation(ea_t address);

    // Function operations

    /**
     * Get a function's address from its name
     * @param name Function name
     * @return Function address or BADADDR if not found
     */
    static ea_t get_function_address(const std::string& name);

    /**
     * Get a function's name from its address
     * @param address Function address
     * @return Function name or empty string if unnamed
     */
    static std::string get_function_name(ea_t address);

    /**
     * Set a function's name
     * @param address Function address
     * @param name New name
     * @return Success status
     */
    static bool set_function_name(ea_t address, const std::string& name);

    /**
     * Get all string references used by a function
     * @param address Function address
     * @return Vector of string values
     */
    static std::vector<std::string> get_function_string_refs(ea_t address);

    /**
     * Get all data references accessed by a function
     * @param address Function address
     * @return Vector of data addresses
     */
    static std::vector<ea_t> get_function_data_refs(ea_t address);

    // Data operations

    /**
     * Get the name of a data item
     * @param address Data address
     * @return Data name or empty string if unnamed
     */
    static std::string get_data_name(ea_t address);

    /**
     * Set the name of a data item
     * @param address Data address
     * @param name New name
     * @return Success status
     */
    static bool set_data_name(ea_t address, const std::string& name);

    /**
     * Returns the value and type of the data item
     * @param address Data address
     * @return Value and type of data
     */
    static std::pair<std::string, std::string> get_data(ea_t address);

    // Comment operations

    /**
     * Add a comment to disassembly at an address
     * @param address Target address
     * @param comment Comment text
     * @return Success status
     */
    static bool add_disassembly_comment(ea_t address, const std::string& comment);

    /**
     * Add a comment to pseudocode
     * @param address Function address
     * @param comment Comment text
     * @return Success status
     */
    static bool add_pseudocode_comment(ea_t address, const std::string& comment);

    /**
     * Clear disassembly comment at an address
     * @param address Target address
     * @return Success status
     */
    static bool clear_disassembly_comment(ea_t address);

    /**
     * Clear all pseudocode comments for a function
     * @param address Function address
     * @return Success status
     */
    static bool clear_pseudocode_comments(ea_t address);

    // Import/Export operations

    /**
     * Get all imported functions
     * @return Map of module names to imported function names
     */
    static std::map<std::string, std::vector<std::string>> get_imports();

    // String operations

    /**
     * Get all strings in the binary
     * @return Vector of string values
     */
    static std::vector<std::string> get_strings();

    /**
     * Search for strings containing text
     * @param text Search text
     * @param is_case_sensitive Case sensitivity flag
     * @return Vector of matching strings
     */
    static std::vector<std::string> search_strings(const std::string& text, bool is_case_sensitive);

    static std::vector<std::pair<ea_t, std::string>> get_named_functions();
    static std::vector<std::pair<ea_t, std::string>> search_named_globals(const std::string& pattern, bool is_regex);
    static std::vector<std::pair<ea_t, std::string>> get_named_globals();
    static std::vector<std::pair<ea_t, std::string>> get_strings_with_addresses(int min_length = 4);
    static std::vector<std::tuple<ea_t, std::string, std::string>> get_entry_points();
};

} // namespace llm_re

#endif //IDA_UTILS_H
