#include "ui_common.h"
#include "log_window.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QScrollBar>
#include <QTextCharFormat>
#include <QTextCursor>

namespace llm_re::ui {

LogWindow::LogWindow(QWidget* parent) : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    
    // Toolbar
    auto* toolbar_layout = new QHBoxLayout();
    
    // Log level filter
    toolbar_layout->addWidget(new QLabel("Level:", this));
    level_filter_ = new QComboBox(this);
    level_filter_->addItem("All");
    level_filter_->addItem("Debug");
    level_filter_->addItem("Info");
    level_filter_->addItem("Warning");
    level_filter_->addItem("Error");
    level_filter_->setCurrentIndex(0);
    toolbar_layout->addWidget(level_filter_);
    
    toolbar_layout->addSpacing(20);
    
    // Source filter
    toolbar_layout->addWidget(new QLabel("Source:", this));
    source_filter_ = new QLineEdit(this);
    source_filter_->setPlaceholderText("Filter by source...");
    source_filter_->setMaximumWidth(150);
    toolbar_layout->addWidget(source_filter_);
    
    toolbar_layout->addSpacing(20);
    
    // Auto-scroll checkbox
    auto_scroll_check_ = new QCheckBox("Auto-scroll", this);
    auto_scroll_check_->setChecked(true);
    toolbar_layout->addWidget(auto_scroll_check_);
    
    toolbar_layout->addStretch();
    
    // Action buttons
    clear_button_ = new QPushButton("Clear", this);
    clear_button_->setMaximumWidth(80);
    toolbar_layout->addWidget(clear_button_);
    
    save_button_ = new QPushButton("Save...", this);
    save_button_->setMaximumWidth(80);
    toolbar_layout->addWidget(save_button_);
    
    // Log display
    log_display_ = new QPlainTextEdit(this);
    log_display_->setReadOnly(true);
    log_display_->setFont(QFont("Consolas", 9));
    log_display_->setMaximumBlockCount(MAX_LOG_ENTRIES);
    
    // Set monospace font and dark background for better readability
    QString style = R"(
        QPlainTextEdit {
            background-color: #1e1e1e;
            color: #d4d4d4;
            selection-background-color: #264f78;
            font-family: Consolas, Monaco, monospace;
        }
    )";
    log_display_->setStyleSheet(style);
    
    // Assembly
    layout->addLayout(toolbar_layout);
    layout->addWidget(log_display_);
    
    // Connect signals
    connect(level_filter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogWindow::on_level_filter_changed);
    
    connect(source_filter_, &QLineEdit::textChanged,
            this, &LogWindow::on_source_filter_changed);
    
    connect(clear_button_, &QPushButton::clicked,
            this, &LogWindow::on_clear_clicked);
    
    connect(save_button_, &QPushButton::clicked,
            this, &LogWindow::on_save_clicked);
    
    connect(auto_scroll_check_, &QCheckBox::toggled,
            this, &LogWindow::on_auto_scroll_toggled);
}

void LogWindow::add_log(claude::LogLevel level, const std::string& source, const std::string& message) {
    // Create log entry
    LogEntry entry;
    entry.timestamp = QDateTime::currentDateTime();
    entry.level = level;
    entry.source = source;
    entry.message = message;
    
    // Store in deque (with size limit)
    log_entries_.push_back(entry);
    if (log_entries_.size() > MAX_LOG_ENTRIES) {
        log_entries_.pop_front();
    }
    
    // Check if entry passes current filters
    bool show = true;
    
    // Level filter
    if (level < min_level_) {
        show = false;
    }
    
    // Source filter
    if (show && !source_filter_text_.isEmpty()) {
        if (!QString::fromStdString(source).contains(source_filter_text_, Qt::CaseInsensitive)) {
            show = false;
        }
    }
    
    // Append to display if it passes filters
    if (show) {
        append_log_to_display(entry);
    }
}

void LogWindow::clear_logs() {
    log_entries_.clear();
    log_display_->clear();
}

bool LogWindow::save_to_file(const QString& filename) {
    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    
    QTextStream stream(&file);
    for (const auto& entry : log_entries_) {
        stream << format_log_entry(entry) << "\n";
    }
    
    file.close();
    return true;
}

void LogWindow::on_level_filter_changed(int index) {
    switch (index) {
        case 0: min_level_ = claude::LogLevel::DEBUG; break;
        case 1: min_level_ = claude::LogLevel::DEBUG; break;
        case 2: min_level_ = claude::LogLevel::INFO; break;
        case 3: min_level_ = claude::LogLevel::WARNING; break;
        case 4: min_level_ = claude::LogLevel::ERROR; break;
        default: min_level_ = claude::LogLevel::DEBUG;
    }
    apply_filters();
}

void LogWindow::on_source_filter_changed(const QString& text) {
    source_filter_text_ = text;
    apply_filters();
}

void LogWindow::on_clear_clicked() {
    clear_logs();
}

void LogWindow::on_save_clicked() {
    QString filename = QFileDialog::getSaveFileName(
        this,
        "Save Log File",
        "orchestrator_log.txt",
        "Text Files (*.txt);;All Files (*)"
    );
    
    if (!filename.isEmpty()) {
        if (save_to_file(filename)) {
            QMessageBox::information(this, "Save Successful", "Log file saved successfully.");
        } else {
            QMessageBox::critical(this, "Save Failed", "Failed to save log file.");
        }
    }
}

void LogWindow::on_auto_scroll_toggled(bool checked) {
    auto_scroll_ = checked;
}

void LogWindow::apply_filters() {
    // Clear display and re-add filtered entries
    log_display_->clear();
    
    for (const auto& entry : log_entries_) {
        bool show = true;
        
        // Level filter
        if (entry.level < min_level_) {
            show = false;
        }
        
        // Source filter
        if (show && !source_filter_text_.isEmpty()) {
            if (!QString::fromStdString(entry.source).contains(source_filter_text_, Qt::CaseInsensitive)) {
                show = false;
            }
        }
        
        if (show) {
            append_log_to_display(entry);
        }
    }
}

void LogWindow::append_log_to_display(const LogEntry& entry) {
    QTextCursor cursor = log_display_->textCursor();
    cursor.movePosition(QTextCursor::End);
    
    // Format timestamp
    QString timestamp = entry.timestamp.toString("hh:mm:ss.zzz");
    
    // Set text color based on log level
    QTextCharFormat format;
    format.setForeground(get_level_color(entry.level));
    
    // Build log line
    QString log_line = QString("[%1] [%2] [%3] %4")
        .arg(timestamp)
        .arg(level_to_string(entry.level))
        .arg(QString::fromStdString(entry.source))
        .arg(QString::fromStdString(entry.message));
    
    // Insert with formatting
    cursor.setCharFormat(format);
    cursor.insertText(log_line);
    
    // Add newline if message doesn't end with one
    if (!entry.message.empty() && entry.message.back() != '\n') {
        cursor.insertText("\n");
    }
    
    // Auto-scroll if enabled
    if (auto_scroll_) {
        QScrollBar* scrollbar = log_display_->verticalScrollBar();
        scrollbar->setValue(scrollbar->maximum());
    }
}

QString LogWindow::format_log_entry(const LogEntry& entry) const {
    return QString("[%1] [%2] [%3] %4")
        .arg(entry.timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz"))
        .arg(level_to_string(entry.level))
        .arg(QString::fromStdString(entry.source))
        .arg(QString::fromStdString(entry.message));
}

QColor LogWindow::get_level_color(claude::LogLevel level) const {
    switch (level) {
        case claude::LogLevel::DEBUG:
            return QColor(128, 128, 128);  // Gray
        case claude::LogLevel::INFO:
            return QColor(212, 212, 212);  // Light gray (default)
        case claude::LogLevel::WARNING:
            return QColor(255, 200, 50);   // Orange
        case claude::LogLevel::ERROR:
            return QColor(255, 100, 100);  // Light red
        default:
            return QColor(212, 212, 212);
    }
}

QString LogWindow::level_to_string(claude::LogLevel level) const {
    switch (level) {
        case claude::LogLevel::DEBUG:   return "DEBUG";
        case claude::LogLevel::INFO:    return "INFO ";
        case claude::LogLevel::WARNING: return "WARN ";
        case claude::LogLevel::ERROR:   return "ERROR";
        default:                        return "?????";
    }
}

} // namespace llm_re::ui