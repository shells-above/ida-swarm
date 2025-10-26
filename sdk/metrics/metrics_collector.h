//
// Metrics Collection Interface for Claude SDK
// Allows optional injection of metrics collection without coupling to specific implementations
//

#ifndef CLAUDE_SDK_METRICS_COLLECTOR_H
#define CLAUDE_SDK_METRICS_COLLECTOR_H

#include <string>
#include <chrono>
#include <cstdint>

namespace claude {

// Component types for metrics tracking
enum class MetricsComponent {
    ORCHESTRATOR,
    AGENT,
    GRADER,
    UNKNOWN
};

// API request metric data
struct ApiMetric {
    std::string component_id;               // e.g., "orchestrator", "agent_1"
    MetricsComponent component;
    int64_t duration_ms;
    int input_tokens;
    int output_tokens;
    int cache_read_tokens;
    int cache_creation_tokens;
    std::string model;
    std::chrono::system_clock::time_point timestamp;
    int iteration;

    ApiMetric()
        : component(MetricsComponent::UNKNOWN)
        , duration_ms(0)
        , input_tokens(0)
        , output_tokens(0)
        , cache_read_tokens(0)
        , cache_creation_tokens(0)
        , iteration(0)
        , timestamp(std::chrono::system_clock::now())
    {}
};

// Tool execution metric data
struct ToolMetric {
    std::string component_id;
    std::string tool_name;
    int64_t duration_ms;
    bool success;
    std::chrono::system_clock::time_point timestamp;
    int iteration;

    ToolMetric()
        : duration_ms(0)
        , success(false)
        , iteration(0)
        , timestamp(std::chrono::system_clock::now())
    {}
};

/**
 * Interface for collecting metrics from the Claude SDK.
 *
 * Implementations can track performance, token usage, and other metrics.
 * The SDK works without any metrics collector (uses NullMetricsCollector by default).
 */
class IMetricsCollector {
public:
    virtual ~IMetricsCollector() = default;

    /**
     * Record an API request metric.
     * Called after each successful API call with timing and token information.
     */
    virtual void record_api_request(const ApiMetric& metric) = 0;

    /**
     * Record a tool execution metric.
     * Called after each tool execution with timing and success information.
     */
    virtual void record_tool_execution(const ToolMetric& metric) = 0;

    /**
     * Check if metrics collection is enabled.
     * Allows implementations to disable collection without being removed.
     */
    virtual bool is_enabled() const = 0;
};

/**
 * Null implementation that does nothing.
 * Used as default when no metrics collector is provided.
 */
class NullMetricsCollector : public IMetricsCollector {
public:
    void record_api_request(const ApiMetric& /*metric*/) override {
        // No-op
    }

    void record_tool_execution(const ToolMetric& /*metric*/) override {
        // No-op
    }

    bool is_enabled() const override {
        return false;
    }
};

} // namespace claude

#endif // CLAUDE_SDK_METRICS_COLLECTOR_H
