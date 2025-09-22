#pragma once

#include "../sdk/tools/registry.h"
#include "orchestrator.h"
#include <fstream>
#include <filesystem>

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
        return "Spawn a specialized reverse engineering agent to analyze binaries and understand code structures. "
               "CRITICAL: Agents are ONLY for reverse engineering tasks - they analyze existing binaries, identify functions, "
               "understand data structures, and document findings. They CANNOT and WILL NOT write implementation files, or generate source code projects. This is a tool for UNDERSTANDING and REVERSE ENGINEERING. "
               "\n\nAgent capabilities: Binary analysis, function identification, data structure reverse engineering, "
               "cross-reference analysis, string analysis, import/export analysis, commenting and naming. "
               "\n\nAgent limitations: Cannot write .cpp/.h/.c files, cannot create complete implementations. If you need file creation, YOU must handle it yourself. "
               "\n\nIMPORTANT: The agent WILL **ONLY** have the information that **YOU PROVIDE TO THEM INSIDE 'task' or 'context'! "
               "This program *does NOT DO ANY ADDITIONAL HANDLING!* "
               "You *MUST THINK DEEPLY ABOUT EXACTLY WHAT THE AGENT NEEDS TO KNOW!* "
               "The spawned agent has *NO ADDITIONAL INFORMATION AT ALL!!* they ONLY know what you provide them!";
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

// Merge database tool - REMOVED: Now automatically called when agents complete
// class MergeDatabaseTool : public OrchestratorToolBase {
// public:
//     using OrchestratorToolBase::OrchestratorToolBase;
//     
//     std::string name() const override {
//         return "merge_database";
//     }
//     
//     std::string description() const override {
//         return "Merge an agent's findings and modifications back into the main database. "
//                "This applies all the changes the agent made to their isolated copy. "
//                "For use when you have an agent apply metadata (function name, comments, types, etc.) "
//                "to its database, and you want to merge those changes into the main database.";
//     }
//     
//     json parameters_schema() const override {
//         return claude::tools::ParameterBuilder()
//             .add_string("agent_id", "The ID of the agent whose work to merge")
//             .build();
//     }
//     
//     claude::tools::ToolResult execute(const json& input) override {
//         try {
//             std::string agent_id = input.at("agent_id");
// 
//             auto result = orchestrator_->merge_database(agent_id);
//             
//             if (result["success"]) {
//                 return claude::tools::ToolResult::success(result);
//             } else {
//                 return claude::tools::ToolResult::failure(result["error"]);
//             }
//         } catch (const std::exception& e) {
//             return claude::tools::ToolResult::failure(e.what());
//         }
//     }
// };

// Write file tool for orchestrator
class WriteFileTool : public OrchestratorToolBase {
public:
    using OrchestratorToolBase::OrchestratorToolBase;
    
    std::string name() const override {
        return "write_file";
    }
    
    std::string description() const override {
        return "Write content to a file. Use this for creating implementation files, "
               "or any other file creation tasks. This is YOUR responsibility as the orchestrator - "
               "agents are only for reverse engineering analysis, not file creation.";
    }
    
    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("file_path", "Path where to write the file")
            .add_string("content", "Content to write to the file")
            .build();
    }
    
    claude::tools::ToolResult execute(const json& input) override {
        try {
            std::string file_path = input.at("file_path");
            std::string content = input.at("content");
            
            // Create directories if they don't exist
            std::filesystem::path path(file_path);
            if (path.has_parent_path()) {
                std::filesystem::create_directories(path.parent_path());
            }
            
            // Write file
            std::ofstream file(file_path);
            if (!file.is_open()) {
                return claude::tools::ToolResult::failure("Failed to open file for writing: " + file_path);
            }
            
            file << content;
            file.close();
            
            json result = {
                {"success", true},
                {"file_path", file_path},
                {"bytes_written", content.length()}
            };
            
            return claude::tools::ToolResult::success(result);
            
        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(e.what());
        }
    }
};

// Register all orchestrator tools
inline void register_orchestrator_tools(claude::tools::ToolRegistry& registry, Orchestrator* orchestrator) {
    registry.register_tool_type<SpawnAgentTool>(orchestrator);
    // MergeDatabaseTool removed - now automatically called when agents complete
    registry.register_tool_type<WriteFileTool>(orchestrator);
}

} // namespace llm_re::orchestrator