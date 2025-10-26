//
// Created by user on 7/27/25.
//

#include "patching/patch_manager.h"
#include "patching/code_injection_manager.h"
#include "core/ida_utils.h"
#include "core/logger.h"
#include "core/ida_validators.h"
#include <loader.hpp>
#include <fstream>

// LIEF headers for binary modification
#include <LIEF/LIEF.hpp>

namespace llm_re {

// Constructor
PatchManager::PatchManager() {
    // Constructor will be called from worker thread
}

// Destructor
PatchManager::~PatchManager() {
    cleanup_keystone();
}

// Initialize patch manager
bool PatchManager::initialize() {
    // All IDA operations must be in sync wrapper
    return IDAUtils::execute_sync_wrapper([this]() -> bool {
        // Initialize Keystone assembler
        if (!init_keystone()) {
            LOG("WARNING: Failed to initialize Keystone assembler. Assembly patching will be unavailable.\n");
            // Don't fail initialization - byte patching can still work
            return true;
        }
        
        return true;
    }, MFF_READ);
}

// Core method 1: Apply byte patch with verification
BytePatchResult PatchManager::apply_byte_patch(ea_t address, 
                                              const std::string& original_hex,
                                              const std::string& new_hex,
                                              const std::string& description) {
    // Execute in thread-safe context
    return IDAUtils::execute_sync_wrapper([&]() -> BytePatchResult {
        BytePatchResult result;
        
        // Validate hex strings
        if (!is_valid_hex_string(original_hex)) {
            result.error_message = "Invalid original hex string format";
            return result;
        }
        
        if (!is_valid_hex_string(new_hex)) {
            result.error_message = "Invalid new hex string format";
            return result;
        }
        
        // Convert hex to bytes
        std::vector<uint8_t> expected_bytes = hex_string_to_bytes(original_hex);
        std::vector<uint8_t> new_bytes = hex_string_to_bytes(new_hex);
        
        if (expected_bytes.empty()) {
            result.error_message = "Original bytes cannot be empty";
            return result;
        }
        
        if (new_bytes.empty()) {
            result.error_message = "New bytes cannot be empty";
            return result;
        }
        
        // Validate address
        std::string error_msg;
        if (!validate_address(address, error_msg)) {
            result.error_message = error_msg;
            return result;
        }
        
        // Skip instruction boundary check for byte patches - they should work on data too
        // The alignment check below is also not needed for data patches
        
        // Verify original bytes match
        if (!verify_original_bytes(address, expected_bytes, error_msg)) {
            result.error_message = error_msg;
            return result;
        }
        
        // Check patch size fits
        if (!validate_patch_size(address, expected_bytes.size(), new_bytes.size(), error_msg)) {
            result.error_message = error_msg;
            return result;
        }
        
        // Check if already patched
        if (patches_.find(address) != patches_.end()) {
            result.error_message = "Address already patched. Revert existing patch first.";
            return result;
        }
        
        // Apply the patch
        if (!write_bytes(address, new_bytes)) {
            result.error_message = "Failed to write bytes to memory";
            return result;
        }
        
        // Create patch entry
        PatchEntry entry;
        entry.address = address;
        entry.original_bytes = expected_bytes;
        entry.patched_bytes = new_bytes;
        entry.description = description;
        entry.timestamp = std::chrono::system_clock::now();
        entry.is_assembly_patch = false;
        
        // Store patch
        patches_[address] = entry;
        
        // Trigger reanalysis
        trigger_reanalysis(address, new_bytes.size());
        
        result.success = true;
        result.bytes_patched = new_bytes.size();
        return result;
    }, MFF_WRITE);
}

// Core method 2: Apply assembly patch with verification
AssemblyPatchResult PatchManager::apply_assembly_patch(ea_t address,
                                                     const std::string& original_asm,
                                                     const std::string& new_asm,
                                                     const std::string& description) {
    // Execute in thread-safe context
    return IDAUtils::execute_sync_wrapper([&]() -> AssemblyPatchResult {
        AssemblyPatchResult result;
        
        // Validate address
        std::string error_msg;
        if (!validate_address(address, error_msg)) {
            result.error_message = error_msg;
            return result;
        }
        
        // Check instruction boundary
        if (!validate_instruction_boundary(address, error_msg)) {
            result.error_message = error_msg;
            return result;
        }
        
        // Verify original assembly matches
        if (!verify_original_asm(address, original_asm, error_msg)) {
            result.error_message = error_msg;
            return result;
        }
        
        // Check if Keystone is available
        if (!ks_) {
            result.error_message = "Keystone assembler not initialized. Cannot perform assembly patching. " 
                                 "Supported architectures: x86, x86_64, ARM, ARM64, PowerPC, MIPS, SPARC.";
            return result;
        }
        
        // Assemble new instruction
        auto [success, new_bytes] = assemble_instruction(new_asm, address);
        if (!success) {
            result.error_message = "Failed to assemble instruction: '" + new_asm + 
                                 "' at address " + IDAValidators::format_address_hex(address) +
                                 ". Check syntax and ensure instruction is valid for current processor.";
            return result;
        }
        
        // Get original instruction size
        insn_t insn;
        if (decode_insn(&insn, address) == 0) {
            result.error_message = "Failed to decode original instruction";
            return result;
        }
        size_t original_size = insn.size;
        
        // Check if new instruction fits
        if (new_bytes.size() > original_size) {
            result.error_message = "New instruction too large. Original: " + 
                                 std::to_string(original_size) + " bytes, New: " + 
                                 std::to_string(new_bytes.size()) + " bytes";
            return result;
        }
        
        // Check alignment requirements for RISC architectures
        if ((PH.id == PLFM_ARM && !inf_is_64bit() && !(get_sreg(address, str2reg("T")) & 1)) ||
            (PH.id == PLFM_ARM && inf_is_64bit()) ||
            PH.id == PLFM_PPC || PH.id == PLFM_MIPS || PH.id == PLFM_SPARC) {
            
            // Check 4-byte alignment for 32-bit instructions
            if (address % 4 != 0) {
                result.error_message = "Address " + IDAValidators::format_address_hex(address) + 
                                     " is not 4-byte aligned. This architecture requires 4-byte alignment for instructions.";
                return result;
            }
            
            // Check new instruction size is multiple of 4
            if (new_bytes.size() % 4 != 0) {
                result.error_message = "Assembled instruction size (" + std::to_string(new_bytes.size()) + 
                                     " bytes) is not a multiple of 4. This architecture requires 4-byte aligned instructions.";
                return result;
            }
        } else if (PH.id == PLFM_ARM && !inf_is_64bit() && (get_sreg(address, str2reg("T")) & 1)) {
            // Thumb mode - check 2-byte alignment
            if (address % 2 != 0) {
                result.error_message = "Address " + IDAValidators::format_address_hex(address) + 
                                     " is not 2-byte aligned. ARM Thumb mode requires 2-byte alignment.";
                return result;
            }
        }
        
        // Add NOP padding if needed
        size_t nops_needed = 0;
        if (new_bytes.size() < original_size) {
            nops_needed = original_size - new_bytes.size();
            
            // Check if padding would violate alignment
            if ((PH.id == PLFM_PPC || PH.id == PLFM_MIPS || PH.id == PLFM_SPARC) && nops_needed % 4 != 0) {
                result.error_message = "Cannot add " + std::to_string(nops_needed) + 
                                     " bytes of NOP padding. This architecture requires padding to be a multiple of 4 bytes. " +
                                     "Original instruction: " + std::to_string(original_size) + 
                                     " bytes, new instruction: " + std::to_string(new_bytes.size()) + " bytes.";
                return result;
            } else if (PH.id == PLFM_ARM) {
                if (inf_is_64bit() && nops_needed % 4 != 0) {
                    // ARM64 requires 4-byte alignment
                    result.error_message = "Cannot add " + std::to_string(nops_needed) + 
                                         " bytes of NOP padding. ARM64 requires padding to be a multiple of 4 bytes.";
                    return result;
                } else if (!inf_is_64bit()) {
                    bool is_thumb = (get_sreg(address, str2reg("T")) & 1) != 0;
                    if (is_thumb && nops_needed % 2 != 0) {
                        result.error_message = "Cannot add " + std::to_string(nops_needed) + 
                                             " bytes of NOP padding. ARM Thumb mode requires padding to be a multiple of 2 bytes.";
                        return result;
                    } else if (!is_thumb && nops_needed % 4 != 0) {
                        result.error_message = "Cannot add " + std::to_string(nops_needed) + 
                                             " bytes of NOP padding. ARM mode requires padding to be a multiple of 4 bytes.";
                        return result;
                    }
                }
            }
            
            std::vector<uint8_t> nop_bytes = get_nop_bytes(nops_needed, address);
            new_bytes.insert(new_bytes.end(), nop_bytes.begin(), nop_bytes.end());
        }
        
        // Check if already patched
        if (patches_.find(address) != patches_.end()) {
            result.error_message = "Address already patched. Revert existing patch first.";
            return result;
        }
        
        // Read original bytes
        std::vector<uint8_t> original_bytes = read_bytes(address, original_size);
        
        // Apply the patch
        if (!write_bytes(address, new_bytes)) {
            result.error_message = "Failed to write bytes to memory";
            return result;
        }
        
        // Create patch entry
        PatchEntry entry;
        entry.address = address;
        entry.original_bytes = original_bytes;
        entry.patched_bytes = new_bytes;
        entry.description = description;
        entry.timestamp = std::chrono::system_clock::now();
        entry.is_assembly_patch = true;
        entry.original_asm = original_asm;
        entry.patched_asm = new_asm;
        
        // Store patch
        patches_[address] = entry;
        
        // Trigger reanalysis
        trigger_reanalysis(address, new_bytes.size());
        
        result.success = true;
        result.bytes_patched = new_bytes.size() - nops_needed;
        result.nops_added = nops_needed;
        return result;
    }, MFF_WRITE);
}

// Core method 2b: Apply segment injection
SegmentInjectionResult PatchManager::apply_segment_injection(
    ea_t address,
    size_t size,
    const std::vector<uint8_t>& code,
    const std::string& segment_name,
    const std::string& description) {

    return IDAUtils::execute_sync_wrapper([&]() -> SegmentInjectionResult {
        SegmentInjectionResult result;

        // Validate address not already patched
        if (patches_.find(address) != patches_.end()) {
            result.error_message = "Address already patched. Revert existing patch first.";
            return result;
        }

        // Validate code size fits in segment size
        if (code.size() > size) {
            result.error_message = "Code size (" + std::to_string(code.size()) +
                                  ") exceeds segment size (" + std::to_string(size) + ")";
            return result;
        }

        // 1. Create segment in IDA database
        if (!create_segment_in_ida(address, size, segment_name)) {
            result.error_message = "Failed to create segment in IDA database";
            return result;
        }

        // 2. Write code to segment in IDA
        patch_bytes(address, code.data(), code.size());

        // 3. Add segment to binary file via LIEF (if binary path set)
        if (!binary_path_.empty()) {
            if (!add_segment_to_binary_with_lief(address, size, segment_name, code)) {
                result.error_message = "Failed to add segment to binary file";
                // Rollback: delete IDA segment
                del_segm(address, SEGMOD_KILL);
                return result;
            }
        }

        // 4. Track the injection in patches_
        PatchEntry entry;
        entry.address = address;
        entry.original_bytes = {};  // No original bytes for new segment
        entry.patched_bytes = code;
        entry.description = description;
        entry.timestamp = std::chrono::system_clock::now();
        entry.is_assembly_patch = false;
        entry.is_segment_injection = true;
        entry.segment_name = segment_name;
        entry.segment_size = size;

        patches_[address] = entry;

        LOG("PatchManager: Segment injection successful at 0x%llX, size 0x%zX\n",
            (uint64_t)address, size);

        result.success = true;
        result.segment_address = address;
        result.segment_name = segment_name;
        result.allocated_size = size;

        return result;
    }, MFF_WRITE);
}

// Core method 3: Revert patch
bool PatchManager::revert_patch(ea_t address) {
    return IDAUtils::execute_sync_wrapper([&]() -> bool {
        auto it = patches_.find(address);
        if (it == patches_.end()) {
            return false;
        }

        const PatchEntry& patch = it->second;

        if (patch.is_segment_injection) {
            // Revert segment injection
            LOG("Reverting segment injection at 0x%llX ('%s')\n",
                (uint64_t)address, patch.segment_name.c_str());

            // 1. Delete segment from IDA database
            if (!del_segm(address, SEGMOD_KILL)) {
                LOG("ERROR: Failed to delete segment from IDA at 0x%llX\n", (uint64_t)address);
                return false;
            }

            // 2. Remove segment from binary file
            if (!binary_path_.empty()) {
                if (!remove_segment_from_binary(address, patch.segment_name)) {
                    LOG("WARNING: Removed from IDA but failed to remove from binary file\n");
                    // Continue - IDA is reverted which is critical
                }
            }

            LOG("Successfully reverted segment injection at 0x%llX\n", (uint64_t)address);
        } else {
            // Revert byte/assembly patch (existing logic)
            if (!write_bytes(address, patch.original_bytes)) {
                return false;
            }

            trigger_reanalysis(address, patch.original_bytes.size());
        }

        // Remove from patches map
        patches_.erase(it);

        return true;
    }, MFF_WRITE);
}

bool PatchManager::revert_all() {
    return IDAUtils::execute_sync_wrapper([&]() -> bool {
        // Collect all addresses
        std::vector<ea_t> addresses;
        for (const auto& [addr, _] : patches_) {
            addresses.push_back(addr);
        }
        
        // Revert each patch
        bool all_success = true;
        for (ea_t addr : addresses) {
            auto it = patches_.find(addr);
            if (it != patches_.end()) {
                if (write_bytes(addr, it->second.original_bytes)) {
                    trigger_reanalysis(addr, it->second.original_bytes.size());
                } else {
                    all_success = false;
                }
            }
        }
        
        // Clear patches if all successful
        if (all_success) {
            patches_.clear();
        }
        
        return all_success;
    }, MFF_WRITE);
}

// Core method 4: List patches
std::vector<PatchInfo> PatchManager::list_patches() const {
    return IDAUtils::execute_sync_wrapper([&]() -> std::vector<PatchInfo> {
        std::vector<PatchInfo> result;
        
        for (const auto& [addr, patch] : patches_) {
            PatchInfo info;
            info.address = addr;
            info.original_bytes_hex = bytes_to_hex_string(patch.original_bytes);
            info.patched_bytes_hex = bytes_to_hex_string(patch.patched_bytes);
            info.description = patch.description;
            info.timestamp = patch.timestamp;
            info.is_assembly_patch = patch.is_assembly_patch;
            info.original_asm = patch.original_asm;
            info.patched_asm = patch.patched_asm;
            result.push_back(info);
        }
        
        return result;
    }, MFF_READ);
}

std::optional<PatchInfo> PatchManager::get_patch_info(ea_t address) const {
    return IDAUtils::execute_sync_wrapper([&]() -> std::optional<PatchInfo> {
        auto it = patches_.find(address);
        if (it == patches_.end()) {
            return std::nullopt;
        }
        
        PatchInfo info;
        info.address = it->first;
        info.original_bytes_hex = bytes_to_hex_string(it->second.original_bytes);
        info.patched_bytes_hex = bytes_to_hex_string(it->second.patched_bytes);
        info.description = it->second.description;
        info.timestamp = it->second.timestamp;
        info.is_assembly_patch = it->second.is_assembly_patch;
        info.original_asm = it->second.original_asm;
        info.patched_asm = it->second.patched_asm;
        
        return info;
    }, MFF_READ);
}

// Get statistics
PatchStatistics PatchManager::get_statistics() const {
    return IDAUtils::execute_sync_wrapper([&]() -> PatchStatistics {
        PatchStatistics stats = {0, 0, 0, 0};
        
        for (const auto& [_, patch] : patches_) {
            stats.total_patches++;
            if (patch.is_assembly_patch) {
                stats.assembly_patches++;
            } else {
                stats.byte_patches++;
            }
            stats.total_bytes_patched += patch.patched_bytes.size();
        }
        
        return stats;
    }, MFF_READ);
}

// Safety validation methods
bool PatchManager::validate_address(ea_t address, std::string& error_msg) {
    if (!IDAValidators::is_valid_address(address)) {
        error_msg = "Invalid address: " + IDAValidators::format_address_hex(address);
        return false;
    }
    return true;
}

bool PatchManager::validate_instruction_boundary(ea_t address, std::string& error_msg) {
    // First check alignment based on architecture
    if (PH.id == PLFM_ARM) {
        if (inf_is_64bit()) {
            // ARM64 requires 4-byte alignment
            if (address % 4 != 0) {
                error_msg = "Address is not 4-byte aligned (required for ARM64)";
                return false;
            }
        } else {
            // ARM32 - check if Thumb mode
            bool is_thumb = (get_sreg(address, str2reg("T")) & 1) != 0;
            if (is_thumb && address % 2 != 0) {
                error_msg = "Address is not 2-byte aligned (required for ARM Thumb mode)";
                return false;
            } else if (!is_thumb && address % 4 != 0) {
                error_msg = "Address is not 4-byte aligned (required for ARM mode)";
                return false;
            }
        }
    } else if (PH.id == PLFM_PPC || PH.id == PLFM_MIPS || PH.id == PLFM_SPARC) {
        // All these RISC architectures require 4-byte alignment
        if (address % 4 != 0) {
            error_msg = "Address is not 4-byte aligned (required for this RISC architecture)";
            return false;
        }
    }
    
    // Then check if it's at instruction boundary
    insn_t insn;
    if (decode_insn(&insn, address) == 0) {
        error_msg = "Address is not at instruction boundary";
        return false;
    }
    return true;
}

bool PatchManager::validate_patch_size(ea_t address, size_t old_size, size_t new_size, std::string& error_msg) {
    // For byte patches, sizes must match exactly
    if (old_size != new_size) {
        error_msg = "Patch size mismatch. Original: " + std::to_string(old_size) + 
                   " bytes, New: " + std::to_string(new_size) + " bytes";
        return false;
    }
    
    // Check if patch extends beyond segment
    segment_t* seg = getseg(address);
    if (!seg) {
        error_msg = "Address not in any segment";
        return false;
    }
    
    if (address + new_size > seg->end_ea) {
        error_msg = "Patch extends beyond segment boundary";
        return false;
    }
    
    return true;
}

bool PatchManager::verify_original_bytes(ea_t address, const std::vector<uint8_t>& expected, std::string& error_msg) {
    std::vector<uint8_t> actual = read_bytes(address, expected.size());
    
    if (actual != expected) {
        error_msg = "Original bytes do not match. Expected: " + bytes_to_hex_string(expected) +
                   ", Actual: " + bytes_to_hex_string(actual);
        return false;
    }
    
    return true;
}

bool PatchManager::verify_original_asm(ea_t address, const std::string& expected_asm, std::string& error_msg) {
    std::string actual_asm = disassemble_at(address);
    std::string norm_expected = normalize_assembly(expected_asm);
    std::string norm_actual = normalize_assembly(actual_asm);
    
    if (norm_expected != norm_actual) {
        error_msg = "Original assembly does not match. Expected: " + expected_asm +
                   ", Actual: " + actual_asm;
        return false;
    }
    
    return true;
}

// Hex string utilities
bool PatchManager::is_valid_hex_string(const std::string& hex) {
    std::string cleaned;
    
    // Remove spaces and validate
    for (char c : hex) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            continue;
        }
        if (!std::isxdigit(c)) {
            return false;
        }
        cleaned += c;
    }
    
    // Must have even number of hex chars
    return !cleaned.empty() && (cleaned.length() % 2 == 0);
}

std::vector<uint8_t> PatchManager::hex_string_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    std::string cleaned;
    
    // Remove spaces
    for (char c : hex) {
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            cleaned += c;
        }
    }
    
    // Convert pairs of hex chars to bytes
    for (size_t i = 0; i < cleaned.length(); i += 2) {
        std::string byte_str = cleaned.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        bytes.push_back(byte);
    }
    
    return bytes;
}

std::string PatchManager::bytes_to_hex_string(const std::vector<uint8_t>& bytes) {
    std::stringstream ss;
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i > 0) ss << " ";
        ss << std::hex << std::uppercase << std::setfill('0') << std::setw(2) 
           << static_cast<int>(bytes[i]);
    }
    return ss.str();
}

// Assembly utilities
bool PatchManager::init_keystone() {
    // Detect architecture
    ks_arch arch;
    int mode = 0;
    
    // Get processor info - handle all IDA processor types
    if (PH.id == PLFM_386) {
        arch = KS_ARCH_X86;
        if (inf_is_64bit()) {
            mode = KS_MODE_64;
        } else if (inf_is_32bit_exactly()) {
            mode = KS_MODE_32;
        } else if (inf_is_16bit()) {
            mode = KS_MODE_16;
        } else {
            // Default to 32-bit if unclear
            mode = KS_MODE_32;
        }
    } else if (PH.id == PLFM_ARM) {
        if (inf_is_64bit()) {
            arch = KS_ARCH_ARM64;
            mode = KS_MODE_LITTLE_ENDIAN;
        } else {
            arch = KS_ARCH_ARM;
            // Check if Thumb mode
            if (get_sreg(get_screen_ea(), str2reg("T")) & 1) {
                mode = KS_MODE_THUMB;
            } else {
                mode = KS_MODE_ARM;
            }
        }
        // Add endianness
        if (inf_is_be()) {
            mode |= KS_MODE_BIG_ENDIAN;
        } else {
            mode |= KS_MODE_LITTLE_ENDIAN;
        }
    } else if (PH.id == PLFM_PPC) {
        arch = KS_ARCH_PPC;
        mode = inf_is_64bit() ? KS_MODE_PPC64 : KS_MODE_PPC32;
        if (inf_is_be()) {
            mode |= KS_MODE_BIG_ENDIAN;
        } else {
            mode |= KS_MODE_LITTLE_ENDIAN;
        }
    } else if (PH.id == PLFM_MIPS) {
        arch = KS_ARCH_MIPS;
        mode = inf_is_64bit() ? KS_MODE_MIPS64 : KS_MODE_MIPS32;
        // MIPS endianness
        if (inf_is_be()) {
            mode |= KS_MODE_BIG_ENDIAN;
        } else {
            mode |= KS_MODE_LITTLE_ENDIAN;
        }
    } else if (PH.id == PLFM_SPARC) {
        arch = KS_ARCH_SPARC;
        mode = inf_is_64bit() ? KS_MODE_SPARC64 : KS_MODE_SPARC32;
        // SPARC V9 mode for 64-bit
        if (inf_is_64bit()) {
            mode |= KS_MODE_V9;
        }
        if (inf_is_be()) {
            mode |= KS_MODE_BIG_ENDIAN;
        } else {
            mode |= KS_MODE_LITTLE_ENDIAN;
        }
    } else if (PH.id == PLFM_HPPA) {
        // HPPA is not supported by Keystone
        LOG("WARNING: HPPA architecture is not supported by Keystone assembler\n");
        return false;
    } else if (PH.id == PLFM_68K) {
        // M68K is not fully supported by Keystone
        LOG("WARNING: M68K architecture is not supported by Keystone assembler\n");
        return false;
    } else if (PH.id == PLFM_6502 || PH.id == PLFM_65C816) {
        // 6502 family is not supported by Keystone
        LOG("WARNING: 6502 family architecture is not supported by Keystone assembler\n");
        return false;
    } else {
        // Log specific processor ID for debugging
        const char *proc_name = (PH.plnames && PH.plnames[0]) ? PH.plnames[0] : "unknown";
        LOG("WARNING: Unsupported processor type for Keystone: %d (%s)\n", 
            PH.id, proc_name);
        return false;
    }
    
    // Clean up any existing engine
    if (ks_) {
        ks_close(ks_);
        ks_ = nullptr;
    }
    
    // Create Keystone engine
    ks_err err = ks_open(arch, mode, &ks_);
    if (err != KS_ERR_OK) {
        const char *proc_name = (PH.plnames && PH.plnames[0]) ? PH.plnames[0] : "unknown";
        LOG("Failed to initialize Keystone: %s (arch=%d, mode=0x%X, processor=%s)\n", 
            ks_strerror(err), arch, mode, proc_name);
        ks_ = nullptr;
        return false;
    }
    
    // Set syntax mode for x86 (Intel syntax to match IDA)
    if (arch == KS_ARCH_X86) {
        err = ks_option(ks_, KS_OPT_SYNTAX, KS_OPT_SYNTAX_INTEL);
        if (err != KS_ERR_OK) {
            LOG("WARNING: Failed to set Intel syntax for x86: %s\n", ks_strerror(err));
        }
    }
    
    const char *proc_name = (PH.plnames && PH.plnames[0]) ? PH.plnames[0] : "unknown";
    LOG("Keystone initialized successfully for %s (arch=%d, mode=0x%X)\n", 
        proc_name, arch, mode);
    return true;
}

void PatchManager::cleanup_keystone() {
    // Wrap cleanup in sync wrapper to ensure thread safety
    IDAUtils::execute_sync_wrapper([this]() -> bool {
        if (ks_) {
            ks_close(ks_);
            ks_ = nullptr;
        }
        return true;
    }, MFF_WRITE);
}

std::pair<bool, std::vector<uint8_t>> PatchManager::assemble_instruction(const std::string& asm_str, ea_t address) {
    if (!ks_) {
        LOG("ERROR: Keystone not initialized for assembly\n");
        return {false, {}};
    }
    
    // Validate input
    if (asm_str.empty()) {
        LOG("ERROR: Empty assembly string provided\n");
        return {false, {}};
    }
    
    // Store current mode for potential restoration
    bool need_reinit = false;
    ks_arch arch;
    int mode = 0;
    
    // For ARM, we may need to reinitialize Keystone if mode changes
    if (PH.id == PLFM_ARM && !inf_is_64bit()) {
        bool is_thumb = (get_sreg(address, str2reg("T")) & 1) != 0;
        arch = KS_ARCH_ARM;
        mode = is_thumb ? KS_MODE_THUMB : KS_MODE_ARM;
        mode |= inf_is_be() ? KS_MODE_BIG_ENDIAN : KS_MODE_LITTLE_ENDIAN;
        need_reinit = true;
    }
    
    // Check for mode-specific prefixes in assembly string
    std::string cleaned_asm = asm_str;
    if (PH.id == PLFM_ARM && !inf_is_64bit()) {
        // Handle .thumb/.arm directives
        if (cleaned_asm.find(".thumb") != std::string::npos) {
            mode = KS_MODE_THUMB | (inf_is_be() ? KS_MODE_BIG_ENDIAN : KS_MODE_LITTLE_ENDIAN);
            need_reinit = true;
            // Remove directive from assembly string
            size_t pos = cleaned_asm.find(".thumb");
            cleaned_asm.erase(pos, 6);
        } else if (cleaned_asm.find(".arm") != std::string::npos) {
            mode = KS_MODE_ARM | (inf_is_be() ? KS_MODE_BIG_ENDIAN : KS_MODE_LITTLE_ENDIAN);
            need_reinit = true;
            // Remove directive from assembly string
            size_t pos = cleaned_asm.find(".arm");
            cleaned_asm.erase(pos, 4);
        }
    }
    
    // Reinitialize if needed
    if (need_reinit) {
        ks_close(ks_);
        ks_ = nullptr;
        
        ks_err err = ks_open(arch, mode, &ks_);
        if (err != KS_ERR_OK) {
            LOG("ERROR: Failed to reinitialize Keystone for mode change: %s\n", ks_strerror(err));
            return {false, {}};
        }
        
        // Set syntax mode for x86
        if (arch == KS_ARCH_X86) {
            ks_option(ks_, KS_OPT_SYNTAX, KS_OPT_SYNTAX_INTEL);
        }
    }
    
    // Trim whitespace from assembly string
    cleaned_asm.erase(0, cleaned_asm.find_first_not_of(" \t\n\r"));
    cleaned_asm.erase(cleaned_asm.find_last_not_of(" \t\n\r") + 1);
    
    unsigned char* encode = nullptr;
    size_t size = 0;
    size_t count = 0;
    
    // Assemble with error handling
    int result = ks_asm(ks_, cleaned_asm.c_str(), address, &encode, &size, &count);
    if (result != 0) {
        // Get detailed error information
        ks_err err = ks_errno(ks_);
        const char* err_msg = ks_strerror(err);
        
        LOG("ERROR: Keystone assembly failed\n");
        LOG("  Error: %s (code: %d)\n", err_msg, err);
        LOG("  Assembly: '%s'\n", cleaned_asm.c_str());
        LOG("  Address: 0x%llX\n", (uint64_t)address);
        
        // Common error hints
        if (err == KS_ERR_ASM_INVALIDOPERAND) {
            LOG("  Hint: Check operand syntax and register names\n");
        } else if (err == KS_ERR_ASM_MISSINGFEATURE) {
            LOG("  Hint: This instruction may not be supported by Keystone\n");
        } else if (err == KS_ERR_ASM_MNEMONICFAIL) {
            LOG("  Hint: Unknown instruction mnemonic\n");
        }
        
        return {false, {}};
    }
    
    // Validate assembled output
    if (!encode || size == 0) {
        LOG("ERROR: Keystone produced no output for: '%s'\n", cleaned_asm.c_str());
        if (encode) ks_free(encode);
        return {false, {}};
    }
    
    // Check if multiple instructions were assembled when we expected one
    if (count > 1) {
        LOG("WARNING: Multiple instructions assembled (%zu) from: '%s'\n", count, cleaned_asm.c_str());
    }
    
    // Copy to vector
    std::vector<uint8_t> bytes;
    try {
        bytes.assign(encode, encode + size);
    } catch (const std::exception& e) {
        LOG("ERROR: Failed to copy assembled bytes: %s\n", e.what());
        ks_free(encode);
        return {false, {}};
    }
    
    // Free Keystone allocated memory
    ks_free(encode);
    
    // Log successful assembly for debugging
    LOG("Successfully assembled '%s' to %zu bytes\n", cleaned_asm.c_str(), bytes.size());
    
    return {true, bytes};
}

std::string PatchManager::disassemble_at(ea_t address) {
    qstring buf;
    if (generate_disasm_line(&buf, address) == 0) {
        return "";
    }
    
    // Remove color codes and comments
    tag_remove(&buf);
    
    // Find and remove comment
    size_t comment_pos = buf.find(';');
    if (comment_pos != qstring::npos) {
        buf.resize(comment_pos);
    }
    
    // Trim whitespace
    buf.trim2();
    
    return buf.c_str();
}

std::string PatchManager::normalize_assembly(const std::string& asm_str) {
    std::string normalized = asm_str;
    
    // Convert to lowercase
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    
    // Remove extra spaces
    auto new_end = std::unique(normalized.begin(), normalized.end(),
        [](char a, char b) { return std::isspace(a) && std::isspace(b); });
    normalized.erase(new_end, normalized.end());
    
    // Trim
    normalized.erase(0, normalized.find_first_not_of(" \t\n\r"));
    normalized.erase(normalized.find_last_not_of(" \t\n\r") + 1);
    
    return normalized;
}

std::vector<uint8_t> PatchManager::get_nop_bytes(size_t count, ea_t address) {
    std::vector<uint8_t> nops;
    
    // Validate count
    if (count == 0) {
        return nops;
    }
    
    if (count > 1024) {
        LOG("WARNING: Unusually large NOP padding requested: %zu bytes\n", count);
    }
    
    if (PH.id == PLFM_386) {
        // x86/x64 NOP = 0x90
        nops.resize(count, 0x90);
    } else if (PH.id == PLFM_ARM) {
        if (inf_is_64bit()) {
            // ARM64 NOP = 0x1F2003D5 (NOP in little endian)
            size_t nop_count = count / 4;
            for (size_t i = 0; i < nop_count; ++i) {
                if (inf_is_be()) {
                    nops.push_back(0xD5);
                    nops.push_back(0x03);
                    nops.push_back(0x20);
                    nops.push_back(0x1F);
                } else {
                    nops.push_back(0x1F);
                    nops.push_back(0x20);
                    nops.push_back(0x03);
                    nops.push_back(0xD5);
                }
            }
            // Handle remaining bytes - ARM64 requires 4-byte alignment
            size_t remaining = count % 4;
            if (remaining > 0) {
                LOG("WARNING: NOP padding size %zu is not aligned to 4 bytes for ARM64. "
                    "Remaining %zu bytes will be filled with 0x00\n", count, remaining);
                for (size_t i = 0; i < remaining; ++i) {
                    nops.push_back(0x00);
                }
            }
        } else {
            // Check if we're in Thumb mode at the specific address
            ea_t ea = (address != BADADDR) ? address : get_screen_ea();
            bool is_thumb = (get_sreg(ea, str2reg("T")) & 1) != 0;
            
            if (is_thumb) {
                // Thumb NOP = 0xBF00 (2 bytes) in little endian
                size_t nop_count = count / 2;
                for (size_t i = 0; i < nop_count; ++i) {
                    if (inf_is_be()) {
                        nops.push_back(0xBF);
                        nops.push_back(0x00);
                    } else {
                        nops.push_back(0x00);
                        nops.push_back(0xBF);
                    }
                }
                // Handle odd byte count
                if (count % 2 == 1) {
                    nops.push_back(0x00);
                }
            } else {
                // ARM32 NOP = 0xE320F000 (MOV R0, R0) in little endian
                size_t nop_count = count / 4;
                for (size_t i = 0; i < nop_count; ++i) {
                    if (inf_is_be()) {
                        nops.push_back(0xE3);
                        nops.push_back(0x20);
                        nops.push_back(0xF0);
                        nops.push_back(0x00);
                    } else {
                        nops.push_back(0x00);
                        nops.push_back(0xF0);
                        nops.push_back(0x20);
                        nops.push_back(0xE3);
                    }
                }
                // Handle remaining bytes - ARM32 requires 4-byte alignment
                size_t remaining = count % 4;
                if (remaining > 0) {
                    LOG("WARNING: NOP padding size %zu is not aligned to 4 bytes for ARM32. "
                        "Remaining %zu bytes will be filled with 0x00\n", count, remaining);
                    for (size_t i = 0; i < remaining; ++i) {
                        nops.push_back(0x00);
                    }
                }
            }
        }
    } else if (PH.id == PLFM_PPC) {
        // PowerPC NOP = 0x60000000 (ori r0,r0,0)
        size_t nop_count = count / 4;
        for (size_t i = 0; i < nop_count; ++i) {
            if (inf_is_be()) {
                nops.push_back(0x60);
                nops.push_back(0x00);
                nops.push_back(0x00);
                nops.push_back(0x00);
            } else {
                nops.push_back(0x00);
                nops.push_back(0x00);
                nops.push_back(0x00);
                nops.push_back(0x60);
            }
        }
        // Handle remaining bytes
        size_t remaining = count % 4;
        if (remaining > 0) {
            LOG("WARNING: NOP padding size %zu is not aligned to 4 bytes for PowerPC. "
                "Remaining %zu bytes will be filled with 0x00\n", count, remaining);
            for (size_t i = 0; i < remaining; ++i) {
                nops.push_back(0x00);
            }
        }
    } else if (PH.id == PLFM_MIPS) {
        // MIPS NOP = 0x00000000 (sll zero, zero, 0)
        size_t nop_count = count / 4;
        for (size_t i = 0; i < nop_count; ++i) {
            nops.push_back(0x00);
            nops.push_back(0x00);
            nops.push_back(0x00);
            nops.push_back(0x00);
        }
        // Handle remaining bytes
        size_t remaining = count % 4;
        if (remaining > 0) {
            LOG("WARNING: NOP padding size %zu is not aligned to 4 bytes for MIPS. "
                "Remaining %zu bytes will be filled with 0x00\n", count, remaining);
            for (size_t i = 0; i < remaining; ++i) {
                nops.push_back(0x00);
            }
        }
    } else if (PH.id == PLFM_SPARC) {
        // SPARC NOP = 0x01000000 (sethi 0, %g0)
        size_t nop_count = count / 4;
        for (size_t i = 0; i < nop_count; ++i) {
            if (inf_is_be()) {
                nops.push_back(0x01);
                nops.push_back(0x00);
                nops.push_back(0x00);
                nops.push_back(0x00);
            } else {
                nops.push_back(0x00);
                nops.push_back(0x00);
                nops.push_back(0x00);
                nops.push_back(0x01);
            }
        }
        // Handle remaining bytes
        size_t remaining = count % 4;
        for (size_t i = 0; i < remaining; ++i) {
            nops.push_back(0x00);
        }
    } else {
        // Unknown architecture - fill with zeros (safest option)
        const char *proc_name = (PH.plnames && PH.plnames[0]) ? PH.plnames[0] : "unknown";
        LOG("WARNING: Using zero-fill for NOP padding on unknown architecture: %s (id=%d)\n", 
            proc_name, PH.id);
        nops.resize(count, 0x00);
    }
    
    return nops;
}

// IDA interaction
std::vector<uint8_t> PatchManager::read_bytes(ea_t address, size_t size) {
    std::vector<uint8_t> bytes(size);
    get_bytes(bytes.data(), size, address);
    return bytes;
}

bool PatchManager::write_bytes(ea_t address, const std::vector<uint8_t>& bytes) {
    // Validate parameters
    if (bytes.empty()) {
        LOG("ERROR: Cannot write empty byte array\n");
        return false;
    }

    // Check if address is valid and writable
    if (!is_mapped(address)) {
        LOG("ERROR: Address 0x%llX is not mapped\n", (uint64_t)address);
        return false;
    }

    // Check if we can write to this segment
    segment_t* seg = getseg(address);
    if (!seg) {
        LOG("ERROR: No segment at address 0x%llX\n", (uint64_t)address);
        return false;
    }

    // Always patch IDA database
    patch_bytes(address, bytes.data(), bytes.size());

    // Check if we should also patch the file
    if (code_injection_manager_ && code_injection_manager_->is_in_temp_workspace(address)) {
        // Only patch IDA database for temporary workspace
        LOG("Patched temporary workspace at 0x%llX (IDA DB only)\n", (uint64_t)address);
    } else if (!binary_path_.empty()) {
        // Also patch the binary file if we have a path
        uint32_t file_offset = get_fileregion_offset(address);
        if (file_offset != BADADDR) {
            if (apply_to_file(file_offset, bytes)) {
                LOG("Applied dual patch at 0x%llX (IDA DB + file at offset 0x%X)\n",
                    (uint64_t)address, file_offset);
            } else {
                LOG("WARNING: Patched IDA DB but failed to patch file at 0x%llX\n", (uint64_t)address);
                // Continue anyway - IDA DB patch succeeded
            }
        } else {
            LOG("WARNING: Could not get file offset for 0x%llX, IDA DB patched only\n", (uint64_t)address);
        }
    } else {
        LOG("Patched at 0x%llX (IDA DB only - no binary path)\n", (uint64_t)address);
    }

    // patch_bytes doesn't return a value, so we assume success if we get here
    return true;
}

void PatchManager::trigger_reanalysis(ea_t address, size_t size) {
    LOG("Triggering reanalysis for patch at 0x%llX (size: %zu bytes)\n", 
        (uint64_t)address, size);
    
    // Check if the address is inside a function
    func_t *func = get_func(address);
    if (func != nullptr) {
        // We're patching inside a function - need to reanalyze the entire function
        ea_t func_start = func->start_ea;
        ea_t func_end = func->end_ea;
        
        LOG("Patch at 0x%llX is inside function at 0x%llX-0x%llX, reanalyzing entire function\n", 
            (uint64_t)address, (uint64_t)func_start, (uint64_t)func_end);
        
        // Delete the function definition first
        del_func(func_start);
        
        // Delete all items in the function range
        del_items(func_start, DELIT_SIMPLE, func_end - func_start);
        
        // Mark the entire function range for reanalysis
        auto_mark_range(func_start, func_end, AU_USED);
        
        // Force immediate analysis
        plan_and_wait(func_start, func_end);
        
        // Try to recreate the function
        if (!add_func(func_start, func_end)) {
            LOG("Failed to recreate function with original boundaries, trying auto-detection\n");
            // If add_func with explicit end fails, try without end address
            // This lets IDA determine the function boundaries
            if (!add_func(func_start, BADADDR)) {
                LOG("WARNING: Failed to recreate function at 0x%llX after patch\n", (uint64_t)func_start);
                // Last resort: create instructions manually
                create_insn(func_start);
            }
        }
        
        // Final wait to ensure everything is processed
        auto_wait();
    } else {
        // Not in a function - use original behavior
        del_items(address, DELIT_SIMPLE, size);
        auto_mark_range(address, address + size, AU_USED);
        auto_wait();
    }
}

// File patching utilities
bool PatchManager::apply_to_file(uint32_t offset, const std::vector<uint8_t>& bytes) {
    if (binary_path_.empty()) {
        return false;
    }

    std::fstream file(binary_path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        LOG("PatchManager: Failed to open binary file: %s\n", binary_path_.c_str());
        return false;
    }

    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    file.close();

    return true;
}

// Segment injection helpers
bool PatchManager::create_segment_in_ida(ea_t address, size_t size, const std::string& name) {
    segment_t seg;
    seg.start_ea = address;
    seg.end_ea = address + size;
    seg.perm = SEGPERM_EXEC | SEGPERM_READ | SEGPERM_WRITE;
    seg.type = SEG_CODE;

    // Set bitness based on application
    uint app_bitness = inf_get_app_bitness();
    switch (app_bitness) {
        case 64:
            seg.bitness = 2;  // 64-bit
            break;
        case 32:
            seg.bitness = 1;  // 32-bit
            break;
        case 16:
            seg.bitness = 0;  // 16-bit
            break;
        default:
            LOG("PatchManager: WARNING - Unknown bitness %d, defaulting to 32-bit\n", app_bitness);
            seg.bitness = 1;
            break;
    }

    if (!add_segm_ex(&seg, name.c_str(), "CODE", ADDSEG_OR_DIE)) {
        LOG("PatchManager: ERROR - Failed to create segment %s at 0x%llX\n",
            name.c_str(), (uint64_t)address);
        return false;
    }

    LOG("PatchManager: Created segment %s at 0x%llX-0x%llX\n",
        name.c_str(), (uint64_t)address, (uint64_t)(address + size));
    return true;
}


// Add segment to binary using LIEF
bool PatchManager::add_segment_to_binary_with_lief(ea_t address, size_t size,
                                                   const std::string& name,
                                                   const std::vector<uint8_t>& code) {
    LOG("PatchManager: Adding new segment '%s' to binary using LIEF at address 0x%llX, size 0x%llX\n",
        name.c_str(), (uint64_t)address, (uint64_t)size);

    try {
        // Parse the binary with LIEF
        std::unique_ptr<LIEF::Binary> binary = LIEF::Parser::parse(binary_path_);
        if (!binary) {
            LOG("PatchManager: Failed to parse binary with LIEF: %s\n", binary_path_.c_str());
            return false;
        }

        // Detect binary type using IDA's information
        filetype_t file_type = inf_get_filetype();

        // Use the parameters directly
        uint64_t target_address = address;
        size_t target_size = size;
        std::string segment_name = name;

        // Handle different binary formats
        if (file_type == f_PE) {
            // Handle PE (Windows) binaries
            auto* pe_binary = dynamic_cast<LIEF::PE::Binary*>(binary.get());
            if (!pe_binary) {
                LOG("PatchManager: Failed to cast to PE binary\n");
                return false;
            }

            // Create a new section using the provided name
            LIEF::PE::Section new_section;
            new_section.name(segment_name);
            new_section.characteristics(
                static_cast<uint32_t>(LIEF::PE::Section::CHARACTERISTICS::MEM_READ |
                                      LIEF::PE::Section::CHARACTERISTICS::MEM_EXECUTE |
                                      LIEF::PE::Section::CHARACTERISTICS::CNT_CODE)
            );

            // Set virtual address to match IDA segment
            // For PE, we need to convert absolute address to RVA
            uint64_t image_base = pe_binary->optional_header().imagebase();
            uint32_t rva = static_cast<uint32_t>(target_address - image_base);
            new_section.virtual_address(rva);
            new_section.virtual_size(target_size);

            // Set the content
            new_section.content(code);

            // Add the section to the binary
            LIEF::PE::Section* added_section = pe_binary->add_section(new_section);

            LOG("PatchManager: Added PE section at RVA 0x%X (VA: 0x%llX)\n",
                rva, target_address);

        } else if (file_type == f_ELF) {
            // Handle ELF (Linux) binaries
            auto* elf_binary = dynamic_cast<LIEF::ELF::Binary*>(binary.get());
            if (!elf_binary) {
                LOG("PatchManager: Failed to cast to ELF binary\n");
                return false;
            }

            // Create a new segment (PT_LOAD)
            LIEF::ELF::Segment new_segment;
            new_segment.type(LIEF::ELF::SEGMENT_TYPES::PT_LOAD);
            new_segment.flags(
                LIEF::ELF::ELF_SEGMENT_FLAGS::PF_R |
                LIEF::ELF::ELF_SEGMENT_FLAGS::PF_X
            );
            new_segment.content(code);
            new_segment.alignment(0x1000);  // Page alignment

            // Use the address from the parameters
            new_segment.virtual_address(target_address);
            new_segment.virtual_size(target_size);
            new_segment.physical_address(target_address);  // Usually same as virtual
            new_segment.physical_size(code.size());

            // Add the segment
            elf_binary->add(new_segment);

            // Also create a section for better compatibility
            LIEF::ELF::Section new_section;
            new_section.name(segment_name);
            new_section.type(LIEF::ELF::ELF_SECTION_TYPES::SHT_PROGBITS);
            new_section.flags(
                static_cast<uint64_t>(LIEF::ELF::ELF_SECTION_FLAGS::SHF_ALLOC) |
                static_cast<uint64_t>(LIEF::ELF::ELF_SECTION_FLAGS::SHF_EXECINSTR)
            );
            new_section.virtual_address(target_address);
            new_section.size(code.size());
            new_section.content(code);

            elf_binary->add(new_section);

            LOG("PatchManager: Added ELF segment at 0x%llX\n", target_address);

        } else if (file_type == f_MACHO) {
            // Handle Mach-O (macOS) binaries
            auto* macho = dynamic_cast<LIEF::MachO::FatBinary*>(binary.get());
            LIEF::MachO::Binary* macho_binary = nullptr;

            if (macho && !macho->empty()) {
                // Fat binary, use first architecture
                macho_binary = macho->at(0);
            } else {
                // Thin binary
                macho_binary = dynamic_cast<LIEF::MachO::Binary*>(binary.get());
            }

            if (!macho_binary) {
                LOG("PatchManager: Failed to cast to Mach-O binary\n");
                return false;
            }

            // Create a new segment using the provided name
            LIEF::MachO::SegmentCommand new_segment;
            new_segment.name(segment_name);
            new_segment.init_protection(
                static_cast<uint32_t>(LIEF::MachO::VM_PROTECTIONS::VM_PROT_READ) |
                static_cast<uint32_t>(LIEF::MachO::VM_PROTECTIONS::VM_PROT_EXECUTE)
            );
            new_segment.max_protection(
                static_cast<uint32_t>(LIEF::MachO::VM_PROTECTIONS::VM_PROT_READ) |
                static_cast<uint32_t>(LIEF::MachO::VM_PROTECTIONS::VM_PROT_EXECUTE)
            );

            // Use the address from the parameters
            new_segment.virtual_address(target_address);
            new_segment.virtual_size(target_size);
            new_segment.file_size(code.size());

            // Create a section within the segment
            LIEF::MachO::Section new_section;
            new_section.segment_name(segment_name);
            new_section.name("__text");
            new_section.address(target_address);
            new_section.size(code.size());
            new_section.content(code);
            new_section.type(LIEF::MachO::MACHO_SECTION_TYPES::S_REGULAR);
            new_section.flags(
                static_cast<uint32_t>(LIEF::MachO::MACHO_SECTION_FLAGS::S_ATTR_SOME_INSTRUCTIONS) |
                static_cast<uint32_t>(LIEF::MachO::MACHO_SECTION_FLAGS::S_ATTR_PURE_INSTRUCTIONS)
            );

            // Add section to segment
            new_segment.add_section(new_section);

            // Add segment to binary
            macho_binary->add(new_segment);

            LOG("PatchManager: Added Mach-O segment at 0x%llX\n", target_address);

        } else {
            LOG("PatchManager: Unsupported file type: %d\n", file_type);
            return false;
        }

        // Write the modified binary back
        // Build and write the modified binary
        // LIEF uses format-specific builders or direct write
        if (file_type == f_PE) {
            auto* pe_binary = dynamic_cast<LIEF::PE::Binary*>(binary.get());
            if (pe_binary) {
                LIEF::PE::Builder builder(*pe_binary);
                builder.build();
                builder.write(binary_path_);
            }
        } else if (file_type == f_ELF) {
            auto* elf_binary = dynamic_cast<LIEF::ELF::Binary*>(binary.get());
            if (elf_binary) {
                LIEF::ELF::Builder builder(*elf_binary);
                builder.build();
                builder.write(binary_path_);
            }
        } else if (file_type == f_MACHO) {
            // MachO uses direct write, no builder needed
            binary->write(binary_path_);
        }

        LOG("PatchManager: Successfully wrote modified binary to %s\n", binary_path_.c_str());

        return true;

    } catch (const std::exception& e) {
        LOG("PatchManager: Exception: %s\n", e.what());
        return false;
    }
}

// Remove segment from binary using LIEF
bool PatchManager::remove_segment_from_binary(ea_t address, const std::string& segment_name) {
    if (binary_path_.empty()) {
        return true;  // No binary to modify
    }

    try {
        std::unique_ptr<LIEF::Binary> binary = LIEF::Parser::parse(binary_path_);
        if (!binary) {
            LOG("PatchManager: ERROR - Failed to parse binary with LIEF: %s\n", binary_path_.c_str());
            return false;
        }

        filetype_t file_type = inf_get_filetype();
        bool removed = false;

        if (file_type == f_PE) {
            auto* pe_binary = dynamic_cast<LIEF::PE::Binary*>(binary.get());
            if (pe_binary) {
                // Find section by name and remove
                LIEF::PE::Section* section = pe_binary->get_section(segment_name);
                if (section) {
                    pe_binary->remove_section(segment_name);
                    removed = true;
                }
            }
        } else if (file_type == f_ELF) {
            auto* elf_binary = dynamic_cast<LIEF::ELF::Binary*>(binary.get());
            if (elf_binary) {
                // Remove section by name
                LIEF::ELF::Section* section = elf_binary->get_section(segment_name);
                if (section) {
                    elf_binary->remove(*section);
                    removed = true;
                }
                // Also remove corresponding segment
                for (auto& segment : elf_binary->segments()) {
                    if (segment.virtual_address() == address) {
                        elf_binary->remove(segment);
                        break;
                    }
                }
            }
        } else if (file_type == f_MACHO) {
            auto* macho = dynamic_cast<LIEF::MachO::FatBinary*>(binary.get());
            LIEF::MachO::Binary* macho_binary = nullptr;

            if (macho && !macho->empty()) {
                macho_binary = macho->at(0);
            } else {
                macho_binary = dynamic_cast<LIEF::MachO::Binary*>(binary.get());
            }

            if (macho_binary) {
                // Remove segment by name
                LIEF::MachO::SegmentCommand* segment = macho_binary->get_segment(segment_name);
                if (segment) {
                    macho_binary->remove(*segment);
                    removed = true;
                }
            }
        }

        if (!removed) {
            LOG("PatchManager: WARNING - Could not find segment '%s' to remove\n", segment_name.c_str());
            return false;
        }

        // Write modified binary
        if (file_type == f_PE) {
            auto* pe_binary = dynamic_cast<LIEF::PE::Binary*>(binary.get());
            if (pe_binary) {
                LIEF::PE::Builder builder(*pe_binary);
                builder.build();
                builder.write(binary_path_);
            }
        } else if (file_type == f_ELF) {
            auto* elf_binary = dynamic_cast<LIEF::ELF::Binary*>(binary.get());
            if (elf_binary) {
                LIEF::ELF::Builder builder(*elf_binary);
                builder.build();
                builder.write(binary_path_);
            }
        } else if (file_type == f_MACHO) {
            binary->write(binary_path_);
        }

        LOG("PatchManager: Removed segment '%s' from binary\n", segment_name.c_str());
        return true;

    } catch (const std::exception& e) {
        LOG("PatchManager: ERROR - Exception removing segment: %s\n", e.what());
        return false;
    }
}

} // namespace llm_re