//
// Created by user on 6/29/25.
//

#ifndef MEMORY_H
#define MEMORY_H

#include "common.h"

namespace llm_re {

// Unified analysis storage structure
struct AnalysisEntry {
    std::string key;
    std::string content;
    std::string type;  // "note", "finding", "hypothesis", "question", "analysis"
    std::optional<ea_t> address;
    std::vector<ea_t> related_addresses;
    std::time_t timestamp;
};


class BinaryMemory {
private:
    mutable qmutex_t memory_mutex;

    // Core memory storage
    std::map<std::string, AnalysisEntry> analyses;  // Unified storage

    // Helper methods
    std::string generate_analysis_key(const std::string& base_key) const;

public:
    BinaryMemory() {
        memory_mutex = qmutex_create();
    };
    ~BinaryMemory() {
        qmutex_free(memory_mutex);
    };

    // Unified analysis management
    void store_analysis(const std::string& key, const std::string& content, std::optional<ea_t> address, const std::string& type, const std::vector<ea_t>& related_addresses);
    std::vector<AnalysisEntry> get_analysis(const std::string& key = "", std::optional<ea_t> address = std::nullopt, const std::string& type = "", const std::string& pattern = "") const;

    // Memory efficiency
    json export_memory_snapshot() const;
    void import_memory_snapshot(const json& snapshot);
};

} // namespace llm_re

#endif //MEMORY_H