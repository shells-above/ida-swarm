#pragma once

#include "../core/common.h"
#include <vector>
#include <mutex>
#include <chrono>
#include <set>

namespace llm_re::orchestrator {

// Types of no-go zones
enum class NoGoZoneType {
    TEMP_SEGMENT,  // Temporary code injection workspace
    CODE_CAVE      // Used code cave
};

// A no-go zone that agents must avoid
struct NoGoZone {
    ea_t start_address;
    ea_t end_address;
    std::string agent_id;
    NoGoZoneType type;
    std::chrono::system_clock::time_point timestamp;

    // Check if an address range overlaps with this zone
    bool overlaps(ea_t start, ea_t end) const {
        return !(end <= start_address || start >= end_address);
    }

    // Check if a single address is within this zone
    bool contains(ea_t address) const {
        return address >= start_address && address < end_address;
    }
};

// Manages no-go zones across all agents
class NoGoZoneManager {
public:
    NoGoZoneManager();
    ~NoGoZoneManager();

    // Add a new no-go zone
    void add_zone(const NoGoZone& zone);

    // Remove zones for a specific agent (e.g., when agent completes)
    void remove_agent_zones(const std::string& agent_id);

    // Get all current no-go zones
    std::vector<NoGoZone> get_all_zones() const;

    // Get zones of a specific type
    std::vector<NoGoZone> get_zones_by_type(NoGoZoneType type) const;

    // Check if an address range overlaps any no-go zone
    bool is_no_go(ea_t start, ea_t end) const;

    // Check if a single address is in a no-go zone
    bool is_no_go(ea_t address) const;

    // Find a safe allocation address that avoids all no-go zones
    ea_t find_safe_allocation(size_t size, ea_t start_search_from) const;

    // Serialize zone for IRC broadcast
    static std::string serialize_zone(const NoGoZone& zone);

    // Deserialize zone from IRC message
    static std::optional<NoGoZone> deserialize_zone(const std::string& data);

private:
    std::vector<NoGoZone> zones_;
    mutable std::mutex mutex_;

    // Track which agents have zones (for quick cleanup)
    std::map<std::string, std::set<size_t>> agent_zone_indices_;
};

} // namespace llm_re::orchestrator