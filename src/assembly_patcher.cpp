//
// Created by user on 11/27/24.
//

#include "assembly_patcher.h"
#include "patch_manager.h"
#include <ida.hpp>
#include <idp.hpp>
#include <ua.hpp>
#include <allins.hpp>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace llm_re {

AssemblyPatcher::AssemblyPatcher(PatchManager* patch_manager) 
    : patch_manager_(patch_manager), ks_(nullptr), current_arch_(Architecture::UNKNOWN) {
}

AssemblyPatcher::~AssemblyPatcher() {
    cleanup_keystone();
}

bool AssemblyPatcher::initialize() {
    current_arch_ = detect_architecture();
    if (current_arch_ == Architecture::UNKNOWN) {
        return false;
    }
    
    return init_keystone(current_arch_);
}

AssemblyResult AssemblyPatcher::assemble(const std::string& assembly, ea_t address) {
    AssemblyResult result;
    result.success = false;
    
    if (!ks_) {
        result.error_message = "Keystone not initialized";
        return result;
    }
    
    // Normalize assembly
    std::string normalized = normalize_assembly(assembly);
    
    // Assemble
    unsigned char* encode = nullptr;
    size_t size = 0;
    size_t count = 0;
    
    int ks_result = ks_asm(ks_, normalized.c_str(), address, &encode, &size, &count);
    
    if (ks_result != KS_ERR_OK) {
        result.error_message = "Assembly failed: ";
        result.error_message += ks_strerror(ks_errno(ks_));
        return result;
    }
    
    // Copy bytes
    result.bytes.assign(encode, encode + size);
    result.statement_count = count;
    result.success = true;
    
    // Free Keystone allocated memory
    ks_free(encode);
    
    return result;
}

bool AssemblyPatcher::apply_patch(ea_t address, const std::string& new_assembly,
                                const std::string& description,
                                bool verify_original,
                                const std::string& expected_original_asm) {
    // Get current assembly at address if we need to verify
    std::string original_asm;
    std::vector<uint8_t> original_bytes;
    
    if (verify_original || !expected_original_asm.empty()) {
        original_asm = disassemble_at(address);
        if (original_asm.empty()) {
            return false;
        }
        
        // If expected assembly provided, verify it matches
        if (!expected_original_asm.empty()) {
            // Normalize both for comparison
            std::string norm_original = normalize_assembly(original_asm);
            std::string norm_expected = normalize_assembly(expected_original_asm);
            
            if (norm_original != norm_expected) {
                return false;
            }
        }
        
        // Get original bytes
        insn_t insn;
        if (decode_insn(&insn, address) > 0) {
            original_bytes = patch_manager_->read_bytes(address, insn.size);
        }
    }
    
    // Assemble new instruction
    AssemblyResult asm_result = assemble(new_assembly, address);
    if (!asm_result.success) {
        return false;
    }
    
    // Apply patch
    PatchResult patch_result = patch_manager_->apply_assembly_patch(
        address, asm_result.bytes, original_asm, new_assembly,
        description, verify_original, original_bytes);
    
    return patch_result.success;
}

bool AssemblyPatcher::apply_patch_with_nop(ea_t address, const std::string& new_assembly,
                                          size_t original_size, const std::string& description) {
    // Assemble new instruction
    AssemblyResult asm_result = assemble(new_assembly, address);
    if (!asm_result.success) {
        return false;
    }
    
    // Check if new instruction fits
    if (asm_result.bytes.size() > original_size) {
        return false;
    }
    
    std::vector<uint8_t> patched_bytes = asm_result.bytes;
    
    // Add NOPs if needed
    if (asm_result.bytes.size() < original_size) {
        size_t nop_count = original_size - asm_result.bytes.size();
        std::vector<uint8_t> nops = get_nop_bytes(nop_count);
        patched_bytes.insert(patched_bytes.end(), nops.begin(), nops.end());
    }
    
    // Get original assembly
    std::string original_asm = disassemble_at(address, original_size);
    
    // Apply patch
    PatchResult patch_result = patch_manager_->apply_assembly_patch(
        address, patched_bytes, original_asm, 
        new_assembly + " + NOPs", description, false, {});
    
    return patch_result.success;
}

bool AssemblyPatcher::will_fit(const std::string& assembly, ea_t address, size_t max_size) {
    auto size = get_assembled_size(assembly, address);
    return size.has_value() && size.value() <= max_size;
}

bool AssemblyPatcher::validate_syntax(const std::string& assembly) {
    // Try to assemble at address 0 as a syntax check
    AssemblyResult result = assemble(assembly, 0);
    return result.success;
}

std::optional<size_t> AssemblyPatcher::get_assembled_size(const std::string& assembly, ea_t address) {
    AssemblyResult result = assemble(assembly, address);
    if (result.success) {
        return result.bytes.size();
    }
    return std::nullopt;
}

std::string AssemblyPatcher::disassemble_at(ea_t address, size_t max_bytes) {
    std::string result;
    
    insn_t insn;
    if (decode_insn(&insn, address) == 0) {
        return result;
    }
    
    // Generate disassembly text
    qstring mnem;
    print_insn_mnem(&mnem, address);
    result = mnem.c_str();
    
    // Add operands
    qstring ops;
    if (print_operand(&ops, address, 0)) {
        result += " ";
        result += ops.c_str();
        
        // Add remaining operands
        for (int i = 1; i < UA_MAXOP; i++) {
            ops.clear();
            if (print_operand(&ops, address, i)) {
                result += ", ";
                result += ops.c_str();
            }
        }
    }
    
    return result;
}

std::vector<uint8_t> AssemblyPatcher::get_nop_bytes(size_t count) {
    std::vector<uint8_t> nops;
    
    switch (current_arch_) {
        case Architecture::X86_32:
        case Architecture::X86_64:
            // x86 NOP is 0x90
            nops.resize(count, 0x90);
            break;
            
        case Architecture::ARM32:
            // ARM NOP is 0x00 0x00 0x00 0x00 (MOV R0, R0)
            while (count >= 4) {
                nops.push_back(0x00);
                nops.push_back(0x00);
                nops.push_back(0x00);
                nops.push_back(0x00);
                count -= 4;
            }
            break;
            
        case Architecture::ARM64:
            // ARM64 NOP is 0x1F 0x20 0x03 0xD5
            while (count >= 4) {
                nops.push_back(0x1F);
                nops.push_back(0x20);
                nops.push_back(0x03);
                nops.push_back(0xD5);
                count -= 4;
            }
            break;
            
        default:
            break;
    }
    
    return nops;
}

bool AssemblyPatcher::init_keystone(Architecture arch) {
    cleanup_keystone();
    
    get_keystone_params(arch, ks_arch_, ks_mode_);
    
    ks_err err = ks_open(ks_arch_, ks_mode_, &ks_);
    if (err != KS_ERR_OK) {
        return false;
    }
    
    // Set syntax based on architecture
    if (arch == Architecture::X86_32 || arch == Architecture::X86_64) {
        // Use Intel syntax by default
        ks_option(ks_, KS_OPT_SYNTAX, KS_OPT_SYNTAX_INTEL);
    }
    
    return true;
}

void AssemblyPatcher::cleanup_keystone() {
    if (ks_) {
        ks_close(ks_);
        ks_ = nullptr;
    }
}

Architecture AssemblyPatcher::detect_architecture() {
    // Get processor name from IDA
    std::string procname = inf_get_procname().c_str();
    
    // Convert to lowercase for comparison
    std::transform(procname.begin(), procname.end(), procname.begin(), ::tolower);
    
    return processor_to_arch(procname);
}

Architecture AssemblyPatcher::processor_to_arch(const std::string& processor) {
    if (processor.find("x86") != std::string::npos || 
        processor.find("x64") != std::string::npos ||
        processor == "metapc") {
        // Check if 64-bit
        if (inf_is_64bit()) {
            return Architecture::X86_64;
        } else {
            return Architecture::X86_32;
        }
    } else if (processor.find("arm") != std::string::npos) {
        if (inf_is_64bit()) {
            return Architecture::ARM64;
        } else {
            return Architecture::ARM32;
        }
    }
    
    return Architecture::UNKNOWN;
}

void AssemblyPatcher::get_keystone_params(Architecture arch, ks_arch& out_arch, ks_mode& out_mode) {
    switch (arch) {
        case Architecture::X86_32:
            out_arch = KS_ARCH_X86;
            out_mode = KS_MODE_32;
            break;
            
        case Architecture::X86_64:
            out_arch = KS_ARCH_X86;
            out_mode = KS_MODE_64;
            break;
            
        case Architecture::ARM32:
            out_arch = KS_ARCH_ARM;
            out_mode = KS_MODE_ARM;
            break;
            
        case Architecture::ARM64:
            out_arch = KS_ARCH_ARM64;
            out_mode = ks_mode(0);  // ARM64 doesn't need additional mode
            break;
            
        default:
            out_arch = KS_ARCH_X86;
            out_mode = KS_MODE_32;
            break;
    }
}

std::string AssemblyPatcher::normalize_assembly(const std::string& assembly) {
    std::string normalized = assembly;
    
    // Trim whitespace
    size_t start = normalized.find_first_not_of(" \t\n\r");
    if (start != std::string::npos) {
        normalized = normalized.substr(start);
    }
    size_t end = normalized.find_last_not_of(" \t\n\r");
    if (end != std::string::npos) {
        normalized = normalized.substr(0, end + 1);
    }
    
    // Handle common instruction aliases
    if (current_arch_ == Architecture::X86_32 || current_arch_ == Architecture::X86_64) {
        // Replace retn with ret
        if (normalized == "retn" || normalized.find("retn ") == 0) {
            normalized.replace(0, 4, "ret");
        }
    }
    
    return normalized;
}

std::vector<std::string> AssemblyPatcher::split_instructions(const std::string& assembly) {
    std::vector<std::string> instructions;
    std::stringstream ss(assembly);
    std::string line;
    
    while (std::getline(ss, line, ';')) {
        // Also split by newline
        std::stringstream line_ss(line);
        std::string instruction;
        
        while (std::getline(line_ss, instruction, '\n')) {
            // Trim whitespace
            size_t start = instruction.find_first_not_of(" \t");
            if (start != std::string::npos) {
                instruction = instruction.substr(start);
                size_t end = instruction.find_last_not_of(" \t");
                if (end != std::string::npos) {
                    instruction = instruction.substr(0, end + 1);
                }
                
                if (!instruction.empty()) {
                    instructions.push_back(instruction);
                }
            }
        }
    }
    
    return instructions;
}

std::string AssemblyPatcher::get_architecture_name() const {
    switch (current_arch_) {
        case Architecture::X86_32: return "x86 (32-bit)";
        case Architecture::X86_64: return "x86-64";
        case Architecture::ARM32: return "ARM (32-bit)";
        case Architecture::ARM64: return "ARM64";
        default: return "Unknown";
    }
}

} // namespace llm_re