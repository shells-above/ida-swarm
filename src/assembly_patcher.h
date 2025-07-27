//
// Created by user on 11/27/24.
//

#ifndef ASSEMBLY_PATCHER_H
#define ASSEMBLY_PATCHER_H

#include "common.h"
#include <keystone/keystone.h>
#include <vector>
#include <string>
#include <memory>

namespace llm_re {

// Forward declaration
class PatchManager;

// Supported architectures
enum class Architecture {
    X86_32,
    X86_64,
    ARM32,
    ARM64,
    UNKNOWN
};

// Result of assembly operation
struct AssemblyResult {
    bool success;
    std::vector<uint8_t> bytes;
    std::string error_message;
    size_t statement_count;
};

// Assembles instructions using Keystone Engine
class AssemblyPatcher {
public:
    AssemblyPatcher(PatchManager* patch_manager);
    ~AssemblyPatcher();

    // Initialize with current IDA architecture
    bool initialize();

    // Assemble a single instruction or multiple instructions
    AssemblyResult assemble(const std::string& assembly, ea_t address);

    // Apply an assembly patch
    bool apply_patch(ea_t address, const std::string& new_assembly,
                    const std::string& description,
                    bool verify_original = true,
                    const std::string& expected_original_asm = "");

    // Get current architecture
    Architecture get_architecture() const { return current_arch_; }

    // Get architecture name as string
    std::string get_architecture_name() const;

    // Check if an instruction will fit in given space
    bool will_fit(const std::string& assembly, ea_t address, size_t max_size);

    // Auto-NOP remainder bytes if new instruction is smaller
    bool apply_patch_with_nop(ea_t address, const std::string& new_assembly,
                             size_t original_size, const std::string& description);

    // Validate assembly syntax without assembling
    bool validate_syntax(const std::string& assembly);

    // Get the size of assembled instructions without applying
    std::optional<size_t> get_assembled_size(const std::string& assembly, ea_t address);

    // Disassemble bytes at address (for verification)
    std::string disassemble_at(ea_t address, size_t max_bytes = 16);

    // Get NOP instruction for current architecture
    std::vector<uint8_t> get_nop_bytes(size_t count);

private:
    PatchManager* patch_manager_;
    ks_engine* ks_;
    Architecture current_arch_;
    ks_arch ks_arch_;
    ks_mode ks_mode_;

    // Initialize Keystone for specific architecture
    bool init_keystone(Architecture arch);

    // Cleanup Keystone
    void cleanup_keystone();

    // Detect architecture from IDA
    Architecture detect_architecture();

    // Convert IDA processor name to our architecture enum
    Architecture processor_to_arch(const std::string& processor);

    // Get Keystone architecture and mode from our enum
    void get_keystone_params(Architecture arch, ks_arch& out_arch, ks_mode& out_mode);

    // Normalize assembly syntax for consistency
    std::string normalize_assembly(const std::string& assembly);

public:
    // Split assembly string into individual instructions
    std::vector<std::string> split_instructions(const std::string& assembly);
};

} // namespace llm_re

#endif // ASSEMBLY_PATCHER_H