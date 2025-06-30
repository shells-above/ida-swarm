//
// Created by user on 6/29/25.
//

#include "agent.h"

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

void REAgent::set_llm_message_callback(std::function<void(const std::string&, const json&, int)> callback) {
    llm_message_callback = callback;
    if (anthropic) {
        anthropic->set_message_logger(callback);
    }
}

std::vector<AnthropicClient::Tool> REAgent::define_tools() const {
    std::vector<AnthropicClient::Tool> tools;

    // Helper to create parameter schema
    auto make_params = [](const std::vector<std::pair<std::string, std::string>>& params,
                         const std::vector<std::string>& required = {}) -> json {
        json schema;
        schema["type"] = "object";
        schema["properties"] = json::object();

        for (const auto& [name, type] : params) {
            if (type == "integer") {
                schema["properties"][name] = {{"type", "integer"}};
            } else if (type == "boolean") {
                schema["properties"][name] = {{"type", "boolean"}};
            } else if (type == "array") {
                schema["properties"][name] = {{"type", "array"}, {"items", {{"type", "integer"}}}};
            } else {
                schema["properties"][name] = {{"type", "string"}};
            }
        }

        if (!required.empty()) {
            schema["required"] = required;
        }

        return schema;
    };

    // Cross-Reference tools
    tools.push_back({
        "get_xrefs_to",
        "Find what calls or references this address. Returns list of caller addresses. Auto-updates memory with relationships. Essential for understanding how functions are used.",
        make_params({{"address", "integer"}}, {"address"})
    });

    tools.push_back({
        "get_xrefs_from",
        "Find what this address calls or references. Returns list of callee addresses. Auto-updates memory with relationships. Use to trace execution flow.",
        make_params({{"address", "integer"}}, {"address"})
    });

    // Code Analysis tools
    tools.push_back({
        "get_function_disassembly",
        "Get assembly code with comments for a function. Use for low-level analysis, anti-debugging checks, optimizations, or when decompilation fails.",
        make_params({{"address", "integer"}}, {"address"})
    });

    tools.push_back({
        "get_function_decompilation",
        "Get C-like pseudocode for a function. Best for understanding logic, algorithms, and control flow. Easier to read than assembly.",
        make_params({{"address", "integer"}}, {"address"})
    });

    // Function Management tools
    tools.push_back({
        "get_function_address",
        "Convert function name to address. Returns BADADDR if not found. Use to locate known functions.",
        make_params({{"name", "string"}}, {"name"})
    });

    tools.push_back({
        "get_function_name",
        "Get current name of function at address. May be auto-generated like 'sub_401000'. Check before renaming.",
        make_params({{"address", "integer"}}, {"address"})
    });

    tools.push_back({
        "set_function_name",
        "Rename function at address. Use descriptive names like 'validate_license' or 'decrypt_config'. Makes analysis clearer.",
        make_params({{"address", "integer"}, {"name", "string"}}, {"address", "name"})
    });

    // Reference Analysis tools
    tools.push_back({
        "get_function_string_refs",
        "Get all strings used by function. Excellent for finding URLs, error messages, format strings, or understanding function purpose.",
        make_params({{"address", "integer"}}, {"address"})
    });

    tools.push_back({
        "get_function_data_refs",
        "Get global data addresses accessed by function. Tracks global state usage and shared data structures.",
        make_params({{"address", "integer"}}, {"address"})
    });

    // Data Management tools
    tools.push_back({
        "get_data_name",
        "Get name of global variable at address. May be auto-generated.",
        make_params({{"address", "integer"}}, {"address"})
    });

    tools.push_back({
        "set_data_name",
        "Rename global variable at address. Use descriptive names like 'g_license_key' or 'encryption_table'.",
        make_params({{"address", "integer"}, {"name", "string"}}, {"address", "name"})
    });

    tools.push_back({
        "get_data",
        "Get the value of global data at address. Returns string for string data, or hex bytes for other data types.",
        make_params({{"address", "integer"}}, {"address"})
    });

    // Documentation tools
    tools.push_back({
        "add_disassembly_comment",
        "Add comment to a specific instruction. Document important findings at instruction level.",
        make_params({{"address", "integer"}, {"comment", "string"}}, {"address", "comment"})
    });

    tools.push_back({
        "add_pseudocode_comment",
        "Add comment to function pseudocode. Document high-level understanding and algorithms.",
        make_params({{"address", "integer"}, {"comment", "string"}}, {"address", "comment"})
    });

    tools.push_back({
        "clear_disassembly_comment",
        "Remove disassembly comment at address if incorrect or outdated.",
        make_params({{"address", "integer"}}, {"address"})
    });

    tools.push_back({
        "clear_pseudocode_comments",
        "Remove all pseudocode comments for function. Use when starting fresh analysis.",
        make_params({{"address", "integer"}}, {"address"})
    });

    // Binary Information tools
    tools.push_back({
        "get_imports",
        "Get all imported functions grouped by module. Find interesting APIs like crypto (CryptDecrypt), network (WSASocket), or anti-debug (IsDebuggerPresent).",
        make_params({})
    });

    tools.push_back({
        "get_exports",
        "List all exported functions with addresses. Essential for DLL analysis. Find entry points and public APIs.",
        make_params({})
    });

    tools.push_back({
        "search_strings",
        "Find strings containing specific text. Locate keywords like 'update', 'license', 'password', URLs, or error messages. Case-sensitive option available.",
        make_params({{"text", "string"}, {"is_case_sensitive", "boolean"}}, {"text"})
    });

    // Knowledge Management tools
    tools.push_back({
        "set_global_note",
        "Store high-level analysis findings. Use descriptive keys like 'update_mechanism', 'crypto_analysis', 'network_protocol'. Persistent across analysis.",
        make_params({{"key", "string"}, {"content", "string"}}, {"key", "content"})
    });

    tools.push_back({
        "get_global_note",
        "Retrieve previously stored note by key. Check for existing analysis before starting.",
        make_params({{"key", "string"}}, {"key"})
    });

    tools.push_back({
        "list_global_notes",
        "Get all stored note keys. See what aspects have been analyzed.",
        make_params({})
    });

    tools.push_back({
        "search_notes",
        "Search all notes with regex pattern. Find related analyses and connect findings.",
        make_params({{"query", "string"}}, {"query"})
    });

    // Function Analysis tools
    tools.push_back({
        "set_function_analysis",
        "Store function analysis at detail level. Level 1=basic purpose, 2=relationships, 3=detailed logic, 4=comprehensive. Build understanding incrementally.",
        make_params({{"address", "integer"}, {"level", "integer"}, {"analysis", "string"}},
                   {"address", "level", "analysis"})
    });

    tools.push_back({
        "get_function_analysis",
        "Get stored analysis for function. Level 0 returns best available. Check before re-analyzing.",
        make_params({{"address", "integer"}, {"level", "integer"}}, {"address"})
    });

    tools.push_back({
        "get_memory_context",
        "Get all knowledge within call-hop radius. Your 'working memory' for understanding code regions. Essential before deep analysis.",
        make_params({{"address", "integer"}, {"radius", "integer"}}, {"address"})
    });

    // Analysis Tracking tools
    tools.push_back({
        "get_analyzed_functions",
        "List all analyzed functions with maximum detail level achieved. Track analysis coverage.",
        make_params({})
    });

    tools.push_back({
        "find_functions_by_pattern",
        "Search function analyses with regex. Find similar functionality across the binary.",
        make_params({{"pattern", "string"}}, {"pattern"})
    });

    tools.push_back({
        "get_exploration_frontier",
        "Get functions marked but not yet analyzed. See what needs investigation.",
        make_params({})
    });

    // Work Queue tools
    tools.push_back({
        "mark_for_analysis",
        "Queue function for future analysis with reason and priority (1=low, 10=high). Track interesting functions found during exploration.",
        make_params({{"address", "integer"}, {"reason", "string"}, {"priority", "integer"}},
                   {"address", "reason"})
    });

    tools.push_back({
        "get_analysis_queue",
        "View pending analysis queue sorted by priority. Decide what to analyze next.",
        make_params({})
    });

    tools.push_back({
        "set_current_focus",
        "Set anchor point for analysis. Affects detail level calculations - nearby functions get more attention.",
        make_params({{"address", "integer"}}, {"address"})
    });

    // Pattern Recognition tools
    tools.push_back({
        "add_insight",
        "Record analysis insights. Types: 'pattern' (recurring code), 'hypothesis' (theory), 'question' (uncertainty), 'finding' (discovery).",
        make_params({{"type", "string"}, {"description", "string"}, {"related_addresses", "array"}},
                   {"type", "description", "related_addresses"})
    });

    tools.push_back({
        "get_insights",
        "Retrieve recorded insights by type. Empty type returns all. Review patterns and hypotheses.",
        make_params({{"type", "string"}})
    });

    // Bulk Operation tools
    tools.push_back({
        "analyze_cluster",
        "Group related functions for cohesive analysis. Useful for analyzing subsystems like 'crypto_functions' or 'network_handlers'.",
        make_params({{"addresses", "array"}, {"cluster_name", "string"}, {"initial_level", "integer"}},
                   {"addresses", "cluster_name", "initial_level"})
    });

    tools.push_back({
        "get_cluster_analysis",
        "Retrieve all analyses for a named cluster. Understand subsystem functionality.",
        make_params({{"cluster_name", "string"}}, {"cluster_name"})
    });

    tools.push_back({
        "summarize_region",
        "Get summary of everything known in address range. Useful for understanding code sections.",
        make_params({{"start_addr", "integer"}, {"end_addr", "integer"}}, {"start_addr", "end_addr"})
    });

    // Special tool for final report
    tools.push_back({
        "submit_final_report",
        "Submit your final analysis report when you have gathered enough information to answer the user's task. This completes the analysis.",
        make_params({{"report", "string"}}, {"report"})
    });

    return tools;
}

std::string REAgent::build_system_prompt() const {
    return R"(You are an advanced reverse engineering agent working inside IDA Pro. Your goal is to analyze binaries and answer specific questions about their functionality.

You have access to the following tools:

IDA API Tools:
### Cross-References
- **get_xrefs_to(address)** - Find what calls/references this address. Returns list of caller addresses. Auto-updates memory with relationships.
- **get_xrefs_from(address)** - Find what this address calls/references. Returns list of callee addresses. Auto-updates memory with relationships.

### Code Analysis
- **get_function_disassembly(address)** - Get assembly code with comments. Use for low-level analysis, anti-debugging checks, or optimizations. This is expensive! So only use it for functions you really need a detailed understanding of.
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
- **get_data(address)** - Get the value of global data. Returns string content for strings, hex bytes for other data.

### Documentation
- **add_disassembly_comment(address, comment)** - Comment on a specific instruction.
- **add_pseudocode_comment(address, comment)** - Comment on function pseudocode.
- **clear_disassembly_comment(address)** - Remove disassembly comment.
- **clear_pseudocode_comments(address)** - Remove all pseudocode comments.

### Binary Information
- **get_imports()** - Returns map of modules to imported functions. Find interesting APIs (crypto, network, anti-debug).
- **get_exports()** - List exported functions with addresses. Find entry points in DLLs.
- **search_strings(text, is_case_sensitive)** - Find strings containing text. Locate keywords like "update", "license", "password".

## Memory System Tools

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

### Task Completion
- **submit_final_report(report)** - Submit your final analysis report when you have gathered enough information to answer the user's question.

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

Remember to:
1. Start by finding anchor points (strings, function names) relevant to the task
2. Work outward from anchor points, following references
3. Build up your understanding using the memory system
4. Use appropriate detail levels based on relevance
5. Look for patterns and connections between functions
6. Document your findings with notes and insights
7. Update the IDA database with your knowledge by setting function names and comments using your tool calls. Only update information when you are confident you understand it.

When you have gathered enough information to answer the user's question, use the submit_final_report tool with your detailed findings.
It is up to you to figure out how much you will need to reverse engineer the binary using the tools before submitting your final report.

Current task: )";
}

/*
 *haiku sucks really bad, so i put these in the prompt and it didnt help much
Impossible tasks will NEVER be provided to you. I guarantee that there is **ALWAYS** a solution, and your job is to find it.
Work outwards from your anchor points (strings) and examine the functions and their relation to one another to accomplish your task.
Remember! **It is of the utmost importance that once you want to create your final report, you use the submit_final_report tool call to submit your report. Your task WILL NOT FINISH if you don't submit it using this tool call.**
Once you submit your final report, you will be terminated! You must make sure that your final report contains the information the user needs, because NO further investigation will occur.
 */

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


        // Helper function to build a unique key for decompilation/disassembly results
        auto build_result_key = [this](const std::string& tool_name, const json& tool_input) -> std::string {
            std::string key = tool_name;
            if (tool_input.contains("address")) {
                ea_t addr = ActionExecutor::parse_single_address_value(tool_input["address"]);
                key += ":" + std::to_string(addr);
            }
            return key;
        };

        // Helper to check if a tool produces large results
        auto is_large_result_tool = [](const std::string& tool_name) -> bool {
            return tool_name == "get_function_decompilation" ||
                   tool_name == "get_function_disassembly";
        };

        // Track tool calls with their iteration number
        std::map<std::string, std::string> tool_call_to_name;
        std::map<std::string, json> tool_call_to_input;
        std::map<std::string, int> tool_call_iteration;
        std::map<std::string, std::string> latest_tool_results;

        // Log start
        log_callback("Starting analysis for task: " + task);

        // Initialize conversation history
        conversation_history.clear();

        // Build initial prompt
        std::string system_prompt = build_system_prompt() + task;

        AnthropicClient::ChatRequest request;
        request.system_prompt = system_prompt;
        request.messages.emplace_back("user", "Please analyze the binary to answer: " + task);
        request.tools = define_tools();

        // Main agent loop
        int iteration = 0;
        const int max_iterations = 100;
        bool task_complete = false;

        while (iteration < max_iterations && !stop_requested && !task_complete) {
            iteration++;
            anthropic->set_iteration(iteration);

            if (log_callback) {
                log_callback("Iteration " + std::to_string(iteration));
            }

            AnthropicClient::ChatRequest pruned_request = request;
            if (iteration > 1) {
                for (AnthropicClient::ChatMessage& msg: pruned_request.messages) {
                    // Only prune tool results
                    if (msg.role == "tool" && !msg.tool_call_id.empty()) {
                        auto it = tool_call_to_name.find(msg.tool_call_id);
                        if (it != tool_call_to_name.end() && is_large_result_tool(it->second)) {
                            // Check if this result is from a previous iteration
                            if (tool_call_iteration[msg.tool_call_id] < iteration - 1) {
                                // Parse and prune the content
                                try {
                                    json result = json::parse(msg.content);
                                    if (result.contains("decompilation")) {
                                        result["decompilation"] = "[Decompilation pruned - previously shown to LLM. You can request again if you need to analyze it deeper.]";
                                    }
                                    if (result.contains("disassembly")) {
                                        result["disassembly"] = "[Disassembly pruned - previously shown to LLM. You can request again if you need to analyze it deeper.]";
                                    }
                                    msg.content = result.dump();
                                } catch (...) {
                                    // If parsing fails, just leave it as is
                                }
                            }
                        }
                    }
                }
            }

            // Send request
            auto response = anthropic->send_chat_request(pruned_request);

            if (!response.success) {
                if (log_callback) {
                    log_callback("LLM Error: " + response.error);
                    log_callback("Task failed due to API error. Please check your API key.");
                }
                task_complete = true;
                break;
            }

            // Log token usage and progress
            log_token_usage(response, iteration);

            // Add response to ORIGINAL request history
            AnthropicClient::ChatMessage assistant_msg("assistant", response.content);
            assistant_msg.tool_calls = response.tool_calls;
            request.messages.push_back(assistant_msg);

            // Process tool calls
            if (!response.tool_calls.empty()) {
                for (const auto& tool_call : response.tool_calls) {
                    std::string tool_name = tool_call["name"];
                    std::string tool_id = tool_call["id"];
                    json tool_input = tool_call["input"];

                    // Track this tool call
                    tool_call_to_name[tool_id] = tool_name;
                    tool_call_to_input[tool_id] = tool_input;
                    tool_call_iteration[tool_id] = iteration;

                    if (log_callback) {
                        log_callback("Executing tool: " + tool_name + " (" + tool_input.dump() + ")");
                    }

                    // Handle special case: final report
                    if (tool_name == "submit_final_report") {
                        if (log_callback) {
                            log_callback("=== FINAL REPORT ===\n" + tool_input["report"].get<std::string>());
                        }
                        task_complete = true;
                        break;
                    }

                    // Execute the tool
                    json result = executor->execute_action(tool_name, tool_input);

                    // Update latest tool results tracking for large results
                    if (is_large_result_tool(tool_name)) {
                        std::string key = build_result_key(tool_name, tool_input);
                        latest_tool_results[key] = tool_id;
                    }

                    // Add tool result to conversation (unpruned)
                    AnthropicClient::ChatMessage tool_result_msg("tool", result.dump());
                    tool_result_msg.tool_call_id = tool_id;
                    request.messages.push_back(tool_result_msg);
                }
            } else if (response.stop_reason == "end_turn" && response.tool_calls.empty()) {
                // Assistant finished without calling tools - might be thinking or need prompting
                if (iteration > 1) {
                    std::string analyze_more_msg = "Please continue your analysis and use tools to gather more information or submit your final report.";
                    request.messages.emplace_back("user", analyze_more_msg);
                }
            }
        }

        if (iteration >= max_iterations && !task_complete) {
            if (log_callback) {
                log_callback("Reached maximum iterations limit without completing the task");
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

void REAgent::log_token_usage(const AnthropicClient::ChatResponse& response, int iteration) {
    // Update token statistics
    token_stats.total_input_tokens += response.input_tokens;
    token_stats.total_output_tokens += response.output_tokens;
    token_stats.total_cache_creation_tokens += response.cache_creation_input_tokens;
    token_stats.total_cache_read_tokens += response.cache_read_input_tokens;
    token_stats.request_count++;
    
    if (log_callback) {
        std::stringstream log_msg;
        log_msg << "[Iteration " << iteration << "] ";
        log_msg << "Tokens: " << response.input_tokens << " in, " << response.output_tokens << " out";
        
        if (response.cache_read_input_tokens > 0) {
            log_msg << " (" << response.cache_read_input_tokens << " cached)";
        }
        
        log_msg << " | Session Total: " << token_stats.get_total_tokens();
        log_msg << " | Est. Cost: $" << std::fixed << std::setprecision(4) << token_stats.get_estimated_cost();
        
        // Show thinking summary if available
        if (!response.thinking.empty()) {
            std::string thinking_preview = response.thinking.substr(0, 100);
            if (response.thinking.length() > 100) thinking_preview += "...";
            log_msg << " | Reasoning: " << thinking_preview;
        }
        
        log_callback(log_msg.str());
    }
}

} // namespace llm_re