#include "function_prioritizer.h"
#include "../core/logger.h"
#include "../core/ida_utils.h"
#include <ida.hpp>
#include <funcs.hpp>
#include <name.hpp>
#include <entry.hpp>
#include <bytes.hpp>
#include <xref.hpp>
#include <strlist.hpp>
#include <algorithm>
#include <sstream>

namespace llm_re {

// ==========================
// EntryPointHeuristic
// ==========================

bool EntryPointHeuristic::is_entry_point(ea_t function_ea) {
    // Check if this is a registered entry point
    size_t n = get_entry_qty();
    for (size_t i = 0; i < n; i++) {
        uval_t ord = get_entry_ordinal(i);
        ea_t entry_ea = get_entry(ord);
        if (entry_ea == function_ea) {
            return true;
        }
    }
    return false;
}

bool EntryPointHeuristic::is_exported(ea_t function_ea) {
    // Check if function is exported
    qstring name;
    if (get_name(&name, function_ea) > 0) {
        // In IDA, exported functions typically have their export flag set
        func_t* func = get_func(function_ea);
        if (func && (func->flags & FUNC_LIB) == 0) {
            // Check if this is in the entry point list
            return is_entry_point(function_ea);
        }
    }
    return false;
}

bool EntryPointHeuristic::is_main_function(ea_t function_ea) {
    qstring name;
    if (get_name(&name, function_ea) > 0) {
        std::string func_name = name.c_str();
        // Common main function names
        return func_name == "main" || func_name == "wmain" ||
               func_name == "_main" || func_name == "_wmain" ||
               func_name == "WinMain" || func_name == "wWinMain" ||
               func_name == "DllMain" || func_name == "start" ||
               func_name == "_start";
    }
    return false;
}

double EntryPointHeuristic::score(ea_t function_ea) {
    double base_score = 0.0;

    // Determine base score based on entry point type
    if (is_main_function(function_ea)) {
        base_score = 1000.0;  // Main function
    } else if (is_entry_point(function_ea)) {
        base_score = 800.0;   // Entry points (DllMain, etc.)
    } else if (is_exported(function_ea)) {
        base_score = 600.0;   // Exported functions
    } else {
        return 0.0;  // Not an entry point
    }

    // Apply mode modifier
    switch (mode_) {
        case EntryPointMode::TOP_DOWN:
            // Positive scores: analyze entry points FIRST
            return base_score;

        case EntryPointMode::BOTTOM_UP:
            // Negative scores: analyze entry points LAST (benefits from callees)
            return -base_score;

        case EntryPointMode::NEUTRAL:
            // No priority adjustment
            return 0.0;

        default:
            return -base_score;  // Default to bottom-up
    }
}

std::string EntryPointHeuristic::explain(ea_t function_ea) {
    if (is_main_function(function_ea)) {
        return "main_function";
    }
    if (is_entry_point(function_ea)) {
        return "entry_point";
    }
    if (is_exported(function_ea)) {
        return "exported";
    }
    return "";
}

// ==========================
// StringHeavyHeuristic
// ==========================

int StringHeavyHeuristic::count_long_strings_in_function(ea_t function_ea) {
    func_t* func = get_func(function_ea);
    if (!func) {
        return 0;
    }

    int count = 0;
    ea_t current_ea = func->start_ea;

    // Scan through function looking for string references
    while (current_ea < func->end_ea) {
        // Check if this instruction references a string
        xrefblk_t xb;
        for (bool ok = xb.first_from(current_ea, XREF_DATA); ok; ok = xb.next_from()) {
            // Check if the referenced address is a string
            flags64_t flags = get_flags(xb.to);
            if (is_strlit(flags)) {
                // Get string length
                qstring str_data;
                if (get_strlit_contents(&str_data, xb.to, -1, STRTYPE_C) > 0) {
                    if ((int)str_data.length() >= min_string_length_) {
                        count++;
                    }
                }
            }
        }

        current_ea = next_head(current_ea, func->end_ea);
    }

    return count;
}

double StringHeavyHeuristic::score(ea_t function_ea) {
    int string_count = count_long_strings_in_function(function_ea);

    // Scoring: each long string adds value
    // Cap at 10 strings to avoid extreme scores
    return std::min(string_count * 50.0, 500.0);
}

std::string StringHeavyHeuristic::explain(ea_t function_ea) {
    int string_count = count_long_strings_in_function(function_ea);
    if (string_count > 0) {
        return std::to_string(string_count) + "_long_strings";
    }
    return "";
}

// ==========================
// APICallHeuristic
// ==========================

int APICallHeuristic::count_api_calls(ea_t function_ea) {
    func_t* func = get_func(function_ea);
    if (!func) {
        return 0;
    }

    int count = 0;
    ea_t current_ea = func->start_ea;

    // Scan through function looking for calls to library functions
    while (current_ea < func->end_ea) {
        xrefblk_t xb;
        for (bool ok = xb.first_from(current_ea, XREF_ALL); ok; ok = xb.next_from()) {
            // Check if this is a call (not a jump)
            if (xb.iscode && (xb.type == fl_CF || xb.type == fl_CN)) {
                // Check if target is a library function
                func_t* target = get_func(xb.to);
                if (target && (target->flags & FUNC_LIB)) {
                    count++;
                }
            }
        }

        current_ea = next_head(current_ea, func->end_ea);
    }

    return count;
}

double APICallHeuristic::score(ea_t function_ea) {
    int api_count = count_api_calls(function_ea);
    return api_count * 200.0;
}

std::string APICallHeuristic::explain(ea_t function_ea) {
    int api_count = count_api_calls(function_ea);
    if (api_count > 0) {
        return std::to_string(api_count) + "_api_calls";
    }
    return "";
}

// ==========================
// CallerCountHeuristic
// ==========================

int CallerCountHeuristic::count_callers(ea_t function_ea) {
    int count = 0;

    xrefblk_t xb;
    for (bool ok = xb.first_to(function_ea, XREF_ALL); ok; ok = xb.next_to()) {
        // Only count call-type xrefs (not jumps or data refs)
        if (xb.iscode && (xb.type == fl_CF || xb.type == fl_CN)) {
            count++;
        }
    }

    return count;
}

double CallerCountHeuristic::score(ea_t function_ea) {
    int caller_count = count_callers(function_ea);
    // Cap at 600 to avoid extreme scores
    return std::min(caller_count * 30.0, 600.0);
}

std::string CallerCountHeuristic::explain(ea_t function_ea) {
    int caller_count = count_callers(function_ea);
    if (caller_count > 0) {
        return std::to_string(caller_count) + "_callers";
    }
    return "";
}

// ==========================
// InternalCalleeHeuristic
// ==========================

int InternalCalleeHeuristic::count_internal_callees(ea_t function_ea) {
    func_t* func = get_func(function_ea);
    if (!func) {
        return 0;
    }

    int count = 0;
    ea_t current_ea = func->start_ea;

    // Scan through function looking for calls to internal (non-library) functions
    while (current_ea < func->end_ea) {
        xrefblk_t xb;
        for (bool ok = xb.first_from(current_ea, XREF_ALL); ok; ok = xb.next_from()) {
            // Check if this is a call (not a jump)
            if (xb.iscode && (xb.type == fl_CF || xb.type == fl_CN)) {
                // Check if target is NOT a library function (internal call)
                func_t* target = get_func(xb.to);
                if (target && (target->flags & FUNC_LIB) == 0) {
                    count++;
                }
            }
        }

        current_ea = next_head(current_ea, func->end_ea);
    }

    return count;
}

double InternalCalleeHeuristic::score(ea_t function_ea) {
    int internal_count = count_internal_callees(function_ea);
    // NEGATIVE score - functions with many internal calls need callees analyzed first
    return -internal_count * 50.0;
}

std::string InternalCalleeHeuristic::explain(ea_t function_ea) {
    int internal_count = count_internal_callees(function_ea);
    if (internal_count > 0) {
        return std::to_string(internal_count) + "_internal_calls";
    }
    return "";
}

// ==========================
// FunctionSizeHeuristic
// ==========================

double FunctionSizeHeuristic::score(ea_t function_ea) {
    func_t* func = get_func(function_ea);
    if (!func) {
        return 0.0;
    }

    size_t size = func->end_ea - func->start_ea;

    // Prioritize smaller functions (easier wins, builds momentum)
    if (size < 100) return 400.0;
    if (size < 500) return 200.0;
    if (size < 1000) return 0.0;
    if (size < 5000) return -100.0;
    return -200.0;  // Huge functions - defer
}

std::string FunctionSizeHeuristic::explain(ea_t function_ea) {
    func_t* func = get_func(function_ea);
    if (!func) {
        return "";
    }

    size_t size = func->end_ea - func->start_ea;

    if (size < 100) return "tiny";
    if (size < 500) return "small";
    if (size < 1000) return "medium";
    if (size < 5000) return "large";
    return "huge";
}

// ==========================
// FunctionPrioritizer
// ==========================

FunctionPrioritizer::FunctionPrioritizer() {
    // Default heuristics - Smart Hybrid Priority System
    add_heuristic(std::make_unique<APICallHeuristic>(), 2.0);
    add_heuristic(std::make_unique<CallerCountHeuristic>(), 1.5);
    add_heuristic(std::make_unique<StringHeavyHeuristic>(), 2.0);
    add_heuristic(std::make_unique<FunctionSizeHeuristic>(), 1.5);
    add_heuristic(std::make_unique<InternalCalleeHeuristic>(), 1.0);
    add_heuristic(std::make_unique<EntryPointHeuristic>(), 1.0);
}

FunctionPrioritizer::FunctionPrioritizer(const Config& config) {
    configure(config);
}

void FunctionPrioritizer::configure(const Config& config) {
    heuristics_.clear();

    // Add enabled heuristics with configured weights
    // Weights can be positive (prioritize) or negative (deprioritize)

    if (config.swarm.enable_api_call_heuristic) {
        add_heuristic(std::make_unique<APICallHeuristic>(), config.swarm.api_call_weight);
    }

    if (config.swarm.enable_caller_count_heuristic) {
        add_heuristic(std::make_unique<CallerCountHeuristic>(), config.swarm.caller_count_weight);
    }

    if (config.swarm.enable_string_heavy_heuristic) {
        add_heuristic(
            std::make_unique<StringHeavyHeuristic>(config.swarm.min_string_length_for_priority),
            config.swarm.string_heavy_weight
        );
    }

    if (config.swarm.enable_function_size_heuristic) {
        add_heuristic(std::make_unique<FunctionSizeHeuristic>(), config.swarm.function_size_weight);
    }

    if (config.swarm.enable_internal_callee_heuristic) {
        add_heuristic(std::make_unique<InternalCalleeHeuristic>(), config.swarm.internal_callee_weight);
    }

    if (config.swarm.enable_entry_point_heuristic) {
        // Create entry point heuristic with configured mode
        auto entry_heuristic = std::make_unique<EntryPointHeuristic>();
        entry_heuristic->set_mode(config.swarm.entry_point_mode);
        add_heuristic(std::move(entry_heuristic), config.swarm.entry_point_weight);
    }
}

void FunctionPrioritizer::add_heuristic(std::unique_ptr<FunctionHeuristic> heuristic, double weight) {
    heuristics_.push_back({std::move(heuristic), weight});
}

FunctionPriority FunctionPrioritizer::compute_priority(ea_t function_ea) {
    FunctionPriority priority;
    priority.address = function_ea;
    priority.score = 0.0;

    std::vector<std::string> reasons;

    // Combine scores from all heuristics
    for (const auto& wh : heuristics_) {
        double heuristic_score = wh.heuristic->score(function_ea);

        // Always apply the score (positive, negative, or zero)
        priority.score += heuristic_score * wh.weight;

        // Add to metrics/reasons if score is non-zero
        if (heuristic_score != 0.0) {
            std::string explanation = wh.heuristic->explain(function_ea);
            if (!explanation.empty()) {
                priority.metrics.push_back(explanation);
                reasons.push_back(wh.heuristic->name() + ":" + explanation);
            }
        }
    }

    // Create summary reason
    if (reasons.empty()) {
        priority.reason = "standard_priority";
    } else {
        std::ostringstream oss;
        for (size_t i = 0; i < reasons.size(); i++) {
            if (i > 0) oss << ", ";
            oss << reasons[i];
        }
        priority.reason = oss.str();
    }

    return priority;
}

std::vector<FunctionPriority> FunctionPrioritizer::prioritize_functions(
    const std::vector<ea_t>& functions
) {
    return IDAUtils::execute_sync_wrapper([&]() -> std::vector<FunctionPriority> {
        std::vector<FunctionPriority> priorities;
        priorities.reserve(functions.size());

        LOG("FunctionPrioritizer: Computing priorities for %zu functions", functions.size());

        for (ea_t func_ea : functions) {
            priorities.push_back(compute_priority(func_ea));
        }

        // Sort by priority (highest first)
        std::sort(priorities.begin(), priorities.end());

        LOG("FunctionPrioritizer: Top 5 priorities:");
        for (size_t i = 0; i < std::min(size_t(5), priorities.size()); i++) {
            qstring func_name;
            get_name(&func_name, priorities[i].address);
            LOG("  %zu. 0x%llx (%s): score=%.1f reason=%s",
                i + 1, (unsigned long long)priorities[i].address,
                func_name.c_str(), priorities[i].score, priorities[i].reason.c_str());
        }

        return priorities;
    }, MFF_READ);
}

} // namespace llm_re
