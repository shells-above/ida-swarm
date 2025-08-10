#include "config.h"
#include <ida.hpp>
#include <kernwin.hpp>

namespace llm_re {

bool Config::save_to_file(const std::string& path) const {
    try {
        nlohmann::ordered_json j;

        // API settings
        j["api"]["auth_method"] = (api.auth_method == claude::AuthMethod::API_KEY) ? "api_key" : "oauth";
        j["api"]["api_key"] = api.api_key;
        j["api"]["use_oauth"] = api.use_oauth;
        j["api"]["oauth_config_dir"] = api.oauth_config_dir;
        j["api"]["base_url"] = api.base_url;

        // Agent settings
        j["agent"]["model"] = claude::model_to_string(agent.model);
        j["agent"]["max_tokens"] = agent.max_tokens;
        j["agent"]["max_thinking_tokens"] = agent.max_thinking_tokens;
        j["agent"]["temperature"] = agent.temperature;
        j["agent"]["max_iterations"] = agent.max_iterations;
        j["agent"]["enable_thinking"] = agent.enable_thinking;
        j["agent"]["enable_interleaved_thinking"] = agent.enable_interleaved_thinking;
        j["agent"]["enable_deep_analysis"] = agent.enable_deep_analysis;

        // Grader settings
        j["grader"]["model"] = claude::model_to_string(grader.model);
        j["grader"]["max_tokens"] = grader.max_tokens;
        j["grader"]["max_thinking_tokens"] = grader.max_thinking_tokens;

        // UI settings
        j["ui"]["log_buffer_size"] = ui.log_buffer_size;
        j["ui"]["auto_scroll"] = ui.auto_scroll;
        j["ui"]["theme_name"] = ui.theme_name;
        j["ui"]["font_size"] = ui.font_size;
        j["ui"]["show_timestamps"] = ui.show_timestamps;
        j["ui"]["show_tool_details"] = ui.show_tool_details;
        
        // Window management
        j["ui"]["start_minimized"] = ui.start_minimized;
        j["ui"]["remember_window_state"] = ui.remember_window_state;
        
        // Conversation view
        j["ui"]["auto_save_conversations"] = ui.auto_save_conversations;
        j["ui"]["auto_save_interval"] = ui.auto_save_interval;
        j["ui"]["density_mode"] = ui.density_mode;

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
            // Authentication settings
            if (j["api"].contains("auth_method")) {
                std::string method = j["api"]["auth_method"];
                api.auth_method = (method == "oauth") ? claude::AuthMethod::OAUTH : claude::AuthMethod::API_KEY;
            }
            api.api_key = j["api"].value("api_key", api.api_key);
            api.use_oauth = j["api"].value("use_oauth", api.use_oauth);
            api.oauth_config_dir = j["api"].value("oauth_config_dir", api.oauth_config_dir);
            
            // Connection settings
            api.base_url = j["api"].value("base_url", api.base_url);
        }

        // Agent settings
        if (j.contains("agent")) {
            if (j["agent"].contains("model")) {
                agent.model = claude::model_from_string(j["agent"]["model"]);
            }
            agent.max_tokens = j["agent"].value("max_tokens", agent.max_tokens);
            agent.max_thinking_tokens = j["agent"].value("max_thinking_tokens", agent.max_thinking_tokens);
            agent.temperature = j["agent"].value("temperature", agent.temperature);
            agent.max_iterations = j["agent"].value("max_iterations", agent.max_iterations);
            agent.enable_thinking = j["agent"].value("enable_thinking", agent.enable_thinking);
            agent.enable_interleaved_thinking = j["agent"].value("enable_interleaved_thinking", agent.enable_interleaved_thinking);
            agent.enable_deep_analysis = j["agent"].value("enable_deep_analysis", agent.enable_deep_analysis);
        }

        // Grader settings
        if (j.contains("grader")) {
            if (j["grader"].contains("model")) {
                grader.model = claude::model_from_string(j["grader"]["model"]);
            }
            grader.max_tokens = j["grader"].value("max_tokens", grader.max_tokens);
            grader.max_thinking_tokens = j["grader"].value("max_thinking_tokens", grader.max_thinking_tokens);
        }

        // UI settings
        if (j.contains("ui")) {
            ui.log_buffer_size = j["ui"].value("log_buffer_size", ui.log_buffer_size);
            ui.auto_scroll = j["ui"].value("auto_scroll", ui.auto_scroll);
            ui.theme_name = j["ui"].value("theme_name", ui.theme_name);
            ui.font_size = j["ui"].value("font_size", ui.font_size);
            ui.show_timestamps = j["ui"].value("show_timestamps", ui.show_timestamps);
            ui.show_tool_details = j["ui"].value("show_tool_details", ui.show_tool_details);
            
            // Window management
            ui.start_minimized = j["ui"].value("start_minimized", ui.start_minimized);
            ui.remember_window_state = j["ui"].value("remember_window_state", ui.remember_window_state);
            
            // Conversation view
            ui.auto_save_conversations = j["ui"].value("auto_save_conversations", ui.auto_save_conversations);
            ui.auto_save_interval = j["ui"].value("auto_save_interval", ui.auto_save_interval);
            ui.density_mode = j["ui"].value("density_mode", ui.density_mode);
        }


        return true;
    } catch (const std::exception& e) {
        msg("LLM RE: Error loading config: %s\n", e.what());
        return false;
    }
}

void Config::save() const {
    // Get default config path
    char config_path[QMAXPATH];
    qstrncpy(config_path, get_user_idadir(), sizeof(config_path));
    qstrncat(config_path, "/llm_re_config.json", sizeof(config_path));
    
    if (!save_to_file(config_path)) {
        msg("LLM RE: ERROR - Failed to save configuration to: %s\n", config_path);
    }
}

void Config::reset() {
    // Reset to a freshly constructed Config
    *this = Config();
}

} // namespace llm_re