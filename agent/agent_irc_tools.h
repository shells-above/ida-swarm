#pragma once

#include "../agent/tool_system.h"
#include "swarm_agent.h"

namespace llm_re::agent {

// Forward declaration
class SwarmAgent;

// Broadcast message to all agents
class BroadcastMessageTool : public tools::IDAToolBase {
private:
    SwarmAgent* agent_;
    
public:
    BroadcastMessageTool(std::shared_ptr<BinaryMemory> mem,
                        std::shared_ptr<ActionExecutor> exec,
                        SwarmAgent* agent)
        : IDAToolBase(mem, exec), agent_(agent) {}
    
    std::string name() const override {
        return "broadcast_message";
    }
    
    std::string description() const override {
        return "Broadcast a message to all active agents. Use this to share important findings, "
               "ask for help, or coordinate work. All agents will see your message.";
    }
    
    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("message", "Message to broadcast to all agents")
            .build();
    }
    
    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string message = input.at("message");
            
            // Broadcast to #agents channel
            agent_->send_irc_message("#agents", message);
            
            return claude::tools::ToolResult::success({
                {"broadcast", true},
                {"message", message}
            });
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Query a specific agent privately
class QueryAgentTool : public tools::IDAToolBase {
private:
    SwarmAgent* agent_;
    
public:
    QueryAgentTool(std::shared_ptr<BinaryMemory> mem,
                   std::shared_ptr<ActionExecutor> exec,
                   SwarmAgent* agent)
        : IDAToolBase(mem, exec), agent_(agent) {}
    
    std::string name() const override {
        return "query_agent";
    }
    
    std::string description() const override {
        return "Start a private conversation with a specific agent. This creates a private channel "
               "where you can discuss in detail. You can use all your analysis tools during the conversation. "
               "The conversation continues until one of you uses leave_private_channel.";
    }
    
    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("agent_id", "The agent to query (e.g., agent_1, agent_2)")
            .add_string("initial_message", "Your opening message to the agent")
            .build();
    }
    
    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string target = input.at("agent_id");
            std::string message = input.at("initial_message");
            
            // Start private conversation
            agent_->start_private_conversation(target, message);
            
            return claude::tools::ToolResult::success({
                {"private_channel_opened", true},
                {"with_agent", target},
                {"message", "Private conversation started. You can use all tools while discussing."}
            });
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Leave private conversation
class LeavePrivateChannelTool : public tools::IDAToolBase {
private:
    SwarmAgent* agent_;
    
public:
    LeavePrivateChannelTool(std::shared_ptr<BinaryMemory> mem,
                            std::shared_ptr<ActionExecutor> exec,
                            SwarmAgent* agent)
        : IDAToolBase(mem, exec), agent_(agent) {}
    
    std::string name() const override {
        return "leave_private_channel";
    }
    
    std::string description() const override {
        return "Leave the current private conversation or conflict deliberation channel and return to your main task. "
               "Use this when you've finished discussing or reached consensus.";
    }
    
    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .build();  // No parameters needed
    }
    
    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string left_channel = agent_->leave_private_channel();
            
            if (!left_channel.empty()) {
                return claude::tools::ToolResult::success({
                    {"left_channel", left_channel},
                    {"message", "Returned to main task"}
                });
            } else {
                return claude::tools::ToolResult::success({
                    {"message", "Not in any private channel"}
                });
            }
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Register simplified IRC tools
inline void register_agent_irc_tools(claude::tools::ToolRegistry& registry,
                                    SwarmAgent* agent) {
    std::shared_ptr<BinaryMemory> memory = agent->get_memory();
    std::shared_ptr<ActionExecutor> executor = std::make_shared<ActionExecutor>(memory);
    
    registry.register_tool_type<BroadcastMessageTool>(memory, executor, agent);
    registry.register_tool_type<QueryAgentTool>(memory, executor, agent);
    registry.register_tool_type<LeavePrivateChannelTool>(memory, executor, agent);
}

} // namespace llm_re::agent