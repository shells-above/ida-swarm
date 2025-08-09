#pragma once

#include "common_base.h"
#include "api/anthropic_api.h"

namespace llm_re {

// Configuration structure for the plugin
struct Config {
    // Singleton instance
    static Config& instance() {
        static Config instance;
        return instance;
    }
    
    // Save configuration
    void save() const;
    
    // Reset to defaults
    void reset();
    
private:
    Config() = default;
    
public:
    struct APISettings {
        std::string api_key;
        std::string base_url = "https://api.anthropic.com/v1/messages";
        api::Model model = api::Model::Sonnet4;
        int max_tokens = 8192;
        int max_thinking_tokens = 4096;
        double temperature = 0.0;
    } api;

    struct AgentSettings {
        int max_iterations = 1000;
        bool enable_thinking = true;
        bool enable_interleaved_thinking = true;
        bool enable_deep_analysis = false;
    } agent;

    struct GraderSettings {
        api::Model model = api::Model::Opus41;
        int max_tokens = 32000;
        int max_thinking_tokens = 31999;
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

    // Load/save configuration
    bool save_to_file(const std::string& path) const;
    bool load_from_file(const std::string& path);
};

} // namespace llm_re