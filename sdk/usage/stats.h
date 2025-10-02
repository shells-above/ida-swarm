#pragma once

#include "../client/client.h"
#include "pricing.h"
#include <vector>
#include <chrono>

namespace claude::usage {

/**
 * Unified token statistics tracking
 * Combines TokenTracker and CacheStats into a single class
 */
class TokenStats {
public:
    TokenStats() : session_total_(), session_start_(std::chrono::steady_clock::now()) { }

    // Add usage from an API response
    void add_usage(const claude::TokenUsage& usage) {
        session_total_ += usage;
        history_.emplace_back(std::chrono::steady_clock::now(), usage);
    }

    // Get cumulative totals
    claude::TokenUsage get_total() const {
        return session_total_;
    }

    // Get last usage entry
    claude::TokenUsage get_last_usage() const {
        if (history_.empty()) {
            return claude::TokenUsage{};
        }
        return history_.back().second;
    }

    // Get total cost for session
    double get_total_cost() const {
        return PricingModel::calculate_cost(session_total_);
    }

    // Reset all statistics
    void reset() {
        session_total_ = claude::TokenUsage{};
        history_.clear();
        session_start_ = std::chrono::steady_clock::now();
    }

    // Export to JSON
    json to_json() const {
        json j;
        j["session_total"] = session_total_.to_json();
        j["total_cost"] = get_total_cost();

        auto duration = std::chrono::steady_clock::now() - session_start_;
        j["session_duration_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        j["history_count"] = history_.size();
        
        return j;
    }

    // Get formatted summary string for logging
    std::string get_summary() const {
        std::stringstream ss;
        const claude::TokenUsage& total = session_total_;
        
        ss << "Tokens: " << total.input_tokens << " in, " << total.output_tokens << " out";
        ss << " [" << total.cache_read_tokens << " cache read, " << total.cache_creation_tokens << " cache write]";
        ss << " | Cost: $" << std::fixed << std::setprecision(4) << get_total_cost();
        
        return ss.str();
    }

    // Get iteration-specific summary
    std::string get_iteration_summary(const claude::TokenUsage& usage, int iteration) const {
        std::stringstream ss;
        ss << "[Iteration " << iteration << "] ";
        ss << "Tokens: " << usage.input_tokens << " in, " << usage.output_tokens << " out";
        ss << " [" << usage.cache_read_tokens << " cache read, " << usage.cache_creation_tokens << " cache write]";
        ss << " | Total " << get_summary();
        return ss.str();
    }

private:
    claude::TokenUsage session_total_;
    std::vector<std::pair<std::chrono::steady_clock::time_point, claude::TokenUsage>> history_;
    std::chrono::steady_clock::time_point session_start_;
};

} // namespace claude::usage