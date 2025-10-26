#ifndef C_COMPILER_H
#define C_COMPILER_H

#include "core/common.h"
#include "calling_convention.h"
#include <vector>
#include <string>
#include <optional>

namespace llm_re::semantic {

/**
 * @file c_compiler.h
 * @brief C code compilation with automatic external symbol resolution for semantic patching
 *
 * This module solves a critical problem: when you decompile a function to C code and modify it,
 * the C code contains calls to other functions (e.g., check_credentials, malloc) that exist
 * in the binary but are undefined from the compiler's perspective.
 *
 * THE PROBLEM:
 *   Original binary function calls other_function() at address 0x401000.
 *   Decompiled C code: "int result = other_function(arg);"
 *   Compiler sees: "error: use of undeclared identifier 'other_function'"
 *
 * THE SOLUTION (compiler-driven symbol resolution):
 *   1. Try to compile → capture "undefined symbol: other_function" errors
 *   2. Query IDA database for other_function's address (0x401000)
 *   3. Inject #define other_function ((int(*)(int))0x401000)
 *   4. Recompile → success!
 *
 * The compiler uses clang/gcc to generate Intel-syntax assembly with specified calling conventions,
 * then the semantic patch manager assembles this to machine code via Keystone.
 *
 * Thread safety: Symbol resolution uses IDA's execute_sync_wrapper
 */

/**
 * @struct CompilationAttempt
 * @brief Result of a single compilation attempt
 */
struct CompilationAttempt {
    bool success;
    std::string output;  // Assembly output if success, error output if failure
    std::vector<std::string> undefined_symbols;  // Symbols that need resolution
    std::vector<std::string> undefined_types;    // Types that need resolution
    std::string compiler_stderr;
    int exit_code;
};

// Information about a resolved symbol
struct ResolvedSymbol {
    std::string name;
    ea_t address;
    std::string type_signature;  // From IDA (may be empty)
    bool has_type_info;
    bool is_function;  // true if function, false if data variable
};

// Information about a resolved string literal
struct ResolvedString {
    std::string content;  // String content (without quotes)
    ea_t address;         // Address in IDA's string database
};

// Information about a resolved type
struct ResolvedType {
    std::string name;                           // Type name (e.g., "credentials")
    std::string kind;                           // "struct", "union", "enum", "typedef"
    std::string definition;                     // Full C definition from IDA
    std::vector<std::string> dependencies;      // Other types this type references
};

// C code compiler with automatic symbol resolution
class CCompiler {
public:
    CCompiler();
    ~CCompiler();

    // High-level compilation with automatic symbol and type resolution via iterative loop
    CompilationAttempt compile_with_symbol_resolution(
        const std::string& c_code,
        const std::string& architecture,
        const CallingConvention& calling_convention,
        std::vector<ResolvedSymbol>& out_resolved_symbols,
        std::vector<ResolvedType>& out_resolved_types,
        std::string& out_final_c_code,
        int max_iterations = 10
    );

    // Low-level compilation without symbol resolution (for testing)
    CompilationAttempt try_compile(
        const std::string& c_code,
        const std::string& architecture,
        const CallingConvention& calling_convention
    );

    // Compile C code to object file (for machine code extraction via LIEF)
    // Returns path to temporary object file on success, empty string on failure
    std::string compile_to_object_file(
        const std::string& c_code,
        const std::string& architecture,
        const CallingConvention& calling_convention,
        std::string& out_error
    );

private:
    // Symbol resolution
    static std::vector<std::string> parse_undefined_symbols(const std::string& compiler_output);
    static std::optional<ResolvedSymbol> resolve_symbol_via_ida(const std::string& symbol_name);
    static std::string inject_symbol_definitions(const std::string& c_code, const std::vector<ResolvedSymbol>& symbols);

    // Type resolution
    static std::vector<std::string> parse_undefined_types(const std::string& compiler_output);
    static std::optional<ResolvedType> resolve_type_via_ida(const std::string& type_name);
    static std::vector<std::string> extract_type_dependencies(const std::string& type_definition);
    static std::string inject_type_definitions(const std::string& c_code, const std::vector<ResolvedType>& types);

    // String literal resolution
    static std::vector<std::string> parse_string_literals(const std::string& c_code);
    static std::optional<ResolvedString> resolve_string_via_ida(const std::string& string_content);
    static std::string inject_string_definitions(const std::string& c_code, const std::vector<ResolvedString>& strings);

    // Compilation
    std::string generate_compiler_command(const std::string& input_file, const std::string& output_file, const std::string& architecture, const CallingConvention& calling_convention) const;
    std::string get_target_triple(const std::string& architecture) const;

    // Error parsing
    struct ParsedError {
        std::string type;  // "undefined", "syntax", "other"
        std::string symbol;  // For undefined errors
        std::string message;
        int line;
        int column;
    };
    static std::vector<ParsedError> parse_compiler_errors(const std::string& error_output);

    // Utilities
    std::string create_temp_file(const std::string& content, const std::string& extension);
    static std::string execute_command(const std::string& command, int& out_exit_code);
    static std::string read_file(const std::string& path);
    static void delete_temp_file(const std::string& path);

    // IDA type info helpers
    static std::string get_function_type_from_ida(ea_t addr);

    // Compiler path (clang only)
    static constexpr const char* COMPILER_PATH = "clang";

    // Binary format detection (PE vs ELF vs Mach-O)
    bool is_windows_binary_ = false;  // Cached result of (inf_get_filetype() == f_PE)

    // Temp file tracking for cleanup
    std::vector<std::string> temp_files_;
};

} // namespace llm_re::semantic

#endif // C_COMPILER_H
