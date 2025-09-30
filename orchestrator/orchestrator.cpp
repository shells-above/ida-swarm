#include "orchestrator.h"
#include "orchestrator_tools.h"
#include "orchestrator_logger.h"
#include "../agent/consensus_executor.h"
#include "../sdk/auth/oauth_manager.h"
#include <iostream>
#include <sstream>
#include <format>
#include <thread>
#include <chrono>
#include <set>
#include <filesystem>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <cstring>

namespace fs = std::filesystem;

namespace llm_re::orchestrator {

Orchestrator::Orchestrator(const Config& config, const std::string& main_db_path, bool show_ui)
    : config_(config), main_database_path_(main_db_path), show_ui_(show_ui) {
    
    // Extract binary name from IDB path
    fs::path idb_path(main_db_path);
    binary_name_ = idb_path.stem().string(); // Get filename without extension
    
    // Don't initialize logger here - will do it after workspace cleanup in initialize()
    // to avoid the log file being deleted
    
    // Create subsystems with binary name
    db_manager_ = std::make_unique<DatabaseManager>(main_db_path, binary_name_);
    agent_spawner_ = std::make_unique<AgentSpawner>(config, binary_name_);
    tool_tracker_ = std::make_unique<ToolCallTracker>(binary_name_, &event_bus_);
    merge_manager_ = std::make_unique<MergeManager>(tool_tracker_.get());
    nogo_zone_manager_ = std::make_unique<NoGoZoneManager>();
    
    // Create our own OAuth manager if using OAuth authentication
    if (config.api.auth_method == claude::AuthMethod::OAUTH) {
        oauth_manager_ = Config::create_oauth_manager(config.api.oauth_config_dir);
    }
    
    // Setup API client
    if (config.api.auth_method == claude::AuthMethod::OAUTH && oauth_manager_) {
        auto creds = oauth_manager_->get_credentials();
        if (creds) {
            api_client_ = std::make_unique<claude::Client>(creds, config.api.base_url);
        }
    }
    
    if (!api_client_) {
        api_client_ = std::make_unique<claude::Client>(config.api.api_key, config.api.base_url);
    }
    
    // Set log filename for orchestrator to include binary name
    std::string log_filename = std::format("anthropic_requests_{}_orchestrator.log", binary_name_);
    api_client_->set_request_log_filename(log_filename);
    
    // Register orchestrator tools
    register_orchestrator_tools(tool_registry_, this);
}

Orchestrator::~Orchestrator() {
    shutdown();
}

bool Orchestrator::initialize() {
    if (initialized_) return true;
    
    // Clean up any existing workspace directory from previous runs BEFORE initializing logger
    std::filesystem::path workspace_dir = std::filesystem::path("/tmp/ida_swarm_workspace") / binary_name_;
    if (std::filesystem::exists(workspace_dir)) {
        try {
            std::filesystem::remove_all(workspace_dir);
        } catch (const std::exception& e) {
            // Can't log yet, logger not initialized
        }
    }
    
    // NOW initialize logger after cleanup
    g_orch_logger.initialize(binary_name_);
    ORCH_LOG("Orchestrator: Initializing subsystems...\n");
    ORCH_LOG("Orchestrator: Workspace cleaned and logger initialized for binary: %s\n", binary_name_.c_str());
    
    // Ignore SIGPIPE to prevent crashes when IRC connections break
    signal(SIGPIPE, SIG_IGN);
    ORCH_LOG("Orchestrator: Configured SIGPIPE handler\n");
    
    // Initialize tool tracker database
    if (!tool_tracker_->initialize()) {
        ORCH_LOG("Orchestrator: Failed to initialize tool tracker\n");
        return false;
    }
    
    // Start monitoring for new tool calls
    tool_tracker_->start_monitoring();

    // Subscribe to tool call events for real-time processing
    event_bus_subscription_id_ = event_bus_.subscribe(
        [this](const AgentEvent& event) {
            handle_tool_call_event(event);
        },
        {AgentEvent::TOOL_CALL}
    );
    ORCH_LOG("Orchestrator: Subscribed to TOOL_CALL events for real-time processing\n");
    
    // Allocate unique port for IRC server based on binary name
    allocated_irc_port_ = allocate_unique_port();
    
    // Start IRC server for agent communication with binary name
    fs::path idb_path(main_database_path_);
    std::string binary_name = idb_path.stem().string();
    irc_server_ = std::make_unique<irc::IRCServer>(allocated_irc_port_, binary_name);
    if (!irc_server_->start()) {
        ORCH_LOG("Orchestrator: Failed to start IRC server on port %d\n", allocated_irc_port_);
        return false;
    }
    
    ORCH_LOG("Orchestrator: IRC server started on port %d (unique for %s)\n", allocated_irc_port_, binary_name_.c_str());
    
    // Connect IRC client for orchestrator communication
    irc_client_ = std::make_unique<irc::IRCClient>("orchestrator", config_.irc.server, allocated_irc_port_);
    if (!irc_client_->connect()) {
        ORCH_LOG("Orchestrator: Failed to connect IRC client to %s:%d\n", 
            config_.irc.server.c_str(), allocated_irc_port_);
        return false;
    }
    
    // Join standard orchestrator channels
    irc_client_->join_channel("#agents");
    irc_client_->join_channel("#results");
    irc_client_->join_channel("#status");      // For agent status updates
    irc_client_->join_channel("#discoveries"); // For agent discoveries
    
    // Set up message callback to receive agent results
    irc_client_->set_message_callback(
        [this](const std::string& channel, const std::string& sender, const std::string& message) {
            handle_irc_message(channel, sender, message);
        }
    );
    
    ORCH_LOG("Orchestrator: IRC client connected\n");

    // Start conflict channel monitoring thread
    ORCH_LOG("Orchestrator: Starting conflict channel monitor\n");
    conflict_monitor_thread_ = std::thread([this]() {
        while (!conflict_monitor_should_stop_ && !shutting_down_) {
            // Sleep first to give system time to initialize
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // Check for new conflict channels
            if (irc_server_ && irc_client_ && irc_client_->is_connected()) {
                auto channels = irc_server_->list_channels();

                for (const auto& channel : channels) {
                    if (channel.find("#conflict_") == 0) {
                        std::lock_guard<std::mutex> lock(conflicts_mutex_);

                        // Check if we're already monitoring this channel
                        if (active_conflicts_.find(channel) == active_conflicts_.end()) {
                            // New conflict channel discovered - join it!
                            irc_client_->join_channel(channel);

                            // Create session to track it
                            ConflictSession session;
                            session.channel = channel;
                            session.started = std::chrono::steady_clock::now();

                            // Parse channel name to get basic info (format: #conflict_addr_toolname)
                            std::string channel_copy = channel;
                            if (channel_copy.find("#conflict_") == 0) {
                                channel_copy = channel_copy.substr(10);  // Remove "#conflict_"
                                size_t addr_end = channel_copy.find('_');
                                if (addr_end != std::string::npos) {
                                    std::string addr_str = channel_copy.substr(0, addr_end);
                                    std::string tool_name = channel_copy.substr(addr_end + 1);

                                    // Store basic conflict info
                                    ToolConflict conflict;
                                    conflict.conflict_type = tool_name;
                                    conflict.first_call.tool_name = tool_name;
                                    try {
                                        conflict.first_call.address = std::stoull(addr_str, nullptr, 16);
                                    } catch (...) {
                                        conflict.first_call.address = 0;
                                    }
                                    conflict.second_call.tool_name = tool_name;
                                    conflict.second_call.address = conflict.first_call.address;

                                    session.original_conflict = conflict;
                                }
                            }

                            active_conflicts_[channel] = session;
                            ORCH_LOG("Orchestrator: Proactively joined conflict channel %s\n", channel.c_str());
                        }
                    }
                }
            }
        }
        ORCH_LOG("Orchestrator: Conflict channel monitor thread exiting\n");
    });

    // Initialize database manager
    if (!db_manager_->initialize()) {
        ORCH_LOG("Orchestrator: Failed to initialize database manager\n");
        return false;
    }
    
    initialized_ = true;
    ORCH_LOG("Orchestrator: Initialization complete\n");
    return true;
}

bool Orchestrator::initialize_mcp_mode(const std::string& session_id,
                                      const std::string& input_pipe_path,
                                      const std::string& output_pipe_path) {
    mcp_session_id_ = session_id;

    // Initialize ALL orchestrator components (IRC, tool tracker, agent spawner, etc.)
    // MCP mode needs the full orchestrator functionality
    if (!initialize()) {
        return false;
    }

    ORCH_LOG("Orchestrator: Opening MCP pipes for session %s\n", session_id.c_str());
    ORCH_LOG("Orchestrator: Input pipe: %s\n", input_pipe_path.c_str());
    ORCH_LOG("Orchestrator: Output pipe: %s\n", output_pipe_path.c_str());

    // Open input pipe (we read from this)
    mcp_input_fd_ = open(input_pipe_path.c_str(), O_RDONLY);
    if (mcp_input_fd_ < 0) {
        ORCH_LOG("Orchestrator: Failed to open input pipe: %s\n", strerror(errno));
        return false;
    }

    // Open output pipe (we write to this)
    mcp_output_fd_ = open(output_pipe_path.c_str(), O_WRONLY);
    if (mcp_output_fd_ < 0) {
        ORCH_LOG("Orchestrator: Failed to open output pipe: %s\n", strerror(errno));
        close(mcp_input_fd_);
        return false;
    }

    ORCH_LOG("Orchestrator: MCP mode initialized for session %s\n", session_id.c_str());
    return true;
}

void Orchestrator::start_mcp_listener() {
    if (!show_ui_ && mcp_input_fd_ >= 0 && mcp_output_fd_ >= 0) {
        ORCH_LOG("Orchestrator: Starting MCP listener thread\n");

        mcp_listener_thread_ = std::thread([this]() {
            while (!mcp_listener_should_stop_) {
                // Read JSON request from pipe
                std::string buffer;
                char read_buf[4096];

                ssize_t bytes = read(mcp_input_fd_, read_buf, sizeof(read_buf) - 1);
                if (bytes > 0) {
                    read_buf[bytes] = '\0';
                    buffer += read_buf;

                    // Look for complete JSON (newline-delimited)
                    size_t newline_pos = buffer.find('\n');
                    if (newline_pos != std::string::npos) {
                        std::string json_str = buffer.substr(0, newline_pos);
                        buffer = buffer.substr(newline_pos + 1);

                        try {
                            json request = json::parse(json_str);
                            ORCH_LOG("Orchestrator: Received MCP request: %s\n", request["method"].get<std::string>().c_str());

                            // Process request
                            json response = process_mcp_request(request);
                            std::string method = request.value("method", "");

                            // Send response back
                            std::string response_str = response.dump() + "\n";
                            write(mcp_output_fd_, response_str.c_str(), response_str.length());

                            // Handle shutdown after response is sent
                            if (method == "shutdown") {
                                ORCH_LOG("Orchestrator: Shutdown response sent, initiating graceful IDA close...\n");

                                // Close our end of the pipes to signal MCP server we're done
                                if (mcp_input_fd_ >= 0) {
                                    close(mcp_input_fd_);
                                    mcp_input_fd_ = -1;
                                }
                                if (mcp_output_fd_ >= 0) {
                                    close(mcp_output_fd_);
                                    mcp_output_fd_ = -1;
                                }

                                // Set flags to stop threads before database close
                                // This prevents deadlock when shutdown() tries to join this thread
                                mcp_listener_should_stop_ = true;
                                shutting_down_ = true;

                                // Request IDA to save and close the database
                                struct CloseRequest : exec_request_t {
                                    virtual ssize_t idaapi execute() override {
                                        msg("MCP: Saving database before close...\n");

                                        // First save the database
                                        if (save_database()) {
                                            msg("MCP: Database saved successfully\n");
                                        } else {
                                            msg("MCP: Warning - Failed to save database\n");
                                        }

                                        // Then terminate the database
                                        // This will trigger ui_database_closed event
                                        msg("MCP: Calling term_database()...\n");
                                        term_database();

                                        return 0;
                                    }
                                };

                                ORCH_LOG("Orchestrator: Requesting IDA to save and close database...\n");
                                CloseRequest req;
                                execute_sync(req, MFF_WRITE);

                                // The UI close action will trigger ui_database_closed event,
                                // which will call prepare_for_shutdown() -> cleanup() -> shutdown()
                                // But shutdown() will return early because shutting_down_ is already true,
                                // avoiding the thread join deadlock
                                break; // Exit listener loop
                            }

                        } catch (const json::exception& e) {
                            ORCH_LOG("Orchestrator: Failed to parse MCP request: %s\n", e.what());
                        }
                    }
                } else if (bytes == 0) {
                    // Pipe closed
                    ORCH_LOG("Orchestrator: MCP input pipe closed\n");
                    break;
                }
            }
        });
    }
}

json Orchestrator::process_mcp_request(const json& request) {
    json response;
    response["type"] = "response";
    response["id"] = request.value("id", "unknown");

    std::string method = request.value("method", "");

    if (method == "start_task") {
        std::string task = request["params"]["task"];
        ORCH_LOG("Orchestrator: Processing start_task: %s\n", task.c_str());

        // Clear any previous conversation
        clear_conversation();

        // Reset completion flag
        reset_task_completion();

        // Process the task in a separate thread to avoid blocking
        std::thread processing_thread([this, task]() {
            process_user_input(task);
        });

        // Wait for task to complete
        ORCH_LOG("Orchestrator: Waiting for task completion...\n");
        wait_for_task_completion();
        ORCH_LOG("Orchestrator: Task completed, sending response\n");

        // Join the processing thread
        processing_thread.join();

        // Prepare response with final result
        response["result"]["content"] = last_response_text_;
        response["result"]["agents_spawned"] = agents_.size();

    } else if (method == "process_input") {
        std::string input = request["params"]["input"];
        ORCH_LOG("Orchestrator: Processing follow-up input: %s\n", input.c_str());

        // Reset completion flag for continuation
        reset_task_completion();

        // Process the input in a separate thread to avoid blocking
        std::thread processing_thread([this, input]() {
            process_user_input(input);
        });

        // Wait for continuation to complete
        ORCH_LOG("Orchestrator: Waiting for continuation completion...\n");
        wait_for_task_completion();
        ORCH_LOG("Orchestrator: Continuation completed, sending response\n");

        // Join the processing thread
        processing_thread.join();

        // Prepare response with final result
        response["result"]["content"] = last_response_text_;
        response["result"]["agents_active"] = agents_.size() - completed_agents_.size();

    } else if (method == "shutdown") {
        ORCH_LOG("Orchestrator: Received shutdown request\n");
        response["result"]["status"] = "shutting_down";

        // Note: shutdown() will be called after this response is sent
        // No detached thread needed - prevents hanging process

    } else {
        response["error"] = "Unknown method: " + method;
    }

    return response;
}

void Orchestrator::clear_conversation() {
    ORCH_LOG("Orchestrator: Clearing conversation and starting fresh\n");
    
    // Clear conversation history
    conversation_history_.clear();
    
    // Clear any completed agents and results
    completed_agents_.clear();
    agent_results_.clear();
    
    // Reset token stats
    token_stats_.reset();
    
    // Mark conversation as inactive
    conversation_active_ = false;
    
    // Clear current task
    current_user_task_.clear();
    
    // Reset consolidation state
    consolidation_state_.consolidation_in_progress = false;
    consolidation_state_.consolidation_count = 0;
    
    ORCH_LOG("Orchestrator: Conversation cleared, ready for new task\n");
}

void Orchestrator::signal_task_completion() {
    std::lock_guard<std::mutex> lock(task_completion_mutex_);
    task_completed_ = true;
    task_completion_cv_.notify_all();
}

void Orchestrator::wait_for_task_completion() {
    std::unique_lock<std::mutex> lock(task_completion_mutex_);
    task_completion_cv_.wait(lock, [this] { return task_completed_; });
}

void Orchestrator::reset_task_completion() {
    std::lock_guard<std::mutex> lock(task_completion_mutex_);
    task_completed_ = false;
}

void Orchestrator::process_user_input(const std::string& input) {
    // Check if this is a continuation of an existing conversation
    if (conversation_active_) {
        // Continue existing conversation - just add the new user message
        conversation_history_.push_back(claude::messages::Message::user_text(input));
        ORCH_LOG("Orchestrator: Continuing conversation with: %s\n", input.c_str());
    } else {
        // New conversation - clear everything and start fresh
        current_user_task_ = input;
        
        // Clear any completed agents and results from previous tasks
        completed_agents_.clear();
        agent_results_.clear();
        
        // Reset token stats for new task
        token_stats_.reset();
        
        // Initialize conversation history for new task
        conversation_history_.clear();
        conversation_history_.push_back(claude::messages::Message::user_text(input));
        
        // Mark conversation as active
        conversation_active_ = true;
        ORCH_LOG("Orchestrator: Starting new conversation with: %s\n", input.c_str());
    }
    
    ORCH_LOG("Orchestrator: Processing task: %s\n", input.c_str());

    // Emit thinking event
    ORCH_LOG("Orchestrator: Publishing ORCHESTRATOR_THINKING event\n");
    event_bus_.publish(AgentEvent(AgentEvent::ORCHESTRATOR_THINKING, "orchestrator", {}));
    
    // Send to Claude API
    claude::ChatResponse response;
    if (conversation_history_.size() == 1) {
        // First message in conversation - use enhanced thinking prompt
        response = send_orchestrator_request(input);
    } else {
        // Continuing conversation - use the existing history
        response = send_continuation_request();
    }
    
    if (!response.success) {
        ORCH_LOG("Orchestrator: Failed to process request: %s\n", 
            response.error ? response.error->c_str() : "Unknown error");
        return;
    }
    
    // Track initial response tokens
    ORCH_LOG("DEBUG: Initial response usage - In: %d, Out: %d, Cache Read: %d, Cache Write: %d\n",
        response.usage.input_tokens, response.usage.output_tokens,
        response.usage.cache_read_tokens, response.usage.cache_creation_tokens);
    token_stats_.add_usage(response.usage);
    log_token_usage(response.usage, token_stats_.get_total());
    
    // Display orchestrator's response
    std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(response.message);
    if (text) {
        ORCH_LOG("Orchestrator: %s\n", text->c_str());
        
        // Only emit the response if there are no tool calls (otherwise wait for final response)
        std::vector<const claude::messages::ToolUseContent*> initial_tool_calls = 
            claude::messages::ContentExtractor::extract_tool_uses(response.message);
        if (initial_tool_calls.empty()) {
            // No tool calls, this is the final response
            if (text && !text->empty()) {
                last_response_text_ = *text;  // Store for MCP mode
            }
            ORCH_LOG("Orchestrator: Publishing ORCHESTRATOR_RESPONSE event (no tools)\n");
            if (show_ui_) {
                event_bus_.publish(AgentEvent(AgentEvent::ORCHESTRATOR_RESPONSE, "orchestrator", {
                    {"response", *text}
                }));
            }
            // Signal task completion for MCP mode
            if (!show_ui_) {
                signal_task_completion();
            }
        }
    }
    
    // Add response to conversation history
    conversation_history_.push_back(response.message);
    
    // Process any tool calls (spawn_agent, etc.)
    std::vector<claude::messages::Message> tool_results = process_orchestrator_tools(response.message);
    
    // Add tool results to conversation history
    for (const auto& result : tool_results) {
        conversation_history_.push_back(result);
    }
    
    // Continue conversation if needed
    if (!tool_results.empty()) {
        
        // Continue processing until no more tool calls
        while (true) {
            // Check if we need to consolidate context
            if (should_consolidate_context()) {
                ORCH_LOG("Orchestrator: Context limit reached, consolidating conversation...\n");
                consolidate_conversation_context();
            }
            
            // Send tool results back
            claude::ChatResponse continuation = send_continuation_request();
            if (!continuation.success) {
                ORCH_LOG("Orchestrator: Failed to get continuation: %s\n",
                    continuation.error ? continuation.error->c_str() : "Unknown error");

                // Signal task completion for MCP mode before breaking
                if (!show_ui_) {
                    signal_task_completion();
                }
                break;
            }
            
            // Track tokens from continuation response
            ORCH_LOG("DEBUG: Continuation usage - In: %d, Out: %d, Cache Read: %d, Cache Write: %d\n",
                continuation.usage.input_tokens, continuation.usage.output_tokens,
                continuation.usage.cache_read_tokens, continuation.usage.cache_creation_tokens);
            token_stats_.add_usage(continuation.usage);
            
            // Display text if present
            auto cont_text = claude::messages::ContentExtractor::extract_text(continuation.message);
            if (cont_text) {
                ORCH_LOG("Orchestrator: %s\n", cont_text->c_str());
            }
            
            // Process any tool calls in the continuation
            std::vector<claude::messages::Message> cont_tool_results = process_orchestrator_tools(continuation.message);
            
            // If no more tool calls, we're done
            if (cont_tool_results.empty()) {
                // Add the final continuation message to conversation history
                conversation_history_.push_back(continuation.message);
                
                // Publish the final response to UI before breaking
                if (cont_text && !cont_text->empty()) {
                    last_response_text_ = *cont_text;  // Store for MCP mode
                    if (show_ui_) {
                        event_bus_.publish(AgentEvent(AgentEvent::ORCHESTRATOR_RESPONSE, "orchestrator", {
                            {"response", *cont_text}
                        }));
                    }
                }
                // Log token usage after final response (pass per-iteration for context calc)
                log_token_usage(continuation.usage, token_stats_.get_total());

                // Signal task completion for MCP mode
                if (!show_ui_) {
                    signal_task_completion();
                }
                break;
            }
            
            // Add continuation and its tool results to conversation history
            conversation_history_.push_back(continuation.message);
            for (const auto& result : cont_tool_results) {
                conversation_history_.push_back(result);
            }
            
            // Log token usage after each continuation (pass per-iteration for context calc)
            log_token_usage(continuation.usage, token_stats_.get_total());
            
            ORCH_LOG("Orchestrator: Processed %zu more tool calls, continuing conversation...\n", 
                cont_tool_results.size());
        }
    }
}

claude::ChatResponse Orchestrator::send_continuation_request() {
    // Check and refresh OAuth token if needed
    if (!refresh_oauth_if_needed()) {
        ORCH_LOG("Orchestrator: Warning - OAuth token refresh check failed\n");
    }
    
    // Build request using existing conversation history
    claude::ChatRequestBuilder builder;
    builder.with_model(config_.orchestrator.model.model)
           .with_system_prompt(ORCHESTRATOR_SYSTEM_PROMPT)
           .with_max_tokens(config_.orchestrator.model.max_tokens)
           .with_max_thinking_tokens(config_.orchestrator.model.max_thinking_tokens)
           .with_temperature(config_.orchestrator.model.temperature)
           .enable_thinking(config_.orchestrator.model.enable_thinking)
           .enable_interleaved_thinking(false);
    
    // Add tools
    if (tool_registry_.has_tools()) {
        builder.with_tools(tool_registry_);
    }
    
    // Add all conversation history
    for (const auto& msg : conversation_history_) {
        builder.add_message(msg);
    }
    
    auto response = api_client_->send_request(builder.build());

    // Check for OAuth token expiry/revocation and retry if needed
    if (!response.success && response.error &&
        (response.error->find("401") != std::string::npos ||
         response.error->find("unauthorized") != std::string::npos ||
         response.error->find("revoked") != std::string::npos ||
         response.error->find("expired") != std::string::npos)) {
        ORCH_LOG("Orchestrator: Got OAuth auth error, attempting token refresh...\n");
        if (refresh_oauth_if_needed()) {
            response = api_client_->send_request(builder.build());
        }
    }

    return response;
}

claude::ChatResponse Orchestrator::send_orchestrator_request(const std::string& user_input) {
    // Check and refresh OAuth token if needed
    if (!refresh_oauth_if_needed()) {
        ORCH_LOG("Orchestrator: Warning - OAuth token refresh check failed\n");
    }
    
    // Build request with extensive thinking
    claude::ChatRequestBuilder builder;
    builder.with_model(config_.orchestrator.model.model)
           .with_system_prompt(ORCHESTRATOR_SYSTEM_PROMPT)
           .with_max_tokens(config_.orchestrator.model.max_tokens)
           .with_max_thinking_tokens(config_.orchestrator.model.max_thinking_tokens)
           .with_temperature(config_.orchestrator.model.temperature)
           .enable_thinking(config_.orchestrator.model.enable_thinking)
           .enable_interleaved_thinking(false);
    
    // Add tools
    if (tool_registry_.has_tools()) {
        builder.with_tools(tool_registry_);
    }
    
    // Add the user message with thinking prompt
    std::string enhanced_input = DEEP_THINKING_PROMPT;
    enhanced_input += "\n\nUser Task: " + user_input;
    enhanced_input += "\n\nCurrent binary being analyzed: " + main_database_path_;
    enhanced_input += "\n\nCurrent Agents: ";
    
    // Add info about active agents
    if (agents_.empty()) {
        enhanced_input += "None";
    } else {
        for (const auto& [id, info] : agents_) {
            enhanced_input += std::format("\n- {} (task: {})", id, info.task);
        }
    }
    
    builder.add_message(claude::messages::Message::user_text(enhanced_input));
    
    auto response = api_client_->send_request(builder.build());

    // Check for OAuth token expiry/revocation and retry if needed
    if (!response.success && response.error &&
        (response.error->find("OAuth token has expired") != std::string::npos ||
         response.error->find("revoked") != std::string::npos ||
         response.error->find("401") != std::string::npos ||
         response.error->find("unauthorized") != std::string::npos)) {

        ORCH_LOG("Orchestrator: OAuth token error (%s), attempting to refresh...\n",
            response.error->c_str());

        if (refresh_oauth_if_needed()) {
            ORCH_LOG("Orchestrator: Retrying request with refreshed OAuth token...\n");
            response = api_client_->send_request(builder.build());
        } else {
            ORCH_LOG("Orchestrator: Failed to refresh OAuth token\n");
        }
    }

    return response;
}

std::vector<claude::messages::Message> Orchestrator::process_orchestrator_tools(const claude::messages::Message& msg) {
    std::vector<claude::messages::Message> results;
    std::vector<const claude::messages::ToolUseContent*> tool_calls = claude::messages::ContentExtractor::extract_tool_uses(msg);

    // If no tool calls, return empty results
    if (tool_calls.empty()) {
        return results;
    }

    // Create a single User message that will contain all tool results
    claude::messages::Message combined_result(claude::messages::Role::User);

    // First pass: Execute all tools and collect spawn_agent results
    std::map<std::string, std::string> tool_to_agent; // tool_id -> agent_id
    std::vector<std::string> spawned_agent_ids;
    std::vector<std::pair<std::string, claude::messages::Message>> non_spawn_results; // Store non-spawn results
    
    for (const claude::messages::ToolUseContent* tool_use : tool_calls) {
        if (tool_use->name == "spawn_agent") {
            ORCH_LOG("Orchestrator: Executing spawn_agent tool via registry (id: %s)\n", tool_use->id.c_str());
            
            // Execute via tool registry (which calls spawn_agent_async)
            claude::messages::Message result = tool_registry_.execute_tool_call(*tool_use);
            
            // Extract agent_id from the tool result
            claude::messages::ContentExtractor extractor;
            for (const auto& content : result.contents()) {
                content->accept(extractor);
            }
            
            if (!extractor.get_tool_results().empty()) {
                try {
                    json result_json = json::parse(extractor.get_tool_results()[0]->content);
                    if (result_json["success"]) {
                        std::string agent_id = result_json["agent_id"];
                        tool_to_agent[tool_use->id] = agent_id;
                        spawned_agent_ids.push_back(agent_id);
                        ORCH_LOG("Orchestrator: Spawned agent %s for tool call %s\n", 
                            agent_id.c_str(), tool_use->id.c_str());
                    } else {
                        ORCH_LOG("Orchestrator: spawn_agent failed for tool call %s\n", tool_use->id.c_str());
                        tool_to_agent[tool_use->id] = "";  // Empty means error
                    }
                } catch (const std::exception& e) {
                    ORCH_LOG("Orchestrator: Failed to parse spawn_agent result: %s\n", e.what());
                    tool_to_agent[tool_use->id] = "";  // Empty means error
                }
            }
            // Don't add to results yet - we'll enrich it after waiting
        } else {
            // Execute other tools normally and store for later
            ORCH_LOG("Orchestrator: Executing non-spawn_agent tool: %s\n", tool_use->name.c_str());
            non_spawn_results.push_back({tool_use->id, tool_registry_.execute_tool_call(*tool_use)});
        }
    }
    
    // If we spawned any agents, wait for ALL of them to complete
    if (!spawned_agent_ids.empty()) {
        ORCH_LOG("Orchestrator: Waiting for %zu agents to complete their tasks...\n", spawned_agent_ids.size());
        wait_for_agents_completion(spawned_agent_ids);
        ORCH_LOG("Orchestrator: All %zu agents have completed\n", spawned_agent_ids.size());
    }
    
    // Add non-spawn_agent results to the combined message first
    for (const auto& [tool_id, result] : non_spawn_results) {
        // Extract the ToolResultContent from the result message
        claude::messages::ContentExtractor extractor;
        for (const auto& content : result.contents()) {
            content->accept(extractor);
        }
        
        // Add each tool result content to our combined message
        for (const auto& tool_result : extractor.get_tool_results()) {
            combined_result.add_content(std::make_unique<claude::messages::ToolResultContent>(
                tool_result->tool_use_id, tool_result->content, tool_result->is_error
            ));
        }
    }
    
    // Second pass: Add enriched results for spawn_agent calls
    for (const claude::messages::ToolUseContent* tool_use : tool_calls) {
        if (tool_use->name == "spawn_agent") {
            std::map<std::string, std::string>::iterator it = tool_to_agent.find(tool_use->id);
            if (it != tool_to_agent.end()) {
                std::string agent_id = it->second;
                
                if (!agent_id.empty()) {
                    // Get the agent's full report
                    std::string report = get_agent_result(agent_id);
                    
                    // Find agent task
                    std::string task = "";
                    std::map<std::string, AgentInfo>::iterator agent_it = agents_.find(agent_id);
                    if (agent_it != agents_.end()) {
                        task = agent_it->second.task;
                    }
                    
                    // Create enriched result with full report
                    json result_json = {
                        {"agent_id", agent_id},
                        {"task", task},
                        {"report", report}  // Full agent report added here
                    };

                    // Add to the combined message
                    combined_result.add_content(std::make_unique<claude::messages::ToolResultContent>(
                        tool_use->id, result_json.dump(), false
                    ));
                    
                    ORCH_LOG("Orchestrator: Added spawn_agent result with report for %s\n", agent_id.c_str());
                } else {
                    // Create error result
                    json error_json = {
                        {"error", "Failed to spawn agent"}
                    };
                    
                    // Add to the combined message
                    combined_result.add_content(std::make_unique<claude::messages::ToolResultContent>(
                        tool_use->id, error_json.dump(), true  // is_error = true
                    ));
                    
                    ORCH_LOG("Orchestrator: Added spawn_agent error result\n");
                }
            }
        }
    }
    
    // Add the single combined message to results (if it has content)
    if (!combined_result.contents().empty()) {
        results.push_back(combined_result);
    }
    
    return results;
}

json Orchestrator::spawn_agent_async(const std::string& task, const std::string& context) {
    ORCH_LOG("Orchestrator: Spawning agent for task: %s\n", task.c_str());
    
    // Generate agent ID
    std::string agent_id = std::format("agent_{}", next_agent_id_++);
    
    // Emit agent spawning event
    ORCH_LOG("Orchestrator: Publishing AGENT_SPAWNING event for %s\n", agent_id.c_str());
    event_bus_.publish(AgentEvent(AgentEvent::AGENT_SPAWNING, "orchestrator", {
        {"agent_id", agent_id},
        {"task", task}
    }));
    
    // Save and pack current database
    ORCH_LOG("Orchestrator: Creating agent database for %s\n", agent_id.c_str());
    std::string agent_db_path = db_manager_->create_agent_database(agent_id);
    ORCH_LOG("Orchestrator: Agent database created at: %s\n", agent_db_path.c_str());
    if (agent_db_path.empty()) {
        return {
            {"success", false},
            {"error", "Failed to create agent database"}
        };
    }
    
    // Agents will discover each other dynamically via IRC
    std::string agent_prompt = generate_agent_prompt(task, context);
    
    // Prepare agent configuration with swarm settings
    json agent_config = {
        {"agent_id", agent_id},
        {"binary_name", binary_name_},  // Pass binary name to agent
        {"task", task},                 // Include the raw task for IRC sharing
        {"prompt", agent_prompt},       // Full prompt with task and collaboration instructions
        {"database", agent_db_path},
        {"irc_server", config_.irc.server},
        {"irc_port", allocated_irc_port_}  // Use the dynamically allocated port
    };
    
    // Spawn the agent process
    ORCH_LOG("Orchestrator: About to spawn agent process for %s\n", agent_id.c_str());
    int pid = agent_spawner_->spawn_agent(agent_id, agent_db_path, agent_config);
    ORCH_LOG("Orchestrator: Agent spawner returned PID %d for %s\n", pid, agent_id.c_str());
    
    if (pid <= 0) {
        // Emit spawn failed event
        event_bus_.publish(AgentEvent(AgentEvent::AGENT_SPAWN_FAILED, "orchestrator", {
            {"agent_id", agent_id},
            {"error", "Failed to spawn agent process"}
        }));
        
        return {
            {"success", false},
            {"error", "Failed to spawn agent process"}
        };
    }
    
    // Emit spawn complete event
    ORCH_LOG("Orchestrator: Publishing AGENT_SPAWN_COMPLETE event for %s\n", agent_id.c_str());
    event_bus_.publish(AgentEvent(AgentEvent::AGENT_SPAWN_COMPLETE, "orchestrator", {
        {"agent_id", agent_id}
    }));
    
    // Track agent info
    AgentInfo info;
    info.agent_id = agent_id;
    info.task = task;
    info.database_path = agent_db_path;
    info.process_id = pid;
    
    agents_[agent_id] = info;
    
    ORCH_LOG("Orchestrator: Agent %s spawned with PID %d (async)\n", agent_id.c_str(), pid);
    
    return {
        {"success", true},
        {"agent_id", agent_id},
        {"process_id", pid},
        {"database", agent_db_path}
    };
}

std::string Orchestrator::get_agent_result(const std::string& agent_id) const {
    auto it = agent_results_.find(agent_id);
    if (it != agent_results_.end()) {
        return it->second;
    }
    return "Agent did not provide a report";
}

json Orchestrator::merge_database(const std::string& agent_id) {
    auto it = agents_.find(agent_id);
    if (it == agents_.end()) {
        return {
            {"success", false},
            {"error", "Agent not found"}
        };
    }
    
    ORCH_LOG("Orchestrator: Merging database from agent %s\n", agent_id.c_str());
    
    // Get agent's tool calls
    std::vector<ToolCall> tool_calls = tool_tracker_->get_agent_tool_calls(agent_id);
    
    // Apply changes to main database
    auto result = merge_manager_->merge_agent_changes(agent_id);
    
    if (result.success) {
        ORCH_LOG("Orchestrator: Successfully merged %d changes from agent %s\n", 
            result.changes_applied, agent_id.c_str());
            
        return {
            {"success", true},
            {"agent_id", agent_id},
            {"changes_applied", result.changes_applied},
            {"changes_failed", result.changes_failed},
            {"message", std::format("Merged {} changes from agent {}", result.changes_applied, agent_id)}
        };
    } else {
        return {
            {"success", false},
            {"error", result.error_message}
        };
    }
}

std::string Orchestrator::generate_agent_prompt(const std::string& task, const std::string& context) {
    std::string prompt = R"(You are a specialized agent in a multi-agent reverse engineering swarm.

CRITICAL LIMITATION - IMPLEMENTATION TASKS:
If asked to create implementation files (.cpp, .h, .c), write code, or generate complete source projects, you should respond by:
1. Explaining what reverse engineering insights you CAN provide about the target
2. Detailing the specific analysis you can perform (function identification, algorithm understanding, data structure mapping)
3. Clarifying that implementation file creation requires different tools than your reverse engineering capabilities
4. Offering to analyze the target binary to provide the technical understanding needed for someone else to implement
5. You must TEACH your caller that you just simply are not technically able to perform that task, you must make this clear

This educational response helps everyone understand the division of labor between analysis and implementation.

YOUR TASK: )" + task + R"(

CONTEXT: )" + context;

    // Get list of currently active agents with their tasks
    std::vector<std::pair<std::string, std::string>> active_agents;
    for (const auto& [id, info] : agents_) {
        // Check if agent hasn't completed yet (using IRC tracking to avoid hanging on zombies)
        if (completed_agents_.count(id) == 0) {
            active_agents.push_back({id, info.task});
        }
    }
    
    // Add completed agents with their results
    // i do not want to do this, but the orchestrator is not good at understanding that these are starting fresh, and it doesn't provide enough information.
    // if agent collaboration was working better that would solve this, but the agents just go and waste eachothers time so i had to remove it
    // in the future ill redesign all of this (currently super hodgepodge) focused around irc from the get go
    if (!completed_agents_.empty()) {
        prompt += R"(

COMPLETED AGENTS & THEIR RESULTS:
)";
        for (const std::string& agent_id : completed_agents_) {
            auto agent_it = agents_.find(agent_id);
            auto result_it = agent_results_.find(agent_id);
            
            if (agent_it != agents_.end() && result_it != agent_results_.end()) {
                prompt += "- " + agent_id + " (task: " + agent_it->second.task + ")\n";
                prompt += "  Result: " + result_it->second + "\n\n";
            }
        }
        prompt += R"(Use these completed results to:
- Build upon previous findings rather than duplicating work
- Reference specific discoveries from other agents
- Avoid re-analyzing what has already been solved

)";
    }
    
    if (!active_agents.empty()) {
        prompt += R"(CURRENTLY ACTIVE AGENTS:
)";
        for (const auto& [agent_id, agent_task] : active_agents) {
            prompt += "- " + agent_id + " (working on: " + agent_task + ")\n";
        }
        prompt += R"(
You can see what each agent is working on above. Use this information to:
- Share relevant findings with agents working on related tasks
- Coordinate when your tasks overlap or depend on each other
)";
    } else if (completed_agents_.empty()) {
        prompt += R"(

You are currently the only active agent.
- Other agents may join later and will be announced via IRC
)";
    }

    /*
    Remember: You're part of a team. Collaborate effectively, but know when your work is complete.
    */
    prompt += R"(

COLLABORATION CAPABILITIES:
- You are connected to IRC for conflict resolution
- Conflicts are handled automatically in dedicated channels
- You cannot directly message other agents

CONFLICT RESOLUTION:
When you try to modify something another agent has already modified:
1. You'll be notified of the conflict
2. Join the conflict channel to discuss
3. Present your reasoning with specific evidence
4. Listen to other agents' perspectives
5. Work together to determine the most accurate interpretation
6. Update your analysis based on consensus

IMPORTANT NOTES:
- You have full access to analyze and modify the binary
- Your work will be merged back to the main database by the orchestrator
- Quality matters more than speed - be thorough and accurate
- Build on other agents' work rather than duplicating effort

TASK COMPLETION PROTOCOL:
When you have thoroughly analyzed your assigned task and gathered sufficient evidence:
1. Store ALL your key findings using the store_analysis tool
2. Send a comprehensive final report as a regular message with NO tool calls

CRITICAL COMPLETION RULES:
- Your FINAL message must contain NO tool calls - this triggers task completion
- Once you send a message without tools, you are declaring your work DONE
- The system will automatically handle your exit once you send a message without tools
- Focus on YOUR task - complete it thoroughly, report once, then stop

When ready to finish, simply send your final analysis as a message WITHOUT any tool calls.

Begin your analysis now.)";
    
    return prompt;
}

int Orchestrator::allocate_unique_port() {
    // Use standard IRC port range starting at 6667
    constexpr int BASE_PORT = 6667;
    constexpr int PORT_RANGE = 2000;  // Search in range 6667-8666
    
    // Calculate starting port based on binary name hash for predictability
    std::hash<std::string> hasher;
    size_t hash = hasher(binary_name_);
    int start_port = BASE_PORT + (hash % PORT_RANGE);
    
    // Try ports starting from hash-based port
    for (int port = start_port; port < BASE_PORT + PORT_RANGE; ++port) {
        if (irc::IRCServer::is_port_available(port)) {
            return port;
        }
    }
    
    // If no port in upper range, try from base port to start port
    for (int port = BASE_PORT; port < start_port; ++port) {
        if (irc::IRCServer::is_port_available(port)) {
            return port;
        }
    }
    
    // Should not happen unless system has major port exhaustion
    ORCH_LOG("Orchestrator: Warning - Could not find available port in range [%d, %d]\n", 
        BASE_PORT, BASE_PORT + PORT_RANGE - 1);
    return BASE_PORT;  // Return base port as fallback
}

void Orchestrator::wait_for_agents_completion(const std::vector<std::string>& agent_ids) {
    ORCH_LOG("Orchestrator: Waiting for %zu agents to complete...\n", agent_ids.size());
    
    // Wait for all specified agents to send their results or exit
    int check_count = 0;
    std::set<std::string> agents_done;
    
    while (agents_done.size() < agent_ids.size()) {
        agents_done.clear();
        
        // Check each agent for completion
        for (const std::string& agent_id : agent_ids) {
            // Check if agent sent IRC completion message
            bool has_irc_result = (completed_agents_.count(agent_id) > 0);
            
            // Check if agent process has exited
            bool process_exited = false;
            auto agent_it = agents_.find(agent_id);
            if (agent_it != agents_.end()) {
                int pid = agent_it->second.process_id;
                if (pid > 0 && !agent_spawner_->is_agent_running(pid)) {
                    process_exited = true;
                    ORCH_LOG("Orchestrator: Agent %s process %d has exited\n", agent_id.c_str(), pid);
                }
            }
            
            // Consider agent done if EITHER condition is met
            if (has_irc_result || process_exited) {
                agents_done.insert(agent_id);
                
                // If process exited but no IRC message, mark as completed with default message
                if (process_exited && !has_irc_result) {
                    ORCH_LOG("Orchestrator: Agent %s exited without sending result, marking as completed\n", agent_id.c_str());
                    completed_agents_.insert(agent_id);
                    agent_results_[agent_id] = "Agent process terminated without sending final report";
                    
                    // Emit task complete event for UI updates
                    event_bus_.publish(AgentEvent(AgentEvent::TASK_COMPLETE, agent_id, {}));
                }
            }
        }
        
        ORCH_LOG("Orchestrator: Check #%d - %zu/%zu agents completed (IRC: %zu)\n", 
            ++check_count, agents_done.size(), agent_ids.size(), completed_agents_.size());
        
        // Check if all requested agents have completed
        if (agents_done.size() >= agent_ids.size()) {
            ORCH_LOG("Orchestrator: All %zu agents have completed\n", agent_ids.size());
            break;
        }
        
        // Wait before checking again
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    ORCH_LOG("Orchestrator: Agent wait complete\n");
}

bool Orchestrator::refresh_oauth_if_needed() {
    // Only refresh if using OAuth
    if (!oauth_manager_ || config_.api.auth_method != claude::AuthMethod::OAUTH) {
        return true; // Not using OAuth, no refresh needed
    }

    // FIRST: Try to reload credentials from disk
    // Another orchestrator or agent may have already refreshed them
    ORCH_LOG("Orchestrator: Clearing OAuth credential cache and reloading from disk...\n");
    oauth_manager_->clear_cache();

    auto reloaded_creds = oauth_manager_->get_credentials();
    if (reloaded_creds) {
        // Check if the reloaded credentials are still valid
        if (!reloaded_creds->is_expired(300)) {
            ORCH_LOG("Orchestrator: Successfully reloaded fresh credentials from disk\n");
            api_client_->set_oauth_credentials(reloaded_creds);
            return true;
        }
        ORCH_LOG("Orchestrator: Reloaded credentials are still expired, forcing refresh...\n");
    } else {
        ORCH_LOG("Orchestrator: Failed to reload credentials from disk: %s\n",
            oauth_manager_->get_last_error().c_str());
    }

    // SECOND: If reload didn't work or credentials still expired, force refresh
    ORCH_LOG("Orchestrator: Forcing OAuth token refresh via API...\n");

    auto refreshed_creds = oauth_manager_->force_refresh();
    if (!refreshed_creds) {
        ORCH_LOG("Orchestrator: Failed to refresh OAuth token: %s\n",
            oauth_manager_->get_last_error().c_str());
        return false;
    }

    // Update the API client with new credentials
    api_client_->set_oauth_credentials(refreshed_creds);
    ORCH_LOG("Orchestrator: Successfully refreshed OAuth token via API\n");

    return true;
}

void Orchestrator::handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message) {
    ORCH_LOG("DEBUG: IRC message received - Channel: %s, Sender: %s, Message: %s\n", channel.c_str(), sender.c_str(), message.c_str());
    // Emit all IRC messages to the UI for display
    event_bus_.publish(AgentEvent(AgentEvent::MESSAGE, sender, {
        {"channel", channel},
        {"message", message}
    }));
    
    // Check for manual tool execution results
    if (message.find("MANUAL_TOOL_RESULT | ") == 0) {
        handle_manual_tool_result(message);
        return;
    }

    // Check if this is a conflict channel message
    // Note: Don't return here - we need to check for MARKED_CONSENSUS messages below
    if (channel.find("#conflict_") == 0) {
        std::lock_guard<std::mutex> lock(conflicts_mutex_);
        ConflictSession& session = active_conflicts_[channel];

        // Track participants from messages in the channel
        session.participating_agents.insert(sender);

        // Don't return here - MARKED_CONSENSUS messages need to be handled below
    }

    // Handle requests for agents to join conflict discussions
    if (message.find("JOIN_CONFLICT|") == 0) {
        // Format: JOIN_CONFLICT|target|channel
        std::string parts = message.substr(14);  // Skip "JOIN_CONFLICT|"
        size_t pipe = parts.find('|');

        if (pipe != std::string::npos) {
            std::string target_agent = parts.substr(0, pipe);
            std::string conflict_channel = parts.substr(pipe + 1);

            ORCH_LOG("Orchestrator: Request for agent %s to join conflict channel %s\n", target_agent.c_str(), conflict_channel.c_str());

            // Check if agent is running or completed
            auto agent_it = agents_.find(target_agent);
            if (agent_it != agents_.end()) {
                // Agent exists - check if it's running or completed
                if (completed_agents_.count(target_agent) > 0) {
                    // Agent has completed - resurrect it
                    ORCH_LOG("Orchestrator: Agent %s has completed, resurrecting for conflict resolution...\n", target_agent.c_str());

                std::string db_path = agent_it->second.database_path;

                // Create resurrection config - agent will get details from channel
                json resurrection_config = {
                    {"reason", "conflict_resolution"},
                    {"conflict_channel", conflict_channel}
                };

                // Remove from completed set since it's being resurrected
                completed_agents_.erase(target_agent);

                // Resurrect the agent
                int pid = agent_spawner_->resurrect_agent(target_agent, db_path, resurrection_config);
                if (pid > 0) {
                    ORCH_LOG("Orchestrator: Successfully resurrected agent %s (PID %d)\n",
                        target_agent.c_str(), pid);

                    // Update the agent info with new PID
                    agent_it->second.process_id = pid;
                    agent_it->second.task = "Conflict Resolution";

                    // The resurrected agent will join the conflict channel and see the
                    // conflict details that the initiating agent posts there
                } else {
                    ORCH_LOG("Orchestrator: Failed to resurrect agent %s\n", target_agent.c_str());
                    // Add back to completed since resurrection failed
                    completed_agents_.insert(target_agent);
                }
                } else {
                    // Agent is still running - send CONFLICT_INVITE
                    ORCH_LOG("Orchestrator: Agent %s is still running, sending conflict invite...\n",
                             target_agent.c_str());

                    std::string invite_msg = std::format("CONFLICT_INVITE|{}|{}", target_agent, conflict_channel);
                    irc_client_->send_message("#agents", invite_msg);
                    ORCH_LOG("Orchestrator: Sent CONFLICT_INVITE to agent %s for channel %s\n",
                             target_agent.c_str(), conflict_channel.c_str());
                }
            } else {
                ORCH_LOG("Orchestrator: Agent %s not found in agents map\n", target_agent.c_str());
            }
        } else {
            ORCH_LOG("Orchestrator: Invalid JOIN_CONFLICT message format - expecting target|channel\n");
        }
        return;
    }
    
    // Handle MARKED_CONSENSUS messages from conflict channels
    if (channel.find("#conflict_") == 0 && message.find("MARKED_CONSENSUS|") == 0) {
        // Format: MARKED_CONSENSUS|agent_id|consensus
        std::string content = message.substr(17);  // Skip "MARKED_CONSENSUS|"

        size_t first_pipe = content.find('|');

        if (first_pipe != std::string::npos) {
            std::string agent_id = content.substr(0, first_pipe);
            std::string consensus = content.substr(first_pipe + 1);

            ORCH_LOG("Orchestrator: Agent %s marked consensus for %s: %s\n",
                     agent_id.c_str(), channel.c_str(), consensus.c_str());

            // Track the consensus mark
            json tool_call;
            std::set<std::string> agents_copy;
            bool should_enforce = false;

            {
                std::lock_guard<std::mutex> lock(conflicts_mutex_);
                if (active_conflicts_.find(channel) != active_conflicts_.end()) {
                    ConflictSession& session = active_conflicts_[channel];
                    session.consensus_statements[agent_id] = consensus;
                    session.participating_agents.insert(agent_id);

                    // Check if all participating agents have marked consensus
                    bool all_marked = true;
                    for (const std::string& participant: session.participating_agents) {
                        if (session.consensus_statements.find(participant) == session.consensus_statements.end()) {
                            all_marked = false;
                            break;
                        }
                    }

                    if (all_marked && session.participating_agents.size() >= 2 && !session.resolved) {
                        ORCH_LOG("Orchestrator: All agents marked consensus for %s, extracting and enforcing\n", channel.c_str());

                        // Mark as resolved to prevent re-processing
                        session.resolved = true;

                        // Extract the data we need while holding the lock
                        tool_call = extract_consensus_tool_call(session);
                        agents_copy = session.participating_agents;
                        should_enforce = true;
                    }
                }
            } // Lock released here

            // Now enforce consensus without holding the lock
            if (should_enforce) {
                // Spawn a thread to handle consensus enforcement so we don't block the IRC thread
                std::thread enforcement_thread([this, channel, tool_call, agents_copy]() {
                    if (!tool_call.is_null() && tool_call.contains("tool_name") && tool_call.contains("parameters")) {
                        enforce_consensus_tool_execution(channel, tool_call, agents_copy);
                    }

                    if (irc_client_) {
                        // Send to the conflict channel so participating agents see it
                        irc_client_->send_message(channel, "CONSENSUS_COMPLETE");
                    }
                    ORCH_LOG("Orchestrator: Sent CONSENSUS_COMPLETE notification to all agents\n");

                    // Clean up after a delay
                    std::this_thread::sleep_for(std::chrono::seconds(3));

                    // Re-acquire lock to clean up
                    {
                        std::lock_guard<std::mutex> cleanup_lock(conflicts_mutex_);
                        active_conflicts_.erase(channel);
                    }
                });

                // Detach the thread so it runs independently
                enforcement_thread.detach();
            }
        }
        return;
    }

    // Parse AGENT_TOKEN_UPDATE messages from #agents channel
    if (channel == "#agents" && message.find("AGENT_TOKEN_UPDATE | ") == 0) {
        // Format: AGENT_TOKEN_UPDATE | {json}
        std::string json_str = message.substr(21);  // Skip "AGENT_TOKEN_UPDATE | "
        
        ORCH_LOG("DEBUG: Received AGENT_TOKEN_UPDATE from IRC: %s\n", json_str.c_str());
        
        try {
            json metric_json = json::parse(json_str);
            std::string agent_id = metric_json["agent_id"];
            json tokens = metric_json["tokens"];
            json session_tokens = metric_json.value("session_tokens", json());
            int iteration = metric_json.value("iteration", 0);
            
            // Forward to UI via EventBus
            event_bus_.publish(AgentEvent(AgentEvent::AGENT_TOKEN_UPDATE, "orchestrator", {
                {"agent_id", agent_id},
                {"tokens", tokens},
                {"session_tokens", session_tokens},
                {"iteration", iteration}
            }));
            
            ORCH_LOG("Orchestrator: Received token metrics from %s (iteration %d)\n", 
                agent_id.c_str(), iteration);
        } catch (const std::exception& e) {
            ORCH_LOG("Orchestrator: Failed to parse agent metric JSON: %s\n", e.what());
        }
        return;
    }
    
    // Parse AGENT_RESULT messages from #results channel
    if (channel == "#results") {
        if (message.find("AGENT_RESULT|") == 0) {
        // Format: AGENT_RESULT|{json}
        std::string json_str = message.substr(13);  // Skip "AGENT_RESULT|"
        
        try {
            json result_json = json::parse(json_str);
            std::string agent_id = result_json["agent_id"];
            std::string report = result_json["report"];
            
            ORCH_LOG("Orchestrator: Received result from %s: %s\n", agent_id.c_str(), report.c_str());
            
            // Emit swarm result event
            event_bus_.publish(AgentEvent(AgentEvent::SWARM_RESULT, "orchestrator", {
                {"agent_id", agent_id},
                {"result", report}
            }));
            
            // Store the result
            agent_results_[agent_id] = report;
            
            // Mark agent as completed
            completed_agents_.insert(agent_id);
            ORCH_LOG("Orchestrator: Marked %s as completed (have %zu/%zu completions)\n",
                agent_id.c_str(), completed_agents_.size(), agents_.size());
            
            // Emit task complete event for UI updates
            event_bus_.publish(AgentEvent(AgentEvent::TASK_COMPLETE, agent_id, {}));
            
            // Find the agent info
            auto it = agents_.find(agent_id);
            if (it != agents_.end()) {
                // Display the agent's result to the user
                ORCH_LOG("===========================================\n");
                ORCH_LOG("Agent %s completed task: %s\n", agent_id.c_str(), it->second.task.c_str());
                ORCH_LOG("Result: %s\n", report.c_str());
                ORCH_LOG("===========================================\n");
                
                // Automatically merge the agent's database changes
                ORCH_LOG("Orchestrator: Auto-merging database changes from agent %s\n", agent_id.c_str());
                json merge_result = merge_database(agent_id);
                
                if (merge_result["success"]) {
                    ORCH_LOG("Orchestrator: Successfully auto-merged %d changes from agent %s\n",
                        merge_result.value("changes_applied", 0), agent_id.c_str());
                    if (merge_result.value("changes_failed", 0) > 0) {
                        ORCH_LOG("Orchestrator: Warning - %d changes failed to merge\n",
                            merge_result.value("changes_failed", 0));
                    }
                } else {
                    ORCH_LOG("Orchestrator: Failed to auto-merge changes from agent %s: %s\n",
                        agent_id.c_str(), merge_result.value("error", "Unknown error").c_str());
                }
            }
        } catch (const std::exception& e) {
            ORCH_LOG("Orchestrator: Failed to parse agent result JSON: %s\n", e.what());
        }
        }
    }
}

json Orchestrator::extract_consensus_tool_call(const ConflictSession& session) {
    ORCH_LOG("Orchestrator: Extracting consensus tool call from multiple agent statements\n");

    // Check if we have the original conflict details
    if (session.original_conflict.first_call.tool_name.empty()) {
        ORCH_LOG("Orchestrator: WARNING - No original conflict details, falling back\n");

        return {
            {"tool_name", "unknown"}
        };
    }

    try {
        // Create a temporary consensus executor
        agent::ConsensusExecutor executor(config_);

        // Pass all individual consensus statements from each agent
        json tool_call = executor.execute_consensus(session.consensus_statements, session.original_conflict);

        if (tool_call.is_null() || !tool_call.contains("tool_name")) {
            ORCH_LOG("Orchestrator: ConsensusExecutor failed to extract tool call\n");
            // not necessarily a failure, the agents could have decided that no modification was needed in which case no tool call will be extracted

            return {
                {"tool_name", "unknown"}
            };
        }

        ORCH_LOG("Orchestrator: ConsensusExecutor extracted tool call: %s\n", tool_call.dump().c_str());
        return tool_call;

    } catch (const std::exception& e) {
        ORCH_LOG("Orchestrator: ERROR in ConsensusExecutor: %s\n", e.what());

        return {
            {"tool_name", "unknown"}
        };
    }
}


void Orchestrator::enforce_consensus_tool_execution(const std::string& channel, const json& tool_call,
                                                   const std::set<std::string>& agents) {
    ORCH_LOG("Orchestrator: Enforcing consensus tool execution for %zu agents\n", agents.size());

    // Safely extract tool_name with error checking
    if (!tool_call.contains("tool_name") || !tool_call["tool_name"].is_string()) {
        ORCH_LOG("Orchestrator: ERROR - Invalid or missing tool_name in consensus\n");
        return;
    }
    std::string tool_name = tool_call["tool_name"].get<std::string>();

    // Safely extract parameters, ensuring it's an object
    json parameters = json::object();
    if (tool_call.contains("parameters")) {
        if (tool_call["parameters"].is_object()) {
            parameters = tool_call["parameters"];
        } else if (!tool_call["parameters"].is_null()) {
            ORCH_LOG("Orchestrator: WARNING - parameters is not an object, using empty object\n");
        }
    }

    if (tool_name == "unknown") return;

    // Track responses
    {
        std::lock_guard<std::mutex> lock(manual_tool_mutex_);
        manual_tool_responses_.clear();
        for (const auto& agent_id : agents) {
            manual_tool_responses_[agent_id] = false;
        }
    }
    
    // Fix address format if it's a number instead of hex string
    if (parameters.contains("address") && parameters["address"].is_number()) {
        // Convert decimal address to hex string
        uint64_t addr = parameters["address"].get<uint64_t>();
        std::stringstream hex_stream;
        hex_stream << "0x" << std::hex << addr;
        parameters["address"] = hex_stream.str();
        ORCH_LOG("Orchestrator: Converted decimal address to hex: %s\n", hex_stream.str().c_str());
    }

    // Send manual tool execution to each agent
    for (const std::string& agent_id: agents) {
        std::string params_str = parameters.dump();
        std::string message = "MANUAL_TOOL_EXEC|" + agent_id + "|" + tool_name + "|" + params_str;
        
        if (irc_client_) {
            irc_client_->send_message(channel, message);
            ORCH_LOG("Orchestrator: Sent manual tool exec to %s\n", agent_id.c_str());
        }
    }
    
    // Wait for responses with timeout
    auto start_time = std::chrono::steady_clock::now();
    auto timeout = std::chrono::seconds(5);
    
    while (true) {
        // Check if all agents responded
        bool all_responded = true;
        {
            std::lock_guard<std::mutex> lock(manual_tool_mutex_);
            for (const auto& [agent_id, responded] : manual_tool_responses_) {
                if (!responded) {
                    all_responded = false;
                    break;
                }
            }
        }
        
        if (all_responded) {
            ORCH_LOG("Orchestrator: All agents executed consensus tool successfully\n");
            break;
        }

        // Sleep briefly to allow IRC thread to process responses
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check timeout
        if (std::chrono::steady_clock::now() - start_time > timeout) {
            ORCH_LOG("Orchestrator: WARNING - Timeout waiting for manual tool execution responses\n");
            
            // For agents that didn't respond, send fallback message
            std::lock_guard<std::mutex> lock(manual_tool_mutex_);
            for (const auto& [agent_id, responded] : manual_tool_responses_) {
                if (!responded) {
                    std::string fallback = std::format(
                        "[SYSTEM] FOR AGENT: {} ONLY! Manual tool execution failed. Please apply the agreed consensus: {} with parameters: {}",
                        agent_id, tool_name, parameters.dump(2)
                    );
                    
                    // Send as a regular message that will be injected as user message
                    if (irc_client_) {
                        irc_client_->send_message(channel, fallback);
                    }
                }
            }
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    // Verify consensus was applied correctly
    ea_t address = 0;
    if (parameters.contains("address")) {
        try {
            if (parameters["address"].is_string()) {
                address = std::stoull(parameters["address"].get<std::string>(), nullptr, 0);
            } else if (parameters["address"].is_number()) {
                address = parameters["address"].get<ea_t>();
            }
        } catch (...) {
            ORCH_LOG("Orchestrator: Could not extract address for verification\n");
        }
    }
    
    if (address != 0) {
        // Give a moment for database writes to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        bool verified = verify_consensus_applied(agents, address);
        if (verified) {
            ORCH_LOG("Orchestrator: Consensus enforcement verified successfully\n");
        } else {
            ORCH_LOG("Orchestrator: WARNING - Consensus enforcement verification failed\n");
        }
    }
}

void Orchestrator::handle_manual_tool_result(const std::string& message) {
    // Parse result: MANUAL_TOOL_RESULT | <agent_id>|<success/failure>|<result_json>
    if (message.find("MANUAL_TOOL_RESULT | ") != 0) {
        return;
    }
    
    std::string content = message.substr(21);  // Skip "MANUAL_TOOL_RESULT | "
    size_t first_delim = content.find('|');
    if (first_delim == std::string::npos) return;
    
    size_t second_delim = content.find('|', first_delim + 1);
    if (second_delim == std::string::npos) return;
    
    std::string agent_id = content.substr(0, first_delim);
    std::string status = content.substr(first_delim + 1, second_delim - first_delim - 1);
    std::string result_json = content.substr(second_delim + 1);

    ORCH_LOG("Orchestrator: Received manual tool result from '%s': %s\n", agent_id.c_str(), status.c_str());

    // Mark agent as responded
    {
        std::lock_guard<std::mutex> lock(manual_tool_mutex_);
        if (manual_tool_responses_.find(agent_id) != manual_tool_responses_.end()) {
            manual_tool_responses_[agent_id] = true;
            ORCH_LOG("Orchestrator: Marked agent '%s' as responded\n", agent_id.c_str());
        } else {
            ORCH_LOG("Orchestrator: WARNING - Agent '%s' not found in tracking map\n", agent_id.c_str());
        }
    }

    // Debug logging to check what agents we're tracking AFTER update
    {
        std::lock_guard<std::mutex> lock(manual_tool_mutex_);
        ORCH_LOG("Orchestrator: Current response status:\n");
        for (const auto& [id, responded] : manual_tool_responses_) {
            ORCH_LOG("  - '%s': %s\n", id.c_str(), responded ? "responded" : "waiting");
        }
    }
    
    // Parse and log the result details
    try {
        json result = json::parse(result_json);
        if (result["success"]) {
            ORCH_LOG("Orchestrator: Agent %s successfully executed manual tool\n", agent_id.c_str());
        } else {
            ORCH_LOG("Orchestrator: Agent %s failed manual tool execution: %s\n", 
                     agent_id.c_str(), result.value("error", "unknown error").c_str());
        }
    } catch (const std::exception& e) {
        ORCH_LOG("Orchestrator: Failed to parse result JSON: %s\n", e.what());
    }
}

bool Orchestrator::verify_consensus_applied(const std::set<std::string>& agents, ea_t address) {
    ORCH_LOG("Orchestrator: Verifying consensus was applied by all agents at address 0x%llx\n", address);
    
    if (!tool_tracker_) {
        ORCH_LOG("Orchestrator: ERROR - Tool tracker not initialized\n");
        return false;
    }
    
    // Get all manual tool calls at this address
    std::vector<ToolCall> calls = tool_tracker_->get_address_tool_calls(address);
    
    // Filter for manual calls from our agents
    std::map<std::string, json> agent_params;
    for (const auto& call : calls) {
        if (agents.find(call.agent_id) != agents.end() && 
            call.parameters.contains("__is_manual") && 
            call.parameters["__is_manual"]) {
            
            // Remove metadata fields before comparison
            json clean_params = call.parameters;
            clean_params.erase("__is_manual");
            clean_params.erase("__enforced_by");
            
            agent_params[call.agent_id] = clean_params;
        }
    }
    
    // Check if all agents have the same parameters
    if (agent_params.empty()) {
        ORCH_LOG("Orchestrator: WARNING - No manual tool calls found for verification\n");
        return false;
    }
    
    json reference_params;
    for (const auto& [agent_id, params] : agent_params) {
        if (reference_params.is_null()) {
            reference_params = params;
        } else if (params != reference_params) {
            ORCH_LOG("Orchestrator: ERROR - Agent %s applied different parameters: %s vs %s\n",
                     agent_id.c_str(), params.dump().c_str(), reference_params.dump().c_str());
            return false;
        }
    }
    
    ORCH_LOG("Orchestrator: SUCCESS - All %zu agents applied identical values\n", agent_params.size());
    return true;
}

void Orchestrator::log_token_usage(const claude::TokenUsage& per_iteration_usage, const claude::TokenUsage& cumulative_usage) {
    // Use cumulative for totals display
    json tokens_json = {
        {"input_tokens", cumulative_usage.input_tokens},
        {"output_tokens", cumulative_usage.output_tokens},
        {"cache_read_tokens", cumulative_usage.cache_read_tokens},
        {"cache_creation_tokens", cumulative_usage.cache_creation_tokens},
        {"estimated_cost", cumulative_usage.estimated_cost()},
        {"model", model_to_string(cumulative_usage.model)}
    };
    
    // Use per-iteration for context calculation (like agents do)
    json session_tokens_json = {
        {"input_tokens", per_iteration_usage.input_tokens},
        {"output_tokens", per_iteration_usage.output_tokens},
        {"cache_read_tokens", per_iteration_usage.cache_read_tokens},
        {"cache_creation_tokens", per_iteration_usage.cache_creation_tokens}
    };
    
    ORCH_LOG("DEBUG: Publishing token event - Cumulative In: %d, Out: %d | Per-iter In: %d, Out: %d\n",
        cumulative_usage.input_tokens, cumulative_usage.output_tokens,
        per_iteration_usage.input_tokens, per_iteration_usage.output_tokens);
    
    // Emit standardized token event for orchestrator (use AGENT_TOKEN_UPDATE for consistency)
    event_bus_.publish(AgentEvent(AgentEvent::AGENT_TOKEN_UPDATE, "orchestrator", {
        {"agent_id", "orchestrator"},
        {"tokens", tokens_json},
        {"session_tokens", session_tokens_json}  // Per-iteration for context calc
    }));
    
    ORCH_LOG("Orchestrator: Token usage - Input: %d, Output: %d (cumulative)\n",
        cumulative_usage.input_tokens, cumulative_usage.output_tokens);
}

std::vector<std::string> Orchestrator::get_irc_channels() const {
    if (irc_server_) {
        return irc_server_->list_channels();
    }
    return {};
}

void Orchestrator::shutdown() {
    if (shutting_down_) return;
    shutting_down_ = true;

    ORCH_LOG("Orchestrator: Shutting down...\n");

    // Stop conflict monitor thread
    conflict_monitor_should_stop_ = true;
    if (conflict_monitor_thread_.joinable()) {
        ORCH_LOG("Orchestrator: Waiting for conflict monitor thread to exit...\n");
        conflict_monitor_thread_.join();
    }

    // Stop MCP listener if running
    if (!show_ui_) {
        mcp_listener_should_stop_ = true;
        if (mcp_listener_thread_.joinable()) {
            mcp_listener_thread_.join();
        }

        // Close pipes (only if not already closed)
        if (mcp_input_fd_ >= 0) {
            close(mcp_input_fd_);
            mcp_input_fd_ = -1;
        }
        if (mcp_output_fd_ >= 0) {
            close(mcp_output_fd_);
            mcp_output_fd_ = -1;
        }
    }

    // Terminate all agents
    for (auto& [id, info] : agents_) {
        agent_spawner_->terminate_agent(info.process_id);
    }
    
    // Disconnect IRC client
    if (irc_client_) {
        irc_client_->disconnect();
    }
    
    // Stop IRC server
    if (irc_server_) {
        irc_server_->stop();
    }
    
    // Cleanup subsystems
    tool_tracker_.reset();
    merge_manager_.reset();
    agent_spawner_.reset();
    db_manager_.reset();

    ORCH_LOG("Orchestrator: Shutdown complete\n");
}


bool Orchestrator::should_consolidate_context() const {
    if (consolidation_state_.consolidation_in_progress) {
        return false;  // Already consolidating
    }
    
    // Estimate total tokens in conversation history
    size_t total_tokens = 0;
    for (const auto& msg : conversation_history_) {
        std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(msg);
        if (text) {
            total_tokens += text->length() / 4;  // Simple token estimation
        }
        
        // Add tokens for tool calls and results
        auto tool_calls = claude::messages::ContentExtractor::extract_tool_uses(msg);
        for (const auto* tool : tool_calls) {
            total_tokens += tool->name.length() / 4;
            total_tokens += tool->input.dump().length() / 4;
        }
    }
    
    return total_tokens > config_.agent.context_limit;
}

void Orchestrator::consolidate_conversation_context() {
    ORCH_LOG("Orchestrator: Starting context consolidation...\n");
    
    consolidation_state_.consolidation_in_progress = true;
    consolidation_state_.consolidation_count++;
    consolidation_state_.last_consolidation = std::chrono::steady_clock::now();
    
    // Create consolidation summary
    std::string summary = create_orchestrator_consolidation_summary(conversation_history_);
    
    // Replace conversation history with just the summary
    conversation_history_.clear();
    conversation_history_.push_back(claude::messages::Message::user_text(current_user_task_));
    conversation_history_.push_back(claude::messages::Message::assistant_text(summary));
    
    consolidation_state_.consolidation_in_progress = false;
    
    ORCH_LOG("Orchestrator: Context consolidation complete (consolidation #%d)\n", 
        consolidation_state_.consolidation_count);
}

std::string Orchestrator::create_orchestrator_consolidation_summary(const std::vector<claude::messages::Message>& conversation) const {
    // Send conversation to Claude for summarization
    claude::ChatRequestBuilder builder;
    builder.with_model(claude::Model::Sonnet45)  // Use Sonnet for consolidation
           .with_system_prompt(ORCHESTRATOR_CONSOLIDATION_PROMPT)
           .with_max_tokens(64000)
           .with_max_thinking_tokens(12000)
           .with_temperature(1.0)
           .enable_thinking(true);
    
    // Add consolidation prompt
    builder.add_message(claude::messages::Message::user_text("You are consolidating an orchestrator's conversation history."));
    
    // Add conversation history (excluding the current consolidation request)
    for (size_t i = 0; i < conversation.size(); ++i) {
        builder.add_message(conversation[i]);
    }

    claude::ChatResponse response = api_client_->send_request(builder.build());
    
    if (response.success) {
        std::optional<std::string> summary_text = claude::messages::ContentExtractor::extract_text(response.message);
        if (summary_text) {
            return "=== ORCHESTRATOR CONTEXT CONSOLIDATION ===\n\n" + *summary_text;
        }
    }
    
    // Fallback summary if Claude request fails
    return std::format(
        "=== ORCHESTRATOR CONTEXT CONSOLIDATION ===\n\n"
        "User Task: {}\n"
        "Agents Spawned: {}\n"
        "Consolidation Count: {}\n"
        "Note: Full consolidation failed, using fallback summary.",
        current_user_task_,
        agents_.size(),
        consolidation_state_.consolidation_count
    );
}

void Orchestrator::handle_tool_call_event(const AgentEvent& event) {
    // Extract tool call data from event
    if (!event.payload.contains("tool_name") || !event.payload.contains("agent_id")) {
        return;
    }

    std::string tool_name = event.payload["tool_name"];
    std::string agent_id = event.payload["agent_id"];
    ea_t address = event.payload.value("address", BADADDR);
    json parameters = event.payload.value("parameters", json::object());

    // Handle code injection tool calls
    if (tool_name == "allocate_code_workspace") {
        // Extract allocation details from parameters
        if (parameters.contains("temp_address") && parameters.contains("allocated_size")) {
            ea_t start_addr = parameters["temp_address"];
            size_t size = parameters["allocated_size"];
            ea_t end_addr = start_addr + size;

            // Create no-go zone
            NoGoZone zone;
            zone.start_address = start_addr;
            zone.end_address = end_addr;
            zone.agent_id = agent_id;
            zone.type = NoGoZoneType::TEMP_SEGMENT;
            zone.timestamp = std::chrono::system_clock::now();

            // Add to manager
            nogo_zone_manager_->add_zone(zone);

            // Broadcast to all agents
            broadcast_no_go_zone(zone);

            ORCH_LOG("Orchestrator: Broadcasted temp segment no-go zone from %s: 0x%llX-0x%llX\n",
                agent_id.c_str(), (uint64_t)start_addr, (uint64_t)end_addr);
        }
    }
    else if (tool_name == "finalize_code_injection") {
        // Check if a code cave was used
        if (parameters.contains("relocation_method") &&
            parameters["relocation_method"] == "code_cave" &&
            parameters.contains("new_permanent_address") &&
            parameters.contains("code_size")) {

            ea_t cave_addr = parameters["new_permanent_address"];
            size_t size = parameters["code_size"];

            // Create no-go zone for the used code cave
            NoGoZone zone;
            zone.start_address = cave_addr;
            zone.end_address = cave_addr + size;
            zone.agent_id = agent_id;
            zone.type = NoGoZoneType::CODE_CAVE;
            zone.timestamp = std::chrono::system_clock::now();

            // Add to manager
            nogo_zone_manager_->add_zone(zone);

            // Broadcast to all agents
            broadcast_no_go_zone(zone);

            ORCH_LOG("Orchestrator: Broadcasted code cave no-go zone from %s: 0x%llX-0x%llX\n",
                agent_id.c_str(), (uint64_t)cave_addr, (uint64_t)(cave_addr + size));
        }
    }
    // Handle patch tool calls for instant replication
    else if (tool_name == "patch_bytes" || tool_name == "patch_assembly" ||
             tool_name == "revert_patch" || tool_name == "revert_all") {

        // Create a ToolCall structure
        ToolCall call;
        call.agent_id = agent_id;
        call.tool_name = tool_name;
        call.address = address;
        call.parameters = parameters;
        call.timestamp = std::chrono::system_clock::now();
        call.is_write_operation = true;

        // Replicate to all other agents
        replicate_patch_to_agents(agent_id, call);

        ORCH_LOG("Orchestrator: Replicating %s from %s to all other agents\n",
            tool_name.c_str(), agent_id.c_str());
    }
}

void Orchestrator::broadcast_no_go_zone(const NoGoZone& zone) {
    // Serialize the zone
    std::string message = NoGoZoneManager::serialize_zone(zone);

    // Broadcast to all agents via IRC
    if (irc_client_ && irc_client_->is_connected()) {
        irc_client_->send_message("#agents", message);
        ORCH_LOG("Orchestrator: Broadcasted no-go zone via IRC: %s\n", message.c_str());
    } else {
        ORCH_LOG("Orchestrator: WARNING - Could not broadcast no-go zone, IRC not connected\n");
    }
}

void Orchestrator::replicate_patch_to_agents(const std::string& source_agent, const ToolCall& call) {
    // Get all active agents except the source
    for (const auto& [agent_id, agent_info] : agents_) {
        if (agent_id == source_agent) {
            continue;  // Skip the agent that made the patch
        }

        // Get the agent's database path
        std::string agent_db = db_manager_->get_agent_database(agent_id);
        if (agent_db.empty()) {
            ORCH_LOG("Orchestrator: Could not find database for agent %s\n", agent_id.c_str());
            continue;
        }

        // Prepare modified parameters with prefixed description
        json modified_params = call.parameters;
        if (modified_params.contains("description")) {
            std::string original_desc = modified_params["description"];
            modified_params["description"] = "[" + source_agent + "]: " + original_desc;
        } else {
            modified_params["description"] = "[" + source_agent + "]: Replicated patch";
        }

        // Execute the tool on the agent's database
        // Note: This is a simplified version - in practice, we'd need to execute
        // the tool in the context of the agent's database
        // For now, broadcast via IRC for the agent to handle

        std::string patch_msg = std::format("PATCH|{}|{}|{:#x}|{}",
            call.tool_name, source_agent, call.address, modified_params.dump());

        if (irc_client_ && irc_client_->is_connected()) {
            // Send to specific agent channel
            std::string agent_channel = "#agent_" + agent_id;
            irc_client_->send_message(agent_channel, patch_msg);
            ORCH_LOG("Orchestrator: Sent patch replication to %s\n", agent_id.c_str());
        }
    }
}

} // namespace llm_re::orchestrator