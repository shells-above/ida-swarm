#pragma once

#include "../agent/tool_system.h"
#include "swarm_agent.h"

namespace llm_re::agent {

// Forward declaration
class SwarmAgent;

// Tool for agents to send IRC messages during conflict resolution
class SendIRCMessageTool : public claude::tools::Tool {
public:
    explicit SendIRCMessageTool(SwarmAgent* agent) : swarm_agent_(agent) {}

    std::string name() const override {
        return "send_irc_message";
    }

    std::string description() const override {
        return "Send a message to an IRC channel for multi-agent communication and conflict resolution";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("channel", "The IRC channel to send to (e.g., '#conflict_8dec_set_variable_time')")
            .add_string("message", "The message to send")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string channel = input.at("channel");
            std::string message = input.at("message");

            if (!swarm_agent_) {
                return claude::tools::ToolResult::failure("Not in swarm agent mode");
            }

            // Send the message via SwarmAgent's IRC connection
            swarm_agent_->send_irc_message(channel, message);

            json result;
            result["success"] = true;
            result["channel"] = channel;
            result["message_sent"] = message;

            return claude::tools::ToolResult::success(result);

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(
                std::format("Failed to send IRC message: {}", e.what())
            );
        }
    }

private:
    SwarmAgent* swarm_agent_ = nullptr;
};

// Tool for marking consensus reached during conflict resolution
class MarkConsensusReachedTool : public claude::tools::Tool {
public:
    explicit MarkConsensusReachedTool(SwarmAgent* agent) : swarm_agent_(agent) {}

    std::string name() const override {
        return "mark_consensus_reached";
    }

    std::string description() const override {
        return "Mark that consensus has been reached in a conflict resolution discussion. "
               "CRITICAL: ALL agents involved in the conflict MUST call this tool for consensus to be valid. "
               "Only call this when all agents have explicitly agreed on the same solution.";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("consensus",
                "The complete consensus statement that ALL agents have agreed upon. "
                "This MUST contain ALL information needed to perform the tool call that was in conflict, "
                "including the exact address, tool name, and ALL parameters with their specific values. "
                "Be extremely specific and complete - this will be used to execute the actual tool.")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string consensus = input.at("consensus");

            if (!swarm_agent_) {
                return claude::tools::ToolResult::failure("Not in swarm agent mode");
            }

            // Check if we're in an active conflict
            if (!swarm_agent_->has_active_conflict()) {
                return claude::tools::ToolResult::failure("No active conflict to mark consensus for");
            }

            std::string conflict_channel = swarm_agent_->get_conflict_channel();
            std::string agent_id = swarm_agent_->get_agent_id();

            // Send MARKED_CONSENSUS message to the conflict channel itself
            // Format: MARKED_CONSENSUS|agent_id|consensus
            std::string message = std::format("MARKED_CONSENSUS|{}|{}",
                                            agent_id, consensus);
            swarm_agent_->send_irc_message(conflict_channel, message);

            // Set waiting_for_consensus_complete to prevent processing other messages
            SimpleConflictState* conflict = swarm_agent_->get_conflict_by_channel(conflict_channel);
            if (conflict) {
                conflict->waiting_for_consensus_complete = true;
                conflict->my_turn = false;  // Ensure we're not processing turns
            }

            json result;
            result["success"] = true;
            result["message"] = "Consensus marked and sent to orchestrator, waiting for confirmation";
            result["consensus"] = consensus;

            return claude::tools::ToolResult::success(result);

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(
                std::format("Failed to mark consensus: {}", e.what())
            );
        }
    }

private:
    SwarmAgent* swarm_agent_ = nullptr;
};

// Register IRC tools for SwarmAgent
inline void register_swarm_irc_tools(claude::tools::ToolRegistry& registry, SwarmAgent* swarm_agent) {
    registry.register_tool_type<SendIRCMessageTool>(swarm_agent);
    registry.register_tool_type<MarkConsensusReachedTool>(swarm_agent);
}

} // namespace llm_re::agent