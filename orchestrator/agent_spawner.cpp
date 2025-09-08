#include "agent_spawner.h"
#include "orchestrator_logger.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <thread>
#include <chrono>

#ifdef __NT__
#include <windows.h>
#include <tlhelp32.h>
#else
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <spawn.h>
// On macOS, environ needs explicit declaration
extern char **environ;
#endif

namespace fs = std::filesystem;

namespace llm_re::orchestrator {

AgentSpawner::AgentSpawner(const Config& config, const std::string& binary_name) 
    : config_(config), binary_name_(binary_name) {
}

AgentSpawner::~AgentSpawner() {
    terminate_all_agents();
}

int AgentSpawner::spawn_agent(const std::string& agent_id,
                             const std::string& database_path,
                             const json& agent_config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Find IDA executable
    std::string ida_exe = find_ida_executable();
    if (ida_exe.empty()) {
        ORCH_LOG("AgentSpawner: Could not find IDA executable\n");
        return -1;
    }
    
    // Create config file for agent
    std::string config_path = create_agent_config_file(agent_id, agent_config);
    if (config_path.empty()) {
        ORCH_LOG("AgentSpawner: Failed to create agent config\n");
        return -1;
    }
    
    // Prepare launch arguments
    // The agent_id is determined from the workspace path structure
    std::vector<std::string> args = {
        database_path,
        "-A"  // Auto-analysis
    };
    
    // Launch the process
    int pid = launch_process(ida_exe, args);
    
    if (pid > 0) {
        active_processes_[pid] = agent_id;
        ORCH_LOG("AgentSpawner: Launched agent %s with PID %d\n", agent_id.c_str(), pid);
    } else {
        ORCH_LOG("AgentSpawner: Failed to launch agent %s\n", agent_id.c_str());
    }
    
    return pid;
}

int AgentSpawner::resurrect_agent(const std::string& agent_id,
                                 const std::string& database_path,
                                 const json& resurrection_config) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    ORCH_LOG("AgentSpawner: Resurrecting agent %s\n", agent_id.c_str());
    
    // Find IDA executable
    std::string ida_exe = find_ida_executable();
    if (ida_exe.empty()) {
        ORCH_LOG("AgentSpawner: Could not find IDA executable\n");
        return -1;
    }
    
    // Create resurrection marker
    fs::path workspace = fs::path(database_path).parent_path();
    fs::path resurrection_marker = workspace / ".resurrecting";
    try {
        std::ofstream marker(resurrection_marker);
        if (marker.is_open()) {
            marker << std::chrono::system_clock::now().time_since_epoch().count();
            marker.close();
        }
    } catch (const std::exception& e) {
        ORCH_LOG("AgentSpawner: Failed to create resurrection marker: %s\n", e.what());
    }
    
    // Save resurrection config to workspace
    fs::path resurrection_config_file = workspace / "resurrection_config.json";
    try {
        std::ofstream file(resurrection_config_file);
        if (file.is_open()) {
            file << resurrection_config.dump(2);
            file.close();
            ORCH_LOG("AgentSpawner: Saved resurrection config for %s\n", agent_id.c_str());
        }
    } catch (const std::exception& e) {
        ORCH_LOG("AgentSpawner: Failed to save resurrection config: %s\n", e.what());
    }
    
    // Prepare launch arguments
    std::vector<std::string> args = {
        database_path,
        "-A"  // Auto-analysis
    };
    
    // Launch the resurrected process
    int pid = launch_process(ida_exe, args);
    
    if (pid > 0) {
        active_processes_[pid] = agent_id;
        ORCH_LOG("AgentSpawner: Resurrected agent %s with PID %d\n", agent_id.c_str(), pid);
        
        // Wait for agent to clear the resurrection marker (signals it's ready)
        ORCH_LOG("AgentSpawner: Waiting for agent to signal ready...\n");
        const int max_wait_seconds = 30;
        const int poll_interval_ms = 500;
        int waited_ms = 0;
        
        while (fs::exists(resurrection_marker) && waited_ms < (max_wait_seconds * 1000)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
            waited_ms += poll_interval_ms;
        }
        
        if (fs::exists(resurrection_marker)) {
            // Agent didn't clear it in time, remove it ourselves
            ORCH_LOG("AgentSpawner: Agent didn't clear marker after %d seconds, removing manually\n", max_wait_seconds);
            try {
                fs::remove(resurrection_marker);
            } catch (const std::exception& e) {
                ORCH_LOG("AgentSpawner: Failed to remove resurrection marker: %s\n", e.what());
            }
        } else {
            ORCH_LOG("AgentSpawner: Agent signaled ready after %d ms\n", waited_ms);
        }
    } else {
        ORCH_LOG("AgentSpawner: Failed to resurrect agent %s\n", agent_id.c_str());
        
        // Clean up resurrection marker on failure
        try {
            fs::remove(resurrection_marker);
        } catch (...) {}
    }
    
    return pid;
}

bool AgentSpawner::is_agent_running(int pid) const {
#ifdef __NT__
    return is_windows_process_running(pid);
#else
    return is_unix_process_running(pid);
#endif
}

bool AgentSpawner::terminate_agent(int pid) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = active_processes_.find(pid);
    if (it == active_processes_.end()) {
        return false;
    }
    
#ifdef __NT__
    bool result = terminate_windows_process(pid);
#else
    bool result = terminate_unix_process(pid);
#endif
    
    if (result) {
        active_processes_.erase(it);
        ORCH_LOG("AgentSpawner: Terminated process %d\n", pid);
    }
    
    return result;
}

void AgentSpawner::terminate_all_agents() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (const auto& [pid, agent_id] : active_processes_) {
#ifdef __NT__
        terminate_windows_process(pid);
#else
        terminate_unix_process(pid);
#endif
        ORCH_LOG("AgentSpawner: Terminated agent %s (PID %d)\n", agent_id.c_str(), pid);
    }
    
    active_processes_.clear();
}

std::string AgentSpawner::find_ida_executable() const {
    // Try to find IDA executable
    std::vector<std::string> possible_paths;
    
#ifdef __MAC__
    possible_paths = {
        "/Applications/IDA Professional 9.0.app/Contents/MacOS/ida64",
        "/Applications/IDA Pro 9.0/ida64.app/Contents/MacOS/ida64",
        "/Applications/IDA Professional.app/Contents/MacOS/ida64",
        "/Applications/IDA Pro.app/Contents/MacOS/ida64"
    };
#elif defined(__LINUX__)
    possible_paths = {
        "/opt/idapro-9.0/ida64",
        "/opt/ida-9.0/ida64",
        "/usr/local/bin/ida64",
        std::string(getenv("HOME") ?: "") + "/ida-9.0/ida64"
    };
#elif defined(__NT__)
    possible_paths = {
        "C:\\Program Files\\IDA Professional 9.0\\ida64.exe",
        "C:\\Program Files\\IDA Pro 9.0\\ida64.exe",
        "C:\\Program Files (x86)\\IDA Professional 9.0\\ida64.exe",
        "C:\\ida-9.0\\ida64.exe"
    };
#endif
    
    // Check each path
    for (const auto& path : possible_paths) {
        if (fs::exists(path)) {
            ORCH_LOG("AgentSpawner: Found IDA at %s\n", path.c_str());
            return path;
        }
    }
    
    // Try environment variable
    const char* ida_path = getenv("IDA_PATH");
    if (ida_path && fs::exists(ida_path)) {
        return ida_path;
    }
    
    return "";
}

std::string AgentSpawner::create_agent_config_file(const std::string& agent_id, const json& config) {
    fs::path config_dir = "/tmp/ida_swarm_workspace/" + binary_name_ + "/configs";
    fs::create_directories(config_dir);
    
    fs::path config_file = config_dir / (agent_id + "_config.json");
    
    try {
        std::ofstream file(config_file);
        if (file.is_open()) {
            file << config.dump(2);
            file.close();
            return config_file.string();
        }
    } catch (const std::exception& e) {
        ORCH_LOG("AgentSpawner: Failed to write config: %s\n", e.what());
    }
    
    return "";
}

int AgentSpawner::launch_process(const std::string& command, const std::vector<std::string>& args) {
#ifdef __NT__
    return launch_windows_process(command, args);
#else
    return launch_unix_process(command, args);
#endif
}

#ifndef __NT__
int AgentSpawner::launch_unix_process(const std::string& command, const std::vector<std::string>& args) {
    // Build stable argv array - need to keep strings alive during posix_spawn
    std::vector<std::string> stable_args;
    stable_args.push_back(command);  // argv[0] is the program name
    stable_args.insert(stable_args.end(), args.begin(), args.end());
    
    // Build argv array from stable strings
    std::vector<char*> argv;
    for (auto& arg : stable_args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);
    
    pid_t pid;
    int result = posix_spawn(&pid, command.c_str(), nullptr, nullptr, argv.data(), environ);
    
    if (result == 0) {
        return static_cast<int>(pid);
    } else {
        ORCH_LOG("AgentSpawner: posix_spawn failed: %d\n", result);
        return -1;
    }
}

bool AgentSpawner::terminate_unix_process(int pid) {
    // Send SIGTERM first
    if (kill(pid, SIGTERM) == 0) {
        // Give process time to clean up
        usleep(500000);  // 500ms
        
        // Check if still running
        if (kill(pid, 0) == 0) {
            // Still running, send SIGKILL
            kill(pid, SIGKILL);
        }
        return true;
    }
    return false;
}

bool AgentSpawner::is_unix_process_running(int pid) const {
    // Use qwait (IDA's thread-safe wrapper for waitpid) with QWNOHANG to check if process has exited
    // This also reaps zombie processes
    int status;
    // qwait parameters: status pointer, child PID, flags
    pid_t result = qwait(&status, pid, QWNOHANG);
    
    if (result == 0) {
        // Process is still running
        return true;
    } else if (result == pid) {
        // Process has exited and we've reaped it
        ORCH_LOG("AgentSpawner: Process %d has exited (status: %d)\n", pid, status);
        return false;
    } else {
        // Process doesn't exist or we can't wait for it (not our child)
        // Try kill(0) as fallback for processes we didn't spawn
        return kill(pid, 0) == 0;
    }
}
#endif

#ifdef __NT__
int AgentSpawner::launch_windows_process(const std::string& command, const std::vector<std::string>& args) {
    // Build command line
    std::stringstream cmdline;
    cmdline << "\"" << command << "\"";
    for (const auto& arg : args) {
        cmdline << " " << arg;
    }
    
    STARTUPINFOA si = {0};
    PROCESS_INFORMATION pi = {0};
    si.cb = sizeof(si);
    
    if (CreateProcessA(
        command.c_str(),
        const_cast<char*>(cmdline.str().c_str()),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_CONSOLE,
        nullptr,
        nullptr,
        &si,
        &pi
    )) {
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return static_cast<int>(pi.dwProcessId);
    }
    
    return -1;
}

bool AgentSpawner::terminate_windows_process(int pid) {
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
    if (hProcess) {
        BOOL result = TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
        return result != 0;
    }
    return false;
}

bool AgentSpawner::is_windows_process_running(int pid) const {
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (hProcess) {
        DWORD exitCode;
        BOOL result = GetExitCodeProcess(hProcess, &exitCode);
        CloseHandle(hProcess);
        return result && exitCode == STILL_ACTIVE;
    }
    return false;
}
#endif

} // namespace llm_re::orchestrator