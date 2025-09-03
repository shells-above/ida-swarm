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
    
    // Initialize logger first
    g_orch_logger.initialize(binary_name_);
    ORCH_LOG("Orchestrator: Initializing for binary: %s\n", binary_name_.c_str());
    
    // Create subsystems with binary name
    db_manager_ = std::make_unique<DatabaseManager>(main_db_path, binary_name_);
    agent_spawner_ = std::make_unique<AgentSpawner>(config, binary_name_);
    tool_tracker_ = std::make_unique<ToolCallTracker>(binary_name_);
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
    
    // Register orchestrator tools
    register_orchestrator_tools(tool_registry_, this);
}

Orchestrator::~Orchestrator() {
    shutdown();
}

bool Orchestrator::initialize() {
    if (initialized_) return true;
    
    ORCH_LOG("Orchestrator: Initializing subsystems...\n");
    
    // Clean up any existing workspace directory from previous runs
    std::filesystem::path workspace_dir = std::filesystem::path("/tmp/ida_swarm_workspace") / binary_name_;
    if (std::filesystem::exists(workspace_dir)) {
        ORCH_LOG("Orchestrator: Cleaning up previous workspace: %s\n", workspace_dir.string().c_str());
        try {
            std::filesystem::remove_all(workspace_dir);
            ORCH_LOG("Orchestrator: Successfully removed previous workspace\n");
        } catch (const std::exception& e) {
            ORCH_LOG("Orchestrator: Warning - failed to clean previous workspace: %s\n", e.what());
        }
    }
    
    // Ignore SIGPIPE to prevent crashes when IRC connections break
    signal(SIGPIPE, SIG_IGN);
    ORCH_LOG("Orchestrator: Configured SIGPIPE handler\n");
    
    // Initialize tool tracker database
    if (!tool_tracker_->initialize()) {
        ORCH_LOG("Orchestrator: Failed to initialize tool tracker\n");
        return false;
    }
    
    // Start IRC server for agent communication with binary name
    fs::path idb_path(main_database_path_);
    std::string binary_name = idb_path.stem().string();
    irc_server_ = std::make_unique<irc::IRCServer>(config_.irc.port, binary_name);
    if (!irc_server_->start()) {
        ORCH_LOG("Orchestrator: Failed to start IRC server\n");
        return false;
    }
    
    ORCH_LOG("Orchestrator: IRC server started on port %d\n", config_.irc.port);
    
    // Connect IRC client for orchestrator communication
    irc_client_ = std::make_unique<irc::IRCClient>("orchestrator", config_.irc.server, config_.irc.port);
    if (!irc_client_->connect()) {
        ORCH_LOG("Orchestrator: Failed to connect IRC client to %s:%d\n", 
            config_.irc.server.c_str(), config_.irc.port);
        return false;
    }
    
    // Join standard orchestrator channels
    irc_client_->join_channel("#agents");
    irc_client_->join_channel("#agent_query");
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

void Orchestrator::process_user_input(const std::string& input) {
    current_user_task_ = input;
    
    // Clear any completed agents and results from previous tasks
    completed_agents_.clear();
    agent_results_.clear();
    
    // Initialize conversation history for new task
    conversation_history_.clear();
    conversation_history_.push_back(claude::messages::Message::user_text(input));
    
    ORCH_LOG("Orchestrator: Processing task: %s\n", input.c_str());

    // Emit thinking event
    event_bus_.publish(AgentEvent(AgentEvent::ORCHESTRATOR_THINKING, "orchestrator", {}));
    
    // Send to Claude API with deep thinking
    auto response = send_orchestrator_request(input);
    
    if (!response.success) {
        ORCH_LOG("Orchestrator: Failed to process request: %s\n", 
            response.error ? response.error->c_str() : "Unknown error");
        return;
    }
    
    // Display orchestrator's response
    std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(response.message);
    if (text) {
        ORCH_LOG("Orchestrator: %s\n", text->c_str());
        
        // Emit orchestrator response event
        event_bus_.publish(AgentEvent(AgentEvent::ORCHESTRATOR_RESPONSE, "orchestrator", {
            {"response", *text}
        }));
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
            
            // Display text if present
            auto cont_text = claude::messages::ContentExtractor::extract_text(continuation.message);
            if (cont_text) {
                ORCH_LOG("Orchestrator: %s\n", cont_text->c_str());
            }
            
            // Process any tool calls in the continuation
            std::vector<claude::messages::Message> cont_tool_results = process_orchestrator_tools(continuation.message);
            
            // If no more tool calls, we're done
            if (cont_tool_results.empty()) {
                break;
            }
            
            // Add continuation and its tool results to conversation history
            conversation_history_.push_back(continuation.message);
            for (const auto& result : cont_tool_results) {
                conversation_history_.push_back(result);
            }
            
            ORCH_LOG("Orchestrator: Processed %zu more tool calls, continuing conversation...\n", 
                cont_tool_results.size());
        }
    }
    
    // Update token stats
    token_stats_.add_usage(response.usage);
}

claude::ChatResponse Orchestrator::send_orchestrator_request(const std::string& user_input) {
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
    
    return api_client_->send_request(builder.build());
}

std::vector<claude::messages::Message> Orchestrator::process_orchestrator_tools(const claude::messages::Message& msg) {
    std::vector<claude::messages::Message> results;
    std::vector<const claude::messages::ToolUseContent*> tool_calls = claude::messages::ContentExtractor::extract_tool_uses(msg);
    
    // First pass: Execute all tools and collect spawn_agent results
    std::map<std::string, std::string> tool_to_agent; // tool_id -> agent_id
    std::vector<std::string> spawned_agent_ids;
    
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
            // Execute other tools normally and add to results immediately
            ORCH_LOG("Orchestrator: Executing non-spawn_agent tool: %s\n", tool_use->name.c_str());
            results.push_back(tool_registry_.execute_tool_call(*tool_use));
        }
    }
    
    // If we spawned any agents, wait for ALL of them to complete
    if (!spawned_agent_ids.empty()) {
        ORCH_LOG("Orchestrator: Waiting for %zu agents to complete their tasks...\n", spawned_agent_ids.size());
        wait_for_agents_completion(spawned_agent_ids);
        ORCH_LOG("Orchestrator: All %zu agents have completed\n", spawned_agent_ids.size());
    }
    
    // Second pass: Create enriched results for spawn_agent calls
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

                    auto result_msg = claude::messages::Message(claude::messages::Role::User);;
                    result_msg.add_content(std::make_unique<claude::messages::ToolResultContent>(
                        tool_use->id, result_json.dump(), false
                    ));
                    results.push_back(result_msg);
                    
                    ORCH_LOG("Orchestrator: Added spawn_agent result with report for %s\n", agent_id.c_str());
                } else {
                    // Create error result
                    json error_json = {
                        {"error", "Failed to spawn agent"}
                    };
                    
                    auto result_msg = claude::messages::Message(claude::messages::Role::User);;
                    result_msg.add_content(std::make_unique<claude::messages::ToolResultContent>(
                        tool_use->id, error_json.dump(), true  // is_error = true
                    ));
                    results.push_back(result_msg);
                    
                    ORCH_LOG("Orchestrator: Added spawn_agent error result\n");
                }
            }
        }
    }
    
    return results;
}

json Orchestrator::spawn_agent_async(const std::string& task, const std::string& context) {
    ORCH_LOG("Orchestrator: Spawning agent (async) for task: %s\n", task.c_str());
    
    // Generate agent ID
    std::string agent_id = std::format("agent_{}", next_agent_id_++);
    
    // Emit agent spawning event
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
        {"irc_port", config_.irc.port}
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
    
    if (!active_agents.empty()) {
        prompt += R"(

CURRENTLY ACTIVE AGENTS:
)";
        for (const auto& [agent_id, agent_task] : active_agents) {
            prompt += "- " + agent_id + " (working on: " + agent_task + ")\n";
        }
        prompt += R"(
You can see what each agent is working on above. Use this information to:
- Share relevant findings with agents working on related tasks
- Coordinate when your tasks overlap or depend on each other
)";
    } else {
        prompt += R"(

You are currently the only active agent.
- Other agents may join later and will be announced via IRC
)";
    }
    
    prompt += R"(

COLLABORATION CAPABILITIES:
- You are connected to IRC for real-time communication
- New agents joining are announced via AGENT_JOIN messages
- Agents leaving are announced via AGENT_LEAVE messages
- Use ask_agent tool to ask specific agents questions
- Use broadcast_finding tool to share important discoveries
- Join IRC channels for discussions (e.g., #agents, #conflict_*)

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
- Share findings that might be relevant to other agents' tasks

TASK COMPLETION PROTOCOL:
When you have thoroughly analyzed your assigned task and gathered sufficient evidence:
1. Store ALL your key findings using the store_analysis tool
2. Broadcast a final summary of your discoveries (ONE TIME ONLY)
3. Send a comprehensive final report as a regular message with NO tool calls

CRITICAL COMPLETION RULES:
- Your FINAL message must contain NO tool calls - this triggers task completion
- Once you send a message without tools, you are declaring your work DONE
- The system will automatically handle your exit once you send a message without tools
- DO NOT respond to acknowledgments, celebrations, or congratulations from other agents
- DO NOT engage in celebration loops or continue broadcasting after your final summary
- If you see other agents celebrating or acknowledging, ignore these messages
- Focus on YOUR task - complete it thoroughly, report once, then stop

Remember: You're part of a team. Collaborate effectively, but know when your work is complete.
When ready to finish, simply send your final analysis as a message WITHOUT any tool calls.

Begin your analysis now.)";
    
    return prompt;
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

void Orchestrator::handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message) {
    // Only interested in messages on the #results channel
    if (channel != "#results") {
        return;
    }
    
    // Parse AGENT_RESULT messages
    if (message.find("AGENT_RESULT:") == 0) {
        // Format: AGENT_RESULT:{json}
        std::string json_str = message.substr(13);  // Skip "AGENT_RESULT:"
        
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
            
            // Find the agent info
            auto it = agents_.find(agent_id);
            if (it != agents_.end()) {
                // Display the agent's result to the user
                ORCH_LOG("===========================================\n");
                ORCH_LOG("Agent %s completed task: %s\n", agent_id.c_str(), it->second.task.c_str());
                ORCH_LOG("Result: %s\n", report.c_str());
                ORCH_LOG("===========================================\n");
            }
        } catch (const std::exception& e) {
            ORCH_LOG("Orchestrator: Failed to parse agent result JSON: %s\n", e.what());
        }
    }
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