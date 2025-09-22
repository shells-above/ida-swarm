#ifndef CODE_INJECTION_MANAGER_H
#define CODE_INJECTION_MANAGER_H

#include "core/common.h"
#include "patch_manager.h"
#include <segment.hpp>
#include <bytes.hpp>
#include <loader.hpp>
#include <chrono>
#include <unordered_map>
#include <memory>

#include "orchestrator/nogo_zone_manager.h"

namespace llm_re {

// Result structures for code injection operations
struct WorkspaceAllocation {
    bool success = false;
    ea_t temp_segment_ea = BADADDR;      // Temporary segment address in IDA
    size_t allocated_size = 0;            // Size allocated (overestimated)
    std::string segment_name;             // e.g., ".tmpcode_001"
    std::string error_message;
    bool is_temporary = true;             // Flag to indicate this is temporary
};

struct CodePreviewResult {
    bool success = false;
    ea_t start_ea = BADADDR;
    ea_t end_ea = BADADDR;
    size_t code_size = 0;
    std::string disassembly;
    std::vector<uint8_t> final_bytes;
    std::string error_message;
};

struct CodeFinalizationResult {
    bool success = false;
    ea_t old_temp_address = BADADDR;     // Previous temporary address
    ea_t new_permanent_address = BADADDR; // Final permanent address
    size_t code_size = 0;
    std::string relocation_method;        // "code_cave" or "new_segment"
    std::string error_message;
};

// Manages code injection with temporary workspace and relocation
class CodeInjectionManager {
public:
    CodeInjectionManager(PatchManager* patch_manager, const std::string& binary_path);
    ~CodeInjectionManager();

    // Initialize the manager
    bool initialize();

    // Stage 1: Allocate temporary workspace for code development
    WorkspaceAllocation allocate_code_workspace(size_t requested_bytes);

    // Stage 2: Preview the code injection (MANDATORY before finalization)
    CodePreviewResult preview_code_injection(ea_t start_address, ea_t end_address);

    // Stage 3: Finalize and relocate the code to permanent location
    CodeFinalizationResult finalize_code_injection(ea_t start_address, ea_t end_address);

    // Check if an address is in a temporary workspace
    bool is_in_temp_workspace(ea_t address) const;

    // Get information about active workspaces
    std::vector<std::pair<ea_t, size_t>> get_active_workspaces() const;

    // No-go zone management
    void set_no_go_zones(const std::vector<orchestrator::NoGoZone>& zones);
    void create_placeholder_segments_for_no_go_zones();

private:
    // Workspace information tracking
    struct WorkspaceInfo {
        ea_t start_ea;
        ea_t end_ea;
        size_t size;
        std::string segment_name;
        std::chrono::system_clock::time_point created_at;
        bool is_temporary;
    };

    // Preview record for validation
    struct PreviewRecord {
        ea_t start_ea;
        ea_t end_ea;
        size_t code_size;
        std::string disassembly;
        std::vector<uint8_t> final_bytes;
        std::chrono::system_clock::time_point preview_time;
        bool previewed;
    };

    // Code cave information
    struct CodeCave {
        bool found;
        ea_t address;
        size_t size;
        uint32_t file_offset;
    };

    // Private members
    PatchManager* patch_manager_;  // Reference to the patch manager
    std::unordered_map<ea_t, WorkspaceInfo> active_workspaces_;  // Track temp segments
    std::map<std::pair<ea_t, ea_t>, PreviewRecord> preview_cache_;  // Cache previews for validation
    uint32_t next_workspace_id_ = 1;  // For unique segment naming
    std::string agent_binary_path_;  // Path to the agent's copy of the binary
    std::vector<orchestrator::NoGoZone> no_go_zones_;  // No-go zones from other agents

    // Segment management
    ea_t find_safe_address_after_segments() const;
    bool create_temp_segment(ea_t address, size_t size, const std::string& name);
    bool delete_temp_segment(ea_t address);

    // Code cave finding
    CodeCave find_code_cave(size_t needed_size) const;
    size_t count_cave_bytes(ea_t address, size_t max_size) const;
    bool is_in_no_go_zone(ea_t address, size_t size) const;

    // Binary file patching
    bool apply_to_binary_file(uint32_t file_offset, const std::vector<uint8_t>& bytes);

    // New segment creation with LIEF (if no code cave found)
    struct NewSegment {
        ea_t address;
        size_t size;
        std::string name;  // Platform-appropriate segment name
    };
    NewSegment create_permanent_segment(size_t needed_size);
    bool add_segment_with_lief(const NewSegment& segment, const std::vector<uint8_t>& bytes);

    // Utility functions
    std::string get_disassembly(ea_t start, ea_t end) const;
    std::vector<uint8_t> get_bytes_from_range(ea_t start, ea_t end) const;

    // Utility functions
    size_t align_up(size_t size, size_t alignment) const;
    std::string format_address(ea_t address) const;
};

} // namespace llm_re

#endif // CODE_INJECTION_MANAGER_H