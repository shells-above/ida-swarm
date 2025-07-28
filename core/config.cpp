#include "config.h"

namespace llm_re {

bool Config::save_to_file(const std::string& path) const {
    try {
        json j;

        // API settings
        j["api"]["api_key"] = api.api_key;
        j["api"]["base_url"] = api.base_url;
        j["api"]["model"] = api::model_to_string(api.model);
        j["api"]["max_tokens"] = api.max_tokens;
        j["api"]["max_thinking_tokens"] = api.max_thinking_tokens;
        j["api"]["temperature"] = api.temperature;

        // Agent settings
        j["agent"]["max_iterations"] = agent.max_iterations;
        j["agent"]["enable_thinking"] = agent.enable_thinking;
        j["agent"]["enable_interleaved_thinking"] = agent.enable_interleaved_thinking;
        j["agent"]["enable_deep_analysis"] = agent.enable_deep_analysis;
        j["agent"]["verbose_logging"] = agent.verbose_logging;

        // UI settings
        j["ui"]["log_buffer_size"] = ui.log_buffer_size;
        j["ui"]["auto_scroll"] = ui.auto_scroll;
        j["ui"]["theme"] = ui.theme;
        j["ui"]["font_size"] = ui.font_size;
        j["ui"]["show_timestamps"] = ui.show_timestamps;
        j["ui"]["show_tool_details"] = ui.show_tool_details;
        
        // Window management
        j["ui"]["show_tray_icon"] = ui.show_tray_icon;
        j["ui"]["minimize_to_tray"] = ui.minimize_to_tray;
        j["ui"]["close_to_tray"] = ui.close_to_tray;
        j["ui"]["start_minimized"] = ui.start_minimized;
        j["ui"]["remember_window_state"] = ui.remember_window_state;
        j["ui"]["auto_save_layout"] = ui.auto_save_layout;
        
        // Conversation view
        j["ui"]["auto_save_conversations"] = ui.auto_save_conversations;
        j["ui"]["auto_save_interval"] = ui.auto_save_interval;
        j["ui"]["compact_mode"] = ui.compact_mode;
        
        // Inspector
        j["ui"]["inspector_follow_cursor"] = ui.inspector_follow_cursor;
        j["ui"]["inspector_opacity"] = ui.inspector_opacity;
        j["ui"]["inspector_auto_hide"] = ui.inspector_auto_hide;
        j["ui"]["inspector_auto_hide_delay"] = ui.inspector_auto_hide_delay;

        // Export settings
        j["export"]["path"] = export_settings.path;

        // Debug mode
        j["debug_mode"] = debug_mode;

        // Write to file
        std::ofstream file(path);
        if (!file) return false;
        
        file << j.dump(4);
        return true;
    } catch (const std::exception& e) {
        msg("LLM RE: Error saving config: %s\n", e.what());
        return false;
    }
}

bool Config::load_from_file(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file) return false;

        json j;
        file >> j;

        // API settings
        if (j.contains("api")) {
            api.api_key = j["api"].value("api_key", api.api_key);
            api.base_url = j["api"].value("base_url", api.base_url);
            if (j["api"].contains("model")) {
                api.model = api::model_from_string(j["api"]["model"]);
            }
            api.max_tokens = j["api"].value("max_tokens", api.max_tokens);
            api.max_thinking_tokens = j["api"].value("max_thinking_tokens", api.max_thinking_tokens);
            api.temperature = j["api"].value("temperature", api.temperature);
        }

        // Agent settings
        if (j.contains("agent")) {
            agent.max_iterations = j["agent"].value("max_iterations", agent.max_iterations);
            agent.enable_thinking = j["agent"].value("enable_thinking", agent.enable_thinking);
            agent.enable_interleaved_thinking = j["agent"].value("enable_interleaved_thinking", agent.enable_interleaved_thinking);
            agent.enable_deep_analysis = j["agent"].value("enable_deep_analysis", agent.enable_deep_analysis);
            agent.verbose_logging = j["agent"].value("verbose_logging", agent.verbose_logging);
        }

        // UI settings
        if (j.contains("ui")) {
            ui.log_buffer_size = j["ui"].value("log_buffer_size", ui.log_buffer_size);
            ui.auto_scroll = j["ui"].value("auto_scroll", ui.auto_scroll);
            ui.theme = j["ui"].value("theme", ui.theme);
            ui.font_size = j["ui"].value("font_size", ui.font_size);
            ui.show_timestamps = j["ui"].value("show_timestamps", ui.show_timestamps);
            ui.show_tool_details = j["ui"].value("show_tool_details", ui.show_tool_details);
            
            // Window management
            ui.show_tray_icon = j["ui"].value("show_tray_icon", ui.show_tray_icon);
            ui.minimize_to_tray = j["ui"].value("minimize_to_tray", ui.minimize_to_tray);
            ui.close_to_tray = j["ui"].value("close_to_tray", ui.close_to_tray);
            ui.start_minimized = j["ui"].value("start_minimized", ui.start_minimized);
            ui.remember_window_state = j["ui"].value("remember_window_state", ui.remember_window_state);
            ui.auto_save_layout = j["ui"].value("auto_save_layout", ui.auto_save_layout);
            
            // Conversation view
            ui.auto_save_conversations = j["ui"].value("auto_save_conversations", ui.auto_save_conversations);
            ui.auto_save_interval = j["ui"].value("auto_save_interval", ui.auto_save_interval);
            ui.compact_mode = j["ui"].value("compact_mode", ui.compact_mode);
            
            // Inspector
            ui.inspector_follow_cursor = j["ui"].value("inspector_follow_cursor", ui.inspector_follow_cursor);
            ui.inspector_opacity = j["ui"].value("inspector_opacity", ui.inspector_opacity);
            ui.inspector_auto_hide = j["ui"].value("inspector_auto_hide", ui.inspector_auto_hide);
            ui.inspector_auto_hide_delay = j["ui"].value("inspector_auto_hide_delay", ui.inspector_auto_hide_delay);
        }

        // Export settings
        if (j.contains("export")) {
            export_settings.path = j["export"].value("path", export_settings.path);
        }

        // Debug mode
        debug_mode = j.value("debug_mode", debug_mode);

        return true;
    } catch (const std::exception& e) {
        msg("LLM RE: Error loading config: %s\n", e.what());
        return false;
    }
}

} // namespace llm_re