//
// Created by user on 7/27/27.
//

#ifndef PATCH_MANAGER_H
#define PATCH_MANAGER_H

#include "core/common.h"


namespace llm_re {

// Result structures for patch operations
struct BytePatchResult {
    bool success = false;
    std::string error_message;
    size_t bytes_patched = 0;
};

struct AssemblyPatchResult {
    bool success = false;
    std::string error_message;
    size_t bytes_patched = 0;
    size_t nops_added = 0;
};

struct SegmentInjectionResult {
    bool success = false;
    ea_t segment_address = BADADDR;
    std::string segment_name;
    size_t allocated_size = 0;
    std::string error_message;
};

// Simplified patch info for listing
struct PatchInfo {
    ea_t address;
    std::string original_bytes_hex;
    std::string patched_bytes_hex;
    std::string description;
    std::chrono::system_clock::time_point timestamp;
    bool is_assembly_patch;
    std::string original_asm;  // Only for assembly patches
    std::string patched_asm;   // Only for assembly patches
};

// Patch statistics
struct PatchStatistics {
    size_t total_patches;
    size_t assembly_patches;
    size_t byte_patches;
    size_t total_bytes_patched;
};

// Forward declarations
class CodeInjectionManager;

namespace semantic {
    class SemanticPatchManager;  // Forward declaration for friend access
}

// Simplified PatchManager with embedded patchers
class PatchManager {
    // Grant SemanticPatchManager access to private assembly/write methods
    friend class semantic::SemanticPatchManager;

public:
    PatchManager();
    ~PatchManager();

    // Initialize patch manager (creates backup, initializes Keystone)
    bool initialize();

    // Set binary path for dual patching (IDA DB + file)
    void set_binary_path(const std::string& path) {
        binary_path_ = path;
    }

    // Set the code injection manager for integrated patching
    void set_code_injection_manager(CodeInjectionManager* cim) {
        code_injection_manager_ = cim;
    }

    // Core 4 methods for the LLM tools
    
    // 1. Apply byte patch with mandatory original bytes verification
    BytePatchResult apply_byte_patch(ea_t address, 
                                    const std::string& original_hex,
                                    const std::string& new_hex,
                                    const std::string& description);

    // 2. Apply assembly patch with mandatory original assembly verification
    AssemblyPatchResult apply_assembly_patch(ea_t address,
                                           const std::string& original_asm,
                                           const std::string& new_asm,
                                           const std::string& description);

    // 2b. Apply segment injection (creates new segment in IDA + binary)
    SegmentInjectionResult apply_segment_injection(ea_t address,
                                                   size_t size,
                                                   const std::vector<uint8_t>& code,
                                                   const std::string& segment_name,
                                                   const std::string& description);

    // 3. Revert a patch at address
    bool revert_patch(ea_t address);
    bool revert_all();

    // 4. List patches
    std::vector<PatchInfo> list_patches() const;
    std::optional<PatchInfo> get_patch_info(ea_t address) const;
    
    // Statistics
    PatchStatistics get_statistics() const;

    // Utility methods (public for use by CodeInjectionManager)
    static std::string bytes_to_hex_string(const std::vector<uint8_t>& bytes);
    static std::vector<uint8_t> hex_string_to_bytes(const std::string& hex);

private:
    // Internal patch entry
    struct PatchEntry {
        ea_t address;
        std::vector<uint8_t> original_bytes;
        std::vector<uint8_t> patched_bytes;
        std::string description;
        std::chrono::system_clock::time_point timestamp;
        bool is_assembly_patch;
        std::string original_asm;
        std::string patched_asm;

        // Segment injection fields
        bool is_segment_injection = false;  // If true, this is a segment injection
        std::string segment_name;            // For segment deletion
        size_t segment_size = 0;             // Original allocated size
    };

    // Keystone assembler
    ks_engine* ks_ = nullptr;
    
    // Patches storage
    std::unordered_map<ea_t, PatchEntry> patches_;

    // Binary path for file patching
    std::string binary_path_;

    // Integration with code injection system
    CodeInjectionManager* code_injection_manager_ = nullptr;

    // Safety validation methods
    bool validate_address(ea_t address, std::string& error_msg);
    bool validate_instruction_boundary(ea_t address, std::string& error_msg);
    bool validate_patch_size(ea_t address, size_t old_size, size_t new_size, std::string& error_msg);
    bool verify_original_bytes(ea_t address, const std::vector<uint8_t>& expected, std::string& error_msg);
    bool verify_original_asm(ea_t address, const std::string& expected_asm, std::string& error_msg);
    
    // Hex string utilities
    static bool is_valid_hex_string(const std::string& hex);

    // Segment injection helpers
    bool create_segment_in_ida(ea_t address, size_t size, const std::string& name);
    bool add_segment_to_binary_with_lief(ea_t address, size_t size,
                                        const std::string& name,
                                        const std::vector<uint8_t>& code);
    bool remove_segment_from_binary(ea_t address, const std::string& segment_name);

    // Assembly utilities
    bool init_keystone();
    void cleanup_keystone();
    std::pair<bool, std::vector<uint8_t>> assemble_instruction(const std::string& asm_str, ea_t address);
    std::string disassemble_at(ea_t address);
    std::string normalize_assembly(const std::string& asm_str);
    std::vector<uint8_t> get_nop_bytes(size_t count, ea_t address = BADADDR);
    
    // IDA interaction
    std::vector<uint8_t> read_bytes(ea_t address, size_t size);
    bool write_bytes(ea_t address, const std::vector<uint8_t>& bytes);
    void trigger_reanalysis(ea_t address, size_t size);

    // File patching utilities
    bool apply_to_file(uint32_t offset, const std::vector<uint8_t>& bytes);
};

} // namespace llm_re

#endif // PATCH_MANAGER_H