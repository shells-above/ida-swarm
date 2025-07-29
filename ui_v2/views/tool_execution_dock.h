#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"
#include "../models/tool_execution.h"

namespace llm_re::ui_v2 {

// Execution timeline widget
class ExecutionTimelineWidget : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit ExecutionTimelineWidget(QWidget* parent = nullptr);
    
    void setExecutions(const QList<ToolExecution>& executions);
    void highlightExecution(const QUuid& id);
    void setTimeRange(const QDateTime& start, const QDateTime& end);
    void setZoomLevel(int level);
    
    void setShowDependencies(bool show) { showDependencies_ = show; update(); }
    void setShowSubTasks(bool show) { showSubTasks_ = show; update(); }
    void setGroupByTool(bool group) { groupByTool_ = group; update(); }
    
signals:
    void executionClicked(const QUuid& id);
    void executionDoubleClicked(const QUuid& id);
    void timeRangeChanged(const QDateTime& start, const QDateTime& end);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    
private:
    struct TimelineItem {
        ToolExecution execution;
        QRectF rect;
        int row;
    };
    
    void calculateLayout();
    QColor statusColor(ToolExecutionState state) const;
    QString formatDuration(qint64 ms) const;
    
    QList<TimelineItem> items_;
    QDateTime startTime_;
    QDateTime endTime_;
    int zoomLevel_ = 100;
    bool showDependencies_ = true;
    bool showSubTasks_ = false;
    bool groupByTool_ = false;
    
    // Interaction
    QUuid highlightedId_;
    QUuid hoveredId_;
    bool isPanning_ = false;
    QPoint panStartPos_;
    QPointF viewOffset_;
    
    // Layout
    int headerHeight_ = 40;
    int rowHeight_ = 30;
    int margin_ = 10;
};

// Performance chart widget
class PerformanceChartWidget : public BaseStyledWidget {
    Q_OBJECT
    
public:
    enum ChartType {
        LineChart,
        BarChart,
        PieChart,
        ScatterPlot
    };
    
    enum Metric {
        ExecutionTime,
        SuccessRate,
        ThroughputRate,
        ErrorRate
    };
    
    explicit PerformanceChartWidget(QWidget* parent = nullptr);
    
    void setExecutions(const QList<ToolExecution>& executions);
    void setChartType(ChartType type) { chartType_ = type; update(); }
    void setMetric(Metric metric) { metric_ = metric; update(); }
    void setTimeRange(const QDateTime& start, const QDateTime& end);
    void setGroupBy(const QString& field) { groupBy_ = field; update(); }

signals:
    void dataPointClicked(const QString& label, double value);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    
private:
    struct DataPoint {
        QString label;
        double value;
        QColor color;
        QRectF rect;
    };
    
    void calculateData();
    void drawLineChart(QPainter* painter);
    void drawBarChart(QPainter* painter);
    void drawPieChart(QPainter* painter);
    void drawScatterPlot(QPainter* painter);
    void drawAxes(QPainter* painter);
    void drawLegend(QPainter* painter);

    QList<ToolExecution> executions_;
    QList<DataPoint> dataPoints_;
    ChartType chartType_ = LineChart;
    Metric metric_ = ExecutionTime;
    QString groupBy_ = "tool";
    QDateTime startTime_;
    QDateTime endTime_;
    
    // Interaction
    int hoveredPoint_ = -1;
    
    // Layout
    QRectF chartRect_;
    QRectF legendRect_;
};

// Tool execution dock
class ToolExecutionDock : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit ToolExecutionDock(QWidget* parent = nullptr);
    ~ToolExecutionDock() override;
    
    // Execution management
    QUuid startExecution(const QString& toolName, const QJsonObject& parameters);
    void updateProgress(const QUuid& id, int progress, const QString& message = QString());
    void completeExecution(const QUuid& id, bool success, const QString& output = QString());
    void cancelExecution(const QUuid& id);
    
    // Data access
    QList<ToolExecution> executions() const;
    ToolExecution execution(const QUuid& id) const;
    QList<ToolExecution> runningExecutions() const;
    
    // View control
    void showExecution(const QUuid& id);
    void setViewMode(const QString& mode);
    void setTimeRange(const QDateTime& start, const QDateTime& end);
    
    // Filtering
    void setToolFilter(const QStringList& tools);
    void setStatusFilter(const QList<ToolExecutionState>& states);
    void clearFilters();
    
    
    // Tool management
    void registerTool(const QString& name, const QString& description);
    void setToolEnabled(const QString& name, bool enabled);
    QStringList availableTools() const;
    QStringList enabledTools() const;
    
    // Favorites/Shortcuts
    void addFavorite(const QString& toolName, const QJsonObject& parameters);
    void removeFavorite(const QString& name);
    QStringList favorites() const;
    void executeFavorite(const QString& name);
    
    // State export/import
    QJsonObject exportState() const;
    void importState(const QJsonObject& state);
    
signals:
    void executionStarted(const QUuid& id);
    void executionProgress(const QUuid& id, int progress);
    void executionCompleted(const QUuid& id, bool success);
    void executionCancelled(const QUuid& id);
    void retryRequested(const QUuid& id);
    void outputReceived(const QUuid& id, const QString& output);
    void metricsUpdated();
    
public slots:
    void retryExecution(const QUuid& id);
    void clearHistory();
    void updateMetrics();
    
protected:
    void onThemeChanged() override;
    
private slots:
    void onViewModeChanged(int index);
    void onExecutionClicked(const QModelIndex& index);
    void onExecutionDoubleClicked(const QModelIndex& index);
    void onSelectionChanged();
    void onCustomContextMenu(const QPoint& pos);
    void onTimelineExecutionClicked(const QUuid& id);
    void onChartDataPointClicked(const QString& label, double value);
    void onFilterChanged();
    void updateRunningExecutions();
    void autoSave();
    
private:
    void setupUI();
    void createToolBar();
    void createViews();
    void createDetailPanel();
    void createContextMenu();
    void connectSignals();
    void loadSettings();
    void saveSettings();
    void applyFilters();
    
    // Models and views
    class ExecutionModel;
    ExecutionModel* model_ = nullptr;
    QSortFilterProxyModel* proxyModel_ = nullptr;
    
    QSplitter* mainSplitter_ = nullptr;
    QTabWidget* viewTabs_ = nullptr;
    QTreeView* treeView_ = nullptr;
    QTableView* tableView_ = nullptr;
    ExecutionTimelineWidget* timelineWidget_ = nullptr;
    PerformanceChartWidget* chartWidget_ = nullptr;
    
    // Detail panel
    QWidget* detailPanel_ = nullptr;
    QLabel* detailToolLabel_ = nullptr;
    QLabel* detailStatusLabel_ = nullptr;
    QLabel* detailDurationLabel_ = nullptr;
    QTextEdit* detailParametersEdit_ = nullptr;
    QTextEdit* detailOutputEdit_ = nullptr;
    QProgressBar* detailProgressBar_ = nullptr;
    QPushButton* retryButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    
    // Toolbar
    QToolBar* toolBar_ = nullptr;
    QComboBox* toolFilterCombo_ = nullptr;
    QComboBox* statusFilterCombo_ = nullptr;
    QAction* clearHistoryAction_ = nullptr;
    QAction* autoScrollAction_ = nullptr;
    
    // Context menu
    QMenu* contextMenu_ = nullptr;
    
    // State
    QList<ToolExecution> executions_;
    QHash<QUuid, ToolExecution*> executionMap_;
    QUuid selectedExecution_;
    QStringList toolFilter_;
    QList<ToolExecutionState> statusFilter_;
    bool autoScroll_ = true;
    
    // Timers
    QTimer* updateTimer_ = nullptr;
    QTimer* autoSaveTimer_ = nullptr;
    
    // Tool registry
    struct ToolInfo {
        QString name;
        QString description;
        bool enabled = true;
        int executionCount = 0;
        qint64 totalDuration = 0;
        int successCount = 0;
        int failureCount = 0;
    };
    QHash<QString, ToolInfo> tools_;
    
    // Favorites
    struct FavoriteExecution {
        QString name;
        QString toolName;
        QJsonObject parameters;
    };
    QList<FavoriteExecution> favorites_;
    
    // Time range filters
    QDateTime timeRangeStart_;
    QDateTime timeRangeEnd_;
    
    // Statistics
    int completedCount_ = 0;
    int failedCount_ = 0;
};

// Execution model
class ToolExecutionDock::ExecutionModel : public QAbstractItemModel {
    Q_OBJECT
    
public:
    enum Column {
        ToolColumn,
        StatusColumn,
        ProgressColumn,
        DurationColumn,
        StartTimeColumn,
        OutputColumn,
        ColumnCount
    };
    
    enum Role {
        ExecutionRole = Qt::UserRole + 1,
        IdRole,
        StatusRole,
        ProgressRole
    };
    
    explicit ExecutionModel(QObject* parent = nullptr);
    
    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    
    // Data management
    void setExecutions(const QList<ToolExecution>& executions);
    void addExecution(const ToolExecution& execution);
    void updateExecution(const QUuid& id, const ToolExecution& execution);
    void removeExecution(const QUuid& id);
    void clear();
    
    ToolExecution execution(const QUuid& id) const;
    QModelIndex indexForId(const QUuid& id) const;
    
private:
    QList<ToolExecution> executions_;
    QHash<QUuid, int> indexMap_;
};

} // namespace llm_re::ui_v2