#include "nogo_zone_manager.h"
#include "orchestrator_logger.h"
#include <format>
#include <sstream>
#include <algorithm>

namespace llm_re::orchestrator {

NoGoZoneManager::NoGoZoneManager() {
    ORCH_LOG("NoGoZoneManager: Initialized\n");
}

NoGoZoneManager::~NoGoZoneManager() {
}

void NoGoZoneManager::add_zone(const NoGoZone& zone) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check for overlaps with existing zones
    for (const auto& existing : zones_) {
        if (existing.overlaps(zone.start_address, zone.end_address)) {
            ORCH_LOG("NoGoZoneManager: WARNING - New zone from %s overlaps with existing zone from %s\n",
                zone.agent_id.c_str(), existing.agent_id.c_str());
        }
    }

    // Add the zone
    size_t index = zones_.size();
    zones_.push_back(zone);
    agent_zone_indices_[zone.agent_id].insert(index);

    const char* type_str = (zone.type == NoGoZoneType::TEMP_SEGMENT) ? "TEMP_SEGMENT" : "CODE_CAVE";
    ORCH_LOG("NoGoZoneManager: Added %s zone from %s: 0x%llX-0x%llX\n",
        type_str, zone.agent_id.c_str(),
        (uint64_t)zone.start_address, (uint64_t)zone.end_address);
}

void NoGoZoneManager::remove_agent_zones(const std::string& agent_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = agent_zone_indices_.find(agent_id);
    if (it == agent_zone_indices_.end()) {
        return;
    }

    // Remove zones in reverse order to maintain indices
    std::vector<size_t> indices(it->second.begin(), it->second.end());
    std::sort(indices.rbegin(), indices.rend());

    for (size_t idx : indices) {
        if (idx < zones_.size()) {
            zones_.erase(zones_.begin() + idx);
        }
    }

    // Remove from tracking map
    agent_zone_indices_.erase(it);

    // Rebuild indices for all agents (since we modified the vector)
    agent_zone_indices_.clear();
    for (size_t i = 0; i < zones_.size(); ++i) {
        agent_zone_indices_[zones_[i].agent_id].insert(i);
    }

    ORCH_LOG("NoGoZoneManager: Removed all zones for agent %s\n", agent_id.c_str());
}

std::vector<NoGoZone> NoGoZoneManager::get_all_zones() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return zones_;
}

std::vector<NoGoZone> NoGoZoneManager::get_zones_by_type(NoGoZoneType type) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<NoGoZone> result;
    for (const auto& zone : zones_) {
        if (zone.type == type) {
            result.push_back(zone);
        }
    }
    return result;
}

bool NoGoZoneManager::is_no_go(ea_t start, ea_t end) const {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& zone : zones_) {
        if (zone.overlaps(start, end)) {
            return true;
        }
    }
    return false;
}

bool NoGoZoneManager::is_no_go(ea_t address) const {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& zone : zones_) {
        if (zone.contains(address)) {
            return true;
        }
    }
    return false;
}

ea_t NoGoZoneManager::find_safe_allocation(size_t size, ea_t start_search_from) const {
    std::lock_guard<std::mutex> lock(mutex_);

    ea_t current = start_search_from;

    // Keep searching until we find a gap large enough
    while (current != BADADDR) {
        ea_t range_end = current + size;

        // Check if this range overlaps any no-go zone
        bool overlaps = false;
        for (const auto& zone : zones_) {
            if (zone.overlaps(current, range_end)) {
                // Move past this zone and try again
                current = zone.end_address;
                overlaps = true;
                break;
            }
        }

        if (!overlaps) {
            // Found a safe spot
            return current;
        }

        // Check for overflow
        if (current + size < current) {
            break;
        }
    }

    return BADADDR;
}

std::string NoGoZoneManager::serialize_zone(const NoGoZone& zone) {
    // Format: NOGO|TYPE|agent_id|start_addr|end_addr
    const char* type_str = (zone.type == NoGoZoneType::TEMP_SEGMENT) ? "SEGMENT" : "CAVE";

    return std::format("NOGO|{}|{}|{:#x}|{:#x}",
        type_str, zone.agent_id, zone.start_address, zone.end_address);
}

std::optional<NoGoZone> NoGoZoneManager::deserialize_zone(const std::string& data) {
    // Parse format: NOGO|TYPE|agent_id|start_addr|end_addr
    std::stringstream ss(data);
    std::string token;
    std::vector<std::string> tokens;

    while (std::getline(ss, token, '|')) {
        tokens.push_back(token);
    }

    if (tokens.size() != 5 || tokens[0] != "NOGO") {
        return std::nullopt;
    }

    try {
        NoGoZone zone;

        // Parse type
        if (tokens[1] == "SEGMENT") {
            zone.type = NoGoZoneType::TEMP_SEGMENT;
        } else if (tokens[1] == "CAVE") {
            zone.type = NoGoZoneType::CODE_CAVE;
        } else {
            return std::nullopt;
        }

        // Parse agent ID
        zone.agent_id = tokens[2];

        // Parse addresses (handle hex format)
        zone.start_address = std::stoull(tokens[3], nullptr, 0);
        zone.end_address = std::stoull(tokens[4], nullptr, 0);

        zone.timestamp = std::chrono::system_clock::now();

        return zone;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace llm_re::orchestrator