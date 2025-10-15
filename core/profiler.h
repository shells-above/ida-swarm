//
// Performance Profiler for IDA Swarm
// Tracks API request timing, tool execution timing, and token usage
//

#ifndef PROFILER_H
#define PROFILER_H

#include <nlohmann/json.hpp>
#include <chrono>
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <atomic>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <numeric>

using json = nlohmann::json;

namespace llm_re::profiling {

// Metric types
enum class MetricType {
    API_REQUEST,
    TOOL_EXECUTION,
    GRADER_EVALUATION,
    OTHER
};

// Component types
enum class Component {
    ORCHESTRATOR,
    AGENT,
    GRADER,
    UNKNOWN
};

inline std::string metric_type_to_string(MetricType type) {
    switch (type) {
        case MetricType::API_REQUEST: return "API_REQUEST";
        case MetricType::TOOL_EXECUTION: return "TOOL_EXECUTION";
        case MetricType::GRADER_EVALUATION: return "GRADER_EVALUATION";
        case MetricType::OTHER: return "OTHER";
    }
    return "UNKNOWN";
}

inline std::string component_to_string(Component comp) {
    switch (comp) {
        case Component::ORCHESTRATOR: return "ORCHESTRATOR";
        case Component::AGENT: return "AGENT";
        case Component::GRADER: return "GRADER";
        case Component::UNKNOWN: return "UNKNOWN";
    }
    return "UNKNOWN";
}

// API request metric
struct ApiRequestMetric {
    std::string component_id;               // e.g., "orchestrator", "agent_1"
    Component component;
    int64_t duration_ms;
    int input_tokens;
    int output_tokens;
    int cache_read_tokens;
    int cache_creation_tokens;
    std::string model;
    std::chrono::system_clock::time_point timestamp;
    int iteration;

    json to_json() const {
        return {
            {"component_id", component_id},
            {"component", component_to_string(component)},
            {"duration_ms", duration_ms},
            {"input_tokens", input_tokens},
            {"output_tokens", output_tokens},
            {"cache_read_tokens", cache_read_tokens},
            {"cache_creation_tokens", cache_creation_tokens},
            {"model", model},
            {"iteration", iteration},
            {"timestamp", std::chrono::system_clock::to_time_t(timestamp)}
        };
    }
};

// Tool execution metric
struct ToolExecutionMetric {
    std::string component_id;
    std::string tool_name;
    int64_t duration_ms;
    bool success;
    std::chrono::system_clock::time_point timestamp;
    int iteration;

    json to_json() const {
        return {
            {"component_id", component_id},
            {"tool_name", tool_name},
            {"duration_ms", duration_ms},
            {"success", success},
            {"iteration", iteration},
            {"timestamp", std::chrono::system_clock::to_time_t(timestamp)}
        };
    }
};

// Simple timer class
class Timer {
public:
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}

    void reset() {
        start_ = std::chrono::high_resolution_clock::now();
    }

    int64_t elapsed_ms() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
    }

    int64_t elapsed_us() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::microseconds>(now - start_).count();
    }

private:
    std::chrono::high_resolution_clock::time_point start_;
};

// Statistics helper
template<typename T>
struct Stats {
    T min = 0;
    T max = 0;
    T sum = 0;
    T mean = 0;
    T median = 0;
    size_t count = 0;

    void compute(const std::vector<T>& values) {
        if (values.empty()) {
            count = 0;
            return;
        }

        count = values.size();
        min = *std::min_element(values.begin(), values.end());
        max = *std::max_element(values.begin(), values.end());
        sum = std::accumulate(values.begin(), values.end(), T(0));
        mean = sum / count;

        // Compute median
        std::vector<T> sorted = values;
        std::sort(sorted.begin(), sorted.end());
        if (sorted.size() % 2 == 0) {
            median = (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) / 2;
        } else {
            median = sorted[sorted.size() / 2];
        }
    }

    json to_json() const {
        return {
            {"count", count},
            {"min", min},
            {"max", max},
            {"sum", sum},
            {"mean", mean},
            {"median", median}
        };
    }
};

// Main profiler class
class Profiler {
public:
    static Profiler& instance() {
        static Profiler instance;
        return instance;
    }

    // Delete copy/move
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    void enable() { enabled_ = true; }
    void disable() { enabled_ = false; }
    bool is_enabled() const { return enabled_; }

    // Record API request
    void record_api_request(const ApiRequestMetric& metric) {
        if (!enabled_) return;

        std::lock_guard<std::mutex> lock(mutex_);
        api_requests_.push_back(metric);
        total_api_time_ms_ += metric.duration_ms;
        total_input_tokens_ += metric.input_tokens;
        total_output_tokens_ += metric.output_tokens;
        total_cache_read_tokens_ += metric.cache_read_tokens;
        total_cache_creation_tokens_ += metric.cache_creation_tokens;
    }

    // Record tool execution
    void record_tool_execution(const ToolExecutionMetric& metric) {
        if (!enabled_) return;

        std::lock_guard<std::mutex> lock(mutex_);
        tool_executions_.push_back(metric);
        total_tool_time_ms_ += metric.duration_ms;
    }

    // Get summary statistics
    json get_summary() const {
        std::lock_guard<std::mutex> lock(mutex_);

        json summary;
        summary["enabled"] = enabled_.load();
        summary["session_start"] = std::chrono::system_clock::to_time_t(session_start_);

        auto now = std::chrono::system_clock::now();
        auto session_duration = std::chrono::duration_cast<std::chrono::seconds>(now - session_start_).count();
        summary["session_duration_seconds"] = session_duration;

        // API statistics
        json api_stats;
        api_stats["total_requests"] = api_requests_.size();
        api_stats["total_time_ms"] = total_api_time_ms_.load();
        api_stats["total_input_tokens"] = total_input_tokens_.load();
        api_stats["total_output_tokens"] = total_output_tokens_.load();
        api_stats["total_cache_read_tokens"] = total_cache_read_tokens_.load();
        api_stats["total_cache_creation_tokens"] = total_cache_creation_tokens_.load();

        // Compute timing statistics
        if (!api_requests_.empty()) {
            std::vector<int64_t> durations;
            durations.reserve(api_requests_.size());
            for (const auto& req : api_requests_) {
                durations.push_back(req.duration_ms);
            }

            Stats<int64_t> timing_stats;
            timing_stats.compute(durations);
            api_stats["timing"] = timing_stats.to_json();
        }

        // Per-component breakdown
        json component_stats = json::object();
        std::map<Component, std::vector<int64_t>> component_durations;
        std::map<Component, int64_t> component_tokens;

        for (const auto& req : api_requests_) {
            component_durations[req.component].push_back(req.duration_ms);
            component_tokens[req.component] += req.input_tokens + req.output_tokens;
        }

        for (const auto& [comp, durations] : component_durations) {
            Stats<int64_t> comp_stats;
            comp_stats.compute(durations);

            json comp_data;
            comp_data["timing"] = comp_stats.to_json();
            comp_data["total_tokens"] = component_tokens[comp];
            component_stats[component_to_string(comp)] = comp_data;
        }
        api_stats["by_component"] = component_stats;

        summary["api_requests"] = api_stats;

        // Tool statistics
        json tool_stats;
        tool_stats["total_executions"] = tool_executions_.size();
        tool_stats["total_time_ms"] = total_tool_time_ms_.load();

        if (!tool_executions_.empty()) {
            std::vector<int64_t> durations;
            durations.reserve(tool_executions_.size());
            for (const auto& tool : tool_executions_) {
                durations.push_back(tool.duration_ms);
            }

            Stats<int64_t> timing_stats;
            timing_stats.compute(durations);
            tool_stats["timing"] = timing_stats.to_json();
        }

        // Per-tool breakdown
        json per_tool_stats = json::object();
        std::map<std::string, std::vector<int64_t>> tool_durations;
        std::map<std::string, size_t> tool_counts;

        for (const auto& tool : tool_executions_) {
            tool_durations[tool.tool_name].push_back(tool.duration_ms);
            tool_counts[tool.tool_name]++;
        }

        for (const auto& [name, durations] : tool_durations) {
            Stats<int64_t> tool_stat;
            tool_stat.compute(durations);

            json tool_data;
            tool_data["timing"] = tool_stat.to_json();
            tool_data["count"] = tool_counts[name];
            per_tool_stats[name] = tool_data;
        }
        tool_stats["by_tool"] = per_tool_stats;

        summary["tool_executions"] = tool_stats;

        // Overall breakdown
        json overall;
        int64_t total_time = total_api_time_ms_ + total_tool_time_ms_;
        if (total_time > 0) {
            overall["total_time_ms"] = total_time;
            overall["api_time_ms"] = total_api_time_ms_.load();
            overall["tool_time_ms"] = total_tool_time_ms_.load();
            overall["api_percentage"] = (total_api_time_ms_.load() * 100.0) / total_time;
            overall["tool_percentage"] = (total_tool_time_ms_.load() * 100.0) / total_time;
        }
        summary["overall"] = overall;

        // Throughput analysis (multiple perspectives for different use cases)
        if (session_duration > 0) {
            json throughput;

            // Calculate different token totals
            int64_t input_total = total_input_tokens_.load();
            int64_t output_total = total_output_tokens_.load();
            int64_t cache_read_total = total_cache_read_tokens_.load();
            int64_t cache_creation_total = total_cache_creation_tokens_.load();

            // Different aggregations for different use cases
            int64_t new_tokens = input_total + output_total;  // Excludes cache
            int64_t processed_tokens = input_total + output_total + cache_creation_total;  // Includes cache creation (actual processing)
            int64_t all_tokens = input_total + output_total + cache_creation_total + cache_read_total;  // All API activity

            // Session-level throughput (includes idle time between requests)
            json session_level;
            session_level["output_tokens_per_second"] = output_total / (double)session_duration;
            session_level["new_tokens_per_second"] = new_tokens / (double)session_duration;
            session_level["processed_tokens_per_second"] = processed_tokens / (double)session_duration;
            session_level["all_tokens_per_second"] = all_tokens / (double)session_duration;
            throughput["session_level"] = session_level;

            // API-level throughput (excludes idle time, only counts time in API calls)
            double total_api_time_seconds = total_api_time_ms_.load() / 1000.0;
            if (total_api_time_seconds > 0) {
                json api_level;
                api_level["output_tokens_per_second"] = output_total / total_api_time_seconds;
                api_level["new_tokens_per_second"] = new_tokens / total_api_time_seconds;
                api_level["processed_tokens_per_second"] = processed_tokens / total_api_time_seconds;
                api_level["all_tokens_per_second"] = all_tokens / total_api_time_seconds;
                throughput["api_level"] = api_level;
            }

            // Request rate
            throughput["api_requests_per_minute"] = (api_requests_.size() * 60.0) / session_duration;

            // Token totals for reference
            json token_totals;
            token_totals["input_tokens"] = input_total;
            token_totals["output_tokens"] = output_total;
            token_totals["cache_read_tokens"] = cache_read_total;
            token_totals["cache_creation_tokens"] = cache_creation_total;
            token_totals["new_tokens"] = new_tokens;
            token_totals["processed_tokens"] = processed_tokens;
            token_totals["all_tokens"] = all_tokens;
            throughput["token_totals"] = token_totals;

            summary["throughput"] = throughput;
        }

        return summary;
    }

    // Get all API request metrics
    std::vector<ApiRequestMetric> get_api_requests() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return api_requests_;
    }

    // Get all tool execution metrics
    std::vector<ToolExecutionMetric> get_tool_executions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tool_executions_;
    }

    // Print human-readable report
    void print_report(std::ostream& os) const {
        json summary = get_summary();

        os << "\n========================================\n";
        os << "       IDA SWARM PROFILING REPORT       \n";
        os << "========================================\n\n";

        // Session info
        os << "Session Duration: " << summary["session_duration_seconds"].get<int64_t>() << " seconds\n\n";

        // API Statistics
        os << "--- API REQUESTS ---\n";
        if (summary["api_requests"]["total_requests"].get<size_t>() > 0) {
            auto api = summary["api_requests"];
            os << "  Total Requests: " << api["total_requests"] << "\n";
            os << "  Total Time: " << api["total_time_ms"] << " ms\n";
            os << "  Input Tokens: " << api["total_input_tokens"] << "\n";
            os << "  Output Tokens: " << api["total_output_tokens"] << "\n";
            os << "  Cache Read Tokens: " << api["total_cache_read_tokens"] << "\n";
            os << "  Cache Creation Tokens: " << api["total_cache_creation_tokens"] << "\n";

            if (api.contains("timing")) {
                auto timing = api["timing"];
                os << "  Timing: min=" << timing["min"] << "ms, max=" << timing["max"]
                   << "ms, mean=" << timing["mean"] << "ms, median=" << timing["median"] << "ms\n";
            }

            // Per-component
            os << "\n  By Component:\n";
            for (auto& [comp, stats] : api["by_component"].items()) {
                os << "    " << comp << ":\n";
                os << "      Requests: " << stats["timing"]["count"] << "\n";
                os << "      Total Time: " << stats["timing"]["sum"] << " ms\n";
                os << "      Tokens: " << stats["total_tokens"] << "\n";
                os << "      Avg Time: " << stats["timing"]["mean"] << " ms\n";
            }
        } else {
            os << "  No API requests recorded\n";
        }

        // Tool Statistics
        os << "\n--- TOOL EXECUTIONS ---\n";
        if (summary["tool_executions"]["total_executions"].get<size_t>() > 0) {
            auto tools = summary["tool_executions"];
            os << "  Total Executions: " << tools["total_executions"] << "\n";
            os << "  Total Time: " << tools["total_time_ms"] << " ms\n";

            if (tools.contains("timing")) {
                auto timing = tools["timing"];
                os << "  Timing: min=" << timing["min"] << "ms, max=" << timing["max"]
                   << "ms, mean=" << timing["mean"] << "ms, median=" << timing["median"] << "ms\n";
            }

            // Top tools by time
            os << "\n  Top Tools by Total Time:\n";
            std::vector<std::pair<std::string, int64_t>> tool_times;
            for (auto& [name, stats] : tools["by_tool"].items()) {
                tool_times.push_back({name, stats["timing"]["sum"].get<int64_t>()});
            }
            std::sort(tool_times.begin(), tool_times.end(),
                     [](const auto& a, const auto& b) { return a.second > b.second; });

            for (size_t i = 0; i < std::min(size_t(10), tool_times.size()); ++i) {
                const auto& [name, time] = tool_times[i];
                auto stats = tools["by_tool"][name];
                os << "    " << (i+1) << ". " << name << ": " << time << "ms (";
                os << stats["count"] << " calls, avg=" << stats["timing"]["mean"] << "ms)\n";
            }
        } else {
            os << "  No tool executions recorded\n";
        }

        // Overall breakdown
        os << "\n--- OVERALL BREAKDOWN ---\n";
        if (summary["overall"].contains("total_time_ms")) {
            auto overall = summary["overall"];
            os << "  Total Time: " << overall["total_time_ms"] << " ms\n";
            os << "  API Time: " << overall["api_time_ms"] << " ms ("
               << std::fixed << std::setprecision(1) << overall["api_percentage"] << "%)\n";
            os << "  Tool Time: " << overall["tool_time_ms"] << " ms ("
               << std::fixed << std::setprecision(1) << overall["tool_percentage"] << "%)\n";
        }

        // Throughput
        if (summary.contains("throughput")) {
            os << "\n--- THROUGHPUT ---\n";
            auto tp = summary["throughput"];

            // Show token totals for context
            if (tp.contains("token_totals")) {
                auto totals = tp["token_totals"];
                os << "  Token Totals:\n";
                os << "    Input: " << totals["input_tokens"] << "\n";
                os << "    Output: " << totals["output_tokens"] << "\n";
                os << "    Cache Read: " << totals["cache_read_tokens"] << "\n";
                os << "    Cache Creation: " << totals["cache_creation_tokens"] << "\n";
                os << "    New Tokens (input+output): " << totals["new_tokens"] << "\n";
                os << "    Processed Tokens (new+cache_creation): " << totals["processed_tokens"] << "\n";
                os << "    All Tokens (processed+cache_read): " << totals["all_tokens"] << "\n";
            }

            os << "\n  Session-Level (wall-clock time, includes idle):\n";
            if (tp.contains("session_level")) {
                auto session = tp["session_level"];
                os << "    Output Tokens/Second: " << std::fixed << std::setprecision(2)
                   << session["output_tokens_per_second"] << " [generation speed]\n";
                os << "    New Tokens/Second: " << std::fixed << std::setprecision(2)
                   << session["new_tokens_per_second"] << " [input+output]\n";
                os << "    Processed Tokens/Second: " << std::fixed << std::setprecision(2)
                   << session["processed_tokens_per_second"] << " [new+cache_creation]\n";
                os << "    All Tokens/Second: " << std::fixed << std::setprecision(2)
                   << session["all_tokens_per_second"] << " [all API activity]\n";
            }

            if (tp.contains("api_level")) {
                os << "\n  API-Level (active API time only, excludes idle):\n";
                auto api = tp["api_level"];
                os << "    Output Tokens/Second: " << std::fixed << std::setprecision(2)
                   << api["output_tokens_per_second"] << " [generation speed]\n";
                os << "    New Tokens/Second: " << std::fixed << std::setprecision(2)
                   << api["new_tokens_per_second"] << " [input+output]\n";
                os << "    Processed Tokens/Second: " << std::fixed << std::setprecision(2)
                   << api["processed_tokens_per_second"] << " [new+cache_creation]\n";
                os << "    All Tokens/Second: " << std::fixed << std::setprecision(2)
                   << api["all_tokens_per_second"] << " [all API activity]\n";
            }

            os << "\n  Request Rate:\n";
            os << "    API Requests/Minute: " << std::fixed << std::setprecision(2)
               << tp["api_requests_per_minute"] << "\n";
        }

        os << "\n========================================\n";
    }

    // Save full report to file
    void save_report(const std::string& filepath) const {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return;
        }

        print_report(file);
        file.close();
    }

    // Save JSON to file
    void save_json(const std::string& filepath) const {
        std::ofstream file(filepath);
        if (!file.is_open()) {
            return;
        }

        json report;
        report["summary"] = get_summary();

        // Add detailed metrics
        json api_requests = json::array();
        for (const auto& req : api_requests_) {
            api_requests.push_back(req.to_json());
        }
        report["api_requests"] = api_requests;

        json tool_executions = json::array();
        for (const auto& tool : tool_executions_) {
            tool_executions.push_back(tool.to_json());
        }
        report["tool_executions"] = tool_executions;

        file << report.dump(2);
        file.close();
    }

    // Reset all metrics
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        api_requests_.clear();
        tool_executions_.clear();
        total_api_time_ms_ = 0;
        total_tool_time_ms_ = 0;
        total_input_tokens_ = 0;
        total_output_tokens_ = 0;
        total_cache_read_tokens_ = 0;
        total_cache_creation_tokens_ = 0;
        session_start_ = std::chrono::system_clock::now();
    }

private:
    Profiler() : enabled_(false), session_start_(std::chrono::system_clock::now()) {}

    mutable std::mutex mutex_;
    std::atomic<bool> enabled_;

    std::vector<ApiRequestMetric> api_requests_;
    std::vector<ToolExecutionMetric> tool_executions_;

    std::atomic<int64_t> total_api_time_ms_{0};
    std::atomic<int64_t> total_tool_time_ms_{0};
    std::atomic<int64_t> total_input_tokens_{0};
    std::atomic<int64_t> total_output_tokens_{0};
    std::atomic<int64_t> total_cache_read_tokens_{0};
    std::atomic<int64_t> total_cache_creation_tokens_{0};

    std::chrono::system_clock::time_point session_start_;
};

// RAII helper for automatic timing
template<typename MetricType>
class ScopedTimer {
public:
    using Callback = std::function<void(int64_t duration_ms)>;

    ScopedTimer(Callback cb) : callback_(cb) {}

    ~ScopedTimer() {
        if (callback_) {
            callback_(timer_.elapsed_ms());
        }
    }

private:
    Timer timer_;
    Callback callback_;
};

} // namespace llm_re::profiling

#endif // PROFILER_H
