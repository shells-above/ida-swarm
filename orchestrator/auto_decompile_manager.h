#pragma once

#include "core/common.h"
#include "core/config.h"
#include "analysis/function_prioritizer.h"
#include <queue>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <chrono>

namespace llm_re::orchestrator {

// Forward declaration
class Orchestrator;

// Progress tracking for auto-decompile
struct AnalysisProgress {
    size_t total_functions = 0;
    size_t completed_functions = 0;
    size_t failed_functions = 0;
    size_t active_functions = 0;
    size_t pending_functions = 0;
    double percent_complete = 0.0;

    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_update;

    // Currently analyzing functions
    std::map<std::string, ea_t> active_agents;  // agent_id -> function_ea

    // Completed functions list (for reporting)
    std::vector<ea_t> completed_function_addresses;

    // Calculate derived stats
    void update() {
        active_functions = active_agents.size();
        pending_functions = total_functions - completed_functions - failed_functions - active_functions;
        if (total_functions > 0) {
            percent_complete = ((completed_functions + failed_functions) * 100.0) / total_functions;
        }
        last_update = std::chrono::steady_clock::now();
    }

    // Get elapsed time in seconds
    double elapsed_seconds() const {
        auto now = std::chrono::steady_clock::now();
        return std::chrono::duration<double>(now - start_time).count();
    }

    // Estimate remaining time based on current rate
    double estimated_remaining_seconds() const {
        if (completed_functions == 0) return -1.0;  // Can't estimate yet

        double elapsed = elapsed_seconds();
        double rate = completed_functions / elapsed;  // functions per second
        if (rate <= 0) return -1.0;

        size_t remaining = total_functions - completed_functions;
        return remaining / rate;
    }

    // Get current analysis rate in functions per minute
    double functions_per_minute() const {
        if (completed_functions == 0) return 0.0;

        double elapsed = elapsed_seconds();
        if (elapsed <= 0) return 0.0;

        double rate_per_second = completed_functions / elapsed;
        return rate_per_second * 60.0;  // Convert to per minute
    }
};

// Manages the auto-decompile workflow
class AutoDecompileManager {
public:
    explicit AutoDecompileManager(Orchestrator* orchestrator);

    // Start auto-decompile
    void start_auto_decompile();

    // Called when an agent completes successfully
    void on_agent_completed(const std::string& agent_id);

    // Called when an agent crashes
    void on_agent_crashed(const std::string& agent_id);

    // Get current progress
    AnalysisProgress get_progress() const;

    // Check if analysis is active
    bool is_active() const { return active_; }

    // Stop analysis (if running)
    void stop_analysis();

private:
    // Enumerate all non-library functions in the database
    std::vector<ea_t> enumerate_non_library_functions();

    // Spawn the next agent from the queue
    void spawn_next_agent();

    // Generate task prompt for analyzing a function
    std::string generate_function_analysis_prompt(ea_t function_ea, const FunctionPriority& priority);

    // Get function name (or address if unnamed)
    std::string get_function_display_name(ea_t function_ea);

    // Check for crashed/dead agents and clean them up
    void check_agent_health();

    // State
    Orchestrator* orchestrator_;
    const Config& config_;
    FunctionPrioritizer prioritizer_;

    bool active_ = false;
    std::queue<FunctionPriority> pending_functions_;
    AnalysisProgress progress_;
    std::set<ea_t> completed_functions_;

    // Track which agent is analyzing which function
    std::map<std::string, ea_t> agent_to_function_;

    // Retry tracking
    std::map<ea_t, FunctionPriority> function_priorities_;  // Original priorities for retry
    std::map<ea_t, int> function_retry_count_;              // Retry attempts per function
    std::set<ea_t> failed_functions_;                        // Permanently failed functions
    static constexpr int MAX_FUNCTION_RETRIES = 3;
};

} // namespace llm_re::orchestrator
