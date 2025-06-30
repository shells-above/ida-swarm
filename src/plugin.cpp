// #include <iostream>
//
// #include <pro.h>
// #include <prodir.h>
// #include <ida.hpp>
// #include <auto.hpp>
// #include <expr.hpp>
// #include <name.hpp>
// #include <undo.hpp>
// #include <name.hpp>
// #include <diskio.hpp>
// #include <loader.hpp>
// #include <dirtree.hpp>
// #include <kernwin.hpp>
// #include <segment.hpp>
// #include <parsejson.hpp>
//
// // CORRECT IDA PRO 9 C++ PLUGIN CREATION:
// // Define the class that inherits from plugmod_t
// class MyPlugmod : public plugmod_t
// {
// public:
//     // Constructor
//     MyPlugmod()
//     {
//         msg("MyPlugmod: Constructor called.\n");
//     }
//
//     // Destructor
//     virtual ~MyPlugmod()
//     {
//         msg("MyPlugmod: Destructor called.\n");
//     }
//
//     // Method that gets called when the plugin is activated
//     virtual bool idaapi run(size_t arg) override
//     {
//         msg("MyPlugmod.run() called with arg: %zu\n", arg);
//
//         // Add some actual functionality to test
//         msg("Plugin is working! Current database: %s\n", get_path(PATH_TYPE_IDB));
//
//         return true;
//     }
// };
//
// static plugmod_t* idaapi init(void)
// {
//     msg("Plugin init() called\n");
//     return new MyPlugmod();
// }
//
// static void idaapi term(void)
// {
//     msg("Plugin term() called\n");
// }
//
// static bool idaapi run(size_t arg)
// {
//     msg("Plugin run() called with arg: %zu\n", arg);
//     return false; // This shouldn't be called with PLUGIN_MULTI
// }
//
// plugin_t PLUGIN = {
//     IDP_INTERFACE_VERSION,
//     PLUGIN_FIX,         // plugin flags
//     init,                 // initialize
//     term,                 // terminate. this pointer can be nullptr
//     run,                  // invoke the plugin
//     "List functions plugin", // long comment about the plugin
//     "This plugin demonstrates basic functionality", // multiline help about the plugin
//     "List functions",     // the preferred short name of the plugin
//     "Ctrl-Shift-L"        // the preferred hotkey to run the plugin
// };

#include "common.h"
#include "agent.h"
#include <sstream>
#include <chrono>
#include <iomanip>

// Global plugin state
namespace {
    std::unique_ptr<llm_re::REAgent> g_agent;
    std::string g_api_key;
    TWidget* g_log_viewer = nullptr;
    strvec_t g_log_lines;
    
    // UI constants
    const char* PLUGIN_NAME = "LLM RE Agent";
    const char* PLUGIN_HOTKEY = "Ctrl+Shift+L";
    const char* LOG_VIEW_TITLE = "LLM Agent Log";
}

// Forward declarations
plugmod_t* idaapi init();
void idaapi term();
bool idaapi run(size_t arg);

// Plugin description
plugin_t PLUGIN = {
    IDP_INTERFACE_VERSION,
    PLUGIN_MULTI | PLUGIN_FIX,  // Plugin flags
    init,                        // Initialize
    term,                        // Terminate
    run,                         // Run
    "LLM-powered reverse engineering agent",  // Comment
    "Analyzes binaries using LLM to answer specific questions",  // Help
    PLUGIN_NAME,                 // Wanted name
    PLUGIN_HOTKEY               // Wanted hotkey
};

// Get current timestamp string
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "[%H:%M:%S]");
    return ss.str();
}

// Custom viewer handlers
static const custom_viewer_handlers_t log_handlers = {
    nullptr,  // keyboard
    nullptr,  // popup
    nullptr,  // mouse_moved
    nullptr,  // click
    nullptr,  // dblclick
    nullptr,  // current_position_changed
    nullptr,  // close
    nullptr,  // help
    nullptr   // adjust_place
};

// Log window handler
struct log_handler_t : public action_handler_t {
    virtual int idaapi activate(action_activation_ctx_t*) override {
        if (g_log_viewer) {
            // Bring existing viewer to front
            activate_widget(g_log_viewer, true);
        } else {
            // Create new log viewer
            simpleline_place_t s1;
            simpleline_place_t s2(g_log_lines.size() > 0 ? g_log_lines.size() - 1 : 0);
            g_log_viewer = create_custom_viewer(
                LOG_VIEW_TITLE,
                &s1, &s2, &s1,
                nullptr,
                &g_log_lines,
                &log_handlers,
                nullptr
            );
            
            if (g_log_viewer) {
                display_widget(g_log_viewer, WOPN_DP_TAB | WOPN_RESTORE);
            }
        }
        return 1;
    }

    virtual action_state_t idaapi update(action_update_ctx_t*) override {
        return AST_ENABLE_ALWAYS;
    }
};

// Task input form
static const char task_form[] =
    "LLM Reverse Engineering Agent\n"
    "<Task:q:1:50:100::>\n"
    "<API Key:q:2:50:100::>\n";

// Append text to log viewer
void append_to_log(const std::string& text) {
    // Add timestamped line to log
    std::string timestamped = get_timestamp() + " " + text;
    
    simpleline_t line;
    line.line = timestamped.c_str();
    g_log_lines.push_back(line);
    
    // Keep log size reasonable
    if (g_log_lines.size() > 1000) {
        g_log_lines.erase(g_log_lines.begin());
    }
    
    // Update viewer if it exists
    if (g_log_viewer) {
        // Refresh the viewer
        refresh_custom_viewer(g_log_viewer);
        
        // Scroll to bottom
        simpleline_place_t bottom(g_log_lines.size() - 1);
        jumpto(g_log_viewer, &bottom, 0, 0);
    }
    
    // Also output to IDA's message window
    msg("%s\n", timestamped.c_str());
}

// Initialize plugin
plugmod_t* idaapi init() {
    // Plugin works in all IDA versions
    if (!is_idaq()) return PLUGIN_SKIP;

    // Register action for showing log
    register_action(action_desc_t{
        sizeof(action_desc_t),
        "llm_agent:show_log",
        "Show LLM Agent Log",
        new log_handler_t(),
        &PLUGIN,
        nullptr,
        "Show the LLM agent log window",
        -1
    });

    // Add menu item
    attach_action_to_menu("View/Open subviews/", "llm_agent:show_log", SETMENU_APP);

    msg("%s plugin initialized\n", PLUGIN_NAME);
    return PLUGIN_KEEP;
}

// Run plugin
bool idaapi run(size_t arg) {
    // Get task from user
    qstring task, api_key;

    // Load saved API key if available
    if (!g_api_key.empty()) {
        api_key = g_api_key.c_str();
    }

    if (!ask_form(task_form, &task, &api_key)) {
        return false;
    }

    if (task.empty()) {
        warning("Please enter a task for the agent");
        return false;
    }

    if (api_key.empty()) {
        warning("Please enter your Anthropic API key");
        return false;
    }

    // Save API key
    g_api_key = api_key.c_str();

    // Create log viewer if it doesn't exist
    if (!g_log_viewer) {
        // Execute the show log action to create the viewer
        process_ui_action("llm_agent:show_log");
    } else {
        // Bring existing viewer to front
        activate_widget(g_log_viewer, true);
    }

    // Create or restart agent
    if (!g_agent) {
        g_agent = std::make_unique<llm_re::REAgent>(g_api_key);
        g_agent->set_log_callback(append_to_log);
        g_agent->start();
    }

    // Set task
    append_to_log("Starting new task: " + std::string(task.c_str()));
    g_agent->set_task(task.c_str());

    return true;
}

// Terminate plugin
void idaapi term(void) {
    // Stop agent
    if (g_agent) {
        g_agent->stop();
        g_agent.reset();
    }

    // Close log viewer if open
    if (g_log_viewer) {
        close_widget(g_log_viewer, 0);
        g_log_viewer = nullptr;
    }

    // Clear log lines
    g_log_lines.clear();

    // Unregister action
    unregister_action("llm_agent:show_log");

    msg("%s plugin terminated\n", PLUGIN_NAME);
}