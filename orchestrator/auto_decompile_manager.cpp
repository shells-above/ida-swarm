#include "auto_decompile_manager.h"
#include "orchestrator.h"
#include "../core/logger.h"
#include "../core/ida_utils.h"
#include "../agent/event_bus.h"
#include <ida.hpp>
#include <funcs.hpp>
#include <segment.hpp>
#include <name.hpp>
#include <sstream>
#include <iomanip>

namespace llm_re::orchestrator {

AutoDecompileManager::AutoDecompileManager(Orchestrator* orchestrator)
    : orchestrator_(orchestrator)
    , config_(Config::instance())
    , prioritizer_(config_)
{
    LOG("AutoDecompileManager: Initialized with max parallel agents = %d",
        config_.swarm.max_parallel_auto_decompile_agents);
}

std::vector<ea_t> AutoDecompileManager::enumerate_non_library_functions() {
    return IDAUtils::execute_sync_wrapper([&]() -> std::vector<ea_t> {
        std::vector<ea_t> functions;

        LOG("AutoDecompileManager: Enumerating functions...");

        // Iterate through all functions in the database
        size_t total_count = get_func_qty();
        for (size_t i = 0; i < total_count; i++) {
            func_t* func = getn_func(i);
            if (!func) continue;

            // Skip library functions
            if (func->flags & FUNC_LIB) {
                continue;
            }

            // Skip thunk functions (import stubs, jump tables)
            if (func->flags & FUNC_THUNK) {
                continue;
            }

            // Skip function tails (these are continuations, not real functions)
            if (func->flags & FUNC_TAIL) {
                continue;
            }

            // Skip outlined code (not real functions)
            if (func->flags & FUNC_OUTLINE) {
                continue;
            }

            // Skip hidden functions
            if (func->flags & FUNC_HIDDEN) {
                continue;
            }

            // Skip functions in external/import segments
            segment_t* seg = getseg(func->start_ea);
            if (seg && seg->type == SEG_XTRN) {
                continue;
            }

            functions.push_back(func->start_ea);
        }

        LOG("AutoDecompileManager: Found %zu non-library functions (out of %zu total)",
            functions.size(), total_count);

        return functions;
    }, MFF_READ);
}

std::string AutoDecompileManager::get_function_display_name(ea_t function_ea) {
    return IDAUtils::execute_sync_wrapper([&]() -> std::string {
        qstring func_name;
        if (get_name(&func_name, function_ea) > 0) {
            return func_name.c_str();
        }

        // No name, return address
        std::ostringstream oss;
        oss << "sub_" << std::hex << std::uppercase << function_ea;
        return oss.str();
    }, MFF_READ);
}

std::string AutoDecompileManager::generate_function_analysis_prompt(
    ea_t function_ea,
    const FunctionPriority& priority
) {
    std::string func_name = get_function_display_name(function_ea);

    std::ostringstream prompt;
    prompt << "COMPREHENSIVE FUNCTION REVERSAL\n\n";
    prompt << "Target Function: 0x" << std::hex << std::uppercase << function_ea;
    prompt << " (" << func_name << ")\n\n";

    // Add priority context if available
    // if (priority.score > 0 && !priority.reason.empty()) {
    //     prompt << "Priority Reason: " << priority.reason << "\n";
    //     if (!priority.metrics.empty()) {
    //         prompt << "Metrics: ";
    //         for (size_t i = 0; i < priority.metrics.size(); i++) {
    //             if (i > 0) prompt << ", ";
    //             prompt << priority.metrics[i];
    //         }
    //         prompt << "\n";
    //     }
    //     prompt << "\n";
    // }

    prompt << "Your mission: Perform a COMPLETE reversal of this function to achieve source-level decompilation quality.\n\n";

    prompt << "REQUIREMENTS:\n";
    prompt << "1. Set ALL local variable names meaningfully\n";
    prompt << "2. Set ALL local variable types precisely\n";
    prompt << "3. Set function name (if not already well-named)\n";
    prompt << "4. Set function prototype with proper parameter names and types\n";
    prompt << "5. Add comments explaining non-obvious logic\n";
    prompt << "6. Apply or create struct/enum types where needed\n\n";

    prompt << "CRITICAL: Before creating any new types:\n";
    prompt << "- Use search_local_types() to check if another agent already created similar types\n";
    prompt << "- Reuse existing types whenever possible for consistency\n";
    prompt << "- Only create new types if no suitable type exists\n\n";

    prompt << "QUALITY STANDARD:\n";
    prompt << "The decompilation must look like well-written source code. Variable names should ";
    prompt << "reveal intent, types should be precise, and control flow should be clear.\n\n";

    prompt << "Explore the function thoroughly, understand its purpose in the broader program context, ";
    prompt << "and make it perfect.\n\n";

    prompt << "When satisfied with the reversal quality, your work will be automatically merged back ";
    prompt << "to the main database.";

    return prompt.str();
}

void AutoDecompileManager::start_auto_decompile() {
    if (active_) {
        LOG("AutoDecompileManager: Analysis already active, ignoring start request");
        return;
    }

    LOG("AutoDecompileManager: Starting full binary analysis");

    // Reset state
    progress_ = AnalysisProgress();
    progress_.start_time = std::chrono::steady_clock::now();
    progress_.last_update = progress_.start_time;

    completed_functions_.clear();
    agent_to_function_.clear();
    while (!pending_functions_.empty()) {
        pending_functions_.pop();
    }

    // Clear retry tracking
    function_priorities_.clear();
    function_retry_count_.clear();
    failed_functions_.clear();

    // Enumerate functions
    std::vector<ea_t> functions = enumerate_non_library_functions();
    if (functions.empty()) {
        LOG("AutoDecompileManager: No non-library functions found");
        // Emit event
        json event_data = {
            {"total_functions", 0},
            {"message", "No non-library functions found"}
        };
        get_event_bus().emit("orchestrator", AgentEvent::AUTO_DECOMPILE_COMPLETED, event_data);
        return;
    }

    // Prioritize functions
    std::vector<FunctionPriority> priorities = prioritizer_.prioritize_functions(functions);

    // Build queue
    for (const auto& priority : priorities) {
        pending_functions_.push(priority);
    }

    progress_.total_functions = pending_functions_.size();
    progress_.completed_functions = 0;
    progress_.update();

    active_ = true;

    // Emit start event
    json event_data = {
        {"total_functions", progress_.total_functions}
    };
    get_event_bus().emit("orchestrator", AgentEvent::AUTO_DECOMPILE_STARTED, event_data);

    LOG("AutoDecompileManager: Starting analysis of %zu functions with up to %d parallel agents",
        progress_.total_functions, config_.swarm.max_parallel_auto_decompile_agents);

    // Spawn initial batch of agents (up to max parallel)
    for (int i = 0; i < config_.swarm.max_parallel_auto_decompile_agents && !pending_functions_.empty(); i++) {
        spawn_next_agent();
    }
}

void AutoDecompileManager::spawn_next_agent() {
    // First, check if any agents have crashed and clean them up
    check_agent_health();

    if (pending_functions_.empty()) {
        LOG("AutoDecompileManager: No more functions to analyze");

        // If no active agents either, we're done
        if (progress_.active_agents.empty()) {
            LOG("AutoDecompileManager: Decompilation complete!");
            active_ = false;

            // Emit completion event
            json event_data = {
                {"total_functions", progress_.total_functions},
                {"completed_functions", progress_.completed_functions},
                {"elapsed_seconds", progress_.elapsed_seconds()}
            };
            get_event_bus().emit("orchestrator", AgentEvent::AUTO_DECOMPILE_COMPLETED, event_data);
        }

        return;
    }

    // Get next function from queue
    FunctionPriority priority = pending_functions_.front();
    pending_functions_.pop();

    ea_t function_ea = priority.address;
    std::string func_name = get_function_display_name(function_ea);

    // Save priority for potential retry
    function_priorities_[function_ea] = priority;

    LOG("AutoDecompileManager: Spawning agent for function 0x%llx (%s) - priority: %.1f (%s)",
        (unsigned long long)function_ea, func_name.c_str(), priority.score, priority.reason.c_str());

    // Generate task prompt
    std::string task = generate_function_analysis_prompt(function_ea, priority);

    // Spawn agent via orchestrator
    json spawn_result = orchestrator_->spawn_agent_async(task, "auto_decompile");

    if (spawn_result.contains("error")) {
        LOG("AutoDecompileManager: Failed to spawn agent for 0x%llx: %s",
            (unsigned long long)function_ea, spawn_result["error"].get<std::string>().c_str());
        return;
    }

    // Extract agent ID from result
    std::string agent_id = spawn_result["agent_id"];

    LOG("AutoDecompileManager: Spawned agent %s for function 0x%llx (%s)",
        agent_id.c_str(), (unsigned long long)function_ea, func_name.c_str());

    // Track the agent
    agent_to_function_[agent_id] = function_ea;
    progress_.active_agents[agent_id] = function_ea;
    progress_.update();

    // Emit progress event
    json progress_event = {
        {"total_functions", progress_.total_functions},
        {"completed_functions", progress_.completed_functions},
        {"active_functions", progress_.active_functions},
        {"pending_functions", progress_.pending_functions},
        {"percent_complete", progress_.percent_complete},
        {"active_agents", json::object()}
    };

    // Add active agents to event
    for (const auto& [aid, fea] : progress_.active_agents) {
        progress_event["active_agents"][aid] = fea;
    }

    get_event_bus().emit("orchestrator", AgentEvent::AUTO_DECOMPILE_PROGRESS, progress_event);
}

void AutoDecompileManager::on_agent_completed(const std::string& agent_id) {
    if (!active_) {
        return;
    }

    // Find which function this agent was analyzing
    auto it = agent_to_function_.find(agent_id);
    if (it == agent_to_function_.end()) {
        LOG("AutoDecompileManager: Agent %s completed but not tracked", agent_id.c_str());
        return;
    }

    ea_t function_ea = it->second;
    std::string func_name = get_function_display_name(function_ea);

    LOG("AutoDecompileManager: Agent %s completed analysis of function 0x%llx (%s)",
        agent_id.c_str(), (unsigned long long)function_ea, func_name.c_str());

    // Mark function as completed
    completed_functions_.insert(function_ea);
    progress_.completed_functions++;
    progress_.completed_function_addresses.push_back(function_ea);

    // Remove from active tracking
    agent_to_function_.erase(it);
    progress_.active_agents.erase(agent_id);
    progress_.update();

    // Emit progress event
    json progress_event = {
        {"total_functions", progress_.total_functions},
        {"completed_functions", progress_.completed_functions},
        {"active_functions", progress_.active_functions},
        {"pending_functions", progress_.pending_functions},
        {"percent_complete", progress_.percent_complete},
        {"completed_function", function_ea},
        {"active_agents", json::object()}
    };

    // Add active agents to event
    for (const auto& [aid, fea] : progress_.active_agents) {
        progress_event["active_agents"][aid] = fea;
    }

    get_event_bus().emit("orchestrator", AgentEvent::AUTO_DECOMPILE_PROGRESS, progress_event);

    // Spawn next agent
    spawn_next_agent();
}

void AutoDecompileManager::on_agent_crashed(const std::string& agent_id) {
    if (!active_) {
        return;
    }

    // Find which function this agent was analyzing
    auto it = agent_to_function_.find(agent_id);
    if (it == agent_to_function_.end()) {
        LOG("AutoDecompileManager: Agent %s crashed but not tracked", agent_id.c_str());
        return;
    }

    ea_t function_ea = it->second;
    std::string func_name = get_function_display_name(function_ea);

    // Look up original priority for retry
    auto priority_it = function_priorities_.find(function_ea);
    if (priority_it == function_priorities_.end()) {
        LOG("AutoDecompileManager: CRITICAL - No priority found for function 0x%llx", (unsigned long long)function_ea);
        // This shouldn't happen, but handle gracefully
        // Treat as permanent failure
        failed_functions_.insert(function_ea);
        progress_.failed_functions++;
        agent_to_function_.erase(it);
        progress_.active_agents.erase(agent_id);
        progress_.update();
        spawn_next_agent();
        return;
    }

    FunctionPriority priority = priority_it->second;

    // Get current retry count
    int retry_count = function_retry_count_[function_ea];  // Defaults to 0 if not found

    if (retry_count < MAX_FUNCTION_RETRIES) {
        // Retry the function
        retry_count++;
        function_retry_count_[function_ea] = retry_count;

        LOG("AutoDecompileManager: Agent %s crashed analyzing 0x%llx (%s) - retrying (attempt %d/%d)",
            agent_id.c_str(), (unsigned long long)function_ea, func_name.c_str(),
            retry_count, MAX_FUNCTION_RETRIES);

        // Re-queue with original priority
        pending_functions_.push(priority);

        // Remove from active tracking
        agent_to_function_.erase(it);
        progress_.active_agents.erase(agent_id);
        progress_.update();

        // Emit retry event
        json event_data = {
            {"agent_id", agent_id},
            {"function", function_ea},
            {"function_name", func_name},
            {"retry_attempt", retry_count},
            {"max_retries", MAX_FUNCTION_RETRIES}
        };
        get_event_bus().emit("orchestrator", AgentEvent::AUTO_DECOMPILE_PROGRESS, event_data);

    } else {
        // Max retries exceeded - permanent failure
        LOG("AutoDecompileManager: Agent %s crashed analyzing 0x%llx (%s) - MAX RETRIES EXCEEDED, marking as failed",
            agent_id.c_str(), (unsigned long long)function_ea, func_name.c_str());

        // Mark as permanently failed
        failed_functions_.insert(function_ea);
        progress_.failed_functions++;

        // Remove from active tracking
        agent_to_function_.erase(it);
        progress_.active_agents.erase(agent_id);
        progress_.update();

        // Emit failure event
        json event_data = {
            {"agent_id", agent_id},
            {"function", function_ea},
            {"function_name", func_name},
            {"reason", "max_retries_exceeded"},
            {"retry_count", retry_count}
        };
        get_event_bus().emit("orchestrator", AgentEvent::AUTO_DECOMPILE_PROGRESS, event_data);
    }

    // Spawn next agent to keep work flowing
    spawn_next_agent();
}

void AutoDecompileManager::check_agent_health() {
    if (!active_) {
        return;
    }

    std::vector<std::string> dead_agents;

    // Check each active agent to see if its process is still alive
    for (const auto& [agent_id, function_ea] : agent_to_function_) {
        // Check if agent process has exited (same pattern as orchestrator)
        bool process_exited = false;
        auto agent_it = orchestrator_->agents_.find(agent_id);
        if (agent_it != orchestrator_->agents_.end()) {
            int pid = agent_it->second.process_id;
            if (pid > 0 && !orchestrator_->agent_spawner_->is_agent_running(pid)) {
                process_exited = true;

                // Check if agent sent a result before dying
                bool has_result = (orchestrator_->completed_agents_.count(agent_id) > 0);

                if (!has_result) {
                    // Agent crashed without sending result!
                    std::string func_name = get_function_display_name(function_ea);
                    LOG("AutoDecompileManager: Agent %s (analyzing 0x%llx %s) crashed - process %d exited without sending result",
                        agent_id.c_str(), (unsigned long long)function_ea, func_name.c_str(), pid);
                    dead_agents.push_back(agent_id);
                }
            }
        }
    }

    // Clean up crashed agents
    for (const std::string& agent_id : dead_agents) {
        // Handle crashed agent with retry logic
        on_agent_crashed(agent_id);
    }
}

AnalysisProgress AutoDecompileManager::get_progress() const {
    return progress_;
}

void AutoDecompileManager::stop_analysis() {
    if (!active_) {
        return;
    }

    LOG("AutoDecompileManager: Stopping auto decompile");

    active_ = false;

    // Clear queues
    while (!pending_functions_.empty()) {
        pending_functions_.pop();
    }

    // Clear retry tracking
    function_priorities_.clear();
    function_retry_count_.clear();
    failed_functions_.clear();

    // Note: We don't kill active agents, we just stop spawning new ones
    // The active agents will complete their work

    // Emit stopped event
    json event_data = {
        {"total_functions", progress_.total_functions},
        {"completed_functions", progress_.completed_functions},
        {"stopped", true}
    };
    get_event_bus().emit("orchestrator", AgentEvent::AUTO_DECOMPILE_COMPLETED, event_data);
}

} // namespace llm_re::orchestrator
