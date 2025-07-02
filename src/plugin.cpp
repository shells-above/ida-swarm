//
// Created by user on 6/30/25.
//

#include <ida.hpp>
#include <idp.hpp>
#include <loader.hpp>
#include <kernwin.hpp>
#include <map>

#include "main_form.h"

namespace llm_re {

// Plugin module
class llm_plugin_t : public plugmod_t, public event_listener_t {
    MainForm* main_form = nullptr;
    std::vector<qstring> registered_actions;
    std::map<qstring, qstring> action_menupaths;  // Store menu paths for detaching
    bool shutting_down = false;

    // Action handler that checks if plugin is still valid
    struct llm_action_handler_t : public action_handler_t {
        llm_plugin_t* plugin;

        llm_action_handler_t(llm_plugin_t* p) : plugin(p) {}

        bool is_valid() const {
            return plugin && !plugin->shutting_down;
        }

        virtual int idaapi activate(action_activation_ctx_t* ctx) override {
            if (!is_valid()) {
                return 0;
            }
            return do_activate(ctx);
        }

        virtual int do_activate(action_activation_ctx_t* ctx) = 0;

        virtual action_state_t idaapi update(action_update_ctx_t* ctx) override {
            if (!is_valid()) {
                return AST_DISABLE;
            }
            return AST_ENABLE_ALWAYS;
        }
    };

    // Action handlers as member variables
    struct show_ui_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int do_activate(action_activation_ctx_t* ctx) override {
            plugin->show_main_form();
            return 1;
        }
    };

    struct analyze_function_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int do_activate(action_activation_ctx_t* ctx) override {
            plugin->analyze_function();
            return 1;
        }
    };

    struct analyze_selection_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int do_activate(action_activation_ctx_t* ctx) override {
            plugin->analyze_selection();
            return 1;
        }
    };

    struct find_vulnerabilities_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int do_activate(action_activation_ctx_t* ctx) override {
            plugin->find_vulnerabilities();
            return 1;
        }
    };

    struct identify_crypto_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int do_activate(action_activation_ctx_t* ctx) override {
            plugin->identify_crypto();
            return 1;
        }
    };

    struct explain_code_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int do_activate(action_activation_ctx_t* ctx) override {
            plugin->explain_code();
            return 1;
        }
    };

    // Handler instances
    show_ui_ah_t* show_ui_handler = nullptr;
    analyze_function_ah_t* analyze_function_handler = nullptr;
    analyze_selection_ah_t* analyze_selection_handler = nullptr;
    find_vulnerabilities_ah_t* find_vulnerabilities_handler = nullptr;
    identify_crypto_ah_t* identify_crypto_handler = nullptr;
    explain_code_ah_t* explain_code_handler = nullptr;

public:
    llm_plugin_t();
    virtual ~llm_plugin_t();

    // plugmod_t virtual functions
    virtual bool idaapi run(size_t arg) override;

    // event_listener_t virtual function
    virtual ssize_t idaapi on_event(ssize_t code, va_list va) override;

    void show_main_form();
    void register_actions();
    void unregister_actions();
    void cleanup_form();

    void analyze_function();
    void analyze_selection();
    void find_vulnerabilities();
    void identify_crypto();
    void explain_code();
};

// Implementation
llm_plugin_t::llm_plugin_t() {
    msg("LLM RE: Plugin initialized for IDB\n");

    // Hook UI events to detect when IDA is closing
    hook_event_listener(HT_UI, this);

    // Create handler instances
    show_ui_handler = new show_ui_ah_t(this);
    analyze_function_handler = new analyze_function_ah_t(this);
    analyze_selection_handler = new analyze_selection_ah_t(this);
    find_vulnerabilities_handler = new find_vulnerabilities_ah_t(this);
    identify_crypto_handler = new identify_crypto_ah_t(this);
    explain_code_handler = new explain_code_ah_t(this);

    // Register actions after handlers are created
    register_actions();
}

llm_plugin_t::~llm_plugin_t() {
    msg("LLM RE: Plugin cleanup started\n");

    shutting_down = true;

    // Unhook events first
    unhook_event_listener(HT_UI, this);

    // Clean up form
    cleanup_form();

    // Unregister actions before deleting handlers
    unregister_actions();

    // Delete handler instances
    delete show_ui_handler;
    delete analyze_function_handler;
    delete analyze_selection_handler;
    delete find_vulnerabilities_handler;
    delete identify_crypto_handler;
    delete explain_code_handler;

    // Set pointers to nullptr for safety
    show_ui_handler = nullptr;
    analyze_function_handler = nullptr;
    analyze_selection_handler = nullptr;
    find_vulnerabilities_handler = nullptr;
    identify_crypto_handler = nullptr;
    explain_code_handler = nullptr;

    msg("LLM RE: Plugin terminated\n");
}

ssize_t idaapi llm_plugin_t::on_event(ssize_t code, va_list va) {
    switch (code) {
        case ui_database_inited:
            // Database fully loaded
            break;
        case ui_ready_to_run:
            // UI is ready
            break;
        case ui_database_closed:
            // Database is being closed
            cleanup_form();
            break;
    }
    return 0;
}

void llm_plugin_t::cleanup_form() {
    if (main_form) {
        // Disconnect any signals first
        main_form->disconnect();

        // Schedule for deletion using Qt's deleteLater
        // This is safer than immediate deletion
        main_form->deleteLater();
        main_form = nullptr;
    }
}

bool llm_plugin_t::run(size_t arg) {
    if (!shutting_down) {
        show_main_form();
    }
    return true;
}

void llm_plugin_t::show_main_form() {
    if (shutting_down) {
        return;
    }

    if (!main_form) {
        // Create form without parent - Qt will handle it properly
        main_form = new MainForm(nullptr);

        // Set window flags to ensure proper cleanup
        main_form->setWindowFlags(main_form->windowFlags() | Qt::Window);
        main_form->setAttribute(Qt::WA_DeleteOnClose, false);  // We'll manage deletion

        ea_t current_ea = get_screen_ea();
        if (current_ea != BADADDR) {
            main_form->set_current_address(current_ea);
        }
    }

    if (main_form) {
        main_form->show();
        main_form->raise();
        main_form->activateWindow();
    }
}

void llm_plugin_t::register_actions() {
    // Generate a unique prefix for this instance
    char prefix[32];
    ::qsnprintf(prefix, sizeof(prefix), "llm_re_%p", this);

    // Define and register actions
    struct {
        const char* base_name;
        const char* label;
        action_handler_t* handler;
        const char* shortcut;
        const char* tooltip;
        const char* menupath;
        bool use_global_shortcut;  // Whether to use global shortcut
    } actions[] = {
        {
            "show_ui",
            "LLM RE Agent",
            show_ui_handler,
            "Ctrl+Shift+L",
            "Show LLM Reverse Engineering Agent",
            "Edit/LLM RE/Show Agent",
            true  // Main UI action gets global shortcut
        },
        {
            "analyze_function",
            "Analyze with LLM",
            analyze_function_handler,
            "Ctrl+Shift+A",
            "Analyze current function with LLM",
            "Edit/LLM RE/Analyze Function",
            false  // Local shortcut only
        },
        {
            "analyze_selection",
            "Analyze Selection with LLM",
            analyze_selection_handler,
            nullptr,
            "Analyze selected code with LLM",
            "Edit/LLM RE/Analyze Selection",
            false
        },
        {
            "find_vulnerabilities",
            "Find Vulnerabilities",
            find_vulnerabilities_handler,
            nullptr,
            "Search for vulnerabilities with LLM",
            "Edit/LLM RE/Find Vulnerabilities",
            false
        },
        {
            "identify_crypto",
            "Identify Cryptography",
            identify_crypto_handler,
            nullptr,
            "Identify cryptographic routines with LLM",
            "Edit/LLM RE/Identify Cryptography",
            false
        },
        {
            "explain_code",
            "Explain Code",
            explain_code_handler,
            nullptr,
            "Get LLM explanation of current code",
            "Edit/LLM RE/Explain Code",
            false
        }
    };

    // Keep track of whether we've registered the main action globally
    static bool global_actions_registered = false;

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
        if (action.use_global_shortcut && !global_actions_registered) {
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
    if (!global_actions_registered && !registered_actions.empty()) {
        global_actions_registered = true;
    }

    // Add toolbar button for main UI (use the first registered action)
    if (!registered_actions.empty()) {
        attach_action_to_toolbar("AnalysisToolBar", registered_actions[0].c_str());
    }

    msg("LLM RE: Registered %d actions\n", registered_actions.size());
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

void llm_plugin_t::analyze_function() {
    if (shutting_down) {
        return;
    }

    show_main_form();

    if (!main_form) {
        return;
    }

    ea_t ea = get_screen_ea();
    func_t* func = get_func(ea);

    if (!func) {
        warning("No function at current address");
        return;
    }

    qstring func_name;
    get_func_name(&func_name, func->start_ea);

    std::string task = "Analyze the function '" + std::string(func_name.c_str()) +
                      "' at address " + std::to_string(func->start_ea) + ". " +
                      "Provide:\n"
                      "1. Function purpose and behavior\n"
                      "2. Parameter analysis\n"
                      "3. Return value analysis\n"
                      "4. Key algorithms or logic\n"
                      "5. Potential issues or vulnerabilities";

    main_form->set_current_address(func->start_ea);
    main_form->execute_task(task);
}

void llm_plugin_t::analyze_selection() {
    if (shutting_down) {
        return;
    }

    show_main_form();

    if (!main_form) {
        return;
    }

    ea_t start_ea = BADADDR, end_ea = BADADDR;

    if (read_range_selection(nullptr, &start_ea, &end_ea)) {
        std::string task = "Analyze the code from " + std::to_string(start_ea) +
                          " to " + std::to_string(end_ea) + ". " +
                          "Explain what this code does and identify any interesting patterns or issues.";

        main_form->set_current_address(start_ea);
        main_form->execute_task(task);
    } else {
        warning("No selection found");
    }
}

void llm_plugin_t::find_vulnerabilities() {
    if (shutting_down) {
        return;
    }

    show_main_form();

    if (!main_form) {
        return;
    }

    std::string task =
        "Search for potential security vulnerabilities in the current binary. "
        "Focus on:\n"
        "- Buffer overflows\n"
        "- Format string bugs\n"
        "- Integer overflows\n"
        "- Use after free\n"
        "- Race conditions\n"
        "- Insecure API usage\n"
        "Provide specific addresses and explanations for any findings.";

    main_form->execute_task(task);
}

void llm_plugin_t::identify_crypto() {
    if (shutting_down) {
        return;
    }

    show_main_form();

    if (!main_form) {
        return;
    }

    std::string task =
        "Identify cryptographic algorithms and routines in this binary. "
        "Look for:\n"
        "- Encryption/decryption functions\n"
        "- Hash functions (MD5, SHA, etc.)\n"
        "- Key generation or management\n"
        "- Common crypto constants\n"
        "- Custom crypto implementations\n"
        "For each finding, provide the address and identify the algorithm if possible.";

    main_form->execute_task(task);
}

void llm_plugin_t::explain_code() {
    if (shutting_down) {
        return;
    }

    show_main_form();

    if (!main_form) {
        return;
    }

    ea_t ea = get_screen_ea();
    func_t* func = get_func(ea);

    if (func) {
        std::string task = "Explain what the code at address " + std::to_string(ea) + " does. " +
                          "Provide a clear, concise explanation suitable for documentation.";
        main_form->set_current_address(ea);
        main_form->execute_task(task);
    } else {
        std::string task = "Explain what is at address " + std::to_string(ea) + ". " +
                          "If it's code, explain what it does. " +
                          "If it's data, explain its purpose and structure.";
        main_form->set_current_address(ea);
        main_form->execute_task(task);
    }
}

// Plugin interface functions
static plugmod_t* idaapi init() {
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
    PLUGIN_MULTI,                              // Use only PLUGIN_MULTI
    llm_re::init,
    nullptr,                                   // term must be nullptr for PLUGIN_MULTI
    nullptr,                                   // run must be nullptr for PLUGIN_MULTI
    "LLM Reverse Engineering Agent",
    "AI-powered reverse engineering agent",
    "LLM RE Agent",
    ""
};