#pragma once

#include "core/common.h"
#include "core/config.h"
#include <vector>
#include <string>
#include <memory>

namespace llm_re {

// Represents a function with its priority score for analysis
struct FunctionPriority {
    ea_t address;
    double score;
    std::string reason;
    std::vector<std::string> metrics;  // Detailed metrics: "entry_point", "10_long_strings", etc.

    // For sorting in descending order (highest priority first)
    bool operator<(const FunctionPriority& other) const {
        return score > other.score;  // Reverse comparison for descending sort
    }
};

// Base class for function prioritization heuristics
class FunctionHeuristic {
public:
    virtual ~FunctionHeuristic() = default;

    // Score a function (higher = higher priority)
    virtual double score(ea_t function_ea) = 0;

    // Heuristic name for debugging/logging
    virtual std::string name() const = 0;

    // Optional: explain why this score was given
    virtual std::string explain(ea_t function_ea) { return ""; }
};

// Prioritizes entry points (main, DllMain, exported functions, etc.)
class EntryPointHeuristic : public FunctionHeuristic {
public:
    using EntryPointMode = Config::SwarmSettings::EntryPointMode;

    EntryPointHeuristic() : mode_(EntryPointMode::BOTTOM_UP) {}

    void set_mode(EntryPointMode mode) { mode_ = mode; }
    EntryPointMode get_mode() const { return mode_; }

    double score(ea_t function_ea) override;
    std::string name() const override { return "EntryPoint"; }
    std::string explain(ea_t function_ea) override;

private:
    bool is_entry_point(ea_t function_ea);
    bool is_exported(ea_t function_ea);
    bool is_main_function(ea_t function_ea);

    EntryPointMode mode_;
};

// Prioritizes functions with many long strings (likely interesting logic)
class StringHeavyHeuristic : public FunctionHeuristic {
public:
    explicit StringHeavyHeuristic(int min_string_length = 10)
        : min_string_length_(min_string_length) {}

    double score(ea_t function_ea) override;
    std::string name() const override { return "StringHeavy"; }
    std::string explain(ea_t function_ea) override;

private:
    int min_string_length_;

    int count_long_strings_in_function(ea_t function_ea);
};

// Prioritizes functions that call APIs (library functions)
// Functions with API calls reveal their purpose through API names
class APICallHeuristic : public FunctionHeuristic {
public:
    double score(ea_t function_ea) override;
    std::string name() const override { return "APICall"; }
    std::string explain(ea_t function_ea) override;

private:
    int count_api_calls(ea_t function_ea);
};

// Prioritizes functions called by many other functions (high-impact utilities)
class CallerCountHeuristic : public FunctionHeuristic {
public:
    double score(ea_t function_ea) override;
    std::string name() const override { return "CallerCount"; }
    std::string explain(ea_t function_ea) override;

private:
    int count_callers(ea_t function_ea);
};

// NEGATIVE priority for functions that call many internal functions
// These need their callees analyzed first (bottom-up analysis)
class InternalCalleeHeuristic : public FunctionHeuristic {
public:
    double score(ea_t function_ea) override;
    std::string name() const override { return "InternalCallee"; }
    std::string explain(ea_t function_ea) override;

private:
    int count_internal_callees(ea_t function_ea);
};

// Prioritizes smaller functions (easier wins, builds momentum)
class FunctionSizeHeuristic : public FunctionHeuristic {
public:
    double score(ea_t function_ea) override;
    std::string name() const override { return "FunctionSize"; }
    std::string explain(ea_t function_ea) override;
};

// Main function prioritizer that combines multiple heuristics
class FunctionPrioritizer {
public:
    FunctionPrioritizer();
    explicit FunctionPrioritizer(const Config& config);

    // Prioritize a list of functions
    std::vector<FunctionPriority> prioritize_functions(
        const std::vector<ea_t>& functions
    );

    // Add a custom heuristic
    void add_heuristic(std::unique_ptr<FunctionHeuristic> heuristic, double weight = 1.0);

    // Configure from Config
    void configure(const Config& config);

private:
    struct WeightedHeuristic {
        std::unique_ptr<FunctionHeuristic> heuristic;
        double weight;
    };

    std::vector<WeightedHeuristic> heuristics_;

    // Compute priority for a single function
    FunctionPriority compute_priority(ea_t function_ea);
};

} // namespace llm_re
