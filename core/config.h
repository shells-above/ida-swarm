#pragma once

#include "common_base.h"
#include "api/anthropic_api.h"

namespace llm_re {

// Configuration structure for the plugin
struct Config {
    struct APISettings {
        std::string api_key;
        std::string base_url = "https://api.anthropic.com/v1/messages";
        api::Model model = api::Model::Sonnet4;
        int max_tokens = 8192;
        int max_thinking_tokens = 2048;
        double temperature = 0.0;
    } api;

    struct AgentSettings {
        int max_iterations = 1000;
        bool enable_thinking = true;
        bool enable_interleaved_thinking = false;
        bool enable_deep_analysis = false;
        bool verbose_logging = false;
    } agent;

    struct UISettings {
        int log_buffer_size = 1000;
        bool auto_scroll = true;
        int theme = 0;  // 0=default, 1=dark, 2=light
        int font_size = 10;
        bool show_timestamps = true;
        bool show_tool_details = true;
        
        // Window management
        bool show_tray_icon = true;
        bool minimize_to_tray = true;
        bool close_to_tray = false;
        bool start_minimized = false;
        bool remember_window_state = true;
        bool auto_save_layout = true;
        
        // Conversation view
        bool auto_save_conversations = true;
        int auto_save_interval = 60;  // seconds
        bool compact_mode = false;
        
        // Inspector
        bool inspector_follow_cursor = true;
        int inspector_opacity = 80;  // percentage
        bool inspector_auto_hide = true;
        int inspector_auto_hide_delay = 3000;  // milliseconds
    } ui;

    bool debug_mode = false;

    // Load/save configuration
    bool save_to_file(const std::string& path) const;
    bool load_from_file(const std::string& path);
};

} // namespace llm_re