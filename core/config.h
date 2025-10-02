#pragma once

#include "common_base.h"
#include "sdk/client/client.h"
#include "sdk/auth/oauth_manager.h"
#include <memory>

namespace llm_re {

// Configuration structure for the plugin
struct Config {
    // Singleton instance
    static Config& instance() {
        static Config instance;
        return instance;
    }
    
    // Create a new OAuth manager instance for a component
    // Each component creates its own instance that reads from the same credential files
    static std::shared_ptr<claude::auth::OAuthManager> create_oauth_manager(const std::string& config_dir) {
        return std::make_shared<claude::auth::OAuthManager>(config_dir);
    }
    
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
        // Swarm-specific settings
    } swarm;

    // Load/save configuration
    bool save_to_file(const std::string& path) const;
    bool load_from_file(const std::string& path);
};

} // namespace llm_re