#include "plugin.h"
#include "common.h"
#include "agent.h"
#include "ida_utils.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <mutex>
#include <algorithm>

// UI constants definitions
const char* PLUGIN_NAME = "LLM RE Agent";
const char* PLUGIN_HOTKEY = "Ctrl+Shift+L";
const char* LOG_VIEW_TITLE = "LLM Agent Log";
const char* LLM_MSG_VIEW_TITLE = "LLM Messages";

// Global plugin state
namespace {
    std::unique_ptr<llm_re::REAgent> g_agent;
    std::string g_api_key;

    // normal logs for the user to see in the pop up
    TWidget* g_log_viewer = nullptr;
    strvec_t g_log_lines;
    std::atomic<bool> g_terminating{false};
    log_handler_t* g_log_handler = nullptr;  // Store the handler

    // in depth logs of the llms actual messages
    TWidget* g_llm_msg_viewer = nullptr;
    strvec_t g_llm_msg_lines;
    llm_msg_handler_t* g_llm_msg_handler = nullptr;  // Store the handler
    std::atomic<int> g_llm_msg_counter{0};
    std::ofstream g_llm_msg_file;

    // New handlers for resume/continue
    resume_handler_t* g_resume_handler = nullptr;
    continue_handler_t* g_continue_handler = nullptr;
}

// Forward declarations
void format_llm_message_for_ida(const std::string& direction, const json& message, strvec_t& lines);
void log_llm_message(const std::string& direction, const json& message, int iteration = -1);
void format_json_for_display(const json& j, strvec_t& lines, int indent = 0);

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

// Close handler for log viewer
static void idaapi log_viewer_close(TWidget* cv, void*) {
    // Clear the global pointer when viewer is closed
    if (cv == g_log_viewer) {
        g_log_viewer = nullptr;
        msg("LLM Agent Log viewer closed\n");
    }
}

// LLM message viewer close handler
static void idaapi llm_msg_viewer_close(TWidget* cv, void*) {
    if (cv == g_llm_msg_viewer) {
        g_llm_msg_viewer = nullptr;
        msg("LLM Msg Log viewer closed\n");
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

// LLM message viewer handlers
static const custom_viewer_handlers_t llm_msg_handlers = {
    nullptr,  // keyboard
    nullptr,  // popup
    nullptr,  // mouse_moved
    nullptr,  // click
    nullptr,  // dblclick
    nullptr,  // current_position_changed
    llm_msg_viewer_close,  // close
    nullptr,  // help
    nullptr,  // adjust_place
    nullptr,  // get_place_xcoord
    nullptr,  // location_changed
    nullptr,  // can_navigate
};

// Resume handler - for resuming after API errors
int idaapi resume_handler_t::activate(action_activation_ctx_t*) {
    if (!g_agent || !g_agent->is_paused()) {
        warning("No paused task to resume");
        return 0;
    }

    append_to_log("Resuming paused task...");
    g_agent->resume();
    update_ui_state();
    return 1;
}

action_state_t idaapi resume_handler_t::update(action_update_ctx_t*) {
    if (g_agent && g_agent->is_paused()) {
        return AST_ENABLE;
    }
    return AST_DISABLE;
}

// Continue handler - for continuing after final report
int idaapi continue_handler_t::activate(action_activation_ctx_t*) {
    if (!g_agent || (!g_agent->is_completed() && !g_agent->is_idle())) {
        warning("Agent must complete a task before continuing");
        return 0;
    }

    // Get additional instructions from user
    qstring additional_task;
    if (!ask_str(&additional_task, HIST_CMT, "Enter additional instructions for the agent:") || additional_task.empty()) {
        return 0;
    }

    append_to_log("Continuing with new instructions: " + std::string(additional_task.c_str()));
    g_agent->continue_with_task(additional_task.c_str());
    update_ui_state();
    return 1;
}

action_state_t idaapi continue_handler_t::update(action_update_ctx_t*) {
    if (g_agent && (g_agent->is_completed() || g_agent->is_idle())) {
        return AST_ENABLE;
    }
    return AST_DISABLE;
}

// Log window handler
int idaapi log_handler_t::activate(action_activation_ctx_t*) {
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

action_state_t idaapi log_handler_t::update(action_update_ctx_t*) {
    return AST_ENABLE_ALWAYS;
}

// Show LLM messages window handler
int idaapi llm_msg_handler_t::activate(action_activation_ctx_t*) {
    if (g_llm_msg_viewer) {
        activate_widget(g_llm_msg_viewer, true);
    } else {
        simpleline_place_t s1;
        simpleline_place_t s2(g_llm_msg_lines.size() > 0 ? g_llm_msg_lines.size() - 1 : 0);
        g_llm_msg_viewer = create_custom_viewer(
            LLM_MSG_VIEW_TITLE,
            &s1, &s2, &s1,
            nullptr,
            &g_llm_msg_lines,
            &llm_msg_handlers,
            nullptr
        );

        if (g_llm_msg_viewer) {
            display_widget(g_llm_msg_viewer, WOPN_DP_TAB | WOPN_RESTORE);
        }
    }
    return 1;
}

action_state_t idaapi llm_msg_handler_t::update(action_update_ctx_t*) {
    return AST_ENABLE_ALWAYS;
}

// Update UI state based on agent state
void update_ui_state() {
    if (!g_agent) return;

    // Update action states by refreshing the UI
    refresh_idaview_anyway();

    // Add status to log
    std::string status = "Agent Status: ";
    if (g_agent->is_running()) {
        status += "Running";
    } else if (g_agent->is_paused()) {
        status += "Paused (API Error - use 'Resume' to continue)";
    } else if (g_agent->is_completed()) {
        status += "Completed (use 'Continue' to add more instructions)";
    } else {
        status += "Idle";
    }

    append_to_log(status);
}

// Format JSON for display with syntax coloring
void format_llm_message_for_ida(const std::string& direction, const json& message, strvec_t& lines) {
    if (direction == "REQUEST") {
        // For requests, show ALL new messages since the last request
        if (message.contains("messages") && message["messages"].is_array() && !message["messages"].empty()) {
            const nlohmann::basic_json<>& messages = message["messages"];

            // Find the most recent messages that haven't been shown yet
            // We'll show the last few messages to provide context
            int msgs_to_show = 1;  // Show last message
            int start_idx = std::max(0, (int)messages.size() - msgs_to_show);

            // Show each message in the request
            for (int i = start_idx; i < (int)messages.size(); i++) {
                nlohmann::basic_json<> msg = messages[i];
                if (!msg.contains("role")) continue;

                std::string role = msg["role"].get<std::string>();

                // Add a separator between messages
                if (i > start_idx) {
                    simpleline_t sep;
                    sep.line = "";
                    lines.push_back(sep);
                }

                // Check if this is a user message with tool_result content
                if (role == "user" && msg.contains("content") && msg["content"].is_array()) {
                    bool has_tool_result = false;
                    for (const nlohmann::basic_json<> &content_item: msg["content"]) {
                        if (content_item.contains("type") && content_item["type"] == "tool_result") {
                            has_tool_result = true;

                            simpleline_t role_line;
                            role_line.line = "Tool Result:";
                            role_line.color = COLOR_KEYWORD;
                            lines.push_back(role_line);

                            if (content_item.contains("tool_use_id")) {
                                std::string tool_use_id = content_item["tool_use_id"].get<std::string>();
                                simpleline_t id_line;
                                id_line.line = ("Tool Call ID: " + tool_use_id).c_str();
                                id_line.color = COLOR_IMPNAME;
                                lines.push_back(id_line);
                            }

                            // Show the tool result content
                            if (content_item.contains("content") && content_item["content"].is_string()) {
                                std::string content = content_item["content"].get<std::string>();

                                // Try to parse as JSON for better display
                                try {
                                    json parsed_content = json::parse(content);

                                    // Show raw JSON in a formatted way
                                    std::string json_str = parsed_content.dump();
                                    if (json_str.length() > 300) {
                                        json_str = json_str.substr(0, 297) + "...";
                                    }

                                    simpleline_t result_line;
                                    result_line.line = ("Result: " + json_str).c_str();
                                    result_line.color = COLOR_DEFAULT;
                                    lines.push_back(result_line);
                                } catch (...) {
                                    // If not JSON, show as plain text
                                    if (content.length() > 300) {
                                        content = content.substr(0, 297) + "...";
                                    }

                                    simpleline_t result_line;
                                    result_line.line = ("Result: " + content).c_str();
                                    result_line.color = COLOR_DEFAULT;
                                    lines.push_back(result_line);
                                }
                            }
                            break;  // We found and processed the tool result
                        }
                    }

                    // If no tool result found, show as regular user message
                    if (!has_tool_result) {
                        simpleline_t role_line;
                        role_line.line = ("Role: " + role).c_str();
                        role_line.color = COLOR_KEYWORD;
                        lines.push_back(role_line);

                        // Show any text content
                        for (const nlohmann::basic_json<> &content_item: msg["content"]) {
                            if (content_item.contains("type") && content_item["type"] == "text" &&
                                content_item.contains("text")) {
                                std::string text = content_item["text"].get<std::string>();
                                // Split long content into multiple lines
                                size_t pos = 0;
                                while (pos < text.length()) {
                                    size_t line_end = std::min(pos + 80, text.length());
                                    if (line_end < text.length()) {
                                        size_t space_pos = text.find_last_of(' ', line_end);
                                        if (space_pos != std::string::npos && space_pos > pos) {
                                            line_end = space_pos;
                                        }
                                    }

                                    simpleline_t content_line;
                                    content_line.line = text.substr(pos, line_end - pos).c_str();
                                    content_line.color = COLOR_DEFAULT;
                                    lines.push_back(content_line);

                                    pos = line_end + (line_end < text.length() && text[line_end] == ' ' ? 1 : 0);
                                }
                            }
                        }
                    }
                } else if (role == "assistant" && msg.contains("tool_calls")) {
                    simpleline_t role_line;
                    role_line.line = "Role: assistant (with tool calls)";
                    role_line.color = COLOR_KEYWORD;
                    lines.push_back(role_line);

                    // Show tool calls from assistant
                    for (const nlohmann::basic_json<> &tool_call: msg["tool_calls"]) {
                        if (tool_call.contains("name") && tool_call.contains("id")) {
                            std::string tool_name = tool_call["name"];
                            std::string tool_id = tool_call["id"];

                            simpleline_t tool_call_line;
                            tool_call_line.line = ("→ Tool Call: " + tool_name + " (ID: " + tool_id + ")").c_str();
                            tool_call_line.color = COLOR_IMPNAME;
                            lines.push_back(tool_call_line);

                            if (tool_call.contains("input")) {
                                std::string args = tool_call["input"].dump();
                                if (args.length() > 100) {
                                    args = args.substr(0, 97) + "...";
                                }
                                simpleline_t args_line;
                                args_line.line = ("  Args: " + args).c_str();
                                args_line.color = COLOR_DEFAULT;
                                lines.push_back(args_line);
                            }
                        }
                    }
                } else {
                    // Regular message (user, system, etc.)
                    simpleline_t role_line;
                    role_line.line = ("Role: " + role).c_str();
                    role_line.color = COLOR_KEYWORD;
                    lines.push_back(role_line);

                    // Handle content based on type
                    if (msg.contains("content")) {
                        if (msg["content"].is_string()) {
                            std::string content = msg["content"].get<std::string>();
                            // Split long content into multiple lines
                            size_t pos = 0;
                            while (pos < content.length()) {
                                size_t line_end = std::min(pos + 80, content.length());
                                // Try to break at word boundary
                                if (line_end < content.length()) {
                                    size_t space_pos = content.find_last_of(' ', line_end);
                                    if (space_pos != std::string::npos && space_pos > pos) {
                                        line_end = space_pos;
                                    }
                                }

                                simpleline_t content_line;
                                content_line.line = content.substr(pos, line_end - pos).c_str();
                                content_line.color = COLOR_DEFAULT;
                                lines.push_back(content_line);

                                pos = line_end + (line_end < content.length() && content[line_end] == ' ' ? 1 : 0);
                            }
                        }
                    }
                }
            }
        }
    } else if (direction == "RESPONSE") {
        // For responses, show the assistant's reply and any tool calls
        if (message.contains("content") && message["content"].is_array()) {
            for (const nlohmann::basic_json<> &content_item: message["content"]) {
                if (content_item.contains("type")) {
                    std::string type = content_item["type"].get<std::string>();

                    if (type == "text" && content_item.contains("text")) {
                        std::string text = content_item["text"].get<std::string>();

                        simpleline_t type_line;
                        type_line.line = "Assistant:";
                        type_line.color = COLOR_KEYWORD;
                        lines.push_back(type_line);

                        // Split text into lines
                        size_t pos = 0;
                        while (pos < text.length()) {
                            size_t line_end = std::min(pos + 80, text.length());
                            if (line_end < text.length()) {
                                size_t space_pos = text.find_last_of(' ', line_end);
                                if (space_pos != std::string::npos && space_pos > pos) {
                                    line_end = space_pos;
                                }
                            }

                            simpleline_t text_line;
                            text_line.line = text.substr(pos, line_end - pos).c_str();
                            text_line.color = COLOR_DEFAULT;
                            lines.push_back(text_line);

                            pos = line_end + (line_end < text.length() && text[line_end] == ' ' ? 1 : 0);
                        }
                    } else if (type == "tool_use" && content_item.contains("name")) {
                        std::string tool_name = content_item["name"].get<std::string>();
                        std::string tool_id = "";
                        if (content_item.contains("id")) {
                            tool_id = content_item["id"].get<std::string>();
                        }

                        simpleline_t tool_line;
                        if (!tool_id.empty()) {
                            tool_line.line = ("Tool: " + tool_name + " (ID: " + tool_id + ")").c_str();
                        } else {
                            tool_line.line = ("Tool: " + tool_name).c_str();
                        }
                        tool_line.color = COLOR_IMPNAME;
                        lines.push_back(tool_line);

                        // Show arguments if present
                        if (content_item.contains("input")) {
                            std::string args = content_item["input"].dump();
                            // Truncate very long arguments
                            if (args.length() > 150) {
                                args = args.substr(0, 147) + "...";
                            }
                            simpleline_t args_line;
                            args_line.line = ("Args: " + args).c_str();
                            args_line.color = COLOR_DEFAULT;
                            lines.push_back(args_line);
                        }
                    }
                }
            }
        }
    }
}

void format_json_for_display(const json& j, strvec_t& lines, int indent) {
    const std::string indent_str(indent * 2, ' ');

    if (j.is_object()) {
        for (nlohmann::basic_json<>::const_iterator it = j.begin(); it != j.end(); ++it) {
            simpleline_t line;
            std::string key_line = indent_str + "\"" + it.key() + "\": ";

            if (it.value().is_string()) {
                std::string value = it.value().get<std::string>();
                // Truncate very long strings
                if (value.length() > 100) {
                    value = value.substr(0, 97) + "...";
                }
                // Escape newlines for display
                std::replace(value.begin(), value.end(), '\n', ' ');
                key_line += "\"" + value + "\"";

                line.line = key_line.c_str();
                line.color = COLOR_STRING;  // Green for strings
            } else if (it.value().is_number() || it.value().is_boolean()) {
                key_line += it.value().dump();
                line.line = key_line.c_str();
                line.color = COLOR_NUMBER;  // Blue for numbers
            } else if (it.value().is_null()) {
                key_line += "null";
                line.line = key_line.c_str();
                line.color = COLOR_DEFAULT;
            } else if (it.value().is_object() || it.value().is_array()) {
                line.line = key_line.c_str();
                line.color = COLOR_DEFAULT;
                lines.push_back(line);
                format_json_for_display(it.value(), lines, indent + 1);
                continue;
            }

            lines.push_back(line);
        }
    } else if (j.is_array()) {
        int idx = 0;
        for (const nlohmann::basic_json<> &item: j) {
            simpleline_t line;
            line.line = (indent_str + "[" + std::to_string(idx++) + "]").c_str();
            line.color = COLOR_DEFAULT;
            lines.push_back(line);
            format_json_for_display(item, lines, indent + 1);
        }
    } else {
        simpleline_t line;
        line.line = (indent_str + j.dump()).c_str();
        line.color = COLOR_DEFAULT;
        lines.push_back(line);
    }
}

// Log LLM messages (thread-safe)
void log_llm_message(const std::string& direction, const json& message, int iteration) {
    if (g_terminating) return;

    // Prepare the message - use atomic increment
    int msg_num = ++g_llm_msg_counter;  // Atomic increment returns the new value
    std::string header = get_timestamp() + " [" + std::to_string(msg_num) + "] " + direction;
    if (iteration > 0) {
        header += " (Iteration " + std::to_string(iteration) + ")";
    }

    // Write to file (thread-safe)
    static std::mutex file_mutex;
    try {
        std::lock_guard<std::mutex> lock(file_mutex);

        if (!g_llm_msg_file.is_open()) {
            std::string log_path = std::string(get_user_idadir()) + "/llm_messages.json";
            g_llm_msg_file.open(log_path, std::ios::app);
        }

        if (g_llm_msg_file.is_open()) {
            json log_entry;
            log_entry["timestamp"] = get_timestamp();
            log_entry["sequence"] = msg_num;  // Use the local variable
            log_entry["direction"] = direction;
            log_entry["iteration"] = iteration;
            log_entry["message"] = message;

            g_llm_msg_file << log_entry.dump() << std::endl;
            g_llm_msg_file.flush();
        }
    } catch (...) {}

    // Update viewer on main thread
    llm_re::IDAUtils::execute_sync_wrapper([header, message, direction]() {
        if (g_terminating) return;

        // Add header
        simpleline_t header_line;
        header_line.line = ("=== " + header + " ===").c_str();
        header_line.color = COLOR_IMPNAME;  // Bright color for headers
        g_llm_msg_lines.push_back(header_line);

        // Add simplified display for IDA window
        format_llm_message_for_ida(direction, message, g_llm_msg_lines);

        // Add separator
        simpleline_t sep_line;
        sep_line.line = "";
        g_llm_msg_lines.push_back(sep_line);

        // Keep reasonable size
        while (g_llm_msg_lines.size() > 5000) {
            g_llm_msg_lines.erase(g_llm_msg_lines.begin(), g_llm_msg_lines.begin() + 100);
        }

        // Update viewer if exists
        if (g_llm_msg_viewer) {
            refresh_custom_viewer(g_llm_msg_viewer);
            simpleline_place_t bottom(g_llm_msg_lines.size() - 1);
            jumpto(g_llm_msg_viewer, &bottom, 0, 0);
        }
    }, MFF_WRITE);
}

// Initialize plugin
plugmod_t* idaapi init() {
    // Plugin works in all IDA versions
    if (!is_idaq()) return PLUGIN_SKIP;

    // Reset state
    g_terminating = false;
    g_llm_msg_counter = 0;
    g_log_viewer = nullptr;
    g_llm_msg_viewer = nullptr;

    // Close file if it was left open
    if (g_llm_msg_file.is_open()) {
        g_llm_msg_file.close();
    }

    // Clear debug log at startup
    try {
        std::string log_path = std::string(get_user_idadir()) + "/llm_plugin.log";
        std::ofstream log_file(log_path, std::ios::trunc);
        if (log_file.is_open()) {
            log_file << get_timestamp() << " === PLUGIN INIT ===" << std::endl;
        }
        std::string llm_log_path = std::string(get_user_idadir()) + "/llm_messages.json";
        std::ofstream llm_log_file(llm_log_path, std::ios::trunc);
    } catch (...) {}

    // Create action handlers
    g_log_handler = new log_handler_t();
    g_llm_msg_handler = new llm_msg_handler_t();
    g_resume_handler = new resume_handler_t();
    g_continue_handler = new continue_handler_t();

    // Register action for showing log
    register_action(action_desc_t{
        sizeof(action_desc_t),
        "llm_agent:show_log",
        "Show LLM Agent Log",
        g_log_handler,
        &PLUGIN,
        nullptr,
        "Show the LLM agent log window",
        -1
    });

    // Register showing llm message log
    register_action(action_desc_t{
        sizeof(action_desc_t),
        "llm_agent:show_llm_messages",
        "Show LLM Messages",
        g_llm_msg_handler,
        &PLUGIN,
        nullptr,
        "Show the LLM message exchange log",
        -1
    });

    // Register resume action
    register_action(action_desc_t{
        sizeof(action_desc_t),
        "llm_agent:resume",
        "Resume LLM Agent",
        g_resume_handler,
        &PLUGIN,
        "Ctrl+Shift+R",
        "Resume agent after API error",
        -1
    });

    // Register continue action
    register_action(action_desc_t{
        sizeof(action_desc_t),
        "llm_agent:continue",
        "Continue LLM Agent",
        g_continue_handler,
        &PLUGIN,
        "Ctrl+Shift+C",
        "Continue agent with new instructions",
        -1
    });

    // Add menu items
    attach_action_to_menu("View/Open subviews/", "llm_agent:show_log", SETMENU_APP);
    attach_action_to_menu("View/Open subviews/", "llm_agent:show_llm_messages", SETMENU_APP);
    attach_action_to_menu("Edit/LLM Agent/", "llm_agent:resume", SETMENU_APP);
    attach_action_to_menu("Edit/LLM Agent/", "llm_agent:continue", SETMENU_APP);

    msg("%s plugin initialized\n", PLUGIN_NAME);
    return PLUGIN_KEEP;
}

// Run plugin
bool idaapi run(size_t arg) {
    // Check if agent exists and is busy
    if (g_agent && g_agent->is_running()) {
        warning("Agent is currently running. Please wait for it to complete or pause.");
        return false;
    }

    // Get task
    qstring task;
    if (!ask_str(&task, HIST_CMT, "Enter task for the LLM agent:") || task.empty()) {
        warning("Please enter a task for the agent");
        return false;
    }

    // Get API key (with default from saved)
    qstring api_key = g_api_key.c_str();
    if (!ask_str(&api_key, HIST_CMT, "Enter your Anthropic API key:") || api_key.empty()) {
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
        g_agent->set_llm_message_callback(log_llm_message);
        g_agent->start();
    }

    // Set task
    append_to_log("Starting new task: " + std::string(task.c_str()));
    g_agent->set_task(task.c_str());
    update_ui_state();

    return true;
}

// Terminate plugin
void idaapi term() {
    // Set termination flag
    g_terminating = true;

    log_to_file_only("=== TERM START ===");
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
            if (g_llm_msg_file.is_open()) {
                g_llm_msg_file.close();
            }

            if (g_log_viewer) {
                close_widget(g_log_viewer, 0);
                g_log_viewer = nullptr;
            }

            if (g_llm_msg_viewer) {
                close_widget(g_llm_msg_viewer, 0);
                g_llm_msg_viewer = nullptr;
            }

            g_log_lines.clear();
            g_llm_msg_lines.clear();

            unregister_action("llm_agent:show_log");
            unregister_action("llm_agent:show_llm_messages");
            unregister_action("llm_agent:resume");
            unregister_action("llm_agent:continue");

        }, MFF_WRITE);

        log_to_file_only("execute_sync_wrapper returned");
    } catch (...) {
        log_to_file_only("execute_sync_wrapper exception");
    }

    // Delete action handlers AFTER unregistering actions
    if (g_log_handler) {
        delete g_log_handler;
        g_log_handler = nullptr;
    }
    if (g_llm_msg_handler) {
        delete g_llm_msg_handler;
        g_llm_msg_handler = nullptr;
    }
    if (g_resume_handler) {
        delete g_resume_handler;
        g_resume_handler = nullptr;
    }
    if (g_continue_handler) {
        delete g_continue_handler;
        g_continue_handler = nullptr;
    }

    log_to_file_only("=== TERM END ===");

    // Reset flag
    g_terminating = false;
}