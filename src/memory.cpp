//
// Created by user on 6/29/25.
//

#include "memory.h"

namespace llm_re {


BinaryMemory::BinaryMemory() : current_focus(0) {}

BinaryMemory::~BinaryMemory() {}

int BinaryMemory::calculateDistance(ea_t from, ea_t to) const {
    if (from == to) return 0;

    // Check cache first
    auto cache_key = std::make_pair(from, to);
    auto it = call_graph_cache.find(cache_key);
    if (it != call_graph_cache.end()) {
        return it->second;
    }

    // Compute actual distance
    int distance = compute_call_graph_distance(from, to);
    call_graph_cache[cache_key] = distance;
    return distance;
}

int BinaryMemory::compute_call_graph_distance(ea_t from, ea_t to) const {
    // BFS to find shortest path in call graph
    std::queue<std::pair<ea_t, int>> bfs_queue;
    std::set<ea_t> visited;

    bfs_queue.push({from, 0});
    visited.insert(from);

    while (!bfs_queue.empty() && bfs_queue.size() < 1000) { // Limit search
        auto [current, distance] = bfs_queue.front();
        bfs_queue.pop();

        if (current == to) {
            return distance;
        }

        // Check if we have this function in memory
        auto func_it = function_memories.find(current);
        if (func_it != function_memories.end()) {
            // Check callees
            for (ea_t callee : func_it->second.callees) {
                if (visited.find(callee) == visited.end()) {
                    visited.insert(callee);
                    bfs_queue.push({callee, distance + 1});
                }
            }
            // Check callers (for backward reachability)
            for (ea_t caller : func_it->second.callers) {
                if (visited.find(caller) == visited.end()) {
                    visited.insert(caller);
                    bfs_queue.push({caller, distance + 1});
                }
            }
        }

        // If function not in memory, we need to get its xrefs
        // This is a limitation - we work with what we have cached
    }

    // If no path found or anchor point
    if (anchor_points.find(to) != anchor_points.end()) {
        return 2; // Close to anchor
    }

    return 10; // Default far distance
}

void BinaryMemory::set_global_note(const std::string& key, const std::string& content) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    global_notes[key] = content;
}

std::string BinaryMemory::get_global_note(const std::string& key) const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    auto it = global_notes.find(key);
    return (it != global_notes.end()) ? it->second : "";
}

std::vector<std::string> BinaryMemory::list_global_notes() const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    std::vector<std::string> keys;
    for (const auto& pair : global_notes) {
        keys.push_back(pair.first);
    }
    return keys;
}

std::vector<std::pair<std::string, std::string>> BinaryMemory::search_notes(const std::string& query) const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    std::vector<std::pair<std::string, std::string>> results;

    std::regex pattern(query, std::regex_constants::icase);
    for (const auto& pair : global_notes) {
        if (std::regex_search(pair.second, pattern)) {
            // Extract snippet around match
            size_t pos = pair.second.find(query);
            size_t start = (pos > 50) ? pos - 50 : 0;
            size_t end = std::min(pos + query.length() + 50, pair.second.length());
            std::string snippet = pair.second.substr(start, end - start);
            results.push_back({pair.first, snippet});
        }
    }
    return results;
}

void BinaryMemory::set_function_analysis(ea_t address, DetailLevel level, const std::string& analysis) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    auto& func_mem = function_memories[address];
    func_mem.address = address;
    func_mem.descriptions[level] = analysis;
    func_mem.current_level = std::max(func_mem.current_level, level);
    func_mem.last_updated = std::time(nullptr);
}

std::string BinaryMemory::get_function_analysis(ea_t address, DetailLevel level) const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    auto it = function_memories.find(address);
    if (it == function_memories.end()) return "";

    const auto& func_mem = it->second;
    if (level == DetailLevel::SUMMARY) {
        // Return best available
        for (int l = static_cast<int>(DetailLevel::COMPREHENSIVE); l >= static_cast<int>(DetailLevel::SUMMARY); --l) {
            auto desc_it = func_mem.descriptions.find(static_cast<DetailLevel>(l));
            if (desc_it != func_mem.descriptions.end()) {
                return desc_it->second;
            }
        }
    } else {
        auto desc_it = func_mem.descriptions.find(level);
        if (desc_it != func_mem.descriptions.end()) {
            return desc_it->second;
        }
    }
    return "";
}

MemoryContext BinaryMemory::get_memory_context(ea_t address, int radius) const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    MemoryContext context;

    // Get functions within radius
    for (const auto& pair : function_memories) {
        int distance = calculateDistance(address, pair.first);
        if (distance <= radius) {
            context.nearby_functions.push_back(pair.second);
        } else if (distance <= radius * 2) {
            context.context_functions.push_back(pair.second);
        }
    }

    // Include all global notes
    for (const auto& pair : global_notes) {
        context.llm_memory[pair.first] = pair.second;
    }

    return context;
}

std::vector<std::tuple<ea_t, std::string, DetailLevel>> BinaryMemory::get_analyzed_functions() const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    std::vector<std::tuple<ea_t, std::string, DetailLevel>> result;

    for (const auto& pair : function_memories) {
        result.push_back({pair.first, pair.second.name, pair.second.current_level});
    }
    return result;
}

std::vector<ea_t> BinaryMemory::find_functions_by_pattern(const std::string& pattern) const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    std::vector<ea_t> results;
    std::regex re(pattern, std::regex_constants::icase);

    for (const auto& pair : function_memories) {
        for (const auto& desc_pair : pair.second.descriptions) {
            if (std::regex_search(desc_pair.second, re)) {
                results.push_back(pair.first);
                break;
            }
        }
    }
    return results;
}

std::vector<std::tuple<ea_t, std::string, std::string>> BinaryMemory::get_exploration_frontier() const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    std::vector<std::tuple<ea_t, std::string, std::string>> frontier;

    // Convert queue to vector (queue doesn't allow iteration)
    std::priority_queue<AnalysisQueueItem> temp_queue = analysis_queue;
    while (!temp_queue.empty()) {
        const auto& item = temp_queue.top();
        auto it = function_memories.find(item.address);
        std::string name = (it != function_memories.end()) ? it->second.name : "";
        frontier.push_back({item.address, name, item.reason});
        temp_queue.pop();
    }

    return frontier;
}

void BinaryMemory::mark_for_analysis(ea_t address, const std::string& reason, int priority) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    analysis_queue.push({address, reason, priority});
}

std::vector<std::tuple<ea_t, std::string, int>> BinaryMemory::get_analysis_queue() const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    std::vector<std::tuple<ea_t, std::string, int>> result;

    std::priority_queue<AnalysisQueueItem> temp_queue = analysis_queue;
    while (!temp_queue.empty()) {
        const auto& item = temp_queue.top();
        result.push_back({item.address, item.reason, item.priority});
        temp_queue.pop();
    }

    return result;
}

void BinaryMemory::set_current_focus(ea_t address) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    current_focus = address;
}

ea_t BinaryMemory::get_current_focus() const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    return current_focus;
}

void BinaryMemory::add_insight(const std::string& type, const std::string& description, const std::vector<ea_t>& related_addresses) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    insights.push_back({type, description, related_addresses, std::time(nullptr)});
}

std::vector<std::tuple<std::string, std::vector<ea_t>>> BinaryMemory::get_insights(const std::string& type) const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    std::vector<std::tuple<std::string, std::vector<ea_t>>> result;

    for (const auto& insight : insights) {
        if (type.empty() || insight.type == type) {
            result.push_back({insight.description, insight.related_addresses});
        }
    }
    return result;
}

void BinaryMemory::analyze_cluster(const std::vector<ea_t>& addresses, const std::string& cluster_name, DetailLevel initial_level) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    for (ea_t addr : addresses) {
        auto& func_mem = function_memories[addr];
        func_mem.address = addr;
        // Tag as part of cluster
        func_mem.descriptions[DetailLevel::SUMMARY] = "Part of cluster: " + cluster_name;
        mark_for_analysis(addr, "Cluster analysis: " + cluster_name, 7);
    }
}

std::map<ea_t, std::string> BinaryMemory::get_cluster_analysis(const std::string& cluster_name) const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    std::map<ea_t, std::string> result;

    for (const auto& pair : function_memories) {
        for (const auto& desc_pair : pair.second.descriptions) {
            if (desc_pair.second.find("cluster: " + cluster_name) != std::string::npos) {
                result[pair.first] = get_function_analysis(pair.first);
                break;
            }
        }
    }
    return result;
}

std::string BinaryMemory::summarize_region(ea_t start_addr, ea_t end_addr) const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    std::stringstream summary;

    summary << "Region summary [0x" << std::hex << start_addr << " - 0x" << end_addr << "]:\n";

    int func_count = 0;
    for (const auto& pair : function_memories) {
        if (pair.first >= start_addr && pair.first <= end_addr) {
            func_count++;
            summary << "- " << pair.second.name << " (0x" << std::hex << pair.first << "): ";
            summary << get_function_analysis(pair.first) << "\n";
        }
    }

    summary << "Total functions: " << func_count << "\n";
    return summary.str();
}

json BinaryMemory::export_memory_snapshot() const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    json snapshot;

    // Export function memories
    json functions = json::array();
    for (const auto& pair : function_memories) {
        json func;
        func["address"] = HexAddress(pair.first);
        func["name"] = pair.second.name;
        func["distance_from_anchor"] = pair.second.distance_from_anchor;
        func["current_level"] = static_cast<int>(pair.second.current_level);

        json descriptions;
        for (const auto& desc_pair : pair.second.descriptions) {
            descriptions[std::to_string(static_cast<int>(desc_pair.first))] = desc_pair.second;
        }
        func["descriptions"] = descriptions;

        func["callers"] = json::array();
        for (ea_t caller : pair.second.callers) {
            func["callers"].push_back(HexAddress(caller));
        }

        func["callees"] = json::array();
        for (ea_t callee : pair.second.callees) {
            func["callees"].push_back(HexAddress(callee));
        }

        func["string_refs"] = pair.second.string_refs;
        func["data_refs"] = pair.second.data_refs;
        func["last_updated"] = pair.second.last_updated;
        func["needs_reanalysis"] = pair.second.needs_reanalysis;

        functions.push_back(func);
    }
    snapshot["functions"] = functions;

    // Export global notes
    snapshot["global_notes"] = global_notes;

    // Export insights
    json insights_json = json::array();
    for (const auto& insight : insights) {
        json ins;
        ins["type"] = insight.type;
        ins["description"] = insight.description;
        ins["related_addresses"] = insight.related_addresses;
        ins["timestamp"] = insight.timestamp;
        insights_json.push_back(ins);
    }
    snapshot["insights"] = insights_json;

    // Export state
    snapshot["current_focus"] = current_focus;
    snapshot["anchor_points"] = json::array();
    for (ea_t anchor : anchor_points) {
        snapshot["anchor_points"].push_back(anchor);
    }

    return snapshot;
}

void BinaryMemory::import_memory_snapshot(const json& snapshot) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    // Clear existing data
    function_memories.clear();
    global_notes.clear();
    insights.clear();
    anchor_points.clear();

    // Import function memories
    if (snapshot.contains("functions")) {
        for (const auto& func : snapshot["functions"]) {
            FunctionMemory fm;
            fm.address = func["address"];
            fm.name = func["name"];
            fm.distance_from_anchor = func["distance_from_anchor"];
            fm.current_level = static_cast<DetailLevel>(func["current_level"].get<int>());

            if (func.contains("descriptions")) {
                for (const auto& [level_str, desc] : func["descriptions"].items()) {
                    DetailLevel level = static_cast<DetailLevel>(std::stoi(level_str));
                    fm.descriptions[level] = desc;
                }
            }

            if (func.contains("callers")) {
                for (const nlohmann::basic_json<> &caller: func["callers"]) {
                    fm.callers.insert(caller.get<ea_t>());
                }
            }

            if (func.contains("callees")) {
                for (const nlohmann::basic_json<> &callee: func["callees"]) {
                    fm.callees.insert(callee.get<ea_t>());
                }
            }

            fm.string_refs = func["string_refs"].get<std::vector<std::string>>();
            fm.data_refs = func["data_refs"].get<std::vector<ea_t>>();
            fm.last_updated = func["last_updated"];
            fm.needs_reanalysis = func["needs_reanalysis"];

            function_memories[fm.address] = fm;
        }
    }

    // Import global notes
    if (snapshot.contains("global_notes")) {
        global_notes = snapshot["global_notes"].get<std::map<std::string, std::string>>();
    }

    // Import insights
    if (snapshot.contains("insights")) {
        for (const auto& ins : snapshot["insights"]) {
            Insight insight;
            insight.type = ins["type"];
            insight.description = ins["description"];
            insight.related_addresses = ins["related_addresses"].get<std::vector<ea_t>>();
            insight.timestamp = ins["timestamp"];
            insights.push_back(insight);
        }
    }

    // Import state
    if (snapshot.contains("current_focus")) {
        current_focus = snapshot["current_focus"];
    }

    if (snapshot.contains("anchor_points")) {
        for (const nlohmann::basic_json<> &anchor: snapshot["anchor_points"]) {
            anchor_points.insert(anchor.get<ea_t>());
        }
    }
}

DetailLevel BinaryMemory::get_required_detail_level(ea_t func_addr) const {
    int distance = calculateDistance(func_addr, current_focus);
    if (distance == 0) return DetailLevel::COMPREHENSIVE;
    if (distance <= 2) return DetailLevel::ANALYTICAL;
    if (distance <= 4) return DetailLevel::CONTEXTUAL;
    return DetailLevel::SUMMARY;
}

void BinaryMemory::propagate_new_information(ea_t updated_func) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    auto it = function_memories.find(updated_func);
    if (it == function_memories.end()) return;

    auto& func = it->second;
    for (ea_t caller : func.callers) {
        auto caller_it = function_memories.find(caller);
        if (caller_it != function_memories.end()) {
            caller_it->second.needs_reanalysis = true;
        }
    }

    for (ea_t callee : func.callees) {
        auto callee_it = function_memories.find(callee);
        if (callee_it != function_memories.end()) {
            callee_it->second.needs_reanalysis = true;
        }
    }
}

void BinaryMemory::add_anchor_point(ea_t address) {
    std::lock_guard<std::mutex> lock(memory_mutex);
    anchor_points.insert(address);

    auto it = function_memories.find(address);
    if (it != function_memories.end()) {
        it->second.distance_from_anchor = -1;
    }
}

bool BinaryMemory::is_anchor_point(ea_t address) const {
    std::lock_guard<std::mutex> lock(memory_mutex);
    return anchor_points.find(address) != anchor_points.end();
}

void BinaryMemory::update_function_relationships(ea_t func_addr, const std::set<ea_t>& callers, const std::set<ea_t>& callees) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    auto& func = function_memories[func_addr];
    func.callers = callers;
    func.callees = callees;
    func.last_updated = std::time(nullptr);
}

void BinaryMemory::update_function_refs(ea_t func_addr, const std::vector<std::string>& string_refs, const std::vector<ea_t>& data_refs) {
    std::lock_guard<std::mutex> lock(memory_mutex);

    auto& func = function_memories[func_addr];
    func.string_refs = string_refs;
    func.data_refs = data_refs;
    func.last_updated = std::time(nullptr);
}

} // namespace llm_re
