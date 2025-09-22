#pragma once

#include "ui_common.h"
#include "orchestrator_worker.h"
#include <QObject>
#include <QThread>
#include <memory>

namespace llm_re::ui {

// Bridge between orchestrator and UI - handles task submission
class UIOrchestratorBridge : public QObject {
    Q_OBJECT
    
public:
    static UIOrchestratorBridge& instance() {
        static UIOrchestratorBridge instance;
        return instance;
    }
    
    ~UIOrchestratorBridge();
    
    void set_orchestrator(orchestrator::Orchestrator* orch);
    
    orchestrator::Orchestrator* get_orchestrator() {
        return orchestrator_;
    }
    
    // Submit task to orchestrator (non-blocking)
    void submit_task(const std::string& task);
    
    // Clear the conversation in orchestrator
    void clear_conversation();
    
    // Check if currently processing
    bool is_processing() const { return is_processing_; }

signals:
    // Signal to worker thread to process task
    void process_task_requested(const QString& task);
    
    // Signals for UI updates
    void processing_started();
    void processing_completed();
    void status_update(const QString& message);
    void error_occurred(const QString& error);

private slots:
    void on_processing_started();
    void on_processing_completed();

private:
    UIOrchestratorBridge();
    
    orchestrator::Orchestrator* orchestrator_ = nullptr;
    QThread* worker_thread_ = nullptr;
    OrchestratorWorker* worker_ = nullptr;
    bool is_processing_ = false;
    
    void setup_worker_thread();
    void cleanup_worker_thread();
};

} // namespace llm_re::ui