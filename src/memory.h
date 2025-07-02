//
// Created by user on 6/29/25.
//

#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"

namespace llm_re {

enum class DetailLevel {
    SUMMARY = 0,        // Basic function purpose
    CONTEXTUAL = 1,     // How it relates to nearby functions
    ANALYTICAL = 2,     // Detailed analysis with data flow
    COMPREHENSIVE = 3   // Full breakdown including all relationships
};

// Unified analysis storage structure
struct AnalysisEntry {
    std::string key;
    std::string content;
    std::string type;  // "note", "finding", "hypothesis", "question", "analysis"
    std::optional<ea_t> address;
    std::vector<ea_t> related_addresses;
    std::time_t timestamp;

    // For function-specific analysis
    std::optional<DetailLevel> detail_level;
};

struct FunctionMemory {
    ea_t address;
    std::string name;
    int distance_from_anchor;  // -1 if this IS an anchor
    DetailLevel current_level;

    // Relationships
    std::set<ea_t> callers;
    std::set<ea_t> callees;
    std::vector<std::string> string_refs;
    std::vector<ea_t> data_refs;

    // Metadata
    std::time_t last_updated;
    bool needs_reanalysis;

    // Analysis keys associated with this function
    std::set<std::string> analysis_keys;
};

struct MemoryContext {
    std::vector<FunctionMemory> nearby_functions;
    std::vector<FunctionMemory> context_functions;
    std::map<std::string, std::string> llm_memory;  // Summary of relevant analyses
};

struct AnalysisQueueItem {
    ea_t address;
    std::string reason;
    int priority;

    bool operator<(const AnalysisQueueItem& other) const {
        return priority < other.priority;  // Higher priority first
    }
};

class BinaryMemory {
private:
    mutable std::mutex memory_mutex;

    // Core memory storage
    std::map<ea_t, FunctionMemory> function_memories;
    std::map<std::string, AnalysisEntry> analyses;  // Unified storage
    std::priority_queue<AnalysisQueueItem> analysis_queue;

    // State tracking
    std::set<ea_t> anchor_points;
    ea_t current_focus;

    // Call graph cache
    mutable std::map<std::pair<ea_t, ea_t>, int> call_graph_cache;

    // Helper methods
    int calculateDistance(ea_t from, ea_t to) const;
    int compute_call_graph_distance(ea_t from, ea_t to) const;
    std::string generate_analysis_key(const std::string& base_key) const;

public:
    BinaryMemory();
    ~BinaryMemory();

    // Unified analysis management
    void store_analysis(const std::string& key, const std::string& content,
                       std::optional<ea_t> address, const std::string& type,
                       const std::vector<ea_t>& related_addresses);

    std::vector<AnalysisEntry> get_analysis(const std::string& key = "",
                                           std::optional<ea_t> address = std::nullopt,
                                           const std::string& type = "",
                                           const std::string& pattern = "") const;

    // Function memory management
    void set_function_analysis(ea_t address, DetailLevel level, const std::string& analysis);
    std::string get_function_analysis(ea_t address, DetailLevel level = DetailLevel::SUMMARY) const;
    MemoryContext get_memory_context(ea_t address, int radius = 2) const;

    // Memory queries
    std::vector<std::tuple<ea_t, std::string, DetailLevel>> get_analyzed_functions() const;
    std::vector<ea_t> find_functions_by_pattern(const std::string& pattern) const;
    std::vector<std::tuple<ea_t, std::string, std::string>> get_exploration_frontier() const;

    // Working memory
    void mark_for_analysis(ea_t address, const std::string& reason, int priority = 5);
    std::vector<std::tuple<ea_t, std::string, int>> get_analysis_queue() const;
    void set_current_focus(ea_t address);
    ea_t get_current_focus() const;

    // Bulk operations
    void analyze_cluster(const std::vector<ea_t>& addresses, const std::string& cluster_name, DetailLevel initial_level);
    std::map<ea_t, std::string> get_cluster_analysis(const std::string& cluster_name) const;

    // Memory efficiency
    json export_memory_snapshot() const;
    void import_memory_snapshot(const json& snapshot);

    // Utility methods
    DetailLevel get_required_detail_level(ea_t func_addr) const;
    void propagate_new_information(ea_t updated_func);
    void add_anchor_point(ea_t address);
    bool is_anchor_point(ea_t address) const;

    // Update function relationships
    void update_function_relationships(ea_t func_addr, const std::set<ea_t>& callers, const std::set<ea_t>& callees);
    void update_function_refs(ea_t func_addr, const std::vector<std::string>& string_refs, const std::vector<ea_t>& data_refs);

    // Legacy compatibility helpers (used internally)
    void set_global_note(const std::string& key, const std::string& content);
    std::string get_global_note(const std::string& key) const;
    std::vector<std::string> list_global_notes() const;
    std::vector<std::pair<std::string, std::string>> search_notes(const std::string& query) const;
    void add_insight(const std::string& type, const std::string& description, const std::vector<ea_t>& related_addresses);
    std::vector<std::tuple<std::string, std::vector<ea_t>>> get_insights(const std::string& type = "") const;
};

} // namespace llm_re

#endif //MEMORY_H