#include "conflict_detector.h"
#include "../core/logger.h"

namespace llm_re::agent {

ConflictDetector::ConflictDetector(const std::string& agent_id, const std::string& binary_name)
    : agent_id_(agent_id) {
    tracker_ = std::make_unique<orchestrator::ToolCallTracker>(binary_name);
}

ConflictDetector::~ConflictDetector() {
}

bool ConflictDetector::initialize() {
    // Initialize the shared tracker
    if (!tracker_->initialize()) {
        LOG_INFO("ConflictDetector: Failed to initialize tracker\n");
        return false;
    }
    
    LOG_INFO("ConflictDetector: Initialized for agent %s\n", agent_id_.c_str());
    return true;
}

std::vector<orchestrator::ToolConflict> ConflictDetector::check_conflict(const std::string& tool_name,
                                                                         ea_t address,
                                                                         const json& parameters) {
    // Check for conflicts with other agents' changes
    std::vector<orchestrator::ToolConflict> conflicts = tracker_->check_for_conflicts(agent_id_, tool_name, address, parameters);

    if (!conflicts.empty()) {
        conflict_count_ += conflicts.size();
        LOG_INFO("ConflictDetector: Found %zu conflicts for %s at 0x%llx\n",
            conflicts.size(), tool_name.c_str(), address);
    }

    return conflicts;
}

bool ConflictDetector::record_tool_call(const std::string& tool_name, ea_t address, const json& parameters) {
    // Record this agent's tool call
    bool success = tracker_->record_tool_call(agent_id_, tool_name, address, parameters);
    
    if (success) {
        LOG_INFO("ConflictDetector: Recorded %s at 0x%llx\n", tool_name.c_str(), address);
    } else {
        LOG_INFO("ConflictDetector: Failed to record %s at 0x%llx\n", tool_name.c_str(), address);
    }
    
    return success;
}

bool ConflictDetector::is_address_modified(ea_t address) const {
    // Check if any agent has modified this address
    std::vector<orchestrator::ToolCall> calls = tracker_->get_address_tool_calls(address);
    
    for (const orchestrator::ToolCall& call: calls) {
        if (call.is_write_operation && call.agent_id != agent_id_) {
            return true;
        }
    }
    
    return false;
}

} // namespace llm_re::agent