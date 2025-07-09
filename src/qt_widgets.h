//
// Created by user on 6/30/25.
//

#ifndef QT_WIDGETS_H
#define QT_WIDGETS_H

#include "common.h"
#include "anthropic_api.h"
#include "memory.h"
#include "message_types.h"

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
        std::chrono::system_clock::time_point timestamp;
        LogLevel level;
        std::string message;
        std::string source;

        static std::string level_to_string(LogLevel l) {
            switch (l) {
                case LogLevel::DEBUG: return "DEBUG";
                case LogLevel::INFO: return "INFO";
                case LogLevel::WARNING: return "WARNING";
                case LogLevel::ERROR: return "ERROR";
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
            int max_thinking_tokens = 2048;
            double temperature = 0.0;
        } api;

        struct AgentSettings {
            int max_iterations = 100;
            bool enable_thinking = false;
            bool enable_interleaved_thinking = false;
            bool enable_deep_analysis = false;
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
        } export_settings;

        bool debug_mode = false;

        bool save_to_file(const std::string& path) const;
        bool load_from_file(const std::string& path);
    };
}

namespace llm_re::ui {

// Color scheme for syntax highlighting
struct ColorScheme {
    static constexpr uint32_t KEYWORD = 0x0080FF;      // Orange
    static constexpr uint32_t STRING = 0x00FF00;       // Green
    static constexpr uint32_t NUMBER = 0xFF8000;       // Blue
    static constexpr uint32_t COMMENT = 0x808080;      // Gray
    static constexpr uint32_t ERROR = 0x0000FF;        // Red
    static constexpr uint32_t SUCCESS = 0x00FF00;      // Green
    static constexpr uint32_t WARNING = 0x00AAFF;      // Orange
    static constexpr uint32_t FUNCTION = 0xFF00FF;     // Magenta
    static constexpr uint32_t ADDRESS = 0xFFFF00;      // Cyan
};

// Enhanced message viewer with collapsible sections
class CollapsibleMessageWidget : public QWidget {
    Q_OBJECT

    QVBoxLayout* layout;
    QPushButton* header_button;
    QWidget* content_widget;
    bool collapsed = false;

public:
    CollapsibleMessageWidget(const QString& title, QWidget* parent = nullptr);

    void set_content(QWidget* widget);
    void set_collapsed(bool collapse);
    void toggle_collapsed();

signals:
    void toggled(bool collapsed);

private slots:
    void on_header_clicked();
};

// Syntax highlighted code viewer
class CodeViewer : public QTextEdit {
    Q_OBJECT

public:
    enum Language { C, Assembly, JSON, Markdown };

    CodeViewer(Language lang = C, QWidget* parent = nullptr);

    void set_code(const QString& code);
    void set_language(Language lang);
    void highlight_line(int line, QColor color = Qt::yellow);
    void clear_highlights();

private:
    Language language;
    void apply_syntax_highlighting();
    void highlight_assembly();
    void highlight_json();
    void highlight_markdown();
};

// Tool execution viewer with live updates
class ToolExecutionWidget : public QWidget {
    Q_OBJECT

    QTreeWidget* execution_tree;
    QTextBrowser* result_viewer;
    QProgressBar* progress_bar;
    QLabel* status_label;

public:
    ToolExecutionWidget(QWidget* parent = nullptr);

    void add_tool_call(const std::string& tool_id, const std::string& tool_name, const json& input);
    void update_tool_result(const std::string& tool_id, const json& result);
    void set_progress(int value, const std::string& status);

private slots:
    void on_item_selected();
};

// Session timeline widget
class SessionTimelineWidget : public QWidget {
    Q_OBJECT

public:
    struct Event {
        std::chrono::steady_clock::time_point timestamp;
        std::string type;  // "start", "tool", "message", "error", "complete"
        std::string description;
        json metadata;
    };

    SessionTimelineWidget(QWidget* parent = nullptr);

    void add_event(const Event& event);
    void clear_events();
    void set_session_info(const std::string& task, const api::TokenUsage& usage);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    std::vector<Event> events;
    std::string session_task;
    api::TokenUsage token_usage;
    std::optional<Event> hover_event;

    Event* event_at_point(const QPoint& point);
    void draw_timeline(QPainter& painter);
    void draw_event(QPainter& painter, const Event& event, int x, int y);
};

// Advanced search dialog
class SearchDialog : public QDialog {
    Q_OBJECT

    QLineEdit* search_edit;
    QComboBox* search_type;
    QCheckBox* case_sensitive;
    QCheckBox* regex_search;
    QCheckBox* whole_words;
    QTreeWidget* results_tree;

public:
    enum SearchType {
        Logs,
        Messages,
        Memory,
        All
    };

    struct SearchResult {
        SearchType type;
        std::string context;
        std::string match;
        int line_number;
        json metadata;
    };

    SearchDialog(QWidget* parent = nullptr);

    void set_search_data(const std::vector<LogEntry>& logs,
                        const std::vector<messages::Message>& messages,
                        const json& memory);

signals:
    void result_selected(const SearchResult& result);

private slots:
    void perform_search();
    void on_result_double_clicked(QTreeWidgetItem* item);

private:
    std::vector<LogEntry> search_logs;
    std::vector<messages::Message> search_messages;
    json search_memory;

    std::vector<SearchResult> search_in_logs(const std::string& query);
    std::vector<SearchResult> search_in_messages(const std::string& query);
    std::vector<SearchResult> search_in_memory(const std::string& query);
};

// Export options dialog
class ExportDialog : public QDialog {
    Q_OBJECT

    QCheckBox* include_logs;
    QCheckBox* include_messages;
    QCheckBox* include_memory;
    QCheckBox* include_stats;
    QCheckBox* include_timeline;
    QComboBox* format_combo;
    QLineEdit* template_edit;
    QPushButton* browse_template;

public:
    enum ExportFormat {
        Markdown,
        JSON
    };

    struct ExportOptions {
        bool logs = true;
        bool messages = true;
        bool memory = true;
        bool statistics = true;
        bool timeline = false;
        ExportFormat format = Markdown;
        std::string custom_template;
    };

    ExportDialog(QWidget* parent = nullptr);

    ExportOptions get_options() const;

private slots:
    void on_browse_template();
    void on_format_changed(int index);
};

// Statistics dashboard widget
class StatsDashboard : public QWidget {
    Q_OBJECT

    struct ChartWidget;

    QGridLayout* layout;
    ChartWidget* tool_chart;
    ChartWidget* time_chart;
    QTextBrowser* summary_browser;

public:
    StatsDashboard(QWidget* parent = nullptr);

    void update_stats(const json& agent_state,
                     const std::vector<SessionInfo>& sessions,
                     const json& tool_stats);

private:
    QString generate_summary_html(const json& stats);
    void update_tool_chart(const json& tool_stats);
    void update_time_chart(const std::vector<SessionInfo>& sessions);
};

// Plugin configuration widget
class ConfigWidget : public QWidget {
    Q_OBJECT

    // API settings
    QLineEdit* api_key_edit;
    QPushButton* test_api_button;
    QLabel* api_status_label;

    // Model settings
    QComboBox* model_combo;
    QSpinBox* max_tokens_spin;
    QSpinBox* max_thinking_tokens_spin;
    QSpinBox* max_iterations_spin;
    QDoubleSpinBox* temperature_spin;
    QCheckBox* enable_thinking_check;
    QCheckBox* enable_interleaved_thinking_check;
    QCheckBox* enable_deep_analysis_check;

    // UI settings
    QSpinBox* log_buffer_spin;
    QCheckBox* auto_scroll_check;
    QComboBox* theme_combo;
    QSpinBox* font_size_spin;

    // Export settings
    QLineEdit* export_path_edit;

    // Advanced settings
    QCheckBox* debug_mode_check;

public:
    ConfigWidget(QWidget* parent = nullptr);

    void load_settings(const Config& config);
    void save_settings(Config& config);

signals:
    void settings_changed();

private slots:
    void on_test_api();
    void on_browse_export_path();
    void on_reset_defaults();
};

// Quick task templates
class TaskTemplateWidget : public QWidget {
    Q_OBJECT

    QListWidget* template_list;
    QTextEdit* template_preview;
    QPushButton* use_button;
    QPushButton* edit_button;
    QPushButton* new_button;
    QPushButton* delete_button;

public:
    struct TaskTemplate {
        std::string name;
        std::string description;
        std::string task;
        std::map<std::string, std::string> variables;
    };

    TaskTemplateWidget(QWidget* parent = nullptr);

    void load_templates();
    void save_templates();

signals:
    void template_selected(const TaskTemplate& tmpl);

private slots:
    void on_template_selected();
    void on_use_template();
    void on_edit_template();
    void on_new_template();
    void on_delete_template();

private:
    std::vector<TaskTemplate> templates;
};

class MemoryDockWidget : public QWidget {
    Q_OBJECT

public:
    MemoryDockWidget(QWidget* parent = nullptr);

    void update_memory(std::shared_ptr<BinaryMemory> memory);
    void set_current_address(ea_t address);  // Highlight analyses for current location

signals:
    void address_selected(ea_t address);  // When user clicks on an address in analysis

private:
    QTabWidget* tabs_;

    // Tab 1: Timeline View - See analyses in chronological order
    QTreeWidget* timeline_tree_;
    QTextEdit* timeline_viewer_;
    QComboBox* timeline_filter_;
    QDateEdit* date_from_;
    QDateEdit* date_to_;

    // Tab 2: Function View - See all analyses grouped by function
    QTreeWidget* function_tree_;
    QTextEdit* function_viewer_;
    QLineEdit* function_search_;
    QPushButton* expand_all_;
    QPushButton* collapse_all_;

    // Tab 3: Analysis Browser - Enhanced search and filter
    QLineEdit* search_edit_;
    QComboBox* type_filter_;
    QComboBox* sort_by_;
    QListWidget* analysis_list_;
    QTextEdit* analysis_viewer_;
    QLabel* analysis_info_label_;

    // Tab 4: Relationships - See connections between analyses
    QGraphicsView* relationship_view_;
    QComboBox* relationship_type_;

    // Tab 5: Statistics - Useful metrics
    QTextBrowser* stats_browser_;
    QPushButton* refresh_stats_;

    // Context menu actions
    QAction* copy_action_;
    QAction* export_action_;
    QAction* goto_address_action_;

    std::shared_ptr<BinaryMemory> memory_;
    ea_t current_address_ = BADADDR;

    // Helper methods
    QString format_analysis_preview(const AnalysisEntry& entry, int max_length = 100);
    QString format_timestamp(std::time_t timestamp);
    QString get_function_name(ea_t address);
    QIcon get_type_icon(const std::string& type);
    QColor get_type_color(const std::string& type);

    void populate_timeline_view();
    void populate_function_view();
    void populate_analysis_browser();
    void update_statistics();
    void build_relationship_graph();

    void apply_theme();

private slots:
    void on_timeline_filter_changed();
    void on_timeline_item_selected();
    void on_function_search_changed();
    void on_function_item_selected();
    void on_analysis_search_changed();
    void on_analysis_item_selected();
    void on_sort_changed();
    void on_context_menu(const QPoint& pos);
    void on_copy_analysis();
    void on_export_analysis();
    void on_goto_address();
    void refresh_views();
};



} // namespace llm_re::ui

// Register custom types for Qt
Q_DECLARE_METATYPE(llm_re::ui::SearchDialog::SearchResult)
Q_DECLARE_METATYPE(llm_re::LogLevel)

#endif //QT_WIDGETS_H