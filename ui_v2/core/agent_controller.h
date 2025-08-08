#pragma once

#include "ui_v2_common.h"
#include "../models/conversation_model.h"

// Forward declarations
namespace llm_re {
    struct Config;
    class REAgent;
    namespace api {
        struct ChatResponse;
        struct TokenUsage;
    }
    namespace messages {
        class Message;
        enum class Role;
        struct ToolUseContent;
        struct ToolResultContent;
    }
}

namespace llm_re::ui_v2 {

class ConversationView;
class MemoryDock;
class ToolExecutionDock;
class ConsoleDock;

// Controller that bridges between the REAgent and ui_v2
class AgentController : public QObject {
    Q_OBJECT
    
public:
    explicit AgentController(QObject* parent = nullptr);
    ~AgentController();
    
    // Initialize with configuration
    bool initialize(const Config& config);
    void shutdown();
    
    // Agent control
    void executeTask(const std::string& task);
    void stopExecution();
    void resumeExecution();
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
    const Config& config() const { return *config_; }
    void updateConfig(const Config& config);
    
    // Memory management
    void saveMemory(const QString& path);
    void loadMemory(const QString& path);
    
    // Statistics
    api::TokenUsage getTokenUsage() const;
    json getAgentState() const;
    
signals:
    // Status updates
    void statusChanged(const QString& status);
    void errorOccurred(const QString& error);
    
    // Agent state
    void agentStarted();
    void agentStopped();
    void agentPaused();
    void agentCompleted();
    
    // Token usage
    void tokenUsageUpdated(int inputTokens, int outputTokens, double cost);
    
    // Iteration updates
    void iterationChanged(int iteration);
    
    // Final report
    void finalReportGenerated(const QString& report);
    
private slots:
    void onAgentMessage(int messageType, const QString& dataStr);
    
private:
    // Message handlers
    void handleLogMessage(const json& data);
    void handleApiMessage(const json& data);
    void handleStateChanged(const json& data);
    void handleToolStarted(const json& data);
    void handleToolExecuted(const json& data);
    void handleFinalReport(const json& data);
    
    // Message conversion
    std::unique_ptr<Message> convertApiMessage(const messages::Message& apiMsg);
    MessageRole convertMessageRole(messages::Role role);
    MessageType inferMessageType(const messages::Message& msg);
    
    // UI updates
    void addMessageToConversation(std::unique_ptr<Message> msg);
    void updateMemoryView();
    
    // Core components
    std::unique_ptr<REAgent> agent_;
    std::unique_ptr<Config> config_;
    ConversationModel* conversationModel_ = nullptr;
    
    // Connected UI components
    ConversationView* conversationView_ = nullptr;
    MemoryDock* memoryDock_ = nullptr;
    ToolExecutionDock* toolDock_ = nullptr;
    ConsoleDock* consoleDock_ = nullptr;
    
    // State tracking
    bool isInitialized_ = false;
    int currentIteration_ = 0;
    std::map<QString, QString> toolIdToMessageId_;  // tool_id -> message_id mapping
    std::map<QString, QUuid> toolIdToExecId_;  // tool_id -> execution_id mapping
    
    // Statistics
    std::chrono::steady_clock::time_point sessionStart_;
};

} // namespace llm_re::ui_v2