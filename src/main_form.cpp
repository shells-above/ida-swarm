//
// Created by user on 6/30/25.
//

#include "main_form.h"

namespace llm_re {

// Global instance
static MainForm* g_main_form = nullptr;

MainForm* get_main_form() {
    return g_main_form;
}

// Worker implementation
void AgentWorker::process() {
    try {
        agent_->set_task(task_);
        emit finished();
    } catch (const std::exception& e) {
        emit error(QString::fromStdString(e.what()));
    }
}

// MainForm implementation
MainForm::MainForm(QWidget* parent) : QMainWindow(parent) {
    g_main_form = this;

    // Load configuration
    config_ = std::make_unique<Config>();
    load_settings();

    // Setup UI
    setup_ui();
    setup_menus();
    setup_toolbars();
    setup_status_bar();
    setup_docks();
    setup_central_widget();

    // Setup agent after UI is ready
    setup_agent();

    // Connect signals
    connect_signals();

    // Apply theme
    apply_theme(config_->ui.theme);

    // Update initial state
    update_ui_state();
}

MainForm::~MainForm() {
    // Stop any running tasks
    if (agent_ && agent_->is_running()) {
        agent_->stop();
    }

    if (worker_thread_ && worker_thread_->isRunning()) {
        worker_thread_->quit();
        worker_thread_->wait();
    }

    // Save settings
    save_settings();

    if (g_main_form == this) {
        g_main_form = nullptr;
    }
}

void MainForm::setup_ui() {
    setWindowTitle("LLM Reverse Engineering Assistant");
    resize(1200, 800);

    // Create progress overlay
    progress_overlay_ = new ui::ProgressOverlay(this);
    progress_overlay_->hide();
}

void MainForm::setup_menus() {
    // File menu
    QMenu* file_menu = menuBar()->addMenu("&File");

    export_action_ = file_menu->addAction("&Export Session...", this, &MainForm::on_export_clicked);
    export_action_->setShortcut(QKeySequence::Save);

    file_menu->addSeparator();

    QAction* quit_action = file_menu->addAction("&Close", this, &QWidget::close);
    quit_action->setShortcut(QKeySequence::Quit);

    // Edit menu
    QMenu* edit_menu = menuBar()->addMenu("&Edit");

    clear_action_ = edit_menu->addAction("&Clear", this, &MainForm::on_clear_clicked);
    clear_action_->setShortcut(QKeySequence("Ctrl+L"));

    edit_menu->addSeparator();

    search_action_ = edit_menu->addAction("&Search...", this, &MainForm::on_search_clicked);
    search_action_->setShortcut(QKeySequence::Find);

    // View menu
    QMenu* view_menu = menuBar()->addMenu("&View");

    toggle_memory_action_ = view_menu->addAction("&Memory View");
    toggle_memory_action_->setCheckable(true);
    toggle_memory_action_->setChecked(true);

    toggle_tools_action_ = view_menu->addAction("&Tools View");
    toggle_tools_action_->setCheckable(true);
    toggle_tools_action_->setChecked(true);

    toggle_stats_action_ = view_menu->addAction("&Statistics");
    toggle_stats_action_->setCheckable(true);
    toggle_stats_action_->setChecked(false);

    // Tools menu
    QMenu* tools_menu = menuBar()->addMenu("&Tools");

    tools_menu->addAction("&Templates...", this, &MainForm::on_templates_clicked);
    tools_menu->addSeparator();
    settings_action_ = tools_menu->addAction("&Settings...", this, &MainForm::on_settings_clicked);
    settings_action_->setShortcut(QKeySequence::Preferences);

    // Help menu
    QMenu* help_menu = menuBar()->addMenu("&Help");

    about_action_ = help_menu->addAction("&About...", this, &MainForm::on_about_clicked);
}

void MainForm::setup_toolbars() {
    QToolBar* main_toolbar = addToolBar("Main");
    main_toolbar->setMovable(false);

    execute_action_ = main_toolbar->addAction("Execute", this, &MainForm::on_execute_clicked);
    execute_action_->setShortcut(QKeySequence("Ctrl+Return"));

    stop_action_ = main_toolbar->addAction("Stop", this, &MainForm::on_stop_clicked);
    stop_action_->setEnabled(false);

    main_toolbar->addSeparator();

    templates_button_ = new QPushButton("Templates");
    connect(templates_button_, &QPushButton::clicked, this, &MainForm::on_templates_clicked);
    main_toolbar->addWidget(templates_button_);

    main_toolbar->addSeparator();
    main_toolbar->addAction(search_action_);
    main_toolbar->addAction(export_action_);
}

void MainForm::setup_status_bar() {
    status_label_ = new QLabel("Ready");
    statusBar()->addWidget(status_label_);

    statusBar()->addWidget(new QLabel(" | "));

    token_label_ = new QLabel("Tokens: 0");
    statusBar()->addWidget(token_label_);

    statusBar()->addWidget(new QLabel(" | "));

    iteration_label_ = new QLabel("Iteration: 0");
    statusBar()->addWidget(iteration_label_);

    status_progress_ = new QProgressBar();
    status_progress_->setMaximumWidth(200);
    status_progress_->setVisible(false);
    statusBar()->addPermanentWidget(status_progress_);
}

void MainForm::setup_docks() {
    // Memory dock
    memory_dock_ = new QDockWidget("Memory", this);
    memory_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    QWidget* memory_widget = new QWidget();
    QVBoxLayout* memory_layout = new QVBoxLayout(memory_widget);

    memory_map_ = new ui::MemoryMapWidget();
    connect(memory_map_, &ui::MemoryMapWidget::address_clicked,
            this, &MainForm::on_address_clicked);
    memory_layout->addWidget(memory_map_);

    memory_tree_ = new QTreeWidget();
    memory_tree_->setHeaderLabels({"Address", "Name", "Level", "Status"});
    memory_tree_->setAlternatingRowColors(true);
    memory_layout->addWidget(memory_tree_);

    memory_dock_->setWidget(memory_widget);
    addDockWidget(Qt::RightDockWidgetArea, memory_dock_);
    connect(toggle_memory_action_, &QAction::toggled,
            memory_dock_, &QDockWidget::setVisible);

    // Tools dock
    tools_dock_ = new QDockWidget("Tool Execution", this);
    tools_dock_->setAllowedAreas(Qt::BottomDockWidgetArea);

    tool_execution_ = new ui::ToolExecutionWidget();
    tools_dock_->setWidget(tool_execution_);
    addDockWidget(Qt::BottomDockWidgetArea, tools_dock_);
    connect(toggle_tools_action_, &QAction::toggled,
            tools_dock_, &QDockWidget::setVisible);

    // Stats dock
    stats_dock_ = new QDockWidget("Statistics", this);
    stats_dock_->setAllowedAreas(Qt::AllDockWidgetAreas);

    stats_dashboard_ = new ui::StatsDashboard();
    stats_dock_->setWidget(stats_dashboard_);
    addDockWidget(Qt::RightDockWidgetArea, stats_dock_);
    stats_dock_->hide();
    connect(toggle_stats_action_, &QAction::toggled,
            stats_dock_, &QDockWidget::setVisible);

    // Tab docks
    tabifyDockWidget(memory_dock_, stats_dock_);
    memory_dock_->raise();
}

void MainForm::setup_central_widget() {
    QWidget* central = new QWidget();
    setCentralWidget(central);

    QVBoxLayout* layout = new QVBoxLayout(central);

    // Task input area
    QWidget* input_widget = new QWidget();
    QVBoxLayout* input_layout = new QVBoxLayout(input_widget);

    QLabel* task_label = new QLabel("Task:");
    input_layout->addWidget(task_label);

    task_input_ = new QTextEdit();
    task_input_->setMaximumHeight(100);
    task_input_->setPlaceholderText("Enter your reverse engineering task here...");
    input_layout->addWidget(task_input_);

    QHBoxLayout* button_layout = new QHBoxLayout();

    execute_button_ = new QPushButton("Execute");
    execute_button_->setDefault(true);
    connect(execute_button_, &QPushButton::clicked, this, &MainForm::on_execute_clicked);
    button_layout->addWidget(execute_button_);

    stop_button_ = new QPushButton("Stop");
    stop_button_->setEnabled(false);
    connect(stop_button_, &QPushButton::clicked, this, &MainForm::on_stop_clicked);
    button_layout->addWidget(stop_button_);

    button_layout->addStretch();
    input_layout->addLayout(button_layout);

    layout->addWidget(input_widget);

    // Main tabs
    main_tabs_ = new QTabWidget();
    connect(main_tabs_, &QTabWidget::currentChanged, this, &MainForm::on_tab_changed);

    // Chat tab
    chat_widget_ = new QWidget();
    QVBoxLayout* chat_layout = new QVBoxLayout(chat_widget_);

    message_list_ = new QListWidget();
    message_list_->setAlternatingRowColors(true);
    message_list_->setWordWrap(true);
    chat_layout->addWidget(message_list_);

    main_tabs_->addTab(chat_widget_, "Conversation");

    // Logs tab
    QWidget* log_widget = new QWidget();
    QVBoxLayout* log_layout = new QVBoxLayout(log_widget);

    QHBoxLayout* log_controls = new QHBoxLayout();
    log_controls->addWidget(new QLabel("Level:"));

    log_level_filter_ = new QComboBox();
    log_level_filter_->addItems({"All", "Info", "Warning", "Error"});
    connect(log_level_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainForm::on_log_level_changed);
    log_controls->addWidget(log_level_filter_);

    log_controls->addStretch();

    clear_log_button_ = new QPushButton("Clear");
    connect(clear_log_button_, &QPushButton::clicked, [this]() {
        log_entries_.clear();
        log_viewer_->clear();
    });
    log_controls->addWidget(clear_log_button_);

    log_layout->addLayout(log_controls);

    log_viewer_ = new QTextEdit();
    log_viewer_->setReadOnly(true);
    log_viewer_->setFont(QFont("Consolas", 9));
    log_layout->addWidget(log_viewer_);

    main_tabs_->addTab(log_widget, "Logs");

    // Timeline tab
    timeline_ = new ui::SessionTimelineWidget();
    main_tabs_->addTab(timeline_, "Timeline");

    layout->addWidget(main_tabs_);
}

void MainForm::setup_agent() {
    // Create agent
    agent_ = std::make_unique<REAgent>(*config_);

    // Set callbacks
    agent_->set_log_callback([this](const std::string& msg) {
        QMetaObject::invokeMethod(this, "on_agent_log",
                                 Qt::QueuedConnection,
                                 Q_ARG(QString, QString::fromStdString(msg)));
    });

    agent_->set_message_log_callback([this](const std::string& type, const json& content, int iteration) {
        current_iteration_ = iteration;

        // Convert JSON content to string for simplicity
        std::string content_str = content.dump(2);

        QMetaObject::invokeMethod(this, "on_agent_message",
                                 Qt::QueuedConnection,
                                 Q_ARG(QString, QString::fromStdString(type)),
                                 Q_ARG(QString, QString::fromStdString(content_str)));
    });

    agent_->set_final_report_callback([this](const std::string& report) {
        log(LogEntry::INFO, "=== FINAL REPORT ===");
        log(LogEntry::INFO, report);
        log(LogEntry::INFO, "===================");

        // Add as message to chat
        messages::Message msg = messages::Message::assistant_text(report);
        QMetaObject::invokeMethod(this, [this, msg]() {
            add_message_to_chat(msg);
        }, Qt::QueuedConnection);
    });
}

void MainForm::connect_signals() {
    // Dock visibility
    connect(memory_dock_, &QDockWidget::visibilityChanged,
            toggle_memory_action_, &QAction::setChecked);
    connect(tools_dock_, &QDockWidget::visibilityChanged,
            toggle_tools_action_, &QAction::setChecked);
    connect(stats_dock_, &QDockWidget::visibilityChanged,
            toggle_stats_action_, &QAction::setChecked);
}

void MainForm::show_and_raise() {
    show();
    raise();
    activateWindow();
}

void MainForm::execute_task(const std::string& task) {
    task_input_->setText(QString::fromStdString(task));
    on_execute_clicked();
}

void MainForm::set_current_address(ea_t addr) {
    current_address_ = addr;
    status_label_->setText(QString("Current: 0x%1").arg(addr, 0, 16));
    memory_map_->highlight_address(addr);
}

void MainForm::on_execute_clicked() {
    if (is_running_) return;

    std::string task = task_input_->toPlainText().toStdString();
    if (task.empty()) {
        QMessageBox::warning(this, "Warning", "Please enter a task to execute.");
        return;
    }

    // Clear previous session
    message_list_->clear();
    timeline_->clear_events();

    // Create new session
    SessionInfo session;
    session.id = "session_" + std::to_string(sessions_.size() + 1);
    session.task = task;
    session.start_time = std::chrono::system_clock::now();
    session_start_ = std::chrono::steady_clock::now();
    sessions_.push_back(session);

    // Update UI
    is_running_ = true;
    update_ui_state();
    progress_overlay_->show_progress("Executing task...");
    progress_overlay_->set_cancelable(true);

    // Add timeline event
    ui::SessionTimelineWidget::Event start_event;
    start_event.timestamp = std::chrono::steady_clock::now();
    start_event.type = "start";
    start_event.description = "Task started";
    timeline_->add_event(start_event);

    // Log
    log(LogEntry::INFO, "Starting task: " + task);

    // Add initial user message to chat
    messages::Message user_msg = messages::Message::user_text(task);
    add_message_to_chat(user_msg);

    // Create worker thread
    worker_thread_ = new QThread();
    worker_ = new AgentWorker(agent_.get(), task);
    worker_->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::started, worker_, &AgentWorker::process);
    connect(worker_, &AgentWorker::finished, this, &MainForm::on_worker_finished);
    connect(worker_, &AgentWorker::error, this, &MainForm::on_worker_error);
    connect(worker_, &AgentWorker::progress, this, &MainForm::on_worker_progress);
    connect(worker_, &AgentWorker::finished, worker_thread_, &QThread::quit);
    connect(worker_, &AgentWorker::finished, worker_, &QObject::deleteLater);
    connect(worker_thread_, &QThread::finished, worker_thread_, &QObject::deleteLater);
    connect(progress_overlay_, &ui::ProgressOverlay::cancelled, [this]() {
        agent_->stop();
    });

    worker_thread_->start();
}

void MainForm::on_stop_clicked() {
    if (!is_running_) return;

    log(LogEntry::WARNING, "Stopping task...");
    agent_->stop();

    if (worker_thread_ && worker_thread_->isRunning()) {
        worker_thread_->quit();
        worker_thread_->wait();
    }
}

void MainForm::on_clear_clicked() {
    if (is_running_) {
        QMessageBox::warning(this, "Warning", "Cannot clear while task is running.");
        return;
    }

    task_input_->clear();
    message_list_->clear();
    log_viewer_->clear();
    log_entries_.clear();
    timeline_->clear_events();
    memory_map_->clear_highlights();
    memory_tree_->clear();

    log(LogEntry::INFO, "Cleared all data");
}

void MainForm::on_export_clicked() {
    if (!export_dialog_) {
        export_dialog_ = new ui::ExportDialog(this);
    }

    if (export_dialog_->exec() == QDialog::Accepted) {
        export_session(export_dialog_->get_options());
    }
}

void MainForm::on_settings_clicked() {
    if (!config_widget_) {
        config_widget_ = new ui::ConfigWidget();
        connect(config_widget_, &ui::ConfigWidget::settings_changed,
                this, &MainForm::on_settings_changed);
    }

    config_widget_->load_settings(*config_);

    QDialog dialog(this);
    dialog.setWindowTitle("Settings");
    dialog.setModal(true);
    dialog.resize(600, 500);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(config_widget_);

    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);

    if (dialog.exec() == QDialog::Accepted) {
        on_settings_changed();
    }
}

void MainForm::on_templates_clicked() {
    if (!template_widget_) {
        template_widget_ = new ui::TaskTemplateWidget();
        connect(template_widget_, &ui::TaskTemplateWidget::template_selected,
                this, &MainForm::on_template_selected);
    }

    QDialog dialog(this);
    dialog.setWindowTitle("Task Templates");
    dialog.setModal(true);
    dialog.resize(700, 500);

    QVBoxLayout* layout = new QVBoxLayout(&dialog);
    layout->addWidget(template_widget_);

    dialog.exec();
}

void MainForm::on_search_clicked() {
    if (!search_dialog_) {
        search_dialog_ = new ui::SearchDialog(this);
        connect(search_dialog_, &ui::SearchDialog::result_selected,
                this, &MainForm::on_search_result_selected);
    }

    // TODO: Update search data
    search_dialog_->show();
    search_dialog_->raise();
}

void MainForm::on_about_clicked() {
    QMessageBox::about(this, "About",
        "<h3>LLM Reverse Engineering Assistant</h3>"
        "<p>Version 1.0.0</p>"
        "<p>An AI-powered reverse engineering assistant for IDA Pro.</p>"
        "<p>Uses Claude API to provide intelligent analysis and automation.</p>"
        "<p>Copyright © 2025</p>");
}

void MainForm::on_agent_log(const QString& message) {
    log(LogEntry::INFO, message.toStdString());
}

void MainForm::on_agent_message(const QString& type, const QString& content) {
    // Add timeline event
    ui::SessionTimelineWidget::Event event;
    event.timestamp = std::chrono::steady_clock::now();
    event.type = "message";
    event.description = type.toStdString() + ": " + truncate_string(content.toStdString(), 50);
    timeline_->add_event(event);

    // Update UI based on message type
    if (type == "REQUEST" || type == "RESPONSE") {
        try {
            json msg_json = json::parse(content.toStdString());

            // If it's a response with content, show it in chat
            if (type == "RESPONSE" && msg_json.contains("content")) {
                // Convert API response to message
                if (msg_json.contains("stop_reason")) {
                    std::string stop_reason = msg_json["stop_reason"];
                    on_agent_state_changed(QString::fromStdString(stop_reason));
                }

                // Handle usage stats
                if (msg_json.contains("usage")) {
                    json usage = msg_json["usage"];
                    int total = usage.value("input_tokens", 0) + usage.value("output_tokens", 0);
                    token_label_->setText(QString("Tokens: %1").arg(total));
                }
            }
        } catch (...) {
            // Not JSON, just log it
        }
    }

    // Update iteration
    iteration_label_->setText(QString("Iteration: %1").arg(current_iteration_));
}

void MainForm::on_agent_tool_executed(const QString& tool, const QString& input, const QString& result) {
    try {
        json input_json = json::parse(input.toStdString());
        json result_json = json::parse(result.toStdString());

        tool_execution_->add_tool_call(tool.toStdString(), input_json);
        tool_execution_->update_tool_result(tool.toStdString(), result_json);

        // Add timeline event
        ui::SessionTimelineWidget::Event event;
        event.timestamp = std::chrono::steady_clock::now();
        event.type = "tool";
        event.description = "Executed: " + tool.toStdString();
        event.metadata["tool"] = tool.toStdString();
        event.metadata["success"] = result_json.value("success", false);
        timeline_->add_event(event);

        // Update memory view if needed
        update_memory_view();

    } catch (const std::exception& e) {
        log(LogEntry::ERROR, "Failed to parse tool execution: " + std::string(e.what()));
    }
}

void MainForm::on_agent_state_changed(const QString& state) {
    status_label_->setText(state);

    if (state.contains("thinking", Qt::CaseInsensitive)) {
        progress_overlay_->update_progress(-1, "AI is thinking...");
    } else if (state.contains("tool", Qt::CaseInsensitive)) {
        progress_overlay_->update_progress(-1, state);
    }
}

void MainForm::on_agent_progress(int iteration, int total_tokens) {
    iteration_label_->setText(QString("Iteration: %1").arg(iteration));
    token_label_->setText(QString("Tokens: %1").arg(total_tokens));
}

void MainForm::on_worker_finished() {
    is_running_ = false;
    progress_overlay_->hide();
    update_ui_state();

    // Complete session
    auto& session = sessions_.back();
    session.end_time = std::chrono::system_clock::now();
    session.token_usage = agent_->get_token_usage();
    session.message_count = message_list_->count();
    session.success = true;
    session.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - session_start_).count();

    // Update timeline
    timeline_->set_session_info(session.task, session.token_usage);

    // Update statistics
    update_statistics();

    log(LogEntry::INFO, "Task completed successfully");
    worker_thread_ = nullptr;
    worker_ = nullptr;
}

void MainForm::on_worker_error(const QString& error) {
    is_running_ = false;
    progress_overlay_->hide();
    update_ui_state();

    // Complete session with error
    auto& session = sessions_.back();
    session.end_time = std::chrono::system_clock::now();
    session.success = false;
    session.error_message = error.toStdString();

    log(LogEntry::ERROR, "Task failed: " + error.toStdString());
    QMessageBox::critical(this, "Error", QString("Task failed: %1").arg(error));

    worker_thread_ = nullptr;
    worker_ = nullptr;
}

void MainForm::on_worker_progress(const QString& message) {
    progress_overlay_->update_progress(-1, message);
}

void MainForm::on_address_clicked(ea_t addr) {
    jumpto(addr);
    set_current_address(addr);
}

void MainForm::on_search_result_selected(const ui::SearchDialog::SearchResult& result) {
    // TODO: Implement search result handling
}

void MainForm::on_template_selected(const ui::TaskTemplateWidget::TaskTemplate& tmpl) {
    std::string task = tmpl.task;

    // Replace variables
    for (const auto& [key, value] : tmpl.variables) {
        std::string placeholder = "{" + key + "}";
        std::string actual_value;

        if (value == "current_ea") {
            actual_value = format_address(current_address_);
        } else {
            actual_value = value;
        }

        size_t pos = 0;
        while ((pos = task.find(placeholder, pos)) != std::string::npos) {
            task.replace(pos, placeholder.length(), actual_value);
            pos += actual_value.length();
        }
    }

    task_input_->setText(QString::fromStdString(task));
}

void MainForm::update_statistics() {
    json tool_stats;
    // TODO: Collect tool usage statistics

    stats_dashboard_->update_stats(agent_->get_state_json(), sessions_, tool_stats);
}

void MainForm::update_ui_state() {
    execute_button_->setEnabled(!is_running_);
    execute_action_->setEnabled(!is_running_);
    stop_button_->setEnabled(is_running_);
    stop_action_->setEnabled(is_running_);
    task_input_->setReadOnly(is_running_);

    if (is_running_) {
        status_progress_->setVisible(true);
        status_progress_->setMaximum(0);  // Indeterminate
    } else {
        status_progress_->setVisible(false);
    }
}

void MainForm::update_memory_view() {
    // Get memory snapshot
    json memory_snapshot = agent_->get_memory()->export_memory_snapshot();

    // Update memory map
    memory_map_->update_memory(memory_snapshot);

    // Update memory tree
    memory_tree_->clear();

    if (memory_snapshot.contains("functions")) {
        for (const auto& func : memory_snapshot["functions"]) {
            QTreeWidgetItem* item = new QTreeWidgetItem(memory_tree_);
            item->setText(0, QString::fromStdString(func["address"].get<std::string>()));
            item->setText(1, QString::fromStdString(func.value("name", "unknown")));
            item->setText(2, QString::number(func.value("current_level", 0)));

            // Add analysis levels as children
            if (func.contains("descriptions")) {
                for (const auto& [level, desc] : func["descriptions"].items()) {
                    QTreeWidgetItem* child = new QTreeWidgetItem(item);
                    child->setText(0, QString("Level %1").arg(level.data()));
                    child->setText(1, QString::fromStdString(desc.get<std::string>().substr(0, 50) + "..."));
                }
            }
        }
    }
}

void MainForm::on_tab_changed(int index) {
    // Update focus based on tab
}

void MainForm::on_log_level_changed(int index) {
    // Re-filter logs
    log_viewer_->clear();

    for (const auto& entry : log_entries_) {
        if (index > 0 && entry.level < index - 1) {
            continue;
        }

        QString formatted = QString("[%1] %2: %3\n")
            .arg(QString::fromStdString(format_timestamp(entry.timestamp)))
            .arg(QString::fromStdString(LogEntry::level_to_string(entry.level)))
            .arg(QString::fromStdString(entry.message));

        log_viewer_->append(formatted);
    }
}

void MainForm::on_settings_changed() {
    config_widget_->save_settings(*config_);
    save_settings();

    // Re-initialize agent with new settings
    setup_agent();

    // Apply UI changes
    apply_theme(config_->ui.theme);
    log_viewer_->setFont(QFont("Consolas", config_->ui.font_size));
}

void MainForm::load_settings() {
    QSettings settings("llm_re", "main_form");

    // Window geometry
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());

    QString plugins_dir = QString::fromUtf8(idadir("plugins"));
    QString default_config = plugins_dir + "/llm_re_config.json";

    std::string config_path = settings.value("config_path", default_config).toString().toStdString();

    msg(("loading from cfg path: " + config_path).c_str());
    config_->load_from_file(config_path);
    msg(("loaded api key " + config_->api.api_key).c_str());
}

void MainForm::save_settings() {
    QSettings settings("llm_re", "main_form");

    // Window geometry
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());

    QString plugins_dir = QString::fromUtf8(idadir("plugins"));
    QString default_config = plugins_dir + "/llm_re_config.json";

    std::string config_path = settings.value("config_path", default_config).toString().toStdString();

    config_->save_to_file(config_path);
}

void MainForm::log(LogEntry::Level level, const std::string& message) {
    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.message = message;
    entry.source = "UI";

    log_entries_.push_back(entry);

    // Apply filter
    int filter_level = log_level_filter_->currentIndex();
    if (filter_level > 0 && entry.level < filter_level - 1) {
        return;
    }

    // Format and append
    QString formatted = QString("[%1] %2: %3")
        .arg(QString::fromStdString(format_timestamp(entry.timestamp)))
        .arg(QString::fromStdString(LogEntry::level_to_string(entry.level)))
        .arg(QString::fromStdString(message));

    // Color based on level
    QTextCursor cursor = log_viewer_->textCursor();
    cursor.movePosition(QTextCursor::End);

    QTextCharFormat format;
    switch (entry.level) {
        case LogEntry::DEBUG:
            format.setForeground(Qt::gray);
            break;
        case LogEntry::INFO:
            format.setForeground(Qt::black);
            break;
        case LogEntry::WARNING:
            format.setForeground(QColor(255, 140, 0));  // Orange
            break;
        case LogEntry::ERROR:
            format.setForeground(Qt::red);
            break;
    }

    cursor.insertText(formatted + "\n", format);

    if (config_->ui.auto_scroll) {
        log_viewer_->ensureCursorVisible();
    }
}

void MainForm::add_message_to_chat(const messages::Message& msg) {
    QListWidgetItem* item = new QListWidgetItem(message_list_);

    // Create custom widget
    QString role_str = (msg.role() == messages::Role::User) ? "You" : "Assistant";
    ui::CollapsibleMessageWidget* msg_widget = new ui::CollapsibleMessageWidget(role_str);

    // Extract text content
    std::string content;
    for (const auto& c : msg.contents()) {
        if (auto text = dynamic_cast<const messages::TextContent*>(c.get())) {
            content += text->text + "\n";
        } else if (auto tool = dynamic_cast<const messages::ToolUseContent*>(c.get())) {
            content += "Tool: " + tool->name + "\n";
            content += "Input: " + tool->input.dump(2) + "\n";
        } else if (auto result = dynamic_cast<const messages::ToolResultContent*>(c.get())) {
            content += "Result: " + result->content + "\n";
        }
    }

    // Create content viewer
    ui::CodeViewer* viewer = new ui::CodeViewer(ui::CodeViewer::Markdown);
    viewer->set_code(QString::fromStdString(content));
    viewer->setMaximumHeight(400);

    msg_widget->set_content(viewer);

    // Style based on role
    if (msg.role() == messages::Role::User) {
        msg_widget->setStyleSheet("background-color: #e3f2fd;");
    } else {
        msg_widget->setStyleSheet("background-color: #f5f5f5;");
    }

    item->setSizeHint(msg_widget->sizeHint());
    message_list_->setItemWidget(item, msg_widget);

    if (config_->ui.auto_scroll) {
        message_list_->scrollToBottom();
    }
}

void MainForm::export_session(const ui::ExportDialog::ExportOptions& options) {
    QString filename = QFileDialog::getSaveFileName(this,
        "Export Session", config_->export_settings.path.c_str(),
        "Markdown (*.md);;HTML (*.html);;JSON (*.json)");

    if (filename.isEmpty()) return;

    try {
        std::ofstream file(filename.toStdString());
        if (!file) {
            throw std::runtime_error("Failed to open file");
        }

        // Build export data
        json export_data;

        if (options.memory) {
            export_data["memory"] = agent_->get_memory()->export_memory_snapshot();
        }

        if (options.logs) {
            export_data["logs"] = json::array();
            for (const auto& entry : log_entries_) {
                export_data["logs"].push_back({
                    {"timestamp", format_timestamp(entry.timestamp)},
                    {"level", LogEntry::level_to_string(entry.level)},
                    {"message", entry.message}
                });
            }
        }

        if (options.statistics && !sessions_.empty()) {
            const auto& session = sessions_.back();
            export_data["statistics"] = {
                {"task", session.task},
                {"duration_ms", session.duration_ms},
                {"tokens", session.token_usage.total()},
                {"tool_calls", session.tool_calls},
                {"success", session.success}
            };
        }

        // Write based on format
        file << export_data.dump(2);

        log(LogEntry::INFO, "Session exported to: " + filename.toStdString());

    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export Error",
            QString("Failed to export: %1").arg(e.what()));
    }
}

void MainForm::apply_theme(int theme_index) {
    QString style;

    switch (theme_index) {
        case 1:  // Dark theme
            style = R"(
                QWidget {
                    background-color: #2b2b2b;
                    color: #ffffff;
                }
                QTextEdit, QLineEdit, QListWidget, QTreeWidget {
                    background-color: #3c3c3c;
                    border: 1px solid #555555;
                }
                QPushButton {
                    background-color: #3c3c3c;
                    border: 1px solid #555555;
                    padding: 5px;
                }
                QPushButton:hover {
                    background-color: #484848;
                }
                QTabWidget::pane {
                    border: 1px solid #555555;
                }
                QTabBar::tab {
                    background-color: #3c3c3c;
                    padding: 5px;
                }
                QTabBar::tab:selected {
                    background-color: #484848;
                }
            )";
            break;

        case 2:  // Light theme
            style = R"(
                QWidget {
                    background-color: #f5f5f5;
                    color: #000000;
                }
                QTextEdit, QLineEdit, QListWidget, QTreeWidget {
                    background-color: #ffffff;
                    border: 1px solid #cccccc;
                }
                QPushButton {
                    background-color: #ffffff;
                    border: 1px solid #cccccc;
                    padding: 5px;
                }
                QPushButton:hover {
                    background-color: #e0e0e0;
                }
            )";
            break;

        default:  // Default theme
            style = "";
            break;
    }

    setStyleSheet(style);
}

std::string MainForm::format_timestamp(const std::chrono::system_clock::time_point& tp) {
    auto time_t = std::chrono::system_clock::to_time_t(tp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
    return ss.str();
}

void MainForm::closeEvent(QCloseEvent* event) {
    if (is_running_) {
        if (QMessageBox::question(this, "Confirm",
                                "A task is currently running. Are you sure you want to close?",
                                QMessageBox::Yes | QMessageBox::No) == QMessageBox::No) {
            event->ignore();
            return;
        }

        on_stop_clicked();
    }

    event->accept();
}

} // namespace llm_re