//
// Profiling Manager - High-level interface for controlling profiling
//

#ifndef PROFILING_MANAGER_H
#define PROFILING_MANAGER_H

#include "profiler.h"
#include "config.h"
#include <iostream>
#include <filesystem>
#include <format>

namespace llm_re::profiling {

class ProfilingManager {
public:
    static ProfilingManager& instance() {
        static ProfilingManager instance;
        return instance;
    }

    // Initialize profiling from config
    void initialize(const Config& config) {
        // Check if profiling should be enabled (add to config later)
        // For now, enable by default
        enable();
    }

    // Enable/disable profiling
    void enable() {
        Profiler::instance().enable();
    }

    void disable() {
        Profiler::instance().disable();
    }

    bool is_enabled() const {
        return Profiler::instance().is_enabled();
    }

    // Save comprehensive report to files (returns true on success)
    bool save_report(const std::string& binary_name) {
        if (!is_enabled()) {
            return false;
        }

        std::string workspace_dir = "/tmp/ida_swarm_workspace";
        std::filesystem::path profiling_dir = std::filesystem::path(workspace_dir) / binary_name / "profiling";

        // Create profiling directory
        try {
            std::filesystem::create_directories(profiling_dir);
        } catch (const std::exception& e) {
            return false;
        }

        // Save human-readable report
        std::string report_path = (profiling_dir / "profile_report.txt").string();
        Profiler::instance().save_report(report_path);

        // Save JSON data
        std::string json_path = (profiling_dir / "profile_data.json").string();
        Profiler::instance().save_json(json_path);

        return true;
    }

    // Get report directory path
    std::string get_report_directory(const std::string& binary_name) const {
        std::string workspace_dir = "/tmp/ida_swarm_workspace";
        std::filesystem::path profiling_dir = std::filesystem::path(workspace_dir) / binary_name / "profiling";
        return profiling_dir.string();
    }

    // Get current profiling data as JSON
    json get_summary() {
        if (!is_enabled()) {
            return json::object();
        }

        return Profiler::instance().get_summary();
    }

    // Reset all profiling data
    void reset() {
        Profiler::instance().reset();
    }

private:
    ProfilingManager() {}
};

} // namespace llm_re::profiling

#endif // PROFILING_MANAGER_H
