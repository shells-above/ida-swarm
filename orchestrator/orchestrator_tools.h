#pragma once

#include "../sdk/tools/registry.h"
#include "orchestrator.h"

namespace llm_re::orchestrator {

// Forward declaration
class Orchestrator;

// Base tool for orchestrator
class OrchestratorToolBase : public claude::tools::Tool {
protected:
    Orchestrator* orchestrator_;
    
public:
    explicit OrchestratorToolBase(Orchestrator* orch) : orchestrator_(orch) {}
    virtual ~OrchestratorToolBase() = default;
};

// Spawn agent tool
class SpawnAgentTool : public OrchestratorToolBase {
public:
    using OrchestratorToolBase::OrchestratorToolBase;
    
    std::string name() const override {
        return "spawn_agent";
    }
    
    std::string description() const override {
        return "Spawn a new agent to work on a specific task. The agent will work on an isolated "
               "copy of the database and can communicate with other agents via IRC. "
               "Think VERY carefully about the task and context before spawning.";
    }
    
    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("task", "The specific task for this agent to accomplish")
            .add_string("context", "Important context about the overall goal and how this fits in", false)
            .build();
    }
    
    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string task = input.at("task");
            std::string context = input.value("context", "");
            
            // Always spawn asynchronously - batching is handled by orchestrator
            auto result = orchestrator_->spawn_agent_async(task, context);
            
            if (result["success"]) {
                return claude::tools::ToolResult::success(result);
            } else {
                return claude::tools::ToolResult::failure(result["error"]);
            }
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Merge database tool
class MergeDatabaseTool : public OrchestratorToolBase {
public:
    using OrchestratorToolBase::OrchestratorToolBase;
    
    std::string name() const override {
        return "merge_database";
    }
    
    std::string description() const override {
        return "Merge an agent's findings and modifications back into the main database. "
               "This applies all the changes the agent made to their isolated copy. "
               "For use when you have an agent apply metadata (function name, comments, types, etc.) "
               "to its database, and you want to merge those changes into the main database.";
    }
    
    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("agent_id", "The ID of the agent whose work to merge")
            .build();
    }
    
    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string agent_id = input.at("agent_id");

            auto result = orchestrator_->merge_database(agent_id);
            
            if (result["success"]) {
                return claude::tools::ToolResult::success(result);
            } else {
                return claude::tools::ToolResult::failure(result["error"]);
            }
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Register all orchestrator tools
inline void register_orchestrator_tools(claude::tools::ToolRegistry& registry, Orchestrator* orchestrator) {
    registry.register_tool_type<SpawnAgentTool>(orchestrator);
    registry.register_tool_type<MergeDatabaseTool>(orchestrator);
}

} // namespace llm_re::orchestrator