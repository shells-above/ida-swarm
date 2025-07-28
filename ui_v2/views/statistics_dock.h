#pragma once

#include "../core/base_styled_widget.h"
#include <QDateTime>
#include <QJsonObject>
#include <memory>

class QTabWidget;
class QChartView;
class QChart;
class QValueAxis;
class QDateTimeAxis;
class QBarSeries;
class QLineSeries;
class QPieSeries;
class QAreaSeries;
class QScatterSeries;
class QTimer;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QTableWidget;
class QTreeWidget;

namespace llm_re::ui_v2 {

// Statistics data point
struct StatDataPoint {
    QDateTime timestamp;
    QString category;
    QString subcategory;
    double value;
    QJsonObject metadata;
};

// Base chart widget with common functionality
class BaseChartWidget : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit BaseChartWidget(QWidget* parent = nullptr);
    ~BaseChartWidget() override;
    
    void setTitle(const QString& title);
    void setAnimated(bool animated);
    void setTheme(const QString& theme);
    
    virtual void setData(const QList<StatDataPoint>& data) = 0;
    virtual void refresh() = 0;
    virtual void exportChart(const QString& format);
    
    QChart* chart() const { return chart_; }
    
signals:
    void dataPointClicked(const StatDataPoint& point);
    void rangeChanged(const QDateTime& start, const QDateTime& end);
    
protected:
    void setupChart();
    virtual void updateChart() = 0;
    void applyChartTheme();
    
    QChartView* chartView_ = nullptr;
    QChart* chart_ = nullptr;
    QList<StatDataPoint> data_;
    bool animated_ = true;
    QString chartTheme_ = "dark";
};

// Message statistics chart
class MessageStatsChart : public BaseChartWidget {
    Q_OBJECT
    
public:
    explicit MessageStatsChart(QWidget* parent = nullptr);
    
    void setData(const QList<StatDataPoint>& data) override;
    void refresh() override;
    
    void setTimeRange(const QDateTime& start, const QDateTime& end);
    void setGroupBy(const QString& groupBy); // hour, day, week, month
    void setMetric(const QString& metric); // count, length, tokens
    
protected:
    void updateChart() override;
    
private:
    void createSeries();
    void updateAxes();
    
    QLineSeries* userSeries_ = nullptr;
    QLineSeries* assistantSeries_ = nullptr;
    QLineSeries* systemSeries_ = nullptr;
    QAreaSeries* totalArea_ = nullptr;
    
    QDateTimeAxis* xAxis_ = nullptr;
    QValueAxis* yAxis_ = nullptr;
    
    QString groupBy_ = "hour";
    QString metric_ = "count";
    QDateTime startTime_;
    QDateTime endTime_;
};

// Tool usage chart
class ToolUsageChart : public BaseChartWidget {
    Q_OBJECT
    
public:
    explicit ToolUsageChart(QWidget* parent = nullptr);
    
    void setData(const QList<StatDataPoint>& data) override;
    void refresh() override;
    
    void setChartType(const QString& type); // bar, pie, stacked
    void setMetric(const QString& metric); // count, duration, success_rate
    void setTopN(int n);
    
protected:
    void updateChart() override;
    
private:
    void createBarChart();
    void createPieChart();
    void createStackedChart();
    
    QString chartType_ = "bar";
    QString metric_ = "count";
    int topN_ = 10;
    
    QBarSeries* barSeries_ = nullptr;
    QPieSeries* pieSeries_ = nullptr;
};

// Performance metrics chart
class PerformanceChart : public BaseChartWidget {
    Q_OBJECT
    
public:
    explicit PerformanceChart(QWidget* parent = nullptr);
    
    void setData(const QList<StatDataPoint>& data) override;
    void refresh() override;
    
    void addMetric(const QString& name, const QString& unit);
    void removeMetric(const QString& name);
    void setMetricVisible(const QString& name, bool visible);
    
protected:
    void updateChart() override;
    
private:
    struct MetricInfo {
        QString name;
        QString unit;
        QLineSeries* series = nullptr;
        QValueAxis* axis = nullptr;
        bool visible = true;
    };
    
    QHash<QString, MetricInfo> metrics_;
    QDateTimeAxis* timeAxis_ = nullptr;
};

// Token usage chart
class TokenUsageChart : public BaseChartWidget {
    Q_OBJECT
    
public:
    explicit TokenUsageChart(QWidget* parent = nullptr);
    
    void setData(const QList<StatDataPoint>& data) override;
    void refresh() override;
    
    void setModel(const QString& model);
    void setCostPerToken(double cost);
    void setShowCost(bool show);
    void setShowCumulative(bool show);
    
protected:
    void updateChart() override;
    
private:
    QLineSeries* inputTokensSeries_ = nullptr;
    QLineSeries* outputTokensSeries_ = nullptr;
    QLineSeries* totalTokensSeries_ = nullptr;
    QLineSeries* cumulativeSeries_ = nullptr;
    QLineSeries* costSeries_ = nullptr;
    
    QString model_ = "claude-3";
    double costPerToken_ = 0.00001;
    bool showCost_ = false;
    bool showCumulative_ = true;
};

// Memory analysis chart
class MemoryAnalysisChart : public BaseChartWidget {
    Q_OBJECT
    
public:
    explicit MemoryAnalysisChart(QWidget* parent = nullptr);
    
    void setData(const QList<StatDataPoint>& data) override;
    void refresh() override;
    
    void setAnalysisType(const QString& type); // coverage, confidence, complexity
    void setGroupBy(const QString& groupBy); // module, function, time
    
protected:
    void updateChart() override;
    
private:
    QString analysisType_ = "coverage";
    QString groupBy_ = "module";
    
    QScatterSeries* scatterSeries_ = nullptr;
    QBarSeries* barSeries_ = nullptr;
};

// Statistics summary widget
class StatsSummaryWidget : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit StatsSummaryWidget(QWidget* parent = nullptr);
    
    void updateStats(const QJsonObject& stats);
    void setTimeRange(const QDateTime& start, const QDateTime& end);
    
    void addCustomStat(const QString& name, const QString& value, const QString& icon = QString());
    void clearCustomStats();
    
signals:
    void statClicked(const QString& name);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    
private:
    struct StatItem {
        QString name;
        QString value;
        QString icon;
        QString tooltip;
        QRectF rect;
        bool isCustom = false;
    };
    
    void layoutStats();
    
    QList<StatItem> stats_;
    int hoveredStat_ = -1;
    int columns_ = 4;
    int cardHeight_ = 80;
    int spacing_ = 16;
};

// Real-time metrics widget
class RealtimeMetricsWidget : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit RealtimeMetricsWidget(QWidget* parent = nullptr);
    ~RealtimeMetricsWidget();
    
    void addMetric(const QString& name, const QString& unit, double min = 0, double max = 100);
    void updateMetric(const QString& name, double value);
    void removeMetric(const QString& name);
    
    void setUpdateInterval(int ms);
    void setHistorySize(int size);
    void start();
    void stop();
    
signals:
    void metricUpdated(const QString& name, double value);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    
private slots:
    void updateDisplay();
    
private:
    struct Metric {
        QString name;
        QString unit;
        double value = 0;
        double min = 0;
        double max = 100;
        QList<double> history;
        QColor color;
    };
    
    void drawMetric(QPainter* painter, const Metric& metric, const QRectF& rect);
    void drawSparkline(QPainter* painter, const QList<double>& values, const QRectF& rect);
    
    QHash<QString, Metric> metrics_;
    QTimer* updateTimer_ = nullptr;
    int updateInterval_ = 1000;
    int historySize_ = 50;
    bool isRunning_ = false;
};

// Historical comparison widget
class HistoricalComparisonWidget : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit HistoricalComparisonWidget(QWidget* parent = nullptr);
    
    void setCurrentPeriod(const QDateTime& start, const QDateTime& end);
    void setComparisonPeriod(const QDateTime& start, const QDateTime& end);
    void setMetrics(const QStringList& metrics);
    void updateData(const QJsonObject& currentData, const QJsonObject& comparisonData);
    
    void setComparisonType(const QString& type); // previous, same_day_last_week, custom
    
signals:
    void metricSelected(const QString& metric);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    struct ComparisonMetric {
        QString name;
        double currentValue = 0;
        double previousValue = 0;
        double change = 0;
        double changePercent = 0;
        QString trend; // up, down, stable
    };
    
    void calculateChanges();
    void drawMetricCard(QPainter* painter, const ComparisonMetric& metric, const QRectF& rect);
    
    QList<ComparisonMetric> metrics_;
    QDateTime currentStart_;
    QDateTime currentEnd_;
    QDateTime comparisonStart_;
    QDateTime comparisonEnd_;
    QString comparisonType_ = "previous";
};

// Main statistics dock
class StatisticsDock : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit StatisticsDock(QWidget* parent = nullptr);
    ~StatisticsDock() override;
    
    // Data updates
    void addDataPoint(const StatDataPoint& point);
    void addDataPoints(const QList<StatDataPoint>& points);
    void clearData();
    
    // Time range
    void setTimeRange(const QDateTime& start, const QDateTime& end);
    QDateTime startTime() const { return startTime_; }
    QDateTime endTime() const { return endTime_; }
    
    // View control
    void setCurrentView(const QString& view);
    void refreshAll();
    
    // Export
    void exportData(const QString& format = "csv");
    void exportCharts(const QString& format = "png");
    void generateReport(const QString& format = "pdf");
    
    // Custom metrics
    void registerCustomMetric(const QString& name, const QString& unit);
    void updateCustomMetric(const QString& name, double value);
    
    // Real-time mode
    void setRealtimeEnabled(bool enabled);
    bool isRealtimeEnabled() const { return realtimeEnabled_; }
    
signals:
    void dataPointClicked(const StatDataPoint& point);
    void timeRangeChanged(const QDateTime& start, const QDateTime& end);
    void viewChanged(const QString& view);
    void reportGenerated(const QString& path);
    void customMetricUpdated(const QString& name, double value);
    
public slots:
    void updateStatistics();
    void resetTimeRange();
    
protected:
    void onThemeChanged() override;
    
private slots:
    void onTimeRangeChanged();
    void onViewTabChanged(int index);
    void onRefreshClicked();
    void onExportClicked();
    void onSettingsClicked();
    void onChartDataPointClicked(const StatDataPoint& point);
    void updateRealtimeMetrics();
    
private:
    void setupUI();
    void createToolBar();
    void createViews();
    void connectSignals();
    void loadSettings();
    void saveSettings();
    void calculateStatistics();
    void updateAllCharts();
    
    // Data
    QList<StatDataPoint> dataPoints_;
    QDateTime startTime_;
    QDateTime endTime_;
    bool realtimeEnabled_ = false;
    
    // UI elements
    QToolBar* toolBar_ = nullptr;
    QTabWidget* viewTabs_ = nullptr;
    
    // Summary view
    StatsSummaryWidget* summaryWidget_ = nullptr;
    QTableWidget* detailsTable_ = nullptr;
    
    // Charts
    MessageStatsChart* messageChart_ = nullptr;
    ToolUsageChart* toolChart_ = nullptr;
    PerformanceChart* performanceChart_ = nullptr;
    TokenUsageChart* tokenChart_ = nullptr;
    MemoryAnalysisChart* memoryChart_ = nullptr;
    
    // Real-time view
    RealtimeMetricsWidget* realtimeWidget_ = nullptr;
    
    // Comparison view
    HistoricalComparisonWidget* comparisonWidget_ = nullptr;
    
    // Controls
    QDateTimeEdit* startDateEdit_ = nullptr;
    QDateTimeEdit* endDateEdit_ = nullptr;
    QComboBox* presetCombo_ = nullptr;
    QCheckBox* autoRefreshCheck_ = nullptr;
    QSpinBox* refreshIntervalSpin_ = nullptr;
    
    // Actions
    QAction* refreshAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* settingsAction_ = nullptr;
    QAction* realtimeAction_ = nullptr;
    
    // Timers
    QTimer* refreshTimer_ = nullptr;
    QTimer* realtimeTimer_ = nullptr;
    
    // Statistics cache
    QJsonObject cachedStats_;
    QDateTime lastUpdate_;
    
    // Custom metrics
    QHash<QString, double> customMetrics_;
};

// Statistics settings dialog
class StatsSettingsDialog : public QDialog {
    Q_OBJECT
    
public:
    explicit StatsSettingsDialog(QWidget* parent = nullptr);
    
    void setAutoRefreshEnabled(bool enabled);
    bool isAutoRefreshEnabled() const;
    
    void setRefreshInterval(int seconds);
    int refreshInterval() const;
    
    void setDefaultTimeRange(const QString& range);
    QString defaultTimeRange() const;
    
    void setChartAnimationsEnabled(bool enabled);
    bool chartAnimationsEnabled() const;
    
    void setExportFormats(const QStringList& formats);
    QStringList exportFormats() const;
    
private:
    void setupUI();
    
    QCheckBox* autoRefreshCheck_ = nullptr;
    QSpinBox* refreshIntervalSpin_ = nullptr;
    QComboBox* timeRangeCombo_ = nullptr;
    QCheckBox* animationsCheck_ = nullptr;
    QListWidget* exportFormatsList_ = nullptr;
};

} // namespace llm_re::ui_v2