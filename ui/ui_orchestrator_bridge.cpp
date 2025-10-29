// Include order is critical! ui_common.h handles the proper ordering
#include "ui_common.h"
#include "ui_orchestrator_bridge.h"
#include "../orchestrator/orchestrator.h"
#include "../core/logger.h"

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
    LOG("UIOrchestratorBridge: Setting up worker thread\n");
    
    // Create worker thread
    worker_thread_ = new QThread();
    LOG("UIOrchestratorBridge: Created QThread\n");
    
    // Create worker
    worker_ = new OrchestratorWorker(orchestrator_);
    LOG("UIOrchestratorBridge: Created OrchestratorWorker\n");
    
    // Move worker to thread
    worker_->moveToThread(worker_thread_);
    LOG("UIOrchestratorBridge: Moved worker to thread\n");
    
    // Connect signals for task processing
    LOG("UIOrchestratorBridge: Connecting signals...\n");
    
    bool connected = connect(this, &UIOrchestratorBridge::process_task_requested,
                           worker_, &OrchestratorWorker::process_task,
                           Qt::QueuedConnection);
    LOG("UIOrchestratorBridge: process_task_requested connection: %s\n", connected ? "SUCCESS" : "FAILED");
    
    // Connect worker signals to bridge signals
    connected = connect(worker_, &OrchestratorWorker::processing_started,
                       this, &UIOrchestratorBridge::on_processing_started,
                       Qt::QueuedConnection);
    LOG("UIOrchestratorBridge: processing_started connection: %s\n", connected ? "SUCCESS" : "FAILED");
    
    connected = connect(worker_, &OrchestratorWorker::processing_completed,
                       this, &UIOrchestratorBridge::on_processing_completed,
                       Qt::QueuedConnection);
    LOG("UIOrchestratorBridge: processing_completed connection: %s\n", connected ? "SUCCESS" : "FAILED");
    
    connected = connect(worker_, &OrchestratorWorker::status_update,
                       this, &UIOrchestratorBridge::status_update,
                       Qt::QueuedConnection);
    LOG("UIOrchestratorBridge: status_update connection: %s\n", connected ? "SUCCESS" : "FAILED");
    
    connected = connect(worker_, &OrchestratorWorker::error_occurred,
                       this, &UIOrchestratorBridge::error_occurred,
                       Qt::QueuedConnection);
    LOG("UIOrchestratorBridge: error_occurred connection: %s\n", connected ? "SUCCESS" : "FAILED");
    
    // Clean up when thread finishes
    connect(worker_thread_, &QThread::finished,
            worker_, &QObject::deleteLater);
    
    // Start the thread
    worker_thread_->start();
    LOG("UIOrchestratorBridge: Worker thread started\n");
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
    LOG("UIOrchestratorBridge: submit_task called\n");
    
    if (!orchestrator_) {
        LOG("UIOrchestratorBridge: ERROR - orchestrator_ is null\n");
        emit error_occurred("Orchestrator not initialized");
        return;
    }
    
    if (is_processing_) {
        LOG("UIOrchestratorBridge: ERROR - already processing\n");
        emit error_occurred("Already processing a task");
        return;
    }
    
    LOG("UIOrchestratorBridge: Emitting process_task_requested signal\n");
    // Convert to QString and emit signal to worker thread
    QString qtask = QString::fromStdString(task);
    emit process_task_requested(qtask);
}

void UIOrchestratorBridge::clear_conversation() {
    LOG("UIOrchestratorBridge: clear_conversation called\n");
    
    if (!orchestrator_) {
        LOG("UIOrchestratorBridge: ERROR - orchestrator_ is null\n");
        return;
    }
    
    // Clear the conversation in the orchestrator
    orchestrator_->clear_conversation();
    LOG("UIOrchestratorBridge: Conversation cleared in orchestrator\n");
}

void UIOrchestratorBridge::start_auto_decompile() {
    LOG("UIOrchestratorBridge: start_auto_decompile called");

    if (!orchestrator_) {
        LOG("UIOrchestratorBridge: ERROR - orchestrator_ is null");
        emit error_occurred("Orchestrator not initialized");
        return;
    }

    // Trigger auto-decompile on orchestrator
    orchestrator_->start_auto_decompile();
}

void UIOrchestratorBridge::stop_auto_decompile() {
    LOG("UIOrchestratorBridge: stop_auto_decompile called");

    if (!orchestrator_) {
        LOG("UIOrchestratorBridge: ERROR - orchestrator_ is null");
        return;
    }

    // Stop auto-decompile on orchestrator
    orchestrator_->stop_auto_decompile();
}

void UIOrchestratorBridge::on_processing_started() {
    LOG("UIOrchestratorBridge: on_processing_started called\n");
    is_processing_ = true;
    LOG("UIOrchestratorBridge: Emitting processing_started to UI\n");
    emit processing_started();
}

void UIOrchestratorBridge::on_processing_completed() {
    is_processing_ = false;
    emit processing_completed();
}

} // namespace llm_re::ui