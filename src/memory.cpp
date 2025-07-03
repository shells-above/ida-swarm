//
// Created by user on 6/29/25.
//

#include "memory.h"
#include <sstream>
#include <iomanip>
#include <regex>

namespace llm_re {

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

std::string BinaryMemory::generate_analysis_key(const std::string& base_key) const {
    // If key already exists, append a number
    if (analyses.find(base_key) == analyses.end()) {
        return base_key;
    }

    int counter = 1;
    std::string new_key;
    do {
        new_key = base_key + "_" + std::to_string(counter++);
    } while (analyses.find(new_key) != analyses.end());

    return new_key;
}

// Unified analysis management
void BinaryMemory::store_analysis(const std::string& key, const std::string& content,
                                 std::optional<ea_t> address, const std::string& type,
                                 const std::vector<ea_t>& related_addresses) {
    qmutex_locker_t lock(memory_mutex);

    // Generate unique key if needed
    std::string actual_key = generate_analysis_key(key);

    AnalysisEntry entry;
    entry.key = actual_key;
    entry.content = content;
    entry.type = type;
    entry.address = address;
    entry.related_addresses = related_addresses;
    entry.timestamp = std::time(nullptr);

    // Determine detail level for function analysis
    if (address && type == "analysis") {
        DetailLevel level = get_required_detail_level(*address);
        entry.detail_level = level;
    }

    analyses[actual_key] = entry;

    // Update function memory if address is provided
    if (address) {
        auto& func_mem = function_memories[*address];
        func_mem.address = *address;
        func_mem.analysis_keys.insert(actual_key);
        func_mem.last_updated = std::time(nullptr);

        if (entry.detail_level) {
            func_mem.current_level = std::max(func_mem.current_level, *entry.detail_level);
        }
    }

    // Update function memories for related addresses
    for (ea_t related : related_addresses) {
        auto& func_mem = function_memories[related];
        func_mem.address = related;
        func_mem.analysis_keys.insert(actual_key);
    }
}

std::vector<AnalysisEntry> BinaryMemory::get_analysis(const std::string& key,
                                                     std::optional<ea_t> address,
                                                     const std::string& type,
                                                     const std::string& pattern) const {
    qmutex_locker_t lock(memory_mutex);
    std::vector<AnalysisEntry> results;

    // If specific key requested
    if (!key.empty()) {
        auto it = analyses.find(key);
        if (it != analyses.end()) {
            results.push_back(it->second);
        }
        return results;
    }

    // Build regex pattern if provided
    std::regex regex_pattern;
    bool use_regex = !pattern.empty();
    if (use_regex) {
        try {
            regex_pattern = std::regex(pattern, std::regex_constants::icase);
        } catch (...) {
            use_regex = false;
        }
    }

    // Search through all analyses
    for (const auto& [analysis_key, entry] : analyses) {
        // Check type filter
        if (!type.empty() && entry.type != type) continue;

        // Check address filter
        if (address) {
            bool address_match = false;
            if (entry.address && *entry.address == *address) {
                address_match = true;
            } else {
                for (ea_t related : entry.related_addresses) {
                    if (related == *address) {
                        address_match = true;
                        break;
                    }
                }
            }
            if (!address_match) continue;
        }

        // Check pattern filter
        if (use_regex) {
            if (!std::regex_search(entry.content, regex_pattern)) continue;
        }

        results.push_back(entry);
    }

    // Sort by timestamp (newest first)
    std::sort(results.begin(), results.end(),
              [](const AnalysisEntry& a, const AnalysisEntry& b) {
                  return a.timestamp > b.timestamp;
              });

    return results;
}

// Function memory management
void BinaryMemory::set_function_analysis(ea_t address, DetailLevel level, const std::string& analysis) {
    // Store as unified analysis
    std::string key = "func_" + std::to_string(address) + "_level" + std::to_string(static_cast<int>(level));
    store_analysis(key, analysis, address, "analysis", {});
}

std::string BinaryMemory::get_function_analysis(ea_t address, DetailLevel level) const {
    // Try to get analysis at specific level
    if (level != DetailLevel::SUMMARY) {
        std::string key = "func_" + std::to_string(address) + "_level" + std::to_string(static_cast<int>(level));
        auto entries = get_analysis(key);
        if (!entries.empty()) {
            return entries[0].content;
        }
    }

    // For SUMMARY or if specific level not found, get best available
    auto entries = get_analysis("", address, "analysis");

    // Sort by detail level (highest first)
    std::sort(entries.begin(), entries.end(),
              [](const AnalysisEntry& a, const AnalysisEntry& b) {
                  if (a.detail_level && b.detail_level) {
                      return *a.detail_level > *b.detail_level;
                  }
                  return false;
              });

    if (!entries.empty()) {
        return entries[0].content;
    }

    return "";
}

MemoryContext BinaryMemory::get_memory_context(ea_t address, int radius) const {
    qmutex_locker_t lock(memory_mutex);
    MemoryContext context;

    // Get functions within radius
    for (const auto& [func_addr, func_mem] : function_memories) {
        int distance = calculateDistance(address, func_addr);

        // Create a copy for the context
        FunctionMemory func_copy = func_mem;
        func_copy.distance_from_anchor = distance;

        if (distance <= radius) {
            context.nearby_functions.push_back(func_copy);
        } else if (distance <= radius * 2) {
            context.context_functions.push_back(func_copy);
        }
    }

    // Sort by distance
    auto sort_by_distance = [](const FunctionMemory& a, const FunctionMemory& b) {
        return a.distance_from_anchor < b.distance_from_anchor;
    };
    std::sort(context.nearby_functions.begin(), context.nearby_functions.end(), sort_by_distance);
    std::sort(context.context_functions.begin(), context.context_functions.end(), sort_by_distance);

    // Build LLM memory summary from relevant analyses
    auto relevant_analyses = get_analysis("", std::nullopt, "", "");
    for (const auto& entry : relevant_analyses) {
        if (entry.type == "note" || entry.type == "finding") {
            std::string summary_key = entry.type + "_" + entry.key.substr(0, 20);
            context.llm_memory[summary_key] = entry.content.substr(0, 200) + "...";
        }
    }

    return context;
}

std::vector<std::tuple<ea_t, std::string, DetailLevel>> BinaryMemory::get_analyzed_functions() const {
    qmutex_locker_t lock(memory_mutex);
    std::vector<std::tuple<ea_t, std::string, DetailLevel>> result;

    for (const auto& [address, func_mem] : function_memories) {
        if (!func_mem.analysis_keys.empty()) {
            result.push_back({address, func_mem.name, func_mem.current_level});
        }
    }

    return result;
}

std::vector<ea_t> BinaryMemory::find_functions_by_pattern(const std::string& pattern) const {
    qmutex_locker_t lock(memory_mutex);
    std::vector<ea_t> results;
    std::set<ea_t> unique_results;

    std::regex re(pattern, std::regex_constants::icase);

    // Search through all analyses
    for (const auto& [key, entry] : analyses) {
        if (std::regex_search(entry.content, re)) {
            if (entry.address) {
                unique_results.insert(*entry.address);
            }
            for (ea_t related : entry.related_addresses) {
                unique_results.insert(related);
            }
        }
    }

    // Also search function names
    for (const auto& [address, func_mem] : function_memories) {
        if (std::regex_search(func_mem.name, re)) {
            unique_results.insert(address);
        }
    }

    results.assign(unique_results.begin(), unique_results.end());
    return results;
}

std::vector<std::tuple<ea_t, std::string, std::string>> BinaryMemory::get_exploration_frontier() const {
    qmutex_locker_t lock(memory_mutex);
    std::vector<std::tuple<ea_t, std::string, std::string>> frontier;

    // Get items from analysis queue
    std::priority_queue<AnalysisQueueItem> temp_queue = analysis_queue;
    while (!temp_queue.empty() && frontier.size() < 20) {
        const auto& item = temp_queue.top();
        auto it = function_memories.find(item.address);
        std::string name = (it != function_memories.end()) ? it->second.name : "";
        frontier.push_back({item.address, name, item.reason});
        temp_queue.pop();
    }

    // Add functions that need reanalysis
    for (const auto& [address, func_mem] : function_memories) {
        if (func_mem.needs_reanalysis && frontier.size() < 30) {
            frontier.push_back({address, func_mem.name, "Needs reanalysis due to updated dependencies"});
        }
    }

    return frontier;
}

void BinaryMemory::mark_for_analysis(ea_t address, const std::string& reason, int priority) {
    qmutex_locker_t lock(memory_mutex);
    analysis_queue.push({address, reason, priority});

    // Initialize function memory if not exists
    if (function_memories.find(address) == function_memories.end()) {
        function_memories[address].address = address;
    }
}

std::vector<std::tuple<ea_t, std::string, int>> BinaryMemory::get_analysis_queue() const {
    qmutex_locker_t lock(memory_mutex);
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
    qmutex_locker_t lock(memory_mutex);
    current_focus = address;

    // Initialize function memory if not exists
    if (function_memories.find(address) == function_memories.end()) {
        function_memories[address].address = address;
    }
}

ea_t BinaryMemory::get_current_focus() const {
    qmutex_locker_t lock(memory_mutex);
    return current_focus;
}

void BinaryMemory::analyze_cluster(const std::vector<ea_t>& addresses, const std::string& cluster_name, DetailLevel initial_level) {
    qmutex_locker_t lock(memory_mutex);

    // Create cluster analysis entry
    std::string cluster_key = "cluster_" + cluster_name;
    std::stringstream content;
    content << "Cluster analysis for: " << cluster_name << "\n";
    content << "Functions in cluster: " << addresses.size() << "\n";

    store_analysis(cluster_key, content.str(), std::nullopt, "analysis", addresses);

    // Mark all functions for analysis
    for (ea_t addr : addresses) {
        auto& func_mem = function_memories[addr];
        func_mem.address = addr;
        mark_for_analysis(addr, "Part of cluster: " + cluster_name, 7);
    }
}

std::map<ea_t, std::string> BinaryMemory::get_cluster_analysis(const std::string& cluster_name) const {
    qmutex_locker_t lock(memory_mutex);
    std::map<ea_t, std::string> result;

    // Find cluster analysis
    std::string cluster_key = "cluster_" + cluster_name;
    auto entries = get_analysis(cluster_key);

    if (!entries.empty()) {
        // Get analyses for all related addresses
        for (ea_t addr : entries[0].related_addresses) {
            std::string analysis = get_function_analysis(addr);
            if (!analysis.empty()) {
                result[addr] = analysis;
            }
        }
    }

    return result;
}

json BinaryMemory::export_memory_snapshot() const {
    qmutex_locker_t lock(memory_mutex);
    json snapshot;

    // Export function memories
    json functions = json::array();
    for (const auto& [address, func_mem] : function_memories) {
        json func;
        func["address"] = HexAddress(address);
        func["name"] = func_mem.name;
        func["distance_from_anchor"] = func_mem.distance_from_anchor;
        func["current_level"] = static_cast<int>(func_mem.current_level);

        func["callers"] = json::array();
        for (ea_t caller : func_mem.callers) {
            func["callers"].push_back(HexAddress(caller));
        }

        func["callees"] = json::array();
        for (ea_t callee : func_mem.callees) {
            func["callees"].push_back(HexAddress(callee));
        }

        func["string_refs"] = func_mem.string_refs;

        func["data_refs"] = json::array();
        for (ea_t ref : func_mem.data_refs) {
            func["data_refs"].push_back(HexAddress(ref));
        }

        func["last_updated"] = func_mem.last_updated;
        func["needs_reanalysis"] = func_mem.needs_reanalysis;

        func["analysis_keys"] = json::array();
        for (const std::string& key : func_mem.analysis_keys) {
            func["analysis_keys"].push_back(key);
        }

        functions.push_back(func);
    }
    snapshot["functions"] = functions;

    // Export analyses
    json analyses_json = json::array();
    for (const auto& [key, entry] : analyses) {
        json analysis;
        analysis["key"] = entry.key;
        analysis["content"] = entry.content;
        analysis["type"] = entry.type;
        if (entry.address) {
            analysis["address"] = HexAddress(*entry.address);
        }

        analysis["related_addresses"] = json::array();
        for (ea_t addr : entry.related_addresses) {
            analysis["related_addresses"].push_back(HexAddress(addr));
        }

        analysis["timestamp"] = entry.timestamp;
        if (entry.detail_level) {
            analysis["detail_level"] = static_cast<int>(*entry.detail_level);
        }

        analyses_json.push_back(analysis);
    }
    snapshot["analyses"] = analyses_json;

    // Export state
    snapshot["current_focus"] = HexAddress(current_focus);

    snapshot["anchor_points"] = json::array();
    for (ea_t anchor : anchor_points) {
        snapshot["anchor_points"].push_back(HexAddress(anchor));
    }

    // Export queue
    json queue_json = json::array();
    std::priority_queue<AnalysisQueueItem> temp_queue = analysis_queue;
    while (!temp_queue.empty()) {
        const auto& item = temp_queue.top();
        json queue_item;
        queue_item["address"] = HexAddress(item.address);
        queue_item["reason"] = item.reason;
        queue_item["priority"] = item.priority;
        queue_json.push_back(queue_item);
        temp_queue.pop();
    }
    snapshot["analysis_queue"] = queue_json;

    return snapshot;
}

void BinaryMemory::import_memory_snapshot(const json& snapshot) {
    qmutex_locker_t lock(memory_mutex);

    // Clear existing data
    function_memories.clear();
    analyses.clear();
    anchor_points.clear();
    call_graph_cache.clear();
    while (!analysis_queue.empty()) {
        analysis_queue.pop();
    }

    // Import function memories
    if (snapshot.contains("functions")) {
        for (const auto& func : snapshot["functions"]) {
            FunctionMemory fm;
            fm.address = func["address"];
            fm.name = func.value("name", "");
            fm.distance_from_anchor = func["distance_from_anchor"];
            fm.current_level = static_cast<DetailLevel>(func["current_level"].get<int>());

            if (func.contains("callers")) {
                for (const auto& caller : func["callers"]) {
                    fm.callers.insert(caller.get<ea_t>());
                }
            }

            if (func.contains("callees")) {
                for (const auto& callee : func["callees"]) {
                    fm.callees.insert(callee.get<ea_t>());
                }
            }

            if (func.contains("string_refs")) {
                fm.string_refs = func["string_refs"].get<std::vector<std::string>>();
            }

            if (func.contains("data_refs")) {
                for (const auto& ref : func["data_refs"]) {
                    fm.data_refs.push_back(ref.get<ea_t>());
                }
            }

            fm.last_updated = func["last_updated"];
            fm.needs_reanalysis = func["needs_reanalysis"];

            if (func.contains("analysis_keys")) {
                for (const auto& key : func["analysis_keys"]) {
                    fm.analysis_keys.insert(key.get<std::string>());
                }
            }

            function_memories[fm.address] = fm;
        }
    }

    // Import analyses
    if (snapshot.contains("analyses")) {
        for (const auto& analysis : snapshot["analyses"]) {
            AnalysisEntry entry;
            entry.key = analysis["key"];
            entry.content = analysis["content"];
            entry.type = analysis["type"];

            if (analysis.contains("address")) {
                entry.address = analysis["address"].get<ea_t>();
            }

            if (analysis.contains("related_addresses")) {
                for (const auto& addr : analysis["related_addresses"]) {
                    entry.related_addresses.push_back(addr.get<ea_t>());
                }
            }

            entry.timestamp = analysis["timestamp"];

            if (analysis.contains("detail_level")) {
                entry.detail_level = static_cast<DetailLevel>(analysis["detail_level"].get<int>());
            }

            analyses[entry.key] = entry;
        }
    }

    // Import state
    if (snapshot.contains("current_focus")) {
        current_focus = snapshot["current_focus"];
    }

    if (snapshot.contains("anchor_points")) {
        for (const auto& anchor : snapshot["anchor_points"]) {
            anchor_points.insert(anchor.get<ea_t>());
        }
    }

    // Import queue
    if (snapshot.contains("analysis_queue")) {
        for (const auto& item : snapshot["analysis_queue"]) {
            AnalysisQueueItem queue_item;
            queue_item.address = item["address"];
            queue_item.reason = item["reason"];
            queue_item.priority = item["priority"];
            analysis_queue.push(queue_item);
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
    qmutex_locker_t lock(memory_mutex);

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
    qmutex_locker_t lock(memory_mutex);
    anchor_points.insert(address);

    auto it = function_memories.find(address);
    if (it != function_memories.end()) {
        it->second.distance_from_anchor = -1;
    }
}

bool BinaryMemory::is_anchor_point(ea_t address) const {
    qmutex_locker_t lock(memory_mutex);
    return anchor_points.find(address) != anchor_points.end();
}

void BinaryMemory::update_function_relationships(ea_t func_addr, const std::set<ea_t>& callers, const std::set<ea_t>& callees) {
    qmutex_locker_t lock(memory_mutex);

    auto& func = function_memories[func_addr];
    func.address = func_addr;
    func.callers = callers;
    func.callees = callees;
    func.last_updated = std::time(nullptr);
}

void BinaryMemory::update_function_refs(ea_t func_addr, const std::vector<std::string>& string_refs, const std::vector<ea_t>& data_refs) {
    qmutex_locker_t lock(memory_mutex);

    auto& func = function_memories[func_addr];
    func.address = func_addr;
    func.string_refs = string_refs;
    func.data_refs = data_refs;
    func.last_updated = std::time(nullptr);
}

// Legacy compatibility helpers
void BinaryMemory::set_global_note(const std::string& key, const std::string& content) {
    store_analysis(key, content, std::nullopt, "note", {});
}

std::string BinaryMemory::get_global_note(const std::string& key) const {
    auto entries = get_analysis(key);
    return entries.empty() ? "" : entries[0].content;
}

std::vector<std::string> BinaryMemory::list_global_notes() const {
    auto entries = get_analysis("", std::nullopt, "note");
    std::vector<std::string> keys;
    for (const auto& entry : entries) {
        keys.push_back(entry.key);
    }
    return keys;
}

std::vector<std::pair<std::string, std::string>> BinaryMemory::search_notes(const std::string& query) const {
    auto entries = get_analysis("", std::nullopt, "note", query);
    std::vector<std::pair<std::string, std::string>> results;

    for (const auto& entry : entries) {
        // Extract snippet around match
        size_t pos = entry.content.find(query);
        if (pos != std::string::npos) {
            size_t start = (pos > 50) ? pos - 50 : 0;
            size_t end = std::min(pos + query.length() + 50, entry.content.length());
            std::string snippet = entry.content.substr(start, end - start);
            results.push_back({entry.key, snippet});
        }
    }

    return results;
}

void BinaryMemory::add_insight(const std::string& type, const std::string& description, const std::vector<ea_t>& related_addresses) {
    std::string key = "insight_" + type + "_" + std::to_string(std::time(nullptr));
    store_analysis(key, description, std::nullopt, type, related_addresses);
}

std::vector<std::tuple<std::string, std::vector<ea_t>>> BinaryMemory::get_insights(const std::string& type) const {
    auto entries = get_analysis("", std::nullopt, type);
    std::vector<std::tuple<std::string, std::vector<ea_t>>> results;

    for (const auto& entry : entries) {
        if (type.empty() || entry.type == type) {
            results.push_back({entry.content, entry.related_addresses});
        }
    }

    return results;
}

} // namespace llm_re