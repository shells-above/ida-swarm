#pragma once

#include <QUuid>
#include <QString>
#include <QJsonObject>
#include <QDateTime>
#include <QMap>
#include <QList>

namespace llm_re::ui_v2 {

// Tool execution states
enum class ToolExecutionState {
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled
};

// Unified tool execution data structure
struct ToolExecution {
    // Sub-task definition
    struct SubTask {
        QString name;
        int progress = 0;
        bool completed = false;
    };
    
    // Core identity
    QUuid id;                    // Unique execution ID
    QString toolId;              // External tool ID from agent
    QString toolName;            // Human-readable tool name
    QString description;         // Optional description
    
    // Execution data
    QJsonObject parameters;      // Input parameters
    QString output;              // Tool output
    QString errorMessage;        // Error message if failed
    int exitCode = 0;            // Exit code
    
    // Status and progress
    ToolExecutionState state = ToolExecutionState::Pending;
    int progress = 0;            // 0-100
    QString progressMessage;     // Current status message
    
    // Timing
    QDateTime startTime;
    QDateTime endTime;
    qint64 duration = 0;         // milliseconds
    
    // Relationships (for tool dock visualization)
    QUuid parentId;              // Parent execution if hierarchical
    QList<QUuid> dependencyIds;  // Dependencies that must complete first
    QList<SubTask> subTasks;     // Sub-tasks for complex operations
    
    // Metadata
    QStringList affectedFiles;   // Files modified by this tool
    QMap<QString, QVariant> metadata;  // Additional tool-specific data
    
    // Helper methods
    bool isRunning() const { return state == ToolExecutionState::Running; }
    bool isCompleted() const { return state == ToolExecutionState::Completed; }
    bool isFailed() const { return state == ToolExecutionState::Failed; }
    bool isCancelled() const { return state == ToolExecutionState::Cancelled; }
    bool isFinished() const { return isCompleted() || isFailed() || isCancelled(); }
    
    // Calculate duration if not set
    qint64 getDuration() const {
        if (duration > 0) return duration;
        if (startTime.isValid() && endTime.isValid()) {
            return startTime.msecsTo(endTime);
        }
        return 0;
    }
};

} // namespace llm_re::ui_v2