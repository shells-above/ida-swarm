#pragma once

#include "../agent/tool_system.h"
#include "swarm_agent.h"
#include <chrono>
#include <random>

namespace llm_re::agent {

// Forward declaration
class SwarmAgent;

// Helper function to generate unique request IDs
inline std::string generate_lldb_request_id(const std::string& agent_id) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 999999);

    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    return std::format("{}_{}_{}",  agent_id, timestamp, dis(gen));
}

// Tool for starting an LLDB remote debugging session
class StartLLDBSessionTool : public claude::tools::Tool {
public:
    explicit StartLLDBSessionTool(SwarmAgent* agent) : swarm_agent_(agent) {}

    std::string name() const override {
        return "start_lldb_session";
    }

    std::string description() const override {
        return R"(Start a remote LLDB debugging session on a jailbroken iOS device.

Debugging is EXPENSIVE AND VERY SLOW! Only use debugging if you ABSOLUTELY can't avoid it.

EXTREMELY IMPORTANT:
You may be reverse engineering DANGEROUS FILES! (malware).
There are ABSOLUTELY ZERO PROTECTIONS PUT IN PLACE.
This debugging is performed on BARE METAL! There is NO virtualization.
You **ABSOLUTELY MUST** do your due diligence and MAKE 100% SURE THIS BINARY IS CLEAN BEFORE TRYING TO DEBUG.
The safety of this system LIES IN YOUR HANDS ONLY.

IMPORTANT QUEUEING BEHAVIOR:
Only ONE agent can debug at a time per device. If all devices are busy, you will be placed in a FIFO queue.
The timeout parameter controls how long you're willing to wait in the queue. If the timeout expires before it's your turn, the tool will fail.

RETURNS:
- session_id: Use this ID in all subsequent debugging commands
- lldb_cheatsheet: Quick reference for LLDB commands

The process will be stopped at entry point. Use send_lldb_command to interact with LLDB as if you were using a terminal.)";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_integer("timeout_seconds", "Maximum time in seconds to wait for your turn in the debug queue (default: 300 seconds / 5 minutes)", false)
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            if (!swarm_agent_) {
                return claude::tools::ToolResult::failure("Not in swarm agent mode");
            }

            // Get timeout from input, default to 5 minutes (300 seconds)
            int timeout_seconds = input.value("timeout_seconds", 300);
            int timeout_ms = timeout_seconds * 1000;

            std::string agent_id = swarm_agent_->get_agent_id();
            std::string request_id = generate_lldb_request_id(agent_id);

            // Send IRC message to orchestrator on #lldb_control channel
            // Format: LLDB_START_SESSION|request_id|agent_id|timeout_ms
            std::string message = std::format("LLDB_START_SESSION|{}|{}|{}", request_id, agent_id, timeout_ms);
            swarm_agent_->send_irc_message("#lldb_control", message);

            LOG_INFO("LLDB Tool: Sent start session request (request_id=%s, timeout=%ds)\n",
                     request_id.c_str(), timeout_seconds);

            // Wait for response (may block for a long time if queue is not empty)
            json response = swarm_agent_->wait_for_lldb_response(request_id, timeout_ms);

            if (response["status"] == "error") {
                return claude::tools::ToolResult::failure(response["error"]);
            }

            return claude::tools::ToolResult::success(response);

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(
                std::format("Failed to start LLDB session: {}", e.what())
            );
        }
    }

private:
    SwarmAgent* swarm_agent_ = nullptr;
};

// Tool for sending commands to an active LLDB session
class SendLLDBCommandTool : public claude::tools::Tool {
public:
    explicit SendLLDBCommandTool(SwarmAgent* agent) : swarm_agent_(agent) {}

    std::string name() const override {
        return "send_lldb_command";
    }

    std::string description() const override {
        return R"(Send a raw LLDB command to your active debugging session.

CRITICAL: LLDB works with runtime addresses, *not* IDA static addresses!
*Always use convert_ida_address* tool to translate IDA addresses to runtime addresses before setting breakpoints or examining memory. The system will NOT handle this for you. You MUST do it yourself.)";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("session_id", "Session ID returned from start_lldb_session")
            .add_string("command", "Raw LLDB command to execute")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            if (!swarm_agent_) {
                return claude::tools::ToolResult::failure("Not in swarm agent mode");
            }

            std::string session_id = input.at("session_id");
            std::string command = input.at("command");
            std::string agent_id = swarm_agent_->get_agent_id();
            std::string request_id = generate_lldb_request_id(agent_id);

            // Send IRC message to orchestrator
            // Format: LLDB_SEND_COMMAND|request_id|session_id|agent_id|command
            std::string message = std::format("LLDB_SEND_COMMAND|{}|{}|{}|{}", request_id, session_id, agent_id, command);
            swarm_agent_->send_irc_message("#lldb_control", message);

            LOG_INFO("LLDB Tool: Sent command request (request_id=%s, session=%s, cmd=%s)\n", request_id.c_str(), session_id.c_str(), command.c_str());

            // Wait for response (60 second timeout)
            json response = swarm_agent_->wait_for_lldb_response(request_id, 60000);

            if (response["status"] == "error") {
                return claude::tools::ToolResult::failure(response["error"]);
            }

            return claude::tools::ToolResult::success(response);

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(
                std::format("Failed to send LLDB command: {}", e.what())
            );
        }
    }

private:
    SwarmAgent* swarm_agent_ = nullptr;
};

// Tool for converting IDA virtual addresses to runtime addresses
class ConvertIDAAddressTool : public claude::tools::Tool {
public:
    explicit ConvertIDAAddressTool(SwarmAgent* agent) : swarm_agent_(agent) {}

    std::string name() const override {
        return "convert_ida_address";
    }

    std::string description() const override {
        return R"(Convert an IDA virtual address to its runtime memory address for use in LLDB.

WHY THIS IS CRITICAL:
IDA Pro works with VIRTUAL addresses from the binary file. At runtime, the operating system loads the binary at a different base address due to ASLR and PIE. LLDB works with RUNTIME addresses.

WHAT THIS TOOL DOES:
1. Queries LLDB for the runtime base address via `image list`
2. Gets IDA's static base address
3. Calculates: runtime_address = (ida_address - ida_base) + runtime_base

WHEN TO USE:
- ALWAYS call this before setting breakpoints in LLDB
- ALWAYS call this before examining memory at an IDA address
- ALWAYS call this before any LLDB command that references a specific address

EXAMPLE WORKFLOW:
1. You identify an interesting function in IDA at address 0x100001234
2. Call: convert_ida_address(session_id="...", ida_address=0x100001234)
3. Get back: runtime_address=0x10abcd234
4. Set breakpoint: send_lldb_command(session_id="...", command="br set -a 0x10abcd234"))";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("session_id", "Session ID returned from start_lldb_session")
            .add_integer("ida_address", "IDA virtual address to convert (e.g., 0x100001234)")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            if (!swarm_agent_) {
                return claude::tools::ToolResult::failure("Not in swarm agent mode");
            }

            std::string session_id = input.at("session_id");
            uint64_t ida_address = input.at("ida_address");
            std::string agent_id = swarm_agent_->get_agent_id();
            std::string request_id = generate_lldb_request_id(agent_id);

            // Send IRC message to orchestrator
            // Format: LLDB_CONVERT_ADDRESS|request_id|session_id|agent_id|ida_address
            std::string message = std::format("LLDB_CONVERT_ADDRESS|{}|{}|{}|{}", request_id, session_id, agent_id, ida_address);
            swarm_agent_->send_irc_message("#lldb_control", message);

            LOG_INFO("LLDB Tool: Sent address conversion request (request_id=%s, session=%s, ida_addr=0x%llx)\n", request_id.c_str(), session_id.c_str(), (unsigned long long)ida_address);

            // Wait for response (30 second timeout)
            json response = swarm_agent_->wait_for_lldb_response(request_id, 30000);

            if (response["status"] == "error") {
                return claude::tools::ToolResult::failure(response["error"]);
            }

            return claude::tools::ToolResult::success(response);

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(
                std::format("Failed to convert address: {}", e.what())
            );
        }
    }

private:
    SwarmAgent* swarm_agent_ = nullptr;
};

// Tool for stopping an LLDB session
class StopLLDBSessionTool : public claude::tools::Tool {
public:
    explicit StopLLDBSessionTool(SwarmAgent* agent) : swarm_agent_(agent) {}

    std::string name() const override {
        return "stop_lldb_session";
    }

    std::string description() const override {
        return R"(Stop your active LLDB debugging session.

WHAT THIS DOES:
1. Sends 'quit' command to LLDB
2. Terminates the LLDB process
3. Terminates the debugged process on the remote device
4. Releases your position in the debug queue
5. Allows the next waiting agent to begin debugging

WHEN TO USE:
- When you've finished debugging and collected the information you need
- When you want to release the debug session for other agents
- Before the agent terminates (to clean up resources)

IMPORTANT:
Always call this when done debugging! If you don't, other agents waiting in the queue will be blocked until your agent terminates.

RETURNS:
- success: true if session stopped successfully)";
    }

    json parameters_schema() const override {
        return claude::tools::ParameterBuilder()
            .add_string("session_id", "Session ID to stop")
            .build();
    }

    claude::tools::ToolResult execute(const json& input) override {
        try {
            if (!swarm_agent_) {
                return claude::tools::ToolResult::failure("Not in swarm agent mode");
            }

            std::string session_id = input.at("session_id");
            std::string agent_id = swarm_agent_->get_agent_id();
            std::string request_id = generate_lldb_request_id(agent_id);

            // Send IRC message to orchestrator
            // Format: LLDB_STOP_SESSION|request_id|session_id|agent_id
            std::string message = std::format("LLDB_STOP_SESSION|{}|{}|{}", request_id, session_id, agent_id);
            swarm_agent_->send_irc_message("#lldb_control", message);

            LOG_INFO("LLDB Tool: Sent stop session request (request_id=%s, session=%s)\n", request_id.c_str(), session_id.c_str());

            // Wait for response (30 second timeout)
            json response = swarm_agent_->wait_for_lldb_response(request_id, 30000);

            if (response["status"] == "error") {
                return claude::tools::ToolResult::failure(response["error"]);
            }

            return claude::tools::ToolResult::success(response);

        } catch (const std::exception& e) {
            return claude::tools::ToolResult::failure(
                std::format("Failed to stop LLDB session: {}", e.what())
            );
        }
    }

private:
    SwarmAgent* swarm_agent_ = nullptr;
};

// Register LLDB tools for SwarmAgent
inline void register_lldb_tools(claude::tools::ToolRegistry& registry, SwarmAgent* swarm_agent) {
    registry.register_tool_type<StartLLDBSessionTool>(swarm_agent);
    registry.register_tool_type<SendLLDBCommandTool>(swarm_agent);
    registry.register_tool_type<ConvertIDAAddressTool>(swarm_agent);
    registry.register_tool_type<StopLLDBSessionTool>(swarm_agent);
}

} // namespace llm_re::agent
