//
// Created by user on 6/30/25.
//

#include "ui_v2/views/main_window.h"
#include "ui_v2/core/agent_controller.h"
#include "core/ida_utils.h"
#include "core/config.h"

namespace llm_re {

// Plugin module
class llm_plugin_t : public plugmod_t, public event_listener_t {
    ui_v2::MainWindow* main_window = nullptr;
    ui_v2::AgentController* agent_controller = nullptr;
    std::vector<qstring> registered_actions;
    std::map<qstring, qstring> action_menupaths;
    qstring idb_path_;
    bool shutting_down = false;
    bool window_closed = false;
    Config* config_ = nullptr;

    // Action handler that checks if plugin is still valid
    struct llm_action_handler_t : public action_handler_t {
        llm_plugin_t* plugin;

        llm_action_handler_t(llm_plugin_t* p) : plugin(p) {}

        virtual int idaapi activate(action_activation_ctx_t* ctx) override {
            if (!plugin || plugin->shutting_down) {
                return 0;
            }
            // IDA guarantees this is called from main thread
            return do_activate(ctx);
        }

        virtual int do_activate(action_activation_ctx_t* ctx) = 0;

        virtual action_state_t idaapi update(action_update_ctx_t* ctx) override {
            if (!plugin || plugin->shutting_down) {
                return AST_DISABLE;
            }
            return AST_ENABLE_ALWAYS;
        }
    };

    // Action handlers as member variables
    struct show_ui_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int do_activate(action_activation_ctx_t* ctx) override {
            plugin->show_main_window();
            return 1;
        }
    };

    struct comprehensive_re_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int do_activate(action_activation_ctx_t* ctx) override {
            plugin->comprehensive_reverse_engineering();
            return 1;
        }
    };

    // Handler instances
    show_ui_ah_t* show_ui_handler = nullptr;
    comprehensive_re_ah_t* comprehensive_re_handler = nullptr;

public:
    llm_plugin_t();
    virtual ~llm_plugin_t();

    // plugmod_t virtual functions
    virtual bool idaapi run(size_t arg) override;

    // event_listener_t virtual function
    virtual ssize_t idaapi on_event(ssize_t code, va_list va) override;

    void register_actions();
    void unregister_actions();
    void cleanup_window();
    void prepare_for_shutdown();
    void load_config();

    // actions
    void show_main_window();
    void comprehensive_reverse_engineering();
};

// Simplified plugin instance manager - no mutex needed since IDA guarantees main thread
class PluginInstanceManager {
private:
    static std::map<qstring, llm_plugin_t*> instances_;  // IDB path -> plugin instance

public:
    static void register_instance(const qstring& idb_path, llm_plugin_t* instance) {
        instances_[idb_path] = instance;
    }

    static void unregister_instance(const qstring& idb_path) {
        instances_.erase(idb_path);
    }

    static llm_plugin_t* get_instance(const qstring& idb_path) {
        auto it = instances_.find(idb_path);
        return (it != instances_.end()) ? it->second : nullptr;
    }

    static void shutdown_all() {
        for (auto& [path, instance] : instances_) {
            if (instance) {
                instance->prepare_for_shutdown();
            }
        }
        instances_.clear();
    }
};

std::map<qstring, llm_plugin_t*> PluginInstanceManager::instances_;

// Implementation
llm_plugin_t::llm_plugin_t() {
    // Constructor is guaranteed to run in main thread by IDA

    // Initialize CURL globally for the plugin
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Get IDB path for instance tracking
    // Handle case where no database is loaded yet
    const char* path = get_path(PATH_TYPE_IDB);
    if (path && path[0] != '\0') {
        idb_path_ = path;
    } else {
        idb_path_ = "no_database";
    }

    msg("LLM RE: Plugin initialized for IDB: %s\n", idb_path_.c_str());

    // Load configuration
    load_config();

    // Register this instance
    PluginInstanceManager::register_instance(idb_path_, this);

    // Hook UI events to detect when IDA is closing
    hook_event_listener(HT_UI, this);

    // Create handler instances
    show_ui_handler = new show_ui_ah_t(this);
    comprehensive_re_handler = new comprehensive_re_ah_t(this);

    // Register actions after handlers are created
    register_actions();
}

llm_plugin_t::~llm_plugin_t() {
    // Destructor is guaranteed to run in main thread by IDA
    msg("LLM RE: Plugin cleanup started for %s\n", idb_path_.c_str());

    prepare_for_shutdown();

    // Unregister from manager
    PluginInstanceManager::unregister_instance(idb_path_);

    // Unhook events first
    unhook_event_listener(HT_UI, this);

    // Clean up window and controller
    cleanup_window();

    // Unregister actions before deleting handlers
    unregister_actions();

    // Delete handler instances
    if (show_ui_handler) {
        delete show_ui_handler;
        show_ui_handler = nullptr;
    }
    if (comprehensive_re_handler) {
        delete comprehensive_re_handler;
        comprehensive_re_handler = nullptr;
    }

    // Cleanup CURL globally
    curl_global_cleanup();

    msg("LLM RE: Plugin terminated for %s\n", idb_path_.c_str());
}

void llm_plugin_t::prepare_for_shutdown() {
    shutting_down = true;

    // Notify the window that we're shutting down
    if (main_window) {
        main_window->setShuttingDown(true);
    }

    // Clean up window if it exists
    cleanup_window();
}

ssize_t idaapi llm_plugin_t::on_event(ssize_t code, va_list va) {
    switch (code) {
        case ui_database_inited:
            // Database fully loaded
            break;

        case ui_ready_to_run:
            // UI is ready
            break;

        case ui_saving:
            // IDA is saving the database
            msg("LLM RE: Database saving - preparing cleanup\n");
            // Don't cleanup yet, just prepare
            break;

        case ui_saved:
            // Database has been saved
            msg("LLM RE: Database saved\n");
            break;

        case ui_database_closed:
            msg("LLM RE: Received ui_database_closed event\n");
            break;

        case ui_destroying_plugmod:
            {
                // Check if it's our plugin being destroyed
                const plugmod_t* mod = va_arg(va, const plugmod_t*);
                if (mod == this) {
                    msg("LLM RE: Plugin module being destroyed\n");
                    prepare_for_shutdown();
                }
            }
            break;
    }
    return 0;
}

void llm_plugin_t::register_actions() {
    // Generate a unique prefix for this instance
    char prefix[64];
    ::qsnprintf(prefix, sizeof(prefix), "llm_re_%s_%p", qbasename(idb_path_.c_str()), this);

    // Define and register actions
    struct {
        const char* base_name;
        const char* label;
        action_handler_t* handler;
        const char* shortcut;
        const char* tooltip;
        const char* menupath;
        bool use_global_shortcut;
    } actions[] = {
        {
            "show_ui",
            "LLM RE Agent",
            show_ui_handler,
            "Ctrl+Shift+L",
            "Show LLM Reverse Engineering Agent",
            "Edit/LLM RE/Show Agent",
            true
        },
    {
        "comprehensive_re",
        "Comprehensive Reverse Engineering",
        comprehensive_re_handler,
        "Ctrl+Shift+R",
        "Perform systematic reverse engineering with full annotation",
        "Edit/LLM RE/Comprehensive Analysis",
        false
        }
    };

    // Keep track of whether we've registered the main action globally
    static std::map<qstring, bool> global_actions_registered;
    bool& registered_for_idb = global_actions_registered[idb_path_];

    // Register each action
    for (const auto& action : actions) {
        // Create unique action name for this instance
        qstring action_name;
        action_name.sprnt("%s:%s", prefix, action.base_name);

        action_desc_t desc = {};
        desc.cb = sizeof(action_desc_t);
        desc.name = action_name.c_str();
        desc.label = action.label;
        desc.handler = action.handler;
        desc.owner = this;
        desc.tooltip = action.tooltip;
        desc.icon = -1;
        desc.flags = ADF_OT_PLUGMOD;

        // Only use global shortcuts for the first instance to avoid conflicts
        if (action.use_global_shortcut && !registered_for_idb) {
            desc.shortcut = action.shortcut;
            desc.flags |= ADF_GLOBAL;
        } else if (!action.use_global_shortcut) {
            desc.shortcut = action.shortcut;
        }

        if (register_action(desc)) {
            registered_actions.push_back(action_name);

            // Attach to menu and store the path for later detachment
            if (action.menupath) {
                attach_action_to_menu(action.menupath, action_name.c_str(), SETMENU_APP);
                action_menupaths[action_name] = action.menupath;
            }
        } else {
            msg("LLM RE: Failed to register action %s\n", action_name.c_str());
        }
    }

    // Mark global actions as registered after first instance
    if (!registered_for_idb && !registered_actions.empty()) {
        registered_for_idb = true;
    }

    // Add toolbar button for main UI (use the first registered action)
    if (!registered_actions.empty()) {
        attach_action_to_toolbar("AnalysisToolBar", registered_actions[0].c_str());
    }

    msg("LLM RE: Registered %zu actions\n", registered_actions.size());
}

void llm_plugin_t::unregister_actions() {
    // Detach from toolbar first
    if (!registered_actions.empty()) {
        detach_action_from_toolbar("AnalysisToolBar", registered_actions[0].c_str());
    }

    // Process actions in reverse order
    for (auto it = registered_actions.rbegin(); it != registered_actions.rend(); ++it) {
        const qstring& action_name = *it;

        // Detach from menu if it was attached
        auto menu_it = action_menupaths.find(action_name);
        if (menu_it != action_menupaths.end()) {
            detach_action_from_menu(menu_it->second.c_str(), action_name.c_str());
        }

        // Unregister the action
        unregister_action(action_name.c_str());
    }

    registered_actions.clear();
    action_menupaths.clear();
}

void llm_plugin_t::cleanup_window() {
    if (agent_controller) {
        msg("LLM RE: Cleaning up agent controller\n");
        agent_controller->shutdown();
        delete agent_controller;
        agent_controller = nullptr;
    }

    if (main_window && !window_closed) {
        msg("LLM RE: Cleaning up main window\n");

        // Mark as closed to prevent double cleanup
        window_closed = true;

        // Disconnect all signals first
        main_window->disconnect();

        // Close the window
        main_window->close();

        // Delete the window
        delete main_window;
        main_window = nullptr;
    }
}

bool llm_plugin_t::run(size_t arg) {
    if (!shutting_down) {
        show_main_window();
    }
    return true;
}

void llm_plugin_t::show_main_window() {
    if (shutting_down) {
        return;
    }

    if (!config_) {
        msg("LLM RE: Cannot show window - configuration not loaded\n");
        warning("LLM RE Plugin: Configuration file required.\n\n"
                "Please create llm_re_config.json in your IDA user directory\n"
                "with your Anthropic API key and other settings.\n\n"
                "See llm_re_config.json.example for the required format.");
        return;
    }

    if (!main_window || window_closed) {
        // Create window with proper parent - use nullptr for independent window
        main_window = new ui_v2::MainWindow(nullptr);
        window_closed = false;

        // Create and initialize agent controller
        if (!agent_controller) {
            agent_controller = new ui_v2::AgentController(main_window);
            if (!agent_controller->initialize(*config_)) {
                msg("LLM RE: Failed to initialize agent controller\n");
                delete agent_controller;
                agent_controller = nullptr;
                return;
            }

            // Connect controller to UI components
            agent_controller->connectConversationView(main_window->conversationView());
            agent_controller->connectMemoryDock(main_window->memoryDock());
            agent_controller->connectToolDock(main_window->toolDock());
            agent_controller->connectConsoleDock(main_window->consoleDock());
            
            // Pass agent controller to UI controller
            if (main_window->controller()) {
                main_window->controller()->setAgentController(agent_controller);
            }
            
            // Connect agent controller error messages to status bar
            QObject::connect(agent_controller, &ui_v2::AgentController::errorOccurred,
                           [this](const QString& message) {
                               if (main_window) {
                                   main_window->showStatusMessage(message, 5000); // 5 second timeout for debug messages
                               }
                           });
        }

        // Connect destroyed signal to mark window as closed
        QObject::connect(main_window, &QObject::destroyed, [this]() {
            window_closed = true;
            main_window = nullptr;
        });
    }

    if (main_window) {
        main_window->showWindow();
        main_window->bringToFront();
    }
}

void llm_plugin_t::comprehensive_reverse_engineering() {
    if (shutting_down) {
        return;
    }

    show_main_window();

    if (!main_window || !agent_controller) {
        return;
    }

    ea_t ea = get_screen_ea();
    func_t* func = get_func(ea);

    std::string starting_point = "";
    if (func) {
        qstring func_name;
        get_func_name(&func_name, func->start_ea);

        // Format address as hex
        char addr_str[32];
        ::qsnprintf(addr_str, sizeof(addr_str), "0x%llx", func->start_ea);

        starting_point = "Starting from function '" + std::string(func_name.c_str()) +
                        "' at address " + addr_str + ", ";
    }

    std::string task = starting_point + R"(

Begin complete reverse engineering of this binary. Transform it into readable source code through systematic analysis and aggressive typing.

Remember: Define structures immediately when you see patterns (with gaps if needed), update function prototypes to propagate types, and iterate until 95%+ of the code has meaningful names and proper types.

This will take hundreds of iterations. Begin your first pass now.)";

    agent_controller->executeTask(task);
}

void llm_plugin_t::load_config() {
    config_ = &Config::instance();
    
    // Use a proper buffer instead of modifying const memory
    char config_path[QMAXPATH];
    qstrncpy(config_path, get_user_idadir(), sizeof(config_path));
    qstrncat(config_path, "/llm_re_config.json", sizeof(config_path));
    
    if (!config_->load_from_file(config_path)) {
        msg("LLM RE: ERROR - Configuration file not found at: %s\n", config_path);
        msg("LLM RE: Please create a configuration file with your API key and settings.\n");
        msg("LLM RE: See llm_re_config.json.example for the required format.\n");
        config_ = nullptr;  // Clear config to indicate failure
    }
}

// Plugin interface functions
static plugmod_t* idaapi init() {
    // IDA guarantees this is called from main thread
    if (!is_idaq()) {
        msg("LLM RE: This plugin requires IDA with GUI support\n");
        return nullptr;
    }

    // For PLUGIN_MULTI, always create a new instance
    return new llm_plugin_t();
}

} // namespace llm_re

// Plugin description - must be in global namespace for IDA to load it
plugin_t PLUGIN = {
    IDP_INTERFACE_VERSION,
    PLUGIN_MULTI | PLUGIN_FIX,
    llm_re::init,
    nullptr,                                   // term must be nullptr for PLUGIN_MULTI
    nullptr,                                   // run must be nullptr for PLUGIN_MULTI
    "LLM Reverse Engineering Agent",
    "AI-powered reverse engineering agent",
    "LLM RE Agent",
    ""
};