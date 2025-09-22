#include "../include/mcp_server.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <csignal>
#include <chrono>

namespace fs = std::filesystem;

namespace llm_re::mcp {

// Global server instance for signal handling
static MCPServer* g_server_instance = nullptr;

// Signal handler for graceful shutdown
static void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cerr << "\nReceived shutdown signal, cleaning up..." << std::endl;
        if (g_server_instance) {
            g_server_instance->shutdown();
        }
        exit(0);
    }
}

MCPServer::MCPServer() {
    g_server_instance = this;
}

MCPServer::~MCPServer() {
    shutdown();
    g_server_instance = nullptr;
}

bool MCPServer::initialize() {
    // Load configuration
    load_configuration();

    // Create session manager
    session_manager_ = std::make_unique<SessionManager>();

    // Create MCP stdio server
    mcp_server_ = std::make_unique<StdioMCPServer>("IDA Swarm MCP Server", "1.0.0");

    // Register tools
    register_tools();

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);


    std::cerr << "MCP Server initialized successfully" << std::endl;
    return true;
}

void MCPServer::start() {
    std::cerr << "Starting IDA Swarm MCP Server..." << std::endl;
    std::cerr << "Server ready. Waiting for MCP client connections..." << std::endl;

    // Start the MCP server (blocking)
    mcp_server_->start();
}

void MCPServer::shutdown() {
    if (should_shutdown_.exchange(true)) {
        return; // Already shutting down
    }

    std::cerr << "Shutting down MCP server..." << std::endl;


    // Close all sessions
    if (session_manager_) {
        session_manager_->close_all_sessions();
    }

    std::cerr << "MCP server shutdown complete" << std::endl;
}

void MCPServer::register_tools() {
    // Tool 1: Start Analysis Session
    nlohmann::json start_schema = {
        {"type", "object"},
        {"properties", {
            {"binary_path", {
                {"type", "string"},
                {"description", "Absolute path to a binary file or IDA database. Can be a raw executable "
                                "(EXE, ELF, Mach-O, DLL, etc.) that IDA will analyze, or an existing IDA "
                                "database file (.idb, .i64). When given a raw binary, IDA performs initial "
                                "auto-analysis to identify functions, data, and code flow. When given an "
                                "existing database, it uses the pre-analyzed information."}
            }},
            {"task", {
                {"type", "string"},
                {"description", "The initial task or question for the AI orchestrator. The orchestrator interprets this, "
                                "creates specialized agents, and coordinates their analysis of the binary. Agents have "
                                "access to the full IDA database including disassembly, decompilation, cross-references, "
                                "function calls, data structures, and can perform both static and semantic analysis. "
                                "The orchestrator determines what types of agents to spawn and how to coordinate them "
                                "based on understanding your request."}
            }}
        }},
        {"required", nlohmann::json::array({"binary_path", "task"})}
    };

    mcp_server_->register_tool(
        "start_analysis_session",
        "Launches IDA Pro with an AI orchestrator that creates and manages a swarm of specialized reverse engineering agents. "
        "The system works as follows: IDA Pro opens the specified database file, an orchestrator process starts and "
        "interprets your task, then spawns multiple AI agents that collaborate to analyze the binary. Each agent can "
        "read disassembly, understand control flow, examine data structures, trace execution paths, and reason about "
        "program behavior. Agents communicate findings to each other and can recursively spawn sub-agents for detailed "
        "analysis. The orchestrator maintains overall coherence and synthesizes agent findings. Returns a session_id "
        "that identifies this specific IDA instance and orchestrator for continued interaction. "
        "The orchestrator is INCREDIBLY CAPABLE! It is VERY GOOD AT REVERSE ENGINEERING (it can also write files, so if you need that you can tell it to write a file. "
        "Make sure to give the orchestrator TRULY what you are trying to do, and what  you need reversed and WHY you need it reversed. "
        "By giving the orchestrator the TRUE CONTEXT about what you are trying to do it can spawn agents smarter, and it WILL ANSWER YOUR TASK MUCH MUCH BETTER! "
        "This is why giving TRUE CONTEXT is so important, by giving the context about what is ACTUALLY happening the orchestrator WILL WORK BETTER FOR YOU!",
        start_schema,
        [this](const nlohmann::json& params) {
            return handle_start_analysis_session(params);
        }
    );

    // Tool 2: Send Message to Session
    nlohmann::json message_schema = {
        {"type", "object"},
        {"properties", {
            {"session_id", {
                {"type", "string"},
                {"description", "The session identifier that was returned by start_analysis_session. "
                                "This routes your message to a specific IDA Pro instance and its orchestrator."}
            }},
            {"message", {
                {"type", "string"},
                {"description", "A message for the orchestrator managing the agent swarm. The orchestrator maintains "
                                "context from all previous interactions in this session, understands the current state "
                                "of analysis, and knows what its agents have discovered. It will interpret your message, "
                                "determine what needs to be done, and coordinate its agents accordingly. Agents can "
                                "perform new analysis, refine previous findings, generate code, or provide explanations "
                                "based on their understanding of the binary."}
            }}
        }},
        {"required", nlohmann::json::array({"session_id", "message"})}
    };

    mcp_server_->register_tool(
        "send_message",
        "Sends a message to an active reverse engineering session's orchestrator. The orchestrator is already managing "
        "a swarm of AI agents that have been analyzing the binary since the session started. It maintains full context "
        "of what has been discovered, what agents are active, and what analysis has been performed. Your message is "
        "interpreted in this context. The orchestrator can direct existing agents, spawn new specialized agents, have "
        "agents collaborate on specific aspects, or synthesize findings from multiple agents. The agents have continuous "
        "access to the IDA database and can perform any analysis that IDA enables - reading assembly, following calls, "
        "understanding data structures, decompiling functions, and reasoning about program semantics.",
        message_schema,
        [this](const nlohmann::json& params) {
            return handle_send_message(params);
        }
    );

    // Tool 3: Close Session
    nlohmann::json close_schema = {
        {"type", "object"},
        {"properties", {
            {"session_id", {
                {"type", "string"},
                {"description", "The session identifier of an active analysis session to terminate."}
            }}
        }},
        {"required", nlohmann::json::array({"session_id"})}
    };

    mcp_server_->register_tool(
        "close_session",
        "Terminates an active reverse engineering session. This stops the orchestrator process, terminates all AI agents "
        "that were analyzing the binary, closes the IDA Pro application window, and cleans up associated resources. "
        "The session's agent swarm ceases to exist and all in-memory analysis state is lost. The IDA database file "
        "on disk remains unchanged unless agents explicitly saved modifications during the session.",
        close_schema,
        [this](const nlohmann::json& params) {
            return handle_close_session(params);
        }
    );

    std::cerr << "Registered 3 MCP tools: start_analysis_session, send_message, close_session" << std::endl;
}

nlohmann::json MCPServer::handle_start_analysis_session(const nlohmann::json& params) {
    try {
        // Extract parameters
        std::string binary_path = params.at("binary_path");
        std::string task = params.at("task");

        // Validate binary path
        if (!fs::exists(binary_path)) {
            return nlohmann::json{
                {"type", "text"},
                {"error", "Binary file not found: " + binary_path}
            };
        }

        // No restriction on file type - IDA can handle both raw binaries and databases
        // todo: user has to manually accept the auto analysis settings if making a new idb

        std::cerr << "Starting new analysis session for: " << binary_path << std::endl;
        std::cerr << "Task: " << task << std::endl;

        // Create session
        std::string session_id;
        try {
            session_id = session_manager_->create_session(binary_path, task);
        } catch (const std::exception& e) {
            return nlohmann::json{
                {"type", "text"},
                {"error", std::string("Failed to create session: ") + e.what()}
            };
        }

        std::cerr << "Created session: " << session_id << std::endl;

        // Wait for initial response from orchestrator
        nlohmann::json response = session_manager_->wait_for_initial_response(session_id);

        if (response.contains("error")) {
            // Clean up failed session
            session_manager_->close_session(session_id);
            return nlohmann::json{
                {"type", "text"},
                {"error", response["error"]}
            };
        }

        // Format successful response
        nlohmann::json result;
        result["type"] = "text";
        result["session_id"] = session_id;

        // Extract content from result object and include session_id
        if (response.contains("result") && response["result"].contains("content")) {
            result["text"] = "Session ID: " + session_id + "\n\n" +
                             response["result"]["content"].get<std::string>();
        } else {
            // No content in response likely means orchestrator failed to start properly
            session_manager_->close_session(session_id);
            return nlohmann::json{
                {"type", "text"},
                {"error", "Failed to get initial response from orchestrator. Session closed."}
            };
        }

        // Include additional info if available
        if (response.contains("result") && response["result"].contains("agents_spawned")) {
            result["agents_spawned"] = response["result"]["agents_spawned"];
        }

        // Add session info
        result["session_info"] = {
            {"session_id", session_id},
            {"binary_path", binary_path},
            {"status", "active"}
        };

        return result;

    } catch (const std::exception& e) {
        return nlohmann::json{
            {"type", "text"},
            {"error", std::string("Exception in start_analysis_session: ") + e.what()}
        };
    }
}

nlohmann::json MCPServer::handle_send_message(const nlohmann::json& params) {
    try {
        // Extract parameters
        std::string session_id = params.at("session_id");
        std::string message = params.at("message");

        std::cerr << "Sending message to session " << session_id << ": " << message << std::endl;

        // Send message to session
        nlohmann::json response = session_manager_->send_message(session_id, message);

        if (response.contains("error")) {
            return nlohmann::json{
                {"type", "text"},
                {"error", response["error"]}
            };
        }

        // Format successful response
        nlohmann::json result;
        result["type"] = "text";

        // Extract content from result object
        if (response.contains("result") && response["result"].contains("content")) {
            result["text"] = response["result"]["content"];
        } else if (response.contains("content")) {
            // Fallback for direct content field
            result["text"] = response["content"];
        } else {
            result["text"] = "Message sent to orchestrator";
        }

        // Include session status
        nlohmann::json status = session_manager_->get_session_status(session_id);
        if (status["exists"].get<bool>()) {
            result["session_status"] = {
                {"active", status["active"]},
                {"last_activity_seconds_ago", status["last_activity_seconds_ago"]}
            };
        }

        // Include agent info if available
        if (response.contains("result") && response["result"].contains("agents_active")) {
            result["agents_active"] = response["result"]["agents_active"];
        }

        return result;

    } catch (const std::exception& e) {
        return nlohmann::json{
            {"type", "text"},
            {"error", std::string("Exception in send_message: ") + e.what()}
        };
    }
}

nlohmann::json MCPServer::handle_close_session(const nlohmann::json& params) {
    try {
        // Extract session ID
        std::string session_id = params.at("session_id");

        std::cerr << "Closing session: " << session_id << std::endl;

        // Get session status before closing
        nlohmann::json status = session_manager_->get_session_status(session_id);

        if (!status["exists"].get<bool>()) {
            return nlohmann::json{
                {"type", "text"},
                {"error", "Session not found: " + session_id}
            };
        }

        // Close the session
        bool success = session_manager_->close_session(session_id);

        if (success) {
            std::cerr << "Session " << session_id << " closed successfully" << std::endl;

            return nlohmann::json{
                {"type", "text"},
                {"text", "Session closed successfully"},
                {"session_id", session_id},
                {"success", true}
            };
        } else {
            return nlohmann::json{
                {"type", "text"},
                {"error", "Failed to close session"},
                {"session_id", session_id},
                {"success", false}
            };
        }

    } catch (const std::exception& e) {
        return nlohmann::json{
            {"type", "text"},
            {"error", std::string("Exception in close_session: ") + e.what()}
        };
    }
}


void MCPServer::load_configuration() {
    // Try to load config from file
    fs::path config_dir = fs::path(getenv("HOME")) / ".ida_re_mcp";
    fs::path config_file = config_dir / "server_config.json";

    if (fs::exists(config_file)) {
        try {
            std::ifstream file(config_file);
            nlohmann::json j;
            file >> j;

            if (j.contains("max_sessions")) {
                config_.max_sessions = j["max_sessions"];
            }
            if (j.contains("ida_path")) {
                config_.ida_path = j["ida_path"];
            }
            if (j.contains("log_file")) {
                config_.log_file = j["log_file"];
            }
            if (j.contains("log_level")) {
                config_.log_level = j["log_level"];
            }

            std::cerr << "Loaded configuration from: " << config_file << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to load config file: " << e.what() << std::endl;
            std::cerr << "Using default configuration" << std::endl;
        }
    } else {
        // Create default config file
        if (!fs::exists(config_dir)) {
            fs::create_directories(config_dir);
        }

        nlohmann::json default_config = {
            {"max_sessions", config_.max_sessions},
            {"ida_path", config_.ida_path},
            {"log_file", config_.log_file},
            {"log_level", config_.log_level}
        };

        try {
            std::ofstream file(config_file);
            file << default_config.dump(4);
            std::cerr << "Created default configuration at: " << config_file << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Failed to create config file: " << e.what() << std::endl;
        }
    }

    // Display active configuration
    std::cerr << "MCP Server Configuration:" << std::endl;
    std::cerr << "  Max sessions: " << config_.max_sessions << std::endl;
    std::cerr << "  IDA path: " << config_.ida_path << std::endl;
}

} // namespace llm_re::mcp

// Main entry point
int main(int argc, char* argv[]) {
    std::cerr << "==============================================\n";
    std::cerr << "    IDA Swarm MCP Server v1.0.0\n";
    std::cerr << "==============================================\n\n";

    llm_re::mcp::MCPServer server;

    if (!server.initialize()) {
        std::cerr << "Failed to initialize MCP server" << std::endl;
        return 1;
    }

    // Start server (blocking)
    try {
        server.start();
    } catch (const std::exception& e) {
        std::cerr << "Server terminated: " << e.what() << std::endl;
    }

    return 0;
}