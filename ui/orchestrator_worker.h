#pragma once

#include "ui_common.h"
#include <QObject>
#include <QThread>
#include <string>

// Forward declaration - full definition in orchestrator.h (include in .cpp only)
namespace llm_re::orchestrator {
    class Orchestrator;
}

namespace llm_re::ui {

// Worker class that runs orchestrator operations on a background thread
class OrchestratorWorker : public QObject {
    Q_OBJECT
    
public:
    explicit OrchestratorWorker(orchestrator::Orchestrator* orch, QObject* parent = nullptr);
    ~OrchestratorWorker() = default;

public slots:
    // Process a task from the user (runs on worker thread)
    void process_task(const QString& task);
    
    // Stop processing and clean up
    void stop();

signals:
    // Emitted when processing starts
    void processing_started();
    
    // Emitted when processing completes
    void processing_completed();
    
    // Emitted for status updates
    void status_update(const QString& message);
    
    // Emitted on error
    void error_occurred(const QString& error);

private:
    orchestrator::Orchestrator* orchestrator_;
    bool should_stop_ = false;
    
    // EventBus for status updates
    EventBus& event_bus_ = get_event_bus();
};

} // namespace llm_re::ui