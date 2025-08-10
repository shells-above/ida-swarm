#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"

namespace llm_re::ui_v2 {

// Log entry structure
struct LogEntry {
    QDateTime timestamp;
    llm_re::LogLevel level;  // Use core LogLevel enum
    QString category;
    QString message;
    QJsonObject metadata;
};

class ConsoleDock : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit ConsoleDock(QWidget* parent = nullptr);
    ~ConsoleDock() override = default;
    
    // Log management
    void addLog(const LogEntry& entry);
    void clearLogs();
    void setMaxLogCount(int count);
    
    // Filtering
    void setLevelFilter(int minLevel);
    void setCategoryFilter(const QStringList& categories);
    void setTextFilter(const QString& text);
    
    // Export
    void exportLogs(const QString& filename);
    
signals:
    void logCountChanged(int count);
    void filterChanged();
    
    
protected:
    void onThemeChanged() override;
    
private:
    void setupUi();
    void createActions();
    void createToolBar();
    void createLogView();
    void createFilterBar();
    void updateLogView();
    void applyFilters();
    
    // Actions
    QAction* clearAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* autoScrollAction_ = nullptr;
    QAction* wrapTextAction_ = nullptr;
    QAction* showTimestampsAction_ = nullptr;
    
    // UI Components
    QToolBar* toolBar_ = nullptr;
    QTextEdit* logView_ = nullptr;
    QWidget* filterBar_ = nullptr;
    QComboBox* levelFilter_ = nullptr;
    QLineEdit* textFilter_ = nullptr;
    QCheckBox* regexFilter_ = nullptr;
    
    // Data
    QList<LogEntry> logs_;
    int maxLogCount_ = 10000;
    int minLevelFilter_ = 0;
    QStringList categoryFilter_;
    QString textFilterString_;
    bool autoScroll_ = true;
    bool wrapText_ = true;
    bool showTimestamps_ = true;
    bool useRegex_ = false;
    
    // Colors for log levels
    struct LogColors {
        QColor debug;
        QColor info;
        QColor warning;
        QColor error;
    } logColors_;
};

} // namespace llm_re::ui_v2