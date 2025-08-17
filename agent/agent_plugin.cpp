#include "../core/common_base.h"
#include "swarm_agent.h"
#include "../core/config.h"
#include "../core/ida_utils.h"
#include <loader.hpp>
#include <fstream>
#include <filesystem>
#include <string>
#include <thread>
#include <chrono>
#include <cstdlib>  // for _exit

namespace fs = std::filesystem;

namespace llm_re::agent {

// Agent plugin module - Runs in spawned IDA instances
class agent_plugin_t : public plugmod_t, public event_listener_t {
    SwarmAgent* agent_ = nullptr;
    Config* config_ = nullptr;
    json agent_config_;
    qstring idb_path_;
    std::string agent_id_;  // Store agent ID from command line
    bool shutting_down_ = false;
    bool is_spawned_agent_ = false;  // True only if we're a spawned agent

public:
    agent_plugin_t() {
        // Don't do anything in constructor - wait for run() to tell us if we're a spawned agent
        hook_event_listener(HT_UI, this);
        config_ = &Config::instance();
        
        // Don't get database path yet - wait for ui_ready_to_run
        // This ensures we get the correct path
    }

    virtual ~agent_plugin_t() {
        unhook_event_listener(HT_UI, this);
        cleanup();
    }

    // plugmod_t interface
    virtual bool idaapi run(size_t arg) override {
        if (shutting_down_) return false;
        
        // Only create and start agent if we're a spawned agent with valid config
        if (!agent_ && is_spawned_agent_ && !agent_config_.empty()) {
            // Get the orchestrator-generated prompt (includes task and collaboration instructions)
            std::string orchestrator_prompt = agent_config_["prompt"];
            
            agent_ = new SwarmAgent(*config_, agent_id_);
            
            // Initialize with configuration
            if (!agent_->initialize(agent_config_)) {
                msg("Agent Plugin: Failed to initialize agent\n");
                delete agent_;
                agent_ = nullptr;
                return false;
            }
            
            msg("Agent Plugin: Starting agent %s\n", agent_id_.c_str());
            msg("Agent Plugin: Orchestrator prompt: %.200s...\n", orchestrator_prompt.c_str());
            
            // Start working with the orchestrator's prompt
            agent_->start_task(orchestrator_prompt);
            
            // Set up callback to monitor agent state for completion
            agent_->set_message_callback([this](AgentMessageType type, const Agent::CallbackData& data) {
                if (type == AgentMessageType::StateChanged && !data.json_data.empty()) {
                    int status = data.json_data.value("status", -1);
                    if (status == (int)AgentState::Status::Completed && is_spawned_agent_) {
                        msg("Agent Plugin: Task completed for spawned agent, requesting IDA exit\n");
                        if (!shutting_down_) {
                            msg("Agent Plugin: Force exit after timeout\n");
                            _exit(0);  // Force process termination
                        }
                    }
                }
            });
        }
        
        return true;
    }

    // event_listener_t interface
    virtual ssize_t idaapi on_event(ssize_t code, va_list va) override {
        switch (code) {
            case ui_database_closed:
                msg("Agent Plugin: Database closing, shutting down\n");
                prepare_for_shutdown();
                break;
                
            case ui_ready_to_run:
                // Get database path now that IDA is ready
                if (idb_path_.empty()) {
                    const char* path = get_path(PATH_TYPE_IDB);
                    if (path && path[0] != '\0') {
                        idb_path_ = path;
                        msg("Agent Plugin: Database path: %s\n", idb_path_.c_str());
                    }
                }
                
                // Check if we're in an agent workspace by checking the path
                if (!is_spawned_agent_ && !idb_path_.empty()) {
                    if (idb_path_.find("/ida_swarm_workspace/") != qstring::npos &&
                        (idb_path_.find("/agents/agent_") != qstring::npos ||
                         idb_path_.find("\\agents\\agent_") != qstring::npos)) {
                        
                        // Extract agent ID from path
                        // Path format: /tmp/ida_swarm_workspace/<binary_name>/agents/agent_1/database.i64
                        fs::path db_path(idb_path_.c_str());
                        fs::path parent = db_path.parent_path();
                        
                        if (parent.filename().string().substr(0, 6) == "agent_") {
                            agent_id_ = parent.filename().string();
                            is_spawned_agent_ = true;
                            msg("Agent Plugin: Detected spawned agent from workspace path: %s\n", agent_id_.c_str());
                            
                            // Load the agent configuration
                            if (load_agent_config_by_id(agent_id_)) {
                                msg("Agent Plugin: Loaded config for agent %s\n", agent_id_.c_str());
                                // Start the agent
                                run(0);
                            } else {
                                msg("Agent Plugin: Failed to load config for agent %s\n", agent_id_.c_str());
                                is_spawned_agent_ = false;
                            }
                        }
                    }
                    
                    if (!is_spawned_agent_) {
                        msg("Agent Plugin: Not a spawned agent (regular IDA session)\n");
                    }
                }
                break;
        }
        return 0;
    }

private:
    bool load_agent_config() {
        // Look for config file based on database name
        fs::path db_path(idb_path_.c_str());
        std::string agent_id = db_path.parent_path().filename().string();
        
        return load_agent_config_by_id(agent_id);
    }
    
    bool load_agent_config_by_id(const std::string& agent_id) {
        // Extract binary name from the database path
        // Path format: /tmp/ida_swarm_workspace/<binary_name>/agents/agent_1/database.i64
        fs::path db_path(idb_path_.c_str());
        fs::path agents_dir = db_path.parent_path().parent_path(); // Go up to agents/ directory
        fs::path binary_dir = agents_dir.parent_path(); // Go up to binary name directory
        std::string binary_name = binary_dir.filename().string();
        
        // Config should be in workspace configs directory for this binary
        fs::path config_path = fs::path("/tmp/ida_swarm_workspace") / binary_name / "configs" / (agent_id + "_config.json");
        
        if (!fs::exists(config_path)) {
            msg("Agent Plugin: Config not found at %s\n", config_path.string().c_str());
            return false;
        }
        
        try {
            std::ifstream file(config_path);
            file >> agent_config_;
            file.close();
            
            msg("Agent Plugin: Loaded config for agent %s\n", 
                agent_config_["agent_id"].get<std::string>().c_str());
            return true;
            
        } catch (const std::exception& e) {
            msg("Agent Plugin: Failed to parse config: %s\n", e.what());
            return false;
        }
    }
    
    void prepare_for_shutdown() {
        shutting_down_ = true;
        cleanup();
    }
    
    void cleanup() {
        if (agent_) {
            agent_->shutdown();
            delete agent_;
            agent_ = nullptr;
        }
    }
};

} // namespace llm_re::agent

// Plugin interface
static plugmod_t* idaapi init() {
    // Agent plugin can run in any IDA version
    return new llm_re::agent::agent_plugin_t();
}

// Plugin description
plugin_t PLUGIN = {
    IDP_INTERFACE_VERSION,
    PLUGIN_MULTI | PLUGIN_FIX,
    init,
    nullptr,
    nullptr,
    "LLM Swarm Agent",
    "Agent for multi-agent reverse engineering",
    "LLM Agent",
    nullptr  // No hotkey for agent plugin
};