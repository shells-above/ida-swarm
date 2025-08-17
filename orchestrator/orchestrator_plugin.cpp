#include "../core/common_base.h"
#include "orchestrator.h"
#include "../core/config.h"
#include "../core/ida_utils.h"
#include <loader.hpp>

namespace llm_re::orchestrator {

// Orchestrator plugin module - This is the ONLY plugin that talks to the user
class orchestrator_plugin_t : public plugmod_t, public event_listener_t {
    Orchestrator* orchestrator_ = nullptr;
    Config* config_ = nullptr;
    qstring idb_path_;
    bool shutting_down_ = false;

public:
    orchestrator_plugin_t() {
        hook_event_listener(HT_UI, this);
        config_ = &Config::instance();
        
        // Don't get database path yet - wait for ui_ready_to_run
        msg("LLM RE Orchestrator: Plugin loaded, waiting for IDA to be ready\n");
    }

    virtual ~orchestrator_plugin_t() {
        unhook_event_listener(HT_UI, this);
        cleanup();
    }

    // plugmod_t interface
    virtual bool idaapi run(size_t arg) override {
        if (shutting_down_) return false;
        
        // Make sure we have the database path
        if (idb_path_.empty()) {
            const char* path = get_path(PATH_TYPE_IDB);
            if (path && path[0] != '\0') {
                idb_path_ = path;
            } else {
                msg("LLM RE Orchestrator: ERROR - No database path available\n");
                return false;
            }
        }
        
        if (!orchestrator_) {
            // Create orchestrator on first run
            orchestrator_ = new Orchestrator(*config_, idb_path_.c_str());
            if (!orchestrator_->initialize()) {
                msg("LLM RE Orchestrator: Failed to initialize orchestrator\n");
                delete orchestrator_;
                orchestrator_ = nullptr;
                return false;
            }
            msg("LLM RE Orchestrator: Started orchestrator for %s\n", idb_path_.c_str());
        }
        
        // Start interactive session with user
        orchestrator_->start_interactive_session();
        return true;
    }

    // event_listener_t interface
    virtual ssize_t idaapi on_event(ssize_t code, va_list va) override {
        switch (code) {
            case ui_database_closed:
                msg("LLM RE Orchestrator: Database closing, shutting down\n");
                prepare_for_shutdown();
                break;
                
            case ui_ready_to_run:
                // Get database path now that IDA is ready
                if (idb_path_.empty()) {
                    const char* path = get_path(PATH_TYPE_IDB);
                    if (path && path[0] != '\0') {
                        idb_path_ = path;
                        msg("LLM RE Orchestrator: IDA ready, database path: %s\n", idb_path_.c_str());
                    } else {
                        msg("LLM RE Orchestrator: IDA ready but no database path available\n");
                    }
                }
                break;
        }
        return 0;
    }

private:
    void prepare_for_shutdown() {
        shutting_down_ = true;
        cleanup();
    }
    
    void cleanup() {
        if (orchestrator_) {
            orchestrator_->shutdown();
            delete orchestrator_;
            orchestrator_ = nullptr;
        }
    }
};

} // namespace llm_re::orchestrator

// Plugin interface
static plugmod_t* idaapi init() {
    if (!is_idaq()) {
        return nullptr; // GUI version only
    }
    
    return new llm_re::orchestrator::orchestrator_plugin_t();
}

// Plugin description
plugin_t PLUGIN = {
    IDP_INTERFACE_VERSION,
    PLUGIN_MULTI | PLUGIN_FIX,
    init,
    nullptr,
    nullptr,
    "LLM Reverse Engineering Orchestrator",
    "The orchestrator for multi-agent reverse engineering",
    "LLM RE Orchestrator",
    "Ctrl+Shift+O"  // Hotkey for orchestrator
};