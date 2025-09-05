#pragma once

#include "ui_common.h"
#include "../sdk/common.h"
#include <QWidget>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QDateTime>
#include <deque>

namespace llm_re::ui {

// Log entry structure
struct LogEntry {
    QDateTime timestamp;
    claude::LogLevel level;
    std::string source;
    std::string message;
};

// Log window widget for displaying orchestrator and agent logs
class LogWindow : public QWidget {
    Q_OBJECT

public:
    explicit LogWindow(QWidget* parent = nullptr);
    
    // Add log entry
    void add_log(claude::LogLevel level, const std::string& source, const std::string& message);
    
    // Clear all logs
    void clear_logs();
    
    // Save logs to file
    bool save_to_file(const QString& filename);

private slots:
    void on_level_filter_changed(int index);
    void on_source_filter_changed(const QString& text);
    void on_clear_clicked();
    void on_save_clicked();
    void on_auto_scroll_toggled(bool checked);

private:
    // UI components
    QPlainTextEdit* log_display_;
    QComboBox* level_filter_;
    QLineEdit* source_filter_;
    QPushButton* clear_button_;
    QPushButton* save_button_;
    QCheckBox* auto_scroll_check_;
    
    // Log storage
    std::deque<LogEntry> log_entries_;
    static constexpr size_t MAX_LOG_ENTRIES = 10000;
    
    // Current filters
    claude::LogLevel min_level_ = claude::LogLevel::DEBUG;
    QString source_filter_text_;
    bool auto_scroll_ = true;
    
    // Helper methods
    void apply_filters();
    void append_log_to_display(const LogEntry& entry);
    QString format_log_entry(const LogEntry& entry) const;
    QColor get_level_color(claude::LogLevel level) const;
    QString level_to_string(claude::LogLevel level) const;
};

} // namespace llm_re::ui