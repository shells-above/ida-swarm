// Include order is critical! ui_common.h handles the proper ordering
#include "ui_common.h"
#include "orchestrator_worker.h"
#include "../orchestrator/orchestrator.h"
#include "../core/logger.h"

#include <QDebug>

namespace llm_re::ui {

OrchestratorWorker::OrchestratorWorker(orchestrator::Orchestrator* orch, QObject* parent)
    : QObject(parent), orchestrator_(orch) {
}

void OrchestratorWorker::process_task(const QString& task) {
    LOG("OrchestratorWorker: process_task called");
    
    if (!orchestrator_) {
        LOG("OrchestratorWorker: ERROR - orchestrator_ is null\n");
        emit error_occurred("Orchestrator not initialized");
        return;
    }
    
    LOG("OrchestratorWorker: Emitting processing_started signal\n");
    // Emit started signal
    emit processing_started();
    emit status_update("Processing task...");
    
    // Convert QString to std::string
    std::string task_str = task.toStdString();
    
    // Emit event that task is being processed (UI will pick this up)
    event_bus_.publish(AgentEvent(AgentEvent::ORCHESTRATOR_INPUT, "orchestrator", {
        {"input", task_str}
    }));
    
    try {
        // Process the task - this will block but we're on a worker thread
        // The orchestrator will emit events through EventBus as it works
        orchestrator_->process_user_input(task_str);
        
        // Emit completion
        emit processing_completed();
        emit status_update("Task completed");
        
    } catch (const std::exception& e) {
        emit error_occurred(QString("Error processing task: %1").arg(e.what()));
        emit processing_completed();
    } catch (...) {
        emit error_occurred("Unknown error occurred while processing task");
        emit processing_completed();
    }
}

void OrchestratorWorker::stop() {
    should_stop_ = true;
    // In the future, we might need to interrupt long-running operations
}

} // namespace llm_re::ui