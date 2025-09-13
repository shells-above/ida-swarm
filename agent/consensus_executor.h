#pragma once

#include "agent.h"
#include "../orchestrator/tool_call_tracker.h"
#include <map>
#include <string>

namespace llm_re::agent {

// Import ToolConflict from orchestrator namespace
using orchestrator::ToolConflict;

// Special agent for parsing consensus and extracting tool calls
// This agent doesn't actually execute tools - it intercepts them
class ConsensusExecutor : public Agent {
public:
    ConsensusExecutor(const Config& config);
    virtual ~ConsensusExecutor() = default;
    
    // Execute consensus with context about the original conflict
    // Returns the tool call that should be executed
    json execute_consensus(const std::map<std::string, std::string>& agreements,
                          const ToolConflict& original_conflict);
    
protected:
    // Override to intercept tool calls instead of executing them
    std::vector<claude::messages::Message> process_tool_calls(
        const claude::messages::Message& msg, int iteration) override;
    
private:
    json captured_tool_call_;  // Store the intercepted tool call
    bool tool_intercepted_ = false;
    
    // Format the prompt with full conflict context
    std::string format_consensus_prompt(const std::map<std::string, std::string>& agreements,
                                       const ToolConflict& conflict);
};

} // namespace llm_re::agent