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
#include <QScrollArea>

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
    setWindowTitle("IDA Swarm - Orchestrator Control");
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
    token_tracker_ = new TokenTracker(this);  // Create token tracker
    
    // Bottom tabs for IRC, Tool calls, Token Usage, and Logs
    bottom_tabs_ = new QTabWidget(this);
    irc_viewer_ = new IRCViewer(this);
    tool_tracker_ = new ToolCallTracker(this);
    log_window_ = new LogWindow(this);
    
    bottom_tabs_->addTab(irc_viewer_, "IRC Communication");
    bottom_tabs_->addTab(tool_tracker_, "Tool Calls");
    bottom_tabs_->addTab(token_tracker_, "Token Usage");  // Add token tracker tab
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
    
    connect(task_panel_, &TaskPanel::clear_conversation_requested,
            []() {
                // Clear the orchestrator conversation
                UIOrchestratorBridge::instance().clear_conversation();
            });
    
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
    
    // Create menu bar
    create_menus();
    
    // Status bar
    statusBar()->showMessage("Ready");
}

void OrchestratorUI::create_menus() {
    // Create File menu  
    QMenu* fileMenu = menuBar()->addMenu("&File");
    
    // Add preferences action with standard keyboard shortcut
    QAction* preferencesAction = new QAction("&Preferences...", this);
    // Use standard preferences shortcut (Cmd+, on Mac, Ctrl+, on other platforms)
    preferencesAction->setShortcut(QKeySequence::Preferences);
    connect(preferencesAction, &QAction::triggered, this, &OrchestratorUI::on_preferences_clicked);
    fileMenu->addAction(preferencesAction);
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
            
        // Removed METRIC handler - all token updates now use AGENT_TOKEN_UPDATE
            
        case AgentEvent::TASK_COMPLETE:
            agent_monitor_->on_agent_completed(event.source);
            // Remove the agent's context bar when it completes
            metrics_panel_->remove_agent_context(event.source);
            // Mark agent as completed in token tracker
            token_tracker_->mark_agent_completed(event.source);
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
                log_window_->add_log(level, message);
            }
            break;
            
        case AgentEvent::AGENT_TOKEN_UPDATE:
            // Real-time token updates from agents and orchestrator
            if (event.payload.contains("agent_id") && event.payload.contains("tokens")) {
                std::string agent_id = event.payload["agent_id"];
                json token_data = event.payload["tokens"];
                
                msg("DEBUG: Received AGENT_TOKEN_UPDATE for %s: %s", 
                    agent_id.c_str(), token_data.dump().c_str());
                
                token_tracker_->update_agent_tokens(agent_id, token_data);
                
                // Get the TOTAL usage across all agents and orchestrator
                claude::TokenUsage total_usage = token_tracker_->get_total_usage();
                
                // Update main metrics panel with TOTAL tokens (orchestrator + all agents)
                metrics_panel_->set_token_usage(
                    total_usage.input_tokens, 
                    total_usage.output_tokens,
                    total_usage.cache_read_tokens, 
                    total_usage.cache_creation_tokens
                );
                
                // Set the correct total cost (sum of pre-calculated costs from all agents)
                double total_cost = token_tracker_->get_total_cost();
                metrics_panel_->set_total_cost(total_cost);
                
                // Update context bars
                if (agent_id == "orchestrator") {
                    // Get per-iteration tokens for context calculation
                    json session_tokens = event.payload.value("session_tokens", json());
                    size_t session_input = session_tokens.value("input_tokens", 0);
                    size_t session_cache_read = session_tokens.value("cache_read_tokens", 0);
                    
                    // Fall back to cumulative if session_tokens not available (old format)
                    if (session_tokens.empty()) {
                        session_input = token_data.value("input_tokens", 0);
                        session_cache_read = token_data.value("cache_read_tokens", 0);
                    }
                    
                    // Orchestrator uses 200k context (hardcoded in orchestrator.cpp)
                    const size_t orchestrator_context_limit = 200000;
                    size_t total_context_used = session_input + session_cache_read;
                    double context_percent = (total_context_used * 100.0) / orchestrator_context_limit;
                    metrics_panel_->update_context_usage(context_percent);
                    
                } else {
                    // Update agent context bars
                    size_t input_tokens = token_data.value("input_tokens", 0);
                    size_t cache_read_tokens = token_data.value("cache_read_tokens", 0);
                    
                    // Get per-iteration tokens for context calculation if available
                    json session_tokens = event.payload.value("session_tokens", json());
                    size_t session_input = session_tokens.value("input_tokens", input_tokens);
                    size_t session_cache_read = session_tokens.value("cache_read_tokens", cache_read_tokens);
                    
                    msg("DEBUG: Agent %s cumulative - In: %zu, Cache Read: %zu",
                        agent_id.c_str(), input_tokens, cache_read_tokens);
                    msg("DEBUG: Agent %s per-iteration - In: %zu, Cache Read: %zu",
                        agent_id.c_str(), session_input, session_cache_read);
                    
                    // Calculate context percentage using per-iteration tokens
                    // Total context = input + cache_read
                    const Config& config = Config::instance();
                    size_t max_context_tokens = config.agent.context_limit;
                    size_t total_context_used = session_input + session_cache_read;
                    double context_percent = (total_context_used * 100.0) / max_context_tokens;
                    
                    msg("DEBUG: Agent %s context usage: %zu / %zu = %.2f%% (per-iteration)",
                        agent_id.c_str(), total_context_used, max_context_tokens, context_percent);
                    
                    // Update the agent's context bar with per-iteration percentage but cumulative token counts
                    metrics_panel_->update_agent_context(agent_id, context_percent, input_tokens, cache_read_tokens);
                }
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
    token_tracker_->clear_all();
    metrics_panel_->clear_agent_contexts();
    
    // Also clear the orchestrator conversation
    UIOrchestratorBridge::instance().clear_conversation();
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
        // Also clear the orchestrator conversation
        emit clear_conversation_requested();
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
    int active_count = 0;
    
    // Count only active agents (not completed or failed)
    for (int i = 0; i < agent_table_->rowCount(); ++i) {
        if (agent_table_->item(i, 2)) {  // Status column
            QString status = agent_table_->item(i, 2)->text();
            // Count agents that are spawning, active, idle, running, or paused
            // Don't count completed or failed agents
            if (status != "Completed" && status != "Failed") {
                active_count++;
            }
        }
    }
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

    // Style the #conflicts option differently to indicate it's a meta-channel
    QFont conflicts_font = channel_combo_->font();
    conflicts_font.setItalic(true);
    channel_combo_->setItemData(3, conflicts_font, Qt::FontRole);
    channel_combo_->setItemData(3, "View list of all conflict resolution channels", Qt::ToolTipRole);

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
    
    connect(message_tree_, &QTreeWidget::itemDoubleClicked,
            this, &IRCViewer::on_item_double_clicked);
}

void IRCViewer::add_message(const std::string& channel, const std::string& sender, const std::string& message) {
    // Store the message
    IRCMessage msg;
    msg.time = QDateTime::currentDateTime().toString("hh:mm:ss");
    msg.channel = QString::fromStdString(channel);
    msg.sender = QString::fromStdString(sender);
    msg.message = QString::fromStdString(message);
    all_messages_.push_back(msg);
    
    // Detect and track conflict channels
    if (channel.find("#conflict_") == 0) {
        discovered_conflict_channels_.insert(channel);
    }
    
    // Only add to tree if we're not showing the conflict list
    if (!showing_conflict_list_) {
        auto* item = new QTreeWidgetItem(message_tree_);
        item->setText(0, msg.time);
        item->setText(1, msg.channel);
        item->setText(2, msg.sender);
        item->setText(3, msg.message);

        // Add tooltip showing full message for all columns
        item->setToolTip(0, msg.message);
        item->setToolTip(1, msg.message);
        item->setToolTip(2, msg.message);
        item->setToolTip(3, msg.message);

        // No special background for conflict channels
        
        apply_filters();
    }
}

void IRCViewer::add_join(const std::string& channel, const std::string& nick) {
    add_message(channel, "***", nick + " has joined");
}

void IRCViewer::add_part(const std::string& channel, const std::string& nick) {
    add_message(channel, "***", nick + " has left");
}

void IRCViewer::clear_messages() {
    message_tree_->clear();
    all_messages_.clear();
    discovered_conflict_channels_.clear();
    showing_conflict_list_ = false;
}

void IRCViewer::set_channel_filter(const std::string& channel) {
    current_channel_filter_ = channel;
    apply_filters();
}

void IRCViewer::on_channel_selected() {
    if (updating_dropdown_) return;  // Prevent recursion

    QString channel = channel_combo_->currentText();

    if (channel == "All Channels") {
        current_channel_filter_.clear();
        showing_conflict_list_ = false;
    } else if (channel == "#conflicts") {
        current_channel_filter_.clear();  // FIX: Don't set filter for meta-channel
        showing_conflict_list_ = true;
        refresh_conflict_channels();  // Proactive discovery
    } else {
        current_channel_filter_ = channel.toStdString();
        showing_conflict_list_ = false;
    }

    apply_filters();
}

void IRCViewer::on_filter_changed(const QString& text) {
    apply_filters();
}

void IRCViewer::on_item_double_clicked(QTreeWidgetItem* item, int column) {
    // If we're showing the conflict list and user double-clicks a channel
    if (showing_conflict_list_ && item) {
        QString channel_name = item->text(1);  // Channel column
        if (channel_name.startsWith("#conflict_")) {
            // Switch to viewing this specific conflict channel
            current_channel_filter_ = channel_name.toStdString();
            showing_conflict_list_ = false;

            // Prevent signal loop when updating dropdown
            updating_dropdown_ = true;

            // Update dropdown to reflect we're viewing a specific channel
            // Add the conflict channel to dropdown if not there
            int index = channel_combo_->findText(channel_name);
            if (index == -1) {
                channel_combo_->addItem(channel_name);
                index = channel_combo_->count() - 1;
            }
            channel_combo_->setCurrentIndex(index);

            // Re-enable signal handling
            updating_dropdown_ = false;

            apply_filters();
        }
    }
}

void IRCViewer::show_conflict_channel_list() {
    // Clear tree and show list of discovered conflict channels
    message_tree_->clear();

    // Update headers for conflict list mode
    message_tree_->setHeaderLabels(QStringList()
        << "" << "Conflict Channel" << "Status" << "Action");

    for (const auto& conflict_channel : discovered_conflict_channels_) {
        auto* item = new QTreeWidgetItem(message_tree_);
        item->setText(0, "");
        item->setText(1, QString::fromStdString(conflict_channel));

        // Count messages for this channel
        int msg_count = 0;
        for (const auto& msg : all_messages_) {
            if (msg.channel.toStdString() == conflict_channel) {
                msg_count++;
            }
        }
        item->setText(2, QString("%1 messages").arg(msg_count));

        item->setText(3, "Double-click to view");

        // Make it stand out as clickable
        QFont font = item->font(1);
        font.setBold(true);
        item->setFont(1, font);

        // Add tooltip with channel details
        QString channel_qstr = QString::fromStdString(conflict_channel);
        item->setToolTip(1, QString("Conflict resolution channel: %1").arg(channel_qstr));
        item->setToolTip(3, "Double-click to view messages in this conflict channel");
    }

    if (discovered_conflict_channels_.empty()) {
        auto* item = new QTreeWidgetItem(message_tree_);
        item->setText(0, "");
        item->setText(1, "");
        item->setText(2, "");
        item->setText(3, "No conflict channels active");
        item->setForeground(3, Qt::gray);
    }

    // Restore normal headers when leaving conflict list mode (will be done in apply_filters)
}

void IRCViewer::apply_filters() {
    // If we're showing the conflict channel list, display that instead of messages
    if (showing_conflict_list_) {
        show_conflict_channel_list();
        return;
    }

    // Restore normal headers for message view
    message_tree_->setHeaderLabels(QStringList() << "Time" << "Channel" << "Sender" << "Message");

    // Rebuild tree with all messages and apply filters
    message_tree_->clear();
    
    QString filter_text = filter_input_->text().toLower();
    
    for (const auto& msg : all_messages_) {
        bool visible = true;
        
        // Channel filter
        if (!current_channel_filter_.empty()) {
            if (msg.channel.toStdString() != current_channel_filter_) {
                visible = false;
            }
        }
        
        // Text filter
        if (visible && !filter_text.isEmpty()) {
            bool match = msg.sender.toLower().contains(filter_text) ||
                         msg.message.toLower().contains(filter_text);
            if (!match) {
                visible = false;
            }
        }
        
        if (visible) {
            auto* item = new QTreeWidgetItem(message_tree_);
            item->setText(0, msg.time);
            item->setText(1, msg.channel);
            item->setText(2, msg.sender);
            item->setText(3, msg.message);

            // Add tooltip showing full message for all columns
            item->setToolTip(0, msg.message);
            item->setToolTip(1, msg.message);
            item->setToolTip(2, msg.message);
            item->setToolTip(3, msg.message);

            // No special highlighting for conflict channels
        }
    }
}

void IRCViewer::refresh_conflict_channels() {
    // Get orchestrator through bridge
    auto* bridge = &UIOrchestratorBridge::instance();
    auto* orch = bridge->get_orchestrator();

    if (orch) {
        auto channels = orch->get_irc_channels();
        discovered_conflict_channels_.clear();

        for (const auto& channel : channels) {
            if (channel.find("#conflict_") == 0) {
                discovered_conflict_channels_.insert(channel);
            }
        }
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
    tool_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    
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
        auto* result_item = new QTableWidgetItem(is_write ? "Write" : "Read");
        if (is_write) {
            result_item->setForeground(QColor(255, 140, 0));  // Orange text for writes
            QFont font = result_item->font();
            font.setBold(true);
            result_item->setFont(font);
        } else {
            result_item->setForeground(QColor(100, 100, 100));  // Gray text for reads
        }
        tool_table_->setItem(row, 4, result_item);
    } else if (tool_data.contains("result")) {
        // Legacy format from agents
        if (tool_data["result"].contains("success") && tool_data["result"]["success"] == false) {
            auto* result_item = new QTableWidgetItem("Failed");
            result_item->setForeground(QColor(200, 0, 0));  // Red text for failures
            QFont font = result_item->font();
            font.setBold(true);
            result_item->setFont(font);
            tool_table_->setItem(row, 4, result_item);
        } else {
            auto* result_item = new QTableWidgetItem("Success");
            result_item->setForeground(QColor(0, 150, 0));  // Green text for success
            tool_table_->setItem(row, 4, result_item);
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
    layout->setSpacing(5);
    layout->setContentsMargins(5, 5, 5, 5);
    
    // Compact token usage section (horizontal layout)
    auto* token_group = new QGroupBox("Total Token Usage", this);
    token_group->setMaximumHeight(80);
    auto* token_layout = new QHBoxLayout(token_group);
    
    // Input/Output tokens
    auto* io_layout = new QVBoxLayout();
    auto* input_line = new QHBoxLayout();
    input_line->addWidget(new QLabel("In:", this));
    input_tokens_label_ = new QLabel("0", this);
    input_line->addWidget(input_tokens_label_);
    input_line->addStretch();
    io_layout->addLayout(input_line);
    
    auto* output_line = new QHBoxLayout();
    output_line->addWidget(new QLabel("Out:", this));
    output_tokens_label_ = new QLabel("0", this);
    output_line->addWidget(output_tokens_label_);
    output_line->addStretch();
    io_layout->addLayout(output_line);
    token_layout->addLayout(io_layout);
    
    // Cache tokens
    auto* cache_layout = new QVBoxLayout();
    auto* cache_read_line = new QHBoxLayout();
    cache_read_line->addWidget(new QLabel("Cache R:", this));
    cache_read_label_ = new QLabel("0", this);
    cache_read_label_->setStyleSheet("QLabel { color: #0080ff; }");
    cache_read_line->addWidget(cache_read_label_);
    cache_read_line->addStretch();
    cache_layout->addLayout(cache_read_line);
    
    auto* cache_write_line = new QHBoxLayout();
    cache_write_line->addWidget(new QLabel("Cache W:", this));
    cache_write_label_ = new QLabel("0", this);
    cache_write_label_->setStyleSheet("QLabel { color: #ff8000; }");
    cache_write_line->addWidget(cache_write_label_);
    cache_write_line->addStretch();
    cache_layout->addLayout(cache_write_line);
    token_layout->addLayout(cache_layout);
    
    // Total and cost
    auto* total_layout = new QVBoxLayout();
    auto* total_line = new QHBoxLayout();
    total_line->addWidget(new QLabel("Total:", this));
    total_tokens_label_ = new QLabel("0", this);
    total_tokens_label_->setStyleSheet("QLabel { font-weight: bold; }");
    total_line->addWidget(total_tokens_label_);
    total_line->addStretch();
    total_layout->addLayout(total_line);
    
    auto* cost_line = new QHBoxLayout();
    cost_line->addWidget(new QLabel("Cost:", this));
    cost_label_ = new QLabel("$0.00", this);
    cost_label_->setStyleSheet("QLabel { color: #008000; font-weight: bold; }");
    cost_line->addWidget(cost_label_);
    cost_line->addStretch();
    total_layout->addLayout(cost_line);
    token_layout->addLayout(total_layout);
    
    // Orchestrator context section
    auto* orch_context_group = new QGroupBox("Orchestrator Context", this);
    orch_context_group->setMaximumHeight(80);
    auto* orch_context_layout = new QVBoxLayout(orch_context_group);
    
    // Create orchestrator context bar with label on same line
    auto* orch_bar_layout = new QHBoxLayout();
    context_label_ = new QLabel("0%", this);
    context_label_->setMinimumWidth(40);
    context_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    orch_bar_layout->addWidget(context_label_);
    
    context_bar_ = create_context_bar();
    orch_bar_layout->addWidget(context_bar_);
    orch_context_layout->addLayout(orch_bar_layout);
    
    // Agent context usage section with scroll area
    auto* agent_context_group = new QGroupBox("Agent Context Usage", this);
    auto* agent_group_layout = new QVBoxLayout(agent_context_group);
    
    agent_context_scroll_ = new QScrollArea(this);
    agent_context_scroll_->setWidgetResizable(true);
    agent_context_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    agent_context_container_ = new QWidget();
    agent_context_layout_ = new QVBoxLayout(agent_context_container_);
    agent_context_layout_->setSpacing(2);
    agent_context_layout_->addStretch();
    
    agent_context_scroll_->setWidget(agent_context_container_);
    agent_group_layout->addWidget(agent_context_scroll_);
    
    // Add all groups to main layout
    layout->addWidget(token_group);
    layout->addWidget(orch_context_group);
    layout->addWidget(agent_context_group, 1); // Give it stretch priority
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
    
    // Cost is now set separately via set_total_cost() to use correct per-agent pricing
}

void MetricsPanel::set_token_usage(size_t input_tokens, size_t output_tokens, 
                                   size_t cache_read, size_t cache_write) {
    // Set absolute values (not incremental)
    total_input_tokens_ = input_tokens;
    total_output_tokens_ = output_tokens;
    total_cache_read_tokens_ = cache_read;
    total_cache_write_tokens_ = cache_write;
    
    input_tokens_label_->setText(QString::number(total_input_tokens_));
    output_tokens_label_->setText(QString::number(total_output_tokens_));
    
    // Update cache labels
    cache_read_label_->setText(QString::number(total_cache_read_tokens_));
    cache_write_label_->setText(QString::number(total_cache_write_tokens_));
    
    // Calculate total
    size_t total = total_input_tokens_ + total_output_tokens_;
    total_tokens_label_->setText(QString::number(total));
    
    // Cost is now set separately via set_total_cost() to use correct per-agent pricing
}

void MetricsPanel::set_total_cost(double cost) {
    cost_label_->setText(QString("$%1").arg(cost, 0, 'f', 4));
}

void MetricsPanel::update_context_usage(double percent) {
    context_bar_->setValue(static_cast<int>(percent));
    update_context_bar_style(context_bar_, percent);
    context_label_->setText(QString("%1%").arg(percent, 0, 'f', 1));
}

void MetricsPanel::update_agent_context(const std::string& agent_id, double percent,
                                       size_t input_tokens, size_t cache_read_tokens) {
    // Find or create agent context bar
    if (agent_contexts_.find(agent_id) == agent_contexts_.end()) {
        // Create new agent context bar
        AgentContextBar& agent_bar = agent_contexts_[agent_id];
        
        // Create horizontal layout for this agent
        auto* agent_layout = new QHBoxLayout();
        agent_layout->setSpacing(5);
        
        // Agent name label
        agent_bar.label = new QLabel(QString::fromStdString(agent_id), this);
        agent_bar.label->setMinimumWidth(80);
        agent_bar.label->setMaximumWidth(100);
        agent_layout->addWidget(agent_bar.label);
        
        // Percentage label
        agent_bar.tokens_label = new QLabel("0%", this);
        agent_bar.tokens_label->setMinimumWidth(40);
        agent_bar.tokens_label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        agent_layout->addWidget(agent_bar.tokens_label);
        
        // Progress bar
        agent_bar.bar = create_context_bar();
        agent_layout->addWidget(agent_bar.bar, 1); // Give it stretch
        
        // Insert before the stretch at the end
        int insert_pos = agent_context_layout_->count() - 1; // Before stretch
        agent_context_layout_->insertLayout(insert_pos, agent_layout);
    }
    
    // Update the agent's context bar
    AgentContextBar& agent_bar = agent_contexts_[agent_id];
    agent_bar.percentage = percent;
    agent_bar.bar->setValue(static_cast<int>(percent));
    update_context_bar_style(agent_bar.bar, percent);
    
    // Update label with percentage and token count
    QString tokens_text = QString("%1%").arg(percent, 0, 'f', 1);
    agent_bar.tokens_label->setText(tokens_text);
    
    // Add tooltip with detailed info
    QString tooltip = QString("%1\nInput: %2 tokens\nCache Read: %3 tokens\nContext Used: %5%")
        .arg(QString::fromStdString(agent_id))
        .arg(input_tokens)
        .arg(cache_read_tokens)
        .arg(percent, 0, 'f', 1);
    agent_bar.bar->setToolTip(tooltip);
}

void MetricsPanel::clear_agent_contexts() {
    // Clear all agent context bars
    for (auto& [id, agent_bar] : agent_contexts_) {
        delete agent_bar.bar;
        delete agent_bar.label;
        delete agent_bar.tokens_label;
    }
    agent_contexts_.clear();
    
    // Clear the layout
    QLayoutItem* item;
    while ((item = agent_context_layout_->takeAt(0)) != nullptr) {
        if (item->layout()) {
            QLayoutItem* subItem;
            while ((subItem = item->layout()->takeAt(0)) != nullptr) {
                delete subItem;
            }
        }
        delete item;
    }
    
    // Re-add the stretch
    agent_context_layout_->addStretch();
}

void MetricsPanel::remove_agent_context(const std::string& agent_id) {
    auto it = agent_contexts_.find(agent_id);
    if (it == agent_contexts_.end()) {
        return;
    }
    
    // Find and remove the layout containing this agent's widgets
    for (int i = 0; i < agent_context_layout_->count() - 1; i++) {  // -1 to skip the stretch
        QLayoutItem* item = agent_context_layout_->itemAt(i);
        if (item && item->layout()) {
            QHBoxLayout* hlayout = qobject_cast<QHBoxLayout*>(item->layout());
            if (hlayout && hlayout->count() > 0) {
                QLabel* label = qobject_cast<QLabel*>(hlayout->itemAt(0)->widget());
                if (label && label->text() == QString::fromStdString(agent_id)) {
                    // Found it - remove the layout
                    agent_context_layout_->takeAt(i);
                    
                    // Delete all widgets in the layout
                    while (QLayoutItem* subItem = hlayout->takeAt(0)) {
                        if (subItem->widget()) {
                            delete subItem->widget();
                        }
                        delete subItem;
                    }
                    delete hlayout;
                    break;
                }
            }
        }
    }
    
    // Remove from map
    agent_contexts_.erase(it);
    
    // Force layout update
    if (agent_context_container_) {
        agent_context_container_->update();
    }
}

QProgressBar* MetricsPanel::create_context_bar() {
    auto* bar = new QProgressBar(this);
    bar->setMinimum(0);
    bar->setMaximum(100);
    bar->setValue(0);
    bar->setTextVisible(false);
    bar->setMaximumHeight(20);
    return bar;
}

void MetricsPanel::update_context_bar_style(QProgressBar* bar, double percentage) {
    if (percentage < 50) {
        // Green - safe zone
        bar->setStyleSheet(
            "QProgressBar::chunk {"
            "    background-color: #27ae60;"
            "}"
        );
    } else if (percentage < 80) {
        // Yellow - warning zone
        bar->setStyleSheet(
            "QProgressBar::chunk {"
            "    background-color: #f39c12;"
            "}"
        );
    } else {
        // Red - critical zone
        bar->setStyleSheet(
            "QProgressBar::chunk {"
            "    background-color: #e74c3c;"
            "}"
        );
    }
}


// TokenTracker implementation
TokenTracker::TokenTracker(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    
    // Create header with total cost
    auto* header_layout = new QHBoxLayout();
    auto* title_label = new QLabel("<b>Real-time Token Usage</b>");
    total_cost_label_ = new QLabel("Total Cost: $0.0000");
    total_cost_label_->setAlignment(Qt::AlignRight);
    header_layout->addWidget(title_label);
    header_layout->addStretch();
    header_layout->addWidget(total_cost_label_);
    layout->addLayout(header_layout);
    
    // Create token table
    token_table_ = new QTableWidget(this);
    token_table_->setColumnCount(7);
    token_table_->setHorizontalHeaderLabels({
        "Agent/Orchestrator", "Status", "Input", "Output", "Cache Read", "Cache Write", "Cost"
    });
    
    // Set column widths
    token_table_->setColumnWidth(0, 150);  // Agent ID
    token_table_->setColumnWidth(1, 80);   // Status
    token_table_->setColumnWidth(2, 80);   // Input tokens
    token_table_->setColumnWidth(3, 80);   // Output tokens
    token_table_->setColumnWidth(4, 90);   // Cache read tokens
    token_table_->setColumnWidth(5, 90);   // Cache write tokens
    token_table_->setColumnWidth(6, 100);  // Cost
    
    token_table_->horizontalHeader()->setStretchLastSection(true);
    token_table_->setAlternatingRowColors(true);
    token_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    token_table_->setSortingEnabled(true);
    
    layout->addWidget(token_table_);
}

void TokenTracker::update_agent_tokens(const std::string& agent_id, const json& token_data) {
    msg("TokenTracker: Updating tokens for %s with data: %s", 
        agent_id.c_str(), token_data.dump().c_str());
    
    // Update or create agent entry
    AgentTokens& agent = agent_tokens_[agent_id];
    
    // Parse token data
    if (token_data.contains("input_tokens")) {
        agent.current.input_tokens = token_data["input_tokens"];
    }
    if (token_data.contains("output_tokens")) {
        agent.current.output_tokens = token_data["output_tokens"];
    }
    if (token_data.contains("cache_read_tokens")) {
        agent.current.cache_read_tokens = token_data["cache_read_tokens"];
    }
    if (token_data.contains("cache_creation_tokens")) {
        agent.current.cache_creation_tokens = token_data["cache_creation_tokens"];
    }
    
    // Always set the model if provided
    if (token_data.contains("model")) {
        std::string model_str = token_data["model"];
        agent.current.model = claude::model_from_string(model_str);
    }
    
    // Always calculate cost from cumulative totals
    // The estimated_cost from agent is per-iteration, but we want total cost
    agent.estimated_cost = agent.current.estimated_cost();
    
    if (token_data.contains("iteration")) {
        agent.iteration = token_data["iteration"];
    }
    
    agent.is_active = true;
    agent.last_update = std::chrono::steady_clock::now();
    
    // Update display
    refresh_display();
}

void TokenTracker::mark_agent_completed(const std::string& agent_id) {
    auto it = agent_tokens_.find(agent_id);
    if (it != agent_tokens_.end()) {
        it->second.is_completed = true;
        it->second.is_active = false;  // No longer active
        refresh_display();
    }
}

void TokenTracker::clear_all() {
    agent_tokens_.clear();
    token_table_->setRowCount(0);
    total_cost_label_->setText("Total Cost: $0.0000");
}

claude::TokenUsage TokenTracker::get_total_usage() const {
    claude::TokenUsage total;
    for (const auto& [id, agent] : agent_tokens_) {
        total += agent.current;
    }
    return total;
}

double TokenTracker::get_total_cost() const {
    double total_cost = 0.0;
    for (const auto& [id, agent] : agent_tokens_) {
        total_cost += agent.estimated_cost;
    }
    return total_cost;
}

void TokenTracker::refresh_display() {
    // Update table rows
    token_table_->setRowCount(agent_tokens_.size());
    
    int row = 0;
    double total_cost = 0.0;
    
    for (const auto& [agent_id, agent] : agent_tokens_) {
        // Extract numeric part for proper sorting
        int sort_value = 0;
        if (agent_id.starts_with("agent_")) {
            try {
                sort_value = std::stoi(agent_id.substr(6));  // Extract number after "agent_"
            } catch (...) {
                sort_value = 999999;  // Fallback for non-numeric agents
            }
        } else if (agent_id == "orchestrator") {
            sort_value = -1;  // Orchestrator always sorts first
        } else {
            sort_value = 999999;  // Other entries sort last
        }

        // Agent/Orchestrator name - use NumericTableWidgetItem for proper sorting
        NumericTableWidgetItem *id_item = new NumericTableWidgetItem(QString::fromStdString(agent_id), sort_value);

        // Make orchestrator bold
        if (agent_id == "orchestrator") {
            id_item->setFont(QFont("", -1, QFont::Bold));
        }

        token_table_->setItem(row, 0, id_item);
        
        // Status (active/completed)
        QString status;
        if (agent.is_completed) {
            status = "Completed";
        } else {
            status = "Active";
        }
        
        auto* status_item = new QTableWidgetItem(status);
        status_item->setForeground(QColor(39, 174, 96)); // Green for both
        token_table_->setItem(row, 1, status_item);
        
        // Input tokens - use NumericTableWidgetItem for proper numeric sorting
        auto* input_item = new NumericTableWidgetItem(
            QString::number(agent.current.input_tokens),
            static_cast<int>(agent.current.input_tokens)
        );
        input_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        token_table_->setItem(row, 2, input_item);

        // Output tokens - use NumericTableWidgetItem for proper numeric sorting
        auto* output_item = new NumericTableWidgetItem(
            QString::number(agent.current.output_tokens),
            static_cast<int>(agent.current.output_tokens)
        );
        output_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        token_table_->setItem(row, 3, output_item);

        // Cache read tokens - use NumericTableWidgetItem for proper numeric sorting
        auto* cache_read_item = new NumericTableWidgetItem(
            QString::number(agent.current.cache_read_tokens),
            static_cast<int>(agent.current.cache_read_tokens)
        );
        cache_read_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        token_table_->setItem(row, 4, cache_read_item);

        // Cache write tokens - use NumericTableWidgetItem for proper numeric sorting
        auto* cache_write_item = new NumericTableWidgetItem(
            QString::number(agent.current.cache_creation_tokens),
            static_cast<int>(agent.current.cache_creation_tokens)
        );
        cache_write_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        token_table_->setItem(row, 5, cache_write_item);

        // Cost (use pre-calculated cost from agent) - use DoubleTableWidgetItem for proper numeric sorting
        double cost = agent.estimated_cost;
        total_cost += cost;
        auto* cost_item = new DoubleTableWidgetItem(format_cost(cost), cost);
        cost_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        token_table_->setItem(row, 6, cost_item);
        
        row++;
    }
    
    // Update total cost
    total_cost_label_->setText(QString("Total Cost: %1").arg(format_cost(total_cost)));
}

QString TokenTracker::format_tokens(const claude::TokenUsage& usage) {
    return QString("%1 in / %2 out")
        .arg(usage.input_tokens)
        .arg(usage.output_tokens);
}

QString TokenTracker::format_cost(double cost) {
    return QString("$%1").arg(cost, 0, 'f', 4);
}

} // namespace llm_re::ui