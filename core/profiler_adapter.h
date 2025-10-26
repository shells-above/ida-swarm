//
// Profiler Adapter - Bridges SDK IMetricsCollector interface to core Profiler implementation
//

#ifndef PROFILER_ADAPTER_H
#define PROFILER_ADAPTER_H

#include "../sdk/metrics/metrics_collector.h"
#include "profiler.h"

namespace llm_re {

/**
 * Adapter that implements the SDK's IMetricsCollector interface
 * and forwards calls to the core Profiler singleton.
 *
 * This allows the SDK to remain decoupled from the profiler implementation
 * while still enabling profiling when desired.
 */
class ProfilerAdapter : public claude::IMetricsCollector {
public:
    ProfilerAdapter() = default;
    ~ProfilerAdapter() override = default;

    void record_api_request(const claude::ApiMetric& metric) override {
        // Convert SDK metric to profiler metric
        profiling::ApiRequestMetric profiler_metric;
        profiler_metric.component_id = metric.component_id;
        profiler_metric.component = convert_component(metric.component);
        profiler_metric.duration_ms = metric.duration_ms;
        profiler_metric.input_tokens = metric.input_tokens;
        profiler_metric.output_tokens = metric.output_tokens;
        profiler_metric.cache_read_tokens = metric.cache_read_tokens;
        profiler_metric.cache_creation_tokens = metric.cache_creation_tokens;
        profiler_metric.model = metric.model;
        profiler_metric.timestamp = metric.timestamp;
        profiler_metric.iteration = metric.iteration;

        profiling::Profiler::instance().record_api_request(profiler_metric);
    }

    void record_tool_execution(const claude::ToolMetric& metric) override {
        // Convert SDK metric to profiler metric
        profiling::ToolExecutionMetric profiler_metric;
        profiler_metric.component_id = metric.component_id;
        profiler_metric.tool_name = metric.tool_name;
        profiler_metric.duration_ms = metric.duration_ms;
        profiler_metric.success = metric.success;
        profiler_metric.timestamp = metric.timestamp;
        profiler_metric.iteration = metric.iteration;

        profiling::Profiler::instance().record_tool_execution(profiler_metric);
    }

    bool is_enabled() const override {
        return profiling::Profiler::instance().is_enabled();
    }

private:
    // Convert SDK component type to profiler component type
    profiling::Component convert_component(claude::MetricsComponent component) const {
        switch (component) {
            case claude::MetricsComponent::ORCHESTRATOR:
                return profiling::Component::ORCHESTRATOR;
            case claude::MetricsComponent::AGENT:
                return profiling::Component::AGENT;
            case claude::MetricsComponent::GRADER:
                return profiling::Component::GRADER;
            case claude::MetricsComponent::UNKNOWN:
            default:
                return profiling::Component::UNKNOWN;
        }
    }
};

} // namespace llm_re

#endif // PROFILER_ADAPTER_H
