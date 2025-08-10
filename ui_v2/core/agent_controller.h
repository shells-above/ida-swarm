#pragma once

#include "ui_v2_common.h"
#include "../models/conversation_model.h"
#include "agent/agent.h"
#include "api/anthropic_api.h"
#include <memory>
#include <vector>

#include "ui_v2/views/console_dock.h"

namespace llm_re {
    struct Config;
}

namespace llm_re::ui_v2 {

class ConversationView;
class MemoryDock;
class ToolExecutionDock;
class ConsoleDock;

// AgentController that directly uses the Agent class
// Agent remains UI-agnostic through its callback system
class AgentController : public QObject {
    Q_OBJECT
    
public:
    explicit AgentController(QObject* parent = nullptr);
    ~AgentController();
    
    // Initialize with configuration
    bool initialize(const Config& config);
    void shutdown();
    
    // Agent control - direct Agent methods
    void executeTask(const std::string& task);
    void stopExecution() const;
    void resumeExecution() const;
    void continueWithTask(const std::string& additional);
    void injectUserMessage(const std::string& message);
    
    // State queries
    bool isRunning() const;
    bool isPaused() const;
    bool isCompleted() const;
    bool canContinue() const;
    std::string getLastError() const;
    
    // UI component connections
    void connectConversationView(ConversationView* view);
    void connectMemoryDock(MemoryDock* dock);
    void connectToolDock(ToolExecutionDock* dock);
    void connectConsoleDock(ConsoleDock* dock);
    
    // Configuration
    const Config& config() const;
    void updateConfig(const Config& config);
    
    // Memory management
    void saveMemory(const QString& path);
    void loadMemory(const QString& path);
    
    // Statistics
    api::TokenUsage getTokenUsage() const;
    json getAgentState() const;
    
    // Manual tool execution
    QJsonObject executeManualTool(const QString& toolName, const QJsonObject& parameters);
    QJsonArray getAvailableTools() const;
    
signals:
    // Status updates
    void statusChanged(const QString& status);
    void agentStarted();
    void agentPaused();
    void agentCompleted();
    void errorOccurred(const QString& error);
    
    // Progress updates
    void iterationChanged(int iteration);
    void tokenUsageUpdated(int inputTokens, int outputTokens, double estimatedCost);
    void finalReportGenerated(const QString& report);
    
private:
    // Agent message callback handler - receives both messages and JSON
    void handleAgentMessage(AgentMessageType type, const Agent::CallbackData& data);
    
    // Helper methods
    void addMessageToConversation(std::shared_ptr<messages::Message> msg);
    void updateMemoryView();
    QString agentStatusToString(AgentState::Status status) const;
    
    // Lightweight logging helper
    void logToConsole(LogEntry::Level level, const QString& category, const QString& message, const QJsonObject& metadata = QJsonObject());
    
    // Core components
    std::unique_ptr<Agent> agent_;
    
    // Connected UI components
    ConversationModel* conversationModel_ = nullptr;
    ConversationView* conversationView_ = nullptr;
    MemoryDock* memoryDock_ = nullptr;
    ToolExecutionDock* toolDock_ = nullptr;
    ConsoleDock* consoleDock_ = nullptr;
    
    // State tracking
    bool isInitialized_ = false;
    QString currentTaskId_;
    std::map<QString, QUuid> toolIdToExecId_;   // tool_id -> execution_id mapping
    uint64_t lastMemoryVersion_ = 0;            // Track memory version for efficient change detection
    
    // Statistics
    std::chrono::steady_clock::time_point sessionStart_;
};

} // namespace llm_re::ui_v2