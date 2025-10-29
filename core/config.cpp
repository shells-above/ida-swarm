#include "config.h"
#include "logger.h"
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
        j["agent"]["enable_thinking"] = agent.enable_thinking;
        j["agent"]["enable_interleaved_thinking"] = agent.enable_interleaved_thinking;
        j["agent"]["enable_deep_analysis"] = agent.enable_deep_analysis;
        j["agent"]["enable_python_tool"] = agent.enable_python_tool;
        j["agent"]["context_limit"] = agent.context_limit;

        // Grader settings
        j["grader"]["enabled"] = grader.enabled;
        j["grader"]["model"] = claude::model_to_string(grader.model);
        j["grader"]["max_tokens"] = grader.max_tokens;
        j["grader"]["max_thinking_tokens"] = grader.max_thinking_tokens;

        // IRC settings (now at top level)
        j["irc"]["server"] = irc.server;

        // Orchestrator settings
        j["orchestrator"]["model"]["model"] = claude::model_to_string(orchestrator.model.model);
        j["orchestrator"]["model"]["max_tokens"] = orchestrator.model.max_tokens;
        j["orchestrator"]["model"]["max_thinking_tokens"] = orchestrator.model.max_thinking_tokens;
        j["orchestrator"]["model"]["temperature"] = orchestrator.model.temperature;
        j["orchestrator"]["model"]["enable_thinking"] = orchestrator.model.enable_thinking;

        // Profiling settings
        j["profiling"]["enabled"] = profiling.enabled;

        // Swarm settings
        j["swarm"]["max_parallel_auto_decompile_agents"] = swarm.max_parallel_auto_decompile_agents;

        // Function prioritization heuristics
        j["swarm"]["enable_api_call_heuristic"] = swarm.enable_api_call_heuristic;
        j["swarm"]["api_call_weight"] = swarm.api_call_weight;

        j["swarm"]["enable_caller_count_heuristic"] = swarm.enable_caller_count_heuristic;
        j["swarm"]["caller_count_weight"] = swarm.caller_count_weight;

        j["swarm"]["enable_string_heavy_heuristic"] = swarm.enable_string_heavy_heuristic;
        j["swarm"]["string_heavy_weight"] = swarm.string_heavy_weight;
        j["swarm"]["min_string_length_for_priority"] = swarm.min_string_length_for_priority;

        j["swarm"]["enable_function_size_heuristic"] = swarm.enable_function_size_heuristic;
        j["swarm"]["function_size_weight"] = swarm.function_size_weight;

        j["swarm"]["enable_internal_callee_heuristic"] = swarm.enable_internal_callee_heuristic;
        j["swarm"]["internal_callee_weight"] = swarm.internal_callee_weight;

        j["swarm"]["enable_entry_point_heuristic"] = swarm.enable_entry_point_heuristic;
        j["swarm"]["entry_point_weight"] = swarm.entry_point_weight;
        j["swarm"]["entry_point_mode"] = static_cast<int>(swarm.entry_point_mode);

        // Write to file
        std::ofstream file(path);
        if (!file) return false;

        file << j.dump(4);
        return true;
    } catch (const std::exception& e) {
        LOG("LLM RE: Error saving config: %s\n", e.what());
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
            agent.enable_thinking = j["agent"].value("enable_thinking", agent.enable_thinking);
            agent.enable_interleaved_thinking = j["agent"].value("enable_interleaved_thinking", agent.enable_interleaved_thinking);
            agent.enable_deep_analysis = j["agent"].value("enable_deep_analysis", agent.enable_deep_analysis);
            agent.enable_python_tool = j["agent"].value("enable_python_tool", agent.enable_python_tool);
            agent.context_limit = j["agent"].value("context_limit", agent.context_limit);
        }

        // Grader settings
        if (j.contains("grader")) {
            grader.enabled = j["grader"].value("enabled", grader.enabled);
            if (j["grader"].contains("model")) {
                grader.model = claude::model_from_string(j["grader"]["model"]);
            }
            grader.max_tokens = j["grader"].value("max_tokens", grader.max_tokens);
            grader.max_thinking_tokens = j["grader"].value("max_thinking_tokens", grader.max_thinking_tokens);
        }

        // IRC settings
        if (j.contains("irc")) {
            irc.server = j["irc"].value("server", irc.server);
        }

        // Orchestrator settings
        if (j.contains("orchestrator")) {

            if (j["orchestrator"].contains("model")) {
                if (j["orchestrator"]["model"].contains("model")) {
                    orchestrator.model.model = claude::model_from_string(j["orchestrator"]["model"]["model"]);
                }
                orchestrator.model.max_tokens = j["orchestrator"]["model"].value("max_tokens", orchestrator.model.max_tokens);
                orchestrator.model.max_thinking_tokens = j["orchestrator"]["model"].value("max_thinking_tokens", orchestrator.model.max_thinking_tokens);
                orchestrator.model.temperature = j["orchestrator"]["model"].value("temperature", orchestrator.model.temperature);
                orchestrator.model.enable_thinking = j["orchestrator"]["model"].value("enable_thinking", orchestrator.model.enable_thinking);
            }
        }

        // Profiling settings
        if (j.contains("profiling")) {
            profiling.enabled = j["profiling"].value("enabled", profiling.enabled);
        }

        // Swarm settings
        if (j.contains("swarm")) {
            swarm.max_parallel_auto_decompile_agents = j["swarm"].value("max_parallel_auto_decompile_agents", swarm.max_parallel_auto_decompile_agents);

            // Function prioritization heuristics
            swarm.enable_api_call_heuristic = j["swarm"].value("enable_api_call_heuristic", swarm.enable_api_call_heuristic);
            swarm.api_call_weight = j["swarm"].value("api_call_weight", swarm.api_call_weight);

            swarm.enable_caller_count_heuristic = j["swarm"].value("enable_caller_count_heuristic", swarm.enable_caller_count_heuristic);
            swarm.caller_count_weight = j["swarm"].value("caller_count_weight", swarm.caller_count_weight);

            swarm.enable_string_heavy_heuristic = j["swarm"].value("enable_string_heavy_heuristic", swarm.enable_string_heavy_heuristic);
            swarm.string_heavy_weight = j["swarm"].value("string_heavy_weight", swarm.string_heavy_weight);
            swarm.min_string_length_for_priority = j["swarm"].value("min_string_length_for_priority", swarm.min_string_length_for_priority);

            swarm.enable_function_size_heuristic = j["swarm"].value("enable_function_size_heuristic", swarm.enable_function_size_heuristic);
            swarm.function_size_weight = j["swarm"].value("function_size_weight", swarm.function_size_weight);

            swarm.enable_internal_callee_heuristic = j["swarm"].value("enable_internal_callee_heuristic", swarm.enable_internal_callee_heuristic);
            swarm.internal_callee_weight = j["swarm"].value("internal_callee_weight", swarm.internal_callee_weight);

            swarm.enable_entry_point_heuristic = j["swarm"].value("enable_entry_point_heuristic", swarm.enable_entry_point_heuristic);
            swarm.entry_point_weight = j["swarm"].value("entry_point_weight", swarm.entry_point_weight);

            int mode_int = j["swarm"].value("entry_point_mode", static_cast<int>(swarm.entry_point_mode));
            swarm.entry_point_mode = static_cast<Config::SwarmSettings::EntryPointMode>(mode_int);
        }

        return true;
    } catch (const std::exception& e) {
        LOG("LLM RE: Error loading config: %s\n", e.what());
        return false;
    }
}

void Config::load() {
    // Get default config path
    char config_path[QMAXPATH];
    qstrncpy(config_path, get_user_idadir(), sizeof(config_path));
    qstrncat(config_path, "/llm_re_config.json", sizeof(config_path));
    
    // Try to load from file, ignore if it doesn't exist (will use defaults)
    if (load_from_file(config_path)) {
        LOG("LLM RE: Configuration loaded from: %s\n", config_path);
    } else {
        LOG("LLM RE: Using default configuration (no config file found or load failed)\n");
    }
}

void Config::save() const {
    // Get default config path
    char config_path[QMAXPATH];
    qstrncpy(config_path, get_user_idadir(), sizeof(config_path));
    qstrncat(config_path, "/llm_re_config.json", sizeof(config_path));
    
    if (!save_to_file(config_path)) {
        LOG("LLM RE: ERROR - Failed to save configuration to: %s\n", config_path);
    }
}

void Config::reset() {
    // Reset to a freshly constructed Config
    *this = Config();
}

} // namespace llm_re