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
    
    // Save configuration
    void save() const;
    
    // Reset to defaults
    void reset();
    
private:
    Config() = default;
    
public:
    struct IRCSettings {
        // Server configuration
        std::string server = "127.0.0.1";
        int port = 6667;
        
        // Channel formats
        std::string conflict_channel_format = "#conflict_{address}_{type}";
        std::string private_channel_format = "#private_{agent1}_{agent2}";
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
        claude::Model model = claude::Model::Sonnet4;
        int max_tokens = 8192;
        int max_thinking_tokens = 4096;
        double temperature = 0.0;
        
        // Agent behavior settings
        int max_iterations = 1000;
        bool enable_thinking = true;
        bool enable_interleaved_thinking = true;
        bool enable_deep_analysis = false;
        bool enable_python_tool = false;  // Disabled by default for security
    } agent;

    struct GraderSettings {
        bool enabled = true;  // Whether the grader is enabled
        claude::Model model = claude::Model::Opus41;
        int max_tokens = 32000;
        int max_thinking_tokens = 31999;
        int context_limit = 140000;  // Leave buffer below 150k limit
    } grader;

    struct UISettings {
        int log_buffer_size = 1000;
        bool auto_scroll = true;
        std::string theme_name = "dark";  // "default", "dark", "light", or custom theme name
        int font_size = 10;
        bool show_timestamps = true;
        bool show_tool_details = true;
        
        // Window management
        bool start_minimized = false;
        bool remember_window_state = true;
        
        // Conversation view
        bool auto_save_conversations = true;
        int auto_save_interval = 60;  // seconds
        int density_mode = 1;  // 0=Compact, 1=Cozy, 2=Spacious
    } ui;

    struct OrchestratorSettings {
        // Model Configuration (for orchestrator's reasoning)
        struct Model {
            claude::Model model = claude::Model::Sonnet4;
            int max_tokens = 32000;
            int max_thinking_tokens = 31999;
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