/**
 * LLM RE Plugin - Unified Multi-Agent Reverse Engineering System
 * 
 * This plugin operates in two modes:
 * 1. ORCHESTRATOR MODE (normal IDA): User interface for multi-agent analysis
 * 2. SWARM AGENT MODE (spawned IDA): Automated agent for collaborative analysis
 */

#include "core/common_base.h"
#include "orchestrator/orchestrator.h"
#include "agent/swarm_agent.h"
#include "agent/event_bus.h"
#include "agent/message_adapter.h"
#include "ui/orchestrator_ui.h"
#include "ui/ui_orchestrator_bridge.h"
#include "core/config.h"
#include "core/ida_utils.h"
#include <loader.hpp>
#include <filesystem>
#include <fstream>
#include <cstdlib>  // for _exit

namespace fs = std::filesystem;

namespace llm_re {

// Unified plugin that handles both orchestrator and swarm agent modes
class llm_re_plugin_t : public plugmod_t, public event_listener_t {
public:
    enum Mode {
        ORCHESTRATOR,  // Normal IDA - user interacts with orchestrator
        SWARM_AGENT    // Spawned IDA - runs as automated swarm agent
    };

private:
    Mode mode_ = ORCHESTRATOR;
    
    // Orchestrator mode
    orchestrator::Orchestrator* orchestrator_ = nullptr;
    ui::OrchestratorUI* ui_window_ = nullptr;
    
    // Swarm agent mode
    agent::SwarmAgent* swarm_agent_ = nullptr;
    json agent_config_;
    std::string agent_id_;
    std::string state_subscription_id_;
    
    // Common
    Config* config_ = nullptr;
    qstring idb_path_;
    bool shutting_down_ = false;
    

public:
    llm_re_plugin_t() {
        hook_event_listener(HT_UI, this);
        config_ = &Config::instance();
        
        // Load configuration from file
        config_->load();
        
        msg("LLM RE: Plugin loaded, detecting mode...\n");
    }
    
    virtual ~llm_re_plugin_t() {
        unhook_event_listener(HT_UI, this);
        cleanup();
    }
    
    // plugmod_t interface
    virtual bool idaapi run(size_t arg) override {
        if (shutting_down_) return false;
        
        // In swarm agent mode, this is called to start the agent
        if (mode_ == SWARM_AGENT && !swarm_agent_ && !agent_config_.empty()) {
            start_swarm_agent();
        }
        // In orchestrator mode, call the orchestrator
        else if (mode_ == ORCHESTRATOR) {
            start_orchestrator();
        }
        
        return true;
    }
    
    // event_listener_t interface
    virtual ssize_t idaapi on_event(ssize_t code, va_list va) override {
        switch (code) {
            case ui_database_closed:
                msg("LLM RE: Database closing, shutting down\n");
                prepare_for_shutdown();
                break;
                
            case ui_ready_to_run:
                on_ida_ready();
                break;
        }
        return 0;
    }

private:
    void on_ida_ready() {
        // Get database path
        if (idb_path_.empty()) {
            const char* path = get_path(PATH_TYPE_IDB);
            if (path && path[0] != '\0') {
                idb_path_ = path;
            }
        }
        
        // Detect mode based on path
        detect_mode();
        
        // Setup based on mode
        if (mode_ == ORCHESTRATOR) {
            setup_orchestrator_mode();
        } else if (mode_ == SWARM_AGENT) {
            setup_swarm_agent_mode();
        }
    }
    
    void detect_mode() {
        if (idb_path_.empty()) {
            mode_ = ORCHESTRATOR;
            return;
        }
        
        // Check if we're in a swarm agent workspace
        if (idb_path_.find("/ida_swarm_workspace/") != qstring::npos &&
            (idb_path_.find("/agents/agent_") != qstring::npos ||
             idb_path_.find("\\agents\\agent_") != qstring::npos)) {
            
            // Extract agent ID from path
            fs::path db_path(idb_path_.c_str());
            fs::path parent = db_path.parent_path();
            
            if (parent.filename().string().substr(0, 6) == "agent_") {
                agent_id_ = parent.filename().string();
                mode_ = SWARM_AGENT;
                msg("LLM RE: Detected SWARM AGENT mode (ID: %s)\n", agent_id_.c_str());
                return;
            }
        }
        
        mode_ = ORCHESTRATOR;
        msg("LLM RE: Running in ORCHESTRATOR mode\n");
    }
    
    void setup_orchestrator_mode() {
        // Create and initialize orchestrator on startup
        if (!orchestrator_) {
            orchestrator_ = new orchestrator::Orchestrator(*config_, idb_path_.c_str());
            if (!orchestrator_->initialize()) {
                msg("LLM RE: Failed to initialize orchestrator\n");
                delete orchestrator_;
                orchestrator_ = nullptr;
                return;
            }
            
            // Set orchestrator in the bridge so UI can communicate with it
            ui::UIOrchestratorBridge::instance().set_orchestrator(orchestrator_);
        }
        msg("LLM RE: Orchestrator ready\n");
    }
    
    void setup_swarm_agent_mode() {
        // Load agent configuration
        if (load_agent_config()) {
            msg("LLM RE: Loaded config for agent %s\n", agent_id_.c_str());
            // Start the agent
            run(0);
        } else {
            msg("LLM RE: Failed to load agent config\n");
            mode_ = ORCHESTRATOR;  // Fall back to orchestrator mode
        }
    }
    
    void start_orchestrator() {
        if (!orchestrator_) {
            msg("LLM RE: Orchestrator not initialized\n");
            return;
        }
        
        // Create UI window if it doesn't exist
        if (!ui_window_) {
            ui_window_ = new ui::OrchestratorUI(nullptr);
            msg("LLM RE: Created orchestrator UI\n");
        }
        
        // Show the UI window
        ui_window_->show_ui();
        msg("LLM RE: Showing orchestrator UI\n");
    }
    
    void start_swarm_agent() {
        if (swarm_agent_) return;
        
        // Get the orchestrator-generated prompt
        std::string orchestrator_prompt = agent_config_["prompt"];
        
        swarm_agent_ = new agent::SwarmAgent(*config_, agent_id_);
        
        // Initialize with configuration
        if (!swarm_agent_->initialize(agent_config_)) {
            msg("LLM RE: Failed to initialize swarm agent\n");
            delete swarm_agent_;
            swarm_agent_ = nullptr;
            return;
        }
        
        msg("LLM RE: Starting swarm agent %s\n", agent_id_.c_str());
        msg("LLM RE: Task: %.200s...\n", orchestrator_prompt.c_str());
        
        // Start working with the orchestrator's prompt
        swarm_agent_->start_task(orchestrator_prompt);
        
        // Subscribe to agent state changes to monitor completion
        auto& bus = get_event_bus();
        state_subscription_id_ = bus.subscribe([this](const AgentEvent& event) {
            if (event.type == AgentEvent::STATE && event.source == agent_id_) {
                int status = event.payload.value("status", -1);
                if (status == (int)AgentState::Status::Completed) {
                    msg("LLM RE: Task completed for swarm agent, requesting IDA exit\n");
                    if (!shutting_down_) {
                        // Give some time for final messages to send
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                        _exit(0);  // Force process termination
                    }
                }
            }
        }, {AgentEvent::STATE});
    }
    
    bool load_agent_config() {
        // Extract binary name from the database path
        fs::path db_path(idb_path_.c_str());
        fs::path agents_dir = db_path.parent_path().parent_path();
        fs::path binary_dir = agents_dir.parent_path();
        std::string binary_name = binary_dir.filename().string();
        
        // Config should be in workspace configs directory
        fs::path config_path = fs::path("/tmp/ida_swarm_workspace") / binary_name / "configs" / (agent_id_ + "_config.json");
        
        if (!fs::exists(config_path)) {
            msg("LLM RE: Config not found at %s\n", config_path.string().c_str());
            return false;
        }
        
        try {
            std::ifstream file(config_path);
            file >> agent_config_;
            file.close();
            return true;
        } catch (const std::exception& e) {
            msg("LLM RE: Failed to parse config: %s\n", e.what());
            return false;
        }
    }
    
    void prepare_for_shutdown() {
        shutting_down_ = true;
        cleanup();
    }
    
    void cleanup() {
        // Unsubscribe from event bus
        if (!state_subscription_id_.empty()) {
            get_event_bus().unsubscribe(state_subscription_id_);
            state_subscription_id_.clear();
        }
        
        // Clean up UI
        if (ui_window_) {
            delete ui_window_;
            ui_window_ = nullptr;
        }
        
        // Clean up orchestrator mode
        if (orchestrator_) {
            orchestrator_->shutdown();
            delete orchestrator_;
            orchestrator_ = nullptr;
        }
        
        // Clean up swarm agent mode
        if (swarm_agent_) {
            swarm_agent_->shutdown();
            delete swarm_agent_;
            swarm_agent_ = nullptr;
        }
    }
};

} // namespace llm_re

// Plugin interface
static plugmod_t* idaapi init() {
    // Plugin can run in any IDA version
    return new llm_re::llm_re_plugin_t();
}

// Plugin description
plugin_t PLUGIN = {
    IDP_INTERFACE_VERSION,
    PLUGIN_MULTI | PLUGIN_FIX,
    init,
    nullptr,
    nullptr,
    "LLM Multi-Agent RE",
    "Orchestrated multi-agent reverse engineering with LLMs",
    "LLM RE System",
    "Ctrl+Shift+O"
};