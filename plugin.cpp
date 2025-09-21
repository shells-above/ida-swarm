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
        ORCHESTRATOR,       // Normal IDA - user interacts with orchestrator
        SWARM_AGENT,        // Spawned IDA - runs as automated swarm agent
        RESURRECTED_AGENT,  // Resurrected agent for conflict resolution
        MCP_ORCHESTRATOR    // MCP mode - orchestrator without UI
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

    // MCP mode data
    std::string mcp_session_id_;
    std::string mcp_input_pipe_;
    std::string mcp_output_pipe_;

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
        // In resurrected mode, start with restored state
        else if (mode_ == RESURRECTED_AGENT && !swarm_agent_ && !agent_config_.empty()) {
            start_resurrected_agent();
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
        } else if (mode_ == RESURRECTED_AGENT) {
            setup_resurrected_agent_mode();
        } else if (mode_ == MCP_ORCHESTRATOR) {
            setup_mcp_orchestrator_mode();
        }
    }
    
    void detect_mode() {
        // Check for MCP config file first
        if (!idb_path_.empty()) {
            fs::path idb_dir = fs::path(idb_path_.c_str()).parent_path();
            fs::path mcp_config_path = idb_dir / "mcp_orchestrator_config.json";

            if (fs::exists(mcp_config_path)) {
                msg("LLM RE: Found MCP config file at %s\n", mcp_config_path.string().c_str());

                try {
                    // Read the config file
                    std::ifstream config_file(mcp_config_path);
                    json mcp_config;
                    config_file >> mcp_config;
                    config_file.close();

                    // Extract data from config
                    if (mcp_config.contains("session_id")) {
                        mcp_session_id_ = mcp_config["session_id"].get<std::string>();
                    }
                    if (mcp_config.contains("input_pipe")) {
                        mcp_input_pipe_ = mcp_config["input_pipe"].get<std::string>();
                    }
                    if (mcp_config.contains("output_pipe")) {
                        mcp_output_pipe_ = mcp_config["output_pipe"].get<std::string>();
                    }

                    // Delete the config file immediately after reading
                    fs::remove(mcp_config_path);
                    msg("LLM RE: Deleted MCP config file after reading\n");

                    mode_ = MCP_ORCHESTRATOR;
                    msg("LLM RE: Detected MCP orchestrator mode for session %s\n", mcp_session_id_.c_str());
                    return;

                } catch (const std::exception& e) {
                    msg("LLM RE: Failed to read MCP config: %s\n", e.what());
                    // Fall through to other detection methods
                }
            }
        }

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
                
                // Check if this is a resurrection by looking for saved state
                // conversation_state is only saved when an agent completes
                // so we know if this exists for us we are being resurrected
                fs::path conversation_state = parent / "conversation_state.json";

                if (fs::exists(conversation_state)) {
                    mode_ = RESURRECTED_AGENT;
                    msg("LLM RE: Detected RESURRECTED AGENT mode (ID: %s)\n", agent_id_.c_str());
                } else {
                    mode_ = SWARM_AGENT;
                    msg("LLM RE: Detected SWARM AGENT mode (ID: %s)\n", agent_id_.c_str());
                }
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
    
    void setup_resurrected_agent_mode() {
        msg("LLM RE: Setting up resurrected agent %s\n", agent_id_.c_str());

        // Load both the original config and the saved state
        if (load_agent_config() && load_saved_state()) {
            msg("LLM RE: Loaded config and state for resurrected agent %s\n", agent_id_.c_str());
            // Start the resurrected agent
            run(0);
        } else {
            msg("LLM RE: Failed to resurrect agent %s\n", agent_id_.c_str());
            mode_ = ORCHESTRATOR;  // Fall back to orchestrator mode
        }
    }

    void setup_mcp_orchestrator_mode() {
        // Create orchestrator without UI
        if (!orchestrator_) {
            if (mcp_session_id_.empty()) {
                msg("LLM RE: ERROR - MCP session ID not available\n");
                return;
            }

            msg("LLM RE: Starting MCP orchestrator for session %s\n", mcp_session_id_.c_str());
            msg("LLM RE: Input pipe: %s\n", mcp_input_pipe_.c_str());
            msg("LLM RE: Output pipe: %s\n", mcp_output_pipe_.c_str());

            orchestrator_ = new orchestrator::Orchestrator(*config_, idb_path_.c_str(), false /* no UI */);
            if (!orchestrator_->initialize_mcp_mode(mcp_session_id_, mcp_input_pipe_, mcp_output_pipe_)) {
                msg("LLM RE: Failed to initialize MCP orchestrator\n");
                delete orchestrator_;
                orchestrator_ = nullptr;
                return;
            }

            // Start the MCP listener thread
            orchestrator_->start_mcp_listener();

            msg("LLM RE: MCP orchestrator ready for session %s\n", mcp_session_id_.c_str());
        }
    }
    
    bool load_saved_state() {
        fs::path db_path(idb_path_.c_str());
        fs::path workspace = db_path.parent_path();
        
        // Load conversation state
        fs::path conversation_state_file = workspace / "conversation_state.json";

        if (!fs::exists(conversation_state_file)) {
            msg("LLM RE: No conversation state found for resurrection\n");
            return false;
        }
        
        try {
            // Load conversation state
            std::ifstream conv_file(conversation_state_file);
            json conversation_state;
            conv_file >> conversation_state;
            conv_file.close();
            
            // Merge states into agent config for resurrection
            agent_config_["saved_conversation"] = conversation_state["conversation"];  // restore conversation
            agent_config_["saved_task"] = conversation_state["task"];
            
            // Load resurrection config if present
            fs::path resurrection_config = workspace / "resurrection_config.json";
            if (fs::exists(resurrection_config)) {
                std::ifstream res_file(resurrection_config);
                json res_config;
                res_file >> res_config;
                res_file.close();
                
                // Merge resurrection-specific config
                agent_config_["conflict_channel"] = res_config.value("conflict_channel", "");
                agent_config_["resurrection_reason"] = res_config["reason"];  // "conflict_resolution" or "retry"
            }
            
            msg("LLM RE: Successfully loaded saved state for resurrection\n");
            return true;
            
        } catch (const std::exception& e) {
            msg("LLM RE: Failed to load saved state: %s\n", e.what());
            return false;
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
        
        msg("LLM RE: start_swarm_agent() called\n");
        
        // Get the orchestrator-generated prompt
        msg("LLM RE: Extracting prompt from config...\n");
        std::string orchestrator_prompt = agent_config_["prompt"];
        msg("LLM RE: Got prompt: %.100s...\n", orchestrator_prompt.c_str());
        
        msg("LLM RE: About to create SwarmAgent object for %s\n", agent_id_.c_str());
        msg("LLM RE: config_ pointer = %p\n", (void*)config_);
        
        // Safety check
        if (!config_) {
            msg("LLM RE: ERROR - config_ is NULL!\n");
            return;
        }
        
        // CRITICAL: This is where it might crash
        msg("LLM RE: Creating SwarmAgent with config at %p and agent_id %s\n", (void*)config_, agent_id_.c_str());
        
        // Validate config pointer
        if (!config_) {
            msg("LLM RE: ERROR - config_ is NULL!\n");
            return;
        }
        
        // Try to access config to verify it's valid
        msg("LLM RE: Config auth_method=%d, api_key_len=%zu\n", 
            (int)config_->api.auth_method, config_->api.api_key.length());
        
        msg("LLM RE: About to call new SwarmAgent\n");
        try {
            swarm_agent_ = new agent::SwarmAgent(*config_, agent_id_);
        } catch (const std::exception& e) {
            msg("LLM RE: Exception creating SwarmAgent: %s\n", e.what());
            return;
        } catch (...) {
            msg("LLM RE: Unknown exception creating SwarmAgent\n");
            return;
        }
        
        msg("LLM RE: SwarmAgent object created successfully\n");
        
        // Initialize with configuration
        msg("LLM RE: About to call swarm_agent_->initialize()\n");
        if (!swarm_agent_->initialize(agent_config_)) {
            msg("LLM RE: Failed to initialize swarm agent\n");
            delete swarm_agent_;
            swarm_agent_ = nullptr;
            return;
        }
        msg("LLM RE: SwarmAgent initialization returned successfully\n");
        
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
                    msg("LLM RE: Task completed for swarm agent, graceful shutdown initiated\n");
                    if (!shutting_down_) {
                        shutting_down_ = true;
                        // Request graceful shutdown to save database and exit IDA
                        if (swarm_agent_) {
                            swarm_agent_->request_graceful_shutdown();
                        }
                    }
                }
            }
        }, {AgentEvent::STATE});
    }
    
    void start_resurrected_agent() {
        if (swarm_agent_) return;
        
        msg("LLM RE: start_resurrected_agent() called for %s\n", agent_id_.c_str());
        
        // Create the SwarmAgent with resurrection config
        try {
            swarm_agent_ = new agent::SwarmAgent(*config_, agent_id_);
        } catch (const std::exception& e) {
            msg("LLM RE: Exception creating resurrected SwarmAgent: %s\n", e.what());
            return;
        }
        
        msg("LLM RE: Resurrected SwarmAgent object created\n");
        
        // Initialize with resurrection config
        if (!swarm_agent_->initialize(agent_config_)) {
            msg("LLM RE: Failed to initialize resurrected agent\n");
            delete swarm_agent_;
            swarm_agent_ = nullptr;
            return;
        }
        
        // Restore conversation history if available
        if (agent_config_.contains("saved_conversation")) {
            msg("LLM RE: Restoring conversation history...\n");
            swarm_agent_->restore_conversation_history(agent_config_["saved_conversation"]);
        }
        
        // If this is for conflict resolution, jump directly to conflict
        if (agent_config_.contains("conflict_channel")) {
            std::string conflict_channel = agent_config_["conflict_channel"];
            msg("LLM RE: Setting up for conflict resolution in channel %s\n", conflict_channel.c_str());

            // Create conflict state BEFORE joining (so we can track turns)
            // my_turn will be set to true when we see initiator's message via IRC replay
            swarm_agent_->add_conflict_state(conflict_channel, false);
            msg("LLM RE: Created conflict state for channel %s\n", conflict_channel.c_str());

            // Join the conflict channel (this triggers history replay)
            swarm_agent_->join_irc_channel(conflict_channel);

            // Start the conflict resolution task
            std::string task = "Participate in conflict resolution in channel " + conflict_channel +
                               ". The conflict details will appear in the channel history.";
            swarm_agent_->start_task(task);
            msg("LLM RE: Started conflict resolution task\n");
        } else {
            // if no conflict channel we probably crashed so retry
            // Resume normal task
            std::string task = agent_config_.value("saved_task", "Continue analysis");
            swarm_agent_->start_task(task);
        }
        
        msg("LLM RE: Resurrected agent %s is now active\n", agent_id_.c_str());
        
        // Subscribe to state changes
        EventBus& bus = get_event_bus();
        state_subscription_id_ = bus.subscribe([this](const AgentEvent& event) {
            if (event.type == AgentEvent::STATE && event.source == agent_id_) {
                int status = event.payload.value("status", -1);
                if (status == (int)AgentState::Status::Completed) {
                    msg("LLM RE: Resurrected agent completed, graceful shutdown\n");
                    if (!shutting_down_) {
                        shutting_down_ = true;
                        // Request graceful shutdown to save database and exit IDA
                        if (swarm_agent_) {
                            swarm_agent_->request_graceful_shutdown();
                        }
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
    "IDA Swarm",
    "Ctrl+Shift+O"
};