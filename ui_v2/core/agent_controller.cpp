#include "ui_v2_common.h"
#include "agent_controller.h"
#include "core/config.h"
#include "json_utils.h"
#include <set>
#include <fstream>
#include "../views/conversation_view.h"
#include "../views/memory_dock.h"
#include "../views/tool_execution_dock.h"
#include "../views/console_dock.h"
#include "../models/tool_execution.h"

namespace llm_re::ui_v2 {

AgentController::AgentController(QObject* parent)
    : QObject(parent) {
}

AgentController::~AgentController() {
    shutdown();
}

bool AgentController::initialize(const Config& config) {
    if (isInitialized_) {
        return true;
    }
    
    try {
        // Create agent directly
        agent_ = std::make_unique<Agent>(config);
        
        // Set the message callback to handle agent messages
        agent_->set_message_callback(
            [this](AgentMessageType type, const json& data) {
                // Marshal to Qt thread if needed
                QMetaObject::invokeMethod(this, [this, type, data]() {
                    handleAgentMessage(type, data);
                }, Qt::QueuedConnection);
            });
        
        // Start agent worker thread
        agent_->start();
        
        isInitialized_ = true;
        emit statusChanged("Agent initialized");
        
        return true;
        
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Failed to initialize: %1").arg(e.what()));
        return false;
    }
}

void AgentController::shutdown() {
    if (!isInitialized_) {
        return;
    }
    
    // Stop and cleanup agent
    if (agent_) {
        agent_->stop();
        agent_.reset();
    }
    
    isInitialized_ = false;
}

// ============================================================================
// Agent Control
// ============================================================================

void AgentController::executeTask(const std::string& task) {
    if (!agent_) {
        emit errorOccurred("Agent not initialized");
        return;
    }
    
    // Clear conversation if starting fresh
    if (conversationModel_) {
        conversationModel_->clearMessages();
    }
    
    // Add user message to conversation
    auto userMsg = std::make_unique<Message>(QString::fromStdString(task), MessageRole::User);
    userMsg->metadata().timestamp = QDateTime::currentDateTime();
    addMessageToConversation(std::move(userMsg));
    
    // Submit task to agent
    agent_->set_task(task);
    
    // Generate a task ID for tracking
    currentTaskId_ = QUuid::createUuid().toString();
}

void AgentController::stopExecution() const {
    if (agent_) {
        agent_->stop();
    }
}

void AgentController::resumeExecution() const {
    if (agent_) {
        agent_->resume();
    }
}

void AgentController::continueWithTask(const std::string& additional) {
    if (!agent_ || !canContinue()) {
        emit errorOccurred("Cannot continue - agent must be completed or idle");
        return;
    }
    
    // Add user message
    auto userMsg = std::make_unique<Message>(QString::fromStdString(additional), MessageRole::User);
    userMsg->metadata().timestamp = QDateTime::currentDateTime();
    addMessageToConversation(std::move(userMsg));
    
    // Continue task
    agent_->continue_with_task(additional);
    currentTaskId_ = QUuid::createUuid().toString();
}

void AgentController::injectUserMessage(const std::string& message) {
    if (!agent_ || !isRunning()) {
        emit errorOccurred("Cannot inject message - agent not running");
        return;
    }
    
    // Add to UI immediately
    auto userMsg = std::make_unique<Message>(QString::fromStdString(message), MessageRole::User);
    userMsg->metadata().timestamp = QDateTime::currentDateTime();
    addMessageToConversation(std::move(userMsg));
    
    // Inject into agent
    agent_->inject_user_message(message);
}

// ============================================================================
// State Queries
// ============================================================================

bool AgentController::isRunning() const {
    return agent_ && agent_->is_running();
}

bool AgentController::isPaused() const {
    return agent_ && agent_->is_paused();
}

bool AgentController::isCompleted() const {
    return agent_ && agent_->is_completed();
}

bool AgentController::canContinue() const {
    return agent_ && (agent_->is_completed() || agent_->is_idle());
}

std::string AgentController::getLastError() const {
    if (!agent_) {
        return "";
    }
    
    return agent_->get_last_error();
}

// ============================================================================
// UI Component Connections
// ============================================================================

void AgentController::connectConversationView(ConversationView* view) {
    conversationView_ = view;
    if (view) {
        conversationModel_ = view->model();
    }
}

void AgentController::connectMemoryDock(MemoryDock* dock) {
    memoryDock_ = dock;
    updateMemoryView();
}

void AgentController::connectToolDock(ToolExecutionDock* dock) {
    toolDock_ = dock;
    if (toolDock_) {
        toolDock_->setAgentController(this);
    }
}

void AgentController::connectConsoleDock(ConsoleDock* dock) {
    consoleDock_ = dock;
}

// ============================================================================
// Configuration
// ============================================================================

void AgentController::updateConfig(const Config& config) {
    if (agent_) {
        // Agent doesn't have update_config, would need to restart
        shutdown();
        initialize(config);
    }
}

// ============================================================================
// Memory Management
// ============================================================================

void AgentController::saveMemory(const QString& path) {
    if (agent_ && agent_->get_memory()) {
        // Export memory snapshot and save to file
        json snapshot = agent_->get_memory()->export_memory_snapshot();
        std::ofstream file(path.toStdString());
        if (file.is_open()) {
            file << snapshot.dump(2);
            file.close();
        }
    }
}

void AgentController::loadMemory(const QString& path) {
    if (agent_ && agent_->get_memory()) {
        // Load memory snapshot from file
        std::ifstream file(path.toStdString());
        if (file.is_open()) {
            json snapshot;
            file >> snapshot;
            file.close();
            agent_->get_memory()->import_memory_snapshot(snapshot);
            updateMemoryView();
        }
    }
}

// ============================================================================
// Statistics
// ============================================================================

api::TokenUsage AgentController::getTokenUsage() const {
    return agent_ ? agent_->get_token_usage() : api::TokenUsage{};
}

json AgentController::getAgentState() const {
    return agent_ ? agent_->get_state_json() : json{};
}

// ============================================================================
// Manual Tool Execution
// ============================================================================

QJsonObject AgentController::executeManualTool(const QString& toolName, const QJsonObject& parameters) {
    if (!agent_) {
        return QJsonObject{
            {"success", false},
            {"error", "Agent not initialized"}
        };
    }
    
    // Direct conversion without string roundtrip
    json params = JsonUtils::qJsonToJson(parameters);
    
    // Execute the tool through agent's tool registry
    try {
        json result = agent_->execute_manual_tool(toolName.toStdString(), params);
        return JsonUtils::jsonToQJson(result);
    } catch (const std::exception& e) {
        return QJsonObject{
            {"success", false},
            {"error", QString::fromStdString(e.what())}
        };
    }
}

QJsonArray AgentController::getAvailableTools() const {
    if (!agent_) {
        return QJsonArray{};
    }
    
    // Get tools from agent and convert to QJsonArray
    json tools = agent_->get_available_tools();
    return JsonUtils::jsonArrayToQJson(tools);
}

// ============================================================================
// Agent Message Handler
// ============================================================================

void AgentController::handleAgentMessage(AgentMessageType type, const json& data) {
    switch (type) {
        case AgentMessageType::Log: {
            // Handle log messages
            int level = data.value("level", 0);
            std::string message = data.value("message", "");
            
            LogEntry::Level logLevel = LogEntry::Debug;
            if (level == 1) logLevel = LogEntry::Info;
            else if (level == 2) logLevel = LogEntry::Warning;
            else if (level >= 3) logLevel = LogEntry::Error;
            
            logToConsole(logLevel, "Agent", QString::fromStdString(message));
            break;
        }
        
        case AgentMessageType::ApiMessage: {
            // Handle API messages (thinking blocks, responses, etc.)
            std::string msgType = data.value("type", "");
            json content = data.value("content", json{});
            
            if (msgType == "thinking") {
                // Add thinking message to conversation
                auto msg = std::make_unique<Message>();
                msg->setThinkingContent(QString::fromStdString(content.value("text", "")));
                msg->setRole(MessageRole::Assistant);
                msg->metadata().timestamp = QDateTime::currentDateTime();
                addMessageToConversation(std::move(msg));
            } else if (msgType == "response") {
                // Add assistant response to conversation
                auto msg = std::make_unique<Message>();
                msg->setContent(QString::fromStdString(content.value("text", "")));
                msg->setRole(MessageRole::Assistant);
                msg->setType(MessageType::Text);
                msg->metadata().timestamp = QDateTime::currentDateTime();
                addMessageToConversation(std::move(msg));
            } else if (msgType == "token_usage") {
                // Update token usage
                int input = content.value("input_tokens", 0);
                int output = content.value("output_tokens", 0);
                double cost = content.value("estimated_cost", 0.0);
                emit tokenUsageUpdated(input, output, cost);
            }
            break;
        }
        
        case AgentMessageType::StateChanged: {
            // Handle state changes
            int status = data.value("status", 0);
            AgentState::Status agentStatus = static_cast<AgentState::Status>(status);
            
            QString statusStr = agentStatusToString(agentStatus);
            emit statusChanged(statusStr);
            
            switch (agentStatus) {
                case AgentState::Status::Running:
                    emit agentStarted();
                    break;
                case AgentState::Status::Paused:
                    emit agentPaused();
                    break;
                case AgentState::Status::Completed:
                    emit agentCompleted();
                    break;
                default:
                    break;
            }
            break;
        }
        
        case AgentMessageType::ToolStarted: {
            // Handle tool execution started
            std::string toolId = data.value("tool_id", "");
            std::string toolName = data.value("tool_name", "");
            json input = data.value("input", json{});
            
            QString qToolId = QString::fromStdString(toolId);
            QString qToolName = QString::fromStdString(toolName);
            
            // Convert parameters
            QJsonObject paramsObj = JsonUtils::jsonToQJson(input);
            
            // Log to console
            logToConsole(LogEntry::Info, "Tool", 
                        QString("Executing tool: %1").arg(qToolName));
            
            // Update tool dock
            if (toolDock_) {
                paramsObj["__tool_id"] = qToolId;
                QUuid execId = toolDock_->startExecution(qToolName, paramsObj);
                toolIdToExecId_[qToolId] = execId;
            }
            break;
        }
        
        case AgentMessageType::ToolExecuted: {
            // Handle tool execution completed
            std::string toolId = data.value("tool_id", "");
            std::string toolName = data.value("tool_name", "");
            json result = data.value("result", json{});
            bool success = !result.contains("error");
            
            QString qToolId = QString::fromStdString(toolId);
            QString qToolName = QString::fromStdString(toolName);
            
            // Log to console
            logToConsole(success ? LogEntry::Info : LogEntry::Warning, "Tool",
                        QString("Tool %1: %2")
                            .arg(qToolName)
                            .arg(success ? "succeeded" : "failed"));
            
            // Update tool dock
            if (toolDock_) {
                auto it = toolIdToExecId_.find(qToolId);
                if (it != toolIdToExecId_.end()) {
                    QString output = QString::fromStdString(result.dump());
                    toolDock_->completeExecution(it->second, success, output);
                    toolIdToExecId_.erase(it);
                }
            }
            break;
        }
        
        case AgentMessageType::FinalReport: {
            // Handle final report
            std::string report = data.value("report", "");
            QString qReport = QString::fromStdString(report);
            
            // Create final report message
            auto reportMsg = std::make_unique<Message>(qReport, MessageRole::Assistant);
            reportMsg->setType(MessageType::Analysis);
            reportMsg->metadata().timestamp = QDateTime::currentDateTime();
            reportMsg->metadata().tags << "final-report";
            
            addMessageToConversation(std::move(reportMsg));
            emit finalReportGenerated(qReport);
            break;
        }
    }
    
    // Check for memory updates
    static json lastMemorySnapshot;
    if (agent_ && agent_->get_memory()) {
        json currentSnapshot = agent_->get_memory()->export_memory_snapshot();
        if (currentSnapshot != lastMemorySnapshot) {
            lastMemorySnapshot = currentSnapshot;
            updateMemoryView();
        }
    }
}

// ============================================================================
// Helper Methods
// ============================================================================

void AgentController::addMessageToConversation(std::unique_ptr<Message> msg) {
    if (conversationModel_) {
        conversationModel_->addMessage(std::move(msg));
        
        // Auto-scroll conversation view
        if (conversationView_) {
            conversationView_->scrollToBottom();
        }
    }
}

void AgentController::logToConsole(LogEntry::Level level, const QString& category, 
                                      const QString& message, const QJsonObject& metadata) {
    if (!consoleDock_) {
        return;
    }
    
    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.level = level;
    entry.category = category;
    entry.message = message;
    
    // Only set metadata if it's not empty
    if (!metadata.isEmpty()) {
        entry.metadata = metadata;
    }
    
    consoleDock_->addLog(entry);
}

void AgentController::updateMemoryView() {
    if (!memoryDock_ || !agent_ || !agent_->get_memory()) {
        return;
    }
    
    // Get memory directly from BinaryMemory
    std::shared_ptr<BinaryMemory> memory = agent_->get_memory();
    
    // Get all analyses from memory
    std::vector<llm_re::AnalysisEntry> analyses = memory->get_analysis();
    
    // Track which keys are still present
    std::set<QString> currentKeys;
    
    for (const auto& entry : analyses) {
        QString key = QString::fromStdString(entry.key);
        currentKeys.insert(key);
            
        // Check if this is a new or existing entry
        auto it = memoryKeyToId_.find(key);
        
        MemoryEntry memEntry;
        if (it != memoryKeyToId_.end()) {
            // Existing entry - update it
            memEntry = memoryDock_->entry(it->second);
            memEntry.title = key;
        } else {
            // New entry - create new UUID and add to map
            memEntry.id = QUuid::createUuid();
            memEntry.title = key;
            memoryKeyToId_[key] = memEntry.id;
        }
        
        // Update fields from AnalysisEntry
        if (entry.address.has_value()) {
            memEntry.address = QString("0x%1").arg(entry.address.value(), 0, 16);
        } else {
            memEntry.address = "";
        }
        
        memEntry.analysis = QString::fromStdString(entry.content);
        memEntry.timestamp = QDateTime::fromSecsSinceEpoch(entry.timestamp);
            
        // Add or update entry
        if (it != memoryKeyToId_.end()) {
            memoryDock_->updateEntry(memEntry.id, memEntry);
        } else {
            memoryDock_->addEntry(memEntry);
        }
    }
    
    // Remove entries that are no longer in the snapshot
    for (auto it = memoryKeyToId_.begin(); it != memoryKeyToId_.end(); ) {
        if (currentKeys.find(it->first) == currentKeys.end()) {
            // This key is no longer in the snapshot, remove it
            memoryDock_->removeEntry(it->second);
            it = memoryKeyToId_.erase(it);
        } else {
            ++it;
        }
    }
}

QString AgentController::agentStatusToString(AgentState::Status status) const {
    switch (status) {
        case AgentState::Status::Idle:
            return "Idle";
        case AgentState::Status::Running:
            return "Running";
        case AgentState::Status::Paused:
            return "Paused";
        case AgentState::Status::Completed:
            return "Completed";
        default:
            return "Unknown";
    }
}

} // namespace llm_re::ui_v2