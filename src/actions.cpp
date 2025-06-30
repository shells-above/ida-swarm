//
// Created by user on 6/29/25.
//

#include "actions.h"
#include "ida_utils.h"

#include <utility>

namespace llm_re {

ActionExecutor::ActionExecutor(std::shared_ptr<BinaryMemory> mem) : memory(std::move(mem)) {}

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
    // Route action to appropriate method
    if (action_name == "get_xrefs_to") {
        return get_xrefs_to(params["address"]);
    } else if (action_name == "get_xrefs_from") {
        return get_xrefs_from(params["address"]);
    } else if (action_name == "get_function_disassembly") {
        return get_function_disassembly(params["address"]);
    } else if (action_name == "get_function_decompilation") {
        return get_function_decompilation(params["address"]);
    } else if (action_name == "get_function_address") {
        return get_function_address(params["name"]);
    } else if (action_name == "get_function_name") {
        return get_function_name(params["address"]);
    } else if (action_name == "set_function_name") {
        return set_function_name(params["address"], params["name"]);
    } else if (action_name == "get_function_string_refs") {
        return get_function_string_refs(params["address"]);
    } else if (action_name == "get_function_data_refs") {
        return get_function_data_refs(params["address"]);
    } else if (action_name == "get_data_name") {
        return get_data_name(params["address"]);
    } else if (action_name == "set_data_name") {
        return set_data_name(params["address"], params["name"]);
    } else if (action_name == "add_disassembly_comment") {
        return add_disassembly_comment(params["address"], params["comment"]);
    } else if (action_name == "add_pseudocode_comment") {
        return add_pseudocode_comment(params["address"], params["comment"]);
    } else if (action_name == "clear_disassembly_comment") {
        return clear_disassembly_comment(params["address"]);
    } else if (action_name == "clear_pseudocode_comments") {
        return clear_pseudocode_comments(params["address"]);
    } else if (action_name == "get_imports") {
        return get_imports();
    } else if (action_name == "get_exports") {
        return get_exports();
    } else if (action_name == "search_strings") {
        return search_strings(params["text"], params.value("is_case_sensitive", false));
    } else if (action_name == "set_global_note") {
        return set_global_note(params["key"], params["content"]);
    } else if (action_name == "get_global_note") {
        return get_global_note(params["key"]);
    } else if (action_name == "list_global_notes") {
        return list_global_notes();
    } else if (action_name == "search_notes") {
        return search_notes(params["query"]);
    } else if (action_name == "set_function_analysis") {
        return set_function_analysis(params["address"], params["level"], params["analysis"]);
    } else if (action_name == "get_function_analysis") {
        return get_function_analysis(params["address"], params.value("level", 0));
    } else if (action_name == "get_memory_context") {
        return get_memory_context(params["address"], params.value("radius", 2));
    } else if (action_name == "get_analyzed_functions") {
        return get_analyzed_functions();
    } else if (action_name == "find_functions_by_pattern") {
        return find_functions_by_pattern(params["pattern"]);
    } else if (action_name == "get_exploration_frontier") {
        return get_exploration_frontier();
    } else if (action_name == "mark_for_analysis") {
        return mark_for_analysis(params["address"], params["reason"], params.value("priority", 5));
    } else if (action_name == "get_analysis_queue") {
        return get_analysis_queue();
    } else if (action_name == "set_current_focus") {
        return set_current_focus(params["address"]);
    } else if (action_name == "add_insight") {
        return add_insight(params["type"], params["description"], params["related_addresses"].get<std::vector<ea_t>>());
    } else if (action_name == "get_insights") {
        return get_insights(params.value("type", ""));
    } else if (action_name == "analyze_cluster") {
        return analyze_cluster(params["addresses"].get<std::vector<ea_t>>(), params["cluster_name"], params["initial_level"]);
    } else if (action_name == "get_cluster_analysis") {
        return get_cluster_analysis(params["cluster_name"]);
    } else if (action_name == "summarize_region") {
        return summarize_region(params["start_addr"], params["end_addr"]);
    } else {
        json error_result;
        error_result["success"] = false;
        error_result["error"] = "Unknown action: " + action_name;
        return error_result;
    }
}

} // namespace llm_re
