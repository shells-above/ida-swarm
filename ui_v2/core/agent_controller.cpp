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
            [this](AgentMessageType type, const Agent::CallbackData& data) {
                // Create a safe copy of the callback data for queued execution
                // Copy the message if present to avoid dangling pointers
                Agent::CallbackData safeCopy;
                safeCopy.json_data = data.json_data;
                
                // Deep copy the message if it exists
                std::shared_ptr<messages::Message> messageCopy;
                if (data.message) {
                    messageCopy = std::make_shared<messages::Message>(*data.message);
                }
                
                // Marshal to Qt thread with the safe copy
                QMetaObject::invokeMethod(this, [this, type, safeCopy, messageCopy]() {
                    Agent::CallbackData localData = safeCopy;
                    if (messageCopy) {
                        localData.message = messageCopy.get();
                    }
                    handleAgentMessage(type, localData);
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
    auto userMsg = std::make_shared<messages::Message>(messages::Role::User);
    userMsg->add_content(std::make_unique<messages::TextContent>(task));
    addMessageToConversation(userMsg);
    
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
    auto userMsg = std::make_shared<messages::Message>(messages::Role::User);
    userMsg->add_content(std::make_unique<messages::TextContent>(additional));
    addMessageToConversation(userMsg);
    
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
    auto userMsg = std::make_shared<messages::Message>(messages::Role::User);
    userMsg->add_content(std::make_unique<messages::TextContent>(message));
    addMessageToConversation(userMsg);
    
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
    if (memoryDock_ && agent_ && agent_->get_memory()) {
        memoryDock_->setMemory(agent_->get_memory());
    }
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
            if (memoryDock_) {
                memoryDock_->refresh();
            }
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

void AgentController::handleAgentMessage(AgentMessageType type, const Agent::CallbackData& data) {
    switch (type) {
        case AgentMessageType::Log: {
            // Log messages are special - they're system messages with log content
            if (data.message && !data.message->contents().empty()) {
                if (auto* text = dynamic_cast<const messages::TextContent*>(data.message->contents()[0].get())) {
                    // Parse log level from the text (format: "[LOG:level] message")
                    QString logText = QString::fromStdString(text->text);
                    LogEntry::Level logLevel = LogEntry::Info;
                    
                    if (logText.startsWith("[LOG:")) {
                        int endIdx = logText.indexOf(']');
                        if (endIdx > 5) {
                            int level = logText.mid(5, endIdx - 5).toInt();
                            if (level == 0) logLevel = LogEntry::Debug;
                            else if (level == 1) logLevel = LogEntry::Info;
                            else if (level == 2) logLevel = LogEntry::Warning;
                            else if (level >= 3) logLevel = LogEntry::Error;
                            logText = logText.mid(endIdx + 2); // Skip "] "
                        }
                    }
                    
                    logToConsole(logLevel, "Agent", logText);
                }
            }
            break;
        }
        
        case AgentMessageType::NewMessage: {
            // Direct message from agent - log to console but DON'T add to conversation UI
            // (we only want final grader output in the conversation)
            if (data.message) {
                // Log ALL content to console for debugging
                for (const auto& content : data.message->contents()) {
                    // Log thinking blocks
                    if (auto* thinking = dynamic_cast<const messages::ThinkingContent*>(content.get())) {
                        logToConsole(LogEntry::Debug, "Thinking", 
                                   QString::fromStdString(thinking->thinking));
                    }
                    // Log text content
                    else if (auto* text = dynamic_cast<const messages::TextContent*>(content.get())) {
                        if (!text->text.empty()) {
                            QString role = data.message->role() == messages::Role::Assistant ? "Assistant" : "Agent";
                            logToConsole(LogEntry::Info, role,
                                       QString::fromStdString(text->text));
                        }
                    }
                }
            }
            break;
        }
        
        case AgentMessageType::StateChanged: {
            // Handle state changes
            int status = data.json_data.value("status", 0);
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
            std::string toolId = data.json_data.value("tool_id", "");
            std::string toolName = data.json_data.value("tool_name", "");
            json input = data.json_data.value("input", json{});
            
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
            std::string toolId = data.json_data.value("tool_id", "");
            std::string toolName = data.json_data.value("tool_name", "");
            json result = data.json_data.value("result", json{});
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
            std::string report = data.json_data.value("report", "");
            QString qReport = QString::fromStdString(report);
            
            // Create final report message using API message type
            auto reportMsg = std::make_shared<messages::Message>(messages::Role::Assistant);
            reportMsg->add_content(std::make_unique<messages::TextContent>(report));
            
            // Create metadata for the report
            MessageMetadata metadata;
            metadata.id = QUuid::createUuid();
            metadata.timestamp = QDateTime::currentDateTime();

            if (conversationModel_) {
                conversationModel_->addMessage(reportMsg, metadata);
            }
            emit finalReportGenerated(qReport);
            break;
        }
    }
    
    // Check for memory updates efficiently using version counter
    if (agent_ && agent_->get_memory() && memoryDock_) {
        uint64_t currentVersion = agent_->get_memory()->get_version();
        if (currentVersion != lastMemoryVersion_) {
            lastMemoryVersion_ = currentVersion;
            memoryDock_->refresh();
        }
    }
}

// ============================================================================
// Helper Methods
// ============================================================================

void AgentController::addMessageToConversation(std::shared_ptr<messages::Message> msg) {
    if (conversationModel_) {
        MessageMetadata metadata;
        metadata.id = QUuid::createUuid();
        metadata.timestamp = QDateTime::currentDateTime();
        
        conversationModel_->addMessage(msg, metadata);
        
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
    
    // Simply refresh the memory dock - it will pull data directly from BinaryMemory
    memoryDock_->refresh();
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