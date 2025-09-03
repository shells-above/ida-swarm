// Include order is critical! ui_common.h handles the proper ordering
#include "ui_common.h"
#include "ui_orchestrator_bridge.h"

#include <QDebug>

namespace llm_re::ui {

UIOrchestratorBridge::UIOrchestratorBridge() : QObject(nullptr) {
}

UIOrchestratorBridge::~UIOrchestratorBridge() {
    cleanup_worker_thread();
}

void UIOrchestratorBridge::set_orchestrator(orchestrator::Orchestrator* orch) {
    // Clean up existing worker if any
    cleanup_worker_thread();
    
    orchestrator_ = orch;
    
    // Set up new worker thread if we have an orchestrator
    if (orchestrator_) {
        setup_worker_thread();
    }
}

void UIOrchestratorBridge::setup_worker_thread() {
    // Create worker thread
    worker_thread_ = new QThread();
    
    // Create worker
    worker_ = new OrchestratorWorker(orchestrator_);
    
    // Move worker to thread
    worker_->moveToThread(worker_thread_);
    
    // Connect signals for task processing
    connect(this, &UIOrchestratorBridge::process_task_requested,
            worker_, &OrchestratorWorker::process_task);
    
    // Connect worker signals to bridge signals
    connect(worker_, &OrchestratorWorker::processing_started,
            this, &UIOrchestratorBridge::on_processing_started);
    
    connect(worker_, &OrchestratorWorker::processing_completed,
            this, &UIOrchestratorBridge::on_processing_completed);
    
    connect(worker_, &OrchestratorWorker::status_update,
            this, &UIOrchestratorBridge::status_update);
    
    connect(worker_, &OrchestratorWorker::error_occurred,
            this, &UIOrchestratorBridge::error_occurred);
    
    // Clean up when thread finishes
    connect(worker_thread_, &QThread::finished,
            worker_, &QObject::deleteLater);
    
    // Start the thread
    worker_thread_->start();
}

void UIOrchestratorBridge::cleanup_worker_thread() {
    if (worker_thread_) {
        // Stop the worker
        if (worker_) {
            worker_->stop();
        }
        
        // Quit the thread
        worker_thread_->quit();
        worker_thread_->wait(5000); // Wait up to 5 seconds
        
        // If thread didn't finish, terminate it
        if (worker_thread_->isRunning()) {
            worker_thread_->terminate();
            worker_thread_->wait();
        }
        
        // Clean up
        delete worker_thread_;
        worker_thread_ = nullptr;
        worker_ = nullptr;
    }
}

void UIOrchestratorBridge::submit_task(const std::string& task) {
    msg("UIOrchestratorBridge: submit_task called\n");
    
    if (!orchestrator_) {
        msg("UIOrchestratorBridge: ERROR - orchestrator_ is null\n");
        emit error_occurred("Orchestrator not initialized");
        return;
    }
    
    if (is_processing_) {
        msg("UIOrchestratorBridge: ERROR - already processing\n");
        emit error_occurred("Already processing a task");
        return;
    }
    
    msg("UIOrchestratorBridge: Emitting process_task_requested signal\n");
    // Convert to QString and emit signal to worker thread
    QString qtask = QString::fromStdString(task);
    emit process_task_requested(qtask);
}

void UIOrchestratorBridge::on_processing_started() {
    msg("UIOrchestratorBridge: on_processing_started called\n");
    is_processing_ = true;
    msg("UIOrchestratorBridge: Emitting processing_started to UI\n");
    emit processing_started();
}

void UIOrchestratorBridge::on_processing_completed() {
    is_processing_ = false;
    emit processing_completed();
}

} // namespace llm_re::ui