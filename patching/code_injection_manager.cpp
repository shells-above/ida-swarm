#include "code_injection_manager.h"
#include "../core/logger.h"
#include "core/ida_utils.h"
#include <ida.hpp>
#include <idp.hpp>
#include <auto.hpp>
#include <funcs.hpp>
#include <ua.hpp>
#include <lines.hpp>
#include <nalt.hpp>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

#include "orchestrator/nogo_zone_manager.h"

// LIEF headers for binary modification
#include <LIEF/LIEF.hpp>

namespace llm_re {

// Constructor
CodeInjectionManager::CodeInjectionManager(PatchManager* patch_manager, const std::string& binary_path)
    : patch_manager_(patch_manager), agent_binary_path_(binary_path) {
}

// Destructor
CodeInjectionManager::~CodeInjectionManager() {
    // Clean up any temporary segments on destruction
    for (const auto& [addr, info] : active_workspaces_) {
        if (info.is_temporary) {
            delete_temp_segment(addr);
        }
    }
}

// Initialize the manager
bool CodeInjectionManager::initialize() {
    // Validate binary path exists (required for segment injection)
    if (agent_binary_path_.empty()) {
        LOG("CodeInjectionManager: ERROR - No binary path provided\n");
        return false;
    }

    if (!std::filesystem::exists(agent_binary_path_)) {
        LOG("CodeInjectionManager: ERROR - Binary file not found: %s\n", agent_binary_path_.c_str());
        return false;
    }

    LOG("CodeInjectionManager: Initialized with binary: %s\n", agent_binary_path_.c_str());
    return true;
}

// PaddingDetector implementation
CodeInjectionManager::PaddingDetector::PaddingDetector() {
    // Get platform information
    arch_ = PH.id;
    is_64bit_ = inf_is_64bit();
    file_type_ = inf_get_filetype();

    // Initialize padding bytes based on architecture and file format
    switch (arch_) {
        case PLFM_386:  // x86/x64 (Intel/AMD)
            // x86 uses NOP (0x90), INT3 (0xCC), and zeros (0x00)
            // PE files favor INT3, ELF/Mach-O favor NOP
            if (file_type_ == f_PE) {
                // Windows PE: INT3 is most common for padding
                padding_bytes_ = {0xCC, 0x90, 0x00};
                primary_padding_ = 0xCC;
            } else {
                // Linux ELF / macOS Mach-O: NOP is most common
                padding_bytes_ = {0x90, 0xCC, 0x00};
                primary_padding_ = 0x90;
            }
            break;

        case PLFM_ARM:  // ARM (32-bit and 64-bit)
            // ARM uses zero padding
            padding_bytes_ = {0x00};
            primary_padding_ = 0x00;
            break;

        case PLFM_MIPS:  // MIPS
            // MIPS uses zero padding (NOP is 0x00000000)
            padding_bytes_ = {0x00};
            primary_padding_ = 0x00;
            break;

        case PLFM_PPC:  // PowerPC
            // PowerPC uses zero padding
            padding_bytes_ = {0x00};
            primary_padding_ = 0x00;
            break;

        default:
            // Conservative default: only accept zeros
            padding_bytes_ = {0x00};
            primary_padding_ = 0x00;
            LOG("CodeInjectionManager: Unknown architecture %u, using conservative padding detection (0x00 only)\n", (unsigned int)arch_);
            break;
    }
}

bool CodeInjectionManager::PaddingDetector::is_padding_byte(uint8_t byte) const {
    return std::find(padding_bytes_.begin(), padding_bytes_.end(), byte) != padding_bytes_.end();
}

// Helper to get padding detector (lazy initialization)
const CodeInjectionManager::PaddingDetector& CodeInjectionManager::get_padding_detector() const {
    if (!padding_detector_) {
        padding_detector_ = std::make_unique<PaddingDetector>();
    }
    return *padding_detector_;
}

// Stage 1: Allocate temporary workspace for code development
WorkspaceAllocation CodeInjectionManager::allocate_code_workspace(size_t requested_bytes) {
    WorkspaceAllocation result;

    // Overestimate the size by 50% and align to page boundary
    size_t actual_size = align_up(requested_bytes * 1.5, 0x1000);

    // Find a safe address after all existing segments
    ea_t new_ea = find_safe_address_after_segments();
    if (new_ea == BADADDR) {
        result.error_message = "Failed to find suitable address for temporary segment";
        return result;
    }

    // Generate unique segment name
    std::stringstream ss;
    ss << ".tmpcode_" << std::setfill('0') << std::setw(3) << next_workspace_id_++;
    std::string seg_name = ss.str();

    // Create the temporary segment in IDA database only
    if (!create_temp_segment(new_ea, actual_size, seg_name)) {
        result.error_message = "Failed to create temporary segment in IDA database";
        return result;
    }

    // Track this workspace
    WorkspaceInfo info;
    info.start_ea = new_ea;
    info.end_ea = new_ea + actual_size;
    info.size = actual_size;
    info.segment_name = seg_name;
    info.created_at = std::chrono::system_clock::now();
    info.is_temporary = true;
    active_workspaces_[new_ea] = info;

    // Prepare successful result
    result.success = true;
    result.temp_segment_ea = new_ea;
    result.allocated_size = actual_size;
    result.segment_name = seg_name;
    result.is_temporary = true;

    LOG("CodeInjectionManager: Allocated temporary workspace at 0x%llX, size 0x%zX\n",
        (uint64_t)new_ea, actual_size);

    return result;
}

// Stage 2: Preview the code injection (MANDATORY before finalization)
CodePreviewResult CodeInjectionManager::preview_code_injection(ea_t start_address, ea_t end_address) {
    CodePreviewResult result;

    // Validate address range
    if (start_address >= end_address) {
        result.error_message = "Invalid address range: start must be less than end";
        return result;
    }

    // Check if addresses are in a temporary workspace
    if (!is_in_temp_workspace(start_address) || !is_in_temp_workspace(end_address - 1)) {
        result.error_message = "Address range not entirely within a temporary workspace";
        return result;
    }

    // Force IDA to analyze the range
    plan_range(start_address, end_address);
    auto_wait();

    // Get disassembly
    std::string disasm = get_disassembly(start_address, end_address);
    if (disasm.empty()) {
        result.error_message = "Failed to get disassembly for the range";
        return result;
    }

    // Get actual bytes
    std::vector<uint8_t> bytes = get_bytes_from_range(start_address, end_address);
    if (bytes.empty()) {
        result.error_message = "Failed to read bytes from the range";
        return result;
    }

    // Create preview record for validation
    PreviewRecord record;
    record.start_ea = start_address;
    record.end_ea = end_address;
    record.code_size = end_address - start_address;
    record.disassembly = disasm;
    record.final_bytes = bytes;
    record.preview_time = std::chrono::system_clock::now();
    record.previewed = true;

    // Cache the preview for validation
    preview_cache_[{start_address, end_address}] = record;

    // Prepare successful result
    result.success = true;
    result.start_ea = start_address;
    result.end_ea = end_address;
    result.code_size = record.code_size;
    result.disassembly = disasm;
    result.final_bytes = bytes;

    LOG("CodeInjectionManager: Preview successful for range 0x%llX-0x%llX (%lu bytes)\n",
        (uint64_t)start_address, (uint64_t)end_address, bytes.size());

    return result;
}

// Stage 3: Finalize and relocate the code to permanent location
CodeFinalizationResult CodeInjectionManager::finalize_code_injection(ea_t start_address, ea_t end_address) {
    CodeFinalizationResult result;

    // Check if preview was done
    auto key = std::make_pair(start_address, end_address);
    auto preview_it = preview_cache_.find(key);
    if (preview_it == preview_cache_.end() || !preview_it->second.previewed) {
        result.error_message = "ERROR: You MUST call preview_code_injection first!\n"
                              "This is a safety requirement. Preview your code at " +
                              format_address(start_address) + " to " +
                              format_address(end_address) + " before finalizing.";
        return result;
    }

    const PreviewRecord& preview = preview_it->second;
    size_t needed_size = preview.code_size;
    ea_t final_address = BADADDR;
    std::string method;

    // Try to find a code cave first
    CodeCave cave = find_code_cave(needed_size);
    if (cave.found) {
        final_address = cave.address;
        method = "code_cave";

        // Read original bytes from the code cave (padding bytes)
        std::vector<uint8_t> original_bytes = get_bytes_from_range(cave.address, cave.address + needed_size);

        // Use PatchManager to apply byte patch to code cave
        BytePatchResult patch_result = patch_manager_->apply_byte_patch(
            cave.address,
            PatchManager::bytes_to_hex_string(original_bytes),
            PatchManager::bytes_to_hex_string(preview.final_bytes),
            "Code injection via code cave"
        );

        if (!patch_result.success) {
            result.error_message = "Failed to patch code cave: " + patch_result.error_message;
            return result;
        }

        LOG("CodeInjectionManager: Patched code cave at 0x%llX via PatchManager\n", (uint64_t)cave.address);
    } else {
        // Need to create new segment
        // Find address for new segment
        ea_t segment_address = find_safe_address_after_segments();
        if (segment_address == BADADDR) {
            result.error_message = "Failed to find safe address for new segment";
            return result;
        }

        // Align size to page boundary
        size_t aligned_size = align_up(needed_size, 0x1000);

        // Generate platform-appropriate segment name
        std::string segment_name = generate_segment_name_for_address(segment_address);

        // Use PatchManager to create segment injection
        SegmentInjectionResult seg_result = patch_manager_->apply_segment_injection(
            segment_address,
            aligned_size,
            preview.final_bytes,
            segment_name,
            "Code injection via new segment"
        );

        if (!seg_result.success) {
            result.error_message = "Failed to inject segment: " + seg_result.error_message;
            return result;
        }

        final_address = seg_result.segment_address;
        method = "new_segment";

        LOG("CodeInjectionManager: Created new segment at 0x%llX via PatchManager\n", (uint64_t)final_address);
    }

    // Mark the relocated code as CODE for IDA analysis
    // This ensures IDA disassembles it instead of showing raw data bytes
    LOG("CodeInjectionManager: Marking relocated code at 0x%llX as CODE\n", (uint64_t)final_address);
    IDAUtils::execute_sync_wrapper([&]() -> bool {
        plan_range(final_address, final_address + needed_size);
        plan_and_wait(final_address, final_address + needed_size);
        return true;
    }, MFF_WRITE);
    LOG("CodeInjectionManager: Code analysis complete at 0x%llX\n", (uint64_t)final_address);

    // Delete temporary segment from IDA database
    delete_temp_segment(start_address);
    active_workspaces_.erase(start_address);

    // Clear preview cache for this range
    preview_cache_.erase(key);

    // Prepare successful result
    result.success = true;
    result.old_temp_address = start_address;
    result.new_permanent_address = final_address;
    result.code_size = needed_size;
    result.relocation_method = method;

    LOG("CodeInjectionManager: Code relocated from 0x%llX to 0x%llX using %s\n",
        (uint64_t)start_address, (uint64_t)final_address, method.c_str());
    LOG("CodeInjectionManager: IMPORTANT - Review all patches for references to old address\n");

    return result;
}

// Check if an address is in a temporary workspace
bool CodeInjectionManager::is_in_temp_workspace(ea_t address) const {
    for (const auto& [start_ea, info] : active_workspaces_) {
        if (info.is_temporary && address >= info.start_ea && address < info.end_ea) {
            return true;
        }
    }
    return false;
}

// Get information about active workspaces
std::vector<std::pair<ea_t, size_t>> CodeInjectionManager::get_active_workspaces() const {
    std::vector<std::pair<ea_t, size_t>> result;
    for (const auto& [addr, info] : active_workspaces_) {
        if (info.is_temporary) {
            result.push_back({addr, info.size});
        }
    }
    return result;
}

// Find a safe address after all existing segments
ea_t CodeInjectionManager::find_safe_address_after_segments() const {
    ea_t last_seg_end = 0;

    // Iterate through all segments to find the highest end address
    for (int i = 0; i < get_segm_qty(); i++) {
        segment_t* seg = getnseg(i);
        if (seg && seg->end_ea > last_seg_end) {
            last_seg_end = seg->end_ea;
        }
    }

    // Align to next page boundary
    ea_t new_address = align_up(last_seg_end, 0x1000);

    // Make sure it doesn't overflow
    if (new_address < last_seg_end) {
        return BADADDR;
    }

    return new_address;
}

// Create a temporary segment in IDA database only
bool CodeInjectionManager::create_temp_segment(ea_t address, size_t size, const std::string& name) {
    // Use add_segm() which is simpler and avoids segment_t memory management issues
    // add_segm() signature: add_segm(para, start, end, name, sclass, flags)
    // - para: paragraph (alignment) - use 0 for default
    // - start/end: address range
    // - name: segment name
    // - sclass: segment class
    // - flags: ADDSEG_* flags

    // ADDSEG_QUIET: Silent mode for automated operation
    // ADDSEG_SPARSE: Use sparse storage (efficient for temp segments)
    if (!add_segm(0, address, address + size, name.c_str(), "CODE",
                  ADDSEG_QUIET | ADDSEG_SPARSE)) {
        LOG("CodeInjectionManager: Failed to create segment %s at 0x%llX\n",
            name.c_str(), (uint64_t)address);
        return false;
    }

    // After creation, get the segment and set its permissions
    segment_t* seg = getseg(address);
    if (seg) {
        seg->perm = SEGPERM_EXEC | SEGPERM_READ | SEGPERM_WRITE;
        seg->type = SEG_CODE;

        // Set bitness based on binary's architecture
        uint app_bitness = inf_get_app_bitness();
        switch (app_bitness) {
            case 64:
                seg->bitness = 2;  // 64-bit
                break;
            case 32:
                seg->bitness = 1;  // 32-bit
                break;
            case 16:
                seg->bitness = 0;  // 16-bit
                break;
            default:
                seg->bitness = 1;  // Default to 32-bit if unknown
                LOG("CodeInjectionManager: Warning - Unknown bitness %d, defaulting to 32-bit\n", app_bitness);
                break;
        }

        // Update the segment in the database
        update_segm(seg);
    }

    LOG("CodeInjectionManager: Created temporary segment %s at 0x%llX-0x%llX\n",
        name.c_str(), (uint64_t)address, (uint64_t)(address + size));

    return true;
}

// Delete a temporary segment
bool CodeInjectionManager::delete_temp_segment(ea_t address) {
    // Find the segment at this address
    segment_t* seg = getseg(address);
    if (!seg) {
        LOG("CodeInjectionManager: No segment found at 0x%llX\n", (uint64_t)address);
        return false;
    }

    // Delete the segment
    if (!del_segm(address, SEGMOD_KILL)) {
        LOG("CodeInjectionManager: Failed to delete segment at 0x%llX\n", (uint64_t)address);
        return false;
    }

    LOG("CodeInjectionManager: Deleted temporary segment at 0x%llX\n", (uint64_t)address);
    return true;
}

// Find a code cave of sufficient size
CodeInjectionManager::CodeCave CodeInjectionManager::find_code_cave(size_t needed_size) const {
    CodeCave result = {false, BADADDR, 0, 0};

    // Iterate through all segments looking for caves
    for (int i = 0; i < get_segm_qty(); i++) {
        segment_t* seg = getnseg(i);
        if (!seg || seg->type != SEG_CODE) {
            continue;
        }

        // Check if segment has executable permissions
        if (!(seg->perm & SEGPERM_EXEC)) {
            continue;
        }

        // Scan the segment for caves (architecture-aware padding byte detection)
        // x86/x64: NOP (0x90), INT3 (0xCC), zeros (0x00)
        // ARM/MIPS/PPC: zeros (0x00)
        ea_t current = seg->start_ea;
        while (current < seg->end_ea) {
            // Check how many consecutive cave bytes we have at this position
            size_t cave_bytes = count_cave_bytes(current, needed_size);

            if (cave_bytes >= needed_size) {
                // Check if this cave is in a no-go zone
                if (is_in_no_go_zone(current, needed_size)) {
                    LOG("CodeInjectionManager: Skipping code cave at 0x%llX - in no-go zone\n",
                        (uint64_t)current);
                    current += needed_size;  // Skip past this cave
                    continue;
                }

                // Found a suitable cave!
                result.found = true;
                result.address = current;
                result.size = needed_size;
                result.file_offset = get_fileregion_offset(current);

                LOG("CodeInjectionManager: Found code cave at 0x%llX, size 0x%zX\n",
                    (uint64_t)current, needed_size);
                return result;
            }

            // Skip ahead efficiently:
            // - If we found some cave bytes but not enough, skip past them
            // - If we found no cave bytes, skip at least 1 byte
            current += (cave_bytes > 0 ? cave_bytes : 1);
        }
    }

    LOG("CodeInjectionManager: No suitable code cave found for size 0x%zX\n", needed_size);
    return result;
}

// Count consecutive padding bytes using architecture-aware detection
size_t CodeInjectionManager::count_cave_bytes(ea_t address, size_t max_size) const {
    const PaddingDetector& detector = get_padding_detector();

    // Get the first byte - must be a valid padding byte for this architecture
    uint8_t first_byte = get_byte(address);
    if (!detector.is_padding_byte(first_byte)) {
        return 0;  // Not a valid padding byte for this platform
    }

    // Count consecutive instances of the SAME padding byte
    // (Caves must be homogeneous - all same byte value)
    size_t count = 0;
    while (count < max_size && get_byte(address + count) == first_byte) {
        count++;
    }

    // If we found no padding, return early
    if (count == 0) {
        return 0;
    }

    // Additional validation: make sure this area is not in active code
    func_t* func = get_func(address);
    if (func && func->start_ea <= address && func->end_ea > address) {
        // Inside a function - need extra validation

        // Check if cave extends beyond function boundary
        if (address + count > func->end_ea) {
            return 0;  // Cave extends beyond function, not usable
        }

        // Check if there are any actual instructions in this range
        ea_t ea = address;
        while (ea < address + count) {
            if (is_code(get_flags(ea))) {
                return 0;  // Contains actual code, not a cave
            }
            ea = next_head(ea, address + count);
            if (ea == BADADDR) break;
        }
    }

    return count;
}

// Get disassembly for a range
std::string CodeInjectionManager::get_disassembly(ea_t start, ea_t end) const {
    std::stringstream ss;

    ea_t ea = start;
    while (ea < end && ea != BADADDR) {
        // Generate disassembly line
        qstring line;
        if (generate_disasm_line(&line, ea, GENDSM_REMOVE_TAGS) > 0) {
            ss << "0x" << std::hex << ea << ": " << line.c_str() << "\n";
        }

        // Move to next instruction
        ea = next_head(ea, end);
    }

    return ss.str();
}

// Get bytes from a range
std::vector<uint8_t> CodeInjectionManager::get_bytes_from_range(ea_t start, ea_t end) const {
    std::vector<uint8_t> result;

    size_t size = end - start;
    if (size == 0 || size > 0x100000) {  // Sanity check: max 1MB
        return result;
    }

    result.resize(size);
    if (get_bytes(result.data(), size, start) != size) {
        result.clear();
    }

    return result;
}

// Utility: Align up to boundary
size_t CodeInjectionManager::align_up(size_t size, size_t alignment) const {
    return (size + alignment - 1) & ~(alignment - 1);
}

// Utility: Format address as string
std::string CodeInjectionManager::format_address(ea_t address) const {
    std::stringstream ss;
    ss << "0x" << std::hex << std::uppercase << address;
    return ss.str();
}

// Utility: Generate platform-appropriate segment name for address
std::string CodeInjectionManager::generate_segment_name_for_address(ea_t address) const {
    std::stringstream addr_ss;
    addr_ss << std::hex << address;
    std::string addr_hex = addr_ss.str();
    std::string seg_name;

    filetype_t file_type = inf_get_filetype();

    if (file_type == f_PE) {
        // PE section names limited to 8 characters
        seg_name = ".i" + addr_hex.substr(addr_hex.length() >= 6 ? addr_hex.length() - 6 : 0);
        if (seg_name.length() > 8) {
            seg_name = seg_name.substr(0, 8);
        }
    } else if (file_type == f_ELF) {
        seg_name = ".inj_" + addr_hex;
    } else if (file_type == f_MACHO) {
        // Mach-O segment names limited to 16 characters
        seg_name = "__INJ_" + addr_hex.substr(addr_hex.length() >= 10 ? addr_hex.length() - 10 : 0);
        if (seg_name.length() > 16) {
            seg_name = seg_name.substr(0, 16);
        }
    } else {
        seg_name = ".inj";
    }

    return seg_name;
}

// Set no-go zones from orchestrator
void CodeInjectionManager::set_no_go_zones(const std::vector<orchestrator::NoGoZone>& zones) {
    no_go_zones_ = zones;
    LOG("CodeInjectionManager: Updated with %lu no-go zones\n", zones.size());

    // Create placeholder segments for the zones
    create_placeholder_segments_for_no_go_zones();
}

// Create placeholder segments for no-go zones so find_safe_address_after_segments skips them
void CodeInjectionManager::create_placeholder_segments_for_no_go_zones() {
    for (const orchestrator::NoGoZone& zone: no_go_zones_) {
        if (zone.type == orchestrator::NoGoZoneType::TEMP_SEGMENT) {
            // Check if segment already exists at this address
            segment_t* existing = getseg(zone.start_address);
            if (!existing) {
                size_t zone_size = zone.end_address - zone.start_address;

                // Generate platform-appropriate segment name for the no-go zone
                // Include address to ensure uniqueness even if same agent has multiple zones
                std::stringstream addr_ss;
                addr_ss << std::hex << zone.start_address;
                std::string addr_hex = addr_ss.str();
                std::string seg_name;

                // Detect binary type and create appropriate name
                filetype_t file_type = inf_get_filetype();
                if (file_type == f_PE) {
                    // PE section names are limited to 8 characters
                    // Use ".ng" + last 5 hex digits of address
                    if (addr_hex.length() > 5) addr_hex = addr_hex.substr(addr_hex.length() - 5);
                    seg_name = ".ng" + addr_hex;
                    if (seg_name.length() > 8) {
                        seg_name = seg_name.substr(0, 8);
                    }
                } else if (file_type == f_ELF) {
                    // ELF sections can have longer names
                    // Include both agent_id and address for uniqueness
                    seg_name = ".nogo_" + zone.agent_id + "_" + addr_hex;
                } else if (file_type == f_MACHO) {
                    // Mach-O segment names are limited to 16 characters
                    // Use "__NG_" + last 11 hex digits
                    if (addr_hex.length() > 11) addr_hex = addr_hex.substr(addr_hex.length() - 11);
                    seg_name = "__NG_" + addr_hex;
                    if (seg_name.length() > 16) {
                        seg_name = seg_name.substr(0, 16);
                    }
                } else {
                    // Default fallback
                    seg_name = ".nogo_" + addr_hex.substr(0, 8);
                }

                // Create empty content (zeros) for the placeholder
                std::vector<uint8_t> empty_bytes(zone_size, 0);

                // Use PatchManager to create placeholder segment (tracks for reverting)
                SegmentInjectionResult placeholder = patch_manager_->apply_segment_injection(
                    zone.start_address,
                    zone_size,
                    empty_bytes,
                    seg_name,
                    "No-go zone placeholder from agent " + zone.agent_id
                );

                if (placeholder.success) {
                    LOG("CodeInjectionManager: Created placeholder segment '%s' for no-go zone from %s\n",
                        seg_name.c_str(), zone.agent_id.c_str());
                } else {
                    LOG("CodeInjectionManager: Failed to create placeholder for no-go zone from %s: %s\n",
                        zone.agent_id.c_str(), placeholder.error_message.c_str());
                }
            }
        }
    }
}

// Check if an address range is in a no-go zone
bool CodeInjectionManager::is_in_no_go_zone(ea_t address, size_t size) const {
    ea_t end_address = address + size;

    for (const auto& zone : no_go_zones_) {
        // Check if the range overlaps with the no-go zone
        if (!(end_address <= zone.start_address || address >= zone.end_address)) {
            return true;  // Overlaps with a no-go zone
        }
    }
    return false;
}

} // namespace llm_re