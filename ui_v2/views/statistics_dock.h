#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"
#include "../widgets/charts/line_chart.h"
#include "../widgets/charts/circular_chart.h"
#include "../widgets/charts/bar_chart.h"
#include "../widgets/charts/heatmap_widget.h"
#include "../widgets/charts/sparkline_widget.h"

namespace llm_re::ui_v2 {

// Forward declarations
class RealtimeMetricsWidget;
class HistoricalComparisonWidget;
class StatsSummaryWidget;

// Statistics data point (reuse existing)
struct StatDataPoint {
    QDateTime timestamp;
    QString category;
    QString subcategory;
    double value;
    QJsonObject metadata;
};

// Main statistics dock using custom charts
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
    void onSettingsClicked();
    void onChartDataPointClicked(int seriesIndex, int pointIndex);
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
    
    // Chart creation methods
    void createMessageStatsChart();
    void createTokenUsageChart();
    void createToolUsageChart();
    void createPerformanceChart();
    void createMemoryAnalysisChart();
    
    // Data processing
    void processMessageStats();
    void processTokenUsage();
    void processToolUsage();
    void processPerformance();
    void processMemoryAnalysis();
    
    // Theme helpers
    QList<QColor> getChartSeriesColors() const;
    QColor getMetricColor(const QString& metricType) const;
    QColor getMetricRangeColor(double normalizedValue) const;
    
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
    
    // Custom Charts
    charts::LineChart* messageChart_ = nullptr;
    charts::CircularChart* tokenUsageChart_ = nullptr;
    charts::BarChart* toolUsageChart_ = nullptr;
    charts::LineChart* performanceChart_ = nullptr;
    charts::HeatmapWidget* memoryAnalysisChart_ = nullptr;
    
    // Sparklines for quick stats
    charts::SparklineWidget* cpuSparkline_ = nullptr;
    charts::SparklineWidget* memorySparkline_ = nullptr;
    charts::SparklineWidget* tokenRateSparkline_ = nullptr;
    
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

// Summary widget with custom visualization
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
    struct StatCard {
        QString name;
        QString value;
        QString subtitle;
        QString icon;
        QColor color;
        charts::SparklineWidget* sparkline = nullptr;
        QRectF rect;
        bool isCustom = false;
        
        // Animation
        float animationProgress = 0.0f;
        QString previousValue;
    };
    
    void layoutCards();
    void drawCard(QPainter* painter, const StatCard& card);
    void animateValueChange(StatCard& card, const QString& newValue);
    
    QList<StatCard> cards_;
    int hoveredCard_ = -1;
    int columns_ = 4;
    int cardHeight_ = 120;
    int cardSpacing_ = 16;
    
    // Animation
    QTimer* animationTimer_ = nullptr;
};

// Real-time metrics with live charts
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
    
private:
    struct Metric {
        QString name;
        QString unit;
        double value = 0;
        double min = 0;
        double max = 100;
        charts::LineChart* chart = nullptr;
        charts::SparklineWidget* sparkline = nullptr;
        QLabel* valueLabel = nullptr;
    };
    
    void setupMetricUI(Metric& metric);
    void updateMetricDisplay(Metric& metric);
    
    QHash<QString, Metric> metrics_;
    QTimer* updateTimer_ = nullptr;
    int updateInterval_ = 1000;
    int historySize_ = 60;
    bool isRunning_ = false;
    
    QGridLayout* metricsLayout_ = nullptr;
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
    
private:
    struct ComparisonCard {
        QString metric;
        double currentValue = 0;
        double previousValue = 0;
        double change = 0;
        double changePercent = 0;
        QString trend; // up, down, stable
        charts::BarChart* comparisonChart = nullptr;
        charts::SparklineWidget* trendLine = nullptr;
    };
    
    void createComparisonCard(const QString& metric);
    void updateComparisonCard(ComparisonCard& card);
    void calculateChanges();
    
    QList<ComparisonCard> cards_;
    QDateTime currentStart_;
    QDateTime currentEnd_;
    QDateTime comparisonStart_;
    QDateTime comparisonEnd_;
    QString comparisonType_ = "previous";
    
    QVBoxLayout* cardsLayout_ = nullptr;
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
    
private:
    void setupUI();
    
    QCheckBox* autoRefreshCheck_ = nullptr;
    QSpinBox* refreshIntervalSpin_ = nullptr;
    QComboBox* timeRangeCombo_ = nullptr;
    QCheckBox* animationsCheck_ = nullptr;
};

} // namespace llm_re::ui_v2