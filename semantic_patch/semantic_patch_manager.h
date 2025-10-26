#ifndef SEMANTIC_PATCH_MANAGER_H
#define SEMANTIC_PATCH_MANAGER_H

#include "core/common.h"
#include "calling_convention.h"
#include "c_compiler.h"
#include "patching/patch_manager.h"
#include "patching/code_injection_manager.h"
#include <map>
#include <memory>
#include <chrono>

namespace llm_re::semantic {

/**
 * @file semantic_patch_manager.h
 * @brief Complete workflow for decompile-transform-recompile semantic binary patching
 *
 * This module orchestrates the entire semantic patching workflow, allowing agents to modify
 * binary functions at the C source level rather than assembly level.
 *
 * THE CORE IDEA:
 *   Instead of tedious assembly patching, work at the semantic (C code) level:
 *     1. Decompile function to C code (via Hex-Rays)
 *     2. Agent modifies C code at algorithmic level
 *     3. Compile modified C with matching calling convention
 *     4. Verify ABI compatibility (CRITICAL safety check)
 *     5. Inject compiled machine code, patch original with JMP
 *
 * THE WORKFLOW (4-stage with mandatory verification gates):
 *
 *   Stage 1: start_semantic_patch()
 *     - Decompile function via Hex-Rays
 *     - Detect calling convention (System V, MS x64, cdecl, etc.)
 *     - Return decompiled C code to agent
 *
 *   Stage 2: compile_replacement()
 *     - Agent provides modified C code
 *     - Automatic symbol resolution (other_function → 0x401000)
 *     - Compile to assembly with matching calling convention
 *
 *   Stage 3: preview_semantic_patch() **MANDATORY SAFETY GATE**
 *     - Analyze compiled assembly's calling convention
 *     - Verify ABI compatibility with original
 *     - Show before/after comparison
 *     - Return WARNINGS if incompatible
 *
 *   Stage 4: finalize_semantic_patch()
 *     - BLOCKED if ABI incompatible (prevents crashes)
 *     - Assemble to machine code via Keystone
 *     - Inject into temporary code segment
 *     - Patch original function with JMP to replacement
 *
 * SAFETY GUARANTEES:
 *   - Calling convention mismatch = finalization blocked
 *   - All IDA SDK operations thread-safe
 *   - Session-based workflow prevents race conditions
 *   - Preview is mandatory before finalization
 *
 * Thread safety: All IDA SDK calls wrapped with execute_sync_wrapper
 */

/**
 * @struct SemanticPatchSession
 * @brief State tracking for a semantic patching workflow session
 *
 * Sessions are isolated and stateful - each represents one function replacement workflow.
 * Progress through 4 stages: start → compile → preview → finalize.
 * Each stage builds on the previous and checks prerequisites.
 */
struct SemanticPatchSession {
    std::string session_id;
    ea_t original_function;
    std::string decompiled_code;
    CallingConvention detected_convention;

    // Compilation state
    bool compiled = false;
    std::string compiled_assembly;      // Assembly text for preview
    std::string compiled_object_path;   // Path to object file for machine code extraction
    std::string final_c_code;           // After symbol resolution
    std::vector<std::string> resolved_symbols;

    // Preview state
    bool previewed = false;
    CallingConvention compiled_convention;
    bool abi_compatible = false;
    std::vector<std::string> warnings;

    // Finalization state
    bool finalized = false;
    ea_t injected_address = BADADDR;

    std::chrono::system_clock::time_point created_at;       ///< When session was created
    std::chrono::system_clock::time_point last_updated;    ///< Last stage completion time
};

/**
 * @brief Result structures for each workflow stage
 * Each stage returns a strongly-typed result with success flag and detailed error messages.
 */

/**
 * @struct StartPatchResult
 * @brief Result from Stage 1: start_semantic_patch()
 */
struct StartPatchResult {
    bool success;
    std::string session_id;
    ea_t function_address;
    std::string decompiled_code;
    CallingConvention detected_convention;
    std::string error_message;              ///< Error message if success=false
};

/**
 * @struct CompileResult
 * @brief Result from Stage 2: compile_replacement()
 */
struct CompileResult {
    bool success;                           ///< true if compilation succeeded
    std::string compiled_assembly;          ///< Generated assembly code (Intel syntax)
    std::vector<std::string> resolved_symbols; ///< Symbols resolved via IDA
    std::string final_c_code;               ///< C code with injected #defines
    std::string error_message;              ///< Detailed error (compilation/symbol resolution)
};

/**
 * @struct PreviewResult
 * @brief Result from Stage 3: preview_semantic_patch() **MANDATORY SAFETY GATE**
 */
struct PreviewResult {
    bool success;                           ///< true if preview generated successfully
    std::string original_assembly;          ///< Original function disassembly
    std::string new_assembly;               ///< Compiled replacement assembly
    CallingConvention original_convention;  ///< Detected original convention
    CallingConvention new_convention;       ///< Compiled code's convention
    bool abi_compatible;                    ///< CRITICAL: true if safe to replace
    std::vector<std::string> warnings;      ///< ABI incompatibility warnings
    std::string analysis;                   ///< Human-readable compatibility analysis
    std::string error_message;              ///< Error message if success=false
};

/**
 * @struct FinalizeResult
 * @brief Result from Stage 4: finalize_semantic_patch()
 */
struct FinalizeResult {
    bool success;                           ///< true if patch applied successfully
    ea_t original_function;                 ///< Original function address
    ea_t new_function_address;              ///< Injected code address
    std::string error_message;              ///< Detailed error if failed
};

/**
 * @class SemanticPatchManager
 * @brief Orchestrates the complete decompile-transform-recompile semantic patching workflow
 *
 * Coordinates four subsystems:
 *   - CallingConventionAnalyzer: Detect and verify ABIs
 *   - CCompiler: Compile C code with symbol resolution
 *   - PatchManager: Apply binary patches and assemble code
 *   - CodeInjectionManager: Allocate code segments
 *
 * Session-based workflow ensures state isolation and mandatory verification gates.
 * Multiple concurrent sessions are supported (each function replacement is independent).
 */
class SemanticPatchManager {
public:
    SemanticPatchManager(PatchManager* patch_manager, CodeInjectionManager* code_injection_manager);
    ~SemanticPatchManager();

    /**
     * @brief Stage 1: Start semantic patch session
     * @param function_address Address of function to patch
     * @return StartPatchResult with session_id, decompiled code, and detected convention
     *
     * Workflow:
     *   1. Verify function_address points to valid function
     *   2. Decompile via Hex-Rays (requires Hex-Rays decompiler)
     *   3. Detect calling convention via IDA type info or prologue analysis
     *   4. Create session with unique ID
     *   5. Return decompiled C code for agent modification
     *
     * Failure cases:
     *   - Not a function
     *   - Hex-Rays decompilation failed
     *   - Decompilation produced empty output
     *
     * @note Thread-safe: Uses execute_sync_wrapper for all IDA operations
     */
    StartPatchResult start_semantic_patch(ea_t function_address);

    /**
     * @brief Stage 2: Compile replacement code with automatic symbol and type resolution
     * @param session_id Session ID from start_semantic_patch()
     * @param c_code Modified C code from agent
     * @param max_iterations Maximum compile-resolve-inject cycles (default 10)
     * @return CompileResult with compiled assembly and resolved symbols/types
     *
     * Iterative resolution workflow:
     *   1. Try to compile C code
     *   2. Parse compiler errors for undefined symbols/types/globals
     *   3. Resolve them from IDA database (addresses for symbols, definitions for types)
     *   4. Inject resolutions and repeat until success or max iterations
     *
     * @note Thread-safe: Uses execute_sync_wrapper
     */
    CompileResult compile_replacement(const std::string& session_id, const std::string& c_code, int max_iterations = 10);

    /**
     * @brief Stage 3: Preview patch and verify ABI compatibility **MANDATORY SAFETY GATE**
     * @param session_id Session ID from start_semantic_patch()
     * @return PreviewResult with ABI compatibility analysis and warnings
     *
     * Workflow:
     *   1. Generate disassembly of original function
     *   2. Analyze compiled assembly's calling convention
     *   3. Compare original_convention vs compiled_convention
     *   4. Generate compatibility warnings if conventions mismatch
     *   5. Provide before/after comparison
     *   6. Mark session as previewed (required for finalization)
     *
     * **CRITICAL: Agent must check abi_compatible flag before calling finalize!**
     * If abi_compatible=false, warnings explain what's wrong (register mismatch, etc.)
     *
     * Failure cases:
     *   - Invalid session_id
     *   - Must call compile_replacement first
     *   - Original function no longer exists
     *
     * @note Thread-safe: Uses execute_sync_wrapper
     */
    PreviewResult preview_semantic_patch(const std::string& session_id);

    /**
     * @brief Stage 4: Finalize semantic patch - inject code and redirect original function
     * @param session_id Session ID from start_semantic_patch()
     * @return FinalizeResult with injected address and patch instruction
     *
     * Workflow:
     *   1. Verify preview was called and ABI is compatible (BLOCKS if not!)
     *   2. Allocate temporary code segment via CodeInjectionManager
     *   3. Assemble compiler output to machine code via Keystone
     *   4. Write machine code to allocated segment
     *   5. Patch original function's first bytes with JMP to new code
     *   6. Mark session as finalized
     *
     * **SAFETY GUARANTEE: Cannot finalize if abi_compatible=false**
     *
     * Failure cases:
     *   - Invalid session_id
     *   - Preview not called
     *   - ABI incompatible (BLOCKED)
     *   - Assembly failed (invalid instruction)
     *   - Code too large for allocated workspace
     *   - JMP patch failed
     *
     * @note Thread-safe: Uses execute_sync_wrapper, write uses MFF_WRITE
     */
    FinalizeResult finalize_semantic_patch(const std::string& session_id);

    /**
     * @brief Check if a session exists
     * @param session_id Session ID to check
     * @return true if session exists
     */
    bool has_session(const std::string& session_id) const;

    /**
     * @brief Cancel and delete a session
     * @param session_id Session ID to cancel
     *
     * Use this if agent decides not to proceed with patching.
     * Does not revert any finalized patches (use PatchManager for that).
     */
    void cancel_session(const std::string& session_id);

    /**
     * @brief Get list of all active session IDs
     * @return Vector of session ID strings
     */
    std::vector<std::string> get_active_sessions() const;

private:
    // Core components
    PatchManager* patch_manager_;
    CodeInjectionManager* code_injection_manager_;
    std::unique_ptr<CCompiler> compiler_;
    std::unique_ptr<CallingConventionAnalyzer> convention_analyzer_;

    // Session storage
    std::map<std::string, SemanticPatchSession> sessions_;
    mutable std::mutex sessions_mutex_;

    // Helper methods
    std::string generate_session_id(ea_t function_address);
    SemanticPatchSession* get_session(const std::string& session_id);

    // Decompilation helpers
    std::string decompile_function(ea_t func_addr);

    // Patching helpers
    bool patch_function_with_jump(ea_t original_func, ea_t new_func);
    std::string generate_far_jump_for_architecture(ea_t from, ea_t to);

    // Machine code extraction from object file
    struct AssembleResult {
        bool success;
        std::vector<uint8_t> machine_code;
        std::string error_message;  // Detailed error for agent
    };
    AssembleResult extract_machine_code_from_object(const std::string& object_path);
};

} // namespace llm_re::semantic

#endif // SEMANTIC_PATCH_MANAGER_H
