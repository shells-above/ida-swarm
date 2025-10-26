#ifndef CALLING_CONVENTION_H
#define CALLING_CONVENTION_H

#include "core/common.h"
#include <vector>
#include <string>

namespace llm_re::semantic {

/**
 * @file calling_convention.h
 * @brief Calling convention detection and ABI compatibility verification for semantic patching
 *
 * This module provides detection and analysis of function calling conventions (ABIs) to ensure
 * that semantically patched functions maintain binary compatibility with their callers.
 *
 * CRITICAL CONCEPT: When replacing a function with compiled C code, the calling convention
 * (register usage, stack layout, cleanup responsibility) MUST match exactly, or the program
 * will crash. This module detects the original convention and verifies the compiled replacement.
 *
 * Supported calling conventions:
 *   - x86-64: System V ABI (Linux/macOS/BSD), Microsoft x64 (Windows)
 *   - x86-32: cdecl, stdcall, fastcall, thiscall
 *   - ARM: AAPCS (32-bit), AAPCS64 (64-bit)
 *
 * Usage pattern:
 *   1. Analyze original function to detect its calling convention
 *   2. Compile replacement C code with matching convention flags
 *   3. Analyze compiled assembly to verify convention matches
 *   4. Only finalize if conventions are compatible
 *
 * Thread safety: All IDA SDK calls are wrapped with execute_sync_wrapper
 */

/**
 * @struct CallingConvention
 * @brief Complete specification of a function calling convention (ABI)
 *
 * Represents the contract between caller and callee for:
 *   - Argument passing (registers, stack order)
 *   - Return value location
 *   - Register preservation requirements
 *   - Stack alignment and cleanup responsibility
 *
 * This structure contains everything needed to:
 *   1. Generate compiler flags to match the convention
 *   2. Verify ABI compatibility between two functions
 *   3. Provide diagnostic information to agents
 */
struct CallingConvention {
    enum Type {
        UNKNOWN,
        // x86-64 conventions
        X64_SYSV,       // Linux/macOS/BSD: RDI, RSI, RDX, RCX, R8, R9
        X64_MS,         // Windows: RCX, RDX, R8, R9

        // x86-32 conventions
        X86_CDECL,      // Stack-based, caller cleans
        X86_STDCALL,    // Stack-based, callee cleans
        X86_FASTCALL,   // ECX, EDX, then stack
        X86_THISCALL,   // ECX for 'this', rest on stack

        // ARM conventions
        ARM_AAPCS,      // R0-R3 for args
        ARM64_AAPCS,    // X0-X7 for args
    };

    Type type = UNKNOWN;
    std::string name;  // Human-readable name

    // Register usage
    std::vector<std::string> arg_registers;        // Registers for arguments (in order)
    std::string return_register;                   // Where return value goes
    std::vector<std::string> callee_saved;         // Registers callee must preserve
    std::vector<std::string> caller_saved;         // Registers caller must save if needed

    // Stack information
    bool uses_stack_args = false;                  // True if args beyond registers go on stack
    int stack_alignment = 0;                       // Required stack alignment (bytes)

    // Compiler flags needed to generate this convention
    std::string compiler_flags;

    /**
     * @brief Get human-readable description of this calling convention
     * @return String like "System V AMD64 ABI (args: rdi, rsi, rdx; ret: rax)"
     */
    std::string to_string() const;

    /**
     * @brief Check if two conventions are ABI-compatible
     * @param other The convention to compare against
     * @return true if replacing a function with 'other' convention would be safe
     *
     * Two conventions are compatible if they agree on:
     *   - Convention type (e.g., both X64_SYSV)
     *   - Argument register order
     *   - Return register
     *
     * Incompatible conventions will cause crashes or wrong behavior if used for replacement.
     */
    bool is_compatible_with(const CallingConvention& other) const;
};

/**
 * @class CallingConventionAnalyzer
 * @brief Detects calling conventions from IDA functions and compiled assembly
 *
 * Detection strategy:
 *   - Uses IDA's type information (tinfo_t and func_type_data_t)
 *   - Extracts actual argument registers from argloc_t
 *   - Returns UNKNOWN if IDA has no type info (no fallback/guessing)
 *
 * The analyzer caches platform information on construction for performance.
 *
 * Thread safety: All public methods use IDA's execute_sync_wrapper for thread safety.
 */
class CallingConventionAnalyzer {
public:
    CallingConventionAnalyzer();
    ~CallingConventionAnalyzer() = default;

    /**
     * @brief Detect calling convention of a function in the IDA database
     * @param func_addr Address of the function to analyze
     * @return Detected CallingConvention (UNKNOWN if IDA has no type info)
     *
     * Detection process:
     *   1. Get function type info (tinfo_t::get_func_details)
     *   2. Extract calling convention (func_type_data_t::get_cc)
     *   3. For x64, examine actual arg registers (argloc_t) to distinguish System V vs MS
     *   4. If no type info available, return UNKNOWN (no fallback/guessing!)
     *
     * Used during semantic patch session initialization to detect the original
     * function's convention that must be matched.
     *
     * @note Thread-safe: Uses IDA's execute_sync_wrapper internally
     */
    CallingConvention analyze_function(ea_t func_addr) const;

    /**
     * @brief Detect calling convention from compiled assembly code
     * @param assembly Assembly code as text (Intel syntax)
     * @param architecture Target architecture ("x86_64", "x86", "arm", "arm64")
     * @return Detected CallingConvention based on register usage patterns
     *
     * This is used to verify that compiler-generated code matches the expected
     * calling convention. Analyzes the first few instructions for argument
     * register usage patterns (e.g., RDI usage = System V, RCX = MS x64).
     *
     * Falls back to platform default if patterns are ambiguous.
     *
     * @note Does NOT use IDA SDK, safe to call from any thread
     */
    CallingConvention analyze_assembly(const std::string& assembly, const std::string& architecture);

    /**
     * @brief Get the default calling convention for current platform
     * @return Platform-appropriate CallingConvention
     *
     * Determined by:
     *   - Architecture (64-bit vs 32-bit)
     *   - OS (Windows vs Unix-like)
     *   - File format (PE vs ELF)
     *
     * Examples:
     *   - Linux x64: System V AMD64 ABI
     *   - Windows x64: Microsoft x64
     *   - Linux x86: cdecl
     */
    CallingConvention get_platform_default() const;

    /**
     * @brief Get the current architecture string
     * @return Architecture string ("x86_64", "x86", "arm64", "arm", or "unknown")
     * @note Returns cached value, safe to call multiple times
     */
    std::string get_architecture() const;

private:

    // Convention builders
    static CallingConvention build_x64_sysv();
    static CallingConvention build_x64_ms();
    static CallingConvention build_x86_cdecl();
    static CallingConvention build_x86_stdcall();
    static CallingConvention build_x86_fastcall();
    static CallingConvention build_x86_thiscall();
    static CallingConvention build_arm_aapcs();
    static CallingConvention build_arm64_aapcs();

    // Cache platform info
    std::string cached_architecture_;
    bool cached_is_64bit_ = false;
    bool cached_is_windows_ = false;
    bool cache_valid_ = false;
};

} // namespace llm_re::semantic

#endif // CALLING_CONVENTION_H
