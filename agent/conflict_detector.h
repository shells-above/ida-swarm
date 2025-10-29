#pragma once

#include "../core/common.h"
#include "../orchestrator/tool_call_tracker.h"
#include <memory>
#include <string>

namespace llm_re::agent {

// Detects conflicts before agent makes changes
class ConflictDetector {
public:
    ConflictDetector(const std::string& agent_id, const std::string& binary_name);
    ~ConflictDetector();
    
    // Initialize the detector
    bool initialize();
    
    // Check for conflicts before a tool call
    std::vector<orchestrator::ToolConflict> check_conflict(const std::string& tool_name,
                                                            ea_t address,
                                                            const json& parameters = json::object());
    
    // Record a tool call after execution
    bool record_tool_call(const std::string& tool_name, ea_t address, const json& parameters);
    
    // Get conflict statistics
    int get_conflict_count() const { return conflict_count_; }
    
    // Check if address has been modified by another agent
    bool is_address_modified(ea_t address) const;
    
private:
    std::string agent_id_;
    std::unique_ptr<orchestrator::ToolCallTracker> tracker_;
    int conflict_count_ = 0;
    
    // Connect to shared tool call database
    bool connect_to_shared_db();
};

} // namespace llm_re::agent