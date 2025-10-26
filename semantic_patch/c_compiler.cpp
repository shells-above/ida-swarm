#include "c_compiler.h"
#include "core/ida_utils.h"
#include "core/logger.h"
#include <ida.hpp>
#include <name.hpp>
#include <typeinf.hpp>
#include <strlist.hpp>
#include <bytes.hpp>
#include <fpro.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include <cstdlib>
#ifdef __NT__
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

namespace llm_re::semantic {

CCompiler::CCompiler() {
    // Detect binary format to generate platform-appropriate assembly
    // This must be called from IDA's main thread
    is_windows_binary_ = IDAUtils::execute_sync_wrapper([]() -> bool {
        return (inf_get_filetype() == f_PE);
    }, MFF_READ);
}

CCompiler::~CCompiler() {
    // Clean up temp files
    for (const std::string& file: temp_files_) {
        delete_temp_file(file);
    }
}

CompilationAttempt CCompiler::compile_with_symbol_resolution(
    const std::string& c_code,
    const std::string& architecture,
    const CallingConvention& calling_convention,
    std::vector<ResolvedSymbol>& out_resolved_symbols,
    std::vector<ResolvedType>& out_resolved_types,
    std::string& out_final_c_code,
    int max_iterations) {

    // Iterative resolution: compile -> check errors -> resolve -> inject -> repeat
    // This elegantly handles all dependencies without manual parsing or topological sorting

    // PREPROCESSING STEP 1: Resolve string literals before compilation loop
    // String literals must be exact matches in IDA's string database
    std::vector<std::string> string_literals = parse_string_literals(c_code);
    std::vector<ResolvedString> resolved_strings;
    std::vector<std::string> failed_strings;

    for (const std::string& str : string_literals) {
        std::optional<ResolvedString> resolved = resolve_string_via_ida(str);
        if (resolved) {
            resolved_strings.push_back(*resolved);
        } else {
            failed_strings.push_back(str);
        }
    }

    // If any strings couldn't be resolved, fail immediately
    if (!failed_strings.empty()) {
        CompilationAttempt failure;
        failure.success = false;

        std::stringstream ss;
        ss << "Cannot resolve the following string literals from IDA database:\n";
        for (size_t i = 0; i < failed_strings.size(); i++) {
            ss << "  \"" << failed_strings[i] << "\"\n";
        }
        ss << "\nString literals must have exact matches in IDA's string database.\n";
        ss << "Since we can't use relocations (object file approach), you must either:\n";
        ss << "  1. Use strings that exist in the binary\n";
        ss << "  2. Reference them by address manually: ((const char*)0xADDRESS)\n";
        ss << "  3. Avoid using string literals that aren't in the binary\n";

        failure.output = ss.str();
        failure.compiler_stderr = ss.str();
        return failure;
    }

    // Inject resolved string literals
    std::string current_code = c_code;
    if (!resolved_strings.empty()) {
        current_code = inject_string_definitions(current_code, resolved_strings);
    }

    std::vector<ResolvedSymbol> all_resolved_symbols;
    std::vector<ResolvedType> all_resolved_types;

    for (int iteration = 0; iteration < max_iterations; iteration++) {
        // Try to compile current code
        CompilationAttempt attempt = try_compile(current_code, architecture, calling_convention);

        if (attempt.success) {
            // Success! Return the compiled result
            out_resolved_symbols = all_resolved_symbols;
            out_resolved_types = all_resolved_types;
            out_final_c_code = current_code;
            return attempt;
        }

        // Parse what the compiler says is missing
        std::vector<std::string> undefined_symbols = parse_undefined_symbols(attempt.compiler_stderr);
        std::vector<std::string> undefined_types = parse_undefined_types(attempt.compiler_stderr);

        if (undefined_symbols.empty() && undefined_types.empty()) {
            // Compilation failed but not due to missing symbols/types
            // (syntax errors, type mismatches, etc.)
            return attempt;
        }

        // Resolve symbols from IDA
        std::vector<ResolvedSymbol> new_symbols;
        std::vector<std::string> failed_symbols;

        for (const std::string& symbol_name : undefined_symbols) {
            std::optional<ResolvedSymbol> resolved = resolve_symbol_via_ida(symbol_name);
            if (resolved) {
                new_symbols.push_back(*resolved);
            } else {
                failed_symbols.push_back(symbol_name);
            }
        }

        // Resolve types from IDA
        std::vector<ResolvedType> new_types;
        std::vector<std::string> failed_types;

        for (const std::string& type_name : undefined_types) {
            std::optional<ResolvedType> resolved = resolve_type_via_ida(type_name);
            if (resolved) {
                new_types.push_back(*resolved);
            } else {
                failed_types.push_back(type_name);
            }
        }

        // Check if anything couldn't be resolved
        if (!failed_symbols.empty() || !failed_types.empty()) {
            CompilationAttempt failure;
            failure.success = false;
            failure.undefined_symbols = failed_symbols;
            failure.undefined_types = failed_types;

            std::stringstream ss;
            ss << "Cannot resolve the following from IDA database:\n";
            if (!failed_symbols.empty()) {
                ss << "  Symbols: ";
                for (size_t i = 0; i < failed_symbols.size(); i++) {
                    if (i > 0) ss << ", ";
                    ss << failed_symbols[i];
                }
                ss << "\n";
            }
            if (!failed_types.empty()) {
                ss << "  Types: ";
                for (size_t i = 0; i < failed_types.size(); i++) {
                    if (i > 0) ss << ", ";
                    ss << failed_types[i];
                }
                ss << "\n";
            }
            ss << "\nThese are referenced in your code but don't exist in the IDA database.\n";
            ss << "Either they're misspelled, or you need to define them yourself.";

            failure.output = ss.str();
            failure.compiler_stderr = ss.str();
            return failure;
        }

        // Check if we made progress this iteration
        if (new_symbols.empty() && new_types.empty()) {
            // Nothing resolved but compiler still unhappy - shouldn't happen, but guard against it
            CompilationAttempt failure;
            failure.success = false;
            failure.output = "Internal error: compiler reported undefined symbols/types but none could be parsed";
            failure.compiler_stderr = attempt.compiler_stderr;
            return failure;
        }

        // Add newly resolved items to accumulator
        all_resolved_symbols.insert(all_resolved_symbols.end(), new_symbols.begin(), new_symbols.end());
        all_resolved_types.insert(all_resolved_types.end(), new_types.begin(), new_types.end());

        // Inject ALL resolved items (types first, then symbols)
        current_code = inject_type_definitions(c_code, all_resolved_types);
        current_code = inject_symbol_definitions(current_code, all_resolved_symbols);

        // Loop continues - will try to compile again
    }

    // Hit maximum iterations without success
    // Try one more compile to get the current list of what's still missing
    CompilationAttempt final_attempt = try_compile(current_code, architecture, calling_convention);

    std::vector<std::string> still_undefined_symbols = parse_undefined_symbols(final_attempt.compiler_stderr);
    std::vector<std::string> still_undefined_types = parse_undefined_types(final_attempt.compiler_stderr);

    CompilationAttempt failure;
    failure.success = false;
    failure.undefined_symbols = still_undefined_symbols;
    failure.undefined_types = still_undefined_types;

    std::stringstream ss;
    ss << "Maximum resolution iterations reached (" << std::to_string(max_iterations) << ").\n";
    ss << "Progress made: " << all_resolved_symbols.size() << " symbols, "
       << all_resolved_types.size() << " types resolved.\n\n";

    if (!still_undefined_symbols.empty() || !still_undefined_types.empty()) {
        ss << "Still missing after " << max_iterations << " iterations:\n";
        if (!still_undefined_symbols.empty()) {
            ss << "  Symbols: ";
            for (size_t i = 0; i < still_undefined_symbols.size(); i++) {
                if (i > 0) ss << ", ";
                ss << still_undefined_symbols[i];
            }
            ss << "\n";
        }
        if (!still_undefined_types.empty()) {
            ss << "  Types: ";
            for (size_t i = 0; i < still_undefined_types.size(); i++) {
                if (i > 0) ss << ", ";
                ss << still_undefined_types[i];
            }
            ss << "\n";
        }
        ss << "\nThis likely indicates:\n";
        ss << "- Very deeply nested type dependencies (try increasing max_iterations)\n";
        ss << "- Circular type dependencies\n";
        ss << "- Types/symbols that don't exist in IDA database (define them manually)\n";
    } else {
        ss << "Compiler is still unhappy but not reporting undefined symbols/types.\n";
        ss << "This indicates a different kind of error (syntax, type mismatch, etc.).\n\n";
        ss << "Last compiler output:\n" << final_attempt.compiler_stderr;
    }

    failure.output = ss.str();
    failure.compiler_stderr = final_attempt.compiler_stderr;
    return failure;
}

CompilationAttempt CCompiler::try_compile(const std::string& c_code, const std::string& architecture, const CallingConvention& calling_convention) {
    CompilationAttempt result;

    // Create temp files
    std::string input_file = create_temp_file(c_code, ".c");
    std::string output_file = input_file + ".s";

    // Generate compiler command
    std::string command = generate_compiler_command(input_file, output_file,
                                                    architecture, calling_convention);

    // Execute compilation
    int exit_code = 0;
    std::string compiler_output = execute_command(command, exit_code);

    result.exit_code = exit_code;
    result.compiler_stderr = compiler_output;

    if (exit_code == 0 && std::filesystem::exists(output_file)) {
        // Success!
        result.success = true;
        result.output = read_file(output_file);
        delete_temp_file(output_file);
    } else {
        // Failure - parse errors
        result.success = false;
        result.output = compiler_output;
        result.undefined_symbols = parse_undefined_symbols(compiler_output);
    }

    return result;
}

std::string CCompiler::compile_to_object_file(
    const std::string& c_code,
    const std::string& architecture,
    const CallingConvention& calling_convention,
    std::string& out_error) {

    // Create temp input file
    std::string input_file = create_temp_file(c_code, ".c");

    // Object file output (use .o extension - works cross-platform)
    std::string output_file = input_file + ".o";

    // Build compiler command for object file generation
    std::stringstream cmd;
    cmd << COMPILER_PATH << " ";

    // Object file compilation flags
    cmd << "-c ";  // Generate object file (not assembly)
    cmd << "-O0 "; // No optimization for predictability
    cmd << "-fno-asynchronous-unwind-tables "; // Cleaner code
    cmd << "-fno-dwarf2-cfi-asm ";
    cmd << "-fno-pic -fno-pie "; // Disable Position-Independent Code
    cmd << "-fno-jump-tables "; // Prevent switch statement jump tables (avoid relocations)

    // Target architecture
    std::string target_triple = get_target_triple(architecture);
    if (!target_triple.empty()) {
        cmd << "-target " << target_triple << " ";
    }

    // Calling convention flags
    std::string cc_flags = calling_convention.compiler_flags;
    if (!cc_flags.empty()) {
        cmd << cc_flags << " ";
    }

    // Input and output
    cmd << "\"" << input_file << "\" ";
    cmd << "-o \"" << output_file << "\" ";

    // Redirect stderr to stdout
    cmd << "2>&1";

    // Execute compilation
    int exit_code = 0;
    std::string compiler_output = execute_command(cmd.str(), exit_code);

    // Clean up input file
    delete_temp_file(input_file);

    if (exit_code == 0 && std::filesystem::exists(output_file)) {
        // Success - return path to object file (caller must clean up)
        LOG("Successfully compiled to object file: %s\n", output_file.c_str());
        return output_file;
    } else {
        // Failure
        out_error = "Failed to compile to object file:\n" + compiler_output;
        LOG("Object file compilation failed: %s\n", compiler_output.c_str());
        return "";
    }
}

// Symbol resolution

std::vector<std::string> CCompiler::parse_undefined_symbols(const std::string& compiler_output) {
    std::vector<std::string> symbols;
    std::set<std::string> unique_symbols;  // Avoid duplicates

    // Regex patterns for different compilers
    std::vector<std::regex> patterns = {
        // Clang: "error: use of undeclared identifier 'foo'"
        std::regex(R"(undeclared identifier '([^']+)')"),
        // GCC: "error: 'foo' undeclared"
        std::regex(R"('([^']+)' undeclared)"),
        // Linker: "undefined reference to `foo'"
        std::regex(R"(undefined reference to [`']([^'`]+)['`])"),
        // Another variant: "undefined symbol: foo"
        std::regex(R"(undefined symbol:?\s+([a-zA-Z_][a-zA-Z0-9_]*))"),
    };

    for (const std::regex& pattern: patterns) {
        auto begin = std::sregex_iterator(compiler_output.begin(), compiler_output.end(), pattern);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            std::string symbol = (*it)[1].str();
            if (unique_symbols.insert(symbol).second) {
                symbols.push_back(symbol);
            }
        }
    }

    return symbols;
}

std::vector<std::string> CCompiler::parse_undefined_types(const std::string& compiler_output) {
    std::vector<std::string> types;
    std::set<std::string> unique_types;  // Avoid duplicates

    // Regex patterns for type errors
    std::vector<std::regex> patterns = {
        // Clang/GCC: "error: unknown type name 'foo'"
        std::regex(R"(unknown type name '([^']+)')"),
        // Clang/GCC: "error: use of undeclared identifier 'struct foo'"
        std::regex(R"(undeclared identifier '(struct|union|enum)\s+([^']+)')"),
        // GCC: "error: 'struct foo' has no member named"
        std::regex(R"('(struct|union|enum)\s+([^']+)'\s+has no member)"),
        // Field access on incomplete type
        std::regex(R"(incomplete\s+(?:definition\s+of\s+)?type\s+'(?:struct|union|enum)\s+([^']+)')"),
        // Clang: "error: variable has incomplete type 'struct foo'"
        std::regex(R"(incomplete type '(?:struct|union|enum)\s+([^']+)')"),
    };

    for (const std::regex& pattern: patterns) {
        auto begin = std::sregex_iterator(compiler_output.begin(), compiler_output.end(), pattern);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            std::string type_name;

            // Handle patterns with and without struct/union/enum keyword capture
            if (it->size() > 2 && !(*it)[2].str().empty()) {
                // Pattern captured "struct foo" separately
                type_name = (*it)[2].str();
            } else {
                // Pattern captured "foo" directly
                type_name = (*it)[1].str();
            }

            // Clean up type name
            // Remove any leading/trailing whitespace
            size_t start = type_name.find_first_not_of(" \t");
            size_t end = type_name.find_last_not_of(" \t");
            if (start != std::string::npos && end != std::string::npos) {
                type_name = type_name.substr(start, end - start + 1);
            }

            if (!type_name.empty() && unique_types.insert(type_name).second) {
                types.push_back(type_name);
            }
        }
    }

    return types;
}

std::optional<ResolvedSymbol> CCompiler::resolve_symbol_via_ida(const std::string& symbol_name) {
    return IDAUtils::execute_sync_wrapper([&]() -> std::optional<ResolvedSymbol> {
        // Try to find symbol in IDA database
        ea_t addr = get_name_ea(BADADDR, symbol_name.c_str());

        if (addr == BADADDR) {
            return std::nullopt;
        }

        ResolvedSymbol resolved;
        resolved.name = symbol_name;
        resolved.address = addr;

        // Get type information
        tinfo_t tif;
        if (get_tinfo(&tif, addr)) {
            // We have type info from IDA
            qstring type_str;
            tif.print(&type_str, nullptr, PRTYPE_1LINE | PRTYPE_TYPE);

            resolved.type_signature = type_str.c_str();
            resolved.has_type_info = true;
            resolved.is_function = tif.is_func();
        } else {
            // No type info - try fallback detection
            func_t* func = get_func(addr);
            if (func) {
                // It's a function without type info
                resolved.has_type_info = false;
                resolved.is_function = true;
            } else {
                // It's data without type info - cannot resolve without type
                return std::nullopt;
            }
        }

        return resolved;
    }, MFF_READ);
}

std::vector<std::string> CCompiler::parse_string_literals(const std::string& c_code) {
    std::vector<std::string> strings;
    std::set<std::string> unique_strings;

    // Match string literals: "string" (handle escaped quotes and other escape sequences)
    // This regex matches: " followed by any number of (non-quote-non-backslash OR backslash-anything) then "
    // Using custom delimiter to avoid conflicts with parentheses in regex
    std::regex string_pattern(R"DELIM("([^"\\]*(\\.[^"\\]*)*)")DELIM");

    auto begin = std::sregex_iterator(c_code.begin(), c_code.end(), string_pattern);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::string str = (*it)[1].str();  // Content without outer quotes

        // Only process non-empty strings (empty strings are less useful to resolve)
        if (!str.empty() && unique_strings.insert(str).second) {
            strings.push_back(str);
        }
    }

    return strings;
}

std::optional<ResolvedString> CCompiler::resolve_string_via_ida(const std::string& string_content) {
    return IDAUtils::execute_sync_wrapper([&]() -> std::optional<ResolvedString> {
        try {
            // Iterate through IDA's string database to find exact match
            size_t string_count = get_strlist_qty();

            for (size_t i = 0; i < string_count; i++) {
                string_info_t si;
                if (!get_strlist_item(&si, i)) {
                    continue;
                }

                // Get string content from IDA
                qstring ida_str;
                if (get_strlit_contents(&ida_str, si.ea, si.length, si.type) <= 0) {
                    continue;
                }

                // Compare with our string content (exact match required)
                if (string_content == ida_str.c_str()) {
                    ResolvedString resolved;
                    resolved.content = string_content;
                    resolved.address = si.ea;
                    return resolved;
                }
            }

            // No match found
            return std::nullopt;
        } catch (const std::exception& e) {
            return std::nullopt;
        }
    }, MFF_READ);
}

std::string CCompiler::inject_string_definitions(const std::string& c_code, const std::vector<ResolvedString>& strings) {
    std::string modified_code = c_code;

    // We need to replace string literals with their resolved addresses
    // Strategy: Replace "string_content" with ((const char*)0xADDRESS)
    // Important: Process longer strings first to avoid partial replacements

    // Sort by length descending to handle overlapping strings correctly
    std::vector<ResolvedString> sorted_strings = strings;
    std::sort(sorted_strings.begin(), sorted_strings.end(),
              [](const ResolvedString& a, const ResolvedString& b) {
                  return a.content.length() > b.content.length();
              });

    for (const ResolvedString& str: sorted_strings) {
        // Build the pattern to match the string literal with quotes
        // Need to escape special regex characters in the string content
        std::string escaped_content = str.content;

        // Escape regex special characters
        static const std::string special_chars = R"(\.^$|*+?()[]{}/)";
        std::string escaped;
        for (char c : escaped_content) {
            if (special_chars.find(c) != std::string::npos) {
                escaped += '\\';
            }
            escaped += c;
        }

        // Create pattern: "escaped_content"
        std::string pattern = "\"" + escaped + "\"";

        // Create replacement: ((const char*)0xADDRESS)
        std::stringstream replacement;
        replacement << "((const char*)0x" << std::hex << str.address << std::dec << ")";

        // Replace all occurrences
        std::regex regex_pattern(pattern);
        modified_code = std::regex_replace(modified_code, regex_pattern, replacement.str());
    }

    return modified_code;
}

std::optional<ResolvedType> CCompiler::resolve_type_via_ida(const std::string& type_name) {
    return IDAUtils::execute_sync_wrapper([&]() -> std::optional<ResolvedType> {
        try {
            // Query IDA's local type library
            LocalTypeDefinition type_def = IDAUtils::get_local_type(type_name);

            ResolvedType resolved;
            resolved.name = type_def.name;
            resolved.kind = type_def.kind;
            resolved.definition = type_def.definition;

            // Extract dependencies from the definition (for informational purposes)
            // The iterative compilation loop handles dependencies automatically,
            // but this info is useful for debugging
            resolved.dependencies = extract_type_dependencies(type_def.definition);

            return resolved;
        } catch (const std::exception& e) {
            // Type not found in IDA database
            return std::nullopt;
        }
    }, MFF_READ);
}

std::vector<std::string> CCompiler::extract_type_dependencies(const std::string& type_definition) {
    std::vector<std::string> dependencies;
    std::set<std::string> unique_deps;

    // Regex patterns to find type references in C definitions
    std::vector<std::regex> patterns = {
        // "struct foo" or "union foo" or "enum foo"
        std::regex(R"(\b(?:struct|union|enum)\s+([a-zA-Z_][a-zA-Z0-9_]*)\b)"),
        // Typedef references (harder to detect, look for known type patterns)
        // This is conservative - matches capitalized identifiers that look like typedefs
        // std::regex(R"(\b([A-Z][a-zA-Z0-9_]*)\b)"),  // Too aggressive, skip for now
    };

    for (const std::regex& pattern: patterns) {
        auto begin = std::sregex_iterator(type_definition.begin(), type_definition.end(), pattern);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            std::string dep_name = (*it)[1].str();

            // Filter out C standard types and keywords
            static const std::set<std::string> standard_types = {
                "int", "char", "short", "long", "float", "double", "void",
                "signed", "unsigned", "const", "volatile", "static", "extern",
                "auto", "register", "inline", "restrict",
                "int8_t", "int16_t", "int32_t", "int64_t",
                "uint8_t", "uint16_t", "uint32_t", "uint64_t",
                "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
                "bool", "true", "false", "_Bool",
            };

            if (standard_types.find(dep_name) == standard_types.end()) {
                if (unique_deps.insert(dep_name).second) {
                    dependencies.push_back(dep_name);
                }
            }
        }
    }

    return dependencies;
}

std::string CCompiler::inject_symbol_definitions(const std::string& c_code, const std::vector<ResolvedSymbol>& symbols) {
    std::stringstream header;
    header << "// Auto-generated symbol definitions from IDA\n";
    header << "// These resolve external symbols to their addresses in the binary\n\n";

    for (const ResolvedSymbol& sym: symbols) {
        header << "// Symbol: " << sym.name << " @ 0x" << std::hex << sym.address << std::dec;
        header << " (" << (sym.is_function ? "function" : "data") << ")\n";

        if (sym.has_type_info && !sym.type_signature.empty()) {
            if (sym.is_function) {
                // Function: cast to function pointer
                header << "#define " << sym.name << " ((" << sym.type_signature << ")0x"
                       << std::hex << sym.address << std::dec << ")\n";
            } else {
                // Data: pointer dereference
                header << "#define " << sym.name << " (*(" << sym.type_signature << "*)0x"
                       << std::hex << sym.address << std::dec << ")\n";
            }
        } else {
            // No type info (should only happen for functions)
            header << "#define " << sym.name << " ((void(*)())0x"
                   << std::hex << sym.address << std::dec << ")\n";
        }
    }

    header << "\n// Original code:\n";
    return header.str() + c_code;
}

std::string CCompiler::inject_type_definitions(const std::string& c_code, const std::vector<ResolvedType>& types) {
    if (types.empty()) {
        return c_code;
    }

    std::stringstream header;
    header << "// Auto-generated type definitions from IDA\n";
    header << "// These resolve undefined types referenced in your code\n\n";

    // Just inject all types in order
    // The iterative compilation loop will handle dependencies automatically
    for (const ResolvedType& type : types) {
        header << "// Type: " << type.name << " (" << type.kind << ")\n";
        header << type.definition << "\n\n";
    }

    header << "// Original code:\n";
    return header.str() + c_code;
}

// Compilation

std::string CCompiler::generate_compiler_command(
    const std::string& input_file,
    const std::string& output_file,
    const std::string& architecture,
    const CallingConvention& calling_convention) const {

    std::stringstream cmd;
    cmd << COMPILER_PATH << " ";

    // Basic flags
    cmd << "-S ";  // Generate assembly
    cmd << "-O0 "; // No optimization for predictability
    cmd << "-fno-asynchronous-unwind-tables "; // Cleaner assembly
    cmd << "-fno-dwarf2-cfi-asm ";
    cmd << "-fno-pic -fno-pie "; // Disable Position-Independent Code (prevents @PLT/@GOTPCREL references)
    cmd << "-fno-jump-tables "; // Prevent switch statement jump tables (avoid relocations)

    // Assembly syntax - Intel syntax in GAS format (Keystone uses GAS mode to parse directives)
    if (architecture == "x86_64" || architecture == "x64" || architecture == "x86" || architecture == "i386") {
        cmd << "-masm=intel ";
    }

    // Target architecture
    std::string target_triple = get_target_triple(architecture);
    if (!target_triple.empty()) {
        cmd << "-target " << target_triple << " ";
    }

    // Calling convention flags
    std::string cc_flags = calling_convention.compiler_flags;
    if (!cc_flags.empty()) {
        cmd << cc_flags << " ";
    }

    // Input and output
    cmd << "\"" << input_file << "\" ";
    cmd << "-o \"" << output_file << "\" ";

    // Redirect stderr to stdout so we can capture it
    cmd << "2>&1";

    return cmd.str();
}

std::string CCompiler::get_target_triple(const std::string& architecture) const {
    // Return platform-appropriate target triple based on binary format
    if (architecture == "x86_64" || architecture == "x64") {
        return is_windows_binary_ ? "x86_64-pc-windows-msvc" : "x86_64-unknown-linux-gnu";
    } else if (architecture == "x86" || architecture == "i386") {
        return is_windows_binary_ ? "i686-pc-windows-msvc" : "i386-unknown-linux-gnu";
    } else if (architecture == "arm") {
        return is_windows_binary_ ? "armv7-pc-windows-msvc" : "arm-unknown-linux-gnueabi";
    } else if (architecture == "arm64" || architecture == "aarch64") {
        return is_windows_binary_ ? "aarch64-pc-windows-msvc" : "aarch64-unknown-linux-gnu";
    }
    return "";
}

std::vector<CCompiler::ParsedError> CCompiler::parse_compiler_errors(const std::string& error_output) {
    std::vector<ParsedError> errors;

    // Parse error messages line by line
    std::istringstream stream(error_output);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.find("error:") != std::string::npos) {
            ParsedError err;
            err.message = line;

            if (line.find("undeclared") != std::string::npos) {
                err.type = "undefined";
                // Extract symbol name
                std::regex symbol_regex("'([^']+)'");
                std::smatch match;
                if (std::regex_search(line, match, symbol_regex)) {
                    err.symbol = match[1].str();
                }
            } else {
                err.type = "other";
            }

            errors.push_back(err);
        }
    }

    return errors;
}

// Utilities

std::string CCompiler::create_temp_file(const std::string& content, const std::string& extension) {
    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "ida_swarm_compile";
    std::filesystem::create_directories(temp_dir);

    // Generate unique filename
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    std::string filename = "code_" + std::to_string(timestamp) + extension;
    std::filesystem::path file_path = temp_dir / filename;

    // Write content
    std::ofstream file(file_path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create temp file: " + file_path.string());
    }
    file << content;
    file.close();

    temp_files_.push_back(file_path.string());
    return file_path.string();
}

std::string CCompiler::execute_command(const std::string& command, int& out_exit_code) {
#ifdef __NT__
    // Windows implementation
    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), NULL, TRUE};

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        throw std::runtime_error("Failed to create pipe");
    }

    STARTUPINFOA si = {0};
    si.cb = sizeof(STARTUPINFOA);
    si.hStdOutput = hWritePipe;
    si.hStdError = hWritePipe;
    si.dwFlags |= STARTF_USESTDHANDLES;

    PROCESS_INFORMATION pi = {0};

    if (!CreateProcessA(NULL, const_cast<char*>(command.c_str()),
                       NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        CloseHandle(hWritePipe);
        CloseHandle(hReadPipe);
        throw std::runtime_error("Failed to execute command");
    }

    CloseHandle(hWritePipe);

    // Read output
    std::string output;
    char buffer[4096];
    DWORD bytes_read;

    while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytes_read, NULL) && bytes_read > 0) {
        buffer[bytes_read] = '\0';
        output += buffer;
    }

    CloseHandle(hReadPipe);
    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    out_exit_code = exit_code;

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return output;
#else
    // Unix implementation
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        throw std::runtime_error("Failed to execute command");
    }

    std::string output;
    char buffer[4096];

    while (qfgets(buffer, sizeof(buffer), pipe) != nullptr) {
        output += buffer;
    }

    int status = pclose(pipe);
    out_exit_code = WEXITSTATUS(status);

    return output;
#endif
}

std::string CCompiler::read_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to read file: " + path);
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

void CCompiler::delete_temp_file(const std::string& path) {
    try {
        std::filesystem::remove(path);
    } catch (...) {
        // Ignore errors
    }
}

// IDA type info helpers

std::string CCompiler::get_function_type_from_ida(ea_t addr) {
    return IDAUtils::execute_sync_wrapper([&]() -> std::string {
        tinfo_t tif;
        if (!get_tinfo(&tif, addr)) {
            return "";
        }

        qstring type_str;
        if (!tif.print(&type_str, nullptr, PRTYPE_1LINE | PRTYPE_TYPE)) {
            return "";
        }

        // IDA's tinfo_t::print() with PRTYPE_TYPE produces C-compatible type strings
        // For functions, it formats as function pointer types suitable for casts
        return type_str.c_str();
    }, MFF_READ);
}

} // namespace llm_re::semantic
