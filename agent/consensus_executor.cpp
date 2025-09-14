#include "consensus_executor.h"
#include <sstream>
#include <iomanip>

namespace llm_re::agent {

ConsensusExecutor::ConsensusExecutor(const Config& config)
    : Agent(config, "consensus_executor") {
    // The base Agent constructor starts a worker thread, but we don't need it
    // for consensus execution since we do synchronous execution
    // We'll just let it run idle - trying to stop it causes access to private members
}

json ConsensusExecutor::execute_consensus(const std::map<std::string, std::string>& agreements,
                                         const ToolConflict& original_conflict) {
    // Reset state
    captured_tool_call_ = json();
    tool_intercepted_ = false;
    
    // Build conversation with full conflict context
    std::string prompt = format_consensus_prompt(agreements, original_conflict);
    
    // Build request using RequestBuilder with extended thinking
    claude::ChatRequestBuilder builder;
    claude::ChatRequest request = builder
        .with_model(claude::Model::Sonnet4)
        .with_system_prompt("You are a consensus executor. Your job is to interpret agreements "
                           "between agents and execute the appropriate tool call based on their consensus. "
                           "You will be given the original conflicting tool calls and the agreements reached. "
                           "Execute the tool with the parameters that match the consensus.")
        .with_tools(tool_registry_)
        .with_max_tokens(8192)
        .with_temperature(0.0)  // Deterministic
        .enable_thinking(true)
        .with_max_thinking_tokens(4096)  // Extended thinking budget
        .add_message(claude::messages::Message::user_text(prompt))
        .build();
    
    try {
        // Get response - Claude will naturally call the appropriate tool
        claude::ChatResponse response = api_client_.send_request(request);
        
        // Process will intercept the tool call
        if (response.stop_reason == claude::StopReason::ToolUse) {
            process_tool_calls(response.message, 0);
        } else {
            msg("ConsensusExecutor: No tool use in response, stop reason: %d\n", 
                static_cast<int>(response.stop_reason));
        }
    } catch (const std::exception& e) {
        msg("ConsensusExecutor: Exception during consensus execution: %s\n", e.what());
    }
    
    // If we didn't capture a tool call, create a fallback
    if (captured_tool_call_.is_null() || !tool_intercepted_) {
        msg("ConsensusExecutor: No tool captured, creating fallback\n");
        
        // Fallback: construct tool call from original conflict
        // The orchestrator will have to use the LLM fallback to instruct agents
        captured_tool_call_ = {
            {"tool_name", original_conflict.first_call.tool_name},
            {"parameters", {
                {"address", std::format("0x{:x}", original_conflict.first_call.address)},
                {"__needs_manual", true},
                {"__fallback_reason", "consensus_executor_failed"}
            }}
        };
    } else {
        // Validate captured tool matches expected
        if (captured_tool_call_.contains("tool_name") && 
            captured_tool_call_["tool_name"] != original_conflict.first_call.tool_name) {
            msg("ConsensusExecutor: WARNING - Different tool selected: %s vs expected %s\n",
                captured_tool_call_["tool_name"].get<std::string>().c_str(),
                original_conflict.first_call.tool_name.c_str());
        }
    }
    
    return captured_tool_call_;
}

std::vector<claude::messages::Message> ConsensusExecutor::process_tool_calls(
    const claude::messages::Message& msg, int iteration) {
    
    auto tool_uses = claude::messages::ContentExtractor::extract_tool_uses(msg);
    
    if (!tool_uses.empty()) {
        // Capture the first tool call instead of executing
        const auto* tool_use = tool_uses[0];
        
        captured_tool_call_ = {
            {"tool_name", tool_use->name},
            {"parameters", tool_use->input}
        };
        tool_intercepted_ = true;
        
        ::msg("ConsensusExecutor: Intercepted tool call: %s with params: %s\n",
            tool_use->name.c_str(), tool_use->input.dump().c_str());
        
        // Return fake success to satisfy the system (though we won't use it)
        return {claude::messages::Message::tool_result(
            tool_use->id, 
            json{{"success", true}, {"intercepted", true}}.dump()
        )};
    }
    
    return {};
}

std::string ConsensusExecutor::format_consensus_prompt(
    const std::map<std::string, std::string>& agreements,
    const ToolConflict& conflict) {

    std::stringstream prompt;

    // Show each agent's individual consensus statement
    prompt << "The agents have marked consensus with the following statements:\n\n";

    for (const auto& [agent_id, consensus] : agreements) {
        prompt << agent_id << " says:\n\"" << consensus << "\"\n\n";
    }

    prompt << "Based on these consensus statements, execute the '" << conflict.first_call.tool_name
           << "' tool at address 0x" << std::hex << conflict.first_call.address
           << ".\n\n";

    prompt << "Extract the agreed-upon parameters from the consensus statements above. "
           << "The agents may have slightly different wording but should agree on the core solution. "
           << "Use your judgment to determine the actual parameters they all agreed upon.";

    return prompt.str();
}

} // namespace llm_re::agent