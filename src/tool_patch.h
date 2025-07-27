//
// Created by user on 11/27/24.
//

#ifndef TOOL_PATCH_H
#define TOOL_PATCH_H

#include "tool_system.h"
#include "patch_manager.h"
#include "assembly_patcher.h"
#include "byte_patcher.h"
#include <memory>

namespace llm_re::tools {

// Tool for patching assembly instructions
class PatchAssemblyTool : public Tool {
    std::shared_ptr<PatchManager> patch_manager_;
    std::shared_ptr<AssemblyPatcher> assembly_patcher_;
    
public:
    PatchAssemblyTool(std::shared_ptr<BinaryMemory> mem, 
                      std::shared_ptr<ActionExecutor> exec,
                      std::shared_ptr<PatchManager> pm,
                      std::shared_ptr<AssemblyPatcher> ap)
        : Tool(mem, exec), patch_manager_(pm), assembly_patcher_(ap) {}
    
    std::string name() const override { return "patch_assembly"; }
    
    std::string description() const override {
        return "Patch assembly instructions at a specific address. Requires original assembly for verification.";
    }
    
    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "Target address to patch")
            .add_string("original_asm", "Original assembly instruction(s) for verification")
            .add_string("new_asm", "New assembly instruction(s) to write")
            .add_boolean("nop_remainder", "Auto-fill with NOPs if new instruction is smaller than original", false)
            .add_string("description", "Description of why this patch is being applied", false)
            .build();
    }
    
    ToolResult execute(const json& input) override;
};

// Tool for patching raw bytes
class PatchBytesTool : public Tool {
    std::shared_ptr<PatchManager> patch_manager_;
    std::shared_ptr<BytePatcher> byte_patcher_;
    
public:
    PatchBytesTool(std::shared_ptr<BinaryMemory> mem,
                   std::shared_ptr<ActionExecutor> exec,
                   std::shared_ptr<PatchManager> pm,
                   std::shared_ptr<BytePatcher> bp)
        : Tool(mem, exec), patch_manager_(pm), byte_patcher_(bp) {}
    
    std::string name() const override { return "patch_bytes"; }
    
    std::string description() const override {
        return "Patch raw bytes at a specific address. Requires original bytes for verification.";
    }
    
    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "Target address to patch")
            .add_string("original_bytes", "Original bytes in hex format (e.g., 'B8 05 00 00 00' or 'B805000000')")
            .add_string("new_bytes", "New bytes to write in hex format")
            .add_string("description", "Description of why this patch is being applied", false)
            .build();
    }
    
    ToolResult execute(const json& input) override;
};

// Tool for reverting patches
class RevertPatchTool : public Tool {
    std::shared_ptr<PatchManager> patch_manager_;
    
public:
    RevertPatchTool(std::shared_ptr<BinaryMemory> mem,
                    std::shared_ptr<ActionExecutor> exec,
                    std::shared_ptr<PatchManager> pm)
        : Tool(mem, exec), patch_manager_(pm) {}
    
    std::string name() const override { return "revert_patch"; }
    
    std::string description() const override {
        return "Revert a previously applied patch at a specific address or range.";
    }
    
    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("address", "Address of patch to revert", false)
            .add_integer("start_address", "Start address of range to revert", false)
            .add_integer("end_address", "End address of range to revert", false)
            .add_boolean("revert_all", "Revert all patches", false)
            .build();
    }
    
    ToolResult execute(const json& input) override;
};

// Tool for listing patches
class ListPatchesTool : public Tool {
    std::shared_ptr<PatchManager> patch_manager_;
    
public:
    ListPatchesTool(std::shared_ptr<BinaryMemory> mem,
                    std::shared_ptr<ActionExecutor> exec,
                    std::shared_ptr<PatchManager> pm)
        : Tool(mem, exec), patch_manager_(pm) {}
    
    std::string name() const override { return "list_patches"; }
    
    std::string description() const override {
        return "List all applied patches or patches in a specific range.";
    }
    
    json parameters_schema() const override {
        return ParameterBuilder()
            .add_integer("start_address", "Start address of range to list", false)
            .add_integer("end_address", "End address of range to list", false)
            .build();
    }
    
    ToolResult execute(const json& input) override;
};

// Patch tools manager - handles initialization and registration
class PatchToolsManager {
    std::shared_ptr<PatchManager> patch_manager_;
    std::shared_ptr<AssemblyPatcher> assembly_patcher_;
    std::shared_ptr<BytePatcher> byte_patcher_;
    
public:
    PatchToolsManager();
    ~PatchToolsManager();
    
    // Initialize all patch tools
    bool initialize();
    
    // Register tools with the tool registry
    void register_tools(ToolRegistry* tool_registry,
                       std::shared_ptr<BinaryMemory> memory,
                       std::shared_ptr<ActionExecutor> executor);
    
    // Get managers
    std::shared_ptr<PatchManager> get_patch_manager() { return patch_manager_; }
    std::shared_ptr<AssemblyPatcher> get_assembly_patcher() { return assembly_patcher_; }
    std::shared_ptr<BytePatcher> get_byte_patcher() { return byte_patcher_; }
};

} // namespace llm_re::tools

#endif // TOOL_PATCH_H