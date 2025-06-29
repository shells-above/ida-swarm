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

// Global plugin state
namespace {
    std::unique_ptr<llm_re::REAgent> g_agent;
    std::string g_api_key;
    HWND g_log_window = nullptr;

    // UI constants
    const char* PLUGIN_NAME = "LLM RE Agent";
    const char* PLUGIN_HOTKEY = "Ctrl+Shift+L";
}

// Forward declarations
int idaapi init(void);
bool idaapi run(size_t arg);
void idaapi term(void);

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

// Log window handler
struct log_handler_t : public action_handler_t {
    virtual int idaapi activate(action_activation_ctx_t*) override {
        if (g_log_window && IsWindow(g_log_window)) {
            ShowWindow(g_log_window, SW_SHOW);
            SetForegroundWindow(g_log_window);
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

// Log window procedure
LRESULT CALLBACK LogWindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Create edit control
        HWND hEdit = CreateWindowEx(
            WS_EX_CLIENTEDGE,
            "EDIT",
            "",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 0, 0,
            hwnd,
            (HMENU)1,
            GetModuleHandle(NULL),
            NULL
        );
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)hEdit);

        // Set font
        HFONT hFont = CreateFont(14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Consolas");
        SendMessage(hEdit, WM_SETFONT, (WPARAM)hFont, TRUE);
        break;
    }

    case WM_SIZE: {
        HWND hEdit = (HWND)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        if (hEdit) {
            RECT rect;
            GetClientRect(hwnd, &rect);
            SetWindowPos(hEdit, NULL, 0, 0, rect.right, rect.bottom, SWP_NOZORDER);
        }
        break;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        g_log_window = nullptr;
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// Append text to log window
void append_to_log(const std::string& text) {
    if (!g_log_window || !IsWindow(g_log_window)) return;

    HWND hEdit = (HWND)GetWindowLongPtr(g_log_window, GWLP_USERDATA);
    if (!hEdit) return;

    // Get current text length
    int len = GetWindowTextLength(hEdit);

    // Move caret to end
    SendMessage(hEdit, EM_SETSEL, len, len);

    // Add timestamp
    SYSTEMTIME st;
    GetLocalTime(&st);
    char timestamp[32];
    snprintf(timestamp, sizeof(timestamp), "[%02d:%02d:%02d] ",
        st.wHour, st.wMinute, st.wSecond);

    // Append text
    std::string timestamped = timestamp + text + "\r\n";
    SendMessage(hEdit, EM_REPLACESEL, FALSE, (LPARAM)timestamped.c_str());

    // Scroll to bottom
    SendMessage(hEdit, EM_SCROLLCARET, 0, 0);
}

// Initialize plugin
int idaapi init(void) {
    // Only for x86/x64
    if (!is_idaq()) return PLUGIN_SKIP;

    // Register log window class
    WNDCLASSEX wc = {0};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = LogWindowProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "LLMAgentLogWindow";
    RegisterClassEx(&wc);

    // Register action for showing log
    register_action(action_desc_t{
        "llm_agent:show_log",
        "Show LLM Agent Log",
        new log_handler_t(),
        PLUGIN_HOTKEY,
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

    // Create log window if needed
    if (!g_log_window || !IsWindow(g_log_window)) {
        g_log_window = CreateWindowEx(
            WS_EX_TOOLWINDOW,
            "LLMAgentLogWindow",
            "LLM Agent Log",
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT, 800, 600,
            NULL, NULL, GetModuleHandle(NULL), NULL
        );
    }

    // Show log window
    ShowWindow(g_log_window, SW_SHOW);

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

    // Destroy log window
    if (g_log_window && IsWindow(g_log_window)) {
        DestroyWindow(g_log_window);
    }

    // Unregister action
    unregister_action("llm_agent:show_log");

    msg("%s plugin terminated\n", PLUGIN_NAME);
}

// Plugin entry point
#ifdef __NT__
__declspec(dllexport)
#endif
plugin_t* PLUGIN_ENTRY(void) {
    return &PLUGIN;
}