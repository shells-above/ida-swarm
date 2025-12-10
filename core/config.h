#pragma once

#include "common_base.h"
#include "sdk/client/client.h"
#include <memory>
#include <vector>
#include <optional>

namespace llm_re {

// Configuration structure for the plugin
struct Config {
    // Singleton instance
    static Config& instance() {
        static Config instance;
        return instance;
    }

    // Note: create_oauth_manager() REMOVED - components now use Client with global OAuth pool

    // Load configuration from default location
    void load();
    
    // Save configuration to default location
    void save() const;
    
    // Reset to defaults
    void reset();
    
private:
    Config() = default;
    
public:
    struct IRCSettings {
        // Server configuration
        std::string server = "127.0.0.1";
    } irc;
    
    struct APISettings {
        // Authentication
        claude::AuthMethod auth_method = claude::AuthMethod::API_KEY;
        std::string api_key;
        bool use_oauth = false;
        std::string oauth_config_dir = "~/.claude_cpp_sdk";
        
        // Connection settings
        std::string base_url = "https://api.anthropic.com/v1/messages";
    } api;

    struct AgentSettings {
        // Model settings
        claude::Model model = claude::Model::Sonnet45;
        int max_tokens = 8192;
        int max_thinking_tokens = 4096;
        double temperature = 0.0;
        
        // Agent behavior settings
        bool enable_thinking = true;
        bool enable_interleaved_thinking = true;
        bool enable_deep_analysis = false;
        bool enable_python_tool = false;  // Disabled by default for security
        
        // Context management
        int context_limit = 150000;  // Token limit for tool result trimming
        int tool_result_trim_buffer = 8000;  // Safety buffer when trimming tool results (increased for conservative estimation)
    } agent;

    struct GraderSettings {
        bool enabled = true;  // Whether the grader is enabled
        claude::Model model = claude::Model::Sonnet45;
        int max_tokens = 32000;
        int max_thinking_tokens = 30000;
        int context_limit = 140000;  // Leave buffer below 150k limit
    } grader;

    struct OrchestratorSettings {
        // Model Configuration (for orchestrator's reasoning)
        struct Model {
            claude::Model model = claude::Model::Sonnet45;
            int max_tokens = 32000;
            int max_thinking_tokens = 30000;
            double temperature = 1.0;
            bool enable_thinking = true;
        } model;
    } orchestrator;
    
    struct SwarmSettings {
        // Auto-decompile settings
        int max_parallel_auto_decompile_agents = 10;  // Max parallel agents for auto-decompile

        // Function Prioritization Heuristics
        // Each heuristic can be enabled/disabled with a weight
        // Positive weights = higher priority, negative weights = lower priority

        // API calls: Functions calling library functions (fopen, malloc, etc.)
        // High positive weight: API names reveal function purpose
        bool enable_api_call_heuristic = true;
        double api_call_weight = 2.0;

        // Caller count: Functions called by many others (high-impact utilities)
        // High positive weight: Understanding these helps many other functions
        bool enable_caller_count_heuristic = true;
        double caller_count_weight = 1.5;

        // String-heavy: Functions with many string references
        // High positive weight: Strings reveal function purpose
        bool enable_string_heavy_heuristic = true;
        double string_heavy_weight = 2.0;
        int min_string_length_for_priority = 10;

        // Function size: Smaller functions first
        // Positive weight: Small functions = quick wins, build momentum
        bool enable_function_size_heuristic = true;
        double function_size_weight = 1.5;

        // Internal callees: Functions calling many internal (non-library) functions
        // NEGATIVE weight: These need callees analyzed first (bottom-up)
        bool enable_internal_callee_heuristic = true;
        double internal_callee_weight = -1.0;  // Negative by default

        // Entry points: main(), DllMain, exports
        // Default NEGATIVE: Entry points benefit from bottom-up analysis
        // Set POSITIVE for libraries (exports are the API) or if you want top-down
        bool enable_entry_point_heuristic = true;
        double entry_point_weight = -1.0;  // Negative by default (bottom-up)

        // Entry point scoring mode
        enum class EntryPointMode {
            BOTTOM_UP,    // Negative scores: analyze entry points LAST (default)
            TOP_DOWN,     // Positive scores: analyze entry points FIRST
            NEUTRAL       // Zero scores: don't prioritize or deprioritize
        } entry_point_mode = EntryPointMode::BOTTOM_UP;
    } swarm;

    struct ProfilingSettings {
        bool enabled = true;  // Enable/disable profiling
    } profiling;

    struct LLDBSettings {
        bool enabled = false;
        std::string lldb_path = "/usr/bin/lldb";

        // Global device registry - jailbroken iOS devices user owns
        // NOTE: Remote debugging currently only supports jailbroken iOS devices.
        // TODO: Future support for Linux (lldb-server), Android, macOS, Windows
        struct GlobalDevice {
            std::string id;                 // Unique identifier (UDID or user-provided)
            std::string name;               // Human-readable name
            std::string host;               // IP/hostname
            int ssh_port = 22;              // SSH port
            std::string ssh_user = "root";  // SSH username
            // NOTE: debugserver_port removed - now auto-derived from IRC port at runtime

            // Auto-discovered device information (cached)
            struct DeviceInfo {
                std::string udid;
                std::string model;
                std::string ios_version;
                std::string name;
            };
            std::optional<DeviceInfo> device_info;
        };
        std::vector<GlobalDevice> devices;  // Global device registry
    } lldb;

    // Load/save configuration
    bool save_to_file(const std::string& path) const;
    bool load_from_file(const std::string& path);
};

} // namespace llm_re