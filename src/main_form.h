//
// Created by user on 6/30/25.
//

#ifndef MAIN_FORM_H
#define MAIN_FORM_H

#include "common.h"
#include "agent.h"
#include "memory.h"
#include "actions.h"
#include "tool_system.h"
#include "message_types.h"
#include "anthropic_api.h"
#include "qt_widgets.h"

QT_BEGIN_NAMESPACE
class QAction;
class QMenu;
class QToolBar;
class QStatusBar;
class QSplitter;
class QTabWidget;
class QTextEdit;
class QPushButton;
class QComboBox;
class QListWidget;
class QTreeWidget;
class QProgressBar;
class QLabel;
class QDockWidget;
QT_END_NAMESPACE

namespace llm_re {

// Worker thread for agent execution
class AgentWorker : public QObject {
    Q_OBJECT

public:
    AgentWorker(REAgent* agent, const std::string& task)
        : agent_(agent), task_(task) {}

    void setResumeMode(bool resume) { resume_mode_ = resume; }
    void setContinueMode(bool cont) { continue_mode_ = cont; }

public slots:
    void process();

signals:
    void finished();
    void error(const QString& error);
    void progress(const QString& message);

private:
    REAgent* agent_;
    std::string task_;
    bool resume_mode_ = false;
    bool continue_mode_ = false;
};

// Main window class
class MainForm : public QMainWindow {
    Q_OBJECT

public:
    MainForm(QWidget* parent = nullptr);
    ~MainForm();

    // Public interface for IDA plugin
    void show_and_raise();
    void execute_task(const std::string& task);
    void set_current_address(ea_t addr);

    // helpers for qt_widgets, not great
    const Config* get_config() const { return config_.get(); }
    bool can_continue() const { return agent_ && (agent_->is_completed() || agent_->is_idle()); }
    void log(LogLevel level, const std::string& message);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // UI actions
    void on_execute_clicked();
    void on_stop_clicked();
    void on_resume_clicked();
    void on_clear_clicked();
    void on_export_clicked();
    void on_settings_clicked();
    void on_templates_clicked();
    void on_open_log_dir();
    void on_search_clicked();
    void on_about_clicked();
    void on_continue_clicked();
    void on_new_task_clicked();

    // Agent callbacks
    void on_agent_log(int level, const QString& message);
    void on_agent_message(const QString& type, const QString& content);
    void on_agent_tool_executed(const QString& tool_id, const QString& tool_name, const QString& input, const QString& result);
    void on_agent_state_changed(const QString& state);
    void on_agent_progress(int iteration, int total_tokens);

    // Worker thread
    void on_worker_finished();
    void on_worker_error(const QString& error);
    void on_worker_progress(const QString& message);

    // UI updates
    void on_address_clicked(ea_t addr);
    void on_search_result_selected(const ui::SearchDialog::SearchResult& result);
    void on_template_selected(const ui::TaskTemplateWidget::TaskTemplate& tmpl);
    void update_statistics();
    void update_ui_state();

    // Tab management
    void on_tab_changed(int index);
    void on_log_level_changed(int index);

    // Settings
    void on_settings_changed();
    void load_settings();
    void save_settings();

private:
    // Setup methods
    void setup_ui();
    void setup_menus();
    void setup_toolbars();
    void setup_status_bar();
    void setup_docks();
    void setup_central_widget();
    void setup_agent();
    void connect_signals();

    // Helper methods
    void init_file_logging();
    void log_to_file(LogLevel level, const std::string& message);
    void log_message_to_file(const std::string& type, const json& content);
    void add_message_to_chat(const messages::Message& msg);
    void export_session(const ui::ExportDialog::ExportOptions& options);
    void apply_theme(int theme_index);
    std::string format_timestamp(const std::chrono::system_clock::time_point& tp);

    // Core components
    std::unique_ptr<REAgent> agent_;
    std::unique_ptr<Config> config_;
    QThread* worker_thread_ = nullptr;
    AgentWorker* worker_ = nullptr;

    // UI components - Main
    QTabWidget* main_tabs_;
    QTextEdit* task_input_;
    QPushButton* execute_button_;
    QPushButton* stop_button_;
    QPushButton* resume_button_;
    QPushButton* templates_button_;
    QWidget* continue_widget_;
    QTextEdit* continue_input_;
    QPushButton* continue_button_;
    QPushButton* new_task_button_;

    // Chat view
    QWidget* chat_widget_;
    QListWidget* message_list_;

    // Log view
    QTextEdit* log_viewer_;
    QComboBox* log_level_filter_;
    QPushButton* clear_log_button_;
    std::vector<LogEntry> log_entries_;

    // File logging
    std::ofstream log_file_;
    std::ofstream message_log_file_;
    std::string log_file_path_;
    std::string message_log_file_path_;

    // Memory view
    ui::MemoryDockWidget* memory_widget_;

    // Tools view
    ui::ToolExecutionWidget* tool_execution_;

    // Timeline view
    ui::SessionTimelineWidget* timeline_;

    // Statistics
    ui::StatsDashboard* stats_dashboard_;

    // Dockable windows
    QDockWidget* memory_dock_;
    QDockWidget* tools_dock_;
    QDockWidget* stats_dock_;

    // Dialogs
    ui::SearchDialog* search_dialog_ = nullptr;
    ui::ExportDialog* export_dialog_ = nullptr;
    ui::ConfigWidget* config_widget_ = nullptr;
    ui::TaskTemplateWidget* template_widget_ = nullptr;

    // Status bar components
    QLabel* status_label_;
    QLabel* token_label_;
    QLabel* iteration_label_;
    QProgressBar* status_progress_;

    // State
    ea_t current_address_ = BADADDR;
    std::vector<SessionInfo> sessions_;
    std::chrono::steady_clock::time_point session_start_;
    bool is_running_ = false;
    int current_iteration_ = 0;

    // Actions
    QAction* clear_action_;
    QAction* export_action_;
    QAction* settings_action_;
    QAction* search_action_;
    QAction* about_action_;
    QAction* toggle_memory_action_;
    QAction* toggle_tools_action_;
    QAction* toggle_stats_action_;
};

// Global instance accessor
MainForm* get_main_form();

// Utility functions
inline std::string format_address(ea_t addr) {
    std::stringstream ss;
    ss << "0x" << std::hex << addr;
    return ss.str();
}

inline std::string truncate_string(const std::string& str, size_t max_len) {
    if (str.length() <= max_len) return str;
    return str.substr(0, max_len - 3) + "...";
}

} // namespace llm_re

#endif // MAIN_FORM_H