#include "orchestrator.h"
#include "orchestrator_tools.h"
#include "orchestrator_logger.h"
#include "../sdk/auth/oauth_manager.h"
#include <iostream>
#include <format>
#include <thread>
#include <chrono>
#include <set>
#include <filesystem>
#include <signal.h>

namespace fs = std::filesystem;

namespace llm_re::orchestrator {

Orchestrator::Orchestrator(const Config& config, const std::string& main_db_path)
    : config_(config), main_database_path_(main_db_path) {
    
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
    
    // Set up message callback to receive agent results
    irc_client_->set_message_callback(
        [this](const std::string& channel, const std::string& sender, const std::string& message) {
            handle_irc_message(channel, sender, message);
        }
    );
    
    ORCH_LOG("Orchestrator: IRC client connected\n");
    
    // Initialize database manager
    if (!db_manager_->initialize()) {
        ORCH_LOG("Orchestrator: Failed to initialize database manager\n");
        return false;
    }
    
    initialized_ = true;
    ORCH_LOG("Orchestrator: Initialization complete\n");
    return true;
}

void Orchestrator::start_interactive_session() {
    // Use IDA's ask_str dialog to get user input
    qstring user_input;
    if (ask_str(&user_input, 0, "What would you like me to investigate?")) {
        if (!user_input.empty()) {
            // Emit user input event
            event_bus_.publish(AgentEvent(AgentEvent::ORCHESTRATOR_INPUT, "orchestrator", {
                {"input", user_input.c_str()}
            }));
            
            process_user_input(user_input.c_str());
        }
    }
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
            ORCH_LOG("Orchestrator: Publishing ORCHESTRATOR_RESPONSE event (no tools)\n");
            event_bus_.publish(AgentEvent(AgentEvent::ORCHESTRATOR_RESPONSE, "orchestrator", {
                {"response", *text}
            }));
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
            claude::ChatRequestBuilder builder;
            builder.with_model(config_.orchestrator.model.model)
                   .with_system_prompt(ORCHESTRATOR_SYSTEM_PROMPT)
                   .with_max_tokens(config_.orchestrator.model.max_tokens)
                   .with_max_thinking_tokens(config_.orchestrator.model.max_thinking_tokens)
                   .with_temperature(config_.orchestrator.model.temperature)
                   .enable_thinking(config_.orchestrator.model.enable_thinking)
                   .enable_interleaved_thinking(false);
            
            if (tool_registry_.has_tools()) {
                builder.with_tools(tool_registry_);
            }
            
            // Add conversation history
            for (const auto& msg : conversation_history_) {
                builder.add_message(msg);
            }
            
            auto continuation = api_client_->send_request(builder.build());
            if (!continuation.success) {
                ORCH_LOG("Orchestrator: Failed to get continuation: %s\n",
                    continuation.error ? continuation.error->c_str() : "Unknown error");
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
                // Publish the final response to UI before breaking
                if (cont_text && !cont_text->empty()) {
                    event_bus_.publish(AgentEvent(AgentEvent::ORCHESTRATOR_RESPONSE, "orchestrator", {
                        {"response", *cont_text}
                    }));
                }
                // Log token usage after final response (pass per-iteration for context calc)
                log_token_usage(continuation.usage, token_stats_.get_total());
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
    
    // Check for OAuth token expiry and retry if needed
    if (!response.success && response.error && 
        (response.error->find("401") != std::string::npos || 
         response.error->find("unauthorized") != std::string::npos)) {
        ORCH_LOG("Orchestrator: Got 401, attempting OAuth token refresh...\n");
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
    
    // Check for OAuth token expiry and retry if needed
    if (!response.success && response.error && 
        response.error->find("OAuth token has expired") != std::string::npos) {
        
        ORCH_LOG("Orchestrator: OAuth token expired, attempting to refresh...\n");
        
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

Remember: You're part of a team. Collaborate effectively, but know when your work is complete.
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
    
    // Check if refresh is needed
    if (oauth_manager_->needs_refresh()) {
        ORCH_LOG("Orchestrator: OAuth token needs refresh, refreshing...\n");
        
        auto refreshed_creds = oauth_manager_->force_refresh();
        if (!refreshed_creds) {
            ORCH_LOG("Orchestrator: Failed to refresh OAuth token: %s\n", 
                oauth_manager_->get_last_error().c_str());
            return false;
        }
        
        // Update the API client with new credentials
        api_client_->set_oauth_credentials(refreshed_creds);
        ORCH_LOG("Orchestrator: Successfully refreshed OAuth token\n");
        
        // Emit event for UI update
        event_bus_.publish(AgentEvent(AgentEvent::ORCHESTRATOR_RESPONSE, "orchestrator", {
            {"token_refreshed", true},
            {"expires_at", refreshed_creds->expires_at}
        }));
    }
    
    return true;
}

void Orchestrator::handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message) {
    ORCH_LOG("DEBUG: IRC message received - Channel: %s, Sender: %s, Message: %s\n", 
        channel.c_str(), sender.c_str(), message.c_str());
    // Emit all IRC messages to the UI for display
    event_bus_.publish(AgentEvent(AgentEvent::MESSAGE, sender, {
        {"channel", channel},
        {"message", message}
    }));
    
    // Check if this is a conflict channel message
    if (channel.find("#conflict_") == 0) {
        handle_conflict_message(channel, sender, message);
        return;
    }
    
    // Handle resurrection requests
    if (message.find("RESURRECT_AGENT|") == 0) {
        // Format: RESURRECT_AGENT|agent_id|channel|conflict_type
        std::string request = message.substr(16);
        size_t first_pipe = request.find('|');
        size_t second_pipe = request.find('|', first_pipe + 1);
        
        if (first_pipe != std::string::npos && second_pipe != std::string::npos) {
            std::string target_agent = request.substr(0, first_pipe);
            std::string conflict_channel = request.substr(first_pipe + 1, second_pipe - first_pipe - 1);
            std::string conflict_type = request.substr(second_pipe + 1);
            
            ORCH_LOG("Orchestrator: Resurrection request for agent %s to join %s\n",
                target_agent.c_str(), conflict_channel.c_str());
            
            // Check if agent is dormant
            if (db_manager_->is_agent_dormant(target_agent)) {
                ORCH_LOG("Orchestrator: Agent %s is dormant, resurrecting...\n", target_agent.c_str());
                
                // Restore the dormant agent's database
                std::string db_path = db_manager_->restore_dormant_agent(target_agent);
                if (db_path.empty()) {
                    ORCH_LOG("Orchestrator: Failed to restore dormant agent %s\n", target_agent.c_str());
                    return;
                }
                
                // Create resurrection config
                json resurrection_config = {
                    {"reason", "conflict_resolution"},
                    {"conflict_channel", conflict_channel},
                    {"conflict", {
                        {"type", conflict_type},
                        {"requesting_agent", sender}
                    }}
                };
                
                // Resurrect the agent
                int pid = agent_spawner_->resurrect_agent(target_agent, db_path, resurrection_config);
                if (pid > 0) {
                    ORCH_LOG("Orchestrator: Successfully resurrected agent %s (PID %d)\n",
                        target_agent.c_str(), pid);
                    
                    // Track the resurrected agent
                    AgentInfo info;
                    info.agent_id = target_agent;
                    info.task = "Conflict Resolution";
                    info.database_path = db_path;
                    info.process_id = pid;
                    agents_[target_agent] = info;
                } else {
                    ORCH_LOG("Orchestrator: Failed to resurrect agent %s\n", target_agent.c_str());
                }
            } else {
                ORCH_LOG("Orchestrator: Agent %s is not dormant or doesn't exist\n", target_agent.c_str());
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
            
            ORCH_LOG("DEBUG: Parsed agent_id=%s, tokens=%s, session=%s\n", 
                agent_id.c_str(), tokens.dump().c_str(), session_tokens.dump().c_str());
            
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

void Orchestrator::handle_conflict_message(const std::string& channel, const std::string& sender, const std::string& message) {
    std::lock_guard<std::mutex> lock(conflicts_mutex_);
    
    // Initialize conflict session if it doesn't exist
    if (active_conflicts_.find(channel) == active_conflicts_.end()) {
        ConflictSession session;
        session.channel = channel;
        session.started = std::chrono::steady_clock::now();
        active_conflicts_[channel] = session;
        
        // Join the conflict channel to monitor it
        if (irc_client_) {
            irc_client_->join_channel(channel);
        }
        
        ORCH_LOG("Orchestrator: Monitoring new conflict channel %s\n", channel.c_str());
    }
    
    ConflictSession& session = active_conflicts_[channel];
    
    // Track AGREE messages from agents
    if (message.find("AGREE:") == 0) {
        std::string agreement = message.substr(6);
        
        // Add agent to participating set
        session.participating_agents.insert(sender);
        
        // Store agreement
        session.agreements[sender] = agreement;
        
        ORCH_LOG("Orchestrator: Agent %s agreed to: %s (channel %s)\n", 
                 sender.c_str(), agreement.c_str(), channel.c_str());
        
        // Check if we should invoke the grader
        check_conflict_consensus(channel);
        
    } else if (message.find("DISAGREE:") == 0) {
        // Agent disagrees - track participation but no agreement yet
        session.participating_agents.insert(sender);
        session.agreements.erase(sender);  // Remove any previous agreement
        
        ORCH_LOG("Orchestrator: Agent %s disagrees in channel %s\n", 
                 sender.c_str(), channel.c_str());
    }
}

void Orchestrator::check_conflict_consensus(const std::string& channel) {
    auto it = active_conflicts_.find(channel);
    if (it == active_conflicts_.end()) return;
    
    ConflictSession& session = it->second;
    
    // Don't invoke grader multiple times
    if (session.grader_invoked) return;
    
    // Check if all participating agents have agreed
    bool all_agreed = true;
    for (const auto& agent : session.participating_agents) {
        if (session.agreements.find(agent) == session.agreements.end()) {
            all_agreed = false;
            break;
        }
    }
    
    if (all_agreed && !session.participating_agents.empty()) {
        ORCH_LOG("Orchestrator: All %zu agents have agreed in %s, invoking grader\n",
                 session.participating_agents.size(), channel.c_str());
        
        session.grader_invoked = true;
        invoke_consensus_grader(channel);
    }
}

void Orchestrator::invoke_consensus_grader(const std::string& channel) {
    auto it = active_conflicts_.find(channel);
    if (it == active_conflicts_.end()) return;
    
    const ConflictSession& session = it->second;
    
    ORCH_LOG("Orchestrator: Invoking Haiku 3.5 grader for channel %s\n", channel.c_str());
    
    // Build grader prompt
    std::string prompt = "Multiple agents discussed a conflict and reached the following agreements:\n\n";
    
    for (const auto& [agent, agreement] : session.agreements) {
        prompt += agent + ": " + agreement + "\n\n";
    }
    
    prompt += "Determine if these agents have reached consensus on the same solution. ";
    prompt += "Look at the semantic meaning, not exact wording. ";
    prompt += "Respond with JSON only: {\"reasoning\": \"...\", \"consensus\": true/false}";
    
    try {
        // Create request for Haiku 3.5
        claude::ChatRequest request;
        request.model = claude::Model::Haiku35;
        request.max_tokens = 500;
        request.messages.push_back(claude::messages::Message::user_text(prompt));
        
        // Send to grader
        claude::ChatResponse response = api_client_->send_request(request);
        
        if (response.success) {
            auto text_opt = response.message.get_text();
            if (text_opt.has_value()) {
                std::string grader_response = *text_opt;
                ORCH_LOG("Orchestrator: Grader response: %s\n", grader_response.c_str());
                
                // Parse JSON response
                json result = json::parse(grader_response);
                bool consensus = result["consensus"];
                std::string reasoning = result["reasoning"];
                
                // Broadcast result to agents
                broadcast_grader_result(channel, consensus, reasoning);
                
                // Update session state
                if (consensus) {
                    active_conflicts_[channel].resolved = true;
                }
            }
        } else {
            std::string error_msg = response.error.value_or("Unknown error");
            ORCH_LOG("Orchestrator: Grader request failed: %s\n", error_msg.c_str());
            // Broadcast error to agents
            broadcast_grader_result(channel, false, "Grader invocation failed: " + error_msg);
        }
        
    } catch (const std::exception& e) {
        ORCH_LOG("Orchestrator: Exception invoking grader: %s\n", e.what());
        broadcast_grader_result(channel, false, "Grader error: " + std::string(e.what()));
    }
}

void Orchestrator::broadcast_grader_result(const std::string& channel, bool consensus, const std::string& reasoning) {
    // Format result message
    json result_json = {
        {"consensus", consensus},
        {"reasoning", reasoning}
    };
    
    std::string message = "GRADER_RESULT: " + result_json.dump();
    
    // Send to conflict channel
    if (irc_client_) {
        irc_client_->send_message(channel, message);
        ORCH_LOG("Orchestrator: Broadcast grader result to %s: consensus=%s\n", 
                 channel.c_str(), consensus ? "true" : "false");
    }
    
    // If consensus reached, clean up the conflict session after a delay
    if (consensus) {
        // Let agents process the result first
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        std::lock_guard<std::mutex> lock(conflicts_mutex_);
        active_conflicts_.erase(channel);
        
        // Leave the channel
        if (irc_client_) {
            irc_client_->leave_channel(channel);
        }
    }
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

void Orchestrator::shutdown() {
    if (shutting_down_) return;
    shutting_down_ = true;
    
    ORCH_LOG("Orchestrator: Shutting down...\n");
    
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
    builder.with_model(claude::Model::Sonnet4)  // Use Sonnet for consolidation
           .with_system_prompt("You are helping consolidate an orchestrator's conversation history.")
           .with_max_tokens(8000)
           .with_temperature(0.1)  // Low temperature for consistent summaries
           .enable_thinking(false);
    
    // Add consolidation prompt
    builder.add_message(claude::messages::Message::user_text(ORCHESTRATOR_CONSOLIDATION_PROMPT));
    
    // Add conversation history (excluding the current consolidation request)
    for (size_t i = 0; i < conversation.size(); ++i) {
        builder.add_message(conversation[i]);
    }
    
    auto response = api_client_->send_request(builder.build());
    
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

} // namespace llm_re::orchestrator