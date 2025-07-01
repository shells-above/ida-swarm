//
// Created by user on 6/30/25.
//

#include <ida.hpp>
#include <idp.hpp>
#include <loader.hpp>
#include <kernwin.hpp>
#include <QtWidgets/QApplication>
#include <QtCore/QTimer>

#include "main_form.h"

namespace llm_re {

// Plugin flags
constexpr int PLUGIN_FLAGS = PLUGIN_FIX | PLUGIN_MULTI;

// Plugin callbacks
static plugmod_t* idaapi init();
static void idaapi term();
static bool idaapi run(size_t arg);

// Plugin description
plugin_t PLUGIN = {
    IDP_INTERFACE_VERSION,
    PLUGIN_FLAGS,
    init,
    term,
    run,
    "LLM Reverse Engineering Assistant",
    "AI-powered reverse engineering assistant",
    "LLM RE Agent",
    "Ctrl+Shift+L"  // Default hotkey
};

// Plugin module
class llm_plugin_t : public plugmod_t {
    MainForm* main_form = nullptr;
    std::vector<action_desc_t> action_descs;

public:
    llm_plugin_t();
    virtual ~llm_plugin_t();

    virtual bool idaapi run(size_t arg) override;

    void show_main_form();
    void register_actions();
    void unregister_actions();

    // Action callbacks
    static int idaapi show_ui_handler(void* user_data, int);
    static int idaapi analyze_function_handler(void* user_data, int);
    static int idaapi analyze_selection_handler(void* user_data, int);
    static int idaapi find_vulnerabilities_handler(void* user_data, int);
    static int idaapi identify_crypto_handler(void* user_data, int);
    static int idaapi explain_code_handler(void* user_data, int);

    void analyze_function();
    void analyze_selection();
    void find_vulnerabilities();
    void identify_crypto();
    void explain_code();
};

// Global plugin instance
static llm_plugin_t* g_plugin = nullptr;

// Action handlers
int idaapi llm_plugin_t::show_ui_handler(void* user_data, int) {
    if (g_plugin) {
        g_plugin->show_main_form();
    }
    return 1;
}

int idaapi llm_plugin_t::analyze_function_handler(void* user_data, int) {
    if (g_plugin) {
        g_plugin->analyze_function();
    }
    return 1;
}

int idaapi llm_plugin_t::analyze_selection_handler(void* user_data, int) {
    if (g_plugin) {
        g_plugin->analyze_selection();
    }
    return 1;
}

int idaapi llm_plugin_t::find_vulnerabilities_handler(void* user_data, int) {
    if (g_plugin) {
        g_plugin->find_vulnerabilities();
    }
    return 1;
}

int idaapi llm_plugin_t::identify_crypto_handler(void* user_data, int) {
    if (g_plugin) {
        g_plugin->identify_crypto();
    }
    return 1;
}

int idaapi llm_plugin_t::explain_code_handler(void* user_data, int) {
    if (g_plugin) {
        g_plugin->explain_code();
    }
    return 1;
}

// Implementation
llm_plugin_t::llm_plugin_t() {
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
    // Define actions
    static const action_desc_t actions[] = {
        ACTION_DESC_LITERAL(
            "llm_re:show_ui",
            "LLM RE Assistant",
            show_ui_handler,
            "Ctrl+Shift+L",
            "Show LLM Reverse Engineering Assistant",
            -1
        ),
        ACTION_DESC_LITERAL(
            "llm_re:analyze_function",
            "Analyze with LLM",
            analyze_function_handler,
            "Ctrl+Shift+A",
            "Analyze current function with LLM",
            -1
        ),
        ACTION_DESC_LITERAL(
            "llm_re:analyze_selection",
            "Analyze Selection with LLM",
            analyze_selection_handler,
            nullptr,
            "Analyze selected code with LLM",
            -1
        ),
        ACTION_DESC_LITERAL(
            "llm_re:find_vulnerabilities",
            "Find Vulnerabilities",
            find_vulnerabilities_handler,
            nullptr,
            "Search for vulnerabilities with LLM",
            -1
        ),
        ACTION_DESC_LITERAL(
            "llm_re:identify_crypto",
            "Identify Cryptography",
            identify_crypto_handler,
            nullptr,
            "Identify cryptographic routines with LLM",
            -1
        ),
        ACTION_DESC_LITERAL(
            "llm_re:explain_code",
            "Explain Code",
            explain_code_handler,
            nullptr,
            "Get LLM explanation of current code",
            -1
        )
    };

    // Register actions
    for (const auto& action : actions) {
        register_action(action);
        action_descs.push_back(action);
    }

    // Add menu items
    attach_action_to_menu("Edit/LLM RE/Show Assistant", "llm_re:show_ui", SETMENU_APP);
    attach_action_to_menu("Edit/LLM RE/Analyze Function", "llm_re:analyze_function", SETMENU_APP);
    attach_action_to_menu("Edit/LLM RE/Analyze Selection", "llm_re:analyze_selection", SETMENU_APP);
    attach_action_to_menu("Edit/LLM RE/Find Vulnerabilities", "llm_re:find_vulnerabilities", SETMENU_APP);
    attach_action_to_menu("Edit/LLM RE/Identify Cryptography", "llm_re:identify_crypto", SETMENU_APP);
    attach_action_to_menu("Edit/LLM RE/Explain Code", "llm_re:explain_code", SETMENU_APP);

    // Add toolbar button
    attach_action_to_toolbar("AnalysisToolBar", "llm_re:show_ui");

    msg("LLM RE: Registered %d actions\n", action_descs.size());
}

void llm_plugin_t::unregister_actions() {
    for (const auto& action : action_descs) {
        unregister_action(action.name);
    }
    action_descs.clear();
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