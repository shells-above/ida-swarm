#include "code_injection_manager.h"
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
    if (agent_binary_path_.empty()) {
        msg("CodeInjectionManager: Warning - no binary path provided\n");
    } else {
        msg("CodeInjectionManager: Using binary path: %s\n", agent_binary_path_.c_str());
    }
    return true;
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

    msg("CodeInjectionManager: Allocated temporary workspace at 0x%llX, size 0x%zX\n",
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

    msg("CodeInjectionManager: Preview successful for range 0x%llX-0x%llX (%lu bytes)\n",
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

        // Apply to actual binary file
        if (!apply_to_binary_file(cave.file_offset, preview.final_bytes)) {
            result.error_message = "Failed to apply code to binary file at code cave";
            return result;
        }

        msg("CodeInjectionManager: Using code cave at 0x%llX\n", (uint64_t)cave.address);
    } else {
        // Need to create new segment
        NewSegment new_seg = create_permanent_segment(needed_size);
        if (new_seg.address == BADADDR) {
            result.error_message = "Failed to create new segment for code injection";
            return result;
        }

        final_address = new_seg.address;
        method = "new_segment";

        // Use LIEF to add segment to actual binary
        if (!add_segment_with_lief(new_seg, preview.final_bytes)) {
            result.error_message = "Failed to add new segment to binary with LIEF";
            return result;
        }

        msg("CodeInjectionManager: Created new segment at 0x%llX\n", (uint64_t)new_seg.address);
    }

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

    msg("CodeInjectionManager: Code relocated from 0x%llX to 0x%llX using %s\n",
        (uint64_t)start_address, (uint64_t)final_address, method.c_str());
    msg("CodeInjectionManager: IMPORTANT - Review all patches for references to old address\n");

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
    segment_t seg;
    seg.start_ea = address;
    seg.end_ea = address + size;
    seg.perm = SEGPERM_EXEC | SEGPERM_READ | SEGPERM_WRITE;
    seg.type = SEG_CODE;

    // Detect binary bitness and set segment appropriately
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
            seg.bitness = 1;  // Default to 32-bit if unknown
            msg("CodeInjectionManager: Warning - Unknown bitness %d, defaulting to 32-bit\n", app_bitness);
            break;
    }

    // Use add_segm_ex for more control
    if (!add_segm_ex(&seg, name.c_str(), "CODE", ADDSEG_OR_DIE)) {
        msg("CodeInjectionManager: Failed to create segment %s at 0x%llX\n",
            name.c_str(), (uint64_t)address);
        return false;
    }

    msg("CodeInjectionManager: Created temporary segment %s at 0x%llX-0x%llX\n",
        name.c_str(), (uint64_t)address, (uint64_t)(address + size));

    return true;
}

// Delete a temporary segment
bool CodeInjectionManager::delete_temp_segment(ea_t address) {
    // Find the segment at this address
    segment_t* seg = getseg(address);
    if (!seg) {
        msg("CodeInjectionManager: No segment found at 0x%llX\n", (uint64_t)address);
        return false;
    }

    // Delete the segment
    if (!del_segm(address, SEGMOD_KILL)) {
        msg("CodeInjectionManager: Failed to delete segment at 0x%llX\n", (uint64_t)address);
        return false;
    }

    msg("CodeInjectionManager: Deleted temporary segment at 0x%llX\n", (uint64_t)address);
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

        // Scan the segment for caves (consecutive 0x00 or 0x90 bytes)
        ea_t current = seg->start_ea;
        while (current < seg->end_ea) {
            // Check how many consecutive cave bytes we have at this position
            size_t cave_bytes = count_cave_bytes(current, needed_size);

            if (cave_bytes >= needed_size) {
                // Check if this cave is in a no-go zone
                if (is_in_no_go_zone(current, needed_size)) {
                    msg("CodeInjectionManager: Skipping code cave at 0x%llX - in no-go zone\n",
                        (uint64_t)current);
                    current += needed_size;  // Skip past this cave
                    continue;
                }

                // Found a suitable cave!
                result.found = true;
                result.address = current;
                result.size = needed_size;
                result.file_offset = get_fileregion_offset(current);

                msg("CodeInjectionManager: Found code cave at 0x%llX, size 0x%zX\n",
                    (uint64_t)current, needed_size);
                return result;
            }

            // Skip ahead efficiently:
            // - If we found some cave bytes but not enough, skip past them
            // - If we found no cave bytes, skip at least 1 byte
            current += (cave_bytes > 0 ? cave_bytes : 1);
        }
    }

    msg("CodeInjectionManager: No suitable code cave found for size 0x%zX\n", needed_size);
    return result;
}

// Count consecutive padding bytes (0x00 or 0xFF) starting at address
size_t CodeInjectionManager::count_cave_bytes(ea_t address, size_t max_size) const {
    size_t count = 0;

    // Look for common padding bytes (zeros or 0xFF)
    while (count < max_size) {
        uint8_t byte = get_byte(address + count);
        if (byte == 0x00 || byte == 0xFF) {
            count++;
        } else {
            break;  // Found a non-padding byte
        }
    }

    // Additional validation: make sure this area is not in active code
    if (count > 0) {
        func_t* func = get_func(address);
        if (func && func->start_ea <= address && func->end_ea > address) {
            // Check if this is padding at the end of a function
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
    }

    return count;
}

// Apply bytes to binary file
bool CodeInjectionManager::apply_to_binary_file(uint32_t file_offset, const std::vector<uint8_t>& bytes) {
    if (agent_binary_path_.empty()) {
        msg("CodeInjectionManager: No binary path available\n");
        return false;
    }

    std::fstream file(agent_binary_path_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        msg("CodeInjectionManager: Failed to open binary file: %s\n", agent_binary_path_.c_str());
        return false;
    }

    file.seekp(file_offset);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    file.close();

    msg("CodeInjectionManager: Applied %lu bytes to binary at offset 0x%X\n",
        bytes.size(), file_offset);

    return true;
}

// Create a permanent segment (when no code cave is found)
CodeInjectionManager::NewSegment CodeInjectionManager::create_permanent_segment(size_t needed_size) {
    NewSegment result = {BADADDR, 0, ""};

    // Find address for new segment
    ea_t new_address = find_safe_address_after_segments();
    if (new_address == BADADDR) {
        return result;
    }

    // Align size to page boundary
    size_t aligned_size = align_up(needed_size, 0x1000);

    // Generate platform-appropriate segment name based on address
    std::stringstream name_ss;
    name_ss << std::hex << new_address;
    std::string seg_name;

    // Detect binary type and create appropriate name
    filetype_t file_type = inf_get_filetype();
    if (file_type == f_PE) {
        // PE section names are limited to 8 characters
        seg_name = ".i" + name_ss.str().substr(name_ss.str().length() >= 6 ? name_ss.str().length() - 6 : 0);
        if (seg_name.length() > 8) {
            seg_name = seg_name.substr(0, 8);
        }
    } else if (file_type == f_ELF) {
        // ELF sections can have longer names
        seg_name = ".inj_" + name_ss.str();
    } else if (file_type == f_MACHO) {
        // Mach-O segment names are limited to 16 characters
        seg_name = "__INJ_" + name_ss.str().substr(name_ss.str().length() >= 10 ? name_ss.str().length() - 10 : 0);
        if (seg_name.length() > 16) {
            seg_name = seg_name.substr(0, 16);
        }
    } else {
        // Default fallback
        seg_name = ".inj";
    }

    // Create segment in IDA database with the same name
    if (!create_temp_segment(new_address, aligned_size, seg_name)) {
        return result;
    }

    result.address = new_address;
    result.size = aligned_size;
    result.name = seg_name;

    return result;
}

// Add segment to binary using LIEF
bool CodeInjectionManager::add_segment_with_lief(const NewSegment& segment, const std::vector<uint8_t>& bytes) {
    msg("CodeInjectionManager: Adding new segment '%s' to binary using LIEF at address 0x%llX, size 0x%llX\n",
        segment.name.c_str(), (uint64_t)segment.address, (uint64_t)segment.size);

    try {
        // Parse the binary with LIEF
        std::unique_ptr<LIEF::Binary> binary = LIEF::Parser::parse(agent_binary_path_);
        if (!binary) {
            msg("CodeInjectionManager: Failed to parse binary with LIEF: %s\n", agent_binary_path_.c_str());
            return false;
        }

        // Detect binary type using IDA's information
        filetype_t file_type = inf_get_filetype();

        // Use the address, size, and name from the segment parameter
        uint64_t target_address = segment.address;
        size_t target_size = segment.size;
        std::string segment_name = segment.name;

        // Handle different binary formats
        if (file_type == f_PE) {
            // Handle PE (Windows) binaries
            auto* pe_binary = dynamic_cast<LIEF::PE::Binary*>(binary.get());
            if (!pe_binary) {
                msg("CodeInjectionManager: Failed to cast to PE binary\n");
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
            new_section.content(bytes);

            // Add the section to the binary
            LIEF::PE::Section* added_section = pe_binary->add_section(new_section);

            msg("CodeInjectionManager: Added PE section at RVA 0x%X (VA: 0x%llX)\n",
                rva, target_address);

        } else if (file_type == f_ELF) {
            // Handle ELF (Linux) binaries
            auto* elf_binary = dynamic_cast<LIEF::ELF::Binary*>(binary.get());
            if (!elf_binary) {
                msg("CodeInjectionManager: Failed to cast to ELF binary\n");
                return false;
            }

            // Create a new segment (PT_LOAD)
            LIEF::ELF::Segment new_segment;
            new_segment.type(LIEF::ELF::SEGMENT_TYPES::PT_LOAD);
            new_segment.flags(
                LIEF::ELF::ELF_SEGMENT_FLAGS::PF_R |
                LIEF::ELF::ELF_SEGMENT_FLAGS::PF_X
            );
            new_segment.content(bytes);
            new_segment.alignment(0x1000);  // Page alignment

            // Use the address from the segment parameter
            new_segment.virtual_address(target_address);
            new_segment.virtual_size(target_size);
            new_segment.physical_address(target_address);  // Usually same as virtual
            new_segment.physical_size(bytes.size());

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
            new_section.size(bytes.size());
            new_section.content(bytes);

            elf_binary->add(new_section);

            msg("CodeInjectionManager: Added ELF segment at 0x%llX\n", target_address);

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
                msg("CodeInjectionManager: Failed to cast to Mach-O binary\n");
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

            // Use the address from the segment parameter
            new_segment.virtual_address(target_address);
            new_segment.virtual_size(target_size);
            new_segment.file_size(bytes.size());

            // Create a section within the segment
            LIEF::MachO::Section new_section;
            new_section.segment_name(segment_name);
            new_section.name("__text");
            new_section.address(target_address);
            new_section.size(bytes.size());
            new_section.content(bytes);
            new_section.type(LIEF::MachO::MACHO_SECTION_TYPES::S_REGULAR);
            new_section.flags(
                static_cast<uint32_t>(LIEF::MachO::MACHO_SECTION_FLAGS::S_ATTR_SOME_INSTRUCTIONS) |
                static_cast<uint32_t>(LIEF::MachO::MACHO_SECTION_FLAGS::S_ATTR_PURE_INSTRUCTIONS)
            );

            // Add section to segment
            new_segment.add_section(new_section);

            // Add segment to binary
            macho_binary->add(new_segment);

            msg("CodeInjectionManager: Added Mach-O segment at 0x%llX\n", target_address);

        } else {
            msg("CodeInjectionManager: Unsupported file type: %d\n", file_type);
            return false;
        }

        // Write the modified binary back
        // Create a backup first
        std::string backup_path = agent_binary_path_ + ".backup";
        std::filesystem::copy_file(agent_binary_path_, backup_path,
                                   std::filesystem::copy_options::overwrite_existing);

        // Build and write the modified binary
        // LIEF uses format-specific builders or direct write
        if (file_type == f_PE) {
            auto* pe_binary = dynamic_cast<LIEF::PE::Binary*>(binary.get());
            if (pe_binary) {
                LIEF::PE::Builder builder(*pe_binary);
                builder.build();
                builder.write(agent_binary_path_);
            }
        } else if (file_type == f_ELF) {
            auto* elf_binary = dynamic_cast<LIEF::ELF::Binary*>(binary.get());
            if (elf_binary) {
                LIEF::ELF::Builder builder(*elf_binary);
                builder.build();
                builder.write(agent_binary_path_);
            }
        } else if (file_type == f_MACHO) {
            // MachO uses direct write, no builder needed
            binary->write(agent_binary_path_);
        }

        msg("CodeInjectionManager: Successfully wrote modified binary to %s\n",
            agent_binary_path_.c_str());
        msg("CodeInjectionManager: Original binary backed up to %s\n",
            backup_path.c_str());

        return true;

    } catch (const std::exception& e) {
        msg("CodeInjectionManager: Exception: %s\n", e.what());
        return false;
    }
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

// Set no-go zones from orchestrator
void CodeInjectionManager::set_no_go_zones(const std::vector<orchestrator::NoGoZone>& zones) {
    no_go_zones_ = zones;
    msg("CodeInjectionManager: Updated with %lu no-go zones\n", zones.size());

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

                // Create a placeholder segment in IDA with the generated name
                if (create_temp_segment(zone.start_address, zone_size, seg_name)) {
                    msg("CodeInjectionManager: Created IDA placeholder segment '%s' for no-go zone from %s\n",
                        seg_name.c_str(), zone.agent_id.c_str());

                    // Also add the segment to the actual binary file
                    // This ensures binary stays synchronized with IDA's view
                    NewSegment binary_segment;
                    binary_segment.address = zone.start_address;
                    binary_segment.size = zone_size;
                    binary_segment.name = seg_name;  // Use the same name for consistency

                    // Create empty content (zeros) for the placeholder
                    std::vector<uint8_t> empty_bytes(zone_size, 0);

                    if (add_segment_with_lief(binary_segment, empty_bytes)) {
                        msg("CodeInjectionManager: Added no-go zone to binary for agent %s at 0x%llX\n",
                            zone.agent_id.c_str(), (uint64_t)zone.start_address);
                    } else {
                        msg("CodeInjectionManager: WARNING - Failed to add no-go zone to binary for agent %s\n",
                            zone.agent_id.c_str());
                    }
                } else {
                    msg("CodeInjectionManager: Failed to create IDA segment for no-go zone from %s\n",
                        zone.agent_id.c_str());
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