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
            .add_string("channel", "The IRC channel to send to (e.g., '#conflict_8dec_set_variable')")
            .add_string("message", "The message to send. For conflicts, start with 'AGREE|' or 'DISAGREE|'")
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

// Register IRC tools for SwarmAgent
inline void register_swarm_irc_tools(claude::tools::ToolRegistry& registry, SwarmAgent* swarm_agent) {
    registry.register_tool_type<SendIRCMessageTool>(swarm_agent);
}

} // namespace llm_re::agent