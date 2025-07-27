//
// Created by user on 11/27/24.
//

#ifndef BYTE_PATCHER_H
#define BYTE_PATCHER_H

#include "common.h"
#include <vector>
#include <string>
#include <optional>

namespace llm_re {

// Forward declaration
class PatchManager;

// Result of a byte patching operation
struct BytePatchResult {
    bool success;
    std::string error_message;
    size_t bytes_patched;
};

// Handles direct byte-level patching
class BytePatcher {
public:
    BytePatcher(PatchManager* patch_manager);
    ~BytePatcher();

    // Apply a byte patch with various input formats
    BytePatchResult apply_patch(ea_t address, const std::vector<uint8_t>& new_bytes,
                               const std::string& description,
                               bool verify_original = true,
                               const std::vector<uint8_t>& expected_original = {});

    // Apply patch from hex string (e.g., "90 90 90" or "909090")
    BytePatchResult apply_patch_hex(ea_t address, const std::string& hex_bytes,
                                   const std::string& description,
                                   bool verify_original = true,
                                   const std::string& expected_hex = "");

    // Patch a single byte
    bool patch_byte(ea_t address, uint8_t byte_value, const std::string& description);

    // Patch a word (2 bytes)
    bool patch_word(ea_t address, uint16_t word_value, const std::string& description,
                    bool little_endian = true);

    // Patch a dword (4 bytes)
    bool patch_dword(ea_t address, uint32_t dword_value, const std::string& description,
                     bool little_endian = true);

    // Patch a qword (8 bytes)
    bool patch_qword(ea_t address, uint64_t qword_value, const std::string& description,
                     bool little_endian = true);

    // Fill range with a specific byte value
    bool fill_range(ea_t start_address, ea_t end_address, uint8_t fill_byte,
                   const std::string& description);

    // Copy bytes from one location to another
    bool copy_bytes(ea_t source_address, ea_t dest_address, size_t size,
                   const std::string& description);

    // Search and replace bytes pattern
    struct SearchReplaceResult {
        size_t occurrences_found;
        size_t occurrences_replaced;
        std::vector<ea_t> replaced_addresses;
    };
    
    SearchReplaceResult search_and_replace(ea_t start_address, ea_t end_address,
                                         const std::vector<uint8_t>& search_pattern,
                                         const std::vector<uint8_t>& replace_pattern,
                                         const std::string& description,
                                         bool replace_all = true);

    // Utility functions
    static std::vector<uint8_t> hex_string_to_bytes(const std::string& hex);
    static std::string bytes_to_hex_string(const std::vector<uint8_t>& bytes,
                                         bool add_spaces = true);
    
    // Validate hex string format
    static bool is_valid_hex_string(const std::string& hex);

    // Check if address range is valid for patching
    bool is_valid_range(ea_t start_address, ea_t end_address, std::string& error_msg);

    // Get bytes at address
    std::vector<uint8_t> read_bytes(ea_t address, size_t size);

    // Check if region is code or data
    enum class MemoryType {
        CODE,
        DATA,
        UNKNOWN
    };
    MemoryType get_memory_type(ea_t address);

private:
    PatchManager* patch_manager_;

    // Helper to convert multi-byte values to byte vector
    std::vector<uint8_t> value_to_bytes(uint64_t value, size_t size, bool little_endian);

    // Find all occurrences of a pattern
    std::vector<ea_t> find_pattern(ea_t start_address, ea_t end_address,
                                  const std::vector<uint8_t>& pattern);
};

} // namespace llm_re

#endif // BYTE_PATCHER_H