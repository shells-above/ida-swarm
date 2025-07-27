//
// Created by user on 11/27/24.
//

#include "tool_patch.h"
#include "ida_utils.h"
#include <ida.hpp>
#include <sstream>
#include <iomanip>

namespace llm_re::tools {

// PatchAssemblyTool implementation
ToolResult PatchAssemblyTool::execute(const json& input) {
    if (!patch_manager_ || !assembly_patcher_) {
        return ToolResult::failure("Patch tools not initialized");
    }
    
    try {
        // Parse parameters
        ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
        std::string original_asm = input.at("original_asm");
        std::string new_asm = input.at("new_asm");
        bool nop_remainder = input.value("nop_remainder", true);
        std::string description = input.value("description", "Assembly patch");
        
        // Execute within IDA's thread-safe context
        auto apply_result = IDAUtils::execute_sync_wrapper([&]() -> std::pair<bool, std::string> {
            // First, verify the original assembly matches
            std::string current_asm = assembly_patcher_->disassemble_at(address);
            if (current_asm.empty()) {
                return {false, "Failed to disassemble at address"};
            }
            
            // Get size of original instruction(s)
            insn_t insn;
            size_t original_size = 0;
            ea_t current_addr = address;
            
            // Calculate total size of original instructions
            // For now, just get size of single instruction at address
            // TODO: handle multiple instructions
            if (decode_insn(&insn, current_addr) == 0) {
                return {false, "Failed to decode instruction"};
            }
            original_size = insn.size;
            
            // Check if new assembly will fit
            auto new_size = assembly_patcher_->get_assembled_size(new_asm, address);
            if (!new_size) {
                return {false, "Failed to assemble new instructions"};
            }
            
            bool success = false;
            std::string error_msg;
            
            if (nop_remainder && *new_size < original_size) {
                // Use NOP padding
                success = assembly_patcher_->apply_patch_with_nop(
                    address, new_asm, original_size, description);
                if (!success) error_msg = "Failed to apply patch with NOP padding";
            } else if (*new_size > original_size) {
                error_msg = "New instruction(s) too large for space. Original: " + 
                           std::to_string(original_size) + " bytes, New: " + 
                           std::to_string(*new_size) + " bytes";
                return {false, error_msg};
            } else {
                // Direct patch
                success = assembly_patcher_->apply_patch(
                    address, new_asm, description, true, original_asm);
                if (!success) error_msg = "Failed to apply patch";
            }
            
            return {success, error_msg};
        }, MFF_WRITE);
        
        if (apply_result.first) {
            json data;
            data["address"] = HexAddress(address);
            data["original_asm"] = original_asm;
            data["new_asm"] = new_asm;
            
            // Get size info in sync wrapper
            auto size_info = IDAUtils::execute_sync_wrapper([&]() -> std::pair<size_t, size_t> {
                auto new_size = assembly_patcher_->get_assembled_size(new_asm, address);
                insn_t insn;
                decode_insn(&insn, address);
                return {new_size.value_or(0), insn.size};
            });
            
            data["bytes_patched"] = size_info.first;
            if (nop_remainder && size_info.first < size_info.second) {
                data["nops_added"] = size_info.second - size_info.first;
            }
            
            return ToolResult::success(data);
        } else {
            return ToolResult::failure(apply_result.second);
        }
        
    } catch (const std::exception& e) {
        return ToolResult::failure(std::string("Exception: ") + e.what());
    }
}

// PatchBytesTool implementation
ToolResult PatchBytesTool::execute(const json& input) {
    if (!patch_manager_ || !byte_patcher_) {
        return ToolResult::failure("Patch tools not initialized");
    }
    
    try {
        // Parse parameters
        ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
        std::string original_hex = input.at("original_bytes");
        std::string new_hex = input.at("new_bytes");
        std::string description = input.value("description", "Byte patch");
        
        // Apply patch in thread-safe context
        auto result = IDAUtils::execute_sync_wrapper([&]() -> BytePatchResult {
            return byte_patcher_->apply_patch_hex(
                address, new_hex, description, true, original_hex);
        }, MFF_WRITE);
        
        if (result.success) {
            json data;
            data["address"] = HexAddress(address);
            data["original_bytes"] = original_hex;
            data["new_bytes"] = new_hex;
            data["bytes_patched"] = result.bytes_patched;
            return ToolResult::success(data);
        } else {
            return ToolResult::failure(result.error_message);
        }
        
    } catch (const std::exception& e) {
        return ToolResult::failure(std::string("Exception: ") + e.what());
    }
}

// RevertPatchTool implementation
ToolResult RevertPatchTool::execute(const json& input) {
    if (!patch_manager_) {
        return ToolResult::failure("Patch manager not initialized");
    }
    
    try {
        // Execute in thread-safe context
        auto result = IDAUtils::execute_sync_wrapper([&]() -> std::pair<bool, json> {
            json data;
            bool success = false;
            
            if (input.value("revert_all", false)) {
                // Revert all patches
                success = patch_manager_->revert_all();
                data["reverted"] = "all";
            } else if (input.contains("address")) {
                // Revert single patch
                ea_t address = ActionExecutor::parse_single_address_value(input.at("address"));
                success = patch_manager_->revert_patch(address);
                data["address"] = HexAddress(address);
            } else if (input.contains("start_address") && input.contains("end_address")) {
                // Revert range
                ea_t start = ActionExecutor::parse_single_address_value(input.at("start_address"));
                ea_t end = ActionExecutor::parse_single_address_value(input.at("end_address"));
                success = patch_manager_->revert_range(start, end);
                data["start_address"] = HexAddress(start);
                data["end_address"] = HexAddress(end);
            } else {
                return {false, json{{"error", "Must specify address, range, or revert_all"}}};
            }
            
            return {success, data};
        }, MFF_WRITE);
        
        if (result.first) {
            return ToolResult::success(result.second);
        } else {
            if (result.second.contains("error")) {
                return ToolResult::failure(result.second["error"]);
            }
            return ToolResult::failure("No patches found to revert");
        }
        
    } catch (const std::exception& e) {
        return ToolResult::failure(std::string("Exception: ") + e.what());
    }
}

// ListPatchesTool implementation
ToolResult ListPatchesTool::execute(const json& input) {
    if (!patch_manager_) {
        return ToolResult::failure("Patch manager not initialized");
    }
    
    try {
        // Execute in thread-safe context
        auto result = IDAUtils::execute_sync_wrapper([&]() -> json {
            std::vector<PatchEntry> patches;
            
            if (input.contains("start_address") && input.contains("end_address")) {
                // List patches in range
                ea_t start = ActionExecutor::parse_single_address_value(input.at("start_address"));
                ea_t end = ActionExecutor::parse_single_address_value(input.at("end_address"));
                patches = patch_manager_->get_patches_in_range(start, end);
            } else {
                // List all patches
                patches = patch_manager_->get_all_patches();
            }
            
            json data;
            json patches_json = json::array();
            
            for (const auto& patch : patches) {
                json patch_json;
                patch_json["address"] = HexAddress(patch.address);
                patch_json["original_bytes"] = BytePatcher::bytes_to_hex_string(patch.original_bytes);
                patch_json["patched_bytes"] = BytePatcher::bytes_to_hex_string(patch.patched_bytes);
                patch_json["description"] = patch.description;
                patch_json["timestamp"] = std::chrono::system_clock::to_time_t(patch.timestamp);
                patch_json["is_assembly_patch"] = patch.is_assembly_patch;
                
                if (patch.is_assembly_patch) {
                    patch_json["original_asm"] = patch.original_asm;
                    patch_json["patched_asm"] = patch.patched_asm;
                }
                
                patches_json.push_back(patch_json);
            }
            
            data["patches"] = patches_json;
            data["count"] = patches.size();
            
            // Add statistics
            auto stats = patch_manager_->get_statistics();
            data["statistics"]["total_patches"] = stats.total_patches;
            data["statistics"]["assembly_patches"] = stats.assembly_patches;
            data["statistics"]["byte_patches"] = stats.byte_patches;
            data["statistics"]["total_bytes_patched"] = stats.total_bytes_patched;
            
            return data;
        });
        
        return ToolResult::success(result);
        
    } catch (const std::exception& e) {
        return ToolResult::failure(std::string("Exception: ") + e.what());
    }
}

// PatchToolsManager implementation
PatchToolsManager::PatchToolsManager() {}

PatchToolsManager::~PatchToolsManager() = default;

bool PatchToolsManager::initialize() {
    // Create managers - initialization happens in thread-safe context
    bool init_success = IDAUtils::execute_sync_wrapper([&]() -> bool {
        patch_manager_ = std::make_shared<PatchManager>();
        if (!patch_manager_->initialize()) {
            return false;
        }
        
        assembly_patcher_ = std::make_shared<AssemblyPatcher>(patch_manager_.get());
        if (!assembly_patcher_->initialize()) {
            return false;
        }
        
        byte_patcher_ = std::make_shared<BytePatcher>(patch_manager_.get());
        
        return true;
    }, MFF_WRITE);
    
    return init_success;
}

void PatchToolsManager::register_tools(ToolRegistry* tool_registry,
                                     std::shared_ptr<BinaryMemory> memory,
                                     std::shared_ptr<ActionExecutor> executor) {
    if (!tool_registry) return;
    
    // Register patch tools
    tool_registry->register_tool(std::make_unique<PatchAssemblyTool>(
        memory, executor, patch_manager_, assembly_patcher_));
    
    tool_registry->register_tool(std::make_unique<PatchBytesTool>(
        memory, executor, patch_manager_, byte_patcher_));
    
    tool_registry->register_tool(std::make_unique<RevertPatchTool>(
        memory, executor, patch_manager_));
    
    tool_registry->register_tool(std::make_unique<ListPatchesTool>(
        memory, executor, patch_manager_));
}

} // namespace llm_re::tools