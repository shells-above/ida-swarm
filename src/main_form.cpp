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
        // Clear any previous error
        agent_->clear_last_error();

        if (resume_mode_) {
            agent_->resume();
        } else if (continue_mode_) {
            agent_->continue_with_task(task_);
        } else {
            agent_->set_task(task_);
        }

        // Wait for completion
        while (agent_->is_running()) {
            if (QThread::currentThread()->isInterruptionRequested()) {
                agent_->stop();
                emit progress("Stopping...");

                int wait_count = 0;
                while (agent_->is_running() && wait_count < 50) {
                    QThread::msleep(100);
                    wait_count++;
                }

                emit error("Task cancelled by user");
                return;
            }

            QThread::msleep(100);
        }

        // Check final status
        if (agent_->is_completed()) {
            emit finished();
        } else if (agent_->is_paused()) {
            // Get the actual error message
            std::string error_msg = agent_->get_last_error();
            if (error_msg.empty()) {
                error_msg = "Task paused due to error";
            }
            emit error(QString::fromStdString(error_msg));
        } else {
            emit error("Task stopped");
        }
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
    if (worker_thread_ && worker_thread_->isRunning()) {
        worker_thread_->requestInterruption();  // if a task is actively running, this will handle stopping it
        worker_thread_->quit();
        worker_thread_->wait();
    }

    // Stop agent internal loop in case the agent was idle
    if (agent_) {
        agent_->stop();
    }

    // Save settings
    save_settings();

    // Close log files
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

    if (g_main_form == this) {
        g_main_form = nullptr;
    }
}

void MainForm::setup_ui() {
    setWindowTitle("LLM Reverse Engineering Agent");
    resize(1200, 800);
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
    memory_dock_ = new QDockWidget("Memory & Analysis", this);
    memory_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    memory_widget_ = new ui::MemoryDockWidget();
    connect(memory_widget_, &ui::MemoryDockWidget::address_clicked,
            this, &MainForm::on_address_clicked);
    connect(memory_widget_, &ui::MemoryDockWidget::continue_requested,
            [this](const QString& instruction) {
                // Use the continue functionality
                if (!agent_->is_completed() && !agent_->is_idle()) {
                    QMessageBox::warning(this, "Cannot Continue",
                        "Please wait for the current analysis to complete.");
                    return;
                }

                // Set the continue input and trigger continue
                continue_input_->setText(instruction);
                on_continue_clicked();
            });

    memory_dock_->setWidget(memory_widget_);
    addDockWidget(Qt::RightDockWidgetArea, memory_dock_);

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
    addDockWidget(Qt::LeftDockWidgetArea, stats_dock_);
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
    agent_ = std::make_unique<REAgent>(*config_);  // isn't actually copying the object, it's giving it a reference to the Config

    // Set callbacks
    agent_->set_log_callback([this](LogLevel level, const std::string& msg) {
        int level_int = static_cast<int>(level);
        QMetaObject::invokeMethod(this, "on_agent_log",
                                 Qt::QueuedConnection,
                                 Q_ARG(int, level_int),
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

    agent_->set_tool_callback([this](const std::string& tool, const json& input, const json& result) {
        QMetaObject::invokeMethod(this, "on_agent_tool_executed",
                                 Qt::QueuedConnection,
                                 Q_ARG(QString, QString::fromStdString(tool)),
                                  Q_ARG(QString, QString::fromStdString(input.dump())),
                                 Q_ARG(QString, QString::fromStdString(result.dump())));
    });

    agent_->set_final_report_callback([this](const std::string& report) {
        log(LogLevel::INFO, "=== FINAL REPORT ===");
        log(LogLevel::INFO, report);
        log(LogLevel::INFO, "===================");

        // Add as message to chat
        messages::Message msg = messages::Message::assistant_text(report);
        QMetaObject::invokeMethod(this, [this, msg]() {
            add_message_to_chat(msg);
        }, Qt::QueuedConnection);
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

void MainForm::set_current_address(ea_t addr) {
    current_address_ = addr;
    status_label_->setText(QString("Current: 0x%1").arg(addr, 0, 16));
    memory_widget_->set_current_focus(addr);
}

void MainForm::on_execute_clicked() {
    if (is_running_) return;

    // Hide continue widget if it's visible
    if (continue_widget_->isVisible()) {
        continue_widget_->setVisible(false);
        task_input_->setVisible(true);
    }

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

    worker_thread_->start();
}

void MainForm::on_stop_clicked() {
    if (!is_running_) return;

    log(LogLevel::WARNING, "Stopping task...");
    agent_->stop();

    if (worker_thread_ && worker_thread_->isRunning()) {
        worker_thread_->requestInterruption();  // when worker_thread_ sees this, it signals to the agent worker_loop to stop
    }
}

void MainForm::on_resume_clicked() {
    if (!agent_->is_paused()) {
        QMessageBox::warning(this, "Warning", "No paused task to resume.");
        return;
    }

    // Update UI
    is_running_ = true;
    update_ui_state();

    log(LogLevel::INFO, "Resuming task...");

    // Create worker thread for resume
    worker_thread_ = new QThread();
    worker_ = new AgentWorker(agent_.get(), "");  // Empty task for resume
    worker_->setResumeMode(true);
    worker_->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::started, worker_, &AgentWorker::process);
    connect(worker_, &AgentWorker::finished, this, &MainForm::on_worker_finished);
    connect(worker_, &AgentWorker::error, this, &MainForm::on_worker_error);
    connect(worker_, &AgentWorker::progress, this, &MainForm::on_worker_progress);
    connect(worker_, &AgentWorker::finished, worker_thread_, &QThread::quit);
    connect(worker_, &AgentWorker::finished, worker_, &QObject::deleteLater);
    connect(worker_thread_, &QThread::finished, worker_thread_, &QObject::deleteLater);

    worker_thread_->start();
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
        "<h3>LLM Reverse Engineering Agent</h3>"
        "<p>Version 1.0.0</p>"
        "<p>An AI-powered reverse engineering agent for IDA Pro.</p>"
        "<p>Uses Claude API to provide intelligent analysis and automation.</p>"
        "<p>Copyright © 2025</p>");
}

void MainForm::on_continue_clicked() {
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

    // Create worker thread for continue
    worker_thread_ = new QThread();
    worker_ = new AgentWorker(agent_.get(), additional_instructions);
    worker_->setContinueMode(true);  // Set continue mode
    worker_->moveToThread(worker_thread_);

    connect(worker_thread_, &QThread::started, worker_, &AgentWorker::process);
    connect(worker_, &AgentWorker::finished, this, &MainForm::on_worker_finished);
    connect(worker_, &AgentWorker::error, this, &MainForm::on_worker_error);
    connect(worker_, &AgentWorker::progress, this, &MainForm::on_worker_progress);
    connect(worker_, &AgentWorker::finished, worker_thread_, &QThread::quit);
    connect(worker_, &AgentWorker::finished, worker_, &QObject::deleteLater);
    connect(worker_thread_, &QThread::finished, worker_thread_, &QObject::deleteLater);

    worker_thread_->start();
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

void MainForm::on_agent_log(int level, const QString& message) {
    LogLevel log_level = static_cast<LogLevel>(level);
    log(log_level, message.toStdString());
}

void MainForm::on_agent_message(const QString& type, const QString& content) {
    // Log the raw message
    try {
        json content_json = json::parse(content.toStdString());
        log_message_to_file(type.toStdString(), content_json);

        // Check for error responses
        if (type == "RESPONSE" && content_json.contains("type") &&
            content_json["type"] == "error") {
            // Log the error details
            if (content_json.contains("error")) {
                json error = content_json["error"];
                std::string error_msg = error.value("message", "Unknown error");
                std::string error_type = error.value("type", "unknown");
                int http_code = content_json.value("_http_code", 0);

                log(LogLevel::ERROR, std::format("API Error (HTTP {}): {} - {}",
                    http_code, error_type, error_msg));
            }
        }
    } catch (...) {
        log_message_to_file(type.toStdString(), content.toStdString());
    }

    // Add timeline event
    ui::SessionTimelineWidget::Event event;
    event.timestamp = std::chrono::steady_clock::now();
    event.type = "message";
    event.description = type.toStdString() + ": " + truncate_string(content.toStdString(), 50);
    timeline_->add_event(event);

    // Update UI based on message type
    if (type == "RESPONSE") {
        try {
            json msg_json = json::parse(content.toStdString());

            // Convert API response to Message and add to chat
            if (msg_json.contains("content")) {
                // Parse the message content
                std::vector<std::unique_ptr<messages::Content>> contents;

                for (const auto& content_item : msg_json["content"]) {
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
        } catch (const std::exception& e) {
            log(LogLevel::ERROR, "Failed to parse message: " + std::string(e.what()));
            // Add more detailed error info for debugging
            log(LogLevel::DEBUG, "Content was: " + content.toStdString());
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
        memory_widget_->update_memory(agent_->get_memory());

    } catch (const std::exception& e) {
        log(LogLevel::ERROR, "Failed to parse tool execution: " + std::string(e.what()));
    }
}

void MainForm::on_agent_state_changed(const QString& state) {
    status_label_->setText(state);
}

void MainForm::on_agent_progress(int iteration, int total_tokens) {
    iteration_label_->setText(QString("Iteration: %1").arg(iteration));
    token_label_->setText(QString("Tokens: %1").arg(total_tokens));
}

void MainForm::on_worker_finished() {
    is_running_ = false;
    update_ui_state();

    // Complete session
    SessionInfo& session = sessions_.back();
    session.end_time = std::chrono::system_clock::now();
    session.token_usage = agent_->get_token_usage();
    session.message_count = message_list_->count();
    session.success = true;
    session.duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - session_start_).count();

    // Update timeline
    timeline_->set_session_info(session.task, session.token_usage);

    // Update statistics
    update_statistics();

    log(LogLevel::INFO, "Task completed successfully");

    // Show continue option
    task_input_->setVisible(false);
    continue_widget_->setVisible(true);
    continue_input_->setFocus();

    worker_thread_ = nullptr;
    worker_ = nullptr;
}

void MainForm::on_worker_error(const QString& error) {
    is_running_ = false;

    // Check if agent is paused (recoverable error)
    bool is_paused = agent_ && agent_->is_paused();

    update_ui_state();

    // Complete session with error
    SessionInfo& session = sessions_.back();
    session.end_time = std::chrono::system_clock::now();
    session.success = false;
    session.error_message = error.toStdString();

    log(LogLevel::ERROR, "Task failed: " + error.toStdString());

    if (is_paused) {
        QMessageBox::information(this, "Task Paused",
            "The task has been paused due to a recoverable error.\n\n"
            "Error: " + error + "\n\n"
            "You can click 'Resume' to continue when the API is available again.");
    } else if (!error.contains("cancelled", Qt::CaseInsensitive)) {
        QMessageBox::critical(this, "Error", QString("Task failed: %1").arg(error));
    }

    worker_thread_ = nullptr;
    worker_ = nullptr;
}

void MainForm::on_worker_progress(const QString& message) {

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

    config_->load_from_file(config_path);
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
    // Create log directory if needed
    std::string log_dir = std::string(get_user_idadir()) + "/llm_re_logs";
    qmkdir(log_dir.c_str(), 0755);

    // Generate timestamp for unique log files
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    char timestamp[32];
    std::strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", std::localtime(&time_t));

    // Open log files
    log_file_path_ = log_dir + "/llm_re_" + timestamp + ".log";
    message_log_file_path_ = log_dir + "/llm_re_messages_" + timestamp + ".jsonl";

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

        log(LogLevel::INFO, "Session exported to: " + filename.toStdString());

    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export Error",
            QString("Failed to export: %1").arg(e.what()));
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

    event->accept();
}

} // namespace llm_re