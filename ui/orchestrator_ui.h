#pragma once

#include <QScrollArea>

#include "ui_common.h"
#include <QTreeWidgetItem>
#include <set>

namespace llm_re::ui {
    // Forward declarations
    class TaskPanel;
    class AgentMonitor;
    class IRCViewer;
    class ToolCallTracker;
    class TokenTracker;
    class MetricsPanel;
    class LogWindow;

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
    void on_preferences_clicked();
    
    // Bridge signals
    void on_processing_started();
    void on_processing_completed();
    void on_status_update(const QString& message);
    void on_error_occurred(const QString& error);

private:
    // Setup methods
    void setup_ui();
    void create_menus();
    void setup_event_subscriptions();
    
    // Main components
    TaskPanel* task_panel_;
    AgentMonitor* agent_monitor_;
    IRCViewer* irc_viewer_;
    ToolCallTracker* tool_tracker_;
    MetricsPanel* metrics_panel_;
    TokenTracker* token_tracker_;  // Real-time token usage for all agents
    LogWindow* log_window_;
    
    // Layout
    QSplitter* main_splitter_;
    QSplitter* left_splitter_;
    QSplitter* right_splitter_;
    QTabWidget* bottom_tabs_;
    
    // EventBus subscription
    std::string event_subscription_id_;
    EventBus& event_bus_ = get_event_bus();
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

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    QTableWidget* agent_table_;
    QLabel* agent_count_label_;
    QTimer* duration_timer_;
    std::map<std::string, QDateTime> agent_spawn_times_;
    std::map<std::string, QDateTime> agent_completion_times_;
    
    int find_agent_row(const std::string& agent_id);
    void update_agent_count();
    void update_durations();
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
    void on_item_double_clicked(QTreeWidgetItem* item, int column);

private:
    QTreeWidget* message_tree_;
    QComboBox* channel_combo_;
    QLineEdit* filter_input_;
    QPushButton* clear_button_;
    
    std::string current_channel_filter_;
    std::set<std::string> discovered_conflict_channels_;
    bool showing_conflict_list_ = false;
    
    // Store all messages to repopulate tree when switching views
    struct IRCMessage {
        QString time;
        QString channel;
        QString sender;
        QString message;
    };
    std::vector<IRCMessage> all_messages_;
    
    void apply_filters();
    void show_conflict_channel_list();
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

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

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
    void update_token_usage(size_t input_tokens, size_t output_tokens, size_t cache_read = 0, size_t cache_write = 0);
    void set_token_usage(size_t input_tokens, size_t output_tokens, size_t cache_read = 0, size_t cache_write = 0);
    void set_total_cost(double cost);
    void update_context_usage(double percent);
    
    // Update agent-specific context usage
    void update_agent_context(const std::string& agent_id, double percent, size_t input_tokens, size_t cache_read_tokens);
    void clear_agent_contexts();
    void remove_agent_context(const std::string& agent_id);

private:
    // Token usage
    QLabel* input_tokens_label_;
    QLabel* output_tokens_label_;
    QLabel* total_tokens_label_;
    QLabel* cache_read_label_;
    QLabel* cache_write_label_;
    QLabel* cost_label_;
    
    // Context usage - orchestrator
    QProgressBar* context_bar_;
    QLabel* context_label_;
    
    // Agent context usage
    struct AgentContextBar {
        QProgressBar* bar;
        QLabel* label;
        QLabel* tokens_label;
        double percentage = 0.0;
    };
    QScrollArea* agent_context_scroll_;
    QWidget* agent_context_container_;
    QVBoxLayout* agent_context_layout_;
    std::map<std::string, AgentContextBar> agent_contexts_;
    
    size_t total_input_tokens_ = 0;
    size_t total_output_tokens_ = 0;
    size_t total_cache_read_tokens_ = 0;
    size_t total_cache_write_tokens_ = 0;
    
    // Helper to create styled progress bar
    QProgressBar* create_context_bar();
    void update_context_bar_style(QProgressBar* bar, double percentage);
};

// Real-time token usage tracker for all agents and orchestrator
class TokenTracker : public QWidget {
    Q_OBJECT

public:
    explicit TokenTracker(QWidget* parent = nullptr);
    
    // Update token usage for an agent or orchestrator
    void update_agent_tokens(const std::string& agent_id, const json& token_data);
    
    // Mark an agent as completed
    void mark_agent_completed(const std::string& agent_id);
    
    // Clear all token data
    void clear_all();
    
    // Get total usage across all agents
    claude::TokenUsage get_total_usage() const;
    
    // Get total cost across all agents (using their pre-calculated costs)
    double get_total_cost() const;

private:
    struct AgentTokens {
        claude::TokenUsage current;
        double estimated_cost = 0.0;  // Store pre-calculated cost from agent
        bool is_active = false;
        bool is_completed = false;  // Track if agent has completed
        std::chrono::steady_clock::time_point last_update;
        int iteration = 0;
    };
    
    // UI Components
    QTableWidget* token_table_;
    QLabel* total_cost_label_;

    // Data
    std::map<std::string, AgentTokens> agent_tokens_;
    
    // Update the display
    void refresh_display();
    QString format_tokens(const claude::TokenUsage& usage);
    QString format_cost(double cost);
};

} // namespace llm_re::ui