#include "ui_v2_common.h"
#include "agent_controller.h"
// Now it's safe to include agent.h because ui_v2_common.h already set up Qt + kernwin
#include "agent/agent.h"
#include "core/config.h"
#include "api/message_types.h"
#include "../views/conversation_view.h"
#include "../views/memory_dock.h"
#include "../views/tool_execution_dock.h"
#include "../views/statistics_dock.h"
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
        emit errorOccurred("DEBUG: Agent initialized and started successfully");
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
    emit errorOccurred(QString("DEBUG: executeTask called with: %1").arg(QString::fromStdString(task)));
    
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

void AgentController::connectStatsDock(StatisticsDock* dock) {
    statsDock_ = dock;
    updateStatistics();
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
    emit errorOccurred(QString("DEBUG: Received agent message type: %1").arg(messageType));
    
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
    
    // Create log message for conversation - make INFO logs visible for debugging
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

void AgentController::handleApiMessage(const json& data) {
    std::string type = data["type"];
    json content = data["content"];
    int iteration = data["iteration"];
    
    if (type == "request" || type == "response") {
        currentIteration_ = iteration;
        emit iterationChanged(iteration);
        
        if (type == "response" && content.contains("message")) {
            // Convert API message to UI message
            try {
                messages::Message apiMsg = messages::Message::from_json(content["message"]);
                auto uiMsg = convertApiMessage(apiMsg);
                if (uiMsg) {
                    addMessageToConversation(std::move(uiMsg));
                }
            } catch (const std::exception& e) {
                emit errorOccurred(QString("Failed to convert API message: %1").arg(e.what()));
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
    
    // Create tool execution message
    auto toolMsg = std::make_unique<Message>();
    toolMsg->setRole(MessageRole::Assistant);
    toolMsg->setType(MessageType::ToolExecution);
    toolMsg->setContent(QString("Executing %1...").arg(toolName));
    toolMsg->metadata().timestamp = QDateTime::currentDateTime();
    
    // Create tool execution object
    auto toolExec = std::make_unique<ToolExecution>();
    toolExec->toolId = toolId;
    toolExec->toolName = toolName;
    
    // Convert json to QJsonObject
    QString jsonStr = QString::fromStdString(input.dump());
    QJsonDocument doc = QJsonDocument::fromJson(jsonStr.toUtf8());
    toolExec->parameters = doc.object();
    
    toolExec->state = ToolExecutionState::Running;
    toolExec->startTime = QDateTime::currentDateTime();
    
    toolMsg->setToolExecution(std::move(toolExec));
    
    // Track tool ID to message ID mapping
    toolIdToMessageId_[toolId] = toolMsg->id().toString();
    
    addMessageToConversation(std::move(toolMsg));
    
    // Update tool dock if connected - use the correct API
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
    
    // Update existing tool execution message
    if (conversationModel_ && toolIdToMessageId_.contains(toolId)) {
        QString messageId = toolIdToMessageId_[toolId];
        
        // Find and update the message
        for (int i = 0; i < conversationModel_->rowCount(); ++i) {
            auto msg = conversationModel_->getMessageAt(i);
            if (msg && msg->id().toString() == messageId) {
                if (auto toolExec = msg->toolExecution()) {
                    toolExec->state = ToolExecutionState::Completed;
                    toolExec->endTime = QDateTime::currentDateTime();
                    toolExec->duration = toolExec->startTime.msecsTo(toolExec->endTime);
                    toolExec->output = QString::fromStdString(result.dump());
                    
                    // Update content with result summary
                    QString newContent = QString("Executed %1 successfully").arg(toolName);
                    
                    // Notify model of update - updateMessage takes QUuid and QString
                    conversationModel_->updateMessage(msg->id(), newContent);
                }
                break;
            }
        }
    }
    
    // Update tool dock
    if (toolDock_ && toolIdToExecId_.contains(toolId)) {
        QUuid execId = toolIdToExecId_[toolId];
        QString output = QString::fromStdString(result.dump());
        toolDock_->completeExecution(execId, true, output);
    }
    
    updateMemoryView();
    updateStatistics();
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
    QTextStream stream(&content);
    
    for (const auto& contentPtr : apiMsg.contents()) {
        if (auto text = dynamic_cast<const messages::TextContent*>(contentPtr.get())) {
            stream << QString::fromStdString(text->text);
        } else if (auto toolUse = dynamic_cast<const messages::ToolUseContent*>(contentPtr.get())) {
            processToolUseContent(uiMsg.get(), toolUse);
        } else if (auto toolResult = dynamic_cast<const messages::ToolResultContent*>(contentPtr.get())) {
            processToolResultContent(uiMsg.get(), toolResult);
        }
        // Note: Thinking content is internal and not shown in UI
    }
    
    if (!content.isEmpty()) {
        uiMsg->setContent(content);
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
    // Check if it contains tool uses
    for (const auto& content : msg.contents()) {
        if (dynamic_cast<const messages::ToolUseContent*>(content.get())) {
            return MessageType::ToolExecution;
        }
    }
    
    // Default based on role
    if (msg.role() == messages::Role::Assistant) {
        return MessageType::Analysis;
    }
    
    return MessageType::Text;
}

void AgentController::processToolUseContent(Message* uiMsg, const messages::ToolUseContent* toolUse) {
    // Tool use is handled separately in handleToolStarted
    // Here we just note it in the message
    QString currentContent = uiMsg->content();
    if (!currentContent.isEmpty()) {
        currentContent += "\n";
    }
    currentContent += QString("[Tool: %1]").arg(QString::fromStdString(toolUse->name));
    uiMsg->setContent(currentContent);
}

void AgentController::processToolResultContent(Message* uiMsg, const messages::ToolResultContent* toolResult) {
    // Add tool result to content
    QString currentContent = uiMsg->content();
    if (!currentContent.isEmpty()) {
        currentContent += "\n";
    }
    
    if (toolResult->is_error) {
        currentContent += QString("[Tool Error: %1]").arg(QString::fromStdString(toolResult->content));
        uiMsg->setType(MessageType::Error);
    } else {
        currentContent += QString("[Tool Result]");
    }
    
    uiMsg->setContent(currentContent);
}

void AgentController::addMessageToConversation(std::unique_ptr<Message> msg) {
    if (conversationModel_) {
        conversationModel_->addMessage(std::move(msg));
        
        // Auto-scroll conversation view
        if (conversationView_) {
            conversationView_->scrollToBottom();
        }
    }
}

void AgentController::updateToolExecution(const QString& toolId, const json& data) {
    // Implementation depends on tool dock API
    if (toolDock_) {
        // Update tool execution visualization
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
        
        if (snapshot.contains("analysis_entries") && snapshot["analysis_entries"].is_array()) {
            for (const auto& entry : snapshot["analysis_entries"]) {
                MemoryEntry memEntry;
                memEntry.id = QUuid::createUuid();
                memEntry.address = QString::fromStdString(entry.value("address", ""));
                memEntry.function = QString::fromStdString(entry.value("function", ""));
                memEntry.module = QString::fromStdString(entry.value("module", ""));
                memEntry.analysis = QString::fromStdString(entry.value("content", "")); // Map content to analysis
                memEntry.timestamp = QDateTime::currentDateTime();
                
                // Extract tags if present
                if (entry.contains("tags") && entry["tags"].is_array()) {
                    for (const auto& tag : entry["tags"]) {
                        memEntry.tags.append(QString::fromStdString(tag));
                    }
                }
                
                // Extract metadata
                if (entry.contains("metadata")) {
                    QString metadataStr = QString::fromStdString(entry["metadata"].dump());
                    memEntry.metadata = QJsonDocument::fromJson(metadataStr.toUtf8()).object();
                }
                
                memoryDock_->addEntry(memEntry);
            }
        }
    }
}

void AgentController::updateStatistics() {
    if (!statsDock_ || !agent_) {
        return;
    }
    
    // Get agent state
    json state = agent_->get_state_json();
    
    // Get token usage
    auto usage = agent_->get_token_usage();
    
    // Create statistics data point
    StatDataPoint dataPoint;
    dataPoint.timestamp = QDateTime::currentDateTime();
    dataPoint.category = "tokens";
    dataPoint.subcategory = "total";
    dataPoint.value = usage.input_tokens + usage.output_tokens;
    dataPoint.metadata = QJsonObject{
        {"input_tokens", usage.input_tokens},
        {"output_tokens", usage.output_tokens},
        {"cache_read_tokens", usage.cache_read_tokens},
        {"cache_creation_tokens", usage.cache_creation_tokens},
        {"estimated_cost", usage.estimated_cost()}
    };
    
    statsDock_->addDataPoint(dataPoint);
    
    // Update iteration data
    if (state.contains("conversation")) {
        StatDataPoint iterationPoint;
        iterationPoint.timestamp = QDateTime::currentDateTime();
        iterationPoint.category = "iterations";
        iterationPoint.value = currentIteration_;
        statsDock_->addDataPoint(iterationPoint);
    }
}

} // namespace llm_re::ui_v2