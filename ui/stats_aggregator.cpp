#include "ui/stats_aggregator.h"
#include "agent/tool_system.h"
#include <algorithm>

namespace llm_re::ui {

StatsAggregator::StatsAggregator(const tools::ToolRegistry& registry)
    : registry_(registry) {}

std::vector<ToolStatsSummary> StatsAggregator::get_top_tools_by_usage(size_t limit) const {
    std::vector<ToolStatsSummary> summaries;
    
    // Get tool stats directly from the registry
    const auto& tool_stats = registry_.get_tool_stats();
    
    for (const auto& [tool_name, stats] : tool_stats) {
        if (stats.execution_count > 0) {
            ToolStatsSummary summary;
            summary.name = tool_name;
            summary.execution_count = stats.execution_count;
            summary.success_count = stats.success_count;
            summary.failure_count = stats.failure_count;
            summary.average_duration_ms = stats.execution_count > 0 
                ? stats.total_duration_ms / stats.execution_count 
                : 0.0;
            summary.success_rate = stats.execution_count > 0
                ? static_cast<double>(stats.success_count) / stats.execution_count
                : 0.0;
            
            summaries.push_back(summary);
        }
    }
    
    // Sort by execution count descending
    std::sort(summaries.begin(), summaries.end(),
              [](const auto& a, const auto& b) {
                  return a.execution_count > b.execution_count;
              });
    
    // Limit to requested number
    if (summaries.size() > limit) {
        summaries.resize(limit);
    }
    
    return summaries;
}

int StatsAggregator::get_total_executions() const {
    int total = 0;
    const auto& tool_stats = registry_.get_tool_stats();
    
    for (const auto& [_, stats] : tool_stats) {
        total += stats.execution_count;
    }
    
    return total;
}

int StatsAggregator::get_total_successes() const {
    int total = 0;
    const auto& tool_stats = registry_.get_tool_stats();
    
    for (const auto& [_, stats] : tool_stats) {
        total += stats.success_count;
    }
    
    return total;
}

double StatsAggregator::get_total_duration_ms() const {
    double total = 0.0;
    const auto& tool_stats = registry_.get_tool_stats();
    
    for (const auto& [_, stats] : tool_stats) {
        total += stats.total_duration_ms;
    }
    
    return total;
}

double StatsAggregator::get_overall_success_rate() const {
    int executions = get_total_executions();
    if (executions == 0) return 0.0;
    
    return static_cast<double>(get_total_successes()) / executions;
}

int StatsAggregator::get_unique_tools_used() const {
    int count = 0;
    const auto& tool_stats = registry_.get_tool_stats();
    
    for (const auto& [_, stats] : tool_stats) {
        if (stats.execution_count > 0) {
            count++;
        }
    }
    
    return count;
}

} // namespace llm_re::ui