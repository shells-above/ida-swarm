#include "merge_manager.h"
#include "../core/logger.h"
#include "../core/config.h"
#include "../patching/code_injection_manager.h"
#include <format>
#include <nalt.hpp>  // For get_input_file_path

namespace llm_re::orchestrator {

// Define the set of write tools
const std::set<std::string> MergeManager::WRITE_TOOLS = {
    "set_name",
    "set_comment",
    "set_function_prototype",
    "set_variable",
    "set_local_type",
    "patch_bytes",
    "patch_assembly",
    "allocate_code_workspace",
    "preview_code_injection",
    "finalize_code_injection",
    "start_semantic_patch",
    "compile_replacement",
    "preview_semantic_patch",
    "finalize_semantic_patch"
};

MergeManager::MergeManager(ToolCallTracker* tracker)
    : tool_tracker_(tracker) {

    // Initialize components for main database context
    executor_ = std::make_shared<ActionExecutor>();
    patch_manager_ = std::make_shared<PatchManager>();

    // Initialize the patch manager
    if (!patch_manager_->initialize()) {
        LOG_INFO("MergeManager: WARNING - Failed to initialize patch manager, patching operations will fail\n");
    }

    // Get the main binary path for dual patching
    std::string binary_path;
    char input_path[MAXSTR];
    if (get_input_file_path(input_path, sizeof(input_path)) > 0) {
        binary_path = input_path;
        LOG_INFO("MergeManager: Main binary path: %s\n", binary_path.c_str());
    } else {
        LOG_INFO("MergeManager: WARNING - Could not get binary path\n");
    }

    // Set up code injection if we have a binary path
    if (!binary_path.empty() && patch_manager_) {
        // Create code injection manager
        code_injection_manager_ = std::make_shared<CodeInjectionManager>(patch_manager_.get(), binary_path);
        if (code_injection_manager_->initialize()) {
            patch_manager_->set_code_injection_manager(code_injection_manager_.get());
            LOG_INFO("MergeManager: Code injection manager initialized\n");
        } else {
            LOG_INFO("MergeManager: WARNING - Failed to initialize code injection manager\n");
            code_injection_manager_ = nullptr;
        }
    }

    // Initialize semantic patch manager if dependencies are available
    if (patch_manager_ && code_injection_manager_) {
        semantic_patch_manager_ = std::make_shared<semantic::SemanticPatchManager>(
            patch_manager_.get(),
            code_injection_manager_.get()
        );
        LOG_INFO("MergeManager: Semantic patch manager initialized\n");
    }

    // Register the same tools that agents use
    tools::register_ida_tools(tool_registry_, executor_, nullptr, patch_manager_, code_injection_manager_, semantic_patch_manager_, Config::instance());
    
    LOG_INFO("MergeManager: Initialized with tool registry and patch manager\n");
}

MergeManager::~MergeManager() {
}

MergeResult MergeManager::merge_agent_changes(const std::string& agent_id) {
    MergeResult result;
    
    LOG_INFO("MergeManager: Starting merge for agent %s\n", agent_id.c_str());
    
    // Get all tool calls from the agent in chronological order
    std::vector<ToolCall> tool_calls = tool_tracker_->get_agent_tool_calls(agent_id);
    
    if (tool_calls.empty()) {
        result.success = true;
        result.error_message = "No tool calls to merge";
        LOG_INFO("MergeManager: No tool calls found for agent %s\n", agent_id.c_str());
        return result;
    }
    
    LOG_INFO("MergeManager: Found %zu tool calls from agent\n", tool_calls.size());
    
    // Process each tool call
    int total_write_ops = 0;
    for (const ToolCall& call: tool_calls) {
        // Skip non-write operations
        if (!is_write_tool(call.tool_name)) {
            continue;
        }
        
        total_write_ops++;
        LOG_INFO("MergeManager: Replaying %s (call #%d)\n", call.tool_name.c_str(), call.id);
        
        // Execute the tool call on the main database
        try {
            // Create a ToolUseContent from the ToolCall
            claude::messages::ToolUseContent tool_use(
                std::format("merge_{}", call.id),
                call.tool_name,
                call.parameters
            );
            
            // Execute the tool
            claude::messages::Message tool_result = tool_registry_.execute_tool_call(tool_use);
            
            // Extract the tool result to check success/failure
            claude::messages::ContentExtractor extractor;
            for (const std::unique_ptr<claude::messages::Content>& content: tool_result.contents()) {
                content->accept(extractor);
            }

            const std::vector<const claude::messages::ToolResultContent*>& tool_results = extractor.get_tool_results();
            if (!tool_results.empty()) {
                // Parse the result JSON
                try {
                    json result_json = json::parse(tool_results[0]->content);
                    
                    if (result_json.value("success", false)) {
                        result.changes_applied++;
                        
                        // Create a summary of what was applied
                        std::string message = result_json.value("message", "Applied successfully");
                        std::string summary = std::format("{}: {}", 
                            call.tool_name, 
                            message.substr(0, 100));  // Truncate long messages
                        result.applied_changes.push_back(summary);
                        
                        LOG_INFO("MergeManager: Successfully applied %s\n", call.tool_name.c_str());
                    } else {
                        result.changes_failed++;
                        std::string error = result_json.value("error", "Unknown error");
                        result.failed_changes.push_back(
                            std::format("{}: {}", call.tool_name, error)
                        );
                        LOG_INFO("MergeManager: Failed to apply %s: %s\n",
                            call.tool_name.c_str(), error.c_str());
                    }
                } catch (const json::exception&) {
                    result.changes_failed++;
                    result.failed_changes.push_back(
                        std::format("{}: Failed to parse result", call.tool_name)
                    );
                    LOG_INFO("MergeManager: Failed to parse result for %s\n", call.tool_name.c_str());
                }
            } else {
                result.changes_failed++;
                result.failed_changes.push_back(
                    std::format("{}: No tool result returned", call.tool_name)
                );
                LOG_INFO("MergeManager: No result returned for %s\n", call.tool_name.c_str());
            }
        } catch (const std::exception& e) {
            result.changes_failed++;
            result.failed_changes.push_back(
                std::format("{}: Exception - {}", call.tool_name, e.what())
            );
            LOG_INFO("MergeManager: Exception applying %s: %s\n", 
                call.tool_name.c_str(), e.what());
        }
    }
    
    // Set success flag
    result.success = (result.changes_failed == 0);
    
    // Log the merge operation
    log_merge(agent_id, result);
    
    LOG_INFO("MergeManager: Merge complete - Applied: %d, Failed: %d (from %d write ops)\n",
        result.changes_applied, result.changes_failed, total_write_ops);
    
    return result;
}

bool MergeManager::is_write_tool(const std::string& tool_name) {
    return WRITE_TOOLS.count(tool_name) > 0;
}

void MergeManager::log_merge(const std::string& agent_id, const MergeResult& result) {
    LOG_INFO("================== MERGE SUMMARY ==================\n");
    LOG_INFO("Agent: %s\n", agent_id.c_str());
    LOG_INFO("Status: %s\n", result.success ? "SUCCESS" : "PARTIAL");
    LOG_INFO("Changes Applied: %d\n", result.changes_applied);
    LOG_INFO("Changes Failed: %d\n", result.changes_failed);

    if (!result.applied_changes.empty()) {
        LOG_INFO("Applied Changes:\n");
        for (const std::string& change: result.applied_changes) {
            LOG_INFO("  - %s\n", change.c_str());
        }
    }
    
    if (!result.failed_changes.empty()) {
        LOG_INFO("Failed Changes:\n");
        for (const std::string& failure: result.failed_changes) {
            LOG_INFO("  - %s\n", failure.c_str());
        }
    }
    
    LOG_INFO("==================================================\n");
}

} // namespace llm_re::orchestrator