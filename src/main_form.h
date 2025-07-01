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

#include <QMainWindow>
#include <QThread>
#include <QTimer>
#include <chrono>

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

// Session information
struct SessionInfo {
    std::string id;
    std::string task;
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point end_time;
    api::TokenUsage token_usage;
    int tool_calls = 0;
    int message_count = 0;
    bool success = true;
    std::string error_message;
    long duration_ms = 0;
};

// Log entry
struct LogEntry {
    enum Level { DEBUG, INFO, WARNING, ERROR };

    std::chrono::system_clock::time_point timestamp;
    Level level;
    std::string message;
    std::string source;

    static std::string level_to_string(Level l) {
        switch (l) {
            case DEBUG: return "DEBUG";
            case INFO: return "INFO";
            case WARNING: return "WARNING";
            case ERROR: return "ERROR";
        }
        return "UNKNOWN";
    }
};

// Configuration
struct Config {
    struct APISettings {
        std::string api_key;
        std::string base_url = "https://api.anthropic.com/v1/messages";
        api::Model model = api::Model::Sonnet4;
        int max_tokens = 8192;
        double temperature = 0.0;
        bool enable_prompt_caching = true;
        int timeout_seconds = 300;
    } api;

    struct AgentSettings {
        int max_iterations = 100;
        bool enable_thinking = false;
        std::string custom_prompt;
        int tool_timeout = 30;
        bool verbose_logging = false;
    } agent;

    struct UISettings {
        int log_buffer_size = 1000;
        bool auto_scroll = true;
        int theme = 0;  // 0=default, 1=dark, 2=light
        int font_size = 10;
        bool show_timestamps = true;
        bool show_tool_details = true;
    } ui;

    struct ExportSettings {
        std::string path = ".";
        bool auto_export = false;
        int format = 0;  // 0=markdown, 1=html, 2=json
        bool include_memory = true;
        bool include_logs = true;
    } export_settings;

    bool debug_mode = false;

    bool save_to_file(const std::string& path) const;
    bool load_from_file(const std::string& path);
};

// Worker thread for agent execution
class AgentWorker : public QObject {
    Q_OBJECT

public:
    AgentWorker(REAgent* agent, const std::string& task)
        : agent_(agent), task_(task) {}

public slots:
    void process();

signals:
    void finished();
    void error(const QString& error);
    void progress(const QString& message);

private:
    REAgent* agent_;
    std::string task_;
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

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    // UI actions
    void on_execute_clicked();
    void on_stop_clicked();
    void on_clear_clicked();
    void on_export_clicked();
    void on_settings_clicked();
    void on_templates_clicked();
    void on_search_clicked();
    void on_about_clicked();

    // Agent callbacks
    void on_agent_log(const QString& message);
    void on_agent_message(const QString& type, const QString& content);
    void on_agent_tool_executed(const QString& tool, const QString& input, const QString& result);
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
    void update_memory_view();

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
    void log(LogEntry::Level level, const std::string& message);
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
    QPushButton* templates_button_;

    // Chat view
    QWidget* chat_widget_;
    QListWidget* message_list_;
    ui::ProgressOverlay* progress_overlay_;

    // Log view
    QTextEdit* log_viewer_;
    QComboBox* log_level_filter_;
    QPushButton* clear_log_button_;
    std::vector<LogEntry> log_entries_;

    // Memory view
    ui::MemoryMapWidget* memory_map_;
    QTreeWidget* memory_tree_;

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
    QAction* execute_action_;
    QAction* stop_action_;
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