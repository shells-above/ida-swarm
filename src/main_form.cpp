//
// Created by user on 6/30/25.
//

#include "main_form.h"

namespace llm_re {

// Global instance management - simplified since IDA ensures main thread
static MainForm* g_main_form = nullptr;

MainForm* get_main_form() {
    if (g_main_form && !g_main_form->shutting_down_) {
        return g_main_form;
    }
    return nullptr;
}

void clear_main_form() {
    // Always called from main thread in IDA
    g_main_form = nullptr;
}

// MainForm implementation
MainForm::MainForm(QWidget* parent) : QMainWindow(parent) {
    // Set global instance - safe since constructor runs in main thread
    g_main_form = this;

    // Load configuration
    config_ = std::make_unique<Config>();
    load_settings();

    // Initialize file logging
    init_file_logging();

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
    // Mark as shutting down
    shutting_down_ = true;

    // Clear global instance
    clear_main_form();

    // Cleanup agent
    cleanup_agent();

    // Save settings
    save_settings();

    // Close log files
    close_file_logging();
}

void MainForm::prepare_shutdown() {
    if (shutting_down_) return;

    shutting_down_ = true;

    // Stop any running operations
    if (is_running_) {
        on_stop_clicked();
    }

    // Cleanup agent
    cleanup_agent();
}

void MainForm::cleanup_agent() {
    if (agent_) {
        agent_->stop();
        agent_.reset();
    }
}

void MainForm::close_file_logging() {
    if (log_file_.is_open()) {
        log_file_ << "=== LLM RE Agent Log Ended ===" << std::endl;
        log_file_.close();
    }

    if (message_log_file_.is_open()) {
        json footer = {
            {"type", "session_end"},
            {"timestamp", format_timestamp(std::chrono::system_clock::now())}
        };
        message_log_file_ << footer.dump() << std::endl;
        message_log_file_.close();
    }
}

void MainForm::setup_ui() {
    setWindowTitle("LLM Reverse Engineering Agent");
    resize(1200, 800);

    // Ensure window is properly managed
    // setAttribute(Qt::WA_DeleteOnClose, false);  // We'll manage deletion ourselves
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

    tools_menu->addAction("&Open Log Directory...", this, &MainForm::on_open_log_dir);

    // Help menu
    QMenu* help_menu = menuBar()->addMenu("&Help");

    about_action_ = help_menu->addAction("&About...", this, &MainForm::on_about_clicked);
}

void MainForm::setup_toolbars() {
    QToolBar* main_toolbar = addToolBar("Main");
    main_toolbar->setMovable(false);

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

    iteration_label_ = new QLabel("Iteration: 0");
    statusBar()->addWidget(iteration_label_);

    statusBar()->addWidget(new QLabel(" | "));

    token_label_ = new QLabel("Tokens: 0");
    statusBar()->addWidget(token_label_);

    status_progress_ = new QProgressBar();
    status_progress_->setMaximumWidth(200);
    status_progress_->setVisible(false);
    statusBar()->addPermanentWidget(status_progress_);
}

void MainForm::setup_docks() {
    // Memory dock
    memory_dock_ = new QDockWidget("Memory & Analysis", this);
    memory_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    memory_widget_ = new ui::MemoryDockWidget();
    memory_dock_->setWidget(memory_widget_);
    addDockWidget(Qt::RightDockWidgetArea, memory_dock_);
    connect(toggle_memory_action_, &QAction::triggered, [this]() {
        if (memory_dock_->isVisible()) {
            // If already visible, raise it to front of tab group
            memory_dock_->raise();
        } else {
            // If hidden, show and raise it
            memory_dock_->show();
            memory_dock_->raise();
        }
        toggle_memory_action_->setChecked(true);
    });

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
    connect(toggle_stats_action_, &QAction::triggered, [this]() {
        if (stats_dock_->isVisible()) {
            // If already visible, raise it to front of tab group
            stats_dock_->raise();
        } else {
            // If hidden, show and raise it
            stats_dock_->show();
            stats_dock_->raise();
        }
        toggle_stats_action_->setChecked(true);
    });

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

    // Continue input area (initially hidden)
    continue_widget_ = new QWidget();
    QVBoxLayout* continue_layout = new QVBoxLayout(continue_widget_);

    QLabel* continue_label = new QLabel("Continue with additional instructions:");
    continue_layout->addWidget(continue_label);

    continue_input_ = new QTextEdit();
    continue_input_->setMaximumHeight(80);
    continue_input_->setPlaceholderText("Enter additional instructions to continue the analysis...");
    continue_layout->addWidget(continue_input_);

    QHBoxLayout* continue_button_layout = new QHBoxLayout();

    continue_button_ = new QPushButton("Continue");
    continue_button_->setDefault(true);
    connect(continue_button_, &QPushButton::clicked, this, &MainForm::on_continue_clicked);
    continue_button_layout->addWidget(continue_button_);

    new_task_button_ = new QPushButton("Start New Task");
    connect(new_task_button_, &QPushButton::clicked, this, &MainForm::on_new_task_clicked);
    continue_button_layout->addWidget(new_task_button_);

    continue_button_layout->addStretch();
    continue_layout->addLayout(continue_button_layout);

    continue_widget_->setVisible(false);  // Hidden by default
    layout->addWidget(continue_widget_);

    QHBoxLayout* button_layout = new QHBoxLayout();

    execute_button_ = new QPushButton("Execute");
    execute_button_->setDefault(true);
    execute_button_->setShortcut(QKeySequence("Ctrl+Return"));
    connect(execute_button_, &QPushButton::clicked, this, &MainForm::on_execute_clicked);
    button_layout->addWidget(execute_button_);

    stop_button_ = new QPushButton("Stop");
    stop_button_->setEnabled(false);
    connect(stop_button_, &QPushButton::clicked, this, &MainForm::on_stop_clicked);
    button_layout->addWidget(stop_button_);

    resume_button_ = new QPushButton("Resume");
    resume_button_->setEnabled(false);
    connect(resume_button_, &QPushButton::clicked, this, &MainForm::on_resume_clicked);
    button_layout->addWidget(resume_button_);

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

    // Set single callback
    agent_->set_message_callback([this](AgentMessageType type, const json& data) {
        if (shutting_down_) return;

        // Convert JSON to QString for Qt's signal/slot system
        QString data_str = QString::fromStdString(data.dump());

        // Use Qt's thread-safe invocation
        QMetaObject::invokeMethod(this, "on_agent_message",
                                 Qt::QueuedConnection,
                                 Q_ARG(int, static_cast<int>(type)),
                                 Q_ARG(QString, data_str));
    });


    agent_->start();
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

void MainForm::on_execute_clicked() {
    if (is_running_ || shutting_down_) return;

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

    // Add timeline event
    ui::SessionTimelineWidget::Event start_event;
    start_event.timestamp = std::chrono::steady_clock::now();
    start_event.type = "start";
    start_event.description = "Task started";
    timeline_->add_event(start_event);

    // Log
    log(LogLevel::INFO, "Starting task: " + task);

    // Add initial user message to chat
    messages::Message user_msg = messages::Message::user_text(task);
    add_message_to_chat(user_msg);

    // Just set the task - agent will notify us when done
    agent_->set_task(task);
}

void MainForm::on_stop_clicked() {
    if (!is_running_ || shutting_down_) return;

    log(LogLevel::WARNING, "Stopping task...");

    if (agent_) {
        agent_->stop();
    }
}

void MainForm::on_resume_clicked() {
    if (shutting_down_) return;

    if (!agent_->is_paused()) {
        QMessageBox::warning(this, "Warning", "No paused task to resume.");
        return;
    }

    is_running_ = true;
    update_ui_state();

    log(LogLevel::INFO, "Resuming task...");
    agent_->resume();  // agent will notify when done
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
    memory_widget_->update_memory(nullptr);  // clear memory view

    log(LogLevel::INFO, "Cleared all data");
}

void MainForm::on_export_clicked() {
    // Create fresh dialog each time (MODAL - blocks interaction)
    ui::ExportDialog* export_dialog = new ui::ExportDialog(this);

    if (export_dialog->exec() == QDialog::Accepted) {
        export_session(export_dialog->get_options());
    }

    delete export_dialog;  // Clean up after modal dialog
}

    void MainForm::on_settings_clicked() {
    // Create fresh ConfigWidget each time (safer approach)
    ui::ConfigWidget* config_widget = new ui::ConfigWidget();
    connect(config_widget, &ui::ConfigWidget::settings_changed,
            this, &MainForm::on_settings_changed);

    config_widget->load_settings(*config_);

    // Create modal dialog on heap
    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle("Settings");
    dialog->setModal(true);
    dialog->resize(600, 500);

    QVBoxLayout* layout = new QVBoxLayout(dialog);
    layout->addWidget(config_widget);

    QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, [this, config_widget, dialog]() {
        // Save settings before closing
        save_settings(*config_widget);
        dialog->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);

    dialog->exec();  // Modal - blocks until user clicks OK or Cancel
    delete dialog;   // Clean up after modal exec()
}

void MainForm::on_templates_clicked() {
    // Create dialog on heap for modal execution
    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle("Task Templates");
    dialog->setModal(true);
    dialog->resize(700, 500);

    // Create a new template widget each time (safest approach)
    ui::TaskTemplateWidget* template_widget = new ui::TaskTemplateWidget();

    // Connect and close dialog when template is selected
    connect(template_widget, &ui::TaskTemplateWidget::template_selected,
            [this, dialog](const ui::TaskTemplateWidget::TaskTemplate& tmpl) {
                on_template_selected(tmpl);
                dialog->accept();  // Close the dialog
            });

    QVBoxLayout* layout = new QVBoxLayout(dialog);
    layout->addWidget(template_widget);

    dialog->exec();  // Modal dialog - blocks until closed
    delete dialog;   // Clean up after modal exec()
}

void MainForm::on_open_log_dir() {
    std::string log_dir = std::string(get_user_idadir()) + "/llm_re_logs";

    // Open in system file explorer
#ifdef _WIN32
    system(("explorer \"" + log_dir + "\"").c_str());
#elif __APPLE__
    system(("open \"" + log_dir + "\"").c_str());
#else
    system(("xdg-open \"" + log_dir + "\"").c_str());
#endif
}

void MainForm::on_search_clicked() {
    // Create fresh dialog each time (NON-MODAL - doesn't block)
    ui::SearchDialog* search_dialog = new ui::SearchDialog(this);
    search_dialog->setAttribute(Qt::WA_DeleteOnClose);  // Auto-delete when closed

    connect(search_dialog, &ui::SearchDialog::result_selected,
            this, &MainForm::on_search_result_selected);

    // TODO: Update search data
    search_dialog->show();  // Non-modal - user can interact with main window
    search_dialog->raise();
}

void MainForm::on_about_clicked() {
    QMessageBox::about(this, "About",
        "<h3>LLM Reverse Engineering Agent</h3>"
        "<p>Version 1.0.0</p>"
        "<p>An AI-powered reverse engineering agent for IDA Pro.</p>"
        "<p>Uses Claude API to provide intelligent analysis and automation.</p>"
        "<p>Copyright © 2025</p>");
}

void MainForm::on_continue_clicked() {
    if (shutting_down_) return;

    std::string additional_instructions = continue_input_->toPlainText().toStdString();
    if (additional_instructions.empty()) {
        QMessageBox::warning(this, "Warning", "Please enter additional instructions to continue.");
        return;
    }

    // Hide continue widget
    continue_widget_->setVisible(false);
    task_input_->setVisible(true);

    // Add user message to chat
    messages::Message user_msg = messages::Message::user_text(additional_instructions);
    add_message_to_chat(user_msg);

    // Clear continue input
    continue_input_->clear();

    // Update UI
    is_running_ = true;
    update_ui_state();

    log(LogLevel::INFO, "Continuing with: " + additional_instructions);

    agent_->continue_with_task(additional_instructions);  // agent will notify when done
}

void MainForm::on_new_task_clicked() {
    // Hide continue widget and show normal task input
    continue_widget_->setVisible(false);
    task_input_->setVisible(true);
    task_input_->clear();
    task_input_->setFocus();

    // Clear conversation
    message_list_->clear();

    log(LogLevel::INFO, "Ready for new task");
}

void MainForm::on_agent_message(int message_type, const QString& data_str) {
    if (shutting_down_) return;

    // Parse the JSON string back
    json data;
    try {
        data = json::parse(data_str.toStdString());
    } catch (const std::exception& e) {
        log(LogLevel::ERROR, "Failed to parse agent message: " + std::string(e.what()));
        return;
    }

    AgentMessageType type = static_cast<AgentMessageType>(message_type);

    switch (type) {
        case AgentMessageType::Log:
            handle_log_message(data);
            break;
        case AgentMessageType::ApiMessage:
            handle_api_message(data);
            break;
        case AgentMessageType::StateChanged:
            handle_state_changed(data);
            break;
        case AgentMessageType::ToolStarted:
            handle_tool_started(data);
            break;
        case AgentMessageType::ToolExecuted:
            handle_tool_executed(data);
            break;
        case AgentMessageType::FinalReport:
            handle_final_report(data);
            break;
    }
}

void MainForm::handle_log_message(const json& data) {
    LogLevel level = static_cast<LogLevel>(data["level"].get<int>());
    std::string message = data["message"];
    log(level, message);
}

void MainForm::handle_api_message(const json& data) {
    std::string type = data["type"];
    json content = data["content"];
    int iteration = data["iteration"];

    current_iteration_ = iteration;

    // Log the raw message
    log_message_to_file(type, content);

    // Check for error responses
    if (type == "RESPONSE" && content.contains("type") &&
        content["type"] == "error") {
        // Log the error details
        if (content.contains("error")) {
            json error = content["error"];
            std::string error_msg = error.value("message", "Unknown error");
            std::string error_type = error.value("type", "unknown");
            int http_code = content.value("_http_code", 0);

            log(LogLevel::ERROR, std::format("API Error (HTTP {}): {} - {}",
                http_code, error_type, error_msg));
        }
    }

    // Add timeline event
    ui::SessionTimelineWidget::Event event;
    event.timestamp = std::chrono::steady_clock::now();
    event.type = "message";
    event.description = type + ": " + truncate_string(content.dump(), 50);
    timeline_->add_event(event);

    // Update UI based on message type
    if (type == "RESPONSE") {
        try {
            // Convert API response to Message and add to chat
            if (content.contains("content")) {
                // Parse the message content
                std::vector<std::unique_ptr<messages::Content>> contents;

                for (const auto& content_item : content["content"]) {
                    if (content_item.contains("type")) {
                        std::string content_type = content_item["type"];

                        if (content_type == "text" && content_item.contains("text")) {
                            contents.push_back(std::make_unique<messages::TextContent>(
                                content_item["text"].get<std::string>()
                            ));
                        } else if (content_type == "tool_use") {
                            auto tool_use = std::make_unique<messages::ToolUseContent>(
                                content_item["id"].get<std::string>(),
                                content_item["name"].get<std::string>(),
                                content_item["input"]
                            );
                            contents.push_back(std::move(tool_use));
                        }
                    }
                }

                if (!contents.empty()) {
                    messages::Message assistant_msg(messages::Role::Assistant);
                    for (auto& content_obj : contents) {
                        assistant_msg.add_content(std::move(content_obj));
                    }
                    add_message_to_chat(assistant_msg);
                }
            }

            // Handle stop reason
            if (content.contains("stop_reason")) {
                std::string stop_reason = content["stop_reason"];
                status_label_->setText(QString::fromStdString(stop_reason));
            }

            // Handle usage stats
            if (content.contains("usage")) {
                json usage = content["usage"];
                int input = usage.value("input_tokens", 0);
                int output = usage.value("output_tokens", 0);
                int cache_read = usage.value("cache_read_input_tokens", 0);
                int cache_write = usage.value("cache_creation_input_tokens", 0);
                int total = input + output + cache_read + cache_write;

                token_label_->setText(QString("Tokens: %1 (%2 in, %3 out, %4 cache read, %5 cache write)")
                    .arg(total).arg(input).arg(output).arg(cache_read).arg(cache_write));
            }
        } catch (const std::exception& e) {
            log(LogLevel::ERROR, "Failed to parse message: " + std::string(e.what()));
            log(LogLevel::DEBUG, "Content was: " + content.dump());
        }
    }

    // Update iteration
    iteration_label_->setText(QString("Iteration: %1").arg(current_iteration_));
}

void MainForm::handle_state_changed(const json& data) {
    AgentState::Status status = static_cast<AgentState::Status>(data["status"].get<int>());

    switch (status) {
        case AgentState::Status::Completed: {
            is_running_ = false;
            update_ui_state();

            // Complete session
            SessionInfo& session = sessions_.back();
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

            log(LogLevel::INFO, "Task completed successfully");

            // Show continue option
            task_input_->setVisible(false);
            task_input_->clear();
            continue_widget_->setVisible(true);
            continue_input_->setFocus();
            break;
        }

        case AgentState::Status::Paused: {
            is_running_ = false;
            update_ui_state();

            std::string error_msg = agent_->get_last_error();
            if (error_msg.empty()) {
                error_msg = "Task paused due to error";
            }

            log(LogLevel::ERROR, "Task paused: " + error_msg);

            QMessageBox::information(this, "Task Paused",
                "The task has been paused due to a recoverable error.\n\n"
                "You can click 'Resume' to continue when the API is available again.");
            break;
        }

        case AgentState::Status::Idle: {
            // Check if this is due to an error
            if (is_running_) {
                is_running_ = false;
                update_ui_state();

                // Check why it stopped
                if (agent_ && !agent_->is_completed() && !agent_->is_paused()) {
                    // It stopped due to an error
                    std::string error_msg = agent_->get_last_error();

                    if (!error_msg.empty()) {
                        // Complete session with error
                        if (!sessions_.empty()) {
                            SessionInfo& session = sessions_.back();
                            session.end_time = std::chrono::system_clock::now();
                            session.success = false;
                            session.error_message = error_msg;
                        }

                        log(LogLevel::ERROR, "Task failed: " + error_msg);

                        // Show error dialog unless it was cancelled
                        if (error_msg.find("cancelled") == std::string::npos) {
                            QMessageBox::critical(this, "Error",
                                QString("Task failed: %1").arg(QString::fromStdString(error_msg)));
                        }
                    } else {
                        log(LogLevel::WARNING, "Task stopped");
                    }
                } else {
                    log(LogLevel::WARNING, "Task stopped");
                }
            }
            break;
        }

        case AgentState::Status::Running: {
            // Already handled by UI
            break;
        }
    }
}

void MainForm::handle_tool_started(const json& data) {
    std::string tool_id = data["tool_id"];
    std::string tool_name = data["tool_name"];
    json input = data["input"];

    // Add tool call to UI with "Running..." status
    tool_execution_->add_tool_call(tool_id, tool_name, input);
}

void MainForm::handle_tool_executed(const json& data) {
    std::string tool_id = data["tool_id"];
    std::string tool_name = data["tool_name"];
    json input = data["input"];
    json result = data["result"];

    // Update the existing tool call with the result
    tool_execution_->update_tool_result(tool_id, result);

    // Add timeline event
    ui::SessionTimelineWidget::Event event;
    event.timestamp = std::chrono::steady_clock::now();
    event.type = "tool";
    event.description = "Executed: " + tool_name;
    event.metadata["tool"] = tool_name;
    event.metadata["success"] = result.value("success", false);
    timeline_->add_event(event);

    // Update memory view if needed
    if (agent_) {
        memory_widget_->update_memory(agent_->get_memory());
    }
}

void MainForm::handle_final_report(const json& data) {
    std::string report = data["report"];

    // Add as message to chat
    messages::Message msg = messages::Message::assistant_text(report);
    add_message_to_chat(msg);
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

    // Make sure we're showing the correct widget
    if (continue_widget_->isVisible()) {
        // If in continue mode, put it in continue_input
        continue_input_->setText(QString::fromStdString(task));
        continue_input_->setFocus();
    } else {
        // Otherwise put it in task_input
        task_input_->setText(QString::fromStdString(task));
        task_input_->setFocus();
    }
}
void MainForm::update_statistics() {
    json tool_stats;
    // TODO: Collect tool usage statistics

    if (agent_) {
        stats_dashboard_->update_stats(agent_->get_state_json(), sessions_, tool_stats);
    }
}

void MainForm::update_ui_state() {
    bool is_paused = agent_ && agent_->is_paused();
    bool is_completed = agent_ && agent_->is_completed();

    execute_button_->setEnabled(!is_running_ && !is_paused);
    stop_button_->setEnabled(is_running_);
    resume_button_->setEnabled(is_paused && !is_running_);

    // Only disable task input if running or paused (not if completed)
    task_input_->setReadOnly(is_running_ || is_paused);

    if (is_running_) {
        status_progress_->setVisible(true);
        status_progress_->setMaximum(0);
    } else {
        status_progress_->setVisible(false);
    }

    // Update status label
    if (is_paused) {
        status_label_->setText("Paused - Click Resume to continue");
    } else if (is_completed && continue_widget_->isVisible()) {
        status_label_->setText("Completed - Enter additional instructions or start a new task");
    }
}

void MainForm::on_tab_changed(int index) {
    // Update focus based on tab
}

void MainForm::on_log_level_changed(int index) {
    // Re-filter logs
    log_viewer_->clear();

    for (const LogEntry& entry: log_entries_) {
        if (index > 0) {
            LogLevel min_level = static_cast<LogLevel>(index - 1);
            if (entry.level < min_level) {
                continue;
            }
        }

        // Use the same formatting as log() method
        QString formatted = QString("[%1] %2: %3")
            .arg(QString::fromStdString(format_timestamp(entry.timestamp)))
            .arg(QString::fromStdString(LogEntry::level_to_string(entry.level)))
            .arg(QString::fromStdString(entry.message));

        // Apply color based on level
        QTextCursor cursor = log_viewer_->textCursor();
        cursor.movePosition(QTextCursor::End);

        QTextCharFormat format;

        // Set base text color for theme
        if (config_->ui.theme == 0 || config_->ui.theme == 1) {  // Dark themes
            format.setForeground(Qt::white);  // Base text is white
        } else {
            format.setForeground(Qt::black);  // Base text is black
        }

        // Then apply level-specific colors
        switch (entry.level) {
            case LogLevel::DEBUG:
                format.setForeground(Qt::gray);
                break;
            case LogLevel::INFO:
                // Keep the base color (white/black)
                break;
            case LogLevel::WARNING:
                format.setForeground(QColor(255, 140, 0));  // Orange
                break;
            case LogLevel::ERROR:
                format.setForeground(Qt::red);
                break;
        }

        cursor.insertText(formatted + "\n", format);
    }

    if (config_->ui.auto_scroll) {
        log_viewer_->ensureCursorVisible();
    }
}

void MainForm::on_settings_changed() {
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

    config_->load_from_file(config_path);
}

void MainForm::save_settings(ui::ConfigWidget& config_widget) {
    config_widget.save_settings(*config_);
    on_settings_changed();
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

void MainForm::init_file_logging() {
    qstring filename = get_path(PATH_TYPE_IDB);
    const char* basename = qbasename(filename.c_str());
    std::string str(basename);
    size_t lastdot = str.find_last_of('.');
    if (lastdot != std::string::npos) {
        str = str.substr(0, lastdot);
    }

    std::string base_log_dir = std::string(get_user_idadir()) + "/llm_re_logs";
    std::string specific_log_dir = base_log_dir + "/" + str;
    qmkdir(base_log_dir.c_str(), 0755);
    qmkdir(specific_log_dir.c_str(), 0755);

    // Generate timestamp for unique log files
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", std::localtime(&time_t));

    // Open log files
    log_file_path_ = specific_log_dir + "/llm_re_" + timestamp + ".log";
    message_log_file_path_ = specific_log_dir + "/llm_re_messages_" + timestamp + ".jsonl";

    log_file_.open(log_file_path_, std::ios::app);
    message_log_file_.open(message_log_file_path_, std::ios::app);

    if (!log_file_.is_open()) {
        msg("Failed to open log file: %s\n", log_file_path_.c_str());
    } else {
        log_file_ << "=== LLM RE Agent Log Started at " << timestamp << " ===" << std::endl;
    }

    if (!message_log_file_.is_open()) {
        msg("Failed to open message log file: %s\n", message_log_file_path_.c_str());
    } else {
        // Write header for JSONL file
        char inBuffer[1024];
        size_t size = get_input_file_path(inBuffer, sizeof(inBuffer));
        std::string input_file = std::string(inBuffer, size);

        json header = {
            {"type", "session_start"},
            {"timestamp", timestamp},
            {"ida_database", input_file}
        };
        message_log_file_ << header.dump() << std::endl;
    }
}

void MainForm::log_to_file(LogLevel level, const std::string& message) {
    if (!log_file_.is_open()) return;

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t));

    log_file_ << "[" << timestamp << "] "
              << "[" << LogEntry::level_to_string(level) << "] "
              << message << std::endl;
    log_file_.flush();  // Ensure it's written immediately
}

void MainForm::log_message_to_file(const std::string& type, const json& content) {
    if (!message_log_file_.is_open()) return;

    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", std::localtime(&time_t));

    json log_entry = {
        {"timestamp", timestamp},
        {"type", type},
        {"iteration", current_iteration_},
        {"content", content}
    };

    message_log_file_ << log_entry.dump() << std::endl;
    message_log_file_.flush();  // Ensure it's written immediately
}

void MainForm::log(LogLevel level, const std::string& message) {
    if (shutting_down_) return;

    LogEntry entry;
    entry.timestamp = std::chrono::system_clock::now();
    entry.level = level;
    entry.message = message;
    entry.source = "UI";

    log_entries_.push_back(entry);

    // Apply filter
    int filter_level = log_level_filter_->currentIndex();
    if (filter_level > 0 && (int)entry.level < filter_level - 1) {
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

    // Set base text color for theme
    if (config_->ui.theme == 0 || config_->ui.theme == 1) {  // Dark themes
        format.setForeground(Qt::white);
    } else {
        format.setForeground(Qt::black);
    }

    switch (entry.level) {
        case LogLevel::DEBUG:
            format.setForeground(Qt::gray);
            break;
        case LogLevel::INFO:
            // Keep base color
            break;
        case LogLevel::WARNING:
            format.setForeground(QColor(255, 140, 0));  // Orange
            break;
        case LogLevel::ERROR:
            format.setForeground(Qt::red);
            break;
    }

    cursor.insertText(formatted + "\n", format);

    if (config_->ui.auto_scroll) {
        log_viewer_->ensureCursorVisible();
    }

    log_to_file(level, message);
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
    if (config_->ui.theme == 0 || config_->ui.theme == 1) {  // Dark themes
        if (msg.role() == messages::Role::User) {
            msg_widget->setStyleSheet("background-color: #1e3a5f;");  // Dark blue
        } else {
            msg_widget->setStyleSheet("background-color: #3c3c3c;");  // Dark gray
        }
    } else {  // Light theme
        if (msg.role() == messages::Role::User) {
            msg_widget->setStyleSheet("background-color: #e3f2fd;");
        } else {
            msg_widget->setStyleSheet("background-color: #f5f5f5;");
        }
    }

    item->setSizeHint(msg_widget->sizeHint());
    message_list_->setItemWidget(item, msg_widget);

    if (config_->ui.auto_scroll) {
        message_list_->scrollToBottom();
    }
}

void MainForm::export_session(const ui::ExportDialog::ExportOptions& options) {
    QString filter;
    QString extension;
    
    // Set file filter based on selected format
    if (options.format == ui::ExportDialog::Markdown) {
        filter = "Markdown (*.md)";
        extension = ".md";
    } else {
        filter = "JSON (*.json)";
        extension = ".json";
    }

    QString filename = QFileDialog::getSaveFileName(this, "Export Session", config_->export_settings.path.c_str(), filter);

    if (filename.isEmpty()) return;

    try {
        std::ofstream file(filename.toStdString());
        if (!file) {
            throw std::runtime_error("Failed to open file");
        }

        // Build export data
        json export_data;

        // Add messages if requested
        if (options.messages && agent_) {
            export_data["messages"] = json::array();
            std::vector<messages::Message> messages = agent_->get_conversation().get_messages();
            for (const messages::Message& msg: messages) {
                json msg_json;
                msg_json["role"] = messages::role_to_string(msg.role());
                msg_json["content"] = json::array();
                
                for (const std::unique_ptr<messages::Content>& content: msg.contents()) {
                    msg_json["content"].push_back(content->to_json());
                }
                export_data["messages"].push_back(msg_json);
            }
        }

        if (options.memory && agent_) {
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
                {"tool_calls", session.tool_calls},
                {"success", session.success}
            };
        }

        // Write based on format
        if (options.format == ui::ExportDialog::Markdown) {
            write_markdown_export(file, export_data, options);
        } else {
            file << export_data.dump(2);
        }

        log(LogLevel::INFO, "Session exported to: " + filename.toStdString());

    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export Error",
            QString("Failed to export: %1").arg(e.what()));
    }
}

void MainForm::write_markdown_export(std::ofstream& file, const json& export_data, const ui::ExportDialog::ExportOptions& options) {
    file << "# Session Export\n\n";
    file << "Generated: " << format_timestamp(std::chrono::system_clock::now()) << "\n\n";

    // Export messages
    if (options.messages && export_data.contains("messages")) {
        file << "## Conversation\n\n";
        for (const auto& msg : export_data["messages"]) {
            std::string role = msg["role"];
            std::string timestamp = msg["timestamp"];
            
            file << "### " << role << " (" << timestamp << ")\n\n";
            
            for (const auto& content : msg["content"]) {
                if (content["type"] == "text") {
                    file << content["text"] << "\n\n";
                } else if (content["type"] == "tool_use") {
                    file << "**Tool Use:** " << content["name"] << "\n";
                    file << "```json\n" << content["input"].dump(2) << "\n```\n\n";
                } else if (content["type"] == "tool_result") {
                    file << "**Tool Result:**\n";
                    file << "```\n" << content["content"] << "\n```\n\n";
                } else if (content["type"] == "thinking") {
                    file << "**Thinking:**\n";
                    file << content["content"] << "\n\n";
                }
            }
        }
    }

    // Export memory
    if (options.memory && export_data.contains("memory")) {
        file << "## Memory Snapshot\n\n";
        file << "```json\n" << export_data["memory"].dump(2) << "\n```\n\n";
    }

    // Export statistics
    if (options.statistics && export_data.contains("statistics")) {
        file << "## Statistics\n\n";
        const auto& stats = export_data["statistics"];
        file << "- **Task:** " << stats["task"] << "\n";
        file << "- **Duration:** " << stats["duration_ms"] << " ms\n";
        file << "- **Tool Calls:** " << stats["tool_calls"] << "\n";
        file << "- **Success:** " << (stats["success"] ? "Yes" : "No") << "\n\n";
    }

    // Export logs
    if (options.logs && export_data.contains("logs")) {
        file << "## Logs\n\n";
        for (const auto& entry : export_data["logs"]) {
            file << "**" << entry["timestamp"] << "** [" << entry["level"] << "] " << entry["message"] << "\n";
        }
    }
}

void MainForm::apply_theme(int theme_index) {
    QString style;

    switch (theme_index) {
        case 0:  // Default theme (should be dark for IDA Pro)
        case 1:  // Dark theme
            style = R"(
                QWidget {
                    background-color: #2b2b2b;
                    color: #ffffff;
                }
                QTextEdit, QLineEdit, QListWidget, QTreeWidget, QTextBrowser {
                    background-color: #3c3c3c;
                    border: 1px solid #555555;
                    color: #ffffff;
                }
                QPushButton {
                    background-color: #3c3c3c;
                    border: 1px solid #555555;
                    padding: 5px;
                    color: #ffffff;
                }
                QPushButton:hover {
                    background-color: #484848;
                }
                QTabWidget::pane {
                    border: 1px solid #555555;
                    background-color: #2b2b2b;
                }
                QTabBar::tab {
                    background-color: #3c3c3c;
                    padding: 5px;
                    color: #ffffff;
                }
                QTabBar::tab:selected {
                    background-color: #484848;
                }
                QComboBox {
                    background-color: #3c3c3c;
                    border: 1px solid #555555;
                    color: #ffffff;
                }
                QComboBox QAbstractItemView {
                    background-color: #3c3c3c;
                    color: #ffffff;
                    selection-background-color: #484848;
                }
                QProgressBar {
                    background-color: #3c3c3c;
                    border: 1px solid #555555;
                    text-align: center;
                    color: #ffffff;
                }
                QProgressBar::chunk {
                    background-color: #5a5a5a;
                }
            )";
            break;

        case 2:  // Light theme
            style = R"(
                QWidget {
                    background-color: #f5f5f5;
                    color: #000000;
                }
                QTextEdit, QLineEdit, QListWidget, QTreeWidget, QTextBrowser {
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

    // Mark as shutting down before accepting
    prepare_shutdown();

    event->accept();
}

} // namespace llm_re