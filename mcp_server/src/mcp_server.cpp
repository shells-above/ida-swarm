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
        std::cerr << "\nReceived shutdown signal (" << (sig == SIGINT ? "SIGINT" : "SIGTERM") << "), cleaning up..." << std::endl;
        if (g_server_instance) {
            // SIGTERM typically means external process manager wants us to exit NOW
            // Use fast shutdown to comply with ~2 second timeout expectation
            if (sig == SIGTERM) {
                g_server_instance->fast_shutdown();
            } else {
                // SIGINT (Ctrl+C) can take longer
                g_server_instance->shutdown();
            }
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

    std::cerr << "Shutting down MCP server (graceful mode)..." << std::endl;

    // Close all sessions gracefully (waits up to 60s for IDA to save)
    if (session_manager_) {
        session_manager_->close_all_sessions();
    }

    std::cerr << "MCP server shutdown complete" << std::endl;
}

void MCPServer::fast_shutdown() {
    if (should_shutdown_.exchange(true)) {
        return; // Already shutting down
    }

    std::cerr << "Fast shutdown mode: force-killing all IDA processes..." << std::endl;

    // Force kill all sessions immediately (no graceful wait)
    if (session_manager_) {
        session_manager_->force_kill_all_sessions();
    }

    std::cerr << "Fast shutdown complete" << std::endl;
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
            }},
            {"run_in_background", {
                {"type", "boolean"},
                {"default", false},
                {"description", "Whether to return immediately without waiting for initial analysis results.\n\n"
                                "DEFAULT (false): BLOCKS until orchestrator completes initial analysis and returns results. "
                                "Use this for normal single-binary analysis workflows.\n\n"
                                "WHEN TO USE run_in_background=true:\n"
                                "- Analyzing MULTIPLE binaries in parallel (e.g., comparing 5 malware variants)\n"
                                "- You have OTHER INDEPENDENT work to do while waiting (documentation, other tasks)\n"
                                "- Starting long analysis and will check results later\n\n"
                                "WHEN NOT TO USE (keep default false):\n"
                                "- Analyzing a single binary and need results to continue\n"
                                "- Sequential workflow where next step depends on results\n"
                                "- User asked to analyze one binary and report findings\n\n"
                                "CRITICAL WARNING: Reverse engineering can take 10+ MINUTES or even HOURS for complex binaries. "
                                "Background mode exists for parallel workflows, not to make slow operations fast.\n\n"
                                "If run_in_background=true: Returns only session_id immediately. Use wait_for_response() "
                                "or get_session_messages() to retrieve results later."}
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
        "The orchestrator is INCREDIBLY CAPABLE! It is VERY GOOD AT REVERSE ENGINEERING (it can also write files, but this is IT. it can reverse engineer, patch binaries, and it can write files but it can NOT explore the file system in ANY way, so if you need that you can tell it to write a file. The orchestrator will ALWAYS write the file NEXT to the binary that you started it on.  "
        "Make sure to give the orchestrator TRULY what you are trying to do, and what  you need reversed and WHY you need it reversed. "
        "By giving the orchestrator the TRUE CONTEXT about what you are trying to do it can spawn agents smarter, and it WILL ANSWER YOUR TASK MUCH MUCH BETTER! "
        "This is why giving TRUE CONTEXT is so important, by giving the context about what is ACTUALLY happening the orchestrator WILL WORK BETTER FOR YOU! ",
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
            }},
            {"run_in_background", {
                {"type", "boolean"},
                {"default", false},
                {"description", "Whether to return immediately without waiting for orchestrator response.\n\n"
                                "DEFAULT (false): BLOCKS until orchestrator processes message and returns response.\n\n"
                                "CRITICAL CONSTRAINT: Only ONE pending message per session at a time. "
                                "If you send with run_in_background=true, you CANNOT send another message to that "
                                "session until the response is retrieved (via wait_for_response or get_session_messages).\n\n"
                                "Attempting to send while message pending will ERROR with message:\n"
                                "'Cannot send message: session is still processing previous message: <pending_message_text>'\n\n"
                                "WHEN TO USE run_in_background=true:\n"
                                "- Sending questions to MULTIPLE sessions simultaneously\n"
                                "- Starting complex analysis and doing other work while waiting\n\n"
                                "WHEN NOT TO USE (keep default false):\n"
                                "- Interactive conversation with orchestrator (ask -> answer -> ask -> answer)\n"
                                "- Single-session workflows\n"
                                "- When you need the answer before proceeding"}
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

    // Tool 4: Get Session Messages
    nlohmann::json get_messages_schema = {
        {"type", "object"},
        {"properties", {
            {"session_id", {
                {"type", "string"},
                {"description", "The session identifier to retrieve messages from."}
            }},
            {"max_messages", {
                {"type", "integer"},
                {"description", "Optional: Maximum number of messages to retrieve. If not specified, returns all pending messages."}
            }}
        }},
        {"required", nlohmann::json::array({"session_id"})}
    };

    mcp_server_->register_tool(
        "get_session_messages",
        "NON-BLOCKING check for accumulated orchestrator responses from background operations.\n\n"
        "BEHAVIOR:\n"
        "- Returns immediately (does NOT wait)\n"
        "- If responses available: Returns them and clears queue, session ready for next message\n"
        "- If no responses yet: Returns empty array, session still processing\n\n"
        "This is a 'poll and check' operation. Use wait_for_response() if you want to block.\n\n"
        "USAGE PATTERN:\n"
        "1. start_analysis_session(..., run_in_background=true) → session_id\n"
        "2. Do other work\n"
        "3. get_session_messages(session_id) → check if ready (may be empty)\n"
        "4. If empty: continue other work, check again later\n"
        "5. If has messages: process results, can now send_message again\n\n"
        "IMPORTANT: Each call retrieves and CLEARS messages. Don't call repeatedly in a loop "
        "or you'll just get empty results after the first call.",
        get_messages_schema,
        [this](const nlohmann::json& params) {
            return handle_get_session_messages(params);
        }
    );

    // Tool 5: Wait for Response
    nlohmann::json wait_response_schema = {
        {"type", "object"},
        {"properties", {
            {"session_id", {
                {"type", "string"},
                {"description", "The session identifier to wait for."}
            }},
            {"timeout_ms", {
                {"type", "integer"},
                {"description", "Optional timeout in milliseconds. If not specified, waits indefinitely. Note: Reverse engineering can take hours, so no timeout may be appropriate."}
            }}
        }},
        {"required", nlohmann::json::array({"session_id"})}
    };

    mcp_server_->register_tool(
        "wait_for_response",
        "BLOCKS until orchestrator response is available for the session.\n\n"
        "BEHAVIOR:\n"
        "- BLOCKS (may wait minutes/hours for complex analysis)\n"
        "- Returns response when available\n"
        "- Marks session ready for next message\n"
        "- If response already available: returns immediately\n\n"
        "CRITICAL USE CASE: Parallel analysis coordination\n\n"
        "EXAMPLE - Analyzing multiple binaries in parallel:\n"
        "  // Start 3 analyses running simultaneously\n"
        "  s1 = start_analysis_session(bin1, task, run_in_background=true)\n"
        "  s2 = start_analysis_session(bin2, task, run_in_background=true)\n"
        "  s3 = start_analysis_session(bin3, task, run_in_background=true)\n\n"
        "  // All 3 are now analyzing in parallel. Wait for all to complete:\n"
        "  r1 = wait_for_response(s1)  // Blocks until s1 finishes\n"
        "  r2 = wait_for_response(s2)  // Blocks until s2 finishes\n"
        "  r3 = wait_for_response(s3)  // Blocks until s3 finishes\n\n"
        "  // Now have all results, synthesize findings\n\n"
        "This is like Promise.all() or asyncio.gather() - enables true parallel execution.\n\n"
        "vs get_session_messages(): That tool returns immediately (non-blocking check). "
        "This tool waits until result ready (blocking wait).",
        wait_response_schema,
        [this](const nlohmann::json& params) {
            return handle_wait_for_response(params);
        }
    );

    std::cerr << "Registered 5 MCP tools: start_analysis_session, send_message, close_session, get_session_messages, wait_for_response" << std::endl;
}

nlohmann::json MCPServer::handle_start_analysis_session(const nlohmann::json& params) {
    try {
        // Extract parameters
        std::string binary_path = params.at("binary_path");
        std::string task = params.at("task");
        bool run_in_background = params.value("run_in_background", false);

        // Validate binary path
        if (!fs::exists(binary_path)) {
            return nlohmann::json{
                {"type", "text"},
                {"text", "Error: Binary file not found: " + binary_path},
                {"isError", true}
            };
        }

        // No restriction on file type - IDA can handle both raw binaries and databases
        // todo: user has to manually accept the auto analysis settings if making a new idb

        std::cerr << "Starting new analysis session for: " << binary_path << std::endl;
        std::cerr << "Task: " << task << std::endl;
        std::cerr << "Background mode: " << (run_in_background ? "true" : "false") << std::endl;

        // Create session
        std::string session_id;
        try {
            session_id = session_manager_->create_session(binary_path, task);
        } catch (const std::exception& e) {
            return nlohmann::json{
                {"type", "text"},
                {"text", std::string("Error: Failed to create session: ") + e.what()},
                {"isError", true}
            };
        }

        std::cerr << "Created session: " << session_id << std::endl;

        // If background mode, return immediately with just session_id
        if (run_in_background) {
            nlohmann::json result;
            result["type"] = "text";
            result["text"] = "Session started in background mode. Session ID: " + session_id + "\n\n"
                             "The orchestrator is now analyzing the binary. Use wait_for_response(\"" + session_id + "\") "
                             "to block until results are ready, or use get_session_messages(\"" + session_id + "\") "
                             "to poll for results without blocking.";
            result["session_id"] = session_id;
            result["background_mode"] = true;
            result["session_info"] = {
                {"session_id", session_id},
                {"binary_path", binary_path},
                {"status", "active"},
                {"background", true}
            };
            return result;
        }

        // Wait for initial response from orchestrator
        nlohmann::json response = session_manager_->wait_for_initial_response(session_id);

        std::cerr << "Got initial response from orchestrator: " << response.dump(2) << std::endl;

        if (response.contains("error")) {
            // Clean up failed session
            session_manager_->close_session(session_id);
            return nlohmann::json{
                {"type", "text"},
                {"text", response["error"].get<std::string>()},  // Changed from "error" to "text"
                {"isError", true}
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
            std::cerr << "Response doesn't contain result.content. Full response: " << response.dump(2) << std::endl;
            session_manager_->close_session(session_id);
            return nlohmann::json{
                {"type", "text"},
                {"text", "Failed to get initial response from orchestrator. Session closed."},  // Changed from "error" to "text"
                {"isError", true}
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
            {"text", std::string("Error: Exception in start_analysis_session: ") + e.what()},
            {"isError", true}
        };
    }
}

nlohmann::json MCPServer::handle_send_message(const nlohmann::json& params) {
    try {
        // Extract parameters
        std::string session_id = params.at("session_id");
        std::string message = params.at("message");
        bool run_in_background = params.value("run_in_background", false);

        std::cerr << "Sending message to session " << session_id << ": " << message << std::endl;
        std::cerr << "Background mode: " << (run_in_background ? "true" : "false") << std::endl;

        // Send message to session (pass wait_for_response parameter)
        nlohmann::json response = session_manager_->send_message(session_id, message, !run_in_background);

        if (response.contains("error")) {
            return nlohmann::json{
                {"type", "text"},
                {"text", "Error: " + response["error"].get<std::string>()},
                {"isError", true}
            };
        }

        // If background mode, return immediately after sending
        if (run_in_background && response.contains("success")) {
            nlohmann::json result;
            result["type"] = "text";
            result["text"] = "Message sent to orchestrator in background mode. Session ID: " + session_id + "\n\n"
                             "The orchestrator is processing your message. Use wait_for_response(\"" + session_id + "\") "
                             "to block until response is ready, or use get_session_messages(\"" + session_id + "\") "
                             "to poll for response without blocking.\n\n"
                             "Remember: You cannot send another message to this session until the response is retrieved.";
            result["session_id"] = session_id;
            result["background_mode"] = true;
            result["message_sent"] = message;
            return result;
        }

        // In blocking mode, response already contains the orchestrator's reply
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
            {"text", std::string("Error: Exception in send_message: ") + e.what()},
            {"isError", true}
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
                {"text", "Error: Session not found: " + session_id},
                {"isError", true}
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
                {"text", "Error: Failed to close session for " + session_id},
                {"isError", true}
            };
        }

    } catch (const std::exception& e) {
        return nlohmann::json{
            {"type", "text"},
            {"text", std::string("Error: Exception in close_session: ") + e.what()},
            {"isError", true}
        };
    }
}


nlohmann::json MCPServer::handle_get_session_messages(const nlohmann::json& params) {
    try {
        // Extract parameters
        std::string session_id = params.at("session_id");
        int max_messages = params.value("max_messages", -1);

        std::cerr << "Getting messages for session " << session_id << std::endl;

        // Get messages from session (non-blocking)
        nlohmann::json response = session_manager_->get_session_messages(session_id, max_messages);

        if (response.contains("error")) {
            return nlohmann::json{
                {"type", "text"},
                {"text", "Error: " + response["error"].get<std::string>()},
                {"isError", true}
            };
        }

        // Format response
        nlohmann::json result;
        result["type"] = "text";

        auto messages = response["messages"];
        bool has_pending = response.value("has_pending", false);

        if (messages.empty()) {
            result["text"] = "No messages available yet for session " + session_id + ".";
            if (has_pending) {
                result["text"] = result["text"].get<std::string>() +
                                "\n\nSession is still processing message: \"" +
                                response["pending_message"].get<std::string>() + "\"";
            }
        } else {
            // Format all retrieved messages
            std::string text = "Retrieved " + std::to_string(messages.size()) + " message(s) from session " + session_id + ":\n\n";
            for (size_t i = 0; i < messages.size(); i++) {
                text += "=== Message " + std::to_string(i + 1) + " ===\n";

                // Extract content from message
                if (messages[i].contains("result") && messages[i]["result"].contains("content")) {
                    text += messages[i]["result"]["content"].get<std::string>();
                } else if (messages[i].contains("content")) {
                    text += messages[i]["content"].get<std::string>();
                } else {
                    text += messages[i].dump();
                }
                text += "\n\n";
            }

            result["text"] = text;
        }

        result["message_count"] = messages.size();
        result["has_pending"] = has_pending;
        result["session_id"] = session_id;

        return result;

    } catch (const std::exception& e) {
        return nlohmann::json{
            {"type", "text"},
            {"text", std::string("Error: Exception in get_session_messages: ") + e.what()},
            {"isError", true}
        };
    }
}

nlohmann::json MCPServer::handle_wait_for_response(const nlohmann::json& params) {
    try {
        // Extract parameters
        std::string session_id = params.at("session_id");
        int timeout_ms = params.value("timeout_ms", -1);

        std::cerr << "Waiting for response from session " << session_id;
        if (timeout_ms > 0) {
            std::cerr << " (timeout: " << timeout_ms << "ms)";
        }
        std::cerr << std::endl;

        // Wait for response (blocking)
        nlohmann::json response = session_manager_->wait_for_response(session_id, timeout_ms);

        if (response.contains("error")) {
            return nlohmann::json{
                {"type", "text"},
                {"text", "Error: " + response["error"].get<std::string>()},
                {"isError", true}
            };
        }

        // Format response
        nlohmann::json result;
        result["type"] = "text";

        // Extract content from response
        if (response.contains("result") && response["result"].contains("content")) {
            result["text"] = response["result"]["content"];
        } else if (response.contains("content")) {
            result["text"] = response["content"];
        } else {
            result["text"] = response.dump();
        }

        result["session_id"] = session_id;

        // Include session status
        nlohmann::json status = session_manager_->get_session_status(session_id);
        if (status["exists"].get<bool>()) {
            result["session_status"] = {
                {"active", status["active"]},
                {"last_activity_seconds_ago", status["last_activity_seconds_ago"]}
            };
        }

        return result;

    } catch (const std::exception& e) {
        return nlohmann::json{
            {"type", "text"},
            {"text", std::string("Error: Exception in wait_for_response: ") + e.what()},
            {"isError", true}
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