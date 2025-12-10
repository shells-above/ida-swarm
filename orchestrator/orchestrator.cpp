#include "orchestrator.h"
#include "orchestrator_tools.h"
#include "remote_sync_manager.h"
#include "../core/logger.h"
#include "../core/ssh_key_manager.h"
#include "../agent/consensus_executor.h"
#include <iostream>
#include <sstream>
#include <fstream>
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
#include <errno.h>

namespace fs = std::filesystem;

namespace llm_re::orchestrator {

// Helper function to read exactly N bytes from a file descriptor
// Handles partial reads and EINTR interrupts
static ssize_t read_exactly(int fd, void* buf, size_t count) {
    size_t total = 0;
    char* ptr = static_cast<char*>(buf);

    while (total < count) {
        ssize_t n = read(fd, ptr + total, count - total);
        if (n == 0) {
            // EOF reached before reading all bytes
            return (total > 0) ? total : 0;
        }
        if (n < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, retry
                continue;
            }
            // Real error
            return -1;
        }
        total += n;
    }
    return total;
}

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
    auto_decompile_manager_ = std::make_unique<AutoDecompileManager>(this);

    // Allocate unique IRC port based on binary name (needed for LLDB debugserver port)
    allocated_irc_port_ = allocate_unique_port();
    LOG_INFO("Orchestrator: Allocated IRC port %d (unique for %s)\n", allocated_irc_port_, binary_name_.c_str());

    // Initialize LLDB session manager if enabled
    if (config.lldb.enabled) {
        try {
            std::filesystem::path workspace_path = std::filesystem::path("/tmp/ida_swarm_workspace") / binary_name_;
            lldb_manager_ = std::make_unique<LLDBSessionManager>(
                config.lldb.lldb_path,
                workspace_path.string(),
                db_manager_.get(),
                allocated_irc_port_  // Pass IRC port for debugserver port auto-assignment
            );
            LOG("Orchestrator: LLDB debugging support initialized\n");
        } catch (const std::exception& e) {
            LOG("Orchestrator: Failed to initialize LLDB manager: %s\n", e.what());
            LOG("Orchestrator: LLDB debugging will be unavailable. Install LLDB or configure path in preferences.\n");
            lldb_manager_ = nullptr;
        }
    }

    // Setup API client (no more OAuth manager - Client uses global pool)
    if (config.api.auth_method == claude::AuthMethod::OAUTH) {
        api_client_ = std::make_unique<claude::Client>(
            claude::AuthMethod::OAUTH,
            "",  // Credential not needed for OAuth (uses global pool)
            config.api.base_url
        );
    } else {
        api_client_ = std::make_unique<claude::Client>(
            claude::AuthMethod::API_KEY,
            config.api.api_key,
            config.api.base_url
        );
    }

    // Set log filename for orchestrator to include binary name
    std::string log_filename = std::format("anthropic_requests_{}_orchestrator.log", binary_name_);
    api_client_->set_request_log_filename(log_filename);

    // Inject profiler adapter and set component ID for metrics collection
    api_client_->set_metrics_collector(&profiler_adapter_);
    api_client_->set_component_id("orchestrator", claude::MetricsComponent::ORCHESTRATOR);
    
    // Register orchestrator tools
    register_orchestrator_tools(tool_registry_, this);
}

Orchestrator::~Orchestrator() {
    shutdown();
}

bool Orchestrator::initialize() {
    if (initialized_) return true;
    
    // Clean up workspace from previous runs BEFORE initializing logger
    // BUT preserve lldb_config.json (user's device configuration)
    std::filesystem::path workspace_dir = std::filesystem::path("/tmp/ida_swarm_workspace") / binary_name_;
    std::filesystem::path lldb_config_path = workspace_dir / "lldb_config.json";
    std::string lldb_config_backup;
    bool had_lldb_config = false;

    if (std::filesystem::exists(workspace_dir)) {
        try {
            // Step 1: Backup lldb_config.json if it exists
            if (std::filesystem::exists(lldb_config_path)) {
                std::ifstream config_file(lldb_config_path);
                if (config_file) {
                    std::stringstream buffer;
                    buffer << config_file.rdbuf();
                    lldb_config_backup = buffer.str();
                    had_lldb_config = true;
                }
            }

            // Step 2: Delete entire workspace directory
            std::filesystem::remove_all(workspace_dir);

            // Step 3: Recreate workspace directory
            std::filesystem::create_directories(workspace_dir);

            // Step 4: Restore lldb_config.json
            if (had_lldb_config) {
                std::ofstream config_file(lldb_config_path);
                if (config_file) {
                    config_file << lldb_config_backup;
                }
            }
        } catch (const std::exception& e) {
            // Can't log yet, logger not initialized
        }
    }

    // NOW initialize logger after cleanup
    std::filesystem::path log_path = workspace_dir / "orchestrator.log";
    g_logger.initialize(log_path.string(), "orchestrator");
    LOG_INFO("Orchestrator: Initializing subsystems...\n");
    LOG_INFO("Orchestrator: Workspace cleaned and logger initialized for binary: %s\n", binary_name_.c_str());

    // Generate SSH keys early (for LLDB remote debugging)
    LOG_INFO("Orchestrator: Ensuring SSH keys exist for remote debugging...\n");
    if (SSHKeyManager::ensure_key_pair_exists()) {
        LOG_INFO("Orchestrator: SSH keys ready at %s\n", SSHKeyManager::get_private_key_path().c_str());
    } else {
        LOG_INFO("Orchestrator: WARNING - Failed to generate SSH keys\n");
    }

    // Ignore SIGPIPE to prevent crashes when IRC connections break
    signal(SIGPIPE, SIG_IGN);
    LOG_INFO("Orchestrator: Configured SIGPIPE handler\n");
    
    // Initialize tool tracker database
    if (!tool_tracker_->initialize()) {
        LOG_INFO("Orchestrator: Failed to initialize tool tracker\n");
        return false;
    }

    // Start monitoring for new tool calls
    tool_tracker_->start_monitoring();

    // Initialize orchestrator memory handler
    std::filesystem::path orch_memory_dir = workspace_dir / "memories";
    std::filesystem::create_directories(orch_memory_dir);
    memory_handler_ = std::make_unique<MemoryToolHandler>(orch_memory_dir.string());
    LOG_INFO("Orchestrator: Memory handler initialized at %s\n", orch_memory_dir.string().c_str());

    // Enable profiling if configured
    if (config_.profiling.enabled) {
        profiling::ProfilingManager::instance().enable();
        LOG_INFO("Orchestrator: Profiling enabled\n");
    } else {
        LOG_INFO("Orchestrator: Profiling disabled (config)\n");
    }

    // Copy extract_results.sh script to workspace
    {
        std::filesystem::path script_path = workspace_dir / "extract_results.sh";
        static const char* EXTRACT_RESULTS_SCRIPT = R"SCRIPTEND(#!/bin/bash

# Extract all agent results from IDA Swarm workspace
# This script should be run from /tmp/ida_swarm_workspace/<binary_name>/
# or can be run from anywhere by passing the workspace path as argument

set -e

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Determine workspace directory
if [ -n "$1" ]; then
    WORKSPACE_DIR="$1"
else
    WORKSPACE_DIR="$(pwd)"
fi

# Validate workspace directory
if [ ! -d "$WORKSPACE_DIR/agents" ]; then
    echo -e "${RED}Error: Not a valid IDA Swarm workspace directory${NC}"
    echo "Expected to find 'agents/' subdirectory in: $WORKSPACE_DIR"
    echo ""
    echo "Usage: $0 [workspace_path]"
    echo "  workspace_path: Path to /tmp/ida_swarm_workspace/<binary_name>/ (default: current directory)"
    exit 1
fi

OUTPUT_FILE="$WORKSPACE_DIR/all_agent_results.txt"
AGENTS_DIR="$WORKSPACE_DIR/agents"
CONFIGS_DIR="$WORKSPACE_DIR/configs"

echo -e "${GREEN}IDA Swarm Results Extraction${NC}"
echo -e "${BLUE}Workspace: $WORKSPACE_DIR${NC}"
echo ""

# Check if jq is available for JSON parsing
if command -v jq &> /dev/null; then
    HAS_JQ=true
    echo -e "${GREEN}✓ jq available for JSON parsing${NC}"
else
    HAS_JQ=false
    echo -e "${YELLOW}⚠ jq not found, using fallback parsing (may be less reliable)${NC}"
fi

echo ""

# Initialize output file
cat > "$OUTPUT_FILE" << 'EOFMARKER'
================================================================================
                     IDA SWARM AGENT RESULTS SUMMARY
================================================================================

This file contains the consolidated results from all reverse engineering agents
that worked on this binary analysis task.

EOFMARKER

echo "Generated at: $(date)" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

# Count agents
agent_count=$(find "$AGENTS_DIR" -mindepth 1 -maxdepth 1 -type d | wc -l)
echo -e "${BLUE}Found $agent_count agent(s) to process${NC}"
echo ""

# Process each agent directory
for agent_dir in "$AGENTS_DIR"/*; do
    if [ ! -d "$agent_dir" ]; then
        continue
    fi

    agent_id=$(basename "$agent_dir")
    echo -e "${YELLOW}Processing: $agent_id${NC}"

    # Start agent section in output
    cat >> "$OUTPUT_FILE" << EOFMARKER

================================================================================
Agent: $agent_id
================================================================================

EOFMARKER

    # Extract task from config
    config_file="$CONFIGS_DIR/${agent_id}_config.json"
    if [ -f "$config_file" ]; then
        echo "  - Reading task from config..."

        if [ "$HAS_JQ" = true ]; then
            task=$(jq -r '.task // "Task not found"' "$config_file")
        else
            # Fallback: grep for "task" field
            task=$(grep -o '"task"[[:space:]]*:[[:space:]]*"[^"]*"' "$config_file" | sed 's/"task"[[:space:]]*:[[:space:]]*"\(.*\)"/\1/' | head -1)
            if [ -z "$task" ]; then
                task="Task not found (parsing error)"
            fi
        fi

        echo "Task:" >> "$OUTPUT_FILE"
        echo "------" >> "$OUTPUT_FILE"
        echo "$task" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
    else
        echo "  - Config file not found"
        echo "Task: [Config file not found]" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
    fi

    # Extract all memories
    memories_dir="$agent_dir/memories"
    if [ -d "$memories_dir" ] && [ "$(ls -A "$memories_dir" 2>/dev/null)" ]; then
        echo "  - Extracting memories..."

        echo "Memories:" >> "$OUTPUT_FILE"
        echo "---------" >> "$OUTPUT_FILE"

        # Find all files in memories directory (recursively)
        find "$memories_dir" -type f | sort | while read -r memory_file; do
            # Get relative path from memories directory
            rel_path=$(echo "$memory_file" | sed "s|^$memories_dir/||")

            echo "" >> "$OUTPUT_FILE"
            echo "  File: $rel_path" >> "$OUTPUT_FILE"
            echo "  $(printf '=%.0s' {1..70})" >> "$OUTPUT_FILE"
            cat "$memory_file" >> "$OUTPUT_FILE"
            echo "" >> "$OUTPUT_FILE"
        done

        echo "" >> "$OUTPUT_FILE"
    else
        echo "  - No memories found"
        echo "Memories: [None]" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
    fi

    # Extract final report from conversation_state.json
    conv_state="$agent_dir/conversation_state.json"
    if [ -f "$conv_state" ]; then
        echo "  - Extracting final report..."

        if [ "$HAS_JQ" = true ]; then
            # Use jq to extract the last assistant message's text content
            report=$(jq -r '
                .conversation // []
                | map(select(.role == "assistant"))
                | last
                | .content // []
                | map(select(.type == "text"))
                | map(.text)
                | join("\n")
            ' "$conv_state")

            if [ -z "$report" ] || [ "$report" = "null" ]; then
                report="[No final report found in conversation]"
            fi
        else
            # Fallback: try to extract last assistant message (very basic)
            # This is fragile but better than nothing
            report=$(grep -A 50 '"role"[[:space:]]*:[[:space:]]*"assistant"' "$conv_state" | tail -50 | grep -o '"text"[[:space:]]*:[[:space:]]*"[^"]*"' | tail -1 | sed 's/"text"[[:space:]]*:[[:space:]]*"\(.*\)"/\1/')

            if [ -z "$report" ]; then
                report="[Unable to parse final report - jq not available]"
            fi
        fi

        echo "Output Report:" >> "$OUTPUT_FILE"
        echo "--------------" >> "$OUTPUT_FILE"
        echo "$report" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
    else
        echo "  - Conversation state not found"
        echo "Output Report: [Conversation state file not found]" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
    fi

    echo -e "${GREEN}  ✓ Complete${NC}"
done

# Extract orchestrator memories at the end
ORCHESTRATOR_MEMORIES="$WORKSPACE_DIR/memories"
if [ -d "$ORCHESTRATOR_MEMORIES" ] && [ "$(ls -A "$ORCHESTRATOR_MEMORIES" 2>/dev/null)" ]; then
    echo ""
    echo -e "${YELLOW}Processing: Orchestrator Memories${NC}"

    cat >> "$OUTPUT_FILE" << EOFMARKER

================================================================================
Orchestrator Memories
================================================================================

EOFMARKER

    # Find all files in orchestrator memories directory (recursively)
    find "$ORCHESTRATOR_MEMORIES" -type f | sort | while read -r memory_file; do
        # Get relative path from memories directory
        rel_path=$(echo "$memory_file" | sed "s|^$ORCHESTRATOR_MEMORIES/||")

        echo "" >> "$OUTPUT_FILE"
        echo "  File: $rel_path" >> "$OUTPUT_FILE"
        echo "  $(printf '=%.0s' {1..70})" >> "$OUTPUT_FILE"
        cat "$memory_file" >> "$OUTPUT_FILE"
        echo "" >> "$OUTPUT_FILE"
    done

    echo "" >> "$OUTPUT_FILE"
    echo -e "${GREEN}  ✓ Complete${NC}"
else
    echo ""
    echo -e "${YELLOW}⚠ No orchestrator memories found${NC}"
fi

# Add footer
cat >> "$OUTPUT_FILE" << EOFMARKER

================================================================================
                              END OF RESULTS
================================================================================
EOFMARKER

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}✓ Extraction complete!${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo -e "Results saved to: ${BLUE}$OUTPUT_FILE${NC}"
echo ""
echo "Summary:"
echo "  - Agents processed: $agent_count"
echo "  - Output file size: $(du -h "$OUTPUT_FILE" | cut -f1)"
echo ""
)SCRIPTEND";

        try {
            std::ofstream script_file(script_path);
            if (script_file.is_open()) {
                script_file << EXTRACT_RESULTS_SCRIPT;
                script_file.close();

                // Make script executable
                #ifndef __NT__
                chmod(script_path.string().c_str(), 0755);
                #endif

                LOG_INFO("Orchestrator: Created extract_results.sh script at %s\n", script_path.string().c_str());
            } else {
                LOG_INFO("Orchestrator: WARNING - Failed to create extract_results.sh script\n");
            }
        } catch (const std::exception& e) {
            LOG_INFO("Orchestrator: WARNING - Error creating extract_results.sh: %s\n", e.what());
        }
    }

    // Subscribe to tool call events for real-time processing
    event_bus_subscription_id_ = event_bus_.subscribe(
        [this](const AgentEvent& event) {
            handle_tool_call_event(event);
        },
        {AgentEvent::TOOL_CALL}
    );
    LOG_INFO("Orchestrator: Subscribed to TOOL_CALL events for real-time processing\n");

    // IRC port already allocated earlier (before LLDB manager init)
    // Start IRC server for agent communication with binary name
    fs::path idb_path(main_database_path_);
    std::string binary_name = idb_path.stem().string();
    irc_server_ = std::make_unique<irc::IRCServer>(allocated_irc_port_, binary_name);
    if (!irc_server_->start()) {
        LOG_INFO("Orchestrator: Failed to start IRC server on port %d\n", allocated_irc_port_);
        return false;
    }
    
    LOG_INFO("Orchestrator: IRC server started on port %d (unique for %s)\n", allocated_irc_port_, binary_name_.c_str());
    
    // Connect IRC client for orchestrator communication
    irc_client_ = std::make_unique<irc::IRCClient>("orchestrator", config_.irc.server, allocated_irc_port_);
    if (!irc_client_->connect()) {
        LOG_INFO("Orchestrator: Failed to connect IRC client to %s:%d\n", 
            config_.irc.server.c_str(), allocated_irc_port_);
        return false;
    }
    
    // Join standard orchestrator channels
    irc_client_->join_channel("#agents");
    irc_client_->join_channel("#results");
    irc_client_->join_channel("#status");      // For agent status updates
    irc_client_->join_channel("#discoveries"); // For agent discoveries

    // Join LLDB control channel if LLDB is enabled
    if (config_.lldb.enabled) {
        irc_client_->join_channel("#lldb_control");
        LOG_INFO("Orchestrator: Joined #lldb_control for remote debugging\n");
    }

    // Set up message callback to receive agent results
    irc_client_->set_message_callback(
        [this](const std::string& channel, const std::string& sender, const std::string& message) {
            handle_irc_message(channel, sender, message);
        }
    );
    
    LOG_INFO("Orchestrator: IRC client connected\n");

    // Validate LLDB connectivity if enabled
    if (config_.lldb.enabled) {
        LOG_INFO("Orchestrator: Validating LLDB connectivity...\n");
        lldb_validation_passed_ = validate_lldb_connectivity();

        if (lldb_validation_passed_) {
            LOG_INFO("Orchestrator: LLDB validation passed - remote debugging enabled\n");
        } else {
            LOG_INFO("Orchestrator: LLDB validation failed - remote debugging disabled\n");
        }
    }

    // Start conflict channel monitoring thread
    LOG_INFO("Orchestrator: Starting conflict channel monitor\n");
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
                            LOG_INFO("Orchestrator: Proactively joined conflict channel %s\n", channel.c_str());
                        }
                    }
                }
            }
        }
        LOG_INFO("Orchestrator: Conflict channel monitor thread exiting\n");
    });

    // Initialize database manager
    if (!db_manager_->initialize()) {
        LOG_INFO("Orchestrator: Failed to initialize database manager\n");
        return false;
    }
    
    initialized_ = true;
    LOG_INFO("Orchestrator: Initialization complete\n");
    return true;
}

bool Orchestrator::initialize_mcp_mode(const std::string& session_id,
                                      const std::string& session_dir) {
    mcp_session_id_ = session_id;
    mcp_session_dir_ = session_dir;

    // Setup pipe paths
    fs::path dir_path(session_dir);
    mcp_request_pipe_ = (dir_path / "request.pipe").string();
    mcp_response_pipe_ = (dir_path / "response.pipe").string();

    // Initialize ALL orchestrator components (IRC, tool tracker, agent spawner, etc.)
    // MCP mode needs the full orchestrator functionality
    if (!initialize()) {
        return false;
    }

    LOG_INFO("Orchestrator: MCP mode initialized for session %s\n", session_id.c_str());
    LOG_INFO("Orchestrator: Session directory: %s\n", session_dir.c_str());
    LOG_INFO("Orchestrator: Request pipe: %s\n", mcp_request_pipe_.c_str());
    LOG_INFO("Orchestrator: Response pipe: %s\n", mcp_response_pipe_.c_str());

    return true;
}

void Orchestrator::start_mcp_listener() {
    if (!show_ui_ && !mcp_session_dir_.empty()) {
        LOG_INFO("Orchestrator: Starting MCP listener thread\n");

        mcp_listener_thread_ = std::thread([this]() {
            LOG_INFO("Orchestrator: Opening MCP pipes...\n");

            // CRITICAL ORDER: MCP server opens request pipe for WRITE first, so we open for READ
            mcp_request_fd_ = open(mcp_request_pipe_.c_str(), O_RDONLY);
            if (mcp_request_fd_ < 0) {
                LOG_INFO("Orchestrator: ERROR - Failed to open request pipe: %s\n", strerror(errno));
                return;
            }

            LOG_INFO("Orchestrator: Request pipe opened (fd=%d)\n", mcp_request_fd_);

            // Then open response pipe for WRITE
            mcp_response_fd_ = open(mcp_response_pipe_.c_str(), O_WRONLY);
            if (mcp_response_fd_ < 0) {
                LOG_INFO("Orchestrator: ERROR - Failed to open response pipe: %s\n", strerror(errno));
                close(mcp_request_fd_);
                mcp_request_fd_ = -1;
                return;
            }

            LOG_INFO("Orchestrator: Response pipe opened (fd=%d), waiting for messages...\n", mcp_response_fd_);

            while (!mcp_listener_should_stop_) {
                // Blocking read of message length
                uint32_t len;
                ssize_t n = read_exactly(mcp_request_fd_, &len, sizeof(len));

                if (n == 0) {
                    // EOF - MCP server closed pipe
                    LOG_INFO("Orchestrator: MCP server closed pipe (EOF)\n");
                    break;
                }

                if (n < 0) {
                    // Error reading from pipe
                    LOG_INFO("Orchestrator: Pipe read error: %s\n", strerror(errno));
                    break;
                }

                // Validate message length
                if (len == 0 || len > 10 * 1024 * 1024) {  // Max 10MB
                    LOG_INFO("Orchestrator: Invalid message length: %u\n", len);
                    break;
                }

                // Read message body
                std::vector<char> buf(len);
                n = read_exactly(mcp_request_fd_, buf.data(), len);

                if (n != static_cast<ssize_t>(len)) {
                    LOG_INFO("Orchestrator: Failed to read complete message (expected %u, got %zd)\n", len, n);
                    break;
                }

                // Parse JSON and process request
                try {
                    json request = json::parse(buf.begin(), buf.end());
                    std::string method = request.value("method", "");

                    LOG_INFO("Orchestrator: Received MCP request: %s\n", method.c_str());

                    // Process request
                    json response = process_mcp_request(request);

                    // Write response with length prefix
                    std::string resp_str = response.dump();
                    uint32_t resp_len = resp_str.size();

                    // Write length
                    if (write(mcp_response_fd_, &resp_len, sizeof(resp_len)) != sizeof(resp_len)) {
                        LOG_INFO("Orchestrator: ERROR - Failed to write response length\n");
                        break;
                    }

                    // Write body
                    if (write(mcp_response_fd_, resp_str.c_str(), resp_len) != static_cast<ssize_t>(resp_len)) {
                        LOG_INFO("Orchestrator: ERROR - Failed to write response body\n");
                        break;
                    }

                    LOG_INFO("Orchestrator: Sent MCP response for method '%s'\n", method.c_str());

                    // Handle shutdown after response is sent
                    if (method == "shutdown") {
                        LOG_INFO("Orchestrator: Shutdown response sent, initiating graceful IDA close...\n");

                        // Set flags to stop threads before database close
                        mcp_listener_should_stop_ = true;
                        shutting_down_ = true;

                        // Close pipes before database close
                        if (mcp_request_fd_ >= 0) {
                            close(mcp_request_fd_);
                            mcp_request_fd_ = -1;
                        }
                        if (mcp_response_fd_ >= 0) {
                            close(mcp_response_fd_);
                            mcp_response_fd_ = -1;
                        }

                        // Request IDA to save and close the database
                        struct CloseRequest : exec_request_t {
                            virtual ssize_t idaapi execute() override {
                                LOG("MCP: Saving database before close...\n");

                                // First save the database
                                if (save_database()) {
                                    LOG("MCP: Database saved successfully\n");
                                } else {
                                    LOG("MCP: Warning - Failed to save database\n");
                                }

                                // Then terminate the database
                                LOG("MCP: Calling term_database()...\n");
                                term_database();

                                return 0;
                            }
                        };

                        LOG_INFO("Orchestrator: Requesting IDA to save and close database...\n");
                        CloseRequest req;
                        execute_sync(req, MFF_WRITE);

                        break; // Exit listener loop
                    }

                } catch (const json::exception& e) {
                    LOG_INFO("Orchestrator: JSON parse error: %s\n", e.what());
                    break;
                } catch (const std::exception& e) {
                    LOG_INFO("Orchestrator: Error processing request: %s\n", e.what());
                    break;
                }
            }

            // Clean up pipe file descriptors
            if (mcp_request_fd_ >= 0) {
                close(mcp_request_fd_);
                mcp_request_fd_ = -1;
            }
            if (mcp_response_fd_ >= 0) {
                close(mcp_response_fd_);
                mcp_response_fd_ = -1;
            }

            LOG_INFO("Orchestrator: MCP listener thread exiting\n");
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
        LOG_INFO("Orchestrator: Processing start_task: %s\n", task.c_str());

        // Clear any previous conversation
        clear_conversation();

        // Reset completion flag
        reset_task_completion();

        // Process the task in a separate thread to avoid blocking
        std::thread processing_thread([this, task]() {
            process_user_input(task);
        });

        // Wait for task to complete
        LOG_INFO("Orchestrator: Waiting for task completion...\n");
        wait_for_task_completion();
        LOG_INFO("Orchestrator: Task completed, sending response\n");

        // Join the processing thread
        processing_thread.join();

        // Prepare response with final result
        response["result"]["content"] = last_response_text_;
        response["result"]["agents_spawned"] = agents_.size();

    } else if (method == "process_input") {
        std::string input = request["params"]["input"];
        LOG_INFO("Orchestrator: Processing follow-up input: %s\n", input.c_str());

        // Reset completion flag for continuation
        reset_task_completion();

        // Process the input in a separate thread to avoid blocking
        std::thread processing_thread([this, input]() {
            process_user_input(input);
        });

        // Wait for continuation to complete
        LOG_INFO("Orchestrator: Waiting for continuation completion...\n");
        wait_for_task_completion();
        LOG_INFO("Orchestrator: Continuation completed, sending response\n");

        // Join the processing thread
        processing_thread.join();

        // Prepare response with final result
        size_t completed_count;
        {
            std::lock_guard<std::mutex> lock(agent_state_mutex_);
            completed_count = completed_agents_.size();
        }
        response["result"]["content"] = last_response_text_;
        response["result"]["agents_active"] = agents_.size() - completed_count;

    } else if (method == "shutdown") {
        LOG_INFO("Orchestrator: Received shutdown request\n");
        response["result"]["status"] = "shutting_down";

        // Note: shutdown() will be called after this response is sent
        // No detached thread needed - prevents hanging process

    } else {
        response["error"] = "Unknown method: " + method;
    }

    return response;
}

void Orchestrator::clear_conversation() {
    LOG_INFO("Orchestrator: Clearing conversation and starting fresh\n");
    
    // Clear conversation history
    conversation_history_.clear();

    // Clear any completed agents and results
    {
        std::lock_guard<std::mutex> lock(agent_state_mutex_);
        completed_agents_.clear();
        agent_results_.clear();
    }

    // Reset token stats
    token_stats_.reset();
    
    // Mark conversation as inactive
    conversation_active_ = false;
    
    // Clear current task
    current_user_task_.clear();

    LOG_INFO("Orchestrator: Conversation cleared, ready for new task\n");
}

void Orchestrator::start_auto_decompile() {
    LOG_INFO("Orchestrator: Starting full binary analysis");

    if (!auto_decompile_manager_) {
        LOG_ERROR("Orchestrator: AutoDecompileManager not initialized");
        event_bus_.emit_error("orchestrator", "AutoDecompilationManager not initialized");
        return;
    }

    // Start auto decompile (this will enumerate functions, prioritize, and spawn agents)
    auto_decompile_manager_->start_auto_decompile();
}

void Orchestrator::stop_auto_decompile() {
    LOG_INFO("Orchestrator: Stopping auto decompilation");

    if (!auto_decompile_manager_) {
        LOG_ERROR("Orchestrator: AutoDecompileManager not initialized");
        return;
    }

    auto_decompile_manager_->stop_analysis();
}

void Orchestrator::revalidate_lldb() {
    LOG_INFO("Orchestrator: Revalidating LLDB connectivity after configuration change\n");

    if (!config_.lldb.enabled) {
        LOG_INFO("Orchestrator: LLDB is disabled in config, skipping revalidation\n");
        lldb_validation_passed_ = false;
        return;
    }

    // Re-run validation (reads fresh lldb_config.json)
    bool new_validation_result = validate_lldb_connectivity();

    if (new_validation_result != lldb_validation_passed_) {
        lldb_validation_passed_ = new_validation_result;

        if (lldb_validation_passed_) {
            LOG_INFO("Orchestrator: LLDB validation NOW PASSED - remote debugging enabled for new agents\n");
        } else {
            LOG_INFO("Orchestrator: LLDB validation NOW FAILED - remote debugging disabled for new agents\n");
        }
    } else {
        LOG_INFO("Orchestrator: LLDB validation status unchanged: %s\n",
                 lldb_validation_passed_ ? "PASSED" : "FAILED");
    }
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
        LOG_INFO("Orchestrator: Continuing conversation with: %s\n", input.c_str());
    } else {
        // New conversation - clear everything and start fresh
        current_user_task_ = input;

        // Clear any completed agents and results from previous tasks
        {
            std::lock_guard<std::mutex> lock(agent_state_mutex_);
            completed_agents_.clear();
            agent_results_.clear();
        }

        // Reset token stats for new task
        token_stats_.reset();
        
        // Initialize conversation history for new task
        conversation_history_.clear();
        conversation_history_.push_back(claude::messages::Message::user_text(input));
        
        // Mark conversation as active
        conversation_active_ = true;
        LOG_INFO("Orchestrator: Starting new conversation");
    }
    
    LOG_INFO("Orchestrator: Processing task: %s\n", input.c_str());

    // Emit thinking event
    LOG_INFO("Orchestrator: Publishing ORCHESTRATOR_THINKING event\n");
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
        LOG_INFO("Orchestrator: Failed to process request: %s\n",
            response.error ? response.error->c_str() : "Unknown error");

        if (!show_ui_) {
            signal_task_completion();
        }
        return;
    }
    
    // Track initial response tokens
    LOG_INFO("DEBUG: Initial response usage - In: %d, Out: %d, Cache Read: %d, Cache Write: %d\n",
        response.usage.input_tokens, response.usage.output_tokens,
        response.usage.cache_read_tokens, response.usage.cache_creation_tokens);
    token_stats_.add_usage(response.usage);
    log_token_usage(response.usage, token_stats_.get_total());

    // Log context clearing if it occurred
    if (response.context_management) {
        for (const auto& edit : response.context_management->applied_edits) {
            LOG_INFO("Orchestrator: Context management cleared %d tool uses (%d tokens)\n",
                edit.cleared_tool_uses, edit.cleared_input_tokens);
        }
    }
    
    // Display orchestrator's response
    std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(response.message);
    if (text) {
        LOG_INFO("Orchestrator: %s\n", text->c_str());
        
        // Only emit the response if there are no tool calls (otherwise wait for final response)
        std::vector<const claude::messages::ToolUseContent*> initial_tool_calls = 
            claude::messages::ContentExtractor::extract_tool_uses(response.message);
        if (initial_tool_calls.empty()) {
            // No tool calls, this is the final response
            if (text && !text->empty()) {
                last_response_text_ = *text;  // Store for MCP mode
            }
            LOG_INFO("Orchestrator: Publishing ORCHESTRATOR_RESPONSE event (no tools)\n");
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
            // Send tool results back with retry logic for server errors
            const int MAX_CONTINUATION_RETRIES = 3;
            const int BASE_RETRY_DELAY_MS = 2000;  // Start with 2 seconds

            claude::ChatResponse continuation;
            bool request_succeeded = false;

            for (int retry = 0; retry <= MAX_CONTINUATION_RETRIES; retry++) {
                continuation = send_continuation_request();

                if (continuation.success) {
                    request_succeeded = true;
                    break;
                }

                // Check if this is a recoverable error (500s, timeouts, etc.)
                if (!claude::Client::is_recoverable_error(continuation)) {
                    LOG_INFO("Orchestrator: Non-recoverable continuation error: %s\n",
                        continuation.error ? continuation.error->c_str() : "Unknown error");
                    break;
                }

                // Don't retry if we've exhausted attempts
                if (retry == MAX_CONTINUATION_RETRIES) {
                    LOG_INFO("Orchestrator: Max continuation retries (%d) exhausted: %s\n",
                        MAX_CONTINUATION_RETRIES,
                        continuation.error ? continuation.error->c_str() : "Unknown error");
                    break;
                }

                // Calculate exponential backoff delay
                int delay_ms = BASE_RETRY_DELAY_MS * (1 << retry);  // 2s, 4s, 8s

                LOG_INFO("Orchestrator: Continuation request failed (recoverable), retrying in %d ms (attempt %d/%d): %s\n",
                    delay_ms, retry + 1, MAX_CONTINUATION_RETRIES,
                    continuation.error ? continuation.error->c_str() : "Unknown error");

                std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            }

            if (!request_succeeded) {
                LOG_INFO("Orchestrator: Failed to get continuation after retries: %s\n",
                    continuation.error ? continuation.error->c_str() : "Unknown error");

                // Signal task completion for MCP mode before breaking
                if (!show_ui_) {
                    signal_task_completion();
                }
                break;
            }
            
            // Track tokens from continuation response
            LOG_INFO("DEBUG: Continuation usage - In: %d, Out: %d, Cache Read: %d, Cache Write: %d\n",
                continuation.usage.input_tokens, continuation.usage.output_tokens,
                continuation.usage.cache_read_tokens, continuation.usage.cache_creation_tokens);
            token_stats_.add_usage(continuation.usage);

            // Log context clearing if it occurred
            if (continuation.context_management) {
                for (const auto& edit : continuation.context_management->applied_edits) {
                    LOG_INFO("Orchestrator: Context management cleared %d tool uses (%d tokens)\n",
                        edit.cleared_tool_uses, edit.cleared_input_tokens);
                }
            }
            
            // Display text if present
            auto cont_text = claude::messages::ContentExtractor::extract_text(continuation.message);
            if (cont_text) {
                LOG_INFO("Orchestrator: %s\n", cont_text->c_str());
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
            
            LOG_INFO("Orchestrator: Processed %zu more tool calls, continuing conversation...\n", 
                cont_tool_results.size());
        }
    }
}

claude::ChatResponse Orchestrator::send_continuation_request() {
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

    LOG_INFO("Orchestrator: Sending continuation request with %zu messages\n", conversation_history_.size());
    return send_request_with_memory(builder);
}

claude::ChatResponse Orchestrator::send_request_with_memory(claude::ChatRequestBuilder& builder) {
    // Enable automatic context clearing
    builder.enable_auto_context_clearing(
        100000,  // trigger at 100k tokens
        5,       // keep 5 most recent tool uses
        {"memory"}  // never clear memory tool results
    );

    auto request = builder.build();

    // Add memory tool (before IDA tools so cache control stays on last IDA tool)
    json memory_tool = {
        {"type", "memory_20250818"},
        {"name", "memory"}
    };
    request.tool_definitions.insert(request.tool_definitions.begin(), memory_tool);

    return api_client_->send_request(request);
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

    return send_request_with_memory(builder);
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
            LOG_INFO("Orchestrator: Executing spawn_agent tool via registry (id: %s)\n", tool_use->id.c_str());

            // Execute via tool registry (which calls spawn_agent_async)
            claude::messages::Message result = tool_registry_.execute_tool_call(*tool_use);

            // Record orchestrator tool call
            tool_tracker_->record_tool_call("orchestrator", tool_use->name, BADADDR, tool_use->input);

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
                        LOG_INFO("Orchestrator: Spawned agent %s for tool call %s\n", 
                            agent_id.c_str(), tool_use->id.c_str());
                    } else {
                        LOG_INFO("Orchestrator: spawn_agent failed for tool call %s\n", tool_use->id.c_str());
                        tool_to_agent[tool_use->id] = "";  // Empty means error
                    }
                } catch (const std::exception& e) {
                    LOG_INFO("Orchestrator: Failed to parse spawn_agent result: %s\n", e.what());
                    tool_to_agent[tool_use->id] = "";  // Empty means error
                }
            }
            // Don't add to results yet - we'll enrich it after waiting
        } else if (tool_use->name == "memory") {
            // Intercept memory tool calls and handle locally
            LOG_INFO("Orchestrator: Executing memory tool (id: %s)\n", tool_use->id.c_str());

            json result = memory_handler_
                ? memory_handler_->execute_command(tool_use->input)
                : json{{"success", false}, {"error", "Memory system not initialized"}};

            // Record orchestrator tool call
            tool_tracker_->record_tool_call("orchestrator", tool_use->name, BADADDR, tool_use->input);

            claude::messages::Message memory_result = claude::messages::Message::tool_result(
                tool_use->id, result.dump()
            );

            non_spawn_results.push_back({tool_use->id, memory_result});
        } else {
            // Execute other tools normally and store for later
            LOG_INFO("Orchestrator: Executing non-spawn_agent tool: %s\n", tool_use->name.c_str());
            non_spawn_results.push_back({tool_use->id, tool_registry_.execute_tool_call(*tool_use)});

            // Record orchestrator tool call
            tool_tracker_->record_tool_call("orchestrator", tool_use->name, BADADDR, tool_use->input);
        }
    }
    
    // If we spawned any agents, wait for ALL of them to complete
    if (!spawned_agent_ids.empty()) {
        LOG_INFO("Orchestrator: Waiting for %zu agents to complete their tasks...\n", spawned_agent_ids.size());
        wait_for_agents_completion(spawned_agent_ids);
        LOG_INFO("Orchestrator: All %zu agents have completed\n", spawned_agent_ids.size());
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
                    
                    LOG_INFO("Orchestrator: Added spawn_agent result with report for %s\n", agent_id.c_str());
                } else {
                    // Create error result
                    json error_json = {
                        {"error", "Failed to spawn agent"}
                    };
                    
                    // Add to the combined message
                    combined_result.add_content(std::make_unique<claude::messages::ToolResultContent>(
                        tool_use->id, error_json.dump(), true  // is_error = true
                    ));
                    
                    LOG_INFO("Orchestrator: Added spawn_agent error result\n");
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
    LOG_INFO("Orchestrator: Spawning agent for task: %s\n", task.c_str());
    
    // Generate agent ID
    std::string agent_id = std::format("agent_{}", next_agent_id_++);
    
    // Emit agent spawning event
    LOG_INFO("Orchestrator: Publishing AGENT_SPAWNING event for %s\n", agent_id.c_str());
    event_bus_.publish(AgentEvent(AgentEvent::AGENT_SPAWNING, "orchestrator", {
        {"agent_id", agent_id},
        {"task", task}
    }));
    
    // Save and pack current database
    LOG_INFO("Orchestrator: Creating agent database for %s\n", agent_id.c_str());
    std::string agent_db_path = db_manager_->create_agent_database(agent_id);
    LOG_INFO("Orchestrator: Agent database created at: %s\n", agent_db_path.c_str());
    if (agent_db_path.empty()) {
        return {
            {"success", false},
            {"error", "Failed to create agent database"}
        };
    }
    
    // Get the agent's binary path (the copied binary that this agent should patch)
    std::string agent_binary_path = db_manager_->get_agent_binary(agent_id);
    LOG_INFO("Orchestrator: Agent binary path: %s\n", agent_binary_path.c_str());

    // Agents will discover each other dynamically via IRC
    std::string agent_prompt = generate_agent_prompt(task, context);

    // Create agent memory directory
    std::filesystem::path workspace_dir = std::filesystem::path("/tmp/ida_swarm_workspace") / binary_name_;
    std::filesystem::path agent_memory_dir = workspace_dir / "agents" / agent_id / "memories";
    std::filesystem::create_directories(agent_memory_dir);
    LOG_INFO("Orchestrator: Created agent memory directory at %s\n", agent_memory_dir.string().c_str());

    // Prepare agent configuration with swarm settings
    json agent_config = {
        {"agent_id", agent_id},
        {"binary_name", binary_name_},  // Pass binary name to agent
        {"task", task},                 // Include the raw task for IRC sharing
        {"prompt", agent_prompt},       // Full prompt with task and collaboration instructions
        {"database", agent_db_path},
        {"agent_binary_path", agent_binary_path},  // Path to agent's copied binary for patching
        {"irc_server", config_.irc.server},
        {"irc_port", allocated_irc_port_},  // Use the dynamically allocated port
        {"memory_directory", agent_memory_dir.string()},  // Fresh memory directory for this agent
        {"context", context},            // Pass context for conditional system prompt and grader control
        {"lldb_validated", lldb_validation_passed_}  // LLDB connectivity validation status
    };
    
    // Spawn the agent process
    LOG_INFO("Orchestrator: About to spawn agent process for %s\n", agent_id.c_str());
    int pid = agent_spawner_->spawn_agent(agent_id, agent_db_path, agent_config);
    LOG_INFO("Orchestrator: Agent spawner returned PID %d for %s\n", pid, agent_id.c_str());
    
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
    LOG_INFO("Orchestrator: Publishing AGENT_SPAWN_COMPLETE event for %s\n", agent_id.c_str());
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
    
    LOG_INFO("Orchestrator: Agent %s spawned with PID %d (async)\n", agent_id.c_str(), pid);
    
    return {
        {"success", true},
        {"agent_id", agent_id},
        {"process_id", pid},
        {"database", agent_db_path}
    };
}

std::string Orchestrator::get_agent_result(const std::string& agent_id) const {
    std::lock_guard<std::mutex> lock(agent_state_mutex_);
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
    
    LOG_INFO("Orchestrator: Merging database from agent %s\n", agent_id.c_str());
    
    // Get agent's tool calls
    std::vector<ToolCall> tool_calls = tool_tracker_->get_agent_tool_calls(agent_id);
    
    // Apply changes to main database
    auto result = merge_manager_->merge_agent_changes(agent_id);
    
    if (result.success) {
        LOG_INFO("Orchestrator: Successfully merged %d changes from agent %s\n", 
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
    // Make a copy of completed agents and results under lock to avoid data races
    std::set<std::string> completed_copy;
    std::map<std::string, std::string> results_copy;
    {
        std::lock_guard<std::mutex> lock(agent_state_mutex_);
        completed_copy = completed_agents_;
        results_copy = agent_results_;
    }

    std::vector<std::pair<std::string, std::string>> active_agents;
    for (const auto& [id, info] : agents_) {
        // Check if agent hasn't completed yet (using IRC tracking to avoid hanging on zombies)
        if (completed_copy.count(id) == 0) {
            active_agents.push_back({id, info.task});
        }
    }

    // Add completed agents with their results
    // i do not want to do this, but the orchestrator is not good at understanding that these are starting fresh, and it doesn't provide enough information.
    // if agent collaboration was working better that would solve this, but the agents just go and waste eachothers time so i had to remove it
    // in the future ill redesign all of this (currently super hodgepodge) focused around irc from the get go
//     if (!completed_copy.empty()) {
//         prompt += R"(
//
// COMPLETED AGENTS & THEIR RESULTS:
// )";
//         for (const std::string& agent_id : completed_copy) {
//             auto agent_it = agents_.find(agent_id);
//             auto result_it = results_copy.find(agent_id);
//
//             if (agent_it != agents_.end() && result_it != results_copy.end()) {
//                 prompt += "- " + agent_id + " (task: " + agent_it->second.task + ")\n";
//                 prompt += "  Result: " + result_it->second + "\n\n";
//             }
//         }
//         prompt += R"(Use these completed results to:
// - Build upon previous findings rather than duplicating work
// - Reference specific discoveries from other agents
// - Avoid re-analyzing what has already been solved
//
// )";
//     }
//
//     if (!active_agents.empty()) {
//         prompt += R"(CURRENTLY ACTIVE AGENTS:
// )";
//         for (const auto& [agent_id, agent_task] : active_agents) {
//             prompt += "- " + agent_id + " (working on: " + agent_task + ")\n";
//         }
//         prompt += R"(
// You can see what each agent is working on above. Use this information to:
// - Share relevant findings with agents working on related tasks
// - Coordinate when your tasks overlap or depend on each other
// )";
//     } else if (completed_copy.empty()) {
//         prompt += R"(
//
// You are currently the only active agent.
// - Other agents may join later and will be announced via IRC
// )";
//     }

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
1. Store ALL your key findings using the memory tool
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
    LOG_INFO("Orchestrator: Warning - Could not find available port in range [%d, %d]\n", 
        BASE_PORT, BASE_PORT + PORT_RANGE - 1);
    return BASE_PORT;  // Return base port as fallback
}

void Orchestrator::wait_for_agents_completion(const std::vector<std::string>& agent_ids) {
    LOG_INFO("Orchestrator: Waiting for %zu agents to complete...\n", agent_ids.size());
    
    // Wait for all specified agents to send their results or exit
    int check_count = 0;
    std::set<std::string> agents_done;
    
    while (agents_done.size() < agent_ids.size()) {
        agents_done.clear();
        
        // Check each agent for completion
        for (const std::string& agent_id : agent_ids) {
            // Check if agent sent IRC completion message
            bool has_irc_result;
            {
                std::lock_guard<std::mutex> lock(agent_state_mutex_);
                has_irc_result = (completed_agents_.count(agent_id) > 0);
            }

            // Check if agent process has exited
            bool process_exited = false;
            auto agent_it = agents_.find(agent_id);
            if (agent_it != agents_.end()) {
                int pid = agent_it->second.process_id;
                if (pid > 0 && !agent_spawner_->is_agent_running(pid)) {
                    process_exited = true;
                    LOG_INFO("Orchestrator: Agent %s process %d has exited\n", agent_id.c_str(), pid);
                }
            }

            // Consider agent done if EITHER condition is met
            if (has_irc_result || process_exited) {
                agents_done.insert(agent_id);

                // If process exited but no IRC message, mark as completed with default message
                if (process_exited && !has_irc_result) {
                    LOG_INFO("Orchestrator: Agent %s exited without sending result, marking as completed\n", agent_id.c_str());
                    {
                        std::lock_guard<std::mutex> lock(agent_state_mutex_);
                        completed_agents_.insert(agent_id);
                        agent_results_[agent_id] = "Agent process terminated without sending final report";
                    }

                    // Emit task complete event for UI updates
                    event_bus_.publish(AgentEvent(AgentEvent::TASK_COMPLETE, agent_id, {}));

                    // Notify decompilation manager if it's active
                    if (auto_decompile_manager_ && auto_decompile_manager_->is_active()) {
                        auto_decompile_manager_->on_agent_completed(agent_id);
                    }

                    // Cleanup agent workspace if it didn't perform any write operations
                    cleanup_agent_directory_if_no_writes(agent_id);
                }
            }
        }

        size_t completed_count;
        {
            std::lock_guard<std::mutex> lock(agent_state_mutex_);
            completed_count = completed_agents_.size();
        }
        LOG_INFO("Orchestrator: Check #%d - %zu/%zu agents completed (IRC: %zu)\n",
            ++check_count, agents_done.size(), agent_ids.size(), completed_count);
        
        // Check if all requested agents have completed
        if (agents_done.size() >= agent_ids.size()) {
            LOG_INFO("Orchestrator: All %zu agents have completed\n", agent_ids.size());
            break;
        }
        
        // Wait before checking again
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    LOG_INFO("Orchestrator: Agent wait complete\n");
}

void Orchestrator::handle_irc_message(const std::string& channel, const std::string& sender, const std::string& message) {
    LOG_INFO("DEBUG: IRC message received - Channel: %s, Sender: %s, Message: %s\n", channel.c_str(), sender.c_str(), message.c_str());
    // Emit all IRC messages to the UI for display
    event_bus_.publish(AgentEvent(AgentEvent::MESSAGE, sender, {
        {"channel", channel},
        {"message", message}
    }));

    // Check for LLDB messages on #lldb_control channel
    if (channel == "#lldb_control" && message.find("LLDB_") == 0) {
        handle_lldb_message(channel, sender, message);
        return;
    }

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

            LOG_INFO("Orchestrator: Request for agent %s to join conflict channel %s\n", target_agent.c_str(), conflict_channel.c_str());

            // Check if agent is running or completed
            auto agent_it = agents_.find(target_agent);
            if (agent_it != agents_.end()) {
                // Agent exists - check if it's running or completed
                bool was_completed;
                {
                    std::lock_guard<std::mutex> lock(agent_state_mutex_);
                    was_completed = (completed_agents_.count(target_agent) > 0);
                    if (was_completed) {
                        // Remove from completed set since it's being resurrected
                        completed_agents_.erase(target_agent);
                    }
                }

                if (was_completed) {
                    // Agent has completed - resurrect it
                    LOG_INFO("Orchestrator: Agent %s has completed, resurrecting for conflict resolution...\n", target_agent.c_str());

                std::string db_path = agent_it->second.database_path;

                // Create resurrection config - agent will get details from channel
                json resurrection_config = {
                    {"reason", "conflict_resolution"},
                    {"conflict_channel", conflict_channel}
                };

                // Resurrect the agent (no lock held during expensive operation)
                int pid = agent_spawner_->resurrect_agent(target_agent, db_path, resurrection_config);
                if (pid > 0) {
                    LOG_INFO("Orchestrator: Successfully resurrected agent %s (PID %d)\n",
                        target_agent.c_str(), pid);

                    // Update the agent info with new PID
                    agent_it->second.process_id = pid;
                    agent_it->second.task = "Conflict Resolution";

                    // The resurrected agent will join the conflict channel and see the
                    // conflict details that the initiating agent posts there
                } else {
                    LOG_INFO("Orchestrator: Failed to resurrect agent %s\n", target_agent.c_str());
                    // Add back to completed since resurrection failed
                    {
                        std::lock_guard<std::mutex> lock(agent_state_mutex_);
                        completed_agents_.insert(target_agent);
                    }
                }
                } else {
                    // Agent is still running - send CONFLICT_INVITE
                    LOG_INFO("Orchestrator: Agent %s is still running, sending conflict invite...\n",
                             target_agent.c_str());

                    std::string invite_msg = std::format("CONFLICT_INVITE|{}|{}", target_agent, conflict_channel);
                    irc_client_->send_message("#agents", invite_msg);
                    LOG_INFO("Orchestrator: Sent CONFLICT_INVITE to agent %s for channel %s\n",
                             target_agent.c_str(), conflict_channel.c_str());
                }
            } else {
                LOG_INFO("Orchestrator: Agent %s not found in agents map\n", target_agent.c_str());
            }
        } else {
            LOG_INFO("Orchestrator: Invalid JOIN_CONFLICT message format - expecting target|channel\n");
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

            LOG_INFO("Orchestrator: Agent %s marked consensus for %s: %s\n",
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
                        LOG_INFO("Orchestrator: All agents marked consensus for %s, extracting and enforcing\n", channel.c_str());

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
                // Check if any agents are still alive before enforcement
                bool any_agents_alive = false;
                for (const std::string& agent_id : agents_copy) {
                    auto agent_it = agents_.find(agent_id);
                    if (agent_it != agents_.end()) {
                        int pid = agent_it->second.process_id;
                        if (pid > 0 && agent_spawner_->is_agent_running(pid)) {
                            any_agents_alive = true;
                            break;
                        }
                    }
                }

                if (!any_agents_alive) {
                    LOG_INFO("Orchestrator: All participating agents have exited, skipping manual enforcement\n");
                    // Just send CONSENSUS_COMPLETE and clean up
                    if (irc_client_) {
                        irc_client_->send_message(channel, "CONSENSUS_COMPLETE");
                    }
                    LOG_INFO("Orchestrator: Sent CONSENSUS_COMPLETE notification\n");

                    // Clean up conflict session
                    std::lock_guard<std::mutex> cleanup_lock(conflicts_mutex_);
                    active_conflicts_.erase(channel);
                } else {
                    // Spawn a thread to handle consensus enforcement so we don't block the IRC thread
                    std::thread enforcement_thread([this, channel, tool_call, agents_copy]() {
                        if (!tool_call.is_null() && tool_call.contains("tool_name") && tool_call.contains("parameters")) {
                            enforce_consensus_tool_execution(channel, tool_call, agents_copy);
                        }

                        if (irc_client_) {
                            // Send to the conflict channel so participating agents see it
                            irc_client_->send_message(channel, "CONSENSUS_COMPLETE");
                        }
                        LOG_INFO("Orchestrator: Sent CONSENSUS_COMPLETE notification to all agents\n");

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
        }
        return;
    }

    // Parse AGENT_TOKEN_UPDATE messages from #agents channel
    if (channel == "#agents" && message.find("AGENT_TOKEN_UPDATE | ") == 0) {
        // Format: AGENT_TOKEN_UPDATE | {json}
        std::string json_str = message.substr(21);  // Skip "AGENT_TOKEN_UPDATE | "
        
        LOG_INFO("DEBUG: Received AGENT_TOKEN_UPDATE from IRC: %s\n", json_str.c_str());
        
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
            
            LOG_INFO("Orchestrator: Received token metrics from %s (iteration %d)\n", 
                agent_id.c_str(), iteration);
        } catch (const std::exception& e) {
            LOG_INFO("Orchestrator: Failed to parse agent metric JSON: %s\n", e.what());
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
            
            LOG_INFO("Orchestrator: Received result from %s: %s\n", agent_id.c_str(), report.c_str());
            
            // Emit swarm result event
            event_bus_.publish(AgentEvent(AgentEvent::SWARM_RESULT, "orchestrator", {
                {"agent_id", agent_id},
                {"result", report}
            }));

            // Store the result and mark agent as completed
            size_t completed_count;
            {
                std::lock_guard<std::mutex> lock(agent_state_mutex_);
                agent_results_[agent_id] = report;
                completed_agents_.insert(agent_id);
                completed_count = completed_agents_.size();
            }
            LOG_INFO("Orchestrator: Marked %s as completed (have %zu/%zu completions)\n",
                agent_id.c_str(), completed_count, agents_.size());

            // Emit task complete event for UI updates
            event_bus_.publish(AgentEvent(AgentEvent::TASK_COMPLETE, agent_id, {}));

            // Notify decompilation manager if it's active
            if (auto_decompile_manager_ && auto_decompile_manager_->is_active()) {
                auto_decompile_manager_->on_agent_completed(agent_id);
            }

            // Find the agent info
            auto it = agents_.find(agent_id);
            if (it != agents_.end()) {
                // Display the agent's result to the user
                LOG_INFO("===========================================\n");
                LOG_INFO("Agent %s completed task: %s\n", agent_id.c_str(), it->second.task.c_str());
                LOG_INFO("Result: %s\n", report.c_str());
                LOG_INFO("===========================================\n");
                
                // Automatically merge the agent's database changes
                LOG_INFO("Orchestrator: Auto-merging database changes from agent %s\n", agent_id.c_str());
                json merge_result = merge_database(agent_id);
                
                if (merge_result["success"]) {
                    LOG_INFO("Orchestrator: Successfully auto-merged %d changes from agent %s\n",
                        merge_result.value("changes_applied", 0), agent_id.c_str());
                    if (merge_result.value("changes_failed", 0) > 0) {
                        LOG_INFO("Orchestrator: Warning - %d changes failed to merge\n",
                            merge_result.value("changes_failed", 0));
                    }
                } else {
                    LOG_INFO("Orchestrator: Failed to auto-merge changes from agent %s: %s\n",
                        agent_id.c_str(), merge_result.value("error", "Unknown error").c_str());
                }

                // Cleanup agent workspace if it didn't perform any write operations
                cleanup_agent_directory_if_no_writes(agent_id);
            }
        } catch (const std::exception& e) {
            LOG_INFO("Orchestrator: Failed to parse agent result JSON: %s\n", e.what());
        }
        }
    }
}

json Orchestrator::extract_consensus_tool_call(const ConflictSession& session) {
    LOG_INFO("Orchestrator: Extracting consensus tool call from multiple agent statements\n");

    // Check if we have the original conflict details
    if (session.original_conflict.first_call.tool_name.empty()) {
        LOG_INFO("Orchestrator: WARNING - No original conflict details, falling back\n");

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
            LOG_INFO("Orchestrator: ConsensusExecutor failed to extract tool call\n");
            // not necessarily a failure, the agents could have decided that no modification was needed in which case no tool call will be extracted

            return {
                {"tool_name", "unknown"}
            };
        }

        LOG_INFO("Orchestrator: ConsensusExecutor extracted tool call: %s\n", tool_call.dump().c_str());
        return tool_call;

    } catch (const std::exception& e) {
        LOG_INFO("Orchestrator: ERROR in ConsensusExecutor: %s\n", e.what());

        return {
            {"tool_name", "unknown"}
        };
    }
}

void Orchestrator::handle_lldb_message(const std::string& channel, const std::string& sender, const std::string& message) {
    if (!lldb_manager_) {
        LOG_INFO("Orchestrator: Received LLDB message but LLDB is not enabled\n");
        return;
    }

    LOG_INFO("Orchestrator: Handling LLDB message from %s: %s\n", sender.c_str(), message.c_str());

    // Parse message type and route to appropriate handler
    // Message formats:
    // LLDB_START_SESSION|request_id|agent_id
    // LLDB_SEND_COMMAND|request_id|session_id|agent_id|command
    // LLDB_CONVERT_ADDRESS|request_id|session_id|agent_id|ida_address
    // LLDB_STOP_SESSION|request_id|session_id|agent_id

    size_t first_pipe = message.find('|');
    if (first_pipe == std::string::npos) {
        LOG_INFO("Orchestrator: Invalid LLDB message format (no pipes)\n");
        return;
    }

    std::string message_type = message.substr(0, first_pipe);
    std::string remainder = message.substr(first_pipe + 1);

    json response;
    std::string request_id;
    std::string agent_id;

    if (message_type == "LLDB_START_SESSION") {
        // Format: LLDB_START_SESSION|request_id|agent_id|timeout_ms
        size_t pipe1 = remainder.find('|');
        if (pipe1 != std::string::npos) {
            request_id = remainder.substr(0, pipe1);
            std::string rest = remainder.substr(pipe1 + 1);

            size_t pipe2 = rest.find('|');
            if (pipe2 != std::string::npos) {
                agent_id = rest.substr(0, pipe2);
                int timeout_ms = std::stoi(rest.substr(pipe2 + 1));
                response = lldb_manager_->handle_start_session(agent_id, request_id, timeout_ms);
            }
        }
    } else if (message_type == "LLDB_SEND_COMMAND") {
        // Format: LLDB_SEND_COMMAND|request_id|session_id|agent_id|command
        size_t pipe1 = remainder.find('|');
        if (pipe1 != std::string::npos) {
            request_id = remainder.substr(0, pipe1);
            remainder = remainder.substr(pipe1 + 1);

            size_t pipe2 = remainder.find('|');
            if (pipe2 != std::string::npos) {
                std::string session_id = remainder.substr(0, pipe2);
                remainder = remainder.substr(pipe2 + 1);

                size_t pipe3 = remainder.find('|');
                if (pipe3 != std::string::npos) {
                    agent_id = remainder.substr(0, pipe3);
                    std::string command = remainder.substr(pipe3 + 1);

                    response = lldb_manager_->handle_send_command(session_id, agent_id, command, request_id);
                }
            }
        }
    } else if (message_type == "LLDB_CONVERT_ADDRESS") {
        // Format: LLDB_CONVERT_ADDRESS|request_id|session_id|agent_id|ida_address
        size_t pipe1 = remainder.find('|');
        if (pipe1 != std::string::npos) {
            request_id = remainder.substr(0, pipe1);
            remainder = remainder.substr(pipe1 + 1);

            size_t pipe2 = remainder.find('|');
            if (pipe2 != std::string::npos) {
                std::string session_id = remainder.substr(0, pipe2);
                remainder = remainder.substr(pipe2 + 1);

                size_t pipe3 = remainder.find('|');
                if (pipe3 != std::string::npos) {
                    agent_id = remainder.substr(0, pipe3);
                    std::string ida_address_str = remainder.substr(pipe3 + 1);

                    uint64_t ida_address = std::stoull(ida_address_str);
                    response = lldb_manager_->handle_convert_address(session_id, agent_id, ida_address, request_id);
                }
            }
        }
    } else if (message_type == "LLDB_STOP_SESSION") {
        // Format: LLDB_STOP_SESSION|request_id|session_id|agent_id
        size_t pipe1 = remainder.find('|');
        if (pipe1 != std::string::npos) {
            request_id = remainder.substr(0, pipe1);
            remainder = remainder.substr(pipe1 + 1);

            size_t pipe2 = remainder.find('|');
            if (pipe2 != std::string::npos) {
                std::string session_id = remainder.substr(0, pipe2);
                agent_id = remainder.substr(pipe2 + 1);

                response = lldb_manager_->handle_stop_session(session_id, agent_id, request_id);
            }
        }
    } else {
        LOG_INFO("Orchestrator: Unknown LLDB message type: %s\n", message_type.c_str());
        return;
    }

    // Send response back to agent on their private channel
    if (!request_id.empty() && !agent_id.empty() && irc_client_) {
        std::string response_channel = std::format("#{}", agent_id);
        std::string response_type = message_type + "_RESPONSE";
        std::string response_message = std::format("{}|{}|{}", response_type, request_id, response.dump());

        LOG_INFO("Orchestrator: Sending LLDB response to %s: %s\n", response_channel.c_str(), response_type.c_str());
        irc_client_->send_message(response_channel, response_message);
    }
}


void Orchestrator::enforce_consensus_tool_execution(const std::string& channel, const json& tool_call,
                                                   const std::set<std::string>& agents) {
    LOG_INFO("Orchestrator: Enforcing consensus tool execution for %zu agents\n", agents.size());

    // Safely extract tool_name with error checking
    if (!tool_call.contains("tool_name") || !tool_call["tool_name"].is_string()) {
        LOG_INFO("Orchestrator: ERROR - Invalid or missing tool_name in consensus\n");
        return;
    }
    std::string tool_name = tool_call["tool_name"].get<std::string>();

    // Safely extract parameters, ensuring it's an object
    json parameters = json::object();
    if (tool_call.contains("parameters")) {
        if (tool_call["parameters"].is_object()) {
            parameters = tool_call["parameters"];
        } else if (!tool_call["parameters"].is_null()) {
            LOG_INFO("Orchestrator: WARNING - parameters is not an object, using empty object\n");
        }
    }

    if (tool_name == "unknown") return;

    // Check if any agents are still alive - if all dead, skip enforcement
    std::set<std::string> alive_agents;
    for (const std::string& agent_id : agents) {
        auto agent_it = agents_.find(agent_id);
        if (agent_it != agents_.end()) {
            int pid = agent_it->second.process_id;
            if (pid > 0 && agent_spawner_->is_agent_running(pid)) {
                alive_agents.insert(agent_id);
            } else {
                LOG_INFO("Orchestrator: Agent %s (PID %d) has exited, skipping enforcement for this agent\n",
                    agent_id.c_str(), pid);
            }
        }
    }

    if (alive_agents.empty()) {
        LOG_INFO("Orchestrator: All agents have exited, skipping consensus enforcement entirely\n");
        return;
    }

    LOG_INFO("Orchestrator: %zu of %zu agents still alive, proceeding with enforcement\n",
        alive_agents.size(), agents.size());

    // Track responses only for alive agents
    {
        std::lock_guard<std::mutex> lock(manual_tool_mutex_);
        manual_tool_responses_.clear();
        for (const auto& agent_id : alive_agents) {
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
        LOG_INFO("Orchestrator: Converted decimal address to hex: %s\n", hex_stream.str().c_str());
    }

    // Send manual tool execution only to alive agents
    for (const std::string& agent_id: alive_agents) {
        std::string params_str = parameters.dump();
        std::string message = "MANUAL_TOOL_EXEC|" + agent_id + "|" + tool_name + "|" + params_str;

        if (irc_client_) {
            irc_client_->send_message(channel, message);
            LOG_INFO("Orchestrator: Sent manual tool exec to %s\n", agent_id.c_str());
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
            LOG_INFO("Orchestrator: All agents executed consensus tool successfully\n");
            break;
        }

        // Sleep briefly to allow IRC thread to process responses
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check timeout
        if (std::chrono::steady_clock::now() - start_time > timeout) {
            LOG_INFO("Orchestrator: WARNING - Timeout waiting for manual tool execution responses\n");
            
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
            LOG_INFO("Orchestrator: Could not extract address for verification\n");
        }
    }
    
    if (address != 0) {
        // Give a moment for database writes to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        // Only verify if we have alive agents - if some agents died, skip verification
        if (alive_agents.size() < agents.size()) {
            LOG_INFO("Orchestrator: Some agents died (%zu alive of %zu total), skipping verification\n",
                alive_agents.size(), agents.size());
        } else {
            bool verified = verify_consensus_applied(alive_agents, address);
            if (verified) {
                LOG_INFO("Orchestrator: Consensus enforcement verified successfully\n");
            } else {
                LOG_INFO("Orchestrator: WARNING - Consensus enforcement verification failed\n");
            }
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

    LOG_INFO("Orchestrator: Received manual tool result from '%s': %s\n", agent_id.c_str(), status.c_str());

    // Mark agent as responded
    {
        std::lock_guard<std::mutex> lock(manual_tool_mutex_);
        if (manual_tool_responses_.find(agent_id) != manual_tool_responses_.end()) {
            manual_tool_responses_[agent_id] = true;
            LOG_INFO("Orchestrator: Marked agent '%s' as responded\n", agent_id.c_str());
        } else {
            LOG_INFO("Orchestrator: WARNING - Agent '%s' not found in tracking map\n", agent_id.c_str());
        }
    }

    // Debug logging to check what agents we're tracking AFTER update
    {
        std::lock_guard<std::mutex> lock(manual_tool_mutex_);
        LOG_INFO("Orchestrator: Current response status:\n");
        for (const auto& [id, responded] : manual_tool_responses_) {
            LOG_INFO("  - '%s': %s\n", id.c_str(), responded ? "responded" : "waiting");
        }
    }
    
    // Parse and log the result details
    try {
        json result = json::parse(result_json);
        if (result["success"]) {
            LOG_INFO("Orchestrator: Agent %s successfully executed manual tool\n", agent_id.c_str());
        } else {
            LOG_INFO("Orchestrator: Agent %s failed manual tool execution: %s\n", 
                     agent_id.c_str(), result.value("error", "unknown error").c_str());
        }
    } catch (const std::exception& e) {
        LOG_INFO("Orchestrator: Failed to parse result JSON: %s\n", e.what());
    }
}

bool Orchestrator::verify_consensus_applied(const std::set<std::string>& agents, ea_t address) {
    LOG_INFO("Orchestrator: Verifying consensus was applied by all agents at address 0x%llx\n", address);
    
    if (!tool_tracker_) {
        LOG_INFO("Orchestrator: ERROR - Tool tracker not initialized\n");
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
        LOG_INFO("Orchestrator: WARNING - No manual tool calls found for verification\n");
        return false;
    }
    
    json reference_params;
    for (const auto& [agent_id, params] : agent_params) {
        if (reference_params.is_null()) {
            reference_params = params;
        } else if (params != reference_params) {
            LOG_INFO("Orchestrator: ERROR - Agent %s applied different parameters: %s vs %s\n",
                     agent_id.c_str(), params.dump().c_str(), reference_params.dump().c_str());
            return false;
        }
    }
    
    LOG_INFO("Orchestrator: SUCCESS - All %zu agents applied identical values\n", agent_params.size());
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
    
    LOG_INFO("DEBUG: Publishing token event - Cumulative In: %d, Out: %d | Per-iter In: %d, Out: %d\n",
        cumulative_usage.input_tokens, cumulative_usage.output_tokens,
        per_iteration_usage.input_tokens, per_iteration_usage.output_tokens);
    
    // Emit standardized token event for orchestrator (use AGENT_TOKEN_UPDATE for consistency)
    event_bus_.publish(AgentEvent(AgentEvent::AGENT_TOKEN_UPDATE, "orchestrator", {
        {"agent_id", "orchestrator"},
        {"tokens", tokens_json},
        {"session_tokens", session_tokens_json}  // Per-iteration for context calc
    }));
    
    LOG_INFO("Orchestrator: Token usage - Input: %d, Output: %d (cumulative)\n",
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

    LOG_INFO("Orchestrator: Shutting down...\n");

    // Stop conflict monitor thread
    conflict_monitor_should_stop_ = true;
    if (conflict_monitor_thread_.joinable()) {
        LOG_INFO("Orchestrator: Waiting for conflict monitor thread to exit...\n");
        conflict_monitor_thread_.join();
    }

    // Stop MCP listener if running
    if (!show_ui_) {
        mcp_listener_should_stop_ = true;
        if (mcp_listener_thread_.joinable()) {
            mcp_listener_thread_.join();
        }
    }

    // Cleanup LLDB sessions for all agents before terminating them
    if (lldb_manager_) {
        LOG_INFO("Orchestrator: Cleaning up LLDB sessions for all agents...\n");
        for (const auto& [agent_id, info] : agents_) {
            lldb_manager_->cleanup_agent_sessions(agent_id);
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

    // Save profiling report
    if (profiling::ProfilingManager::instance().is_enabled()) {
        LOG_INFO("Orchestrator: Saving profiling report...\n");
        bool saved = profiling::ProfilingManager::instance().save_report(binary_name_);
        if (saved) {
            std::string report_dir = profiling::ProfilingManager::instance().get_report_directory(binary_name_);
            LOG_INFO("Orchestrator: Profiling report saved to %s\n", report_dir.c_str());
        } else {
            LOG_INFO("Orchestrator: WARNING - Failed to save profiling report\n");
        }
    }

    // Cleanup subsystems
    tool_tracker_.reset();
    merge_manager_.reset();
    agent_spawner_.reset();
    db_manager_.reset();

    LOG_INFO("Orchestrator: Shutdown complete\n");
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

            LOG_INFO("Orchestrator: Broadcasted temp segment no-go zone from %s: 0x%llX-0x%llX\n",
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

            LOG_INFO("Orchestrator: Broadcasted code cave no-go zone from %s: 0x%llX-0x%llX\n",
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

        LOG_INFO("Orchestrator: Replicating %s from %s to all other agents\n",
            tool_name.c_str(), agent_id.c_str());
    }
}

void Orchestrator::broadcast_no_go_zone(const NoGoZone& zone) {
    // Serialize the zone
    std::string message = NoGoZoneManager::serialize_zone(zone);

    // Broadcast to all agents via IRC
    if (irc_client_ && irc_client_->is_connected()) {
        irc_client_->send_message("#agents", message);
        LOG_INFO("Orchestrator: Broadcasted no-go zone via IRC: %s\n", message.c_str());
    } else {
        LOG_INFO("Orchestrator: WARNING - Could not broadcast no-go zone, IRC not connected\n");
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
            LOG_INFO("Orchestrator: Could not find database for agent %s\n", agent_id.c_str());
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
            LOG_INFO("Orchestrator: Sent patch replication to %s\n", agent_id.c_str());
        }
    }
}

bool Orchestrator::validate_lldb_connectivity() {
    // Ensure SSH keys exist
    if (!SSHKeyManager::ensure_key_pair_exists()) {
        LOG_INFO("Orchestrator: Failed to create/find SSH keys\n");
        return false;
    }

    LOG_INFO("Orchestrator: SSH keys ready at %s\n",
             SSHKeyManager::get_private_key_path().c_str());

    // Check if any devices are configured in global device pool
    if (config_.lldb.devices.empty()) {
        LOG_INFO("Orchestrator: No devices configured in global device pool\n");
        LOG_INFO("Orchestrator: Please add devices via Preferences > LLDB tab\n");
        return false;
    }

    LOG_INFO("Orchestrator: Found %zu devices in global pool\n", config_.lldb.devices.size());

    // Load workspace device overrides from lldb_config.json
    fs::path workspace_dir = fs::path("/tmp/ida_swarm_workspace") / binary_name_;
    fs::path config_path = workspace_dir / "lldb_config.json";

    json workspace_overrides;
    if (fs::exists(config_path)) {
        std::ifstream file(config_path);
        if (file) {
            try {
                file >> workspace_overrides;
                LOG_INFO("Orchestrator: Loaded workspace device overrides from %s\n",
                         config_path.string().c_str());
            } catch (const std::exception& e) {
                LOG_INFO("Orchestrator: Failed to parse lldb_config.json: %s\n", e.what());
                // Continue with empty overrides
            }
        }
    } else {
        LOG_INFO("Orchestrator: No workspace config found, treating all devices as enabled\n");
    }

    // Validate each enabled device
    int validated_devices = 0;
    int enabled_devices = 0;

    for (const auto& global_device : config_.lldb.devices) {
        // Check if device is enabled for this workspace
        bool device_enabled = true;
        std::string remote_binary_path;

        if (workspace_overrides.contains("device_overrides") &&
            workspace_overrides["device_overrides"].contains(global_device.id)) {
            const auto& override = workspace_overrides["device_overrides"][global_device.id];
            device_enabled = override.value("enabled", false);
            remote_binary_path = override.value("remote_binary_path", "");
        }

        if (!device_enabled) {
            LOG_INFO("Orchestrator: Device '%s' (%s) is disabled for this workspace\n",
                     global_device.name.c_str(), global_device.host.c_str());
            continue;
        }

        enabled_devices++;

        // Validate remote_binary_path is set
        if (remote_binary_path.empty()) {
            LOG_INFO("Orchestrator: Device '%s' (%s) is enabled but missing remote_binary_path\n",
                     global_device.name.c_str(), global_device.host.c_str());
            continue;
        }

        // Build RemoteConfig from global device
        RemoteConfig remote_cfg;
        remote_cfg.host = global_device.host;
        remote_cfg.ssh_port = global_device.ssh_port;
        remote_cfg.ssh_user = global_device.ssh_user;
        remote_cfg.debugserver_port = 0;  // Not needed for SSH-only validation

        // Validate connectivity (SSH only)
        LOG_INFO("Orchestrator: Testing device '%s' (%s) - SSH port %d...\n",
                 global_device.name.c_str(), remote_cfg.host.c_str(),
                 remote_cfg.ssh_port);

        ValidationResult result = RemoteSyncManager::validate_connectivity(remote_cfg);

        if (result.is_valid()) {
            LOG_INFO("Orchestrator: ✅ Device '%s' validation passed\n", global_device.name.c_str());
            validated_devices++;
        } else {
            LOG_INFO("Orchestrator: ❌ Device '%s' validation failed: %s\n",
                     global_device.name.c_str(), result.error_message.c_str());
        }
    }

    // Summary
    LOG_INFO("Orchestrator: Validation summary: %d/%d enabled devices validated\n",
             validated_devices, enabled_devices);

    if (validated_devices == 0) {
        if (enabled_devices == 0) {
            LOG_INFO("Orchestrator: No devices enabled for this workspace\n");
        } else {
            LOG_INFO("Orchestrator: All enabled devices failed validation\n");
        }
        return false;
    }

    LOG_INFO("Orchestrator: Connectivity validation successful! (%d device(s) reachable)\n",
             validated_devices);
    return true;
}

bool Orchestrator::cleanup_agent_directory_if_no_writes(const std::string& agent_id) {
    // Check if agent performed any write operations
    if (!tool_tracker_) {
        LOG_INFO("Orchestrator: Tool tracker not available, skipping cleanup for %s\n", agent_id.c_str());
        return false;
    }

    auto write_ops = tool_tracker_->get_agent_write_operations(agent_id);

    if (!write_ops.empty()) {
        LOG_INFO("Orchestrator: Agent %s performed %zu write operations, keeping database\n",
                 agent_id.c_str(), write_ops.size());
        return false;
    }

    // No write operations - safe to delete the database and binary to save storage
    // Keep other files like memories directory
    LOG_INFO("Orchestrator: Agent %s performed no write operations, cleaning up database/binary to save storage\n",
             agent_id.c_str());

    // Get agent's database path
    std::string agent_db = db_manager_->get_agent_database(agent_id);
    if (agent_db.empty()) {
        LOG_INFO("Orchestrator: Could not find database path for agent %s\n", agent_id.c_str());
        return false;
    }

    fs::path db_path(agent_db);
    fs::path agent_dir = db_path.parent_path();

    // Safety check: verify we're deleting from the expected workspace location
    std::string expected_prefix = "/tmp/ida_swarm_workspace/";
    if (agent_dir.string().find(expected_prefix) != 0) {
        LOG_INFO("Orchestrator: Safety check failed - agent dir '%s' not in expected workspace\n",
                 agent_dir.string().c_str());
        return false;
    }

    int files_removed = 0;

    // Delete the .i64 database file
    try {
        if (fs::exists(db_path)) {
            fs::remove(db_path);
            LOG_INFO("Orchestrator: Deleted database %s\n", db_path.filename().string().c_str());
            files_removed++;
        }
    } catch (const fs::filesystem_error& e) {
        LOG_INFO("Orchestrator: Failed to delete database: %s\n", e.what());
    }

    // Delete the binary copy (agent_id + "_" + original_binary_name)
    std::string agent_binary = db_manager_->get_agent_binary(agent_id);
    if (!agent_binary.empty()) {
        try {
            fs::path binary_path(agent_binary);
            if (fs::exists(binary_path)) {
                fs::remove(binary_path);
                LOG_INFO("Orchestrator: Deleted binary %s\n", binary_path.filename().string().c_str());
                files_removed++;
            }
        } catch (const fs::filesystem_error& e) {
            LOG_INFO("Orchestrator: Failed to delete binary: %s\n", e.what());
        }
    }

    LOG_INFO("Orchestrator: Cleaned up %d files for agent %s (memories preserved)\n",
             files_removed, agent_id.c_str());
    return files_removed > 0;
}

} // namespace llm_re::orchestrator