#include "console_dock.h"
#include "../core/theme_manager.h"
#include "../core/ui_utils.h"
#include "../core/color_constants.h"

namespace llm_re::ui_v2 {

ConsoleDock::ConsoleDock(QWidget* parent)
    : BaseStyledWidget(parent) {
    setupUi();
    createActions();
    createToolBar();
    createLogView();
    createFilterBar();
    
    // Apply initial theme
    onThemeChanged();
}

void ConsoleDock::setupUi() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
}

void ConsoleDock::createActions() {
    clearAction_ = new QAction(tr("Clear"), this);
    clearAction_->setIcon(ThemeManager::instance().themedIcon("edit-clear"));
    clearAction_->setToolTip(tr("Clear all logs"));
    connect(clearAction_, &QAction::triggered, this, &ConsoleDock::clearLogs);
    
    exportAction_ = new QAction(tr("Export"), this);
    exportAction_->setIcon(ThemeManager::instance().themedIcon("document-save"));
    exportAction_->setToolTip(tr("Export logs to file"));
    connect(exportAction_, &QAction::triggered, [this]() {
        QString filename = QFileDialog::getSaveFileName(this, tr("Export Logs"), 
                                                       "console_logs.txt", 
                                                       tr("Text Files (*.txt);;All Files (*)"));
        if (!filename.isEmpty()) {
            exportLogs(filename);
        }
    });
    
    autoScrollAction_ = new QAction(tr("Auto Scroll"), this);
    autoScrollAction_->setCheckable(true);
    autoScrollAction_->setChecked(autoScroll_);
    autoScrollAction_->setIcon(ThemeManager::instance().themedIcon("go-bottom"));
    connect(autoScrollAction_, &QAction::toggled, [this](bool checked) {
        autoScroll_ = checked;
    });
    
    wrapTextAction_ = new QAction(tr("Wrap Text"), this);
    wrapTextAction_->setCheckable(true);
    wrapTextAction_->setChecked(wrapText_);
    wrapTextAction_->setIcon(ThemeManager::instance().themedIcon("format-text-wrap"));
    connect(wrapTextAction_, &QAction::toggled, [this](bool checked) {
        wrapText_ = checked;
        if (logView_) {
            logView_->setWordWrapMode(checked ? QTextOption::WrapAtWordBoundaryOrAnywhere 
                                              : QTextOption::NoWrap);
        }
    });
    
    showTimestampsAction_ = new QAction(tr("Show Timestamps"), this);
    showTimestampsAction_->setCheckable(true);
    showTimestampsAction_->setChecked(showTimestamps_);
    showTimestampsAction_->setIcon(ThemeManager::instance().themedIcon("appointment-new"));
    connect(showTimestampsAction_, &QAction::toggled, [this](bool checked) {
        showTimestamps_ = checked;
        updateLogView();
    });
}

void ConsoleDock::createToolBar() {
    toolBar_ = new QToolBar(this);
    toolBar_->setIconSize(QSize(16, 16));
    toolBar_->setMovable(false);
    
    toolBar_->addAction(clearAction_);
    toolBar_->addAction(exportAction_);
    toolBar_->addSeparator();
    toolBar_->addAction(autoScrollAction_);
    toolBar_->addAction(wrapTextAction_);
    toolBar_->addAction(showTimestampsAction_);
    
    layout()->addWidget(toolBar_);
}

void ConsoleDock::createLogView() {
    logView_ = new QTextEdit(this);
    logView_->setReadOnly(true);
    logView_->setWordWrapMode(wrapText_ ? QTextOption::WrapAtWordBoundaryOrAnywhere 
                                        : QTextOption::NoWrap);
    
    // Set monospace font
    QFont font("Consolas");
    font.setStyleHint(QFont::Monospace);
    font.setPointSize(10);
    logView_->setFont(font);
    
    layout()->addWidget(logView_);
}

void ConsoleDock::createFilterBar() {
    filterBar_ = new QWidget(this);
    auto* filterLayout = new QHBoxLayout(filterBar_);
    filterLayout->setContentsMargins(4, 4, 4, 4);
    
    // Level filter
    auto* levelLabel = new QLabel(tr("Level:"), this);
    filterLayout->addWidget(levelLabel);
    
    levelFilter_ = new QComboBox(this);
    levelFilter_->addItems({tr("All"), tr("Info+"), tr("Warning+"), tr("Error")});
    levelFilter_->setCurrentIndex(0);
    connect(levelFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
                setLevelFilter(index);
            });
    filterLayout->addWidget(levelFilter_);
    
    filterLayout->addSpacing(12);
    
    // Text filter
    auto* textLabel = new QLabel(tr("Filter:"), this);
    filterLayout->addWidget(textLabel);
    
    textFilter_ = new QLineEdit(this);
    textFilter_->setPlaceholderText(tr("Filter logs..."));
    textFilter_->setClearButtonEnabled(true);
    connect(textFilter_, &QLineEdit::textChanged, [this](const QString& text) {
        setTextFilter(text);
    });
    filterLayout->addWidget(textFilter_, 1);
    
    // Regex checkbox
    regexFilter_ = new QCheckBox(tr("Regex"), this);
    connect(regexFilter_, &QCheckBox::toggled, [this](bool checked) {
        useRegex_ = checked;
        applyFilters();
    });
    filterLayout->addWidget(regexFilter_);
    
    layout()->addWidget(filterBar_);
}

void ConsoleDock::addLog(const LogEntry& entry) {
    logs_.append(entry);
    
    // Trim logs if exceeding max count
    while (logs_.size() > maxLogCount_) {
        logs_.removeFirst();
    }
    
    // Check if entry passes filters
    if (entry.level < minLevelFilter_) {
        return;
    }
    
    if (!textFilterString_.isEmpty()) {
        if (useRegex_) {
            QRegularExpression regex(textFilterString_);
            if (!regex.match(entry.message).hasMatch()) {
                return;
            }
        } else {
            if (!entry.message.contains(textFilterString_, Qt::CaseInsensitive)) {
                return;
            }
        }
    }
    
    // Format and append log entry
    QString formattedLog;
    QTextStream stream(&formattedLog);
    
    if (showTimestamps_) {
        stream << entry.timestamp.toString("hh:mm:ss.zzz") << " ";
    }
    
    // Level indicator
    QString levelStr;
    QColor levelColor;
    switch (entry.level) {
        case LogEntry::Debug:
            levelStr = "[DEBUG]";
            levelColor = logColors_.debug;
            break;
        case LogEntry::Info:
            levelStr = "[INFO ]";
            levelColor = logColors_.info;
            break;
        case LogEntry::Warning:
            levelStr = "[WARN ]";
            levelColor = logColors_.warning;
            break;
        case LogEntry::Error:
            levelStr = "[ERROR]";
            levelColor = logColors_.error;
            break;
    }
    
    stream << levelStr << " ";
    
    if (!entry.category.isEmpty()) {
        stream << "[" << entry.category << "] ";
    }
    
    stream << entry.message;
    
    // Append to log view with color
    QTextCursor cursor = logView_->textCursor();
    cursor.movePosition(QTextCursor::End);
    
    QTextCharFormat format;
    format.setForeground(levelColor);
    cursor.insertText(formattedLog + "\n", format);
    
    // Auto scroll if enabled
    if (autoScroll_) {
        logView_->moveCursor(QTextCursor::End);
    }
    
    emit logCountChanged(logs_.size());
}

void ConsoleDock::clearLogs() {
    logs_.clear();
    logView_->clear();
    emit logCountChanged(0);
}

void ConsoleDock::setMaxLogCount(int count) {
    maxLogCount_ = count;
    
    // Trim existing logs if needed
    while (logs_.size() > maxLogCount_) {
        logs_.removeFirst();
    }
    
    updateLogView();
}

void ConsoleDock::setLevelFilter(int minLevel) {
    minLevelFilter_ = minLevel;
    applyFilters();
}

void ConsoleDock::setCategoryFilter(const QStringList& categories) {
    categoryFilter_ = categories;
    applyFilters();
}

void ConsoleDock::setTextFilter(const QString& text) {
    textFilterString_ = text;
    applyFilters();
}

void ConsoleDock::exportLogs(const QString& filename) {
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        
        for (const auto& entry : logs_) {
            stream << entry.timestamp.toString("yyyy-MM-dd hh:mm:ss.zzz") << " ";
            
            switch (entry.level) {
                case LogEntry::Debug: stream << "[DEBUG]"; break;
                case LogEntry::Info: stream << "[INFO ]"; break;
                case LogEntry::Warning: stream << "[WARN ]"; break;
                case LogEntry::Error: stream << "[ERROR]"; break;
            }
            
            stream << " ";
            
            if (!entry.category.isEmpty()) {
                stream << "[" << entry.category << "] ";
            }
            
            stream << entry.message << Qt::endl;
        }
    }
}


void ConsoleDock::onThemeChanged() {
    BaseStyledWidget::onThemeChanged();
    
    const auto& colors = ThemeManager::instance().colors();
    
    // Update log colors
    logColors_.debug = colors.textSecondary;
    logColors_.info = colors.textPrimary;
    logColors_.warning = colors.warning;
    logColors_.error = colors.error;
    
    // Update log view styling
    if (logView_) {
        logView_->setStyleSheet(QString(
            "QTextEdit {"
            "    background-color: %1;"
            "    color: %2;"
            "    border: none;"
            "}"
        ).arg(colors.background.name())
         .arg(colors.textPrimary.name()));
    }
    
    // Update filter bar styling
    if (filterBar_) {
        filterBar_->setStyleSheet(QString(
            "QWidget { background-color: %1; }"
            "QLabel { color: %2; }"
        ).arg(colors.surface.name())
         .arg(colors.textSecondary.name()));
    }
}

void ConsoleDock::updateLogView() {
    logView_->clear();
    
    for (const auto& entry : logs_) {
        // Re-add all logs that pass filters
        if (entry.level >= minLevelFilter_) {
            if (!textFilterString_.isEmpty()) {
                if (useRegex_) {
                    QRegularExpression regex(textFilterString_);
                    if (!regex.match(entry.message).hasMatch()) {
                        continue;
                    }
                } else {
                    if (!entry.message.contains(textFilterString_, Qt::CaseInsensitive)) {
                        continue;
                    }
                }
            }
            
            // Add the log entry (duplicate code from addLog, could be refactored)
            QString formattedLog;
            QTextStream stream(&formattedLog);
            
            if (showTimestamps_) {
                stream << entry.timestamp.toString("hh:mm:ss.zzz") << " ";
            }
            
            QString levelStr;
            QColor levelColor;
            switch (entry.level) {
                case LogEntry::Debug:
                    levelStr = "[DEBUG]";
                    levelColor = logColors_.debug;
                    break;
                case LogEntry::Info:
                    levelStr = "[INFO ]";
                    levelColor = logColors_.info;
                    break;
                case LogEntry::Warning:
                    levelStr = "[WARN ]";
                    levelColor = logColors_.warning;
                    break;
                case LogEntry::Error:
                    levelStr = "[ERROR]";
                    levelColor = logColors_.error;
                    break;
            }
            
            stream << levelStr << " ";
            
            if (!entry.category.isEmpty()) {
                stream << "[" << entry.category << "] ";
            }
            
            stream << entry.message;
            
            QTextCursor cursor = logView_->textCursor();
            cursor.movePosition(QTextCursor::End);
            
            QTextCharFormat format;
            format.setForeground(levelColor);
            cursor.insertText(formattedLog + "\n", format);
        }
    }
}

void ConsoleDock::applyFilters() {
    updateLogView();
    emit filterChanged();
}

} // namespace llm_re::ui_v2