//
// Created by user on 6/30/25.
//

#include <ida.hpp>
#include <idp.hpp>
#include <loader.hpp>
#include <kernwin.hpp>

#include "main_form.h"

namespace llm_re {

// Plugin flags
constexpr int PLUGIN_FLAGS = PLUGIN_FIX;

// Forward declaration
class llm_plugin_t;
static llm_plugin_t* g_plugin = nullptr;

// Action handler base class for our actions
struct llm_action_handler_t : public action_handler_t {
    llm_plugin_t* plugin;

    llm_action_handler_t(llm_plugin_t* p) : plugin(p) {}

    virtual int idaapi activate(action_activation_ctx_t* ctx) override = 0;

    virtual action_state_t idaapi update(action_update_ctx_t* ctx) override {
        return AST_ENABLE_ALWAYS;
    }
};

// Plugin module
class llm_plugin_t : public plugmod_t {
    MainForm* main_form = nullptr;
    std::vector<qstring> registered_actions;

    // Action handlers as member variables
    struct show_ui_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int idaapi activate(action_activation_ctx_t* ctx) override;
    };

    struct analyze_function_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int idaapi activate(action_activation_ctx_t* ctx) override;
    };

    struct analyze_selection_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int idaapi activate(action_activation_ctx_t* ctx) override;
    };

    struct find_vulnerabilities_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int idaapi activate(action_activation_ctx_t* ctx) override;
    };

    struct identify_crypto_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int idaapi activate(action_activation_ctx_t* ctx) override;
    };

    struct explain_code_ah_t : public llm_action_handler_t {
        using llm_action_handler_t::llm_action_handler_t;
        virtual int idaapi activate(action_activation_ctx_t* ctx) override;
    };

    // Handler instances
    show_ui_ah_t show_ui_handler;
    analyze_function_ah_t analyze_function_handler;
    analyze_selection_ah_t analyze_selection_handler;
    find_vulnerabilities_ah_t find_vulnerabilities_handler;
    identify_crypto_ah_t identify_crypto_handler;
    explain_code_ah_t explain_code_handler;

public:
    llm_plugin_t();
    virtual ~llm_plugin_t();

    virtual bool idaapi run(size_t arg) override;

    void show_main_form();
    void register_actions();
    void unregister_actions();

    void analyze_function();
    void analyze_selection();
    void find_vulnerabilities();
    void identify_crypto();
    void explain_code();
};

// Action handler implementations
int idaapi llm_plugin_t::show_ui_ah_t::activate(action_activation_ctx_t* ctx) {
    plugin->show_main_form();
    return 1;
}

int idaapi llm_plugin_t::analyze_function_ah_t::activate(action_activation_ctx_t* ctx) {
    plugin->analyze_function();
    return 1;
}

int idaapi llm_plugin_t::analyze_selection_ah_t::activate(action_activation_ctx_t* ctx) {
    plugin->analyze_selection();
    return 1;
}

int idaapi llm_plugin_t::find_vulnerabilities_ah_t::activate(action_activation_ctx_t* ctx) {
    plugin->find_vulnerabilities();
    return 1;
}

int idaapi llm_plugin_t::identify_crypto_ah_t::activate(action_activation_ctx_t* ctx) {
    plugin->identify_crypto();
    return 1;
}

int idaapi llm_plugin_t::explain_code_ah_t::activate(action_activation_ctx_t* ctx) {
    plugin->explain_code();
    return 1;
}

// Implementation
llm_plugin_t::llm_plugin_t()
    : show_ui_handler(this),
      analyze_function_handler(this),
      analyze_selection_handler(this),
      find_vulnerabilities_handler(this),
      identify_crypto_handler(this),
      explain_code_handler(this) {
    msg("LLM RE: Plugin initialized\n");
    register_actions();
}

llm_plugin_t::~llm_plugin_t() {
    if (main_form) {
        main_form->close();
        delete main_form;
        main_form = nullptr;
    }
    unregister_actions();
    msg("LLM RE: Plugin terminated\n");
}

bool llm_plugin_t::run(size_t arg) {
    show_main_form();
    return true;
}

void llm_plugin_t::show_main_form() {
    if (!main_form) {
        main_form = new MainForm(nullptr);

        ea_t current_ea = get_screen_ea();
        if (current_ea != BADADDR) {
            main_form->set_current_address(current_ea);
        }
    }

    main_form->show_and_raise();
}

void llm_plugin_t::register_actions() {
    // Define and register actions
    struct {
        const char* name;
        const char* label;
        action_handler_t* handler;
        const char* shortcut;
        const char* tooltip;
        const char* menupath;
    } actions[] = {
        {
            "llm_re:show_ui",
            "LLM RE Agent",
            &show_ui_handler,
            "Ctrl+Shift+L",
            "Show LLM Reverse Engineering Agent",
            "Edit/LLM RE/Show Agent"
        },
        {
            "llm_re:analyze_function",
            "Analyze with LLM",
            &analyze_function_handler,
            "Ctrl+Shift+A",
            "Analyze current function with LLM",
            "Edit/LLM RE/Analyze Function"
        },
        {
            "llm_re:analyze_selection",
            "Analyze Selection with LLM",
            &analyze_selection_handler,
            nullptr,
            "Analyze selected code with LLM",
            "Edit/LLM RE/Analyze Selection"
        },
        {
            "llm_re:find_vulnerabilities",
            "Find Vulnerabilities",
            &find_vulnerabilities_handler,
            nullptr,
            "Search for vulnerabilities with LLM",
            "Edit/LLM RE/Find Vulnerabilities"
        },
        {
            "llm_re:identify_crypto",
            "Identify Cryptography",
            &identify_crypto_handler,
            nullptr,
            "Identify cryptographic routines with LLM",
            "Edit/LLM RE/Identify Cryptography"
        },
        {
            "llm_re:explain_code",
            "Explain Code",
            &explain_code_handler,
            nullptr,
            "Get LLM explanation of current code",
            "Edit/LLM RE/Explain Code"
        }
    };

    // Register each action
    for (const auto& action : actions) {
        action_desc_t desc = {};
        desc.cb = sizeof(action_desc_t);
        desc.name = action.name;
        desc.label = action.label;
        desc.handler = action.handler;
        desc.owner = this;
        desc.shortcut = action.shortcut;
        desc.tooltip = action.tooltip;
        desc.icon = -1;
        desc.flags = ADF_OT_PLUGMOD;  // Owner is a plugmod_t

        if (register_action(desc)) {
            registered_actions.push_back(action.name);

            // Attach to menu
            if (action.menupath) {
                attach_action_to_menu(action.menupath, action.name, SETMENU_APP);
            }
        } else {
            msg("LLM RE: Failed to register action %s\n", action.name);
        }
    }

    // Add toolbar button for main UI
    attach_action_to_toolbar("AnalysisToolBar", "llm_re:show_ui");

    msg("LLM RE: Registered %d actions\n", registered_actions.size());
}

void llm_plugin_t::unregister_actions() {
    for (const qstring& action_name: registered_actions) {
        unregister_action(action_name.c_str());
    }
    registered_actions.clear();
}

void llm_plugin_t::analyze_function() {
    show_main_form();

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
    show_main_form();

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
    show_main_form();

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
    show_main_form();

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
    show_main_form();

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

    g_plugin = new llm_plugin_t();
    return g_plugin;
}

static void idaapi term() {
    if (g_plugin) {
        delete g_plugin;
        g_plugin = nullptr;
    }
}

static bool idaapi run(size_t arg) {
    if (g_plugin) {
        return g_plugin->run(arg);
    }
    return false;
}

} // namespace llm_re

// Plugin description - must be in global namespace for IDA to load it
plugin_t PLUGIN = {
    IDP_INTERFACE_VERSION,
    llm_re::PLUGIN_FLAGS,
    llm_re::init,
    llm_re::term,
    llm_re::run,
    "LLM Reverse Engineering Assistant",
    "AI-powered reverse engineering assistant",
    "LLM RE Agent",
    "Ctrl+Shift+L"
};