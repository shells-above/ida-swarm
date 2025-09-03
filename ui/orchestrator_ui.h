#pragma once

#include "ui_common.h"
#include <QSplitter>

namespace llm_re::ui {

// Forward declarations
class TaskPanel;
class AgentMonitor;
class IRCViewer;
class ToolCallTracker;
class MetricsPanel;

// Main UI window that observes the EventBus
class OrchestratorUI : public QMainWindow {
    Q_OBJECT

public:
    explicit OrchestratorUI(QWidget* parent = nullptr);
    ~OrchestratorUI();

    // Show the UI (called from plugin)
    void show_ui();

signals:
    // Internal signals for thread-safe UI updates
    void event_received(const AgentEvent& event);

private slots:
    // Handle events from EventBus (via signal)
    void handle_event(const AgentEvent& event);
    
    // UI actions
    void on_task_submitted();
    void on_clear_console();
    void on_pause_resume_clicked();
    void on_preferences_clicked();
    
    // Bridge signals
    void on_processing_started();
    void on_processing_completed();
    void on_status_update(const QString& message);
    void on_error_occurred(const QString& error);

private:
    // Setup methods
    void setup_ui();
    void setup_event_subscriptions();
    void create_menus();
    
    // Main components
    TaskPanel* task_panel_;
    AgentMonitor* agent_monitor_;
    IRCViewer* irc_viewer_;
    ToolCallTracker* tool_tracker_;
    MetricsPanel* metrics_panel_;
    
    // Layout
    QSplitter* main_splitter_;
    QSplitter* left_splitter_;
    QSplitter* right_splitter_;
    QTabWidget* bottom_tabs_;
    
    // EventBus subscription
    std::string event_subscription_id_;
    EventBus& event_bus_ = get_event_bus();
    
    // State
    bool is_paused_ = false;
};

// Task input and orchestrator response panel
class TaskPanel : public QWidget {
    Q_OBJECT

public:
    explicit TaskPanel(QWidget* parent = nullptr);
    
    // Update displays
    void add_orchestrator_message(const std::string& message, bool is_thinking = false);
    void add_user_input(const std::string& input);
    void clear_history();
    std::string get_task_input() const;
    void clear_input();
    void set_thinking_state(bool thinking);
    void format_message(const std::string& speaker, const std::string& message, const QString& color);
    
    // Make these accessible for UI control
    QLineEdit* task_input_;
    QPushButton* submit_button_;

signals:
    void task_submitted();
    
private:
    QTextEdit* conversation_display_;
    QPushButton* clear_button_;
    QLabel* status_label_;
};

// Agent monitoring widget
class AgentMonitor : public QWidget {
    Q_OBJECT

public:
    explicit AgentMonitor(QWidget* parent = nullptr);
    
    // Update agent information
    void on_agent_spawning(const std::string& agent_id, const std::string& task);
    void on_agent_spawned(const std::string& agent_id);
    void on_agent_failed(const std::string& agent_id, const std::string& error);
    void on_agent_state_change(const std::string& agent_id, int state);
    void on_agent_completed(const std::string& agent_id);
    void clear_agents();

private:
    QTableWidget* agent_table_;
    QLabel* agent_count_label_;
    
    int find_agent_row(const std::string& agent_id);
    void update_agent_count();
    QString state_to_string(int state);
};

// IRC communication viewer
class IRCViewer : public QWidget {
    Q_OBJECT

public:
    explicit IRCViewer(QWidget* parent = nullptr);
    
    // Add IRC messages
    void add_message(const std::string& channel, const std::string& sender, const std::string& message);
    void add_join(const std::string& channel, const std::string& nick);
    void add_part(const std::string& channel, const std::string& nick);
    void clear_messages();
    
    // Filter by channel
    void set_channel_filter(const std::string& channel);

private slots:
    void on_channel_selected();
    void on_filter_changed(const QString& text);

private:
    QTreeWidget* message_tree_;
    QComboBox* channel_combo_;
    QLineEdit* filter_input_;
    QPushButton* clear_button_;
    
    std::string current_channel_filter_;
    void apply_filters();
};

// Tool call tracking widget
class ToolCallTracker : public QWidget {
    Q_OBJECT

public:
    explicit ToolCallTracker(QWidget* parent = nullptr);
    
    // Add tool calls
    void add_tool_call(const std::string& agent_id, const json& tool_data);
    void add_conflict(const std::string& description);
    void clear_calls();
    
    // Filter by agent
    void set_agent_filter(const std::string& agent_id);

private slots:
    void on_agent_filter_changed();
    void on_tool_filter_changed(const QString& text);

private:
    QTableWidget* tool_table_;
    QComboBox* agent_filter_;
    QLineEdit* tool_filter_;
    QLabel* call_count_label_;
    QLabel* conflict_count_label_;
    
    int total_calls_ = 0;
    int conflict_count_ = 0;
    std::string current_agent_filter_;
    
    void apply_filters();
    void update_stats();
};

// Metrics and statistics panel
class MetricsPanel : public QWidget {
    Q_OBJECT

public:
    explicit MetricsPanel(QWidget* parent = nullptr);
    
    // Update metrics
    void update_token_usage(size_t input_tokens, size_t output_tokens);
    void update_context_usage(double percent);
    void update_agent_metrics(int active, int total);
    void add_timing_metric(const std::string& operation, double duration_ms);

private:
    // Token usage
    QLabel* input_tokens_label_;
    QLabel* output_tokens_label_;
    QLabel* total_tokens_label_;
    
    // Context usage
    QProgressBar* context_bar_;
    QLabel* context_label_;
    
    // Agent counts
    QLabel* active_agents_label_;
    QLabel* total_agents_label_;
    
    // Timing metrics
    QListWidget* timing_list_;
    
    size_t total_input_tokens_ = 0;
    size_t total_output_tokens_ = 0;
};

} // namespace llm_re::ui