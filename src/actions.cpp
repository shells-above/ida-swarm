//
// Created by user on 6/29/25.
//

#include "actions.h"
#include "ida_utils.h"

namespace llm_re {

ActionExecutor::ActionExecutor(std::shared_ptr<BinaryMemory> mem) : memory(std::move(mem)) {
    register_actions();
}

void ActionExecutor::register_actions() {
    // IDA Core Actions
    action_map["get_xrefs_to"] = [this](const json& params) -> json {
        return get_xrefs_to(params["address"]);
    };

    action_map["get_xrefs_from"] = [this](const json& params) -> json {
        return get_xrefs_from(params["address"]);
    };

    action_map["get_function_disassembly"] = [this](const json& params) -> json {
        return get_function_disassembly(params["address"]);
    };

    action_map["get_function_decompilation"] = [this](const json& params) -> json {
        return get_function_decompilation(params["address"]);
    };

    action_map["get_function_address"] = [this](const json& params) -> json {
        return get_function_address(params["name"]);
    };

    action_map["get_function_name"] = [this](const json& params) -> json {
        return get_function_name(params["address"]);
    };

    action_map["set_function_name"] = [this](const json& params) -> json {
        return set_function_name(params["address"], params["name"]);
    };

    action_map["get_function_string_refs"] = [this](const json& params) -> json {
        return get_function_string_refs(params["address"]);
    };

    action_map["get_function_data_refs"] = [this](const json& params) -> json {
        return get_function_data_refs(params["address"]);
    };

    action_map["get_data_name"] = [this](const json& params) -> json {
        return get_data_name(params["address"]);
    };

    action_map["set_data_name"] = [this](const json& params) -> json {
        return set_data_name(params["address"], params["name"]);
    };

    action_map["add_disassembly_comment"] = [this](const json& params) -> json {
        return add_disassembly_comment(params["address"], params["comment"]);
    };

    action_map["add_pseudocode_comment"] = [this](const json& params) -> json {
        return add_pseudocode_comment(params["address"], params["comment"]);
    };

    action_map["clear_disassembly_comment"] = [this](const json& params) -> json {
        return clear_disassembly_comment(params["address"]);
    };

    action_map["clear_pseudocode_comments"] = [this](const json& params) -> json {
        return clear_pseudocode_comments(params["address"]);
    };

    action_map["get_imports"] = [this](const json& params) -> json {
        return get_imports();
    };

    action_map["get_exports"] = [this](const json& params) -> json {
        return get_exports();
    };

    action_map["search_strings"] = [this](const json& params) -> json {
        return search_strings(params["text"], params.value("is_case_sensitive", false));
    };

    // Memory System Actions
    action_map["set_global_note"] = [this](const json& params) -> json {
        return set_global_note(params["key"], params["content"]);
    };

    action_map["get_global_note"] = [this](const json& params) -> json {
        return get_global_note(params["key"]);
    };

    action_map["list_global_notes"] = [this](const json& params) -> json {
        return list_global_notes();
    };

    action_map["search_notes"] = [this](const json& params) -> json {
        return search_notes(params["query"]);
    };

    action_map["set_function_analysis"] = [this](const json& params) -> json {
        return set_function_analysis(params["address"], params["level"], params["analysis"]);
    };

    action_map["get_function_analysis"] = [this](const json& params) -> json {
        return get_function_analysis(params["address"], params.value("level", 0));
    };

    action_map["get_memory_context"] = [this](const json& params) -> json {
        return get_memory_context(params["address"], params.value("radius", 2));
    };

    action_map["get_analyzed_functions"] = [this](const json& params) -> json {
        return get_analyzed_functions();
    };

    action_map["find_functions_by_pattern"] = [this](const json& params) -> json {
        return find_functions_by_pattern(params["pattern"]);
    };

    action_map["get_exploration_frontier"] = [this](const json& params) -> json {
        return get_exploration_frontier();
    };

    action_map["mark_for_analysis"] = [this](const json& params) -> json {
        return mark_for_analysis(params["address"], params["reason"], params.value("priority", 5));
    };

    action_map["get_analysis_queue"] = [this](const json& params) -> json {
        return get_analysis_queue();
    };

    action_map["set_current_focus"] = [this](const json& params) -> json {
        return set_current_focus(params["address"]);
    };

    action_map["add_insight"] = [this](const json& params) -> json {
        return add_insight(params["type"], params["description"], params["related_addresses"].get<std::vector<ea_t>>());
    };

    action_map["get_insights"] = [this](const json& params) -> json {
        return get_insights(params.value("type", ""));
    };

    action_map["analyze_cluster"] = [this](const json& params) -> json {
        return analyze_cluster(params["addresses"].get<std::vector<ea_t>>(), params["cluster_name"], params["initial_level"]);
    };

    action_map["get_cluster_analysis"] = [this](const json& params) -> json {
        return get_cluster_analysis(params["cluster_name"]);
    };

    action_map["summarize_region"] = [this](const json& params) -> json {
        return summarize_region(params["start_addr"], params["end_addr"]);
    };
}

json ActionExecutor::get_xrefs_to(ea_t address) {
    json result;
    try {
        std::vector<ea_t> xrefs = IDAUtils::get_xrefs_to(address);
        result["success"] = true;
        result["xrefs"] = xrefs;

        // Update memory with caller information
        std::set<ea_t> callers(xrefs.begin(), xrefs.end());
        memory->update_function_relationships(address, callers, {});
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_xrefs_from(ea_t address) {
    json result;
    try {
        std::vector<ea_t> xrefs = IDAUtils::get_xrefs_from(address);
        result["success"] = true;
        result["xrefs"] = xrefs;

        // Update memory with callee information
        std::set<ea_t> callees(xrefs.begin(), xrefs.end());
        memory->update_function_relationships(address, {}, callees);
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_disassembly(ea_t address) {
    json result;
    try {
        std::string disasm = IDAUtils::get_function_disassembly(address);
        result["success"] = true;
        result["disassembly"] = disasm;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_decompilation(ea_t address) {
    json result;
    try {
        std::string decomp = IDAUtils::get_function_decompilation(address);
        result["success"] = true;
        result["decompilation"] = decomp;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_address(const std::string& name) {
    json result;
    try {
        ea_t addr = IDAUtils::get_function_address(name);
        result["success"] = (addr != BADADDR);
        result["address"] = addr;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_name(ea_t address) {
    json result;
    try {
        std::string name = IDAUtils::get_function_name(address);
        result["success"] = true;
        result["name"] = name;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::set_function_name(ea_t address, const std::string& name) {
    json result;
    try {
        bool success = IDAUtils::set_function_name(address, name);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_string_refs(ea_t address) {
    json result;
    try {
        std::vector<std::string> strings = IDAUtils::get_function_string_refs(address);
        result["success"] = true;
        result["strings"] = strings;

        // Update memory
        memory->update_function_refs(address, strings, {});
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_data_refs(ea_t address) {
    json result;
    try {
        std::vector<ea_t> data_refs = IDAUtils::get_function_data_refs(address);
        result["success"] = true;
        result["data_refs"] = data_refs;

        // Update memory
        memory->update_function_refs(address, {}, data_refs);
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_data_name(ea_t address) {
    json result;
    try {
        std::string name = IDAUtils::get_data_name(address);
        result["success"] = true;
        result["name"] = name;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::set_data_name(ea_t address, const std::string& name) {
    json result;
    try {
        bool success = IDAUtils::set_data_name(address, name);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::add_disassembly_comment(ea_t address, const std::string& comment) {
    json result;
    try {
        bool success = IDAUtils::add_disassembly_comment(address, comment);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::add_pseudocode_comment(ea_t address, const std::string& comment) {
    json result;
    try {
        bool success = IDAUtils::add_pseudocode_comment(address, comment);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::clear_disassembly_comment(ea_t address) {
    json result;
    try {
        bool success = IDAUtils::clear_disassembly_comment(address);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::clear_pseudocode_comments(ea_t address) {
    json result;
    try {
        bool success = IDAUtils::clear_pseudocode_comments(address);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_imports() {
    json result;
    try {
        std::map<std::string, std::vector<std::string>> imports = IDAUtils::get_imports();
        result["success"] = true;
        result["imports"] = imports;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_exports() {
    json result;
    try {
        std::vector<std::pair<std::string, ea_t>> exports = IDAUtils::get_exports();
        result["success"] = true;
        json exports_json = json::array();
        for (const std::pair<std::string, ea_t>& exp: exports) {
            json exp_obj;
            exp_obj["name"] = exp.first;
            exp_obj["address"] = exp.second;
            exports_json.push_back(exp_obj);
        }
        result["exports"] = exports_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::search_strings(const std::string& text, bool is_case_sensitive) {
    json result;
    try {
        std::vector<std::string> strings = IDAUtils::search_strings(text, is_case_sensitive);
        result["success"] = true;
        result["strings"] = strings;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

// Memory System Actions

json ActionExecutor::set_global_note(const std::string& key, const std::string& content) {
    json result;
    try {
        memory->set_global_note(key, content);
        result["success"] = true;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_global_note(const std::string& key) {
    json result;
    try {
        std::string content = memory->get_global_note(key);
        result["success"] = true;
        result["content"] = content;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::list_global_notes() {
    json result;
    try {
        std::vector<std::string> keys = memory->list_global_notes();
        result["success"] = true;
        result["keys"] = keys;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::search_notes(const std::string& query) {
    json result;
    try {
        std::vector<std::pair<std::string, std::string>> matches = memory->search_notes(query);
        result["success"] = true;
        json matches_json = json::array();
        for (const std::pair<std::string, std::string> &match: matches) {
            json match_obj;
            match_obj["key"] = match.first;
            match_obj["snippet"] = match.second;
            matches_json.push_back(match_obj);
        }
        result["matches"] = matches_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::set_function_analysis(ea_t address, int level, const std::string& analysis) {
    json result;
    try {
        memory->set_function_analysis(address, static_cast<DetailLevel>(level), analysis);
        result["success"] = true;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_analysis(ea_t address, int level) {
    json result;
    try {
        std::string analysis = memory->get_function_analysis(address, static_cast<DetailLevel>(level));
        result["success"] = true;
        result["analysis"] = analysis;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_memory_context(ea_t address, int radius) {
    json result;
    try {
        MemoryContext context = memory->get_memory_context(address, radius);
        result["success"] = true;

        // Convert context to JSON
        json nearby = json::array();
        for (const FunctionMemory& func: context.nearby_functions) {
            json func_obj;
            func_obj["address"] = func.address;
            func_obj["name"] = func.name;
            func_obj["distance_from_anchor"] = func.distance_from_anchor;
            func_obj["current_level"] = static_cast<int>(func.current_level);
            nearby.push_back(func_obj);
        }
        result["nearby_functions"] = nearby;

        json context_funcs = json::array();
        for (const FunctionMemory& func: context.context_functions) {
            json func_obj;
            func_obj["address"] = func.address;
            func_obj["name"] = func.name;
            func_obj["distance_from_anchor"] = func.distance_from_anchor;
            func_obj["current_level"] = static_cast<int>(func.current_level);
            context_funcs.push_back(func_obj);
        }
        result["context_functions"] = context_funcs;

        result["llm_memory"] = context.llm_memory;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_analyzed_functions() {
    json result;
    try {
        std::vector<std::tuple<ea_t, std::string, DetailLevel>> functions = memory->get_analyzed_functions();
        result["success"] = true;
        json funcs_json = json::array();
        for (const std::tuple<ea_t, std::string, DetailLevel>& func: functions) {
            json func_obj;
            func_obj["address"] = std::get<0>(func);
            func_obj["name"] = std::get<1>(func);
            func_obj["max_level"] = static_cast<int>(std::get<2>(func));
            funcs_json.push_back(func_obj);
        }
        result["functions"] = funcs_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::find_functions_by_pattern(const std::string& pattern) {
    json result;
    try {
        std::vector<ea_t> addresses = memory->find_functions_by_pattern(pattern);
        result["success"] = true;
        result["addresses"] = addresses;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_exploration_frontier() {
    json result;
    try {
        std::vector<std::tuple<ea_t, std::string, std::string>> frontier = memory->get_exploration_frontier();
        result["success"] = true;
        json frontier_json = json::array();
        for (const std::tuple<ea_t, std::string, std::string>& item: frontier) {
            json item_obj;
            item_obj["address"] = std::get<0>(item);
            item_obj["name"] = std::get<1>(item);
            item_obj["reason"] = std::get<2>(item);
            frontier_json.push_back(item_obj);
        }
        result["frontier"] = frontier_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::mark_for_analysis(ea_t address, const std::string& reason, int priority) {
    json result;
    try {
        memory->mark_for_analysis(address, reason, priority);
        result["success"] = true;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_analysis_queue() {
    json result;
    try {
        std::vector<std::tuple<ea_t, std::string, int>> queue = memory->get_analysis_queue();
        result["success"] = true;
        json queue_json = json::array();
        for (const std::tuple<ea_t, std::string, int>& item: queue) {
            json item_obj;
            item_obj["address"] = std::get<0>(item);
            item_obj["reason"] = std::get<1>(item);
            item_obj["priority"] = std::get<2>(item);
            queue_json.push_back(item_obj);
        }
        result["queue"] = queue_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::set_current_focus(ea_t address) {
    json result;
    try {
        memory->set_current_focus(address);
        result["success"] = true;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::add_insight(const std::string& type, const std::string& description, const std::vector<ea_t>& related_addresses) {
    json result;
    try {
        memory->add_insight(type, description, related_addresses);
        result["success"] = true;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_insights(const std::string& type) {
    json result;
    try {
        std::vector<std::tuple<std::string, std::vector<ea_t>>> insights = memory->get_insights(type);
        result["success"] = true;
        json insights_json = json::array();
        for (const std::tuple<std::string, std::vector<ea_t>> &insight: insights) {
            json insight_obj;
            insight_obj["description"] = std::get<0>(insight);
            insight_obj["addresses"] = std::get<1>(insight);
            insights_json.push_back(insight_obj);
        }
        result["insights"] = insights_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::analyze_cluster(const std::vector<ea_t>& addresses, const std::string& cluster_name, int initial_level) {
    json result;
    try {
        memory->analyze_cluster(addresses, cluster_name, static_cast<DetailLevel>(initial_level));
        result["success"] = true;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_cluster_analysis(const std::string& cluster_name) {
    json result;
    try {
        std::map<ea_t, std::string> cluster = memory->get_cluster_analysis(cluster_name);
        result["success"] = true;
        json cluster_json = json::object();
        for (const std::pair<const unsigned long long, std::string> &pair: cluster) {
            cluster_json[std::to_string(pair.first)] = pair.second;
        }
        result["cluster"] = cluster_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::summarize_region(ea_t start_addr, ea_t end_addr) {
    json result;
    try {
        std::string summary = memory->summarize_region(start_addr, end_addr);
        result["success"] = true;
        result["summary"] = summary;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::execute_action(const std::string& action_name, const json& params) {
    auto it = action_map.find(action_name);
    if (it != action_map.end()) {
        try {
            return it->second(params);
        } catch (const std::exception& e) {
            json error_result;
            error_result["success"] = false;
            error_result["error"] = "Action execution failed: " + std::string(e.what());
            return error_result;
        }
    } else {
        json error_result;
        error_result["success"] = false;
        error_result["error"] = "Unknown action: " + action_name;
        return error_result;
    }
}

void ActionExecutor::log_action(const std::string& action, ea_t address,
                               const std::string& old_value, const std::string& new_value,
                               bool success, const std::string& error_msg) {
    std::lock_guard<std::mutex> lock(audit_mutex);

    AuditEntry entry{
        .timestamp = std::time(nullptr),
        .action = action,
        .address = address,
        .old_value = old_value,
        .new_value = new_value,
        .success = success,
        .error_message = error_msg
    };

    audit_log.push_back(entry);

    // Keep only last 10000 entries to prevent unbounded growth
    if (audit_log.size() > 10000) {
        audit_log.erase(audit_log.begin(), audit_log.begin() + 1000);
    }
}

void ActionExecutor::save_audit_log(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(audit_mutex);

    json log_json = json::array();
    for (const auto& entry : audit_log) {
        json entry_json{
                {"timestamp", entry.timestamp},
                {"action", entry.action},
                {"address", entry.address},
                {"old_value", entry.old_value},
                {"new_value", entry.new_value},
                {"success", entry.success},
                {"error_message", entry.error_message}
        };
        log_json.push_back(entry_json);
    }

    std::ofstream file(filename);
    if (file.is_open()) {
        file << log_json.dump(2);
        file.close();
    }
}

std::vector<AuditEntry> ActionExecutor::get_recent_audit_entries(size_t count) const {
    std::lock_guard<std::mutex> lock(audit_mutex);

    std::vector<AuditEntry> recent;
    size_t start = (audit_log.size() > count) ? audit_log.size() - count : 0;

    for (size_t i = start; i < audit_log.size(); ++i) {
        recent.push_back(audit_log[i]);
    }

    return recent;
}

} // namespace llm_re