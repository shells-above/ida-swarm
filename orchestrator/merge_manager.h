#pragma once

#include "../core/common.h"
#include "../analysis/actions.h"
#include "../patching/patch_manager.h"
#include "../patching/code_injection_manager.h"
#include "../semantic_patch/semantic_patch_manager.h"
#include "../sdk/tools/registry.h"
#include "../agent/tool_system.h"
#include "tool_call_tracker.h"
#include <vector>
#include <set>

namespace llm_re::orchestrator {

// Result of a merge operation
struct MergeResult {
    bool success = false;
    int changes_applied = 0;
    int changes_failed = 0;
    std::string error_message;
    std::vector<std::string> applied_changes;
    std::vector<std::string> failed_changes;
};

// Manages merging agent changes back to main database
class MergeManager {
public:
    explicit MergeManager(ToolCallTracker* tracker);
    ~MergeManager();
    
    // Merge all changes from an agent's database
    MergeResult merge_agent_changes(const std::string& agent_id);
    
private:
    // Core components for tool execution on main database
    std::shared_ptr<ActionExecutor> executor_;
    std::shared_ptr<PatchManager> patch_manager_;
    std::shared_ptr<CodeInjectionManager> code_injection_manager_;
    std::shared_ptr<semantic::SemanticPatchManager> semantic_patch_manager_;
    claude::tools::ToolRegistry tool_registry_;
    ToolCallTracker* tool_tracker_;
    
    // Set of write tools that modify the database
    static const std::set<std::string> WRITE_TOOLS;
    
    // Check if a tool is a write operation
    static bool is_write_tool(const std::string& tool_name);
    
    // Log merge operation
    void log_merge(const std::string& agent_id, const MergeResult& result);
};

} // namespace llm_re::orchestrator