//
// Created by user on 11/27/24.
//

#include "byte_patcher.h"
#include "patch_manager.h"
#include <ida.hpp>
#include <bytes.hpp>
#include <segment.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cctype>

namespace llm_re {

BytePatcher::BytePatcher(PatchManager* patch_manager) : patch_manager_(patch_manager) {
}

BytePatcher::~BytePatcher() = default;

BytePatchResult BytePatcher::apply_patch(ea_t address, const std::vector<uint8_t>& new_bytes,
                                       const std::string& description,
                                       bool verify_original,
                                       const std::vector<uint8_t>& expected_original) {
    BytePatchResult result;
    result.success = false;
    result.bytes_patched = 0;
    
    if (new_bytes.empty()) {
        result.error_message = "No bytes to patch";
        return result;
    }
    
    // Use patch manager to apply the patch
    PatchResult patch_result = patch_manager_->apply_patch(
        address, new_bytes, description, verify_original, expected_original);
    
    result.success = patch_result.success;
    result.error_message = patch_result.error_message;
    if (result.success) {
        result.bytes_patched = new_bytes.size();
    }
    
    return result;
}

BytePatchResult BytePatcher::apply_patch_hex(ea_t address, const std::string& hex_bytes,
                                           const std::string& description,
                                           bool verify_original,
                                           const std::string& expected_hex) {
    BytePatchResult result;
    result.success = false;
    result.bytes_patched = 0;
    
    // Validate hex string
    if (!is_valid_hex_string(hex_bytes)) {
        result.error_message = "Invalid hex string format";
        return result;
    }
    
    // Convert hex to bytes
    std::vector<uint8_t> new_bytes = hex_string_to_bytes(hex_bytes);
    std::vector<uint8_t> expected_bytes;
    
    if (!expected_hex.empty()) {
        if (!is_valid_hex_string(expected_hex)) {
            result.error_message = "Invalid expected hex string format";
            return result;
        }
        expected_bytes = hex_string_to_bytes(expected_hex);
    }
    
    return apply_patch(address, new_bytes, description, verify_original, expected_bytes);
}

bool BytePatcher::patch_byte(ea_t address, uint8_t byte_value, const std::string& description) {
    std::vector<uint8_t> bytes = {byte_value};
    BytePatchResult result = apply_patch(address, bytes, description, false);
    return result.success;
}

bool BytePatcher::patch_word(ea_t address, uint16_t word_value, const std::string& description,
                           bool little_endian) {
    std::vector<uint8_t> bytes = value_to_bytes(word_value, 2, little_endian);
    BytePatchResult result = apply_patch(address, bytes, description, false);
    return result.success;
}

bool BytePatcher::patch_dword(ea_t address, uint32_t dword_value, const std::string& description,
                            bool little_endian) {
    std::vector<uint8_t> bytes = value_to_bytes(dword_value, 4, little_endian);
    BytePatchResult result = apply_patch(address, bytes, description, false);
    return result.success;
}

bool BytePatcher::patch_qword(ea_t address, uint64_t qword_value, const std::string& description,
                            bool little_endian) {
    std::vector<uint8_t> bytes = value_to_bytes(qword_value, 8, little_endian);
    BytePatchResult result = apply_patch(address, bytes, description, false);
    return result.success;
}

bool BytePatcher::fill_range(ea_t start_address, ea_t end_address, uint8_t fill_byte,
                           const std::string& description) {
    std::string error_msg;
    if (!is_valid_range(start_address, end_address, error_msg)) {
        return false;
    }
    
    size_t size = end_address - start_address;
    std::vector<uint8_t> fill_bytes(size, fill_byte);
    
    BytePatchResult result = apply_patch(start_address, fill_bytes, description, false);
    return result.success;
}

bool BytePatcher::copy_bytes(ea_t source_address, ea_t dest_address, size_t size,
                           const std::string& description) {
    // Read source bytes
    std::vector<uint8_t> source_bytes = read_bytes(source_address, size);
    if (source_bytes.size() != size) {
        return false;
    }
    
    // Apply to destination
    BytePatchResult result = apply_patch(dest_address, source_bytes, description, false);
    return result.success;
}

BytePatcher::SearchReplaceResult BytePatcher::search_and_replace(
    ea_t start_address, ea_t end_address,
    const std::vector<uint8_t>& search_pattern,
    const std::vector<uint8_t>& replace_pattern,
    const std::string& description,
    bool replace_all) {
    
    SearchReplaceResult result;
    result.occurrences_found = 0;
    result.occurrences_replaced = 0;
    
    if (search_pattern.empty() || replace_pattern.size() != search_pattern.size()) {
        return result;
    }
    
    // Find all occurrences
    std::vector<ea_t> occurrences = find_pattern(start_address, end_address, search_pattern);
    result.occurrences_found = occurrences.size();
    
    // Replace occurrences
    for (ea_t addr : occurrences) {
        BytePatchResult patch_result = apply_patch(addr, replace_pattern,
            description + " at " + std::to_string(addr), true, search_pattern);
        
        if (patch_result.success) {
            result.occurrences_replaced++;
            result.replaced_addresses.push_back(addr);
            
            if (!replace_all) {
                break;
            }
        }
    }
    
    return result;
}

std::vector<uint8_t> BytePatcher::hex_string_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    std::string cleaned;
    
    // Remove spaces and validate characters
    for (char c : hex) {
        if (std::isspace(c)) continue;
        if (!std::isxdigit(c)) return bytes;  // Invalid hex
        cleaned += c;
    }
    
    // Must have even number of hex digits
    if (cleaned.length() % 2 != 0) return bytes;
    
    // Convert pairs of hex digits to bytes
    for (size_t i = 0; i < cleaned.length(); i += 2) {
        std::string byte_str = cleaned.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        bytes.push_back(byte);
    }
    
    return bytes;
}

std::string BytePatcher::bytes_to_hex_string(const std::vector<uint8_t>& bytes, bool add_spaces) {
    std::stringstream ss;
    ss << std::hex << std::uppercase << std::setfill('0');
    
    for (size_t i = 0; i < bytes.size(); i++) {
        if (i > 0 && add_spaces) ss << " ";
        ss << std::setw(2) << static_cast<int>(bytes[i]);
    }
    
    return ss.str();
}

bool BytePatcher::is_valid_hex_string(const std::string& hex) {
    if (hex.empty()) return false;
    
    size_t hex_count = 0;
    for (char c : hex) {
        if (std::isspace(c)) continue;
        if (!std::isxdigit(c)) return false;
        hex_count++;
    }
    
    // Must have even number of hex digits
    return hex_count > 0 && hex_count % 2 == 0;
}

bool BytePatcher::is_valid_range(ea_t start_address, ea_t end_address, std::string& error_msg) {
    if (start_address >= end_address) {
        error_msg = "Invalid range: start address must be less than end address";
        return false;
    }
    
    if (!is_mapped(start_address)) {
        error_msg = "Start address is not mapped";
        return false;
    }
    
    if (!is_mapped(end_address - 1)) {
        error_msg = "End address is not mapped";
        return false;
    }
    
    return true;
}

std::vector<uint8_t> BytePatcher::read_bytes(ea_t address, size_t size) {
    std::vector<uint8_t> bytes(size);
    get_bytes(bytes.data(), size, address);
    return bytes;
}

BytePatcher::MemoryType BytePatcher::get_memory_type(ea_t address) {
    flags_t flags = get_flags(address);
    
    if (is_code(flags)) {
        return MemoryType::CODE;
    } else if (is_data(flags)) {
        return MemoryType::DATA;
    }
    
    return MemoryType::UNKNOWN;
}

std::vector<uint8_t> BytePatcher::value_to_bytes(uint64_t value, size_t size, bool little_endian) {
    std::vector<uint8_t> bytes(size);
    
    for (size_t i = 0; i < size; i++) {
        if (little_endian) {
            bytes[i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
        } else {
            bytes[size - 1 - i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
        }
    }
    
    return bytes;
}

std::vector<ea_t> BytePatcher::find_pattern(ea_t start_address, ea_t end_address,
                                          const std::vector<uint8_t>& pattern) {
    std::vector<ea_t> results;
    
    if (pattern.empty() || start_address >= end_address) {
        return results;
    }
    
    // Read the search range
    size_t range_size = end_address - start_address;
    std::vector<uint8_t> haystack = read_bytes(start_address, range_size);
    
    // Search for pattern
    for (size_t i = 0; i <= haystack.size() - pattern.size(); i++) {
        bool match = true;
        for (size_t j = 0; j < pattern.size(); j++) {
            if (haystack[i + j] != pattern[j]) {
                match = false;
                break;
            }
        }
        
        if (match) {
            results.push_back(start_address + i);
            // Skip past this match to avoid overlapping matches
            i += pattern.size() - 1;
        }
    }
    
    return results;
}

} // namespace llm_re