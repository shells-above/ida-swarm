#include "agent_controller.h"
#include "../views/conversation_view.h"
#include "../views/memory_dock.h"
#include "../views/tool_execution_dock.h"
#include "../views/statistics_dock.h"
#include "core/ida_utils.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <sstream>

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
        conversationModel_->clear();
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
    
    // Create log message for conversation
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
    toolExec->parameters = QJsonObject::fromVariantMap(input.get<QVariantMap>());
    toolExec->state = ToolExecutionState::Running;
    toolExec->startTime = QDateTime::currentDateTime();
    
    toolMsg->setToolExecution(std::move(toolExec));
    
    // Track tool ID to message ID mapping
    toolIdToMessageId_[toolId] = toolMsg->id().toString();
    
    addMessageToConversation(std::move(toolMsg));
    
    // Update tool dock if connected
    if (toolDock_) {
        toolDock_->addToolExecution(toolId, toolName, 
            QJsonDocument(QJsonObject::fromVariantMap(input.get<QVariantMap>())).toJson());
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
            auto msg = conversationModel_->messageAt(i);
            if (msg && msg->id().toString() == messageId) {
                if (auto toolExec = msg->toolExecution()) {
                    toolExec->state = ToolExecutionState::Completed;
                    toolExec->endTime = QDateTime::currentDateTime();
                    toolExec->duration = toolExec->startTime.msecsTo(toolExec->endTime);
                    toolExec->output = QString::fromStdString(result.dump());
                    
                    // Update content with result summary
                    msg->setContent(QString("Executed %1 successfully").arg(toolName));
                    
                    // Notify model of update
                    conversationModel_->updateMessage(i);
                }
                break;
            }
        }
    }
    
    // Update tool dock
    if (toolDock_) {
        toolDock_->updateToolResult(toolId, 
            QJsonDocument(QJsonObject::fromVariantMap(result.get<QVariantMap>())).toJson());
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
        
        // Convert to format expected by memory dock
        QJsonObject memoryData;
        if (snapshot.contains("analysis_entries")) {
            memoryData["entries"] = QJsonArray::fromVariantList(
                snapshot["analysis_entries"].get<QVariantList>());
        }
        
        memoryDock_->updateMemoryData(memoryData);
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