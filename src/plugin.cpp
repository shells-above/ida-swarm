#include "common.h"
#include "agent.h"
#include "ida_utils.h"

struct log_handler_t;

// Global plugin state
namespace {
    std::unique_ptr<llm_re::REAgent> g_agent;
    std::string g_api_key;
    TWidget* g_log_viewer = nullptr;
    strvec_t g_log_lines;
    std::atomic<bool> g_terminating{false};
    log_handler_t* g_log_handler = nullptr;  // Store the handler
    
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
    PLUGIN_FIX,                  // Plugin flags
    init,                        // Initialize
    term,                        // Terminate
    run,                         // Run
    "LLM-powered reverse engineering agent",  // Comment
    "Analyzes binaries using LLM to answer specific questions",  // Help
    PLUGIN_NAME,                 // Wanted name
    PLUGIN_HOTKEY                // Wanted hotkey
};

// Get current timestamp string
std::string get_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "[%H:%M:%S]");
    return ss.str();
}

// File-only logging for debugging (thread-safe, no UI updates)
void log_to_file_only(const std::string& text) {
    std::string timestamped = get_timestamp() + " " + text;

    // Output to message window
    msg("%s\n", timestamped.c_str());

    // Write to file
    try {
        std::string log_path = std::string(get_user_idadir()) + "/llm_plugin.log";
        std::ofstream log_file(log_path, std::ios::app);
        if (log_file.is_open()) {
            log_file << timestamped << std::endl;
            log_file.flush();
        }
    } catch (...) {
        // Ignore errors
    }
}

// Close handler for log viewer
static void idaapi log_viewer_close(TWidget* cv, void*) {
    // Clear the global pointer when viewer is closed
    if (cv == g_log_viewer) {
        g_log_viewer = nullptr;
        msg("LLM Agent Log viewer closed\n");
    }
}

// Custom viewer handlers
static const custom_viewer_handlers_t log_handlers = {
    nullptr,  // keyboard
    nullptr,  // popup
    nullptr,  // mouse_moved
    nullptr,  // click
    nullptr,  // dblclick
    nullptr,  // current_position_changed
    log_viewer_close,  // close
    nullptr,  // help
    nullptr,  // adjust_place
    nullptr,  // get_place_xcoord
    nullptr,  // location_changed
    nullptr,  // can_navigate
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

// Thread-safe append to log function using your wrapper
void append_to_log(const std::string& text) {
    // Don't update UI if we're terminating
    if (g_terminating) {
        log_to_file_only("[TERM] " + text);
        return;
    }

    // Add timestamp
    std::string timestamped = get_timestamp() + " " + text;

    // Always output to IDA's message window (thread-safe)
    msg("%s\n", timestamped.c_str());

    // Save to log file (thread-safe file I/O)
    try {
        std::string log_path = std::string(get_user_idadir()) + "/llm_plugin.log";
        std::ofstream log_file(log_path, std::ios::app);
        if (log_file.is_open()) {
            log_file << timestamped << std::endl;
            log_file.flush();
            log_file.close();
        }
    } catch (const std::exception& e) {
        // Silently ignore log file errors
    }

    // For UI updates, we need to execute on main thread with MFF_WRITE
    llm_re::IDAUtils::execute_sync_wrapper([timestamped]() {
        // Double-check we're not terminating
        if (g_terminating) return;

        // Add timestamped line to log
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
    }, MFF_WRITE);
}

// Initialize plugin
plugmod_t* idaapi init() {
    // Plugin works in all IDA versions
    if (!is_idaq()) return PLUGIN_SKIP;

    // Reset termination flag
    g_terminating = false;

    // Clear debug log at startup
    try {
        std::string log_path = std::string(get_user_idadir()) + "/llm_plugin3.log";
        std::ofstream log_file(log_path, std::ios::trunc);
        if (log_file.is_open()) {
            log_file << get_timestamp() << " === PLUGIN INIT ===" << std::endl;
        }
    } catch (...) {}

    // Create action handler
    g_log_handler = new log_handler_t();

    // Register action for showing log
    register_action(action_desc_t{
        sizeof(action_desc_t),
        "llm_agent:show_log",
        "Show LLM Agent Log",
        g_log_handler,  // Use stored pointer
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
    // Get task
    qstring task;
    if (!ask_str(&task, HIST_IDENT, "Enter task for the LLM agent:") || task.empty()) {
        warning("Please enter a task for the agent");
        return false;
    }

    // Get API key (with default from saved)
    qstring api_key = g_api_key.c_str();
    if (!ask_str(&api_key, HIST_IDENT, "Enter your Anthropic API key:") || api_key.empty()) {
        warning("Please enter your Anthropic API key");
        return false;
    }

    // Save for next time
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
        append_to_log("started");
    }

    // Set task
    append_to_log("Starting new task: " + std::string(task.c_str()));
    g_agent->set_task(task.c_str());

    return true;
}

// Terminate plugin
void idaapi term() {
    // Set termination flag
    g_terminating = true;

    log_to_file_only("=== TERM START ===");
    log_to_file_only("g_agent exists: " + std::string(g_agent ? "yes" : "no"));
    log_to_file_only("g_log_viewer exists: " + std::string(g_log_viewer ? "yes" : "no"));
    log_to_file_only("g_log_lines size: " + std::to_string(g_log_lines.size()));

    // Stop agent
    if (g_agent) {
        log_to_file_only("Calling g_agent->stop()");
        try {
            g_agent->stop();
            log_to_file_only("g_agent->stop() succeeded");
        } catch (const std::exception& e) {
            log_to_file_only("g_agent->stop() exception: " + std::string(e.what()));
        } catch (...) {
            log_to_file_only("g_agent->stop() unknown exception");
        }

        log_to_file_only("Resetting g_agent");
        try {
            g_agent.reset();
            log_to_file_only("g_agent reset succeeded");
        } catch (...) {
            log_to_file_only("g_agent reset failed");
        }
    }

    log_to_file_only("Starting UI cleanup");

    try {
        llm_re::IDAUtils::execute_sync_wrapper([]() {
            // Write to file from main thread to confirm we're there
            std::ofstream debug_file(std::string(get_user_idadir()) + "/llm_plugin.log", std::ios::app);
            if (debug_file.is_open()) {
                debug_file << get_timestamp() << " [MAIN] In execute_sync_wrapper" << std::endl;

                if (g_log_viewer) {
                    debug_file << get_timestamp() << " [MAIN] Closing widget..." << std::endl;
                    debug_file.flush();

                    close_widget(g_log_viewer, 0);

                    debug_file << get_timestamp() << " [MAIN] Widget closed" << std::endl;
                    g_log_viewer = nullptr;
                }

                debug_file << get_timestamp() << " [MAIN] Clearing lines..." << std::endl;
                g_log_lines.clear();

                debug_file << get_timestamp() << " [MAIN] Unregistering action..." << std::endl;
                unregister_action("llm_agent:show_log");

                debug_file << get_timestamp() << " [MAIN] UI cleanup done" << std::endl;
                debug_file.close();
            }
        }, MFF_WRITE);

        log_to_file_only("execute_sync_wrapper returned");
    } catch (const std::exception& e) {
        log_to_file_only("execute_sync_wrapper exception: " + std::string(e.what()));
    } catch (...) {
        log_to_file_only("execute_sync_wrapper unknown exception");
    }

    log_to_file_only("=== TERM END ===");

    // Reset flag
    g_terminating = false;
}