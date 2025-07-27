//
// Created by user on 11/27/24.
//

#ifndef PATCH_MANAGER_H
#define PATCH_MANAGER_H

#include "common.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <chrono>
#include <optional>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace llm_re {

// Represents a single patch applied to the binary
struct PatchEntry {
    ea_t address;
    std::vector<uint8_t> original_bytes;
    std::vector<uint8_t> patched_bytes;
    std::string description;
    std::chrono::system_clock::time_point timestamp;
    bool is_assembly_patch;  // true if created from assembly, false if raw bytes
    std::string original_asm;  // Only filled for assembly patches
    std::string patched_asm;   // Only filled for assembly patches
};

// Result of a patch operation
struct PatchResult {
    bool success;
    std::string error_message;
    std::optional<PatchEntry> patch_entry;
};

// Manages all patches applied to the binary
class PatchManager {
public:
    PatchManager();
    ~PatchManager();

    // Initialize patch manager (creates backup if needed)
    bool initialize();

    // Apply a patch (validates before applying)
    PatchResult apply_patch(ea_t address, const std::vector<uint8_t>& new_bytes,
                          const std::string& description, bool verify_original = true,
                          const std::vector<uint8_t>& expected_original = {});

    // Apply an assembly patch (includes assembly strings for tracking)
    PatchResult apply_assembly_patch(ea_t address, const std::vector<uint8_t>& new_bytes,
                                   const std::string& original_asm, const std::string& new_asm,
                                   const std::string& description, bool verify_original = true,
                                   const std::vector<uint8_t>& expected_original = {});

    // Revert a specific patch
    bool revert_patch(ea_t address);

    // Revert all patches in a range
    bool revert_range(ea_t start_ea, ea_t end_ea);

    // Revert all patches
    bool revert_all();

    // Check if an address is patched
    bool is_patched(ea_t address) const;

    // Check if a range contains any patches
    bool has_patches_in_range(ea_t start_ea, ea_t end_ea) const;

    // Get patch information for an address
    std::optional<PatchEntry> get_patch(ea_t address) const;

    // Get all patches
    std::vector<PatchEntry> get_all_patches() const;

    // Get patches in a range
    std::vector<PatchEntry> get_patches_in_range(ea_t start_ea, ea_t end_ea) const;

    // Save patches to file (for persistence)
    bool save_patches(const std::string& filename) const;

    // Load patches from file
    bool load_patches(const std::string& filename);

    // Export patches to JSON
    json export_patches() const;

    // Import patches from JSON
    bool import_patches(const json& patches_json);

    // Get backup file path
    std::string get_backup_path() const { return backup_path_; }

    // Check if backup exists
    bool has_backup() const;

    // Create backup of current binary
    bool create_backup();

    // Restore from backup
    bool restore_from_backup();

    // Get statistics
    struct Statistics {
        size_t total_patches;
        size_t assembly_patches;
        size_t byte_patches;
        size_t total_bytes_patched;
        std::chrono::system_clock::time_point first_patch_time;
        std::chrono::system_clock::time_point last_patch_time;
    };
    Statistics get_statistics() const;

    // Read bytes from address (public for BytePatcher)
    std::vector<uint8_t> read_bytes(ea_t address, size_t size);

private:
    // Map of address to patch entry
    std::unordered_map<ea_t, PatchEntry> patches_;

    // Path to backup file
    std::string backup_path_;

    // Whether we've created a backup
    bool backup_created_;

    // Input file path
    std::string input_file_path_;

    // Helper functions
    bool validate_patch_size(ea_t address, size_t patch_size, std::string& error_msg);
    bool check_instruction_boundaries(ea_t address, size_t patch_size, std::string& error_msg);
    bool write_bytes(ea_t address, const std::vector<uint8_t>& bytes);
    void trigger_reanalysis(ea_t address, size_t size);
};

} // namespace llm_re

#endif // PATCH_MANAGER_H