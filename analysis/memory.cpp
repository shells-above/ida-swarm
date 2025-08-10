//
// Created by user on 6/29/25.
//

#include "analysis/memory.h"

namespace llm_re {

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
void BinaryMemory::store_analysis(const std::string& key, const std::string& content, std::optional<ea_t> address, const std::string& type, const std::vector<ea_t>& related_addresses) {
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

    analyses[actual_key] = entry;
    version_counter.fetch_add(1);  // Increment version on change
}

std::vector<AnalysisEntry> BinaryMemory::get_analysis(const std::string& key, std::optional<ea_t> address, const std::string& type, const std::string& pattern) const {
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

json BinaryMemory::export_memory_snapshot() const {
    qmutex_locker_t lock(memory_mutex);
    json snapshot;

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

        analyses_json.push_back(analysis);
    }
    snapshot["analyses"] = analyses_json;

    return snapshot;
}

void BinaryMemory::import_memory_snapshot(const json& snapshot) {
    qmutex_locker_t lock(memory_mutex);

    // Clear existing data
    analyses.clear();
    version_counter.fetch_add(1);  // Increment version on import

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

            analyses[entry.key] = entry;
        }
    }
}

} // namespace llm_re