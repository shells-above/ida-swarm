#include "ui_v2_common.h"
#include "agent_controller.h"
// Now it's safe to include agent.h because ui_v2_common.h already set up Qt + kernwin
#include "agent/agent.h"
#include "core/config.h"
#include "api/message_types.h"
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
        config_ = std::make_unique<Config>(config);
        
        // Check if API key is configured
        if (config_->api.api_key.empty()) {
            emit errorOccurred("ERROR: No API key configured! Please set your Anthropic API key in the configuration.");
            return false;
        }
        
        agent_ = std::make_unique<REAgent>(*config_);
        
        // Set up the unified message callback
        agent_->set_message_callback(
            [this](AgentMessageType type, const json& data) {
                // Convert to QString for thread safety with Qt
                QString dataStr = QString::fromStdString(data.dump());
                QMetaObject::invokeMethod(this, "onAgentMessage", 
                    Qt::QueuedConnection,
                    Q_ARG(int, static_cast<int>(type)),
                    Q_ARG(QString, dataStr));
            });
        
        // Start the agent worker thread
        agent_->start();
        
        isInitialized_ = true;
        return true;
        
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Failed to initialize agent: %1").arg(e.what()));
        return false;
    }
}

void AgentController::shutdown() {
    if (agent_) {
        agent_->stop();
        agent_->cleanup_thread();
        agent_.reset();
    }
    isInitialized_ = false;
}

void AgentController::executeTask(const std::string& task) {
    if (!agent_) {
        emit errorOccurred("Agent not initialized");
        return;
    }
    
    sessionStart_ = std::chrono::steady_clock::now();
    currentIteration_ = 0;
    toolIdToMessageId_.clear();
    
    // Clear conversation model if connected
    if (conversationModel_) {
        conversationModel_->clearMessages();
    }
    
    // Add user message to conversation
    auto userMsg = std::make_unique<Message>(QString::fromStdString(task), MessageRole::User);
    userMsg->metadata().timestamp = QDateTime::currentDateTime();
    addMessageToConversation(std::move(userMsg));
    
    // Execute task
    agent_->set_task(task);
    emit agentStarted();
}

void AgentController::stopExecution() {
    if (agent_) {
        agent_->stop();
        emit agentStopped();
    }
}

void AgentController::resumeExecution() {
    if (agent_ && agent_->is_paused()) {
        agent_->resume();
        emit agentStarted();
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
    
    agent_->continue_with_task(additional);
    emit agentStarted();
}

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
    return agent_ ? agent_->get_last_error() : "";
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
    
    // Inject into agent's pending queue
    agent_->inject_user_message(message);
}

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
}

void AgentController::connectConsoleDock(ConsoleDock* dock) {
    consoleDock_ = dock;
}

void AgentController::updateConfig(const Config& config) {
    config_ = std::make_unique<Config>(config);
    // Note: Agent needs to be reinitialized for config changes to take effect
}

void AgentController::saveMemory(const QString& path) {
    if (agent_) {
        agent_->save_memory(path.toStdString());
    }
}

void AgentController::loadMemory(const QString& path) {
    if (agent_) {
        agent_->load_memory(path.toStdString());
        updateMemoryView();
    }
}

api::TokenUsage AgentController::getTokenUsage() const {
    return agent_ ? agent_->get_token_usage() : api::TokenUsage{};
}

json AgentController::getAgentState() const {
    return agent_ ? agent_->get_state_json() : json{};
}

void AgentController::onAgentMessage(int messageType, const QString& dataStr) {
    try {
        json data = json::parse(dataStr.toStdString());
        
        switch (static_cast<AgentMessageType>(messageType)) {
            case AgentMessageType::Log:
                handleLogMessage(data);
                break;
            case AgentMessageType::ApiMessage:
                handleApiMessage(data);
                break;
            case AgentMessageType::StateChanged:
                handleStateChanged(data);
                break;
            case AgentMessageType::ToolStarted:
                handleToolStarted(data);
                break;
            case AgentMessageType::ToolExecuted:
                handleToolExecuted(data);
                break;
            case AgentMessageType::FinalReport:
                handleFinalReport(data);
                break;
        }
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error processing agent message: %1").arg(e.what()));
    }
}

void AgentController::handleLogMessage(const json& data) {
    LogLevel level = static_cast<LogLevel>(data["level"].get<int>());
    std::string message = data["message"];
    
    // Send to console dock if available
    if (consoleDock_) {
        LogEntry entry;
        entry.timestamp = QDateTime::currentDateTime();
        entry.level = static_cast<LogEntry::Level>(static_cast<int>(level));
        entry.category = "Agent";
        entry.message = QString::fromStdString(message);
        
        // Add metadata if available
        if (data.contains("metadata")) {
            QString metadataStr = QString::fromStdString(data["metadata"].dump());
            entry.metadata = QJsonDocument::fromJson(metadataStr.toUtf8()).object();
        }
        
        consoleDock_->addLog(entry);
    }
    
    // Only show important messages in conversation
    bool showInConversation = false;
    
    switch (level) {
        case LogLevel::ERROR:
            showInConversation = true;
            break;
        case LogLevel::WARNING:
            // Only show warnings if they're user-facing
            showInConversation = message.find("Failed") != std::string::npos ||
                               message.find("Error") != std::string::npos;
            break;
        case LogLevel::INFO:
            // Only show specific info messages
            showInConversation = message.find("Starting new task") != std::string::npos ||
                               message.find("Task completed") != std::string::npos ||
                               message.find("Final report") != std::string::npos;
            break;
        default:
            break;
    }
    
    if (showInConversation) {
        auto logMsg = std::make_unique<Message>(QString::fromStdString(message), MessageRole::System);
        logMsg->metadata().timestamp = QDateTime::currentDateTime();
        
        // Set appropriate message type based on log level
        switch (level) {
            case LogLevel::ERROR:
                logMsg->setType(MessageType::Error);
                break;
            case LogLevel::WARNING:
                logMsg->setType(MessageType::Warning);
                break;
            default:
                logMsg->setType(MessageType::Info);
                break;
        }
        
        addMessageToConversation(std::move(logMsg));
    }
}

void AgentController::handleApiMessage(const json& data) {
    std::string type = data["type"];
    json content = data["content"];
    int iteration = data["iteration"];
    
    // Send API info to console
    if (consoleDock_) {
        LogEntry entry;
        entry.timestamp = QDateTime::currentDateTime();
        entry.level = LogEntry::Debug;
        entry.category = "API";
        entry.message = QString("Iteration %1: %2").arg(iteration).arg(QString::fromStdString(type));
        
        // Add token usage if available
        if (content.contains("usage")) {
            json usage = content["usage"];
            entry.metadata = QJsonObject{
                {"iteration", iteration},
                {"type", QString::fromStdString(type)},
                {"input_tokens", usage.value("input_tokens", 0)},
                {"output_tokens", usage.value("output_tokens", 0)},
                {"cache_read_tokens", usage.value("cache_read_tokens", 0)},
                {"cache_creation_tokens", usage.value("cache_creation_tokens", 0)},
                {"estimated_cost", usage.value("estimated_cost", 0.0)}
            };
        } else {
            entry.metadata = QJsonObject{
                {"iteration", iteration},
                {"type", QString::fromStdString(type)}
            };
        }
        
        consoleDock_->addLog(entry);
    }
    
    if (type == "REQUEST" || type == "RESPONSE") {
        currentIteration_ = iteration;
        emit iterationChanged(iteration);
        
        if (type == "RESPONSE" && content.contains("content")) {
            // Convert API response to UI message
            try {
                // Create an assistant message from the response
                messages::Message apiMsg(messages::Role::Assistant);
                
                // Parse the content array
                if (content["content"].is_array()) {
                    for (const auto& item : content["content"]) {
                        if (!item.contains("type")) continue;
                        
                        std::string contentType = item["type"];
                        if (contentType == "text") {
                            if (auto text = messages::TextContent::from_json(item)) {
                                apiMsg.add_content(std::move(text));
                            }
                        } else if (contentType == "thinking") {
                            if (auto thinking = messages::ThinkingContent::from_json(item)) {
                                apiMsg.add_content(std::move(thinking));
                            }
                        } else if (contentType == "tool_use") {
                            if (auto toolUse = messages::ToolUseContent::from_json(item)) {
                                apiMsg.add_content(std::move(toolUse));
                            }
                        }
                    }
                }
                
                // Convert to UI message but only log to console, don't add to conversation
                auto uiMsg = convertApiMessage(apiMsg);
                if (uiMsg && (!uiMsg->content().isEmpty() || !uiMsg->thinkingContent().isEmpty())) {
                    // Log to console but don't add to conversation view
                    if (consoleDock_) {
                        LogEntry entry;
                        entry.timestamp = QDateTime::currentDateTime();
                        entry.level = LogEntry::Info;
                        entry.category = "Assistant";
                        
                        QString contentToLog = uiMsg->content();
                        if (contentToLog.isEmpty() && !uiMsg->thinkingContent().isEmpty()) {
                            contentToLog = "[Thinking] " + uiMsg->thinkingContent();
                        }
                        
                        if (!contentToLog.isEmpty()) {
                            entry.message = contentToLog;
                            entry.metadata = QJsonObject{
                                {"type", static_cast<int>(uiMsg->type())},
                                {"has_thinking", !uiMsg->thinkingContent().isEmpty()}
                            };
                            consoleDock_->addLog(entry);
                        }
                    }
                    // Don't add to conversation view - commented out
                    // addMessageToConversation(std::move(uiMsg));
                }
            } catch (const std::exception& e) {
                emit errorOccurred(QString("Failed to convert API response: %1").arg(e.what()));
            }
        }
        
        // Update token usage if available
        if (content.contains("usage")) {
            json usage = content["usage"];
            int inputTokens = usage.value("input_tokens", 0);
            int outputTokens = usage.value("output_tokens", 0);
            double cost = usage.value("estimated_cost", 0.0);
            emit tokenUsageUpdated(inputTokens, outputTokens, cost);
        }
    }
}

void AgentController::handleStateChanged(const json& data) {
    int status = data["status"];
    AgentState::Status agentStatus = static_cast<AgentState::Status>(status);
    
    QString statusStr;
    switch (agentStatus) {
        case AgentState::Status::Idle:
            statusStr = "Idle";
            break;
        case AgentState::Status::Running:
            statusStr = "Running";
            emit agentStarted();
            break;
        case AgentState::Status::Paused:
            statusStr = "Paused";
            emit agentPaused();
            break;
        case AgentState::Status::Completed:
            statusStr = "Completed";
            emit agentCompleted();
            break;
    }
    
    emit statusChanged(statusStr);
}

void AgentController::handleToolStarted(const json& data) {
    QString toolId = QString::fromStdString(data["tool_id"]);
    QString toolName = QString::fromStdString(data["tool_name"]);
    json input = data["input"];
    
    // Send detailed info to console
    if (consoleDock_) {
        LogEntry entry;
        entry.timestamp = QDateTime::currentDateTime();
        entry.level = LogEntry::Info;
        entry.category = "Tool";
        entry.message = QString("Executing tool: %1 (ID: %2)").arg(toolName).arg(toolId);
        
        // Add input as metadata
        QString inputStr = QString::fromStdString(input.dump());
        entry.metadata = QJsonObject{
            {"tool_id", toolId},
            {"tool_name", toolName},
            {"input", QJsonDocument::fromJson(inputStr.toUtf8()).object()}
        };
        
        consoleDock_->addLog(entry);
    }
    
    // Convert json to QJsonObject for tool dock
    QString jsonStr = QString::fromStdString(input.dump());
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    
    // Update tool dock if connected
    if (toolDock_) {
        QUuid execId = toolDock_->startExecution(toolName, doc.object());
        // Store the execution ID for later updates
        toolIdToExecId_[toolId] = execId;
    }
}

void AgentController::handleToolExecuted(const json& data) {
    QString toolId = QString::fromStdString(data["tool_id"]);
    QString toolName = QString::fromStdString(data["tool_name"]);
    json result = data["result"];
    
    // Send result to console
    if (consoleDock_) {
        LogEntry entry;
        entry.timestamp = QDateTime::currentDateTime();
        entry.level = LogEntry::Info;
        entry.category = "Tool";
        entry.message = QString("Tool completed: %1 (ID: %2)").arg(toolName).arg(toolId);
        
        // Add result as metadata (truncate if too large)
        QString resultStr = QString::fromStdString(result.dump());
        if (resultStr.length() > 1000) {
            resultStr = resultStr.left(997) + "...";
        }
        
        entry.metadata = QJsonObject{
            {"tool_id", toolId},
            {"tool_name", toolName},
            {"result", resultStr}
        };
        
        consoleDock_->addLog(entry);
    }
    
    // Tool execution messages are no longer added to conversation, so no update needed
    // The tool dock handles its own updates via toolIdToExecId_ mapping
    
    // Update tool dock
    if (toolDock_ && toolIdToExecId_.contains(toolId)) {
        QUuid execId = toolIdToExecId_[toolId];
        QString output = QString::fromStdString(result.dump());
        
        // Check multiple ways a tool result might indicate failure
        bool success = true;
        
        // Check for success field
        if (result.contains("success") && result["success"].is_boolean()) {
            success = result["success"].get<bool>();
        }
        // Check for error field (boolean or string)
        else if (result.contains("error")) {
            if (result["error"].is_boolean()) {
                success = !result["error"].get<bool>();
            } else if (result["error"].is_string()) {
                // Non-empty error string indicates failure
                success = result["error"].get<std::string>().empty();
            }
        }
        // Check for failed field
        else if (result.contains("failed") && result["failed"].is_boolean()) {
            success = !result["failed"].get<bool>();
        }
        
        toolDock_->completeExecution(execId, success, output);
    }
    
    updateMemoryView();
}

void AgentController::handleFinalReport(const json& data) {
    QString report = QString::fromStdString(data["report"]);
    
    // Create final report message
    auto reportMsg = std::make_unique<Message>(report, MessageRole::Assistant);
    reportMsg->setType(MessageType::Analysis);
    reportMsg->metadata().timestamp = QDateTime::currentDateTime();
    reportMsg->metadata().tags << "final-report";
    
    addMessageToConversation(std::move(reportMsg));
    
    emit finalReportGenerated(report);
}

std::unique_ptr<Message> AgentController::convertApiMessage(const messages::Message& apiMsg) {
    auto uiMsg = std::make_unique<Message>();
    
    // Set role
    uiMsg->setRole(convertMessageRole(apiMsg.role()));
    
    // Set type
    uiMsg->setType(inferMessageType(apiMsg));
    
    // Set timestamp
    uiMsg->metadata().timestamp = QDateTime::currentDateTime();
    
    // Build content from message parts
    QString content;
    QString thinkingContent;
    QTextStream stream(&content);
    QTextStream thinkingStream(&thinkingContent);
    bool hasToolError = false;
    
    for (const std::unique_ptr<messages::Content>& contentPtr: apiMsg.contents()) {
        if (auto text = dynamic_cast<const messages::TextContent*>(contentPtr.get())) {
            stream << QString::fromStdString(text->text);
        } else if (auto thinking = dynamic_cast<const messages::ThinkingContent*>(contentPtr.get())) {
            // Collect thinking content
            if (!thinkingContent.isEmpty()) {
                thinkingStream << "\n\n";
            }
            thinkingStream << QString::fromStdString(thinking->thinking);
        }
        // else if (auto toolUse = dynamic_cast<const messages::ToolUseContent*>(contentPtr.get())) {
        //     // Add tool use to content stream instead of directly to message
        //     if (!content.isEmpty()) {
        //         stream << "\n";
        //     }
        //     stream << QString("[Tool: %1]").arg(QString::fromStdString(toolUse->name));
        // } else if (auto toolResult = dynamic_cast<const messages::ToolResultContent*>(contentPtr.get())) {
        //     // Add tool result to content stream
        //     if (!content.isEmpty()) {
        //         stream << "\n";
        //     }
        //     if (toolResult->is_error) {
        //         stream << QString("[Tool Error: %1]").arg(QString::fromStdString(toolResult->content));
        //         hasToolError = true;
        //     } else {
        //         stream << QString("[Tool Result]");
        //     }
        // }
    }
    
    // Always set content, even if empty (message might have only thinking content)
    uiMsg->setContent(content);
    
    if (!thinkingContent.isEmpty()) {
        uiMsg->setThinkingContent(thinkingContent);
    }
    
    // Set error type if there was a tool error
    if (hasToolError) {
        uiMsg->setType(MessageType::Error);
    }
    
    return uiMsg;
}

MessageRole AgentController::convertMessageRole(messages::Role role) {
    switch (role) {
        case messages::Role::User:
            return MessageRole::User;
        case messages::Role::Assistant:
            return MessageRole::Assistant;
        case messages::Role::System:
            return MessageRole::System;
        default:
            return MessageRole::User;
    }
}

MessageType AgentController::inferMessageType(const messages::Message& msg) {
    // Default based on role
    if (msg.role() == messages::Role::Assistant) {
        return MessageType::Analysis;
    }
    
    return MessageType::Text;
}

// Tool content processing is now handled inline in convertApiMessage

void AgentController::addMessageToConversation(std::unique_ptr<Message> msg) {
    if (conversationModel_) {
        // Log assistant messages to console
        if (consoleDock_ && msg->role() == MessageRole::Assistant) {
            LogEntry entry;
            entry.timestamp = QDateTime::currentDateTime();
            entry.level = LogEntry::Info;
            entry.category = "Assistant";
            
            // Get the actual content to log
            QString contentToLog = msg->content();
            
            // If there's no regular content but there is thinking content, log that
            if (contentToLog.isEmpty() && !msg->thinkingContent().isEmpty()) {
                contentToLog = "[Thinking] " + msg->thinkingContent();
            }
            
            // Only log if there's actual content
            if (!contentToLog.isEmpty()) {
                entry.message = contentToLog;
                
                // Add metadata
                entry.metadata = QJsonObject{
                    {"type", static_cast<int>(msg->type())},
                    {"has_thinking", !msg->thinkingContent().isEmpty()}
                };
                
                consoleDock_->addLog(entry);
            }
        }
        
        conversationModel_->addMessage(std::move(msg));
        
        // Auto-scroll conversation view
        if (conversationView_) {
            conversationView_->scrollToBottom();
        }
    }
}


void AgentController::updateMemoryView() {
    if (!memoryDock_ || !agent_) {
        return;
    }
    
    auto memory = agent_->get_memory();
    if (memory) {
        // Get memory snapshot and update dock
        json snapshot = memory->export_memory_snapshot();
        
        // Convert analysis entries to MemoryEntry objects
        memoryDock_->clearEntries();
        
        // The key is "analyses" not "analysis_entries"
        if (snapshot.contains("analyses") && snapshot["analyses"].is_array()) {
            for (const auto& entry : snapshot["analyses"]) {
                MemoryEntry memEntry;
                memEntry.id = QUuid::createUuid();
                
                // Map fields from the actual memory structure
                // Memory has: key, content, type, address (optional), related_addresses, timestamp
                
                // Extract title from key field (memory uses "key" but UI uses "title")
                QString key = QString::fromStdString(entry.value("key", ""));
                memEntry.title = key; // Use key as title
                
                // Use address if available, otherwise use the key as a fallback
                if (entry.contains("address") && !entry["address"].is_null() && 
                    !entry["address"].get<std::string>().empty()) {
                    memEntry.address = QString::fromStdString(entry["address"]);
                    } else {
                        // Empty address - the view will handle this
                        memEntry.address = "";
                    }
                
                // Content is the actual analysis text
                memEntry.analysis = QString::fromStdString(entry.value("content", ""));
                
                // Use the stored timestamp if available
                if (entry.contains("timestamp")) {
                    memEntry.timestamp = QDateTime::fromSecsSinceEpoch(entry["timestamp"]);
                } else {
                    memEntry.timestamp = QDateTime::currentDateTime();
                }
                
                memoryDock_->addEntry(memEntry);
            }
        }
    }
}

} // namespace llm_re::ui_v2