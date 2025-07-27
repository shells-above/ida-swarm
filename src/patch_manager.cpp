//
// Created by user on 11/27/24.
//

#include "patch_manager.h"
#include <ida.hpp>
#include <bytes.hpp>
#include <auto.hpp>
#include <loader.hpp>
#include <diskio.hpp>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace llm_re {

PatchManager::PatchManager() : backup_created_(false) {
    // Get input file path
    char buf[MAXSTR];
    get_input_file_path(buf, sizeof(buf));
    input_file_path_ = buf;
}

PatchManager::~PatchManager() = default;

bool PatchManager::initialize() {
    // Create backup path
    backup_path_ = input_file_path_ + ".bak";
    
    // Check if backup already exists
    if (qfileexist(backup_path_.c_str())) {
        backup_created_ = true;
    }
    
    return true;
}

PatchResult PatchManager::apply_patch(ea_t address, const std::vector<uint8_t>& new_bytes,
                                    const std::string& description, bool verify_original,
                                    const std::vector<uint8_t>& expected_original) {
    PatchResult result;
    result.success = false;
    
    // Validate patch size
    if (!validate_patch_size(address, new_bytes.size(), result.error_message)) {
        return result;
    }
    
    // Read current bytes
    std::vector<uint8_t> original_bytes = read_bytes(address, new_bytes.size());
    
    // Verify original bytes if requested
    if (verify_original && !expected_original.empty()) {
        if (original_bytes != expected_original) {
            result.error_message = "Original bytes do not match expected bytes";
            return result;
        }
    }
    
    // Check if already patched at this address
    if (patches_.find(address) != patches_.end()) {
        result.error_message = "Address already patched. Revert existing patch first.";
        return result;
    }
    
    // Create backup on first patch
    if (!backup_created_) {
        if (!create_backup()) {
            result.error_message = "Failed to create backup";
            return result;
        }
    }
    
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
    entry.is_assembly_patch = false;
    
    // Store patch
    patches_[address] = entry;
    
    // Trigger reanalysis
    trigger_reanalysis(address, new_bytes.size());
    
    result.success = true;
    result.patch_entry = entry;
    return result;
}

PatchResult PatchManager::apply_assembly_patch(ea_t address, const std::vector<uint8_t>& new_bytes,
                                             const std::string& original_asm, const std::string& new_asm,
                                             const std::string& description, bool verify_original,
                                             const std::vector<uint8_t>& expected_original) {
    // Use base apply_patch
    PatchResult result = apply_patch(address, new_bytes, description, verify_original, expected_original);
    
    // If successful, update with assembly info
    if (result.success && result.patch_entry) {
        result.patch_entry->is_assembly_patch = true;
        result.patch_entry->original_asm = original_asm;
        result.patch_entry->patched_asm = new_asm;
        
        // Update stored patch
        patches_[address] = *result.patch_entry;
    }
    
    return result;
}

bool PatchManager::revert_patch(ea_t address) {
    auto it = patches_.find(address);
    if (it == patches_.end()) {
        return false;
    }
    
    // Restore original bytes
    if (!write_bytes(address, it->second.original_bytes)) {
        return false;
    }
    
    // Remove from patches
    patches_.erase(it);
    
    // Trigger reanalysis
    trigger_reanalysis(address, it->second.original_bytes.size());
    
    return true;
}

bool PatchManager::revert_range(ea_t start_ea, ea_t end_ea) {
    bool any_reverted = false;
    
    // Collect patches in range
    std::vector<ea_t> to_revert;
    for (const auto& [addr, patch] : patches_) {
        if (addr >= start_ea && addr < end_ea) {
            to_revert.push_back(addr);
        }
    }
    
    // Revert each patch
    for (ea_t addr : to_revert) {
        if (revert_patch(addr)) {
            any_reverted = true;
        }
    }
    
    return any_reverted;
}

bool PatchManager::revert_all() {
    // Collect all addresses
    std::vector<ea_t> to_revert;
    for (const auto& [addr, patch] : patches_) {
        to_revert.push_back(addr);
    }
    
    // Revert each patch
    bool all_reverted = true;
    for (ea_t addr : to_revert) {
        if (!revert_patch(addr)) {
            all_reverted = false;
        }
    }
    
    return all_reverted;
}

bool PatchManager::is_patched(ea_t address) const {
    return patches_.find(address) != patches_.end();
}

bool PatchManager::has_patches_in_range(ea_t start_ea, ea_t end_ea) const {
    for (const auto& [addr, patch] : patches_) {
        if (addr >= start_ea && addr < end_ea) {
            return true;
        }
    }
    return false;
}

std::optional<PatchEntry> PatchManager::get_patch(ea_t address) const {
    auto it = patches_.find(address);
    if (it != patches_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<PatchEntry> PatchManager::get_all_patches() const {
    std::vector<PatchEntry> result;
    for (const auto& [addr, patch] : patches_) {
        result.push_back(patch);
    }
    return result;
}

std::vector<PatchEntry> PatchManager::get_patches_in_range(ea_t start_ea, ea_t end_ea) const {
    std::vector<PatchEntry> result;
    for (const auto& [addr, patch] : patches_) {
        if (addr >= start_ea && addr < end_ea) {
            result.push_back(patch);
        }
    }
    return result;
}

json PatchManager::export_patches() const {
    json patches_json = json::array();
    
    for (const auto& [addr, patch] : patches_) {
        json patch_json;
        patch_json["address"] = HexAddress(patch.address);
        patch_json["original_bytes"] = patch.original_bytes;
        patch_json["patched_bytes"] = patch.patched_bytes;
        patch_json["description"] = patch.description;
        patch_json["timestamp"] = std::chrono::system_clock::to_time_t(patch.timestamp);
        patch_json["is_assembly_patch"] = patch.is_assembly_patch;
        
        if (patch.is_assembly_patch) {
            patch_json["original_asm"] = patch.original_asm;
            patch_json["patched_asm"] = patch.patched_asm;
        }
        
        patches_json.push_back(patch_json);
    }
    
    return patches_json;
}

bool PatchManager::import_patches(const json& patches_json) {
    if (!patches_json.is_array()) {
        return false;
    }
    
    patches_.clear();
    
    for (const auto& patch_json : patches_json) {
        PatchEntry entry;
        entry.address = patch_json["address"].get<ea_t>();
        entry.original_bytes = patch_json["original_bytes"].get<std::vector<uint8_t>>();
        entry.patched_bytes = patch_json["patched_bytes"].get<std::vector<uint8_t>>();
        entry.description = patch_json["description"];
        entry.timestamp = std::chrono::system_clock::from_time_t(patch_json["timestamp"]);
        entry.is_assembly_patch = patch_json["is_assembly_patch"];
        
        if (entry.is_assembly_patch) {
            entry.original_asm = patch_json["original_asm"];
            entry.patched_asm = patch_json["patched_asm"];
        }
        
        patches_[entry.address] = entry;
    }
    
    return true;
}

bool PatchManager::save_patches(const std::string& filename) const {
    try {
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        file << export_patches().dump(4);
        return true;
    } catch (...) {
        return false;
    }
}

bool PatchManager::load_patches(const std::string& filename) {
    try {
        std::ifstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        json patches_json;
        file >> patches_json;
        return import_patches(patches_json);
    } catch (...) {
        return false;
    }
}

bool PatchManager::has_backup() const {
    return qfileexist(backup_path_.c_str());
}

bool PatchManager::create_backup() {
    if (backup_created_) {
        return true;
    }
    
    // Copy input file to backup
    if (!qcopyfile(input_file_path_.c_str(), backup_path_.c_str())) {
        return false;
    }
    
    backup_created_ = true;
    return true;
}

bool PatchManager::restore_from_backup() {
    if (!has_backup()) {
        return false;
    }
    
    // First revert all patches in memory
    revert_all();
    
    // Then copy backup file over original
    return qcopyfile(backup_path_.c_str(), input_file_path_.c_str());
}

PatchManager::Statistics PatchManager::get_statistics() const {
    Statistics stats{};
    stats.total_patches = patches_.size();
    
    if (!patches_.empty()) {
        stats.first_patch_time = patches_.begin()->second.timestamp;
        stats.last_patch_time = patches_.begin()->second.timestamp;
        
        for (const auto& [addr, patch] : patches_) {
            if (patch.is_assembly_patch) {
                stats.assembly_patches++;
            } else {
                stats.byte_patches++;
            }
            
            stats.total_bytes_patched += patch.patched_bytes.size();
            
            if (patch.timestamp < stats.first_patch_time) {
                stats.first_patch_time = patch.timestamp;
            }
            if (patch.timestamp > stats.last_patch_time) {
                stats.last_patch_time = patch.timestamp;
            }
        }
    }
    
    return stats;
}

bool PatchManager::validate_patch_size(ea_t address, size_t patch_size, std::string& error_msg) {
    // Check if address is valid
    if (!is_mapped(address)) {
        error_msg = "Address is not mapped in binary";
        return false;
    }
    
    // Check if entire patch range is valid
    if (!is_mapped(address + patch_size - 1)) {
        error_msg = "Patch extends beyond mapped memory";
        return false;
    }
    
    return true;
}

bool PatchManager::check_instruction_boundaries(ea_t address, size_t patch_size, std::string& error_msg) {
    // Get instruction at address
    insn_t insn;
    if (decode_insn(&insn, address) == 0) {
        error_msg = "Failed to decode instruction at address";
        return false;
    }
    
    // Check if patch would split an instruction
    ea_t end_address = address + patch_size;
    ea_t current = address;
    
    while (current < end_address) {
        if (decode_insn(&insn, current) == 0) {
            error_msg = "Failed to decode instruction in patch range";
            return false;
        }
        
        current += insn.size;
        
        // If instruction extends beyond patch, it would be split
        if (current > end_address && current - insn.size < end_address) {
            error_msg = "Patch would split instruction at boundary";
            return false;
        }
    }
    
    return true;
}

std::vector<uint8_t> PatchManager::read_bytes(ea_t address, size_t size) {
    std::vector<uint8_t> bytes(size);
    get_bytes(bytes.data(), size, address);
    return bytes;
}

bool PatchManager::write_bytes(ea_t address, const std::vector<uint8_t>& bytes) {
    // Use IDA's patch_bytes function
    patch_bytes(address, bytes.data(), bytes.size());
    return true;
}

void PatchManager::trigger_reanalysis(ea_t address, size_t size) {
    // Mark range for reanalysis
    del_items(address, DELIT_SIMPLE, size);
    auto_mark_range(address, address + size, AU_USED);
    auto_wait();
}

} // namespace llm_re