#pragma once

#include "anthropic_api.h"
#include "pricing.h"
#include <vector>
#include <chrono>

namespace llm_re::api {

/**
 * Unified token statistics tracking
 * Combines TokenTracker and CacheStats into a single class
 */
class TokenStats {
public:
    TokenStats() : session_start_(std::chrono::steady_clock::now()) {}

    // Add usage from an API response
    void add_usage(const TokenUsage& usage) {
        session_total_ += usage;
        history_.emplace_back(std::chrono::steady_clock::now(), usage);
        
        // Update cache statistics
        if (usage.cache_read_tokens > 0 || usage.input_tokens > 0) {
            cache_hits_ += usage.cache_read_tokens;
            cache_misses_ += usage.input_tokens;
            
            if (usage.cache_creation_tokens > 0) {
                cache_writes_++;
            }
            
            // Calculate and accumulate savings
            cache_savings_ += PricingModel::calculate_cache_savings(usage);
        }
    }

    // Get cumulative totals
    TokenUsage get_total() const {
        return session_total_;
    }

    // Get last usage entry
    TokenUsage get_last_usage() const {
        if (history_.empty()) {
            return TokenUsage{};
        }
        return history_.back().second;
    }

    // Get cache hit rate
    double get_cache_hit_rate() const {
        int total = cache_hits_ + cache_misses_;
        return total > 0 ? static_cast<double>(cache_hits_) / total : 0.0;
    }

    // Get total cost for session
    double get_total_cost() const {
        return PricingModel::calculate_cost(session_total_);
    }

    // Get cache savings
    double get_cache_savings() const {
        return cache_savings_;
    }

    // Reset all statistics
    void reset() {
        session_total_ = TokenUsage{};
        history_.clear();
        session_start_ = std::chrono::steady_clock::now();
        cache_hits_ = 0;
        cache_misses_ = 0;
        cache_writes_ = 0;
        cache_savings_ = 0.0;
    }

    // Export to JSON
    json to_json() const {
        json j;
        j["session_total"] = session_total_.to_json();
        j["total_cost"] = get_total_cost();
        j["cache_hit_rate"] = get_cache_hit_rate();
        j["cache_hits"] = cache_hits_;
        j["cache_misses"] = cache_misses_;
        j["cache_writes"] = cache_writes_;
        j["cache_savings"] = cache_savings_;
        
        auto duration = std::chrono::steady_clock::now() - session_start_;
        j["session_duration_seconds"] = std::chrono::duration_cast<std::chrono::seconds>(duration).count();
        j["history_count"] = history_.size();
        
        return j;
    }

    // Get formatted summary string for logging
    std::string get_summary() const {
        std::stringstream ss;
        const TokenUsage& total = session_total_;
        
        ss << "Tokens: " << total.input_tokens << " in, " << total.output_tokens << " out";
        ss << " [" << total.cache_read_tokens << " cache read, " << total.cache_creation_tokens << " cache write]";
        ss << " | Cost: $" << std::fixed << std::setprecision(4) << get_total_cost();
        
        if (cache_hits_ + cache_misses_ > 0) {
            ss << " | Cache: " << std::fixed << std::setprecision(1) 
               << (get_cache_hit_rate() * 100) << "% hit rate";
            ss << ", $" << std::fixed << std::setprecision(4) << cache_savings_ << " saved";
        }
        
        return ss.str();
    }

    // Get iteration-specific summary
    std::string get_iteration_summary(const TokenUsage& usage, int iteration) const {
        std::stringstream ss;
        ss << "[Iteration " << iteration << "] ";
        ss << "Tokens: " << usage.input_tokens << " in, " << usage.output_tokens << " out";
        ss << " [" << usage.cache_read_tokens << " cache read, " << usage.cache_creation_tokens << " cache write]";
        ss << " | Total " << get_summary();
        return ss.str();
    }

private:
    TokenUsage session_total_;
    std::vector<std::pair<std::chrono::steady_clock::time_point, TokenUsage>> history_;
    std::chrono::steady_clock::time_point session_start_;
    
    // Cache statistics
    int cache_hits_ = 0;
    int cache_misses_ = 0;
    int cache_writes_ = 0;
    double cache_savings_ = 0.0;
};

} // namespace llm_re::api