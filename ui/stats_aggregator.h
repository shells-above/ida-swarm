#pragma once

#include <vector>
#include <string>
#include <chrono>

namespace llm_re::tools {
    class ToolRegistry;
}

namespace llm_re::ui {

struct ToolStatsSummary {
    std::string name;
    int execution_count;
    int success_count;
    int failure_count;
    double average_duration_ms;
    double success_rate;
};

class StatsAggregator {
private:
    const tools::ToolRegistry& registry_;
    
public:
    explicit StatsAggregator(const tools::ToolRegistry& registry);
    
    // Get top N tools by execution count
    std::vector<ToolStatsSummary> get_top_tools_by_usage(size_t limit = 5) const;
    
    // Get aggregate statistics
    int get_total_executions() const;
    int get_total_successes() const;
    double get_total_duration_ms() const;
    double get_overall_success_rate() const;
    int get_unique_tools_used() const;
};

} // namespace llm_re::ui