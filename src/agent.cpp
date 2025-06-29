//
// Created by user on 6/29/25.
//

#include "agent.h"
#include <fstream>
#include <regex>

namespace llm_re {

void log(LogLevel level, const std::string& message) {
    const char* level_str = "";
    switch (level) {
        case LogLevel::DEBUG: level_str = "[DEBUG]"; break;
        case LogLevel::INFO: level_str = "[INFO]"; break;
        case LogLevel::WARNING: level_str = "[WARNING]"; break;
        case LogLevel::ERROR: level_str = "[ERROR]"; break;
    }
    msg("%s %s\n", level_str, message.c_str());
}

REAgent::REAgent(const std::string& anthropic_api_key)
    : api_key(anthropic_api_key), running(false), stop_requested(false) {

    memory = std::make_shared<BinaryMemory>();
    executor = std::make_shared<ActionExecutor>(memory);
    anthropic = std::make_shared<AnthropicClient>(api_key);
}

REAgent::~REAgent() {
    stop();
}

void REAgent::start() {
    if (running) return;

    running = true;
    stop_requested = false;
    worker_thread = std::thread(&REAgent::worker_loop, this);
}

void REAgent::stop() {
    if (!running) return;

    stop_requested = true;
    task_cv.notify_all();

    if (worker_thread.joinable()) {
        worker_thread.join();
    }

    running = false;
}

void REAgent::set_task(const std::string& task) {
    std::lock_guard<std::mutex> lock(task_mutex);
    current_task = task;
    task_cv.notify_all();
}

void REAgent::set_log_callback(std::function<void(const std::string&)> callback) {
    log_callback = callback;
}

std::string REAgent::build_system_prompt() const {
    return R"(You are an advanced reverse engineering agent working inside IDA Pro. Your goal is to analyze binaries and answer specific questions about their functionality.

You have access to the following actions:

IDA API Actions:
- get_xrefs_to(address) - Find all references TO a location
- get_xrefs_from(address) - Find all references FROM a location
- get_function_disassembly(address) - Get disassembly for a function
- get_function_decompilation(address) - Get decompiled code for a function
- get_function_address(name) - Get address from function name
- get_function_name(address) - Get function name from address
- set_function_name(address, name) - Rename a function
- get_function_string_refs(address) - Get strings used by a function
- get_function_data_refs(address) - Get data references from a function
- get_data_name(address) - Get name of data item
- set_data_name(address, name) - Rename data item
- add_disassembly_comment(address, comment) - Add comment to disassembly
- add_pseudocode_comment(address, comment) - Add comment to pseudocode
- clear_disassembly_comment(address) - Clear disassembly comment
- clear_pseudocode_comments(address) - Clear pseudocode comments
- get_imports() - List all imported functions
- get_exports() - List all exported functions
- get_strings() - List all strings in the binary
- search_strings(text, is_case_sensitive) - Search for strings

Memory System Actions:
- set_global_note(key, content) - Store a note about your findings
- get_global_note(key) - Retrieve a specific note
- list_global_notes() - List all note keys
- search_notes(query) - Search through your notes
- set_function_analysis(address, level, analysis) - Store function analysis
- get_function_analysis(address, level) - Retrieve function analysis
- get_memory_context(address, radius) - Get all knowledge near an address
- get_analyzed_functions() - List all analyzed functions
- find_functions_by_pattern(pattern) - Search function analyses
- get_exploration_frontier() - Get functions marked for analysis
- mark_for_analysis(address, reason, priority) - Queue function for analysis
- get_analysis_queue() - View analysis queue
- set_current_focus(address) - Set current anchor point
- add_insight(type, description, related_addresses) - Record a discovery
- get_insights(type) - Retrieve insights
- analyze_cluster(addresses, cluster_name, initial_level) - Analyze related functions
- get_cluster_analysis(cluster_name) - Get cluster analysis
- summarize_region(start_addr, end_addr) - Summarize memory region

Detail Levels:
1 = SUMMARY: Basic function purpose
2 = CONTEXTUAL: How it relates to nearby functions
3 = ANALYTICAL: Detailed analysis with data flow
4 = COMPREHENSIVE: Full breakdown including all relationships

To execute an action, respond with:
ACTION: action_name
PARAMS: {"param1": value1, "param2": value2}

You can execute multiple actions by using multiple ACTION/PARAMS pairs.

Remember to:
1. Start by finding anchor points (strings, function names) relevant to the task
2. Work outward from anchor points, following references
3. Build up your understanding using the memory system
4. Use appropriate detail levels based on relevance
5. Look for patterns and connections between functions
6. Document your findings with notes and insights

When you have gathered enough information to answer the user's question, respond with:
REPORT: Your detailed findings about the task

Current task: )";
}

json REAgent::parse_llm_action(const std::string& response) const {
    json actions = json::array();

    // Parse ACTION/PARAMS pairs
    std::regex action_regex(R"(ACTION:\s*(\w+)\s*\nPARAMS:\s*(\{[^}]+\}))");
    std::sregex_iterator it(response.begin(), response.end(), action_regex);
    std::sregex_iterator end;

    while (it != end) {
        json action;
        action["name"] = (*it)[1].str();
        try {
            action["params"] = json::parse((*it)[2].str());
        } catch (const std::exception& e) {
            action["params"] = json::object();
        }
        actions.push_back(action);
        ++it;
    }

    // Check for REPORT
    std::regex report_regex(R"(REPORT:\s*(.+))");
    std::smatch report_match;
    if (std::regex_search(response, report_match, report_regex)) {
        json report_action;
        report_action["name"] = "report";
        report_action["params"]["content"] = report_match[1].str();
        actions.push_back(report_action);
    }

    return actions;
}

std::string REAgent::format_action_result(const json& result) const {
    return result.dump(2);
}

void REAgent::worker_loop() {
    while (!stop_requested) {
        std::unique_lock<std::mutex> lock(task_mutex);
        task_cv.wait(lock, [this] { return !current_task.empty() || stop_requested; });

        if (stop_requested) break;
        if (current_task.empty()) continue;

        std::string task = current_task;
        current_task.clear();
        lock.unlock();

        // Log start
        if (log_callback) {
            log_callback("Starting analysis for task: " + task);
        }

        // Initialize conversation
        conversation_history.clear();

        // Build initial prompt
        std::string system_prompt = build_system_prompt() + task;

        AnthropicClient::ChatRequest request;
        request.system_prompt = system_prompt;
        request.messages.push_back({"user", "Please analyze the binary to answer: " + task});

        // Main agent loop
        int iteration = 0;
        const int max_iterations = 50;

        while (iteration < max_iterations && !stop_requested) {
            iteration++;

            if (log_callback) {
                log_callback("Iteration " + std::to_string(iteration));
            }

            // Send request to LLM
            auto response = anthropic->send_chat_request(request);

            if (!response.success) {
                if (log_callback) {
                    log_callback("LLM Error: " + response.error);
                }
                break;
            }

            // Add response to history
            request.messages.push_back({"assistant", response.content});

            // Parse actions from response
            json actions = parse_llm_action(response.content);

            if (actions.empty()) {
                continue;
            }

            // Execute actions
            json results = json::array();
            bool found_report = false;

            for (const auto& action : actions) {
                std::string action_name = action["name"];

                if (action_name == "report") {
                    // Final report
                    if (log_callback) {
                        log_callback("=== FINAL REPORT ===\n" + action["params"]["content"].get<std::string>());
                    }
                    found_report = true;
                    break;
                } else {
                    // Execute action
                    if (log_callback) {
                        log_callback("Executing: " + action_name);
                    }

                    json result = executor->execute_action(action_name, action["params"]);
                    results.push_back({
                        {"action", action_name},
                        {"result", result}
                    });
                }
            }

            if (found_report) {
                break;
            }

            // Add results to conversation
            std::string results_str = "Results:\n" + results.dump(2);
            request.messages.push_back({"user", results_str});
        }

        if (iteration >= max_iterations) {
            if (log_callback) {
                log_callback("Reached maximum iterations limit");
            }
        }
    }
}

std::string REAgent::get_current_state() const {
    json state;
    state["running"] = running.load();
    state["current_task"] = current_task;
    state["memory_snapshot"] = memory->export_memory_snapshot();
    return state.dump(2);
}

void REAgent::save_memory(const std::string& filename) {
    std::ofstream file(filename);
    if (file.is_open()) {
        file << memory->export_memory_snapshot().dump(2);
        file.close();
    }
}

void REAgent::load_memory(const std::string& filename) {
    std::ifstream file(filename);
    if (file.is_open()) {
        json snapshot;
        file >> snapshot;
        memory->import_memory_snapshot(snapshot);
        file.close();
    }
}

} // namespace llm_re
