#include "statistics_dock.h"
#include "../core/theme_manager.h"
#include "../core/ui_constants.h"
#include "../core/ui_utils.h"
#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QAreaSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QPieSeries>
#include <QtCharts/QPieSlice>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QLegend>
#include <QToolBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTableWidget>
#include <QTreeWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QDateTimeEdit>
#include <QTimer>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QPainter>
#include <QMouseEvent>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSettings>
#include <QListWidget>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QtMath>
#include <algorithm>

QT_CHARTS_USE_NAMESPACE

namespace llm_re::ui_v2 {

// BaseChartWidget implementation

BaseChartWidget::BaseChartWidget(QWidget* parent)
    : BaseStyledWidget(parent)
{
    setupChart();
}

BaseChartWidget::~BaseChartWidget() = default;

void BaseChartWidget::setupChart() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    
    chart_ = new QChart();
    chart_->setAnimationOptions(animated_ ? QChart::AllAnimations : QChart::NoAnimation);
    
    chartView_ = new QChartView(chart_, this);
    chartView_->setRenderHint(QPainter::Antialiasing);
    
    layout->addWidget(chartView_);
    
    applyChartTheme();
}

void BaseChartWidget::setTitle(const QString& title) {
    chart_->setTitle(title);
}

void BaseChartWidget::setAnimated(bool animated) {
    animated_ = animated;
    chart_->setAnimationOptions(animated_ ? QChart::AllAnimations : QChart::NoAnimation);
}

void BaseChartWidget::setTheme(const QString& theme) {
    chartTheme_ = theme;
    applyChartTheme();
}


void BaseChartWidget::applyChartTheme() {
    if (chartTheme_ == "dark") {
        chart_->setTheme(QChart::ChartThemeDark);
        chart_->setBackgroundBrush(ThemeManager::instance()->color(ThemeManager::Surface));
        chart_->setTitleBrush(ThemeManager::instance()->color(ThemeManager::OnSurface));
        
        auto* legend = chart_->legend();
        if (legend) {
            legend->setLabelColor(ThemeManager::instance()->color(ThemeManager::OnSurface));
        }
    } else {
        chart_->setTheme(QChart::ChartThemeLight);
    }
}

// MessageStatsChart implementation

MessageStatsChart::MessageStatsChart(QWidget* parent)
    : BaseChartWidget(parent)
{
    setTitle(tr("Message Statistics"));
    
    createSeries();
    updateAxes();
}

void MessageStatsChart::setData(const QList<StatDataPoint>& data) {
    data_ = data;
    updateChart();
}

void MessageStatsChart::refresh() {
    updateChart();
}

void MessageStatsChart::setTimeRange(const QDateTime& start, const QDateTime& end) {
    startTime_ = start;
    endTime_ = end;
    
    if (xAxis_) {
        xAxis_->setRange(start, end);
    }
}

void MessageStatsChart::setGroupBy(const QString& groupBy) {
    groupBy_ = groupBy;
    updateChart();
}

void MessageStatsChart::setMetric(const QString& metric) {
    metric_ = metric;
    updateChart();
}

void MessageStatsChart::createSeries() {
    // Create line series for each message type
    userSeries_ = new QLineSeries();
    userSeries_->setName(tr("User"));
    userSeries_->setColor(QColor("#2196F3"));
    
    assistantSeries_ = new QLineSeries();
    assistantSeries_->setName(tr("Assistant"));
    assistantSeries_->setColor(QColor("#4CAF50"));
    
    systemSeries_ = new QLineSeries();
    systemSeries_->setName(tr("System"));
    systemSeries_->setColor(QColor("#FF9800"));
    
    // Create area series for total
    auto* totalSeries = new QLineSeries();
    totalArea_ = new QAreaSeries(totalSeries);
    totalArea_->setName(tr("Total"));
    totalArea_->setColor(QColor("#9E9E9E"));
    totalArea_->setOpacity(0.3);
    
    // Add to chart
    chart_->addSeries(totalArea_);
    chart_->addSeries(userSeries_);
    chart_->addSeries(assistantSeries_);
    chart_->addSeries(systemSeries_);
}

void MessageStatsChart::updateAxes() {
    // Time axis
    xAxis_ = new QDateTimeAxis();
    xAxis_->setFormat("MMM dd hh:mm");
    xAxis_->setTitleText(tr("Time"));
    chart_->addAxis(xAxis_, Qt::AlignBottom);
    
    // Value axis
    yAxis_ = new QValueAxis();
    yAxis_->setTitleText(metric_ == "count" ? tr("Message Count") : 
                        metric_ == "length" ? tr("Message Length") : tr("Token Count"));
    chart_->addAxis(yAxis_, Qt::AlignLeft);
    
    // Attach axes to series
    userSeries_->attachAxis(xAxis_);
    userSeries_->attachAxis(yAxis_);
    assistantSeries_->attachAxis(xAxis_);
    assistantSeries_->attachAxis(yAxis_);
    systemSeries_->attachAxis(xAxis_);
    systemSeries_->attachAxis(yAxis_);
    totalArea_->attachAxis(xAxis_);
    totalArea_->attachAxis(yAxis_);
}

void MessageStatsChart::updateChart() {
    // Clear existing data
    userSeries_->clear();
    assistantSeries_->clear();
    systemSeries_->clear();
    
    // Group data by time period
    QHash<QDateTime, QHash<QString, double>> groupedData;
    
    for (const StatDataPoint& point : data_) {
        if (point.category != "message") continue;
        
        // Round timestamp based on groupBy
        QDateTime groupTime = point.timestamp;
        if (groupBy_ == "hour") {
            groupTime = QDateTime(groupTime.date(), QTime(groupTime.time().hour(), 0));
        } else if (groupBy_ == "day") {
            groupTime = QDateTime(groupTime.date(), QTime(0, 0));
        } else if (groupBy_ == "week") {
            int dayOfWeek = groupTime.date().dayOfWeek();
            groupTime = groupTime.addDays(-dayOfWeek + 1);
            groupTime = QDateTime(groupTime.date(), QTime(0, 0));
        } else if (groupBy_ == "month") {
            groupTime = QDateTime(QDate(groupTime.date().year(), groupTime.date().month(), 1), QTime(0, 0));
        }
        
        double value = point.value;
        if (metric_ == "length") {
            value = point.metadata.value("length").toDouble();
        } else if (metric_ == "tokens") {
            value = point.metadata.value("tokens").toDouble();
        }
        
        groupedData[groupTime][point.subcategory] += value;
    }
    
    // Add data to series
    QList<QDateTime> times = groupedData.keys();
    std::sort(times.begin(), times.end());
    
    double maxValue = 0;
    for (const QDateTime& time : times) {
        double userValue = groupedData[time].value("user", 0);
        double assistantValue = groupedData[time].value("assistant", 0);
        double systemValue = groupedData[time].value("system", 0);
        double totalValue = userValue + assistantValue + systemValue;
        
        userSeries_->append(time.toMSecsSinceEpoch(), userValue);
        assistantSeries_->append(time.toMSecsSinceEpoch(), assistantValue);
        systemSeries_->append(time.toMSecsSinceEpoch(), systemValue);
        
        if (totalArea_->upperSeries()) {
            static_cast<QLineSeries*>(totalArea_->upperSeries())->append(
                time.toMSecsSinceEpoch(), totalValue
            );
        }
        
        maxValue = std::max(maxValue, totalValue);
    }
    
    // Update axes ranges
    if (!times.isEmpty()) {
        xAxis_->setRange(times.first(), times.last());
        yAxis_->setRange(0, maxValue * 1.1);
    }
}

// ToolUsageChart implementation

ToolUsageChart::ToolUsageChart(QWidget* parent)
    : BaseChartWidget(parent)
{
    setTitle(tr("Tool Usage"));
}

void ToolUsageChart::setData(const QList<StatDataPoint>& data) {
    data_ = data;
    updateChart();
}

void ToolUsageChart::refresh() {
    updateChart();
}

void ToolUsageChart::setChartType(const QString& type) {
    chartType_ = type;
    updateChart();
}

void ToolUsageChart::setMetric(const QString& metric) {
    metric_ = metric;
    updateChart();
}

void ToolUsageChart::setTopN(int n) {
    topN_ = n;
    updateChart();
}

void ToolUsageChart::updateChart() {
    // Clear existing series
    chart_->removeAllSeries();
    
    if (chartType_ == "bar") {
        createBarChart();
    } else if (chartType_ == "pie") {
        createPieChart();
    } else if (chartType_ == "stacked") {
        createStackedChart();
    }
}

void ToolUsageChart::createBarChart() {
    // Aggregate data by tool
    QHash<QString, double> toolData;
    
    for (const StatDataPoint& point : data_) {
        if (point.category != "tool") continue;
        
        double value = point.value;
        if (metric_ == "duration") {
            value = point.metadata.value("duration").toDouble();
        } else if (metric_ == "success_rate") {
            // Calculate success rate
            bool success = point.metadata.value("success").toBool();
            if (toolData.contains(point.subcategory)) {
                // Running average
                double current = toolData[point.subcategory];
                toolData[point.subcategory] = (current + (success ? 100 : 0)) / 2;
            } else {
                toolData[point.subcategory] = success ? 100 : 0;
            }
            continue;
        }
        
        toolData[point.subcategory] += value;
    }
    
    // Sort by value and take top N
    QList<QPair<QString, double>> sortedData;
    for (auto it = toolData.begin(); it != toolData.end(); ++it) {
        sortedData.append({it.key(), it.value()});
    }
    
    std::sort(sortedData.begin(), sortedData.end(),
             [](const auto& a, const auto& b) { return a.second > b.second; });
    
    if (sortedData.size() > topN_) {
        sortedData = sortedData.mid(0, topN_);
    }
    
    // Create bar series
    barSeries_ = new QBarSeries();
    auto* set = new QBarSet(metric_);
    
    QStringList categories;
    for (const auto& item : sortedData) {
        categories << item.first;
        *set << item.second;
    }
    
    barSeries_->append(set);
    chart_->addSeries(barSeries_);
    
    // Create axes
    auto* axisX = new QBarCategoryAxis();
    axisX->append(categories);
    chart_->addAxis(axisX, Qt::AlignBottom);
    barSeries_->attachAxis(axisX);
    
    auto* axisY = new QValueAxis();
    axisY->setTitleText(metric_ == "count" ? tr("Count") :
                       metric_ == "duration" ? tr("Duration (ms)") :
                       tr("Success Rate (%)"));
    chart_->addAxis(axisY, Qt::AlignLeft);
    barSeries_->attachAxis(axisY);
}

void ToolUsageChart::createPieChart() {
    // Aggregate data by tool
    QHash<QString, double> toolData;
    
    for (const StatDataPoint& point : data_) {
        if (point.category != "tool") continue;
        
        double value = point.value;
        if (metric_ == "duration") {
            value = point.metadata.value("duration").toDouble();
        }
        
        toolData[point.subcategory] += value;
    }
    
    // Create pie series
    pieSeries_ = new QPieSeries();
    
    // Sort and take top N
    QList<QPair<QString, double>> sortedData;
    double total = 0;
    for (auto it = toolData.begin(); it != toolData.end(); ++it) {
        sortedData.append({it.key(), it.value()});
        total += it.value();
    }
    
    std::sort(sortedData.begin(), sortedData.end(),
             [](const auto& a, const auto& b) { return a.second > b.second; });
    
    double otherValue = 0;
    for (int i = 0; i < sortedData.size(); ++i) {
        if (i < topN_) {
            auto* slice = pieSeries_->append(sortedData[i].first, sortedData[i].second);
            slice->setLabelVisible(true);
            slice->setLabel(QString("%1 (%2%)").arg(sortedData[i].first)
                          .arg(sortedData[i].second / total * 100, 0, 'f', 1));
        } else {
            otherValue += sortedData[i].second;
        }
    }
    
    if (otherValue > 0) {
        auto* slice = pieSeries_->append(tr("Other"), otherValue);
        slice->setLabelVisible(true);
        slice->setLabel(QString("Other (%1%)").arg(otherValue / total * 100, 0, 'f', 1));
    }
    
    chart_->addSeries(pieSeries_);
}

void ToolUsageChart::createStackedChart() {
    // Group data by time and tool
    QHash<QDateTime, QHash<QString, double>> timeData;
    QSet<QString> allTools;
    
    for (const StatDataPoint& point : data_) {
        if (point.category != "tool") continue;
        
        // Round to hour
        QDateTime time(point.timestamp.date(), QTime(point.timestamp.time().hour(), 0));
        
        double value = point.value;
        if (metric_ == "duration") {
            value = point.metadata.value("duration").toDouble();
        }
        
        timeData[time][point.subcategory] += value;
        allTools.insert(point.subcategory);
    }
    
    // Create stacked bar series
    auto* series = new QBarSeries();
    
    // Create bar sets for each tool
    QHash<QString, QBarSet*> toolSets;
    for (const QString& tool : allTools) {
        auto* set = new QBarSet(tool);
        toolSets[tool] = set;
        series->append(set);
    }
    
    // Add data
    QList<QDateTime> times = timeData.keys();
    std::sort(times.begin(), times.end());
    
    QStringList categories;
    for (const QDateTime& time : times) {
        categories << time.toString("MM/dd hh:00");
        
        for (const QString& tool : allTools) {
            *toolSets[tool] << timeData[time].value(tool, 0);
        }
    }
    
    series->setLabelsVisible(true);
    chart_->addSeries(series);
    
    // Create axes
    auto* axisX = new QBarCategoryAxis();
    axisX->append(categories);
    chart_->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);
    
    auto* axisY = new QValueAxis();
    axisY->setTitleText(metric_ == "count" ? tr("Count") : tr("Duration (ms)"));
    chart_->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);
}

// PerformanceChart implementation

PerformanceChart::PerformanceChart(QWidget* parent)
    : BaseChartWidget(parent)
{
    setTitle(tr("Performance Metrics"));
    
    // Create time axis
    timeAxis_ = new QDateTimeAxis();
    timeAxis_->setFormat("hh:mm:ss");
    timeAxis_->setTitleText(tr("Time"));
    chart_->addAxis(timeAxis_, Qt::AlignBottom);
}

void PerformanceChart::setData(const QList<StatDataPoint>& data) {
    data_ = data;
    updateChart();
}

void PerformanceChart::refresh() {
    updateChart();
}

void PerformanceChart::addMetric(const QString& name, const QString& unit) {
    if (!metrics_.contains(name)) {
        MetricInfo info;
        info.name = name;
        info.unit = unit;
        info.series = new QLineSeries();
        info.series->setName(name);
        
        // Create Y axis for this metric
        info.axis = new QValueAxis();
        info.axis->setTitleText(QString("%1 (%2)").arg(name).arg(unit));
        
        metrics_[name] = info;
        
        chart_->addSeries(info.series);
        chart_->addAxis(info.axis, metrics_.size() % 2 == 0 ? Qt::AlignLeft : Qt::AlignRight);
        
        info.series->attachAxis(timeAxis_);
        info.series->attachAxis(info.axis);
    }
}

void PerformanceChart::removeMetric(const QString& name) {
    if (metrics_.contains(name)) {
        MetricInfo info = metrics_.take(name);
        
        chart_->removeSeries(info.series);
        chart_->removeAxis(info.axis);
        
        delete info.series;
        delete info.axis;
    }
}

void PerformanceChart::setMetricVisible(const QString& name, bool visible) {
    if (metrics_.contains(name)) {
        metrics_[name].visible = visible;
        metrics_[name].series->setVisible(visible);
    }
}

void PerformanceChart::updateChart() {
    // Clear all series
    for (auto& info : metrics_) {
        info.series->clear();
    }
    
    // Group data by metric
    QHash<QString, QList<QPair<QDateTime, double>>> metricData;
    
    for (const StatDataPoint& point : data_) {
        if (point.category == "performance") {
            metricData[point.subcategory].append({point.timestamp, point.value});
        }
    }
    
    // Update series
    for (auto it = metricData.begin(); it != metricData.end(); ++it) {
        if (metrics_.contains(it.key())) {
            MetricInfo& info = metrics_[it.key()];
            
            // Sort by time
            auto& points = it.value();
            std::sort(points.begin(), points.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
            
            // Add to series
            double minValue = std::numeric_limits<double>::max();
            double maxValue = std::numeric_limits<double>::min();
            
            for (const auto& point : points) {
                info.series->append(point.first.toMSecsSinceEpoch(), point.second);
                minValue = std::min(minValue, point.second);
                maxValue = std::max(maxValue, point.second);
            }
            
            // Update axis range
            if (info.axis && minValue < maxValue) {
                info.axis->setRange(minValue * 0.9, maxValue * 1.1);
            }
        }
    }
    
    // Update time axis range
    if (!data_.isEmpty()) {
        auto minTime = std::min_element(data_.begin(), data_.end(),
            [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
        auto maxTime = std::max_element(data_.begin(), data_.end(),
            [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
        
        timeAxis_->setRange(minTime->timestamp, maxTime->timestamp);
    }
}

// TokenUsageChart implementation

TokenUsageChart::TokenUsageChart(QWidget* parent)
    : BaseChartWidget(parent)
{
    setTitle(tr("Token Usage"));
    
    // Create series
    inputTokensSeries_ = new QLineSeries();
    inputTokensSeries_->setName(tr("Input Tokens"));
    inputTokensSeries_->setColor(QColor("#2196F3"));
    
    outputTokensSeries_ = new QLineSeries();
    outputTokensSeries_->setName(tr("Output Tokens"));
    outputTokensSeries_->setColor(QColor("#4CAF50"));
    
    totalTokensSeries_ = new QLineSeries();
    totalTokensSeries_->setName(tr("Total Tokens"));
    totalTokensSeries_->setColor(QColor("#FF9800"));
    
    cumulativeSeries_ = new QLineSeries();
    cumulativeSeries_->setName(tr("Cumulative"));
    cumulativeSeries_->setColor(QColor("#9C27B0"));
    
    costSeries_ = new QLineSeries();
    costSeries_->setName(tr("Cost"));
    costSeries_->setColor(QColor("#F44336"));
    
    chart_->addSeries(inputTokensSeries_);
    chart_->addSeries(outputTokensSeries_);
    chart_->addSeries(totalTokensSeries_);
    chart_->addSeries(cumulativeSeries_);
    chart_->addSeries(costSeries_);
    
    // Create axes
    auto* xAxis = new QDateTimeAxis();
    xAxis->setFormat("hh:mm");
    xAxis->setTitleText(tr("Time"));
    chart_->addAxis(xAxis, Qt::AlignBottom);
    
    auto* yAxis = new QValueAxis();
    yAxis->setTitleText(tr("Tokens"));
    chart_->addAxis(yAxis, Qt::AlignLeft);
    
    auto* costAxis = new QValueAxis();
    costAxis->setTitleText(tr("Cost ($)"));
    chart_->addAxis(costAxis, Qt::AlignRight);
    
    // Attach series to axes
    inputTokensSeries_->attachAxis(xAxis);
    inputTokensSeries_->attachAxis(yAxis);
    outputTokensSeries_->attachAxis(xAxis);
    outputTokensSeries_->attachAxis(yAxis);
    totalTokensSeries_->attachAxis(xAxis);
    totalTokensSeries_->attachAxis(yAxis);
    cumulativeSeries_->attachAxis(xAxis);
    cumulativeSeries_->attachAxis(yAxis);
    costSeries_->attachAxis(xAxis);
    costSeries_->attachAxis(costAxis);
}

void TokenUsageChart::setData(const QList<StatDataPoint>& data) {
    data_ = data;
    updateChart();
}

void TokenUsageChart::refresh() {
    updateChart();
}

void TokenUsageChart::setModel(const QString& model) {
    model_ = model;
    updateChart();
}

void TokenUsageChart::setCostPerToken(double cost) {
    costPerToken_ = cost;
    updateChart();
}

void TokenUsageChart::setShowCost(bool show) {
    showCost_ = show;
    costSeries_->setVisible(show);
}

void TokenUsageChart::setShowCumulative(bool show) {
    showCumulative_ = show;
    cumulativeSeries_->setVisible(show);
}

void TokenUsageChart::updateChart() {
    // Clear series
    inputTokensSeries_->clear();
    outputTokensSeries_->clear();
    totalTokensSeries_->clear();
    cumulativeSeries_->clear();
    costSeries_->clear();
    
    // Filter and sort data
    QList<StatDataPoint> tokenData;
    for (const StatDataPoint& point : data_) {
        if (point.category == "tokens") {
            tokenData.append(point);
        }
    }
    
    std::sort(tokenData.begin(), tokenData.end(),
             [](const auto& a, const auto& b) { return a.timestamp < b.timestamp; });
    
    // Process data
    double cumulative = 0;
    double cumulativeCost = 0;
    
    for (const StatDataPoint& point : tokenData) {
        qint64 time = point.timestamp.toMSecsSinceEpoch();
        
        if (point.subcategory == "input") {
            inputTokensSeries_->append(time, point.value);
        } else if (point.subcategory == "output") {
            outputTokensSeries_->append(time, point.value);
        } else if (point.subcategory == "total") {
            totalTokensSeries_->append(time, point.value);
            cumulative += point.value;
            cumulativeSeries_->append(time, cumulative);
            
            double cost = point.value * costPerToken_;
            cumulativeCost += cost;
            costSeries_->append(time, cumulativeCost);
        }
    }
}

// MemoryAnalysisChart implementation

MemoryAnalysisChart::MemoryAnalysisChart(QWidget* parent)
    : BaseChartWidget(parent)
{
    setTitle(tr("Memory Analysis"));
}

void MemoryAnalysisChart::setData(const QList<StatDataPoint>& data) {
    data_ = data;
    updateChart();
}

void MemoryAnalysisChart::refresh() {
    updateChart();
}

void MemoryAnalysisChart::setAnalysisType(const QString& type) {
    analysisType_ = type;
    updateChart();
}

void MemoryAnalysisChart::setGroupBy(const QString& groupBy) {
    groupBy_ = groupBy;
    updateChart();
}

void MemoryAnalysisChart::updateChart() {
    // Clear existing series
    chart_->removeAllSeries();
    
    if (analysisType_ == "coverage") {
        // Create scatter plot for coverage
        scatterSeries_ = new QScatterSeries();
        scatterSeries_->setName(tr("Coverage"));
        scatterSeries_->setMarkerSize(10);
        
        for (const StatDataPoint& point : data_) {
            if (point.category == "memory" && point.subcategory == "coverage") {
                double x = point.metadata.value("address").toDouble();
                double y = point.value; // Coverage percentage
                scatterSeries_->append(x, y);
            }
        }
        
        chart_->addSeries(scatterSeries_);
        
        // Create axes
        auto* xAxis = new QValueAxis();
        xAxis->setTitleText(tr("Address"));
        chart_->addAxis(xAxis, Qt::AlignBottom);
        scatterSeries_->attachAxis(xAxis);
        
        auto* yAxis = new QValueAxis();
        yAxis->setTitleText(tr("Coverage (%)"));
        yAxis->setRange(0, 100);
        chart_->addAxis(yAxis, Qt::AlignLeft);
        scatterSeries_->attachAxis(yAxis);
        
    } else if (analysisType_ == "confidence") {
        // Create bar chart for confidence by group
        barSeries_ = new QBarSeries();
        
        // Group data
        QHash<QString, QList<double>> groupedData;
        
        for (const StatDataPoint& point : data_) {
            if (point.category == "memory" && point.subcategory == "confidence") {
                QString group;
                if (groupBy_ == "module") {
                    group = point.metadata.value("module").toString();
                } else if (groupBy_ == "function") {
                    group = point.metadata.value("function").toString();
                }
                
                if (!group.isEmpty()) {
                    groupedData[group].append(point.value);
                }
            }
        }
        
        // Calculate average confidence per group
        auto* set = new QBarSet(tr("Average Confidence"));
        QStringList categories;
        
        for (auto it = groupedData.begin(); it != groupedData.end(); ++it) {
            double sum = 0;
            for (double val : it.value()) {
                sum += val;
            }
            double avg = sum / it.value().size();
            
            categories << it.key();
            *set << avg;
        }
        
        barSeries_->append(set);
        chart_->addSeries(barSeries_);
        
        // Create axes
        auto* xAxis = new QBarCategoryAxis();
        xAxis->append(categories);
        chart_->addAxis(xAxis, Qt::AlignBottom);
        barSeries_->attachAxis(xAxis);
        
        auto* yAxis = new QValueAxis();
        yAxis->setTitleText(tr("Confidence (%)"));
        yAxis->setRange(0, 100);
        chart_->addAxis(yAxis, Qt::AlignLeft);
        barSeries_->attachAxis(yAxis);
        
    } else if (analysisType_ == "complexity") {
        // Create scatter plot for complexity
        scatterSeries_ = new QScatterSeries();
        scatterSeries_->setName(tr("Complexity"));
        scatterSeries_->setMarkerSize(8);
        
        for (const StatDataPoint& point : data_) {
            if (point.category == "memory" && point.subcategory == "complexity") {
                double size = point.metadata.value("size").toDouble();
                double complexity = point.value;
                scatterSeries_->append(size, complexity);
            }
        }
        
        chart_->addSeries(scatterSeries_);
        
        // Create axes
        auto* xAxis = new QValueAxis();
        xAxis->setTitleText(tr("Function Size"));
        chart_->addAxis(xAxis, Qt::AlignBottom);
        scatterSeries_->attachAxis(xAxis);
        
        auto* yAxis = new QValueAxis();
        yAxis->setTitleText(tr("Cyclomatic Complexity"));
        chart_->addAxis(yAxis, Qt::AlignLeft);
        scatterSeries_->attachAxis(yAxis);
    }
}

// StatsSummaryWidget implementation

StatsSummaryWidget::StatsSummaryWidget(QWidget* parent)
    : BaseStyledWidget(parent)
{
    setMouseTracking(true);
}

void StatsSummaryWidget::updateStats(const QJsonObject& stats) {
    stats_.clear();
    
    // Add standard stats
    StatItem totalMessages;
    totalMessages.name = tr("Total Messages");
    totalMessages.value = QString::number(stats.value("totalMessages").toInt());
    totalMessages.icon = "message";
    totalMessages.tooltip = tr("Total number of messages in conversation");
    stats_.append(totalMessages);
    
    StatItem activeTools;
    activeTools.name = tr("Tools Used");
    activeTools.value = QString::number(stats.value("toolsUsed").toInt());
    activeTools.icon = "tools";
    activeTools.tooltip = tr("Number of different tools executed");
    stats_.append(activeTools);
    
    StatItem successRate;
    successRate.name = tr("Success Rate");
    successRate.value = QString("%1%").arg(stats.value("successRate").toDouble(), 0, 'f', 1);
    successRate.icon = "check-circle";
    successRate.tooltip = tr("Percentage of successful tool executions");
    stats_.append(successRate);
    
    StatItem avgResponseTime;
    avgResponseTime.name = tr("Avg Response Time");
    avgResponseTime.value = QString("%1s").arg(stats.value("avgResponseTime").toDouble(), 0, 'f', 1);
    avgResponseTime.icon = "clock";
    avgResponseTime.tooltip = tr("Average time to generate response");
    stats_.append(avgResponseTime);
    
    StatItem totalTokens;
    totalTokens.name = tr("Total Tokens");
    totalTokens.value = UIUtils::formatNumber(stats.value("totalTokens").toInt());
    totalTokens.icon = "token";
    totalTokens.tooltip = tr("Total tokens consumed");
    stats_.append(totalTokens);
    
    StatItem memoryEntries;
    memoryEntries.name = tr("Memory Entries");
    memoryEntries.value = QString::number(stats.value("memoryEntries").toInt());
    memoryEntries.icon = "memory";
    memoryEntries.tooltip = tr("Number of analyzed memory locations");
    stats_.append(memoryEntries);
    
    layoutStats();
    update();
}

void StatsSummaryWidget::setTimeRange(const QDateTime& start, const QDateTime& end) {
    Q_UNUSED(start);
    Q_UNUSED(end);
    layoutStats();
    update();
}

void StatsSummaryWidget::addCustomStat(const QString& name, const QString& value, const QString& icon) {
    StatItem item;
    item.name = name;
    item.value = value;
    item.icon = icon;
    item.isCustom = true;
    stats_.append(item);
    
    layoutStats();
    update();
}

void StatsSummaryWidget::clearCustomStats() {
    stats_.erase(std::remove_if(stats_.begin(), stats_.end(),
                               [](const StatItem& item) { return item.isCustom; }),
                 stats_.end());
    
    layoutStats();
    update();
}

void StatsSummaryWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    for (int i = 0; i < stats_.size(); ++i) {
        const StatItem& stat = stats_[i];
        
        // Background
        QColor bgColor = ThemeManager::instance()->color(ThemeManager::SurfaceVariant);
        if (i == hoveredStat_) {
            bgColor = bgColor.lighter(110);
        }
        
        painter.fillRect(stat.rect, bgColor);
        
        // Border
        painter.setPen(ThemeManager::instance()->color(ThemeManager::Outline));
        painter.drawRect(stat.rect);
        
        // Icon
        if (!stat.icon.isEmpty()) {
            QRectF iconRect(stat.rect.x() + 16, stat.rect.y() + 16, 24, 24);
            QIcon icon = UIUtils::icon(stat.icon);
            icon.paint(&painter, iconRect.toRect());
        }
        
        // Name
        painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurfaceVariant));
        painter.setFont(QFont("Sans", 9));
        QRectF nameRect(stat.rect.x() + 16, stat.rect.y() + 48, 
                       stat.rect.width() - 32, 20);
        painter.drawText(nameRect, Qt::AlignCenter, stat.name);
        
        // Value
        painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
        QFont valueFont("Sans", 18);
        valueFont.setWeight(QFont::DemiBold);
        painter.setFont(valueFont);
        QRectF valueRect(stat.rect.x() + 16, stat.rect.y() + 20,
                        stat.rect.width() - 32, 40);
        painter.drawText(valueRect, Qt::AlignCenter, stat.value);
    }
    
    // Tooltip
    if (hoveredStat_ >= 0 && hoveredStat_ < stats_.size()) {
        const StatItem& stat = stats_[hoveredStat_];
        if (!stat.tooltip.isEmpty()) {
            QFontMetrics fm(painter.font());
            QRect tooltipRect = fm.boundingRect(stat.tooltip);
            tooltipRect.adjust(-5, -5, 5, 5);
            tooltipRect.moveTopLeft(QCursor::pos() - mapToGlobal(QPoint(0, 0)) + QPoint(10, 10));
            
            painter.fillRect(tooltipRect, ThemeManager::instance()->color(ThemeManager::Surface));
            painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
            painter.setFont(QFont("Sans", 9));
            painter.drawText(tooltipRect, Qt::AlignCenter, stat.tooltip);
        }
    }
}

void StatsSummaryWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        for (int i = 0; i < stats_.size(); ++i) {
            if (stats_[i].rect.contains(event->pos())) {
                emit statClicked(stats_[i].name);
                break;
            }
        }
    }
}

void StatsSummaryWidget::resizeEvent(QResizeEvent* event) {
    BaseStyledWidget::resizeEvent(event);
    layoutStats();
}

void StatsSummaryWidget::layoutStats() {
    if (stats_.isEmpty()) return;
    
    // Calculate grid layout
    int totalWidth = width() - spacing_ * 2;
    int cardWidth = (totalWidth - spacing_ * (columns_ - 1)) / columns_;
    
    int row = 0;
    int col = 0;
    
    for (int i = 0; i < stats_.size(); ++i) {
        int x = spacing_ + col * (cardWidth + spacing_);
        int y = spacing_ + row * (cardHeight_ + spacing_);
        
        stats_[i].rect = QRectF(x, y, cardWidth, cardHeight_);
        
        col++;
        if (col >= columns_) {
            col = 0;
            row++;
        }
    }
    
    // Update widget height
    int rows = (stats_.size() + columns_ - 1) / columns_;
    setMinimumHeight(rows * (cardHeight_ + spacing_) + spacing_);
}

// RealtimeMetricsWidget implementation

RealtimeMetricsWidget::RealtimeMetricsWidget(QWidget* parent)
    : BaseStyledWidget(parent)
{
    updateTimer_ = new QTimer(this);
    connect(updateTimer_, &QTimer::timeout, this, &RealtimeMetricsWidget::updateDisplay);
}

RealtimeMetricsWidget::~RealtimeMetricsWidget() {
    stop();
}

void RealtimeMetricsWidget::addMetric(const QString& name, const QString& unit, double min, double max) {
    Metric metric;
    metric.name = name;
    metric.unit = unit;
    metric.min = min;
    metric.max = max;
    
    // Assign color
    static QList<QColor> colors = {
        "#2196F3", "#4CAF50", "#FF9800", "#F44336", "#9C27B0",
        "#00BCD4", "#8BC34A", "#FFC107", "#E91E63", "#3F51B5"
    };
    metric.color = colors[metrics_.size() % colors.size()];
    
    metrics_[name] = metric;
    update();
}

void RealtimeMetricsWidget::updateMetric(const QString& name, double value) {
    if (metrics_.contains(name)) {
        Metric& metric = metrics_[name];
        metric.value = value;
        
        // Add to history
        metric.history.append(value);
        if (metric.history.size() > historySize_) {
            metric.history.removeFirst();
        }
        
        emit metricUpdated(name, value);
        
        if (!updateTimer_->isActive()) {
            update();
        }
    }
}

void RealtimeMetricsWidget::removeMetric(const QString& name) {
    metrics_.remove(name);
    update();
}

void RealtimeMetricsWidget::setUpdateInterval(int ms) {
    updateInterval_ = ms;
    if (updateTimer_->isActive()) {
        updateTimer_->setInterval(ms);
    }
}

void RealtimeMetricsWidget::setHistorySize(int size) {
    historySize_ = size;
    
    // Trim existing histories
    for (auto& metric : metrics_) {
        while (metric.history.size() > historySize_) {
            metric.history.removeFirst();
        }
    }
}

void RealtimeMetricsWidget::start() {
    if (!isRunning_) {
        isRunning_ = true;
        updateTimer_->start(updateInterval_);
    }
}

void RealtimeMetricsWidget::stop() {
    if (isRunning_) {
        isRunning_ = false;
        updateTimer_->stop();
    }
}

void RealtimeMetricsWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    if (metrics_.isEmpty()) {
        painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurfaceVariant));
        painter.drawText(rect(), Qt::AlignCenter, tr("No metrics configured"));
        return;
    }
    
    // Calculate layout
    int metricHeight = height() / metrics_.size();
    int y = 0;
    
    for (auto it = metrics_.begin(); it != metrics_.end(); ++it) {
        QRectF metricRect(0, y, width(), metricHeight);
        drawMetric(&painter, it.value(), metricRect);
        y += metricHeight;
    }
}

void RealtimeMetricsWidget::updateDisplay() {
    update();
}

void RealtimeMetricsWidget::drawMetric(QPainter* painter, const Metric& metric, const QRectF& rect) {
    // Background
    painter->fillRect(rect, ThemeManager::instance()->color(ThemeManager::Surface));
    
    // Name and value
    painter->setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
    painter->setFont(QFont("Sans", 10, QFont::DemiBold));
    
    QString text = QString("%1: %2 %3").arg(metric.name)
                                      .arg(metric.value, 0, 'f', 1)
                                      .arg(metric.unit);
    QRectF textRect(rect.x() + 10, rect.y(), rect.width() / 2, 30);
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, text);
    
    // Progress bar
    QRectF barRect(rect.x() + 10, rect.y() + 30, rect.width() - 20, 10);
    painter->fillRect(barRect, ThemeManager::instance()->color(ThemeManager::SurfaceVariant));
    
    double progress = (metric.value - metric.min) / (metric.max - metric.min);
    progress = qBound(0.0, progress, 1.0);
    
    QRectF fillRect = barRect;
    fillRect.setWidth(barRect.width() * progress);
    painter->fillRect(fillRect, metric.color);
    
    // Sparkline
    if (!metric.history.isEmpty()) {
        QRectF sparkRect(rect.x() + rect.width() / 2, rect.y() + 10, 
                        rect.width() / 2 - 20, rect.height() - 20);
        drawSparkline(painter, metric.history, sparkRect);
    }
    
    // Border
    painter->setPen(ThemeManager::instance()->color(ThemeManager::Outline));
    painter->drawRect(rect);
}

void RealtimeMetricsWidget::drawSparkline(QPainter* painter, const QList<double>& values, const QRectF& rect) {
    if (values.size() < 2) return;
    
    // Find min/max
    double min = *std::min_element(values.begin(), values.end());
    double max = *std::max_element(values.begin(), values.end());
    double range = max - min;
    if (range == 0) range = 1;
    
    // Draw line
    QPainterPath path;
    for (int i = 0; i < values.size(); ++i) {
        double x = rect.x() + (rect.width() * i) / (values.size() - 1);
        double y = rect.bottom() - (rect.height() * (values[i] - min)) / range;
        
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    
    painter->setPen(QPen(ThemeManager::instance()->color(ThemeManager::Primary), 2));
    painter->drawPath(path);
}

// HistoricalComparisonWidget implementation

HistoricalComparisonWidget::HistoricalComparisonWidget(QWidget* parent)
    : BaseStyledWidget(parent)
{
}

void HistoricalComparisonWidget::setCurrentPeriod(const QDateTime& start, const QDateTime& end) {
    currentStart_ = start;
    currentEnd_ = end;
}

void HistoricalComparisonWidget::setComparisonPeriod(const QDateTime& start, const QDateTime& end) {
    comparisonStart_ = start;
    comparisonEnd_ = end;
}

void HistoricalComparisonWidget::setMetrics(const QStringList& metrics) {
    metrics_.clear();
    for (const QString& name : metrics) {
        ComparisonMetric metric;
        metric.name = name;
        metrics_.append(metric);
    }
}

void HistoricalComparisonWidget::updateData(const QJsonObject& currentData, const QJsonObject& comparisonData) {
    for (ComparisonMetric& metric : metrics_) {
        metric.currentValue = currentData.value(metric.name).toDouble();
        metric.previousValue = comparisonData.value(metric.name).toDouble();
    }
    
    calculateChanges();
    update();
}

void HistoricalComparisonWidget::setComparisonType(const QString& type) {
    comparisonType_ = type;
}

void HistoricalComparisonWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    if (metrics_.isEmpty()) {
        painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurfaceVariant));
        painter.drawText(rect(), Qt::AlignCenter, tr("No metrics to compare"));
        return;
    }
    
    // Draw period labels
    painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
    painter.setFont(QFont("Sans", 10, QFont::DemiBold));
    
    QString currentLabel = QString("Current: %1 - %2")
        .arg(currentStart_.toString("MMM dd"))
        .arg(currentEnd_.toString("MMM dd"));
    QString comparisonLabel = QString("Previous: %1 - %2")
        .arg(comparisonStart_.toString("MMM dd"))
        .arg(comparisonEnd_.toString("MMM dd"));
    
    painter.drawText(QRect(10, 10, width() - 20, 20), Qt::AlignLeft, currentLabel);
    painter.drawText(QRect(10, 30, width() - 20, 20), Qt::AlignLeft, comparisonLabel);
    
    // Draw metrics
    int y = 60;
    int cardHeight = 80;
    int spacing = 10;
    
    for (const ComparisonMetric& metric : metrics_) {
        QRectF cardRect(10, y, width() - 20, cardHeight);
        drawMetricCard(&painter, metric, cardRect);
        y += cardHeight + spacing;
    }
}

void HistoricalComparisonWidget::calculateChanges() {
    for (ComparisonMetric& metric : metrics_) {
        if (metric.previousValue != 0) {
            metric.change = metric.currentValue - metric.previousValue;
            metric.changePercent = (metric.change / metric.previousValue) * 100;
            
            if (metric.changePercent > 5) {
                metric.trend = "up";
            } else if (metric.changePercent < -5) {
                metric.trend = "down";
            } else {
                metric.trend = "stable";
            }
        } else {
            metric.change = metric.currentValue;
            metric.changePercent = 100;
            metric.trend = "up";
        }
    }
}

void HistoricalComparisonWidget::drawMetricCard(QPainter* painter, const ComparisonMetric& metric, const QRectF& rect) {
    // Background
    painter->fillRect(rect, ThemeManager::instance()->color(ThemeManager::SurfaceVariant));
    
    // Metric name
    painter->setPen(ThemeManager::instance()->color(ThemeManager::OnSurfaceVariant));
    painter->setFont(QFont("Sans", 9));
    painter->drawText(QRectF(rect.x() + 10, rect.y() + 5, rect.width() - 20, 20),
                     Qt::AlignLeft | Qt::AlignVCenter, metric.name);
    
    // Current value
    painter->setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
    painter->setFont(QFont("Sans", 16, QFont::DemiBold));
    painter->drawText(QRectF(rect.x() + 10, rect.y() + 25, rect.width() / 2 - 20, 30),
                     Qt::AlignLeft | Qt::AlignVCenter, QString::number(metric.currentValue, 'f', 0));
    
    // Change indicator
    QColor changeColor;
    QString changeIcon;
    if (metric.trend == "up") {
        changeColor = QColor("#4CAF50");
        changeIcon = "↑";
    } else if (metric.trend == "down") {
        changeColor = QColor("#F44336");
        changeIcon = "↓";
    } else {
        changeColor = ThemeManager::instance()->color(ThemeManager::OnSurfaceVariant);
        changeIcon = "→";
    }
    
    painter->setPen(changeColor);
    painter->setFont(QFont("Sans", 12));
    
    QString changeText = QString("%1 %2 (%3%4%)")
        .arg(changeIcon)
        .arg(qAbs(metric.change), 0, 'f', 0)
        .arg(metric.changePercent > 0 ? "+" : "")
        .arg(metric.changePercent, 0, 'f', 1);
    
    painter->drawText(QRectF(rect.x() + rect.width() / 2, rect.y() + 25, rect.width() / 2 - 10, 30),
                     Qt::AlignRight | Qt::AlignVCenter, changeText);
    
    // Previous value
    painter->setPen(ThemeManager::instance()->color(ThemeManager::OnSurfaceVariant));
    painter->setFont(QFont("Sans", 9));
    painter->drawText(QRectF(rect.x() + 10, rect.y() + 55, rect.width() - 20, 20),
                     Qt::AlignLeft | Qt::AlignVCenter, 
                     QString("Previous: %1").arg(metric.previousValue, 0, 'f', 0));
    
    // Border
    painter->setPen(ThemeManager::instance()->color(ThemeManager::Outline));
    painter->drawRect(rect);
}

// StatisticsDock implementation

StatisticsDock::StatisticsDock(QWidget* parent)
    : BaseStyledWidget(parent)
{
    setupUI();
    connectSignals();
    loadSettings();
    
    // Setup timers
    refreshTimer_ = new QTimer(this);
    connect(refreshTimer_, &QTimer::timeout, this, &StatisticsDock::updateStatistics);
    
    realtimeTimer_ = new QTimer(this);
    realtimeTimer_->setInterval(1000);
    connect(realtimeTimer_, &QTimer::timeout, this, &StatisticsDock::updateRealtimeMetrics);
}

StatisticsDock::~StatisticsDock() {
    saveSettings();
}

void StatisticsDock::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    createToolBar();
    layout->addWidget(toolBar_);
    
    createViews();
    layout->addWidget(viewTabs_);
}

void StatisticsDock::createToolBar() {
    toolBar_ = new QToolBar(this);
    toolBar_->setIconSize(QSize(16, 16));
    
    // Time range controls
    auto* timeLabel = new QLabel(tr("Time Range:"), this);
    toolBar_->addWidget(timeLabel);
    
    startDateEdit_ = new QDateTimeEdit(this);
    startDateEdit_->setCalendarPopup(true);
    startDateEdit_->setDateTime(QDateTime::currentDateTime().addDays(-7));
    toolBar_->addWidget(startDateEdit_);
    
    toolBar_->addWidget(new QLabel(" - ", this));
    
    endDateEdit_ = new QDateTimeEdit(this);
    endDateEdit_->setCalendarPopup(true);
    endDateEdit_->setDateTime(QDateTime::currentDateTime());
    toolBar_->addWidget(endDateEdit_);
    
    // Presets
    presetCombo_ = new QComboBox(this);
    presetCombo_->addItems({tr("Last Hour"), tr("Last 24 Hours"), tr("Last 7 Days"), 
                           tr("Last 30 Days"), tr("Custom")});
    presetCombo_->setCurrentIndex(2); // Last 7 Days
    toolBar_->addWidget(presetCombo_);
    
    toolBar_->addSeparator();
    
    // Auto-refresh
    autoRefreshCheck_ = new QCheckBox(tr("Auto Refresh"), this);
    toolBar_->addWidget(autoRefreshCheck_);
    
    refreshIntervalSpin_ = new QSpinBox(this);
    refreshIntervalSpin_->setRange(5, 300);
    refreshIntervalSpin_->setValue(30);
    refreshIntervalSpin_->setSuffix(tr(" sec"));
    refreshIntervalSpin_->setEnabled(false);
    toolBar_->addWidget(refreshIntervalSpin_);
    
    toolBar_->addSeparator();
    
    // Actions
    refreshAction_ = toolBar_->addAction(UIUtils::icon("view-refresh"), tr("Refresh"));
    refreshAction_->setShortcut(QKeySequence::Refresh);
    
    realtimeAction_ = toolBar_->addAction(UIUtils::icon("media-playback-start"), tr("Real-time"));
    realtimeAction_->setCheckable(true);
    
    
    settingsAction_ = toolBar_->addAction(UIUtils::icon("configure"), tr("Settings"));
}

void StatisticsDock::createViews() {
    viewTabs_ = new QTabWidget(this);
    
    // Summary view
    auto* summaryTab = new QWidget();
    auto* summaryLayout = new QVBoxLayout(summaryTab);
    
    summaryWidget_ = new StatsSummaryWidget(this);
    summaryLayout->addWidget(summaryWidget_);
    
    detailsTable_ = new QTableWidget(this);
    detailsTable_->setAlternatingRowColors(true);
    detailsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    summaryLayout->addWidget(detailsTable_);
    
    viewTabs_->addTab(summaryTab, tr("Summary"));
    
    // Message stats
    messageChart_ = new MessageStatsChart(this);
    viewTabs_->addTab(messageChart_, tr("Messages"));
    
    // Tool usage
    toolChart_ = new ToolUsageChart(this);
    viewTabs_->addTab(toolChart_, tr("Tool Usage"));
    
    // Performance
    performanceChart_ = new PerformanceChart(this);
    performanceChart_->addMetric("Response Time", "ms");
    performanceChart_->addMetric("CPU Usage", "%");
    performanceChart_->addMetric("Memory Usage", "MB");
    viewTabs_->addTab(performanceChart_, tr("Performance"));
    
    // Token usage
    tokenChart_ = new TokenUsageChart(this);
    viewTabs_->addTab(tokenChart_, tr("Tokens"));
    
    // Memory analysis
    memoryChart_ = new MemoryAnalysisChart(this);
    viewTabs_->addTab(memoryChart_, tr("Memory"));
    
    // Real-time metrics
    realtimeWidget_ = new RealtimeMetricsWidget(this);
    realtimeWidget_->addMetric("Messages/min", "msg", 0, 10);
    realtimeWidget_->addMetric("Tokens/min", "tok", 0, 1000);
    realtimeWidget_->addMetric("CPU Usage", "%", 0, 100);
    realtimeWidget_->addMetric("Memory", "MB", 0, 1000);
    viewTabs_->addTab(realtimeWidget_, tr("Real-time"));
    
    // Historical comparison
    comparisonWidget_ = new HistoricalComparisonWidget(this);
    comparisonWidget_->setMetrics({
        "Total Messages", "Tool Executions", "Success Rate",
        "Avg Response Time", "Total Tokens", "Memory Entries"
    });
    viewTabs_->addTab(comparisonWidget_, tr("Comparison"));
}

void StatisticsDock::connectSignals() {
    // Time range
    connect(startDateEdit_, &QDateTimeEdit::dateTimeChanged,
            this, &StatisticsDock::onTimeRangeChanged);
    connect(endDateEdit_, &QDateTimeEdit::dateTimeChanged,
            this, &StatisticsDock::onTimeRangeChanged);
    
    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
        QDateTime now = QDateTime::currentDateTime();
        switch (index) {
        case 0: // Last Hour
            startDateEdit_->setDateTime(now.addSecs(-3600));
            endDateEdit_->setDateTime(now);
            break;
        case 1: // Last 24 Hours
            startDateEdit_->setDateTime(now.addDays(-1));
            endDateEdit_->setDateTime(now);
            break;
        case 2: // Last 7 Days
            startDateEdit_->setDateTime(now.addDays(-7));
            endDateEdit_->setDateTime(now);
            break;
        case 3: // Last 30 Days
            startDateEdit_->setDateTime(now.addDays(-30));
            endDateEdit_->setDateTime(now);
            break;
        }
    });
    
    // Auto-refresh
    connect(autoRefreshCheck_, &QCheckBox::toggled, [this](bool checked) {
        refreshIntervalSpin_->setEnabled(checked);
        if (checked) {
            refreshTimer_->start(refreshIntervalSpin_->value() * 1000);
        } else {
            refreshTimer_->stop();
        }
    });
    
    connect(refreshIntervalSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int value) {
        if (refreshTimer_->isActive()) {
            refreshTimer_->setInterval(value * 1000);
        }
    });
    
    // Actions
    connect(refreshAction_, &QAction::triggered,
            this, &StatisticsDock::updateStatistics);
    
    connect(realtimeAction_, &QAction::toggled,
            this, &StatisticsDock::setRealtimeEnabled);
    
    
    connect(settingsAction_, &QAction::triggered,
            this, &StatisticsDock::onSettingsClicked);
    
    // View tabs
    connect(viewTabs_, &QTabWidget::currentChanged,
            this, &StatisticsDock::onViewTabChanged);
    
    // Chart signals
    connect(messageChart_, &BaseChartWidget::dataPointClicked,
            this, &StatisticsDock::onChartDataPointClicked);
    connect(toolChart_, &BaseChartWidget::dataPointClicked,
            this, &StatisticsDock::onChartDataPointClicked);
    
    // Summary widget
    connect(summaryWidget_, &StatsSummaryWidget::statClicked,
            [this](const QString& name) {
        // Navigate to relevant tab based on stat
        if (name.contains("Message")) {
            viewTabs_->setCurrentIndex(1); // Messages tab
        } else if (name.contains("Tool")) {
            viewTabs_->setCurrentIndex(2); // Tool usage tab
        } else if (name.contains("Token")) {
            viewTabs_->setCurrentIndex(4); // Tokens tab
        }
    });
}

void StatisticsDock::addDataPoint(const StatDataPoint& point) {
    dataPoints_.append(point);
    
    // Update real-time metrics if enabled
    if (realtimeEnabled_ && point.timestamp.secsTo(QDateTime::currentDateTime()) < 60) {
        if (point.category == "message") {
            realtimeWidget_->updateMetric("Messages/min", 
                realtimeWidget_->property("messagesPerMin").toDouble() + 1);
        } else if (point.category == "tokens") {
            realtimeWidget_->updateMetric("Tokens/min",
                realtimeWidget_->property("tokensPerMin").toDouble() + point.value);
        }
    }
}

void StatisticsDock::addDataPoints(const QList<StatDataPoint>& points) {
    dataPoints_.append(points);
}

void StatisticsDock::clearData() {
    dataPoints_.clear();
    updateAllCharts();
}

void StatisticsDock::setTimeRange(const QDateTime& start, const QDateTime& end) {
    startTime_ = start;
    endTime_ = end;
    
    startDateEdit_->setDateTime(start);
    endDateEdit_->setDateTime(end);
    
    emit timeRangeChanged(start, end);
}

void StatisticsDock::setCurrentView(const QString& view) {
    int index = 0;
    if (view == "messages") index = 1;
    else if (view == "tools") index = 2;
    else if (view == "performance") index = 3;
    else if (view == "tokens") index = 4;
    else if (view == "memory") index = 5;
    else if (view == "realtime") index = 6;
    else if (view == "comparison") index = 7;
    
    viewTabs_->setCurrentIndex(index);
}

void StatisticsDock::refreshAll() {
    updateStatistics();
    updateAllCharts();
}



void StatisticsDock::registerCustomMetric(const QString& name, const QString& unit) {
    customMetrics_[name] = 0;
    
    // Add to real-time widget
    if (realtimeWidget_) {
        realtimeWidget_->addMetric(name, unit);
    }
}

void StatisticsDock::updateCustomMetric(const QString& name, double value) {
    if (customMetrics_.contains(name)) {
        customMetrics_[name] = value;
        
        // Update real-time widget
        if (realtimeWidget_) {
            realtimeWidget_->updateMetric(name, value);
        }
        
        // Add as data point
        StatDataPoint point;
        point.timestamp = QDateTime::currentDateTime();
        point.category = "custom";
        point.subcategory = name;
        point.value = value;
        addDataPoint(point);
        
        emit customMetricUpdated(name, value);
    }
}

void StatisticsDock::setRealtimeEnabled(bool enabled) {
    realtimeEnabled_ = enabled;
    
    if (enabled) {
        realtimeTimer_->start();
        realtimeWidget_->start();
    } else {
        realtimeTimer_->stop();
        realtimeWidget_->stop();
    }
}

void StatisticsDock::updateStatistics() {
    calculateStatistics();
    updateAllCharts();
    
    // Update summary
    summaryWidget_->updateStats(cachedStats_);
    
    // Update details table
    detailsTable_->clear();
    detailsTable_->setColumnCount(2);
    detailsTable_->setHorizontalHeaderLabels({tr("Metric"), tr("Value")});
    
    int row = 0;
    for (auto it = cachedStats_.begin(); it != cachedStats_.end(); ++it) {
        detailsTable_->insertRow(row);
        detailsTable_->setItem(row, 0, new QTableWidgetItem(it.key()));
        detailsTable_->setItem(row, 1, new QTableWidgetItem(it.value().toString()));
        row++;
    }
    
    lastUpdate_ = QDateTime::currentDateTime();
}

void StatisticsDock::resetTimeRange() {
    setTimeRange(QDateTime::currentDateTime().addDays(-7), QDateTime::currentDateTime());
}

void StatisticsDock::onThemeChanged() {
    // Update chart themes
    QString theme = ThemeManager::instance()->currentTheme() == ThemeManager::Dark ? "dark" : "light";
    
    messageChart_->setTheme(theme);
    toolChart_->setTheme(theme);
    performanceChart_->setTheme(theme);
    tokenChart_->setTheme(theme);
    memoryChart_->setTheme(theme);
}

void StatisticsDock::onTimeRangeChanged() {
    startTime_ = startDateEdit_->dateTime();
    endTime_ = endDateEdit_->dateTime();
    
    updateStatistics();
    emit timeRangeChanged(startTime_, endTime_);
}

void StatisticsDock::onViewTabChanged(int index) {
    Q_UNUSED(index);
    
    // Update relevant chart when tab changes
    if (viewTabs_->currentWidget() == messageChart_) {
        messageChart_->refresh();
    } else if (viewTabs_->currentWidget() == toolChart_) {
        toolChart_->refresh();
    }
    
    emit viewChanged(viewTabs_->tabText(index));
}

void StatisticsDock::onRefreshClicked() {
    refreshAll();
}


void StatisticsDock::onSettingsClicked() {
    auto* dialog = new StatsSettingsDialog(this);
    
    dialog->setAutoRefreshEnabled(autoRefreshCheck_->isChecked());
    dialog->setRefreshInterval(refreshIntervalSpin_->value());
    dialog->setChartAnimationsEnabled(messageChart_->property("animated").toBool());
    
    if (dialog->exec() == QDialog::Accepted) {
        autoRefreshCheck_->setChecked(dialog->isAutoRefreshEnabled());
        refreshIntervalSpin_->setValue(dialog->refreshInterval());
        
        bool animated = dialog->chartAnimationsEnabled();
        messageChart_->setAnimated(animated);
        toolChart_->setAnimated(animated);
        performanceChart_->setAnimated(animated);
        tokenChart_->setAnimated(animated);
        memoryChart_->setAnimated(animated);
        
        saveSettings();
    }
    
    dialog->deleteLater();
}

void StatisticsDock::onChartDataPointClicked(const StatDataPoint& point) {
    emit dataPointClicked(point);
}

void StatisticsDock::updateRealtimeMetrics() {
    // Calculate real-time metrics
    QDateTime now = QDateTime::currentDateTime();
    QDateTime oneMinuteAgo = now.addSecs(-60);
    
    int messageCount = 0;
    double tokenCount = 0;
    
    for (const StatDataPoint& point : dataPoints_) {
        if (point.timestamp >= oneMinuteAgo) {
            if (point.category == "message") {
                messageCount++;
            } else if (point.category == "tokens" && point.subcategory == "total") {
                tokenCount += point.value;
            }
        }
    }
    
    realtimeWidget_->setProperty("messagesPerMin", messageCount);
    realtimeWidget_->setProperty("tokensPerMin", tokenCount);
    
    // Update real-time display
    realtimeWidget_->updateMetric("Messages/min", messageCount);
    realtimeWidget_->updateMetric("Tokens/min", tokenCount);
}

void StatisticsDock::loadSettings() {
    QSettings settings;
    settings.beginGroup("StatisticsDock");
    
    autoRefreshCheck_->setChecked(settings.value("autoRefresh", false).toBool());
    refreshIntervalSpin_->setValue(settings.value("refreshInterval", 30).toInt());
    presetCombo_->setCurrentIndex(settings.value("timePreset", 2).toInt());
    
    settings.endGroup();
}

void StatisticsDock::saveSettings() {
    QSettings settings;
    settings.beginGroup("StatisticsDock");
    
    settings.setValue("autoRefresh", autoRefreshCheck_->isChecked());
    settings.setValue("refreshInterval", refreshIntervalSpin_->value());
    settings.setValue("timePreset", presetCombo_->currentIndex());
    
    settings.endGroup();
}

void StatisticsDock::calculateStatistics() {
    cachedStats_.clear();
    
    // Filter data by time range
    QList<StatDataPoint> filteredData;
    for (const StatDataPoint& point : dataPoints_) {
        if (point.timestamp >= startTime_ && point.timestamp <= endTime_) {
            filteredData.append(point);
        }
    }
    
    // Calculate metrics
    int totalMessages = 0;
    int toolsUsed = 0;
    int successfulTools = 0;
    int totalTools = 0;
    double totalResponseTime = 0;
    int responseCount = 0;
    double totalTokens = 0;
    QSet<QString> uniqueTools;
    
    for (const StatDataPoint& point : filteredData) {
        if (point.category == "message") {
            totalMessages++;
        } else if (point.category == "tool") {
            totalTools++;
            uniqueTools.insert(point.subcategory);
            if (point.metadata.value("success").toBool()) {
                successfulTools++;
            }
        } else if (point.category == "performance" && point.subcategory == "response_time") {
            totalResponseTime += point.value;
            responseCount++;
        } else if (point.category == "tokens" && point.subcategory == "total") {
            totalTokens += point.value;
        }
    }
    
    cachedStats_["totalMessages"] = totalMessages;
    cachedStats_["toolsUsed"] = uniqueTools.size();
    cachedStats_["successRate"] = totalTools > 0 ? (successfulTools * 100.0 / totalTools) : 100.0;
    cachedStats_["avgResponseTime"] = responseCount > 0 ? (totalResponseTime / responseCount / 1000.0) : 0.0;
    cachedStats_["totalTokens"] = static_cast<int>(totalTokens);
    cachedStats_["memoryEntries"] = 0; // Would be calculated from memory data
}

void StatisticsDock::updateAllCharts() {
    // Filter data by time range
    QList<StatDataPoint> filteredData;
    for (const StatDataPoint& point : dataPoints_) {
        if (point.timestamp >= startTime_ && point.timestamp <= endTime_) {
            filteredData.append(point);
        }
    }
    
    // Update all charts
    messageChart_->setData(filteredData);
    toolChart_->setData(filteredData);
    performanceChart_->setData(filteredData);
    tokenChart_->setData(filteredData);
    memoryChart_->setData(filteredData);
    
    // Update comparison
    if (viewTabs_->currentWidget() == comparisonWidget_) {
        // Calculate comparison period
        qint64 duration = startTime_.msecsTo(endTime_);
        QDateTime compStart = startTime_.addMSecs(-duration);
        QDateTime compEnd = startTime_;
        
        comparisonWidget_->setCurrentPeriod(startTime_, endTime_);
        comparisonWidget_->setComparisonPeriod(compStart, compEnd);
        
        // Calculate stats for both periods
        QJsonObject currentStats = cachedStats_;
        
        // Calculate previous period stats
        QJsonObject previousStats;
        // ... calculation logic ...
        
        comparisonWidget_->updateData(currentStats, previousStats);
    }
}

// StatsSettingsDialog implementation

StatsSettingsDialog::StatsSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Statistics Settings"));
    setModal(true);
    resize(400, 300);
    
    setupUI();
}

void StatsSettingsDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);
    
    // Auto-refresh group
    auto* refreshGroup = new QGroupBox(tr("Auto Refresh"), this);
    auto* refreshLayout = new QFormLayout(refreshGroup);
    
    autoRefreshCheck_ = new QCheckBox(tr("Enable auto refresh"), this);
    refreshLayout->addRow(autoRefreshCheck_);
    
    refreshIntervalSpin_ = new QSpinBox(this);
    refreshIntervalSpin_->setRange(5, 300);
    refreshIntervalSpin_->setSuffix(tr(" seconds"));
    refreshLayout->addRow(tr("Refresh interval:"), refreshIntervalSpin_);
    
    layout->addWidget(refreshGroup);
    
    // Display group
    auto* displayGroup = new QGroupBox(tr("Display"), this);
    auto* displayLayout = new QFormLayout(displayGroup);
    
    timeRangeCombo_ = new QComboBox(this);
    timeRangeCombo_->addItems({tr("Last Hour"), tr("Last 24 Hours"), 
                              tr("Last 7 Days"), tr("Last 30 Days")});
    displayLayout->addRow(tr("Default time range:"), timeRangeCombo_);
    
    animationsCheck_ = new QCheckBox(tr("Enable chart animations"), this);
    displayLayout->addRow(animationsCheck_);
    
    layout->addWidget(displayGroup);
    
    // Export group
    auto* exportGroup = new QGroupBox(tr("Export"), this);
    auto* exportLayout = new QVBoxLayout(exportGroup);
    
    exportFormatsList_ = new QListWidget(this);
    exportFormatsList_->setSelectionMode(QAbstractItemView::MultiSelection);
    
    auto* csvItem = new QListWidgetItem(tr("CSV"), exportFormatsList_);
    csvItem->setCheckState(Qt::Checked);
    
    auto* jsonItem = new QListWidgetItem(tr("JSON"), exportFormatsList_);
    jsonItem->setCheckState(Qt::Checked);
    
    auto* pngItem = new QListWidgetItem(tr("PNG"), exportFormatsList_);
    pngItem->setCheckState(Qt::Checked);
    
    auto* svgItem = new QListWidgetItem(tr("SVG"), exportFormatsList_);
    svgItem->setCheckState(Qt::Unchecked);
    
    auto* pdfItem = new QListWidgetItem(tr("PDF"), exportFormatsList_);
    pdfItem->setCheckState(Qt::Checked);
    
    exportLayout->addWidget(new QLabel(tr("Enabled export formats:"), this));
    exportLayout->addWidget(exportFormatsList_);
    
    layout->addWidget(exportGroup);
    
    // Buttons
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        this
    );
    layout->addWidget(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    // Connect enable/disable logic
    connect(autoRefreshCheck_, &QCheckBox::toggled,
            refreshIntervalSpin_, &QSpinBox::setEnabled);
}

void StatsSettingsDialog::setAutoRefreshEnabled(bool enabled) {
    autoRefreshCheck_->setChecked(enabled);
}

bool StatsSettingsDialog::isAutoRefreshEnabled() const {
    return autoRefreshCheck_->isChecked();
}

void StatsSettingsDialog::setRefreshInterval(int seconds) {
    refreshIntervalSpin_->setValue(seconds);
}

int StatsSettingsDialog::refreshInterval() const {
    return refreshIntervalSpin_->value();
}

void StatsSettingsDialog::setDefaultTimeRange(const QString& range) {
    int index = timeRangeCombo_->findText(range);
    if (index >= 0) {
        timeRangeCombo_->setCurrentIndex(index);
    }
}

QString StatsSettingsDialog::defaultTimeRange() const {
    return timeRangeCombo_->currentText();
}

void StatsSettingsDialog::setChartAnimationsEnabled(bool enabled) {
    animationsCheck_->setChecked(enabled);
}

bool StatsSettingsDialog::chartAnimationsEnabled() const {
    return animationsCheck_->isChecked();
}

void StatsSettingsDialog::setExportFormats(const QStringList& formats) {
    for (int i = 0; i < exportFormatsList_->count(); ++i) {
        QListWidgetItem* item = exportFormatsList_->item(i);
        item->setCheckState(formats.contains(item->text()) ? Qt::Checked : Qt::Unchecked);
    }
}

QStringList StatsSettingsDialog::exportFormats() const {
    QStringList formats;
    for (int i = 0; i < exportFormatsList_->count(); ++i) {
        QListWidgetItem* item = exportFormatsList_->item(i);
        if (item->checkState() == Qt::Checked) {
            formats.append(item->text());
        }
    }
    return formats;
}

} // namespace llm_re::ui_v2