// Include order is critical! ui_common.h handles the proper ordering
#include "ui_common.h"
#include "orchestrator_ui.h"
#include "ui_orchestrator_bridge.h"
#include "preferences_dialog.h"
#include "log_window.h"
#include "../core/config.h"

// Now we can safely include Qt implementation headers
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QListWidget>
#include <QLabel>
#include <QTabWidget>
#include <QPlainTextEdit>
#include <QTreeWidget>
#include <QProgressBar>
#include <QComboBox>
#include <QHeaderView>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QDateTime>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTimer>
#include <QGroupBox>
#include <QGridLayout>
#include <QScrollBar>
#include <QKeyEvent>
#include <QClipboard>
#include <QApplication>

namespace llm_re::ui {

// Main OrchestratorUI implementation
OrchestratorUI::OrchestratorUI(QWidget* parent) 
    : QMainWindow(parent) {
    // Register AgentEvent with Qt's meta-type system for signal/slot connections
    qRegisterMetaType<AgentEvent>("AgentEvent");
    msg("OrchestratorUI: Registered AgentEvent metatype for Qt signal/slot system\n");
    
    setup_ui();
    setup_event_subscriptions();
}

OrchestratorUI::~OrchestratorUI() {
    // Unsubscribe from EventBus
    if (!event_subscription_id_.empty()) {
        event_bus_.unsubscribe(event_subscription_id_);
    }
}

void OrchestratorUI::setup_ui() {
    setWindowTitle("IDA RE Agent - Orchestrator Control");
    resize(1400, 900);
    
    // Create central widget and main layout
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* layout = new QVBoxLayout(central);
    
    // Create main horizontal splitter
    main_splitter_ = new QSplitter(Qt::Horizontal, this);
    
    // Left side - Task panel and Agent monitor
    left_splitter_ = new QSplitter(Qt::Vertical);
    
    task_panel_ = new TaskPanel(this);
    agent_monitor_ = new AgentMonitor(this);
    
    left_splitter_->addWidget(task_panel_);
    left_splitter_->addWidget(agent_monitor_);
    left_splitter_->setStretchFactor(0, 2);  // Task panel gets more space
    left_splitter_->setStretchFactor(1, 1);
    
    // Right side - Metrics at top, tabs at bottom
    right_splitter_ = new QSplitter(Qt::Vertical);
    
    metrics_panel_ = new MetricsPanel(this);
    
    // Bottom tabs for IRC, Tool calls, and Logs
    bottom_tabs_ = new QTabWidget(this);
    irc_viewer_ = new IRCViewer(this);
    tool_tracker_ = new ToolCallTracker(this);
    log_window_ = new LogWindow(this);
    
    bottom_tabs_->addTab(irc_viewer_, "IRC Communication");
    bottom_tabs_->addTab(tool_tracker_, "Tool Calls");
    bottom_tabs_->addTab(log_window_, "Orchestrator Logs");
    
    right_splitter_->addWidget(metrics_panel_);
    right_splitter_->addWidget(bottom_tabs_);
    right_splitter_->setStretchFactor(0, 1);
    right_splitter_->setStretchFactor(1, 3);
    
    // Add to main splitter
    main_splitter_->addWidget(left_splitter_);
    main_splitter_->addWidget(right_splitter_);
    main_splitter_->setStretchFactor(0, 3);
    main_splitter_->setStretchFactor(1, 2);
    
    layout->addWidget(main_splitter_);
    
    // Connect signals
    connect(task_panel_, &TaskPanel::task_submitted, 
            this, &OrchestratorUI::on_task_submitted);
    
    // Connect to bridge signals for progress updates
    msg("OrchestratorUI: Connecting to bridge signals...\n");
    
    bool connected = connect(&UIOrchestratorBridge::instance(), &UIOrchestratorBridge::processing_started,
                           this, &OrchestratorUI::on_processing_started,
                           Qt::AutoConnection);
    msg("OrchestratorUI: processing_started connection: %s\n", connected ? "SUCCESS" : "FAILED");
    
    connected = connect(&UIOrchestratorBridge::instance(), &UIOrchestratorBridge::processing_completed,
                       this, &OrchestratorUI::on_processing_completed,
                       Qt::AutoConnection);
    msg("OrchestratorUI: processing_completed connection: %s\n", connected ? "SUCCESS" : "FAILED");
    
    connected = connect(&UIOrchestratorBridge::instance(), &UIOrchestratorBridge::status_update,
                       this, &OrchestratorUI::on_status_update,
                       Qt::AutoConnection);
    msg("OrchestratorUI: status_update connection: %s\n", connected ? "SUCCESS" : "FAILED");
    
    connected = connect(&UIOrchestratorBridge::instance(), &UIOrchestratorBridge::error_occurred,
                       this, &OrchestratorUI::on_error_occurred,
                       Qt::AutoConnection);
    msg("OrchestratorUI: error_occurred connection: %s\n", connected ? "SUCCESS" : "FAILED");
    
    // Status bar
    statusBar()->showMessage("Ready");
}

void OrchestratorUI::setup_event_subscriptions() {
    // Connect internal signal for thread-safe updates
    connect(this, &OrchestratorUI::event_received,
            this, &OrchestratorUI::handle_event,
            Qt::QueuedConnection);
    
    // Subscribe to ALL EventBus events
    event_subscription_id_ = event_bus_.subscribe(
        [this](const AgentEvent& event) {
            msg("OrchestratorUI: EventBus subscription received event type %d from source '%s'\n", 
                static_cast<int>(event.type), event.source.c_str());
            // Emit signal to handle in UI thread
            emit event_received(event);
        }
    );
}


void OrchestratorUI::handle_event(const AgentEvent& event) {
    msg("OrchestratorUI::handle_event called with event type %d from source '%s'\n",
        static_cast<int>(event.type), event.source.c_str());
    
    // Route events to appropriate widgets based on type
    switch (event.type) {
        case AgentEvent::ORCHESTRATOR_INPUT:
            msg("OrchestratorUI: Handling ORCHESTRATOR_INPUT event\n");
            if (event.payload.contains("input")) {
                task_panel_->add_user_input(event.payload["input"]);
            }
            break;
            
        case AgentEvent::ORCHESTRATOR_THINKING:
            msg("OrchestratorUI: Handling ORCHESTRATOR_THINKING event\n");
            task_panel_->set_thinking_state(true);
            statusBar()->showMessage("Orchestrator thinking...");
            break;
            
        case AgentEvent::ORCHESTRATOR_RESPONSE:
            msg("OrchestratorUI: Handling ORCHESTRATOR_RESPONSE event\n");
            task_panel_->set_thinking_state(false);
            if (event.payload.contains("response")) {
                task_panel_->add_orchestrator_message(event.payload["response"]);
            }
            statusBar()->showMessage("Ready");
            break;
            
        case AgentEvent::AGENT_SPAWNING:
            msg("OrchestratorUI: Handling AGENT_SPAWNING event for agent %s\n",
                event.payload.contains("agent_id") ? event.payload["agent_id"].get<std::string>().c_str() : "unknown");
            if (event.payload.contains("agent_id") && event.payload.contains("task")) {
                agent_monitor_->on_agent_spawning(
                    event.payload["agent_id"],
                    event.payload["task"]
                );
            }
            break;
            
        case AgentEvent::AGENT_SPAWN_COMPLETE:
            if (event.payload.contains("agent_id")) {
                agent_monitor_->on_agent_spawned(event.payload["agent_id"]);
            }
            break;
            
        case AgentEvent::AGENT_SPAWN_FAILED:
            if (event.payload.contains("agent_id")) {
                std::string error = event.payload.contains("error") ? 
                    event.payload["error"] : "Unknown error";
                agent_monitor_->on_agent_failed(event.payload["agent_id"], error);
            }
            break;
            
        case AgentEvent::STATE:
            if (event.payload.contains("status")) {
                agent_monitor_->on_agent_state_change(
                    event.source,
                    event.payload["status"]
                );
            }
            break;
            
        case AgentEvent::TOOL_CALL:
            tool_tracker_->add_tool_call(event.source, event.payload);
            break;
            
        case AgentEvent::METRIC:
            if (event.payload.contains("input_tokens") && event.payload.contains("output_tokens")) {
                // Get cache tokens if provided
                size_t cache_read = 0;
                size_t cache_write = 0;
                if (event.payload.contains("cache_read_input_tokens")) {
                    cache_read = event.payload["cache_read_input_tokens"];
                }
                if (event.payload.contains("cache_creation_input_tokens")) {
                    cache_write = event.payload["cache_creation_input_tokens"];
                }
                
                metrics_panel_->update_token_usage(
                    event.payload["input_tokens"],
                    event.payload["output_tokens"],
                    cache_read,
                    cache_write
                );
            }
            if (event.payload.contains("context_percentage")) {
                metrics_panel_->update_context_usage(
                    event.payload["context_percentage"]
                );
            }
            break;
            
        case AgentEvent::TASK_COMPLETE:
            agent_monitor_->on_agent_completed(event.source);
            break;
            
        case AgentEvent::MESSAGE:
            // Could be IRC message or other
            if (event.payload.contains("channel") && event.payload.contains("message")) {
                irc_viewer_->add_message(
                    event.payload["channel"],
                    event.source,
                    event.payload["message"]
                );
            }
            break;
            
        case AgentEvent::ERROR:
            if (event.payload.contains("error")) {
                statusBar()->showMessage(
                    QString("Error from %1: %2")
                        .arg(QString::fromStdString(event.source))
                        .arg(QString::fromStdString(event.payload["error"])),
                    5000
                );
            }
            break;
            
        case AgentEvent::LOG:
            if (event.payload.contains("level") && event.payload.contains("message")) {
                auto level = static_cast<claude::LogLevel>(event.payload["level"].get<int>());
                std::string message = event.payload["message"];
                log_window_->add_log(level, event.source, message);
            }
            break;
    }
}

void OrchestratorUI::on_task_submitted() {
    msg("OrchestratorUI: on_task_submitted called\n");
    
    std::string task = task_panel_->get_task_input();
    if (task.empty()) {
        msg("OrchestratorUI: Task is empty, returning\n");
        return;
    }
    
    msg("OrchestratorUI: Task: %s\n", task.c_str());
    
    // Clear input
    task_panel_->clear_input();
    
    // Submit task to orchestrator via bridge
    msg("OrchestratorUI: Submitting task to bridge\n");
    UIOrchestratorBridge::instance().submit_task(task);
}

void OrchestratorUI::on_clear_console() {
    task_panel_->clear_history();
    agent_monitor_->clear_agents();
    irc_viewer_->clear_messages();
    tool_tracker_->clear_calls();
}

void OrchestratorUI::on_pause_resume_clicked() {
    is_paused_ = !is_paused_;
    statusBar()->showMessage(is_paused_ ? "Paused" : "Resumed");
}

void OrchestratorUI::on_preferences_clicked() {
    PreferencesDialog dialog(this);
    
    // Connect to configuration changed signal to update UI if needed
    connect(&dialog, &PreferencesDialog::configurationChanged,
            [this]() {
                statusBar()->showMessage("Configuration updated", 3000);
                // The UI components will use Config::instance() directly
                // so no need to manually update anything here
            });
    
    dialog.exec();
}

void OrchestratorUI::on_processing_started() {
    msg("OrchestratorUI: on_processing_started called!\n");
    
    // Disable input while processing
    task_panel_->submit_button_->setEnabled(false);
    task_panel_->task_input_->setEnabled(false);
    
    // Update status
    task_panel_->set_thinking_state(true);
    statusBar()->showMessage("Processing task...");
    
    msg("OrchestratorUI: UI updated to show processing state\n");
}

void OrchestratorUI::on_processing_completed() {
    // Re-enable input
    task_panel_->submit_button_->setEnabled(true);
    task_panel_->task_input_->setEnabled(true);
    
    // Update status
    task_panel_->set_thinking_state(false);
    statusBar()->showMessage("Ready");
}

void OrchestratorUI::on_status_update(const QString& message) {
    statusBar()->showMessage(message);
}

void OrchestratorUI::on_error_occurred(const QString& error) {
    // Show error in status bar
    statusBar()->showMessage(QString("Error: %1").arg(error), 5000);
    
    // Also add to conversation display
    task_panel_->format_message("System", error.toStdString(), "#FF0000");
    
    // Re-enable UI if needed
    task_panel_->submit_button_->setEnabled(true);
    task_panel_->task_input_->setEnabled(true);
    task_panel_->set_thinking_state(false);
}

void OrchestratorUI::show_ui() {
    show();
    raise();
    activateWindow();
}

// TaskPanel implementation
TaskPanel::TaskPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    
    // Conversation display
    conversation_display_ = new QTextEdit(this);
    conversation_display_->setReadOnly(true);
    conversation_display_->setFont(QFont("Consolas", 10));
    
    // Status label
    status_label_ = new QLabel("Ready", this);
    status_label_->setStyleSheet("QLabel { color: green; font-weight: bold; }");
    
    // Input area
    auto* input_layout = new QHBoxLayout();
    
    task_input_ = new QLineEdit(this);
    task_input_->setPlaceholderText("Enter task for orchestrator...");
    task_input_->setFont(QFont("Consolas", 10));
    
    submit_button_ = new QPushButton("Submit Task", this);
    clear_button_ = new QPushButton("Clear", this);
    
    input_layout->addWidget(task_input_);
    input_layout->addWidget(submit_button_);
    input_layout->addWidget(clear_button_);
    
    layout->addWidget(new QLabel("Orchestrator Conversation:", this));
    layout->addWidget(conversation_display_);
    layout->addWidget(status_label_);
    layout->addLayout(input_layout);
    
    // Connect signals
    connect(submit_button_, &QPushButton::clicked, [this]() {
        emit task_submitted();
    });
    
    connect(task_input_, &QLineEdit::returnPressed, [this]() {
        emit task_submitted();
    });
    
    connect(clear_button_, &QPushButton::clicked, [this]() {
        clear_history();
    });
}

void TaskPanel::add_orchestrator_message(const std::string& message, bool is_thinking) {
    QString prefix = is_thinking ? "[THINKING] " : "";
    format_message("Orchestrator", prefix.toStdString() + message, "#0000FF");
}

void TaskPanel::add_user_input(const std::string& input) {
    format_message("User", input, "#008000");
}

void TaskPanel::format_message(const std::string& speaker, const std::string& message, const QString& color) {
    QTextCursor cursor = conversation_display_->textCursor();
    cursor.movePosition(QTextCursor::End);
    
    // Add timestamp
    QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");
    cursor.insertHtml(QString("<span style='color: gray'>[%1]</span> ").arg(timestamp));
    
    // Add speaker
    cursor.insertHtml(QString("<span style='color: %1; font-weight: bold'>%2:</span> ")
        .arg(color)
        .arg(QString::fromStdString(speaker)));
    
    // Add message
    cursor.insertText(QString::fromStdString(message) + "\n\n");
    
    // Scroll to bottom
    conversation_display_->verticalScrollBar()->setValue(
        conversation_display_->verticalScrollBar()->maximum()
    );
}

void TaskPanel::clear_history() {
    conversation_display_->clear();
}

std::string TaskPanel::get_task_input() const {
    return task_input_->text().toStdString();
}

void TaskPanel::clear_input() {
    task_input_->clear();
}

void TaskPanel::set_thinking_state(bool thinking) {
    if (thinking) {
        status_label_->setText("Orchestrator thinking...");
        status_label_->setStyleSheet("QLabel { color: orange; font-weight: bold; }");
    } else {
        status_label_->setText("Ready");
        status_label_->setStyleSheet("QLabel { color: green; font-weight: bold; }");
    }
}

// AgentMonitor implementation
AgentMonitor::AgentMonitor(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    
    // Header
    auto* header_layout = new QHBoxLayout();
    header_layout->addWidget(new QLabel("Active Agents:", this));
    
    agent_count_label_ = new QLabel("0 agents", this);
    agent_count_label_->setStyleSheet("QLabel { font-weight: bold; }");
    header_layout->addWidget(agent_count_label_);
    header_layout->addStretch();
    
    // Agent table
    agent_table_ = new QTableWidget(0, 5, this);
    agent_table_->setHorizontalHeaderLabels(
        QStringList() << "Agent ID" << "Task" << "Status" << "Spawned" << "Duration"
    );
    agent_table_->horizontalHeader()->setStretchLastSection(true);
    agent_table_->setAlternatingRowColors(true);
    agent_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    agent_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
    // Remove white borders from status cells
    agent_table_->setStyleSheet(
        "QTableWidget::item { border: none; }"
        "QTableWidget::item:selected { border: none; }"
        "QTableWidget { gridline-color: rgba(0,0,0,30); }"
    );
    
    // Install event filter to handle copy operations with full task text
    agent_table_->installEventFilter(this);
    
    // Setup duration update timer
    duration_timer_ = new QTimer(this);
    connect(duration_timer_, &QTimer::timeout, this, &AgentMonitor::update_durations);
    duration_timer_->start(1000); // Update every second
    
    layout->addLayout(header_layout);
    layout->addWidget(agent_table_);
}

void AgentMonitor::on_agent_spawning(const std::string& agent_id, const std::string& task) {
    int row = agent_table_->rowCount();
    agent_table_->insertRow(row);
    
    // Store spawn time for duration calculation
    QDateTime spawn_time = QDateTime::currentDateTime();
    agent_spawn_times_[agent_id] = spawn_time;
    
    agent_table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(agent_id)));
    
    // Store full task but display truncated version
    std::string display_task = task.length() > 50 ? task.substr(0, 47) + "..." : task;
    auto* task_item = new QTableWidgetItem(QString::fromStdString(display_task));
    task_item->setToolTip(QString::fromStdString(task));  // Show full task on hover
    task_item->setData(Qt::UserRole, QString::fromStdString(task));  // Store full task for copying
    agent_table_->setItem(row, 1, task_item);
    
    agent_table_->setItem(row, 2, new QTableWidgetItem("Spawning"));
    agent_table_->setItem(row, 3, new QTableWidgetItem(spawn_time.toString("hh:mm:ss")));
    agent_table_->setItem(row, 4, new QTableWidgetItem("0s"));
    
    // Color code status - soft yellow for spawning
    agent_table_->item(row, 2)->setBackground(QColor(255, 248, 220));
    
    update_agent_count();
}

void AgentMonitor::on_agent_spawned(const std::string& agent_id) {
    int row = find_agent_row(agent_id);
    if (row >= 0) {
        agent_table_->item(row, 2)->setText("Active");
        agent_table_->item(row, 2)->setBackground(QColor(245, 255, 245));
        agent_table_->item(row, 2)->setForeground(QColor(60, 120, 60));
    }
}

void AgentMonitor::on_agent_failed(const std::string& agent_id, const std::string& error) {
    int row = find_agent_row(agent_id);
    if (row >= 0) {
        agent_table_->item(row, 2)->setText("Failed");
        agent_table_->item(row, 2)->setBackground(QColor(255, 200, 200));
        agent_table_->item(row, 2)->setToolTip(QString::fromStdString(error));
    }
}

void AgentMonitor::on_agent_state_change(const std::string& agent_id, int state) {
    int row = find_agent_row(agent_id);
    if (row >= 0) {
        QString status = state_to_string(state);
        agent_table_->item(row, 2)->setText(status);
        
        // Update color based on state
        if (state == 0) { // Idle
            agent_table_->item(row, 2)->setBackground(QColor(240, 240, 240));
        } else if (state == 1) { // Running
            agent_table_->item(row, 2)->setBackground(QColor(245, 255, 245));
            agent_table_->item(row, 2)->setForeground(QColor(60, 120, 60));
        } else if (state == 2) { // Paused
            agent_table_->item(row, 2)->setBackground(QColor(255, 248, 220));
        } else if (state == 3) { // Completed
            agent_table_->item(row, 2)->setBackground(QColor(245, 245, 255));
            agent_table_->item(row, 2)->setForeground(QColor(60, 60, 180));
            
            // Also record completion time when state changes to completed
            agent_completion_times_[agent_id] = QDateTime::currentDateTime();
            
            // Update duration one final time
            if (agent_spawn_times_.find(agent_id) != agent_spawn_times_.end()) {
                QDateTime spawn_time = agent_spawn_times_[agent_id];
                QDateTime completion_time = agent_completion_times_[agent_id];
                qint64 seconds_elapsed = spawn_time.secsTo(completion_time);
                
                QString duration_text;
                if (seconds_elapsed < 60) {
                    duration_text = QString("%1s").arg(seconds_elapsed);
                } else if (seconds_elapsed < 3600) {
                    int minutes = seconds_elapsed / 60;
                    int seconds = seconds_elapsed % 60;
                    duration_text = QString("%1m %2s").arg(minutes).arg(seconds);
                } else {
                    int hours = seconds_elapsed / 3600;
                    int minutes = (seconds_elapsed % 3600) / 60;
                    duration_text = QString("%1h %2m").arg(hours).arg(minutes);
                }
                
                if (agent_table_->item(row, 4)) {
                    agent_table_->item(row, 4)->setText(duration_text);
                }
            }
        }
    }
}

void AgentMonitor::on_agent_completed(const std::string& agent_id) {
    int row = find_agent_row(agent_id);
    if (row >= 0) {
        agent_table_->item(row, 2)->setText("Completed");
        agent_table_->item(row, 2)->setBackground(QColor(230, 230, 255));
        
        // Record completion time to stop duration updates
        agent_completion_times_[agent_id] = QDateTime::currentDateTime();
        
        // Update duration one final time with the exact completion duration
        if (agent_spawn_times_.find(agent_id) != agent_spawn_times_.end()) {
            QDateTime spawn_time = agent_spawn_times_[agent_id];
            QDateTime completion_time = agent_completion_times_[agent_id];
            qint64 seconds_elapsed = spawn_time.secsTo(completion_time);
            
            QString duration_text;
            if (seconds_elapsed < 60) {
                duration_text = QString("%1s").arg(seconds_elapsed);
            } else if (seconds_elapsed < 3600) {
                int minutes = seconds_elapsed / 60;
                int seconds = seconds_elapsed % 60;
                duration_text = QString("%1m %2s").arg(minutes).arg(seconds);
            } else {
                int hours = seconds_elapsed / 3600;
                int minutes = (seconds_elapsed % 3600) / 60;
                duration_text = QString("%1h %2m").arg(hours).arg(minutes);
            }
            
            if (agent_table_->item(row, 4)) {
                agent_table_->item(row, 4)->setText(duration_text);
            }
        }
    }
}

void AgentMonitor::clear_agents() {
    agent_table_->setRowCount(0);
    agent_spawn_times_.clear();
    agent_completion_times_.clear();
    update_agent_count();
}

int AgentMonitor::find_agent_row(const std::string& agent_id) {
    for (int i = 0; i < agent_table_->rowCount(); ++i) {
        if (agent_table_->item(i, 0)->text().toStdString() == agent_id) {
            return i;
        }
    }
    return -1;
}

void AgentMonitor::update_agent_count() {
    int count = agent_table_->rowCount();
    agent_count_label_->setText(QString("%1 agent%2").arg(count).arg(count == 1 ? "" : "s"));
}

void AgentMonitor::update_durations() {
    QDateTime current_time = QDateTime::currentDateTime();
    
    for (int row = 0; row < agent_table_->rowCount(); ++row) {
        if (agent_table_->item(row, 0)) {
            std::string agent_id = agent_table_->item(row, 0)->text().toStdString();
            
            // Skip updating duration if agent has completed
            if (agent_completion_times_.find(agent_id) != agent_completion_times_.end()) {
                continue;
            }
            
            // Find spawn time for this agent
            auto spawn_iter = agent_spawn_times_.find(agent_id);
            if (spawn_iter != agent_spawn_times_.end()) {
                // Calculate duration
                qint64 seconds_elapsed = spawn_iter->second.secsTo(current_time);
                
                QString duration_text;
                if (seconds_elapsed < 60) {
                    duration_text = QString("%1s").arg(seconds_elapsed);
                } else if (seconds_elapsed < 3600) {
                    int minutes = seconds_elapsed / 60;
                    int seconds = seconds_elapsed % 60;
                    duration_text = QString("%1m %2s").arg(minutes).arg(seconds);
                } else {
                    int hours = seconds_elapsed / 3600;
                    int minutes = (seconds_elapsed % 3600) / 60;
                    duration_text = QString("%1h %2m").arg(hours).arg(minutes);
                }
                
                // Update duration column
                if (agent_table_->item(row, 4)) {
                    agent_table_->item(row, 4)->setText(duration_text);
                }
            }
        }
    }
}

QString AgentMonitor::state_to_string(int state) {
    switch (state) {
        case 0: return "Idle";
        case 1: return "Running";
        case 2: return "Paused";
        case 3: return "Completed";
        default: return "Unknown";
    }
}

bool AgentMonitor::eventFilter(QObject* obj, QEvent* event) {
    if (obj == agent_table_ && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        
        // Handle Ctrl+C / Cmd+C for copy
        if (keyEvent->matches(QKeySequence::Copy)) {
            QList<QTableWidgetItem*> selected = agent_table_->selectedItems();
            if (!selected.isEmpty()) {
                QString copyText;
                int lastRow = -1;
                
                for (QTableWidgetItem* item : selected) {
                    // Add newline for new rows
                    if (lastRow != -1 && item->row() != lastRow) {
                        copyText += "\n";
                    }
                    // Add tab between columns in same row
                    else if (lastRow == item->row()) {
                        copyText += "\t";
                    }
                    
                    // For Task column (column 1), use the full task data
                    if (item->column() == 1) {
                        QVariant fullTask = item->data(Qt::UserRole);
                        if (fullTask.isValid()) {
                            copyText += fullTask.toString();
                        } else {
                            copyText += item->text();
                        }
                    } else {
                        copyText += item->text();
                    }
                    
                    lastRow = item->row();
                }
                
                QApplication::clipboard()->setText(copyText);
                return true; // Event handled
            }
        }
    }
    
    return QWidget::eventFilter(obj, event);
}

// IRCViewer implementation
IRCViewer::IRCViewer(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    
    // Controls
    auto* control_layout = new QHBoxLayout();
    
    control_layout->addWidget(new QLabel("Channel:", this));
    
    channel_combo_ = new QComboBox(this);
    channel_combo_->addItem("All Channels");
    channel_combo_->addItem("#agents");
    channel_combo_->addItem("#results");
    channel_combo_->addItem("#conflicts");
    channel_combo_->setCurrentIndex(0);
    control_layout->addWidget(channel_combo_);
    
    control_layout->addWidget(new QLabel("Filter:", this));
    
    filter_input_ = new QLineEdit(this);
    filter_input_->setPlaceholderText("Filter messages...");
    control_layout->addWidget(filter_input_);
    
    clear_button_ = new QPushButton("Clear", this);
    control_layout->addWidget(clear_button_);
    
    control_layout->addStretch();
    
    // Message tree
    message_tree_ = new QTreeWidget(this);
    message_tree_->setHeaderLabels(QStringList() << "Time" << "Channel" << "Sender" << "Message");
    message_tree_->setAlternatingRowColors(true);
    message_tree_->setRootIsDecorated(false);
    
    // Adjust column widths
    message_tree_->setColumnWidth(0, 80);
    message_tree_->setColumnWidth(1, 100);
    message_tree_->setColumnWidth(2, 100);
    
    layout->addLayout(control_layout);
    layout->addWidget(message_tree_);
    
    // Connect signals
    connect(channel_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &IRCViewer::on_channel_selected);
    
    connect(filter_input_, &QLineEdit::textChanged,
            this, &IRCViewer::on_filter_changed);
    
    connect(clear_button_, &QPushButton::clicked,
            this, &IRCViewer::clear_messages);
}

void IRCViewer::add_message(const std::string& channel, const std::string& sender, const std::string& message) {
    auto* item = new QTreeWidgetItem(message_tree_);
    
    item->setText(0, QDateTime::currentDateTime().toString("hh:mm:ss"));
    item->setText(1, QString::fromStdString(channel));
    item->setText(2, QString::fromStdString(sender));
    item->setText(3, QString::fromStdString(message));
    
    // Color code by channel - only highlight conflicts
    if (channel == "#conflicts") {
        item->setBackground(1, QColor(255, 220, 220));
    }
    
    apply_filters();
}

void IRCViewer::add_join(const std::string& channel, const std::string& nick) {
    add_message(channel, "***", nick + " has joined");
}

void IRCViewer::add_part(const std::string& channel, const std::string& nick) {
    add_message(channel, "***", nick + " has left");
}

void IRCViewer::clear_messages() {
    message_tree_->clear();
}

void IRCViewer::set_channel_filter(const std::string& channel) {
    current_channel_filter_ = channel;
    apply_filters();
}

void IRCViewer::on_channel_selected() {
    QString channel = channel_combo_->currentText();
    if (channel == "All Channels") {
        current_channel_filter_.clear();
    } else {
        current_channel_filter_ = channel.toStdString();
    }
    apply_filters();
}

void IRCViewer::on_filter_changed(const QString& text) {
    apply_filters();
}

void IRCViewer::apply_filters() {
    QString filter_text = filter_input_->text().toLower();
    
    for (int i = 0; i < message_tree_->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = message_tree_->topLevelItem(i);
        
        bool visible = true;
        
        // Channel filter
        if (!current_channel_filter_.empty()) {
            if (item->text(1).toStdString() != current_channel_filter_) {
                visible = false;
            }
        }
        
        // Text filter
        if (visible && !filter_text.isEmpty()) {
            bool match = item->text(2).toLower().contains(filter_text) ||
                         item->text(3).toLower().contains(filter_text);
            if (!match) {
                visible = false;
            }
        }
        
        item->setHidden(!visible);
    }
}

// ToolCallTracker implementation
ToolCallTracker::ToolCallTracker(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    
    // Controls and stats
    auto* control_layout = new QHBoxLayout();
    
    control_layout->addWidget(new QLabel("Agent:", this));
    
    agent_filter_ = new QComboBox(this);
    agent_filter_->addItem("All Agents");
    control_layout->addWidget(agent_filter_);
    
    control_layout->addWidget(new QLabel("Tool:", this));
    
    tool_filter_ = new QLineEdit(this);
    tool_filter_->setPlaceholderText("Filter tools...");
    control_layout->addWidget(tool_filter_);
    
    call_count_label_ = new QLabel("Total: 0 calls", this);
    control_layout->addWidget(call_count_label_);
    
    conflict_count_label_ = new QLabel("Conflicts: 0", this);
    conflict_count_label_->setStyleSheet("QLabel { color: red; font-weight: bold; }");
    control_layout->addWidget(conflict_count_label_);
    
    control_layout->addStretch();
    
    // Tool call table
    tool_table_ = new QTableWidget(0, 5, this);
    tool_table_->setHorizontalHeaderLabels(
        QStringList() << "Time" << "Agent" << "Tool" << "Parameters" << "Result"
    );
    tool_table_->horizontalHeader()->setStretchLastSection(true);
    tool_table_->setAlternatingRowColors(true);
    
    // Install event filter to handle copy operations with full text
    tool_table_->installEventFilter(this);
    
    layout->addLayout(control_layout);
    layout->addWidget(tool_table_);
    
    // Connect signals
    connect(agent_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolCallTracker::on_agent_filter_changed);
    
    connect(tool_filter_, &QLineEdit::textChanged,
            this, &ToolCallTracker::on_tool_filter_changed);
}

void ToolCallTracker::add_tool_call(const std::string& agent_id, const json& tool_data) {
    int row = tool_table_->rowCount();
    tool_table_->insertRow(row);
    
    tool_table_->setItem(row, 0, new QTableWidgetItem(QDateTime::currentDateTime().toString("hh:mm:ss")));
    tool_table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(agent_id)));
    
    // Extract tool name and parameters
    std::string tool_name = tool_data.contains("tool_name") ? tool_data["tool_name"] : "unknown";
    tool_table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(tool_name)));
    
    // Format parameters (from ToolCallTracker: "parameters")
    if (tool_data.contains("parameters")) {
        std::string params_full = tool_data["parameters"].dump();
        std::string params_display = params_full.length() > 100 ? params_full.substr(0, 97) + "..." : params_full;
        auto* params_item = new QTableWidgetItem(QString::fromStdString(params_display));
        params_item->setToolTip(QString::fromStdString(params_full));  // Show full params on hover
        params_item->setData(Qt::UserRole, QString::fromStdString(params_full));  // Store full params for copying
        tool_table_->setItem(row, 3, params_item);
    } else {
        tool_table_->setItem(row, 3, new QTableWidgetItem("-"));
    }
    
    // Result status - ToolCallTracker events don't have results, just whether it's a write operation
    if (tool_data.contains("is_write")) {
        bool is_write = tool_data["is_write"];
        tool_table_->setItem(row, 4, new QTableWidgetItem(is_write ? "Write" : "Read"));
        tool_table_->item(row, 4)->setBackground(is_write ? QColor(255, 248, 220) : QColor(240, 240, 240));
    } else if (tool_data.contains("result")) {
        // Legacy format from agents
        if (tool_data["result"].contains("success") && tool_data["result"]["success"] == false) {
            tool_table_->setItem(row, 4, new QTableWidgetItem("Failed"));
            tool_table_->item(row, 4)->setBackground(QColor(255, 200, 200));
        } else {
            tool_table_->setItem(row, 4, new QTableWidgetItem("Success"));
            tool_table_->item(row, 4)->setBackground(QColor(200, 255, 200));
        }
    } else {
        tool_table_->setItem(row, 4, new QTableWidgetItem("-"));
    }
    
    // Update agent filter if needed
    bool found = false;
    for (int i = 0; i < agent_filter_->count(); ++i) {
        if (agent_filter_->itemText(i).toStdString() == agent_id) {
            found = true;
            break;
        }
    }
    if (!found) {
        agent_filter_->addItem(QString::fromStdString(agent_id));
    }
    
    total_calls_++;
    update_stats();
    apply_filters();
}

void ToolCallTracker::add_conflict(const std::string& description) {
    conflict_count_++;
    update_stats();
}

void ToolCallTracker::clear_calls() {
    tool_table_->setRowCount(0);
    total_calls_ = 0;
    conflict_count_ = 0;
    update_stats();
}

void ToolCallTracker::set_agent_filter(const std::string& agent_id) {
    current_agent_filter_ = agent_id;
    apply_filters();
}

void ToolCallTracker::on_agent_filter_changed() {
    QString agent = agent_filter_->currentText();
    if (agent == "All Agents") {
        current_agent_filter_.clear();
    } else {
        current_agent_filter_ = agent.toStdString();
    }
    apply_filters();
}

void ToolCallTracker::on_tool_filter_changed(const QString& text) {
    apply_filters();
}

void ToolCallTracker::apply_filters() {
    QString filter_text = tool_filter_->text().toLower();
    
    for (int i = 0; i < tool_table_->rowCount(); ++i) {
        bool visible = true;
        
        // Agent filter
        if (!current_agent_filter_.empty()) {
            if (tool_table_->item(i, 1)->text().toStdString() != current_agent_filter_) {
                visible = false;
            }
        }
        
        // Tool filter
        if (visible && !filter_text.isEmpty()) {
            if (!tool_table_->item(i, 2)->text().toLower().contains(filter_text)) {
                visible = false;
            }
        }
        
        tool_table_->setRowHidden(i, !visible);
    }
}

void ToolCallTracker::update_stats() {
    call_count_label_->setText(QString("Total: %1 calls").arg(total_calls_));
    conflict_count_label_->setText(QString("Conflicts: %1").arg(conflict_count_));
}

bool ToolCallTracker::eventFilter(QObject* obj, QEvent* event) {
    if (obj == tool_table_ && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        
        // Handle Ctrl+C / Cmd+C for copy
        if (keyEvent->matches(QKeySequence::Copy)) {
            QList<QTableWidgetItem*> selected = tool_table_->selectedItems();
            if (!selected.isEmpty()) {
                QString copyText;
                int lastRow = -1;
                
                for (QTableWidgetItem* item : selected) {
                    // Add newline for new rows
                    if (lastRow != -1 && item->row() != lastRow) {
                        copyText += "\n";
                    }
                    // Add tab between columns in same row
                    else if (lastRow == item->row()) {
                        copyText += "\t";
                    }
                    
                    // For Parameters column (column 3), use the full data
                    if (item->column() == 3) {
                        QVariant fullData = item->data(Qt::UserRole);
                        if (fullData.isValid()) {
                            copyText += fullData.toString();
                        } else {
                            copyText += item->text();
                        }
                    } else {
                        copyText += item->text();
                    }
                    
                    lastRow = item->row();
                }
                
                QApplication::clipboard()->setText(copyText);
                return true; // Event handled
            }
        }
    }
    
    return QWidget::eventFilter(obj, event);
}

// MetricsPanel implementation
MetricsPanel::MetricsPanel(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    
    // Token usage section
    auto* token_group = new QGroupBox("Token Usage", this);
    auto* token_layout = new QGridLayout(token_group);
    
    // Row 0: Input tokens
    token_layout->addWidget(new QLabel("Input:", this), 0, 0);
    input_tokens_label_ = new QLabel("0", this);
    token_layout->addWidget(input_tokens_label_, 0, 1);
    
    // Row 1: Output tokens
    token_layout->addWidget(new QLabel("Output:", this), 1, 0);
    output_tokens_label_ = new QLabel("0", this);
    token_layout->addWidget(output_tokens_label_, 1, 1);
    
    // Row 2: Cache read
    token_layout->addWidget(new QLabel("Cache Read:", this), 2, 0);
    cache_read_label_ = new QLabel("0", this);
    cache_read_label_->setStyleSheet("QLabel { color: #0080ff; }");
    token_layout->addWidget(cache_read_label_, 2, 1);
    
    // Row 3: Cache write  
    token_layout->addWidget(new QLabel("Cache Write:", this), 3, 0);
    cache_write_label_ = new QLabel("0", this);
    cache_write_label_->setStyleSheet("QLabel { color: #ff8000; }");
    token_layout->addWidget(cache_write_label_, 3, 1);
    
    // Row 4: Total with separator
    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    token_layout->addWidget(separator, 4, 0, 1, 2);
    
    token_layout->addWidget(new QLabel("Total:", this), 5, 0);
    total_tokens_label_ = new QLabel("0", this);
    total_tokens_label_->setStyleSheet("QLabel { font-weight: bold; font-size: 14px; }");
    token_layout->addWidget(total_tokens_label_, 5, 1);
    
    // Row 6: Estimated cost
    token_layout->addWidget(new QLabel("Est. Cost:", this), 6, 0);
    cost_label_ = new QLabel("$0.00", this);
    cost_label_->setStyleSheet("QLabel { color: #008000; font-weight: bold; }");
    token_layout->addWidget(cost_label_, 6, 1);
    
    // Context usage section
    auto* context_group = new QGroupBox("Context Usage", this);
    auto* context_layout = new QVBoxLayout(context_group);
    
    context_bar_ = new QProgressBar(this);
    context_bar_->setMinimum(0);
    context_bar_->setMaximum(100);
    context_bar_->setValue(0);
    context_bar_->setTextVisible(false);  // Hide the default percentage text
    // Remove custom stylesheet to use default theme appearance
    // The bar will use the system's default progress bar styling
    context_layout->addWidget(context_bar_);
    
    context_label_ = new QLabel("0% of context used", this);
    context_label_->setAlignment(Qt::AlignCenter);
    context_label_->setStyleSheet("QLabel { font-size: 12px; }");
    context_layout->addWidget(context_label_);
    
    
    // Add all groups to main layout
    layout->addWidget(token_group);
    layout->addWidget(context_group);
    layout->addStretch();
}

void MetricsPanel::update_token_usage(size_t input_tokens, size_t output_tokens, 
                                     size_t cache_read, size_t cache_write) {
    total_input_tokens_ += input_tokens;
    total_output_tokens_ += output_tokens;
    total_cache_read_tokens_ += cache_read;
    total_cache_write_tokens_ += cache_write;
    
    input_tokens_label_->setText(QString::number(total_input_tokens_));
    output_tokens_label_->setText(QString::number(total_output_tokens_));
    
    // Update cache labels
    cache_read_label_->setText(QString::number(total_cache_read_tokens_));
    cache_write_label_->setText(QString::number(total_cache_write_tokens_));
    
    // Calculate total
    size_t total = total_input_tokens_ + total_output_tokens_;
    total_tokens_label_->setText(QString::number(total));
    
    // Calculate cost using PricingModel from SDK with the model from config
    const Config& config = Config::instance();
    claude::TokenUsage usage;
    usage.model = config.orchestrator.model.model;  // Use the actual model from config
    usage.input_tokens = total_input_tokens_;
    usage.output_tokens = total_output_tokens_;
    usage.cache_creation_tokens = total_cache_write_tokens_;
    usage.cache_read_tokens = total_cache_read_tokens_;
    
    double total_cost = claude::usage::PricingModel::calculate_cost(usage);
    cost_label_->setText(QString("$%1").arg(total_cost, 0, 'f', 4));
}

void MetricsPanel::update_context_usage(double percent) {
    context_bar_->setValue(static_cast<int>(percent));
    context_label_->setText(QString("%1% of context used").arg(percent, 0, 'f', 1));
    
    // Color code based on usage with smooth gradients
    QString color;
    if (percent > 80) {
        color = "#e74c3c";  // Red
    } else if (percent > 60) {
        color = "#f39c12";  // Orange
    } else if (percent > 40) {
        color = "#f1c40f";  // Yellow
    } else {
        color = "#27ae60";  // Green
    }
    
    // Only style the chunk (filled portion), let the background use default theme
    context_bar_->setStyleSheet(QString(
        "QProgressBar::chunk { background: %1; }").arg(color)
    );
}


} // namespace llm_re::ui