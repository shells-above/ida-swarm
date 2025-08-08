#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"
#include "../models/tool_execution.h"

namespace llm_re::ui_v2 {


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
};

// Tool execution dock
class ToolExecutionDock : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit ToolExecutionDock(QWidget* parent = nullptr);
    ~ToolExecutionDock() override;
    
    // Execution management
    QUuid startExecution(const QString& toolName, const QJsonObject& parameters);
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
    void executionCompleted(const QUuid& id, bool success);
    void executionCancelled(const QUuid& id);
    void outputReceived(const QUuid& id, const QString& output);
    void metricsUpdated();
    
public slots:
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
    class ExecutionFilterProxyModel;
    ExecutionModel* model_ = nullptr;
    ExecutionFilterProxyModel* proxyModel_ = nullptr;
    
    QSplitter* mainSplitter_ = nullptr;
    QTabWidget* viewTabs_ = nullptr;
    QTreeView* treeView_ = nullptr;
    PerformanceChartWidget* chartWidget_ = nullptr;
    
    // Detail panel
    QWidget* detailPanel_ = nullptr;
    QLabel* detailToolLabel_ = nullptr;
    QLabel* detailStatusLabel_ = nullptr;
    QLabel* detailDurationLabel_ = nullptr;
    QTextEdit* detailParametersEdit_ = nullptr;
    QTextEdit* detailOutputEdit_ = nullptr;
    
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
        DurationColumn,
        StartTimeColumn,
        OutputColumn,
        ColumnCount
    };
    
    enum Role {
        ExecutionRole = Qt::UserRole + 1,
        IdRole,
        StatusRole
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

// Custom filter proxy model
class ToolExecutionDock::ExecutionFilterProxyModel : public QSortFilterProxyModel {
public:
    explicit ExecutionFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}
    
    void setToolFilter(const QStringList& tools) { toolFilter_ = tools; invalidateFilter(); }
    void setStatusFilter(const QList<ToolExecutionState>& states) { statusFilter_ = states; invalidateFilter(); }
    void setTimeRange(const QDateTime& start, const QDateTime& end) {
        timeRangeStart_ = start;
        timeRangeEnd_ = end;
        invalidateFilter();
    }
    
protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override {
        QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);
        if (!index.isValid()) {
            return false;
        }
        
        ToolExecution exec = index.data(ExecutionModel::ExecutionRole).value<ToolExecution>();
        
        // Tool filter
        if (!toolFilter_.isEmpty() && !toolFilter_.contains(exec.toolName)) {
            return false;
        }
        
        // Status filter
        if (!statusFilter_.isEmpty() && !statusFilter_.contains(exec.state)) {
            return false;
        }
        
        // Time range filter
        if (timeRangeStart_.isValid() && exec.startTime < timeRangeStart_) {
            return false;
        }
        if (timeRangeEnd_.isValid() && exec.endTime.isValid() && exec.endTime > timeRangeEnd_) {
            return false;
        }
        
        return true;
    }
    
    bool lessThan(const QModelIndex& sourceLeft, const QModelIndex& sourceRight) const override {
        // Special handling for duration column - sort numerically not alphabetically
        if (sourceLeft.column() == ExecutionModel::DurationColumn) {
            ToolExecution leftExec = sourceLeft.sibling(sourceLeft.row(), 0).data(ExecutionModel::ExecutionRole).value<ToolExecution>();
            ToolExecution rightExec = sourceRight.sibling(sourceRight.row(), 0).data(ExecutionModel::ExecutionRole).value<ToolExecution>();
            
            qint64 leftDuration = leftExec.getDuration();
            qint64 rightDuration = rightExec.getDuration();
            
            // Handle cases where duration is 0 (pending/running tasks)
            if (leftDuration == 0 && rightDuration == 0) {
                // Both are 0, maintain original order
                return sourceLeft.row() < sourceRight.row();
            }
            if (leftDuration == 0) {
                // Put 0 duration items at the end when ascending
                return false;
            }
            if (rightDuration == 0) {
                // Put 0 duration items at the end when ascending
                return true;
            }
            
            return leftDuration < rightDuration;
        }
        
        // For other columns, use default sorting
        return QSortFilterProxyModel::lessThan(sourceLeft, sourceRight);
    }
    
private:
    QStringList toolFilter_;
    QList<ToolExecutionState> statusFilter_;
    QDateTime timeRangeStart_;
    QDateTime timeRangeEnd_;
};

} // namespace llm_re::ui_v2