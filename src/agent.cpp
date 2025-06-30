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

// Thread callback function for IDA's qthread
static int idaapi agent_worker_thread(void* ud) {
    REAgent* agent = static_cast<REAgent*>(ud);
    agent->worker_loop();
    return 0;
}

REAgent::REAgent(const std::string& anthropic_api_key)
    : api_key(anthropic_api_key),
      running(false),
      stop_requested(false),
      worker_thread(nullptr),
      task_semaphore(nullptr) {

    memory = std::make_shared<BinaryMemory>();
    executor = std::make_shared<ActionExecutor>(memory);
    anthropic = std::make_shared<AnthropicClient>(api_key);

    // Create semaphore for task notification
    task_semaphore = qsem_create(nullptr, 0);
}

REAgent::~REAgent() {
    stop();
    if (task_semaphore) {
        qsem_free(task_semaphore);
    }
}

void REAgent::start() {
    if (running) return;

    running = true;
    stop_requested = false;

    // Create worker thread using IDA's API
    worker_thread = qthread_create(agent_worker_thread, this);
}

void REAgent::stop() {
    if (!running) return;

    stop_requested = true;

    // Signal the semaphore to wake up the worker thread
    if (task_semaphore) {
        qsem_post(task_semaphore);
    }

    // Wait for thread to finish
    if (worker_thread) {
        qthread_join(worker_thread);
        qthread_free(worker_thread);
        worker_thread = nullptr;
    }

    running = false;
}

void REAgent::set_task(const std::string& task) {
    qmutex_locker_t lock(task_mutex);
    current_task = task;

    // Signal the semaphore to wake up the worker thread
    if (task_semaphore) {
        qsem_post(task_semaphore);
    }
}

void REAgent::set_log_callback(std::function<void(const std::string&)> callback) {
    log_callback = callback;
}

std::string REAgent::build_system_prompt() const {
    return R"(You are an advanced reverse engineering agent working inside IDA Pro. Your goal is to analyze binaries and answer specific questions about their functionality.

You have access to the following actions:

IDA API Actions:
### Cross-References
- **get_xrefs_to(address)** - Find what calls/references this address. Returns list of caller addresses. Auto-updates memory with relationships.
- **get_xrefs_from(address)** - Find what this address calls/references. Returns list of callee addresses. Auto-updates memory with relationships.

### Code Analysis
- **get_function_disassembly(address)** - Get assembly code with comments. Use for low-level analysis, anti-debugging checks, or optimizations.
- **get_function_decompilation(address)** - Get C-like pseudocode. Use for understanding logic, algorithms, and control flow.

### Function Management
- **get_function_address(name)** - Convert function name to address. Returns BADADDR if not found.
- **get_function_name(address)** - Get current name (may be auto-generated like "sub_401000").
- **set_function_name(address, name)** - Rename function. Use descriptive names like "validate_license".

### Reference Analysis
- **get_function_string_refs(address)** - Get all strings used by function. Good for finding URLs, errors, format strings.
- **get_function_data_refs(address)** - Get global data addresses accessed. Tracks global state usage.

### Data Management
- **get_data_name(address)** - Get name of global variable.
- **set_data_name(address, name)** - Rename global variable descriptively.

### Documentation
- **add_disassembly_comment(address, comment)** - Comment on a specific instruction.
- **add_pseudocode_comment(address, comment)** - Comment on function pseudocode.
- **clear_disassembly_comment(address)** - Remove disassembly comment.
- **clear_pseudocode_comments(address)** - Remove all pseudocode comments.

### Binary Information
- **get_imports()** - Returns map of modules to imported functions. Find interesting APIs (crypto, network, anti-debug).
- **get_exports()** - List exported functions with addresses. Find entry points in DLLs.
- **search_strings(text, is_case_sensitive)** - Find strings containing text. Locate keywords like "update", "license", "password".

## Memory System Actions

### Knowledge Management
- **set_global_note(key, content)** - Store discoveries. Use keys like "update_mechanism", "crypto_analysis".
- **get_global_note(key)** - Retrieve stored note.
- **list_global_notes()** - Get all note keys.
- **search_notes(query)** - Search notes with regex. Returns matches with snippets.

### Function Analysis
- **set_function_analysis(address, level: int, analysis)** - Store analysis at detail level:
  - Level 1 = SUMMARY: Basic purpose (1-2 sentences)
  - Level 2 = CONTEXTUAL: How it relates to other functions
  - Level 3 = ANALYTICAL: Detailed logic and data flow
  - Level 4 = COMPREHENSIVE: Complete understanding with all relationships
- **get_function_analysis(address, level: int)** - Get analysis (level 0 = best available).
- **get_memory_context(address, radius)** - Get all knowledge within call-hop radius. Your "working memory".

### Analysis Tracking
- **get_analyzed_functions()** - List all analyzed functions with max detail level achieved.
- **find_functions_by_pattern(pattern)** - Search analyses with regex. Find similar functionality.
- **get_exploration_frontier()** - Get functions marked but not analyzed yet.

### Work Queue
- **mark_for_analysis(address, reason, priority)** - Queue function for analysis (priority 1-10).
- **get_analysis_queue()** - View queue sorted by priority.
- **set_current_focus(address)** - Set anchor point. Affects detail level calculations.

### Pattern Recognition
- **add_insight(type, description, related_addresses)** - Record discoveries:
  - Types: "pattern", "hypothesis", "question", "finding"
- **get_insights(type)** - Retrieve insights (empty type = all).

### Bulk Operations
- **analyze_cluster(addresses, cluster_name, initial_level)** - Group related functions for analysis.
- **get_cluster_analysis(cluster_name)** - Get all analyses for a cluster.
- **summarize_region(start_addr, end_addr)** - Summary of everything known in address range.

## Best Practices

1. **Start with reconnaissance**: Use `search_strings()`, and `get_imports()` to find anchor points (a point which you will work out from to accomplish the user task).

2. **Document as you go**: Use `set_global_note()` for high-level understanding and `set_function_analysis()` for specific functions.

3. **Work systematically**: Use `mark_for_analysis()` to queue functions and make sure to check `get_analysis_queue()` to track progress.

4. **Build incrementally**: Start with level 1 analysis and increase detail as understanding grows.

5. **Record patterns**: Use `add_insight()` for patterns, hypotheses, and questions.

6. **Use meaningful names**: Rename functions and data to make analysis clearer.

7. **Follow the data**: Use cross-references to trace execution flow and data usage.

8. **Check context**: Use `get_memory_context()` before diving deeper.

9. **Think in clusters**: Group related functions to understand subsystems.

10. **Connect findings**: Use `search_notes()` and `find_functions_by_pattern()` to link related analyses.

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

It is up to you to figure out how much you will need to reverse engineer the binary using the actions before responding with a report.

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
        // Wait for a task using semaphore with timeout
        bool got_task = false;

        // Check if we have a task
        {
            qmutex_locker_t lock(task_mutex);
            if (!current_task.empty()) {
                got_task = true;
            }
        }

        // If no task, wait on semaphore
        if (!got_task) {
            // Wait with 100ms timeout to periodically check stop_requested
            qsem_wait(task_semaphore, 100);

            // Check again after waking up
            qmutex_locker_t lock(task_mutex);
            if (current_task.empty() || stop_requested) {
                continue;
            }
        }

        // Get the task
        std::string task;
        {
            qmutex_locker_t lock(task_mutex);
            task = current_task;
            current_task.clear();
        }

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

    {
        qmutex_locker_t lock(const_cast<qmutex_t&>(task_mutex));
        state["current_task"] = current_task;
    }

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