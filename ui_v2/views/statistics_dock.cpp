#include "../core/ui_v2_common.h"
#include "statistics_dock.h"
#include "../core/theme_manager.h"
#include "../core/ui_utils.h"

namespace llm_re::ui_v2 {

StatisticsDock::StatisticsDock(QWidget* parent)
    : BaseStyledWidget(parent) {
    setupUI();
    createToolBar();
    createViews();
    connectSignals();
    loadSettings();
    
    // Initialize time range
    endTime_ = QDateTime::currentDateTime();
    startTime_ = endTime_.addSecs(-24 * 3600);
    
    // Setup timers
    refreshTimer_ = new QTimer(this);
    refreshTimer_->setInterval(5000);
    connect(refreshTimer_, &QTimer::timeout, this, &StatisticsDock::updateStatistics);
    
    realtimeTimer_ = new QTimer(this);
    realtimeTimer_->setInterval(1000);
    connect(realtimeTimer_, &QTimer::timeout, this, &StatisticsDock::updateRealtimeMetrics);
    
    // Initialize with empty data
    updateStatistics();
}

StatisticsDock::~StatisticsDock() {
    saveSettings();
}

void StatisticsDock::addDataPoint(const StatDataPoint& point) {
    dataPoints_.append(point);
    
    // Keep only data within time range
    dataPoints_.erase(
        std::remove_if(dataPoints_.begin(), dataPoints_.end(),
                      [this](const StatDataPoint& p) {
                          return p.timestamp < startTime_ || p.timestamp > endTime_;
                      }),
        dataPoints_.end()
    );
    
    if (realtimeEnabled_) {
        updateRealtimeMetrics();
    }
}

void StatisticsDock::addDataPoints(const QList<StatDataPoint>& points) {
    dataPoints_.append(points);
    
    // Keep only data within time range
    dataPoints_.erase(
        std::remove_if(dataPoints_.begin(), dataPoints_.end(),
                      [this](const StatDataPoint& p) {
                          return p.timestamp < startTime_ || p.timestamp > endTime_;
                      }),
        dataPoints_.end()
    );
    
    updateStatistics();
}

void StatisticsDock::clearData() {
    dataPoints_.clear();
    cachedStats_ = QJsonObject();
    lastUpdate_ = QDateTime();
    
    // Clear all charts
    if (messageChart_) messageChart_->clearSeries();
    if (tokenUsageChart_) tokenUsageChart_->clearData();
    if (toolUsageChart_) toolUsageChart_->clearSeries();
    if (performanceChart_) performanceChart_->clearSeries();
    if (memoryAnalysisChart_) memoryAnalysisChart_->clearData();
    
    // Clear sparklines
    if (cpuSparkline_) cpuSparkline_->clearData();
    if (memorySparkline_) memorySparkline_->clearData();
    if (tokenRateSparkline_) tokenRateSparkline_->clearData();
    
    updateStatistics();
}

void StatisticsDock::setTimeRange(const QDateTime& start, const QDateTime& end) {
    startTime_ = start;
    endTime_ = end;
    
    if (startDateEdit_) startDateEdit_->setDateTime(start);
    if (endDateEdit_) endDateEdit_->setDateTime(end);
    
    emit timeRangeChanged(start, end);
    updateStatistics();
}

void StatisticsDock::setCurrentView(const QString& view) {
    if (!viewTabs_) return;
    
    for (int i = 0; i < viewTabs_->count(); ++i) {
        if (viewTabs_->tabText(i) == view) {
            viewTabs_->setCurrentIndex(i);
            break;
        }
    }
}

void StatisticsDock::refreshAll() {
    updateStatistics();
    if (realtimeEnabled_) {
        updateRealtimeMetrics();
    }
}

void StatisticsDock::registerCustomMetric(const QString& name, const QString& unit) {
    customMetrics_[name] = 0.0;
    
    if (realtimeWidget_) {
        realtimeWidget_->addMetric(name, unit);
    }
}

void StatisticsDock::updateCustomMetric(const QString& name, double value) {
    customMetrics_[name] = value;
    
    if (realtimeWidget_) {
        realtimeWidget_->updateMetric(name, value);
    }
    
    emit customMetricUpdated(name, value);
}

void StatisticsDock::setRealtimeEnabled(bool enabled) {
    realtimeEnabled_ = enabled;
    
    if (realtimeAction_) {
        realtimeAction_->setChecked(enabled);
    }
    
    if (enabled) {
        if (realtimeWidget_) realtimeWidget_->start();
        realtimeTimer_->start();
    } else {
        if (realtimeWidget_) realtimeWidget_->stop();
        realtimeTimer_->stop();
    }
}

void StatisticsDock::updateStatistics() {
    calculateStatistics();
    updateAllCharts();
    
    if (summaryWidget_) {
        summaryWidget_->updateStats(cachedStats_);
    }
    
    lastUpdate_ = QDateTime::currentDateTime();
}

void StatisticsDock::resetTimeRange() {
    endTime_ = QDateTime::currentDateTime();
    startTime_ = endTime_.addSecs(-24 * 3600);
    setTimeRange(startTime_, endTime_);
}

void StatisticsDock::onThemeChanged() {
    BaseStyledWidget::onThemeChanged();
    
    // Update stat card colors
    if (summaryWidget_) {
        // The cards are managed internally by StatsSummaryWidget
        // Just trigger an update to refresh the colors
        summaryWidget_->update();
    }
    
    // Re-process data to update chart colors
    if (!dataPoints_.empty()) {
        updateStatistics();
    }
}

void StatisticsDock::onTimeRangeChanged() {
    if (startDateEdit_ && endDateEdit_) {
        setTimeRange(startDateEdit_->dateTime(), endDateEdit_->dateTime());
    }
}

void StatisticsDock::onViewTabChanged(int index) {
    QString viewName = viewTabs_->tabText(index);
    emit viewChanged(viewName);
}

void StatisticsDock::onRefreshClicked() {
    refreshAll();
}

void StatisticsDock::onSettingsClicked() {
    StatsSettingsDialog dialog(this);
    
    dialog.setAutoRefreshEnabled(autoRefreshCheck_->isChecked());
    dialog.setRefreshInterval(refreshIntervalSpin_->value());
    dialog.setDefaultTimeRange(presetCombo_->currentText());
    dialog.setChartAnimationsEnabled(true);  // Default to true since charts don't have animation getter
    
    if (dialog.exec() == QDialog::Accepted) {
        autoRefreshCheck_->setChecked(dialog.isAutoRefreshEnabled());
        refreshIntervalSpin_->setValue(dialog.refreshInterval());
        
        // Animation settings handled internally by charts
        // No need to set animation on individual charts
        
        saveSettings();
    }
}

void StatisticsDock::onChartDataPointClicked(int seriesIndex, int pointIndex) {
    // Handle chart interaction
    if (seriesIndex >= 0 && pointIndex >= 0 && pointIndex < dataPoints_.size()) {
        emit dataPointClicked(dataPoints_[pointIndex]);
    }
}

void StatisticsDock::updateRealtimeMetrics() {
    // Update real-time metrics based on recent data
    if (!realtimeWidget_) return;
    
    // Calculate metrics from recent data (last minute)
    QDateTime recentTime = QDateTime::currentDateTime().addSecs(-60);
    
    double totalTokens = 0;
    double messageCount = 0;
    
    for (const auto& point : dataPoints_) {
        if (point.timestamp >= recentTime) {
            if (point.category == "tokens") {
                totalTokens += point.value;
            } else if (point.category == "messages") {
                messageCount += point.value;
            }
        }
    }
    
    // Update sparklines with recent values
    if (tokenRateSparkline_) {
        tokenRateSparkline_->appendValue(totalTokens);
    }
    
    // Update custom metrics
    for (auto it = customMetrics_.begin(); it != customMetrics_.end(); ++it) {
        realtimeWidget_->updateMetric(it.key(), it.value());
    }
}

void StatisticsDock::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);
    
    // Create main widget
    auto* contentWidget = new QWidget(this);
    mainLayout->addWidget(contentWidget);
    
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setContentsMargins(8, 8, 8, 8);
    contentLayout->setSpacing(8);
}

void StatisticsDock::createToolBar() {
    toolBar_ = new QToolBar(this);
    toolBar_->setMovable(false);
    
    // Time range controls
    startDateEdit_ = new QDateTimeEdit(startTime_, this);
    startDateEdit_->setCalendarPopup(true);
    startDateEdit_->setDisplayFormat("yyyy-MM-dd HH:mm");
    toolBar_->addWidget(new QLabel("From:", this));
    toolBar_->addWidget(startDateEdit_);
    
    endDateEdit_ = new QDateTimeEdit(endTime_, this);
    endDateEdit_->setCalendarPopup(true);
    endDateEdit_->setDisplayFormat("yyyy-MM-dd HH:mm");
    toolBar_->addWidget(new QLabel("To:", this));
    toolBar_->addWidget(endDateEdit_);
    
    // Preset dropdown
    presetCombo_ = new QComboBox(this);
    presetCombo_->addItems({"Last Hour", "Last 24 Hours", "Last Week", "Last Month", "Custom"});
    presetCombo_->setCurrentText("Last 24 Hours");
    toolBar_->addWidget(presetCombo_);
    
    toolBar_->addSeparator();
    
    // Auto refresh
    autoRefreshCheck_ = new QCheckBox("Auto Refresh", this);
    toolBar_->addWidget(autoRefreshCheck_);
    
    refreshIntervalSpin_ = new QSpinBox(this);
    refreshIntervalSpin_->setRange(1, 60);
    refreshIntervalSpin_->setValue(5);
    refreshIntervalSpin_->setSuffix(" sec");
    toolBar_->addWidget(refreshIntervalSpin_);
    
    toolBar_->addSeparator();
    
    // Actions
    refreshAction_ = toolBar_->addAction(ThemeManager::instance().themedIcon("refresh"), "Refresh");
    realtimeAction_ = toolBar_->addAction(ThemeManager::instance().themedIcon("realtime"), "Real-time");
    realtimeAction_->setCheckable(true);
    settingsAction_ = toolBar_->addAction(ThemeManager::instance().themedIcon("settings"), "Settings");
    
    // Add spacer
    auto* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar_->addWidget(spacer);
    
    // Add sparklines
    cpuSparkline_ = new charts::SparklineWidget(this);
    cpuSparkline_->setMinimumSize(100, 20);
    cpuSparkline_->setMaximumSize(100, 20);
    toolBar_->addWidget(cpuSparkline_);
    
    memorySparkline_ = new charts::SparklineWidget(this);
    memorySparkline_->setMinimumSize(100, 20);
    memorySparkline_->setMaximumSize(100, 20);
    toolBar_->addWidget(memorySparkline_);
    
    tokenRateSparkline_ = new charts::SparklineWidget(this);
    tokenRateSparkline_->setMinimumSize(100, 20);
    tokenRateSparkline_->setMaximumSize(100, 20);
    toolBar_->addWidget(tokenRateSparkline_);
    
    // Add toolbar to main layout
    qobject_cast<QVBoxLayout*>(layout())->insertWidget(0, toolBar_);
}

void StatisticsDock::createViews() {
    viewTabs_ = new QTabWidget(this);
    viewTabs_->setDocumentMode(true);
    
    // Summary tab
    auto* summaryTab = new QWidget();
    auto* summaryLayout = new QVBoxLayout(summaryTab);
    
    summaryWidget_ = new StatsSummaryWidget(summaryTab);
    summaryLayout->addWidget(summaryWidget_);
    
    detailsTable_ = new QTableWidget(summaryTab);
    detailsTable_->setColumnCount(4);
    QStringList headers;
    headers << "Metric" << "Current" << "Average" << "Total";
    detailsTable_->setHorizontalHeaderLabels(headers);
    detailsTable_->horizontalHeader()->setStretchLastSection(true);
    summaryLayout->addWidget(detailsTable_);
    
    viewTabs_->addTab(summaryTab, "Summary");
    
    // Messages tab
    auto* messagesTab = new QWidget();
    auto* messagesLayout = new QVBoxLayout(messagesTab);
    
    createMessageStatsChart();
    messagesLayout->addWidget(messageChart_);
    
    viewTabs_->addTab(messagesTab, "Messages");
    
    // Token Usage tab
    auto* tokensTab = new QWidget();
    auto* tokensLayout = new QVBoxLayout(tokensTab);
    
    createTokenUsageChart();
    tokensLayout->addWidget(tokenUsageChart_);
    
    viewTabs_->addTab(tokensTab, "Token Usage");
    
    // Tool Usage tab
    auto* toolsTab = new QWidget();
    auto* toolsLayout = new QVBoxLayout(toolsTab);
    
    createToolUsageChart();
    toolsLayout->addWidget(toolUsageChart_);
    
    viewTabs_->addTab(toolsTab, "Tool Usage");
    
    // Performance tab
    auto* perfTab = new QWidget();
    auto* perfLayout = new QVBoxLayout(perfTab);
    
    createPerformanceChart();
    perfLayout->addWidget(performanceChart_);
    
    viewTabs_->addTab(perfTab, "Performance");
    
    // Memory Analysis tab
    auto* memoryTab = new QWidget();
    auto* memoryLayout = new QVBoxLayout(memoryTab);
    
    createMemoryAnalysisChart();
    memoryLayout->addWidget(memoryAnalysisChart_);
    
    viewTabs_->addTab(memoryTab, "Memory Analysis");
    
    // Real-time tab
    auto* realtimeTab = new QWidget();
    auto* realtimeLayout = new QVBoxLayout(realtimeTab);
    
    realtimeWidget_ = new RealtimeMetricsWidget(realtimeTab);
    realtimeWidget_->addMetric("Response Time", "ms", 0, 5000);
    realtimeWidget_->addMetric("Token Rate", "tokens/sec", 0, 100);
    realtimeWidget_->addMetric("Memory Usage", "MB", 0, 1024);
    realtimeWidget_->addMetric("Active Tools", "", 0, 10);
    
    realtimeLayout->addWidget(realtimeWidget_);
    
    viewTabs_->addTab(realtimeTab, "Real-time");
    
    // Comparison tab
    auto* comparisonTab = new QWidget();
    auto* comparisonLayout = new QVBoxLayout(comparisonTab);
    
    comparisonWidget_ = new HistoricalComparisonWidget(comparisonTab);
    comparisonWidget_->setMetrics({"Messages", "Tokens", "Errors", "Response Time"});
    
    comparisonLayout->addWidget(comparisonWidget_);
    
    viewTabs_->addTab(comparisonTab, "Comparison");
    
    // Add tabs to main layout
    qobject_cast<QVBoxLayout*>(layout()->itemAt(0)->widget()->layout())->addWidget(viewTabs_);
}

void StatisticsDock::connectSignals() {
    // Time range
    connect(startDateEdit_, &QDateTimeEdit::dateTimeChanged, this, &StatisticsDock::onTimeRangeChanged);
    connect(endDateEdit_, &QDateTimeEdit::dateTimeChanged, this, &StatisticsDock::onTimeRangeChanged);
    
    // Preset combo
    connect(presetCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
                switch (index) {
                    case 0: // Last Hour
                        endTime_ = QDateTime::currentDateTime();
                        startTime_ = endTime_.addSecs(-3600);
                        break;
                    case 1: // Last 24 Hours
                        endTime_ = QDateTime::currentDateTime();
                        startTime_ = endTime_.addDays(-1);
                        break;
                    case 2: // Last Week
                        endTime_ = QDateTime::currentDateTime();
                        startTime_ = endTime_.addDays(-7);
                        break;
                    case 3: // Last Month
                        endTime_ = QDateTime::currentDateTime();
                        startTime_ = endTime_.addMonths(-1);
                        break;
                    case 4: // Custom
                        return;
                }
                setTimeRange(startTime_, endTime_);
            });
    
    // Auto refresh
    connect(autoRefreshCheck_, &QCheckBox::toggled, [this](bool checked) {
        if (checked) {
            refreshTimer_->start(refreshIntervalSpin_->value() * 1000);
        } else {
            refreshTimer_->stop();
        }
    });
    
    connect(refreshIntervalSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            [this](int value) {
                refreshTimer_->setInterval(value * 1000);
            });
    
    // Actions
    connect(refreshAction_, &QAction::triggered, this, &StatisticsDock::onRefreshClicked);
    connect(realtimeAction_, &QAction::toggled, this, &StatisticsDock::setRealtimeEnabled);
    connect(settingsAction_, &QAction::triggered, this, &StatisticsDock::onSettingsClicked);
    
    // View tabs
    connect(viewTabs_, &QTabWidget::currentChanged, this, &StatisticsDock::onViewTabChanged);
    
    // Chart interactions
    if (messageChart_) {
        connect(messageChart_, &charts::LineChart::dataPointClicked,
                this, &StatisticsDock::onChartDataPointClicked);
    }
    
    if (summaryWidget_) {
        connect(summaryWidget_, &StatsSummaryWidget::statClicked,
                [this](const QString& name) {
                    // Switch to appropriate tab based on stat name
                    if (name.contains("Message")) {
                        viewTabs_->setCurrentIndex(1);
                    } else if (name.contains("Token")) {
                        viewTabs_->setCurrentIndex(2);
                    }
                });
    }
}

void StatisticsDock::loadSettings() {
    QSettings settings;
    settings.beginGroup("Statistics");
    
    autoRefreshCheck_->setChecked(settings.value("autoRefresh", false).toBool());
    refreshIntervalSpin_->setValue(settings.value("refreshInterval", 5).toInt());
    presetCombo_->setCurrentText(settings.value("defaultTimeRange", "Last 24 Hours").toString());
    
    settings.endGroup();
}

void StatisticsDock::saveSettings() {
    QSettings settings;
    settings.beginGroup("Statistics");
    
    settings.setValue("autoRefresh", autoRefreshCheck_->isChecked());
    settings.setValue("refreshInterval", refreshIntervalSpin_->value());
    settings.setValue("defaultTimeRange", presetCombo_->currentText());
    
    settings.endGroup();
}

void StatisticsDock::calculateStatistics() {
    cachedStats_ = QJsonObject();
    
    // Process data points
    processMessageStats();
    processTokenUsage();
    processToolUsage();
    processPerformance();
    processMemoryAnalysis();
    
    // Add summary stats
    cachedStats_["totalDataPoints"] = dataPoints_.size();
    cachedStats_["timeRange"] = QJsonObject{
        {"start", startTime_.toString(Qt::ISODate)},
        {"end", endTime_.toString(Qt::ISODate)}
    };
}

void StatisticsDock::updateAllCharts() {
    // Update each chart with processed data
    if (messageChart_) {
        messageChart_->updateData();
    }
    
    if (tokenUsageChart_) {
        tokenUsageChart_->updateData();
    }
    
    if (toolUsageChart_) {
        toolUsageChart_->updateData();
    }
    
    if (performanceChart_) {
        performanceChart_->updateData();
    }
    
    if (memoryAnalysisChart_) {
        memoryAnalysisChart_->updateData();
    }
}

void StatisticsDock::createMessageStatsChart() {
    messageChart_ = new charts::LineChart(this);
    messageChart_->setTitle("Message Statistics");
    // Axis titles are handled internally by the chart
    // Legend is shown by default (LegendConfig position = Right)
    messageChart_->setTimeSeriesMode(true);
    
    // Add series
    charts::ChartSeries userMessages("User Messages");
    userMessages.color = ThemeManager::instance().colors().userMessage;
    messageChart_->addSeries(userMessages);
    
    charts::ChartSeries assistantMessages("Assistant Messages");
    assistantMessages.color = ThemeManager::instance().colors().assistantMessage;
    messageChart_->addSeries(assistantMessages);
    
    charts::ChartSeries toolMessages("Tool Messages");
    toolMessages.color = getMetricColor("tool_messages");
    messageChart_->addSeries(toolMessages);
}

void StatisticsDock::createTokenUsageChart() {
    tokenUsageChart_ = new charts::CircularChart(this);
    tokenUsageChart_->setTitle("Token Usage Distribution");
    tokenUsageChart_->setChartType(charts::CircularChart::Donut);
    // Chart will show legend and values by default
}

void StatisticsDock::createToolUsageChart() {
    toolUsageChart_ = new charts::BarChart(this);
    toolUsageChart_->setTitle("Tool Usage Statistics");
    // Chart shows values by default
    toolUsageChart_->setGradient(true);
    
    // Set categories
    QStringList tools = {"Read", "Write", "Edit", "Search", "Execute", "Other"};
    toolUsageChart_->setCategories(tools);
}

void StatisticsDock::createPerformanceChart() {
    performanceChart_ = new charts::LineChart(this);
    performanceChart_->setTitle("Performance Metrics");
    // Chart configuration - titles and modes are set internally
    performanceChart_->setTimeSeriesMode(true);
    
    // Add series
    charts::ChartSeries responseTime("Response Time (ms)");
    responseTime.color = getMetricColor("response_time");
    responseTime.lineWidth = 2.0f;
    performanceChart_->addSeries(responseTime);
    
    charts::ChartSeries throughput("Throughput (req/min)");
    throughput.color = getMetricColor("throughput");
    throughput.lineWidth = 2.0f;
    performanceChart_->addSeries(throughput);
}

void StatisticsDock::createMemoryAnalysisChart() {
    memoryAnalysisChart_ = new charts::HeatmapWidget(this);
    memoryAnalysisChart_->setTitle("Memory Access Patterns");
    memoryAnalysisChart_->setColorScale(charts::HeatmapTheme::ColorScale::Turbo);
    memoryAnalysisChart_->setShowValues(false);
    memoryAnalysisChart_->setMemoryMode(true);
    
    // Set initial data
    std::vector<std::vector<double>> dummyData(16, std::vector<double>(32, 0.0));
    memoryAnalysisChart_->setData(dummyData);
}

void StatisticsDock::processMessageStats() {
    // Count messages by type over time
    QMap<QDateTime, QMap<QString, int>> messagesByTime;
    
    for (const auto& point : dataPoints_) {
        if (point.category == "message") {
            // Round to nearest minute
            QDateTime rounded = point.timestamp;
            rounded.setTime(QTime(rounded.time().hour(), rounded.time().minute()));
            
            messagesByTime[rounded][point.subcategory]++;
        }
    }
    
    // Update chart
    if (messageChart_) {
        messageChart_->clearSeries();
        
        for (auto it = messagesByTime.begin(); it != messagesByTime.end(); ++it) {
            double timestamp = it.key().toMSecsSinceEpoch();
            
            messageChart_->appendDataPoint(0, charts::ChartDataPoint(
                timestamp, it.value()["user"], "User"));
            messageChart_->appendDataPoint(1, charts::ChartDataPoint(
                timestamp, it.value()["assistant"], "Assistant"));
            messageChart_->appendDataPoint(2, charts::ChartDataPoint(
                timestamp, it.value()["tool"], "Tool"));
        }
    }
    
    // Update summary stats
    int totalMessages = 0;
    for (const auto& point : dataPoints_) {
        if (point.category == "message") {
            totalMessages++;
        }
    }
    
    cachedStats_["messages"] = QJsonObject{
        {"total", totalMessages},
        {"perHour", totalMessages * 3600.0 / startTime_.secsTo(endTime_)}
    };
}

void StatisticsDock::processTokenUsage() {
    // Calculate token usage by type
    QMap<QString, double> tokensByType;
    double totalTokens = 0;
    
    for (const auto& point : dataPoints_) {
        if (point.category == "tokens") {
            tokensByType[point.subcategory] += point.value;
            totalTokens += point.value;
        }
    }
    
    // Update chart
    if (tokenUsageChart_) {
        tokenUsageChart_->clearData();
        
        // Get theme-appropriate colors
        std::vector<QColor> themeColors = charts::ChartTheme::getSeriesColors(
            ThemeManager::instance().currentTheme());
        QList<QColor> colors(themeColors.begin(), themeColors.end());
        
        int colorIndex = 0;
        for (auto it = tokensByType.begin(); it != tokensByType.end(); ++it) {
            charts::ChartDataPoint point;
            point.y = it.value();  // Use y for the value (slice size)
            point.label = it.key();
            point.color = colors[colorIndex % colors.size()];
            tokenUsageChart_->addDataPoint(point);
            colorIndex++;
        }
        
        // Update display with total tokens
        tokenUsageChart_->setTitle(QString("Total Tokens: %1").arg(
            QString::number(totalTokens, 'f', 0)));
    }
    
    cachedStats_["tokens"] = QJsonObject{
        {"total", totalTokens},
        {"byType", QJsonDocument::fromVariant(QVariant::fromValue(tokensByType)).object()}
    };
}

void StatisticsDock::processToolUsage() {
    // Count tool usage
    QMap<QString, int> toolCounts;
    
    for (const auto& point : dataPoints_) {
        if (point.category == "tool") {
            toolCounts[point.subcategory]++;
        }
    }
    
    // Update chart
    if (toolUsageChart_) {
        toolUsageChart_->clearSeries();
        
        std::vector<double> values;
        for (const auto& category : toolUsageChart_->categories()) {
            values.push_back(toolCounts[category]);
        }
        
        toolUsageChart_->addSeries("Usage Count", values);
    }
    
    cachedStats_["tools"] = QJsonObject{
        {"usage", QJsonDocument::fromVariant(QVariant::fromValue(toolCounts)).object()}
    };
}

void StatisticsDock::processPerformance() {
    // Calculate performance metrics over time
    QMap<QDateTime, QMap<QString, double>> perfByTime;
    
    for (const auto& point : dataPoints_) {
        if (point.category == "performance") {
            // Round to nearest minute
            QDateTime rounded = point.timestamp;
            rounded.setTime(QTime(rounded.time().hour(), rounded.time().minute()));
            
            if (point.subcategory == "response_time") {
                perfByTime[rounded]["response_time"] = point.value;
            } else if (point.subcategory == "throughput") {
                perfByTime[rounded]["throughput"] = point.value;
            }
        }
    }
    
    // Update chart
    if (performanceChart_) {
        performanceChart_->clearSeries();
        
        for (auto it = perfByTime.begin(); it != perfByTime.end(); ++it) {
            double timestamp = it.key().toMSecsSinceEpoch();
            
            if (it.value().contains("response_time")) {
                performanceChart_->appendDataPoint(0, charts::ChartDataPoint(
                    timestamp, it.value()["response_time"], "Response Time"));
            }
            
            if (it.value().contains("throughput")) {
                performanceChart_->appendDataPoint(1, charts::ChartDataPoint(
                    timestamp, it.value()["throughput"], "Throughput"));
            }
        }
    }
    
    // Calculate averages
    double avgResponseTime = 0;
    int responseTimeCount = 0;
    
    for (const auto& point : dataPoints_) {
        if (point.category == "performance" && point.subcategory == "response_time") {
            avgResponseTime += point.value;
            responseTimeCount++;
        }
    }
    
    if (responseTimeCount > 0) {
        avgResponseTime /= responseTimeCount;
    }
    
    cachedStats_["performance"] = QJsonObject{
        {"avgResponseTime", avgResponseTime},
        {"samples", responseTimeCount}
    };
}

void StatisticsDock::processMemoryAnalysis() {
    // Create memory access heatmap data
    const int rows = 16;
    const int cols = 32;
    std::vector<std::vector<double>> heatmapData(rows, std::vector<double>(cols, 0.0));
    
    // Process memory access patterns from data points
    for (const auto& point : dataPoints_) {
        if (point.category == "memory" && point.metadata.contains("address")) {
            quint64 address = point.metadata["address"].toString().toULongLong(nullptr, 16);
            int row = (address / 32) % rows;
            int col = address % cols;
            
            if (row < rows && col < cols) {
                heatmapData[row][col] += point.value;
            }
        }
    }
    
    // Normalize data
    double maxValue = 0;
    for (const auto& row : heatmapData) {
        for (double val : row) {
            maxValue = std::max(maxValue, val);
        }
    }
    
    if (maxValue > 0) {
        for (auto& row : heatmapData) {
            for (double& val : row) {
                val /= maxValue;
            }
        }
    }
    
    // Update chart
    if (memoryAnalysisChart_) {
        memoryAnalysisChart_->setData(heatmapData);
    }
}

// Theme helper methods
QList<QColor> StatisticsDock::getChartSeriesColors() const {
    std::vector<QColor> themeColors = charts::ChartTheme::getSeriesColors(
        ThemeManager::instance().currentTheme());
    return QList<QColor>(themeColors.begin(), themeColors.end());
}

QColor StatisticsDock::getMetricColor(const QString& metricType) const {
    const auto& colors = ThemeManager::instance().colors();
    
    if (metricType == "success") return colors.success;
    if (metricType == "warning") return colors.warning;
    if (metricType == "error") return colors.error;
    if (metricType == "info") return colors.info;
    if (metricType == "primary") return colors.primary;
    
    // Specific metric types
    if (metricType == "tool_messages") return colors.info;
    if (metricType == "response_time") return colors.warning;
    if (metricType == "throughput") return colors.success;
    
    // Default to using series colors based on hash
    int colorIndex = qHash(metricType) % 6;
    return charts::ChartTheme::getSeriesColor(ThemeManager::instance().currentTheme(), colorIndex);
}

QColor StatisticsDock::getMetricRangeColor(double normalizedValue) const {
    const auto& colors = ThemeManager::instance().colors();
    
    if (normalizedValue < 0.33) {
        return colors.success;
    } else if (normalizedValue < 0.66) {
        return colors.warning;
    } else {
        return colors.error;
    }
}

// StatsSummaryWidget implementation
StatsSummaryWidget::StatsSummaryWidget(QWidget* parent)
    : BaseStyledWidget(parent) {
    setMinimumHeight(150);
    
    // Setup animation timer
    animationTimer_ = new QTimer(this);
    animationTimer_->setInterval(16);
    connect(animationTimer_, &QTimer::timeout, [this]() {
        bool needsUpdate = false;
        for (auto& card : cards_) {
            if (card.animationProgress < 1.0f) {
                card.animationProgress = std::min(1.0f, card.animationProgress + 0.05f);
                needsUpdate = true;
            }
        }
        
        if (needsUpdate) {
            update();
        } else {
            animationTimer_->stop();
        }
    });
    
    // Initialize default cards with theme colors
    std::vector<QColor> cardColors = charts::ChartTheme::getSeriesColors(
        ThemeManager::instance().currentTheme());
    cards_ = {
        {"Total Messages", "0", "", "message", cardColors[0], nullptr, QRectF(), false},
        {"Tokens Used", "0", "", "token", cardColors[1], nullptr, QRectF(), false},
        {"Tools Called", "0", "", "tool", cardColors[2], nullptr, QRectF(), false},
        {"Avg Response", "0ms", "", "time", cardColors[3], nullptr, QRectF(), false}
    };
    
    // Create sparklines for each card
    for (auto& card : cards_) {
        card.sparkline = new charts::SparklineWidget(this);
        card.sparkline->setSparklineType(charts::SparklineWidget::Area);
        card.sparkline->setLineColor(card.color);
        card.sparkline->setFillColor(card.color);
        card.sparkline->setShowMinMax(false);
        card.sparkline->setShowLastValue(false);
        card.sparkline->setMaxDataPoints(20);
    }
}

void StatsSummaryWidget::updateStats(const QJsonObject& stats) {
    // Update card values with animation
    if (stats.contains("messages")) {
        animateValueChange(cards_[0], QString::number(stats["messages"]["total"].toInt()));
        cards_[0].subtitle = QString("%1/hour").arg(stats["messages"]["perHour"].toDouble(), 0, 'f', 1);
    }
    
    if (stats.contains("tokens")) {
        animateValueChange(cards_[1], QString::number(stats["tokens"]["total"].toDouble(), 'f', 0));
    }
    
    if (stats.contains("tools")) {
        int totalTools = 0;
        QJsonObject usage = stats["tools"]["usage"].toObject();
        for (auto it = usage.begin(); it != usage.end(); ++it) {
            totalTools += it.value().toInt();
        }
        animateValueChange(cards_[2], QString::number(totalTools));
    }
    
    if (stats.contains("performance")) {
        double avgTime = stats["performance"]["avgResponseTime"].toDouble();
        animateValueChange(cards_[3], QString("%1ms").arg(avgTime, 0, 'f', 0));
    }
    
    // Add some dummy data to sparklines
    for (auto& card : cards_) {
        if (card.sparkline) {
            card.sparkline->appendValue(QRandomGenerator::global()->bounded(100));
        }
    }
    
    layoutCards();
    update();
}

void StatsSummaryWidget::setTimeRange(const QDateTime& start, const QDateTime& end) {
    // Update time range display if needed
    update();
}

void StatsSummaryWidget::addCustomStat(const QString& name, const QString& value, const QString& icon) {
    StatCard card;
    card.name = name;
    card.value = value;
    card.icon = icon;
    card.color = charts::ChartTheme::getSeriesColor(ThemeManager::instance().currentTheme(), cards_.size() % 6);
    card.isCustom = true;
    card.sparkline = new charts::SparklineWidget(this);
    card.sparkline->setSparklineType(charts::SparklineWidget::Line);
    
    cards_.append(card);
    layoutCards();
    update();
}

void StatsSummaryWidget::clearCustomStats() {
    cards_.erase(
        std::remove_if(cards_.begin(), cards_.end(),
                      [](const StatCard& card) { return card.isCustom; }),
        cards_.end()
    );
    
    layoutCards();
    update();
}

void StatsSummaryWidget::paintEvent(QPaintEvent* event) {
    BaseStyledWidget::paintEvent(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    for (int i = 0; i < cards_.size(); ++i) {
        if (i == hoveredCard_) {
            // Draw hover effect
            QRectF hoverRect = cards_[i].rect.adjusted(-2, -2, 2, 2);
            painter.setPen(Qt::NoPen);
            painter.setBrush(ThemeManager::adjustAlpha(ThemeManager::instance().colors().surfaceHover, 20));
            painter.drawRoundedRect(hoverRect, 8, 8);
        }
        
        drawCard(&painter, cards_[i]);
    }
}

void StatsSummaryWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        for (int i = 0; i < cards_.size(); ++i) {
            if (cards_[i].rect.contains(event->pos())) {
                emit statClicked(cards_[i].name);
                break;
            }
        }
    }
    
    BaseStyledWidget::mousePressEvent(event);
}

void StatsSummaryWidget::resizeEvent(QResizeEvent* event) {
    BaseStyledWidget::resizeEvent(event);
    layoutCards();
}

void StatsSummaryWidget::layoutCards() {
    if (cards_.empty()) return;
    
    // Calculate optimal grid layout
    int totalCards = cards_.size();
    columns_ = std::min(4, totalCards);
    int rows = (totalCards + columns_ - 1) / columns_;
    
    cardHeight_ = std::min(120, (height() - (rows + 1) * cardSpacing_) / rows);
    
    double cardWidth = (width() - (columns_ + 1) * cardSpacing_) / columns_;
    
    // Position cards
    for (int i = 0; i < cards_.size(); ++i) {
        int row = i / columns_;
        int col = i % columns_;
        
        cards_[i].rect = QRectF(
            cardSpacing_ + col * (cardWidth + cardSpacing_),
            cardSpacing_ + row * (cardHeight_ + cardSpacing_),
            cardWidth,
            cardHeight_
        );
        
        // Position sparkline
        if (cards_[i].sparkline) {
            cards_[i].sparkline->setGeometry(
                cards_[i].rect.left() + 10,
                cards_[i].rect.bottom() - 30,
                cards_[i].rect.width() - 20,
                20
            );
        }
    }
}

void StatsSummaryWidget::drawCard(QPainter* painter, const StatCard& card) {
    painter->save();
    
    // Draw card background
    QRectF cardRect = card.rect;
    
    // Glass morphism effect
    painter->setPen(Qt::NoPen);
    painter->setBrush(ThemeManager::adjustAlpha(ThemeManager::instance().colors().surface, 10));
    painter->drawRoundedRect(cardRect, 8, 8);
    
    // Draw border
    QPen borderPen(ThemeManager::adjustAlpha(ThemeManager::instance().colors().border, 30), 1);
    painter->setPen(borderPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(cardRect, 8, 8);
    
    // Draw icon
    QRectF iconRect(cardRect.left() + 15, cardRect.top() + 15, 24, 24);
    painter->setPen(card.color);
    painter->setFont(QFont("FontAwesome", 16));
    painter->drawText(iconRect, Qt::AlignCenter, card.icon);
    
    // Draw name
    painter->setPen(palette().text().color());
    QFont nameFont = font();
    nameFont.setPointSize(10);
    painter->setFont(nameFont);
    painter->drawText(QRectF(cardRect.left() + 50, cardRect.top() + 15,
                            cardRect.width() - 60, 20),
                     Qt::AlignLeft | Qt::AlignVCenter, card.name);
    
    // Draw value with animation
    QString displayValue = card.value;
    if (card.animationProgress < 1.0f && !card.previousValue.isEmpty()) {
        // Interpolate numeric values if possible
        bool ok1, ok2;
        double prev = card.previousValue.toDouble(&ok1);
        double curr = card.value.toDouble(&ok2);
        
        if (ok1 && ok2) {
            double interpolated = prev + (curr - prev) * card.animationProgress;
            displayValue = QString::number(interpolated, 'f', 0);
        }
    }
    
    QFont valueFont = font();
    valueFont.setPointSize(18);
    valueFont.setBold(true);
    painter->setFont(valueFont);
    painter->setPen(card.color);
    painter->drawText(QRectF(cardRect.left() + 15, cardRect.top() + 40,
                            cardRect.width() - 30, 30),
                     Qt::AlignLeft | Qt::AlignVCenter, displayValue);
    
    // Draw subtitle
    if (!card.subtitle.isEmpty()) {
        QFont subtitleFont = font();
        subtitleFont.setPointSize(9);
        painter->setFont(subtitleFont);
        painter->setPen(palette().text().color());
        painter->setOpacity(0.7);
        painter->drawText(QRectF(cardRect.left() + 15, cardRect.top() + 70,
                                cardRect.width() - 30, 20),
                         Qt::AlignLeft | Qt::AlignVCenter, card.subtitle);
        painter->setOpacity(1.0);
    }
    
    painter->restore();
}

void StatsSummaryWidget::animateValueChange(StatCard& card, const QString& newValue) {
    if (card.value != newValue) {
        card.previousValue = card.value;
        card.value = newValue;
        card.animationProgress = 0.0f;
        
        if (!animationTimer_->isActive()) {
            animationTimer_->start();
        }
    }
}

// RealtimeMetricsWidget implementation
RealtimeMetricsWidget::RealtimeMetricsWidget(QWidget* parent)
    : BaseStyledWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    
    // Create scroll area for metrics
    auto* scrollArea = new QScrollArea(this);
    auto* scrollWidget = new QWidget();
    metricsLayout_ = new QGridLayout(scrollWidget);
    metricsLayout_->setSpacing(16);
    
    scrollArea->setWidget(scrollWidget);
    scrollArea->setWidgetResizable(true);
    mainLayout->addWidget(scrollArea);
    
    // Setup update timer
    updateTimer_ = new QTimer(this);
    connect(updateTimer_, &QTimer::timeout, [this]() {
        for (auto& metric : metrics_) {
            updateMetricDisplay(metric);
        }
    });
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
    
    setupMetricUI(metric);
    metrics_[name] = metric;
}

void RealtimeMetricsWidget::updateMetric(const QString& name, double value) {
    if (metrics_.contains(name)) {
        metrics_[name].value = value;
        
        if (metrics_[name].chart) {
            metrics_[name].chart->appendDataPoint(0, charts::ChartDataPoint(
                QDateTime::currentDateTime().toMSecsSinceEpoch(), value));
        }
        
        if (metrics_[name].sparkline) {
            metrics_[name].sparkline->appendValue(value);
        }
        
        updateMetricDisplay(metrics_[name]);
        emit metricUpdated(name, value);
    }
}

void RealtimeMetricsWidget::removeMetric(const QString& name) {
    if (metrics_.contains(name)) {
        auto& metric = metrics_[name];
        
        if (metric.chart) metric.chart->deleteLater();
        if (metric.sparkline) metric.sparkline->deleteLater();
        if (metric.valueLabel) metric.valueLabel->deleteLater();
        
        metrics_.remove(name);
    }
}

void RealtimeMetricsWidget::setUpdateInterval(int ms) {
    updateInterval_ = ms;
    if (updateTimer_->isActive()) {
        updateTimer_->setInterval(ms);
    }
}

void RealtimeMetricsWidget::setHistorySize(int size) {
    historySize_ = size;
    
    for (auto& metric : metrics_) {
        if (metric.sparkline) {
            metric.sparkline->setMaxDataPoints(size);
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

void RealtimeMetricsWidget::setupMetricUI(Metric& metric) {
    int row = metrics_.size();
    
    // Create group box
    auto* groupBox = new QGroupBox(metric.name, this);
    auto* groupLayout = new QVBoxLayout(groupBox);
    
    // Value display
    metric.valueLabel = new QLabel("0", this);
    QFont valueFont = font();
    valueFont.setPointSize(24);
    valueFont.setBold(true);
    metric.valueLabel->setFont(valueFont);
    metric.valueLabel->setAlignment(Qt::AlignCenter);
    groupLayout->addWidget(metric.valueLabel);
    
    // Sparkline
    metric.sparkline = new charts::SparklineWidget(this);
    metric.sparkline->setSparklineType(charts::SparklineWidget::Area);
    metric.sparkline->setMaxDataPoints(historySize_);
    metric.sparkline->setMinimumHeight(40);
    metric.sparkline->setValueRange(metric.min, metric.max);
    groupLayout->addWidget(metric.sparkline);
    
    // Full chart
    metric.chart = new charts::LineChart(this);
    metric.chart->setMinimumHeight(150);
    // Axes and grid are shown by default
    metric.chart->setTimeSeriesMode(true);
    // Real-time mode and Y-axis range handled internally
    
    charts::ChartSeries series(metric.name);
    series.color = charts::ChartTheme::getSeriesColor(ThemeManager::instance().currentTheme(), 0);
    metric.chart->addSeries(series);
    
    groupLayout->addWidget(metric.chart);
    
    metricsLayout_->addWidget(groupBox, row / 2, row % 2);
}

void RealtimeMetricsWidget::updateMetricDisplay(Metric& metric) {
    if (metric.valueLabel) {
        QString text = QString("%1 %2").arg(metric.value, 0, 'f', 1).arg(metric.unit);
        metric.valueLabel->setText(text);
        
        // Color based on value range using theme colors
        double normalized = (metric.value - metric.min) / (metric.max - metric.min);
        
        // Use theme colors directly for metric ranges
        const auto& colors = ThemeManager::instance().colors();
        QColor color;
        if (normalized < 0.33) {
            color = colors.success;
        } else if (normalized < 0.66) {
            color = colors.warning;
        } else {
            color = colors.error;
        }
        
        metric.valueLabel->setStyleSheet(QString("color: %1;").arg(color.name()));
    }
}

// HistoricalComparisonWidget implementation
HistoricalComparisonWidget::HistoricalComparisonWidget(QWidget* parent)
    : BaseStyledWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    
    // Controls
    auto* controlsLayout = new QHBoxLayout();
    
    auto* typeCombo = new QComboBox(this);
    typeCombo->addItems({"Previous Period", "Same Day Last Week", "Custom"});
    connect(typeCombo, &QComboBox::currentTextChanged,
            [this](const QString& text) {
                if (text == "Previous Period") {
                    setComparisonType("previous");
                } else if (text == "Same Day Last Week") {
                    setComparisonType("same_day_last_week");
                } else {
                    setComparisonType("custom");
                }
            });
    
    controlsLayout->addWidget(new QLabel("Compare with:", this));
    controlsLayout->addWidget(typeCombo);
    controlsLayout->addStretch();
    
    mainLayout->addLayout(controlsLayout);
    
    // Cards container
    auto* scrollArea = new QScrollArea(this);
    auto* scrollWidget = new QWidget();
    cardsLayout_ = new QVBoxLayout(scrollWidget);
    cardsLayout_->setSpacing(16);
    
    scrollArea->setWidget(scrollWidget);
    scrollArea->setWidgetResizable(true);
    mainLayout->addWidget(scrollArea);
}

void HistoricalComparisonWidget::setCurrentPeriod(const QDateTime& start, const QDateTime& end) {
    currentStart_ = start;
    currentEnd_ = end;
    calculateChanges();
}

void HistoricalComparisonWidget::setComparisonPeriod(const QDateTime& start, const QDateTime& end) {
    comparisonStart_ = start;
    comparisonEnd_ = end;
    calculateChanges();
}

void HistoricalComparisonWidget::setMetrics(const QStringList& metrics) {
    // Clear existing cards
    for (auto& card : cards_) {
        if (card.comparisonChart) card.comparisonChart->deleteLater();
        if (card.trendLine) card.trendLine->deleteLater();
    }
    cards_.clear();
    
    // Create new cards
    for (const auto& metric : metrics) {
        createComparisonCard(metric);
    }
}

void HistoricalComparisonWidget::updateData(const QJsonObject& currentData, 
                                           const QJsonObject& comparisonData) {
    for (auto& card : cards_) {
        if (currentData.contains(card.metric)) {
            card.currentValue = currentData[card.metric].toDouble();
        }
        
        if (comparisonData.contains(card.metric)) {
            card.previousValue = comparisonData[card.metric].toDouble();
        }
        
        updateComparisonCard(card);
    }
    
    calculateChanges();
}

void HistoricalComparisonWidget::setComparisonType(const QString& type) {
    comparisonType_ = type;
    
    if (type == "previous") {
        // Set comparison period to previous equivalent period
        qint64 duration = currentStart_.secsTo(currentEnd_);
        comparisonEnd_ = currentStart_;
        comparisonStart_ = comparisonEnd_.addSecs(-duration);
    } else if (type == "same_day_last_week") {
        comparisonStart_ = currentStart_.addDays(-7);
        comparisonEnd_ = currentEnd_.addDays(-7);
    }
    
    calculateChanges();
}

void HistoricalComparisonWidget::createComparisonCard(const QString& metric) {
    ComparisonCard card;
    card.metric = metric;
    
    // Create card widget
    auto* cardWidget = new QWidget(this);
    auto* cardLayout = new QHBoxLayout(cardWidget);
    // Use theme-aware colors for card styling
    QColor bgColor = ThemeManager::adjustAlpha(ThemeManager::instance().colors().surface, 13); // 0.05 * 255 ≈ 13
    cardWidget->setStyleSheet(QString("QWidget { background: rgba(%1,%2,%3,%4); "
                             "border-radius: 8px; padding: 16px; }")
                             .arg(bgColor.red())
                             .arg(bgColor.green())
                             .arg(bgColor.blue())
                             .arg(bgColor.alpha()));
    
    // Left side - metric info
    auto* infoLayout = new QVBoxLayout();
    
    auto* nameLabel = new QLabel(metric, this);
    QFont nameFont = font();
    nameFont.setPointSize(14);
    nameFont.setBold(true);
    nameLabel->setFont(nameFont);
    infoLayout->addWidget(nameLabel);
    
    auto* currentLabel = new QLabel("Current: 0", this);
    infoLayout->addWidget(currentLabel);
    
    auto* previousLabel = new QLabel("Previous: 0", this);
    infoLayout->addWidget(previousLabel);
    
    auto* changeLabel = new QLabel("Change: 0%", this);
    QFont changeFont = font();
    changeFont.setPointSize(16);
    changeFont.setBold(true);
    changeLabel->setFont(changeFont);
    infoLayout->addWidget(changeLabel);
    
    cardLayout->addLayout(infoLayout);
    
    // Middle - comparison chart
    card.comparisonChart = new charts::BarChart(this);
    card.comparisonChart->setMinimumSize(200, 100);
    card.comparisonChart->setMaximumHeight(100);
    // Axes visibility controlled by chart style
    card.comparisonChart->setShowValues(true);
    card.comparisonChart->setCategories({"Previous", "Current"});
    cardLayout->addWidget(card.comparisonChart);
    
    // Right - trend sparkline
    card.trendLine = new charts::SparklineWidget(this);
    card.trendLine->setSparklineType(charts::SparklineWidget::Line);
    card.trendLine->setMinimumSize(150, 50);
    card.trendLine->setMaximumSize(150, 50);
    cardLayout->addWidget(card.trendLine);
    
    cardsLayout_->addWidget(cardWidget);
    cards_.append(card);
}

void HistoricalComparisonWidget::updateComparisonCard(ComparisonCard& card) {
    // Update bar chart
    if (card.comparisonChart) {
        card.comparisonChart->clearSeries();
        std::vector<double> values = {card.previousValue, card.currentValue};
        card.comparisonChart->addSeries(card.metric, values);
    }
    
    // Update trend line with dummy data
    if (card.trendLine) {
        for (int i = 0; i < 10; ++i) {
            double value = card.previousValue + 
                          (card.currentValue - card.previousValue) * i / 9.0;
            card.trendLine->appendValue(value);
        }
    }
}

void HistoricalComparisonWidget::calculateChanges() {
    for (auto& card : cards_) {
        if (card.previousValue != 0) {
            card.change = card.currentValue - card.previousValue;
            card.changePercent = (card.change / card.previousValue) * 100;
            
            if (card.changePercent > 0) {
                card.trend = "up";
            } else if (card.changePercent < 0) {
                card.trend = "down";
            } else {
                card.trend = "stable";
            }
        }
    }
}

// StatsSettingsDialog implementation
StatsSettingsDialog::StatsSettingsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Statistics Settings");
    setModal(true);
    setupUI();
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
    timeRangeCombo_->setCurrentText(range);
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

void StatsSettingsDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);
    
    // Auto refresh section
    auto* refreshGroup = new QGroupBox("Auto Refresh", this);
    auto* refreshLayout = new QVBoxLayout(refreshGroup);
    
    autoRefreshCheck_ = new QCheckBox("Enable auto refresh", this);
    refreshLayout->addWidget(autoRefreshCheck_);
    
    auto* intervalLayout = new QHBoxLayout();
    intervalLayout->addWidget(new QLabel("Refresh interval:", this));
    
    refreshIntervalSpin_ = new QSpinBox(this);
    refreshIntervalSpin_->setRange(1, 60);
    refreshIntervalSpin_->setSuffix(" seconds");
    intervalLayout->addWidget(refreshIntervalSpin_);
    intervalLayout->addStretch();
    
    refreshLayout->addLayout(intervalLayout);
    layout->addWidget(refreshGroup);
    
    // Display section
    auto* displayGroup = new QGroupBox("Display Options", this);
    auto* displayLayout = new QVBoxLayout(displayGroup);
    
    auto* timeRangeLayout = new QHBoxLayout();
    timeRangeLayout->addWidget(new QLabel("Default time range:", this));
    
    timeRangeCombo_ = new QComboBox(this);
    timeRangeCombo_->addItems({"Last Hour", "Last 24 Hours", "Last Week", "Last Month"});
    timeRangeLayout->addWidget(timeRangeCombo_);
    timeRangeLayout->addStretch();
    
    displayLayout->addLayout(timeRangeLayout);
    
    animationsCheck_ = new QCheckBox("Enable chart animations", this);
    displayLayout->addWidget(animationsCheck_);
    
    layout->addWidget(displayGroup);
    
    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    auto* okButton = new QPushButton("OK", this);
    connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(okButton);
    
    auto* cancelButton = new QPushButton("Cancel", this);
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(cancelButton);
    
    layout->addLayout(buttonLayout);
    
    resize(400, 300);
}

} // namespace llm_re::ui_v2