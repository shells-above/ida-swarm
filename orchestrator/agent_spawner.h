#pragma once

#include "../core/common.h"
#include "../core/config.h"
#include <map>
#include <mutex>

namespace llm_re::orchestrator {

// Spawns and manages agent IDA processes
class AgentSpawner {
public:
    AgentSpawner(const Config& config, const std::string& binary_name);
    ~AgentSpawner();
    
    // Spawn a new agent process
    // Returns process ID on success, -1 on failure
    int spawn_agent(const std::string& agent_id, 
                   const std::string& database_path,
                   const json& agent_config);
    
    // Check if agent is still running
    bool is_agent_running(int pid) const;
    
    // Terminate an agent
    bool terminate_agent(int pid);
    
    // Terminate all agents
    void terminate_all_agents();
    
    // Get agent output/logs
    std::string get_agent_output(int pid) const;
    
private:
    const Config& config_;
    std::string binary_name_;
    std::map<int, std::string> active_processes_;  // pid -> agent_id
    mutable std::mutex mutex_;
    
    // Find IDA executable path
    std::string find_ida_executable() const;
    
    // Create agent config file
    std::string create_agent_config_file(const std::string& agent_id, const json& config);
    
    // Launch process
    int launch_process(const std::string& command, const std::vector<std::string>& args);
    
    // Platform-specific process management
#ifdef __NT__
    int launch_windows_process(const std::string& command, const std::vector<std::string>& args);
    bool terminate_windows_process(int pid);
    bool is_windows_process_running(int pid) const;
#else
    int launch_unix_process(const std::string& command, const std::vector<std::string>& args);
    bool terminate_unix_process(int pid);
    bool is_unix_process_running(int pid) const;
#endif
};

} // namespace llm_re::orchestrator