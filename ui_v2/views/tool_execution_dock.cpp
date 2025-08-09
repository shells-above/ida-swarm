#include "../core/ui_v2_common.h"
#include "tool_execution_dock.h"
#include "../core/theme_manager.h"
#include "../core/ui_constants.h"
#include "../core/ui_utils.h"
#include "../core/agent_controller.h"

namespace llm_re::ui_v2 {

// Utility function for formatting durations
static QString formatDuration(qint64 ms) {
    if (ms < 1000) {
        return QString("%1ms").arg(ms);
    } else if (ms < 60000) {
        return QString("%1s").arg(ms / 1000.0, 0, 'f', 1);
    } else if (ms < 3600000) {
        int minutes = ms / 60000;
        int seconds = (ms % 60000) / 1000;
        return QString("%1m %2s").arg(minutes).arg(seconds);
    } else {
        int hours = ms / 3600000;
        int minutes = (ms % 3600000) / 60000;
        return QString("%1h %2m").arg(hours).arg(minutes);
    }
}


// PerformanceChartWidget implementation

PerformanceChartWidget::PerformanceChartWidget(QWidget* parent)
    : BaseStyledWidget(parent)
{
    setMouseTracking(true);
    setMinimumSize(400, 300);
}

void PerformanceChartWidget::setExecutions(const QList<ToolExecution>& executions) {
    executions_ = executions;
    calculateData();
    update();
}

void PerformanceChartWidget::setTimeRange(const QDateTime& start, const QDateTime& end) {
    startTime_ = start;
    endTime_ = end;
    calculateData();
    update();
}

void PerformanceChartWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Background
    painter.fillRect(rect(), ThemeManager::instance().colors().surface);
    
    // Calculate layout
    chartRect_ = rect().adjusted(60, 20, -20, -60);
    
    // Draw chart based on type
    switch (chartType_) {
    case LineChart:
        drawLineChart(&painter);
        break;
    case BarChart:
        drawBarChart(&painter);
        break;
    case PieChart:
        drawPieChart(&painter);
        break;
    case ScatterPlot:
        drawScatterPlot(&painter);
        break;
    }
    
    
    // Tooltip
    if (hoveredPoint_ >= 0 && hoveredPoint_ < dataPoints_.size()) {
        const DataPoint& point = dataPoints_[hoveredPoint_];
        
        QString tooltip = QString("%1: %2").arg(point.label).arg(point.value);
        
        QFontMetrics fm(painter.font());
        QRect tooltipRect = fm.boundingRect(tooltip);
        tooltipRect.adjust(-5, -5, 5, 5);
        tooltipRect.moveTopLeft(QCursor::pos() - mapToGlobal(QPoint(0, 0)) + QPoint(10, 10));
        
        painter.fillRect(tooltipRect, ThemeManager::instance().colors().surface);
        painter.setPen(ThemeManager::instance().colors().textPrimary);
        painter.drawText(tooltipRect, Qt::AlignCenter, tooltip);
    }
}

void PerformanceChartWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        for (int i = 0; i < dataPoints_.size(); ++i) {
            if (dataPoints_[i].rect.contains(event->pos())) {
                emit dataPointClicked(dataPoints_[i].label, dataPoints_[i].value);
                break;
            }
        }
    }
}

void PerformanceChartWidget::mouseMoveEvent(QMouseEvent* event) {
    int oldHovered = hoveredPoint_;
    hoveredPoint_ = -1;
    
    for (int i = 0; i < dataPoints_.size(); ++i) {
        if (dataPoints_[i].rect.contains(event->pos())) {
            hoveredPoint_ = i;
            break;
        }
    }
    
    if (hoveredPoint_ != oldHovered) {
        update();
    }
}

void PerformanceChartWidget::leaveEvent(QEvent* event) {
    Q_UNUSED(event);
    hoveredPoint_ = -1;
    update();
}

void PerformanceChartWidget::calculateData() {
    dataPoints_.clear();
    
    if (executions_.isEmpty()) {
        return;
    }
    
    // Group data
    QHash<QString, double> groupedData;
    QHash<QString, int> groupedCounts;
    
    for (const ToolExecution& exec : executions_) {
        // Filter by time range
        if (startTime_.isValid() && exec.startTime < startTime_) continue;
        if (endTime_.isValid() && exec.endTime > endTime_) continue;
        
        QString group;
        if (groupBy_ == "tool") {
            group = exec.toolName;
        } else if (groupBy_ == "status") {
            switch (exec.state) {
            case ToolExecutionState::Completed: group = "Success"; break;
            case ToolExecutionState::Failed: group = "Failed"; break;
            case ToolExecutionState::Cancelled: group = "Cancelled"; break;
            default: group = "Other"; break;
            }
        } else if (groupBy_ == "hour") {
            group = exec.startTime.toString("yyyy-MM-dd HH:00");
        }
        
        double value = 0;
        switch (metric_) {
        case ExecutionTime:
            value = exec.getDuration();
            break;
        case SuccessRate:
            value = (exec.state == ToolExecutionState::Completed) ? 100 : 0;
            break;
        case ThroughputRate:
            value = 1; // Count
            break;
        case ErrorRate:
            value = (exec.state == ToolExecutionState::Failed) ? 100 : 0;
            break;
        }
        
        groupedData[group] += value;
        groupedCounts[group]++;
    }
    
    // Calculate averages for rate metrics
    if (metric_ == SuccessRate || metric_ == ErrorRate) {
        for (auto it = groupedData.begin(); it != groupedData.end(); ++it) {
            it.value() /= groupedCounts[it.key()];
        }
    }
    
    // Create data points
    int colorIndex = 0;
    const std::vector<QColor>& colors = ThemeManager::instance().chartSeriesColors();
    
    for (auto it = groupedData.begin(); it != groupedData.end(); ++it) {
        DataPoint point;
        point.label = it.key();
        point.value = it.value();
        point.color = colors[colorIndex % colors.size()];
        colorIndex++;
        dataPoints_.append(point);
    }
    
    // Sort by value for better visualization
    std::sort(dataPoints_.begin(), dataPoints_.end(),
             [](const DataPoint& a, const DataPoint& b) {
        return a.value > b.value;
    });
}

void PerformanceChartWidget::drawLineChart(QPainter* painter) {
    if (dataPoints_.isEmpty()) return;
    
    drawAxes(painter);
    
    // Find min/max values
    double minValue = dataPoints_[0].value;
    double maxValue = dataPoints_[0].value;
    for (const DataPoint& point : dataPoints_) {
        minValue = std::min(minValue, point.value);
        maxValue = std::max(maxValue, point.value);
    }
    
    double range = maxValue - minValue;
    if (range == 0) range = 1;
    
    // Draw line
    QPainterPath path;
    QVector<QPointF> points;
    
    for (int i = 0; i < dataPoints_.size(); ++i) {
        qreal x = chartRect_.left() + (chartRect_.width() * i) / (dataPoints_.size() - 1);
        qreal y = chartRect_.bottom() - 
                 (chartRect_.height() * (dataPoints_[i].value - minValue)) / range;
        
        QPointF point(x, y);
        points.append(point);
        
        if (i == 0) {
            path.moveTo(point);
        } else {
            path.lineTo(point);
        }
        
        // Store rect for interaction
        dataPoints_[i].rect = QRectF(x - 5, y - 5, 10, 10);
    }
    
    // Draw line
    painter->setPen(QPen(ThemeManager::instance().colors().primary, 2));
    painter->drawPath(path);
    
    // Draw points
    painter->setBrush(ThemeManager::instance().colors().primary);
    for (int i = 0; i < points.size(); ++i) {
        if (i == hoveredPoint_) {
            painter->drawEllipse(points[i], 6, 6);
        } else {
            painter->drawEllipse(points[i], 4, 4);
        }
    }
}

void PerformanceChartWidget::drawBarChart(QPainter* painter) {
    if (dataPoints_.isEmpty()) return;
    
    drawAxes(painter);
    
    // Find max value
    double maxValue = 0;
    for (const DataPoint& point : dataPoints_) {
        maxValue = std::max(maxValue, point.value);
    }
    
    if (maxValue == 0) maxValue = 1;
    
    // Draw bars
    qreal barWidth = chartRect_.width() / dataPoints_.size() * 0.8;
    qreal spacing = chartRect_.width() / dataPoints_.size() * 0.2;
    
    for (int i = 0; i < dataPoints_.size(); ++i) {
        qreal x = chartRect_.left() + i * (barWidth + spacing) + spacing / 2;
        qreal height = (chartRect_.height() * dataPoints_[i].value) / maxValue;
        qreal y = chartRect_.bottom() - height;
        
        QRectF barRect(x, y, barWidth, height);
        
        QColor color = dataPoints_[i].color;
        if (i == hoveredPoint_) {
            color = color.lighter(110);
        }
        
        painter->fillRect(barRect, color);
        painter->setPen(color.darker(120));
        painter->drawRect(barRect);
        
        // Store rect for interaction
        dataPoints_[i].rect = barRect;
        
        // Value label
        if (barRect.height() > 20) {
            painter->setPen(ThemeManager::instance().colors().textPrimary);
            painter->drawText(barRect, Qt::AlignCenter, QString::number(dataPoints_[i].value, 'f', 0));
        }
    }
}

void PerformanceChartWidget::drawPieChart(QPainter* painter) {
    if (dataPoints_.isEmpty()) return;
    
    // Calculate total
    double total = 0;
    for (const DataPoint& point : dataPoints_) {
        total += point.value;
    }
    
    if (total == 0) return;
    
    // Draw pie
    QRectF pieRect = chartRect_;
    qreal size = std::min(pieRect.width(), pieRect.height()) * 0.8;
    pieRect.setSize(QSizeF(size, size));
    pieRect.moveCenter(chartRect_.center());
    
    qreal startAngle = 90 * 16; // Start at top
    
    for (int i = 0; i < dataPoints_.size(); ++i) {
        qreal spanAngle = (dataPoints_[i].value / total) * 360 * 16;
        
        QColor color = dataPoints_[i].color;
        if (i == hoveredPoint_) {
            // Explode slice
            qreal midAngle = (startAngle + spanAngle / 2) / 16.0 * M_PI / 180.0;
            QPointF offset(10 * cos(midAngle), -10 * sin(midAngle));
            QRectF sliceRect = pieRect.translated(offset);
            
            painter->setBrush(color.lighter(110));
            painter->setPen(color.darker(120));
            painter->drawPie(sliceRect, startAngle, spanAngle);
            
            dataPoints_[i].rect = sliceRect;
        } else {
            painter->setBrush(color);
            painter->setPen(color.darker(120));
            painter->drawPie(pieRect, startAngle, spanAngle);
            
            dataPoints_[i].rect = pieRect;
        }
        
        startAngle += spanAngle;
    }
}

void PerformanceChartWidget::drawScatterPlot(QPainter* painter) {
    if (dataPoints_.isEmpty()) return;
    
    drawAxes(painter);
    
    // For scatter plot, we need two dimensions
    // Use index as X and value as Y
    
    double minValue = dataPoints_[0].value;
    double maxValue = dataPoints_[0].value;
    for (const DataPoint& point : dataPoints_) {
        minValue = std::min(minValue, point.value);
        maxValue = std::max(maxValue, point.value);
    }
    
    double range = maxValue - minValue;
    if (range == 0) range = 1;
    
    // Draw points
    for (int i = 0; i < dataPoints_.size(); ++i) {
        qreal x = chartRect_.left() + (chartRect_.width() * i) / (dataPoints_.size() - 1);
        qreal y = chartRect_.bottom() - 
                 (chartRect_.height() * (dataPoints_[i].value - minValue)) / range;
        
        QColor color = dataPoints_[i].color;
        if (i == hoveredPoint_) {
            painter->setBrush(color.lighter(110));
            painter->drawEllipse(QPointF(x, y), 8, 8);
        } else {
            painter->setBrush(color);
            painter->drawEllipse(QPointF(x, y), 6, 6);
        }
        
        // Store rect for interaction
        dataPoints_[i].rect = QRectF(x - 6, y - 6, 12, 12);
    }
}

void PerformanceChartWidget::drawAxes(QPainter* painter) {
    painter->setPen(ThemeManager::instance().colors().textSecondary);
    
    // X axis
    painter->drawLine(chartRect_.bottomLeft(), chartRect_.bottomRight());
    
    // Y axis
    painter->drawLine(chartRect_.bottomLeft(), chartRect_.topLeft());
    
    // Y axis labels
    int labelCount = 5;
    double maxValue = 0;
    for (const DataPoint& point : dataPoints_) {
        maxValue = std::max(maxValue, point.value);
    }
    
    for (int i = 0; i <= labelCount; ++i) {
        qreal y = chartRect_.bottom() - (chartRect_.height() * i) / labelCount;
        double value = maxValue * i / labelCount;
        
        painter->drawLine(chartRect_.left() - 5, y, chartRect_.left() + 5, y);
        
        QString label;
        if (metric_ == ExecutionTime) {
            label = ::llm_re::ui_v2::formatDuration(static_cast<qint64>(value));
        } else if (metric_ == SuccessRate || metric_ == ErrorRate) {
            label = QString("%1%").arg(value, 0, 'f', 0);
        } else {
            label = QString::number(value, 'f', 0);
        }
        
        QRectF labelRect(chartRect_.left() - 55, y - 10, 50, 20);
        painter->drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, label);
    }
    
    // X axis labels
    if (chartType_ != PieChart) {
        for (int i = 0; i < dataPoints_.size(); ++i) {
            qreal x = chartRect_.left() + (chartRect_.width() * i) / (dataPoints_.size() - 1);
            
            painter->save();
            painter->translate(x, chartRect_.bottom() + 5);
            painter->rotate(45);
            painter->drawText(0, 0, dataPoints_[i].label);
            painter->restore();
        }
    }
}


// formatDuration is now a static utility function above

// ToolExecutionDock implementation

ToolExecutionDock::ToolExecutionDock(QWidget* parent)
    : BaseStyledWidget(parent)
{
    setupUI();
    connectSignals();
    loadSettings();
    
    // Start update timer
    updateTimer_ = new QTimer(this);
    updateTimer_->setInterval(1000); // Update every second
    connect(updateTimer_, &QTimer::timeout, this, &ToolExecutionDock::updateRunningExecutions);
    updateTimer_->start();
    
    // Auto-save timer
    autoSaveTimer_ = new QTimer(this);
    autoSaveTimer_->setInterval(60000); // Save every minute
    connect(autoSaveTimer_, &QTimer::timeout, this, &ToolExecutionDock::autoSave);
    autoSaveTimer_->start();
}

ToolExecutionDock::~ToolExecutionDock() {
    saveSettings();
}

void ToolExecutionDock::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    // Create toolbar
    createToolBar();
    layout->addWidget(toolBar_);
    
    // Create main splitter
    mainSplitter_ = new QSplitter(Qt::Horizontal, this);
    
    // Create views
    createViews();
    
    // Connect tab change signal to update detail panel visibility
    connect(viewTabs_, &QTabWidget::currentChanged, [this](int index) {
        detailPanel_->setVisible(index == 0); // Only show for list view
    });
    
    // Create detail panel
    createDetailPanel();
    
    mainSplitter_->addWidget(viewTabs_);
    mainSplitter_->addWidget(detailPanel_);
    mainSplitter_->setStretchFactor(0, 2);
    mainSplitter_->setStretchFactor(1, 1);
    
    layout->addWidget(mainSplitter_);
    
    // Create context menu
    createContextMenu();
}

void ToolExecutionDock::createToolBar() {
    toolBar_ = new QToolBar(this);
    toolBar_->setIconSize(QSize(16, 16));
    
    // Filters
    auto* toolLabel = new QLabel(tr("Tool:"), this);
    toolBar_->addWidget(toolLabel);
    
    toolFilterCombo_ = new QComboBox(this);
    toolFilterCombo_->addItem(tr("All Tools"));
    toolBar_->addWidget(toolFilterCombo_);
    
    auto* statusLabel = new QLabel(tr("Status:"), this);
    toolBar_->addWidget(statusLabel);
    
    statusFilterCombo_ = new QComboBox(this);
    statusFilterCombo_->addItems({tr("All"), tr("Running"), tr("Success"), 
                                 tr("Failed"), tr("Cancelled")});
    toolBar_->addWidget(statusFilterCombo_);
    
    toolBar_->addSeparator();
    
    // Actions
    manualExecuteAction_ = toolBar_->addAction(ThemeManager::instance().themedIcon("play"), tr("Manual Execute"));
    
    toolBar_->addSeparator();
    
    autoScrollAction_ = toolBar_->addAction(ThemeManager::instance().themedIcon("auto-scroll"), tr("Auto Scroll"));
    autoScrollAction_->setCheckable(true);
    autoScrollAction_->setChecked(autoScroll_);
    
    clearHistoryAction_ = toolBar_->addAction(ThemeManager::instance().themedIcon("edit-clear"), tr("Clear History"));
}

void ToolExecutionDock::createViews() {
    viewTabs_ = new QTabWidget(this);
    viewTabs_->setTabPosition(QTabWidget::South);
    
    // Create model
    model_ = new ExecutionModel(this);
    proxyModel_ = new ExecutionFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);
    
    // List view (tree)
    treeView_ = new QTreeView(this);
    treeView_->setModel(proxyModel_);
    treeView_->setAlternatingRowColors(true);
    treeView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    treeView_->setContextMenuPolicy(Qt::CustomContextMenu);
    treeView_->setSortingEnabled(true);
    treeView_->header()->setStretchLastSection(true);
    // Sort by start time column in descending order (newest first)
    treeView_->sortByColumn(ExecutionModel::StartTimeColumn, Qt::DescendingOrder);
    viewTabs_->addTab(treeView_, tr("List"));
    
    // Performance view
    chartWidget_ = new PerformanceChartWidget(this);
    viewTabs_->addTab(chartWidget_, tr("Performance"));
}

void ToolExecutionDock::createDetailPanel() {
    detailPanel_ = new QWidget(this);
    auto* layout = new QVBoxLayout(detailPanel_);
    
    // Tool info
    auto* infoLayout = new QFormLayout();
    
    detailToolLabel_ = new QLabel(this);
    QFont font = detailToolLabel_->font();
    font.setPointSize(font.pointSize() + 2);
    font.setWeight(QFont::DemiBold);
    detailToolLabel_->setFont(font);
    infoLayout->addRow(tr("Tool:"), detailToolLabel_);
    
    detailStatusLabel_ = new QLabel(this);
    infoLayout->addRow(tr("Status:"), detailStatusLabel_);
    
    detailDurationLabel_ = new QLabel(this);
    infoLayout->addRow(tr("Duration:"), detailDurationLabel_);
    
    layout->addLayout(infoLayout);
    
    // Parameters
    auto* paramsGroup = new QGroupBox(tr("Parameters"), this);
    auto* paramsLayout = new QVBoxLayout(paramsGroup);
    
    detailParametersEdit_ = new QTextEdit(this);
    detailParametersEdit_->setReadOnly(true);
    detailParametersEdit_->setMaximumHeight(100);
    paramsLayout->addWidget(detailParametersEdit_);
    
    layout->addWidget(paramsGroup);
    
    // Output
    auto* outputGroup = new QGroupBox(tr("Output"), this);
    auto* outputLayout = new QVBoxLayout(outputGroup);
    
    detailOutputEdit_ = new QTextEdit(this);
    detailOutputEdit_->setReadOnly(true);
    detailOutputEdit_->setFont(QFont("Consolas", 9));
    outputLayout->addWidget(detailOutputEdit_);
    
    layout->addWidget(outputGroup);
    layout->addStretch();
}

void ToolExecutionDock::createContextMenu() {
    contextMenu_ = new QMenu(this);
    
    contextMenu_->addAction(ThemeManager::instance().themedIcon("edit-copy"), tr("Copy Tool Name"),
                           [this]() {
        if (!selectedExecution_.isNull()) {
            QApplication::clipboard()->setText(execution(selectedExecution_).toolName);
        }
    });
    
    contextMenu_->addAction(ThemeManager::instance().themedIcon("edit-copy"), tr("Copy Parameters"),
                           [this]() {
        if (!selectedExecution_.isNull()) {
            QJsonDocument doc(execution(selectedExecution_).parameters);
            QApplication::clipboard()->setText(doc.toJson(QJsonDocument::Indented));
        }
    });
    
    contextMenu_->addAction(ThemeManager::instance().themedIcon("edit-copy"), tr("Copy Output"),
                           [this]() {
        if (!selectedExecution_.isNull()) {
            QApplication::clipboard()->setText(execution(selectedExecution_).output);
        }
    });
    
    contextMenu_->addSeparator();
    
    contextMenu_->addAction(ThemeManager::instance().themedIcon("bookmark"), tr("Add to Favorites"),
                           [this]() {
        if (!selectedExecution_.isNull()) {
            ToolExecution exec = execution(selectedExecution_);
            bool ok;
            QString name = QInputDialog::getText(
                this, tr("Add to Favorites"),
                tr("Favorite name:"),
                QLineEdit::Normal,
                exec.toolName, &ok
            );
            if (ok && !name.isEmpty()) {
                addFavorite(exec.toolName, exec.parameters);
            }
        }
    });
}

void ToolExecutionDock::connectSignals() {
    // Filters
    connect(toolFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolExecutionDock::onFilterChanged);
    
    connect(statusFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolExecutionDock::onFilterChanged);
    
    // Actions
    connect(manualExecuteAction_, &QAction::triggered,
            this, &ToolExecutionDock::onManualExecute);
    
    connect(autoScrollAction_, &QAction::toggled,
            [this](bool checked) { autoScroll_ = checked; });
    
    connect(clearHistoryAction_, &QAction::triggered,
            this, &ToolExecutionDock::clearHistory);
    
    // Tree/Table views
    connect(treeView_, &QTreeView::clicked,
            this, &ToolExecutionDock::onExecutionClicked);
    
    connect(treeView_, &QTreeView::doubleClicked,
            this, &ToolExecutionDock::onExecutionDoubleClicked);
    
    
    connect(treeView_, &QWidget::customContextMenuRequested,
            this, &ToolExecutionDock::onCustomContextMenu);
    
    
    connect(treeView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ToolExecutionDock::onSelectionChanged);
    
    
    
    // Chart view
    connect(chartWidget_, &PerformanceChartWidget::dataPointClicked,
            this, &ToolExecutionDock::onChartDataPointClicked);
    
}

QUuid ToolExecutionDock::startExecution(const QString& toolName, const QJsonObject& parameters) {
    ToolExecution exec;
    exec.id = QUuid::createUuid();
    exec.toolName = toolName;
    exec.parameters = parameters;
    exec.startTime = QDateTime::currentDateTime();
    exec.state = ToolExecutionState::Running;
    exec.description = parameters.value("description").toString();
    
    // Add to list
    executions_.append(exec);
    executionMap_[exec.id] = &executions_.last();
    
    // Update model
    model_->addExecution(exec);
    
    // Update tool stats
    if (tools_.contains(toolName)) {
        tools_[toolName].executionCount++;
    } else {
        ToolInfo info;
        info.name = toolName;
        info.executionCount = 1;
        tools_[toolName] = info;
        
        // Update filter combo
        toolFilterCombo_->addItem(toolName);
    }
    
    // Auto scroll to new execution
    if (autoScroll_) {
        showExecution(exec.id);
    }
    
    // Update views
    updateMetrics();
    
    emit executionStarted(exec.id);
    return exec.id;
}


void ToolExecutionDock::completeExecution(const QUuid& id, bool success, const QString& output) {
    if (executionMap_.contains(id)) {
        ToolExecution& exec = *executionMap_[id];
        exec.endTime = QDateTime::currentDateTime();
        exec.duration = exec.startTime.msecsTo(exec.endTime);
        exec.state = success ? ToolExecutionState::Completed : ToolExecutionState::Failed;
        exec.output = output;
        
        if (!success && !output.isEmpty()) {
            exec.errorMessage = output;
        }
        
        // Update model
        model_->updateExecution(id, exec);
        
        // Update tool stats
        if (tools_.contains(exec.toolName)) {
            tools_[exec.toolName].totalDuration += exec.getDuration();
            if (success) {
                tools_[exec.toolName].successCount++;
            } else {
                tools_[exec.toolName].failureCount++;
            }
        }
        
        // Update detail panel if selected
        if (selectedExecution_ == id) {
            detailStatusLabel_->setText(success ? tr("Success") : tr("Failed"));
            detailDurationLabel_->setText(::llm_re::ui_v2::formatDuration(exec.getDuration()));
            
            // Format output as JSON
            QJsonParseError error;
            QJsonDocument doc = QJsonDocument::fromJson(output.toUtf8(), &error);
            if (error.error == QJsonParseError::NoError) {
                detailOutputEdit_->setPlainText(doc.toJson(QJsonDocument::Indented));
            } else {
                // If not valid JSON, wrap in a JSON object
                QJsonObject wrapper;
                wrapper["raw_output"] = output;
                QJsonDocument wrapperDoc(wrapper);
                detailOutputEdit_->setPlainText(wrapperDoc.toJson(QJsonDocument::Indented));
            }
        }
        
        // Update views
        updateMetrics();
        
        emit executionCompleted(id, success);
        
        if (!success && !output.isEmpty()) {
            emit outputReceived(id, output);
        }
    }
}

void ToolExecutionDock::cancelExecution(const QUuid& id) {
    if (executionMap_.contains(id)) {
        ToolExecution& exec = *executionMap_[id];
        if (exec.state == ToolExecutionState::Running) {
            exec.endTime = QDateTime::currentDateTime();
            exec.duration = exec.startTime.msecsTo(exec.endTime);
            exec.state = ToolExecutionState::Cancelled;
            exec.output = tr("Execution cancelled by user");
            
            // Update model
            model_->updateExecution(id, exec);
            
            // Update detail panel if selected
            if (selectedExecution_ == id) {
                detailStatusLabel_->setText(tr("Cancelled"));
                detailDurationLabel_->setText(::llm_re::ui_v2::formatDuration(exec.getDuration()));
                // Format output as JSON
                QJsonParseError error;
                QJsonDocument doc = QJsonDocument::fromJson(exec.output.toUtf8(), &error);
                if (error.error == QJsonParseError::NoError) {
                    detailOutputEdit_->setPlainText(doc.toJson(QJsonDocument::Indented));
                } else {
                    // If not valid JSON, wrap in a JSON object
                    QJsonObject wrapper;
                    wrapper["raw_output"] = exec.output;
                    QJsonDocument wrapperDoc(wrapper);
                    detailOutputEdit_->setPlainText(wrapperDoc.toJson(QJsonDocument::Indented));
                }
            }
            
            emit executionCancelled(id);
        }
    }
}

QList<ToolExecution> ToolExecutionDock::executions() const {
    return executions_;
}

ToolExecution ToolExecutionDock::execution(const QUuid& id) const {
    if (executionMap_.contains(id)) {
        return *executionMap_[id];
    }
    return ToolExecution();
}

QList<ToolExecution> ToolExecutionDock::runningExecutions() const {
    QList<ToolExecution> running;
    for (const ToolExecution& exec : executions_) {
        if (exec.state == ToolExecutionState::Running) {
            running.append(exec);
        }
    }
    return running;
}

void ToolExecutionDock::showExecution(const QUuid& id) {
    selectedExecution_ = id;
    
    // Update detail panel
    if (executionMap_.contains(id)) {
        const ToolExecution& exec = *executionMap_[id];
        
        detailToolLabel_->setText(exec.toolName);
        
        QString statusText;
        switch (exec.state) {
        case ToolExecutionState::Pending: statusText = tr("Pending"); break;
        case ToolExecutionState::Running: statusText = tr("Running"); break;
        case ToolExecutionState::Completed: statusText = tr("Success"); break;
        case ToolExecutionState::Failed: statusText = tr("Failed"); break;
        case ToolExecutionState::Cancelled: statusText = tr("Cancelled"); break;
        }
        detailStatusLabel_->setText(statusText);
        
        if (exec.getDuration() > 0) {
            detailDurationLabel_->setText(::llm_re::ui_v2::formatDuration(exec.getDuration()));
        } else {
            detailDurationLabel_->setText(tr("In progress..."));
        }
        
        
        QJsonDocument doc(exec.parameters);
        detailParametersEdit_->setPlainText(doc.toJson(QJsonDocument::Indented));
        
        // Format output as JSON
        QJsonParseError error;
        QJsonDocument outputDoc = QJsonDocument::fromJson(exec.output.toUtf8(), &error);
        if (error.error == QJsonParseError::NoError) {
            detailOutputEdit_->setPlainText(outputDoc.toJson(QJsonDocument::Indented));
        } else {
            // If not valid JSON, wrap in a JSON object
            QJsonObject wrapper;
            wrapper["raw_output"] = exec.output;
            QJsonDocument wrapperDoc(wrapper);
            detailOutputEdit_->setPlainText(wrapperDoc.toJson(QJsonDocument::Indented));
        }
        
        
        // Show in current view
        int currentTabIndex = viewTabs_->currentIndex();
        if (currentTabIndex == 0) {
            // List view
            QModelIndex index = model_->indexForId(id);
            if (index.isValid()) {
                QModelIndex proxyIndex = proxyModel_->mapFromSource(index);
                treeView_->scrollTo(proxyIndex);
                treeView_->setCurrentIndex(proxyIndex);
            }
        }
    }
}

void ToolExecutionDock::setViewMode(const QString& mode) {
    int index = 0;
    if (mode == "performance") index = 1;
    
    viewTabs_->setCurrentIndex(index);
}

void ToolExecutionDock::setTimeRange(const QDateTime& start, const QDateTime& end) {
    timeRangeStart_ = start;
    timeRangeEnd_ = end;
    chartWidget_->setTimeRange(start, end);
    applyFilters();
}

void ToolExecutionDock::setToolFilter(const QStringList& tools) {
    toolFilter_ = tools;
    applyFilters();
}

void ToolExecutionDock::setStatusFilter(const QList<ToolExecutionState>& states) {
    statusFilter_ = states;
    applyFilters();
}

void ToolExecutionDock::clearFilters() {
    toolFilterCombo_->setCurrentIndex(0);
    statusFilterCombo_->setCurrentIndex(0);
    toolFilter_.clear();
    statusFilter_.clear();
    applyFilters();
}


void ToolExecutionDock::registerTool(const QString& name, const QString& description) {
    if (!tools_.contains(name)) {
        ToolInfo info;
        info.name = name;
        info.description = description;
        tools_[name] = info;
        
        toolFilterCombo_->addItem(name);
    }
}

void ToolExecutionDock::setToolEnabled(const QString& name, bool enabled) {
    if (tools_.contains(name)) {
        tools_[name].enabled = enabled;
    }
}

QStringList ToolExecutionDock::availableTools() const {
    return tools_.keys();
}

QStringList ToolExecutionDock::enabledTools() const {
    QStringList enabled;
    for (auto it = tools_.begin(); it != tools_.end(); ++it) {
        if (it.value().enabled) {
            enabled.append(it.key());
        }
    }
    return enabled;
}

void ToolExecutionDock::addFavorite(const QString& toolName, const QJsonObject& parameters) {
    FavoriteExecution fav;
    fav.name = toolName;
    fav.toolName = toolName;
    fav.parameters = parameters;
    favorites_.append(fav);
    saveSettings();
}

void ToolExecutionDock::removeFavorite(const QString& name) {
    favorites_.erase(
        std::remove_if(favorites_.begin(), favorites_.end(),
                      [&name](const FavoriteExecution& fav) {
                          return fav.name == name;
                      }),
        favorites_.end());
    saveSettings();
}

QStringList ToolExecutionDock::favorites() const {
    QStringList names;
    for (const FavoriteExecution& fav : favorites_) {
        names.append(fav.name);
    }
    return names;
}

void ToolExecutionDock::executeFavorite(const QString& name) {
    for (const FavoriteExecution& fav : favorites_) {
        if (fav.name == name) {
            startExecution(fav.toolName, fav.parameters);
            break;
        }
    }
}

QJsonObject ToolExecutionDock::exportState() const {
    QJsonObject state;
    
    // Export all executions
    QJsonArray executionsArray;
    for (const auto& exec : executions_) {
        QJsonObject execObj;
        execObj["id"] = exec.id.toString();
        execObj["toolName"] = exec.toolName;
        execObj["parameters"] = exec.parameters;
        execObj["state"] = static_cast<int>(exec.state);
        execObj["output"] = exec.output;
        execObj["error"] = exec.errorMessage;
        execObj["startTime"] = exec.startTime.toString(Qt::ISODate);
        if (exec.endTime.isValid()) {
            execObj["endTime"] = exec.endTime.toString(Qt::ISODate);
        }
        execObj["duration"] = exec.duration;
        executionsArray.append(execObj);
    }
    state["executions"] = executionsArray;
    
    // Export view state
    const QStringList viewModes = {"list", "performance"};
    int currentTab = viewTabs_->currentIndex();
    state["viewMode"] = (currentTab >= 0 && currentTab < viewModes.size()) ? viewModes[currentTab] : "list";
    state["autoScroll"] = autoScroll_;
    
    // Export filters
    QJsonObject filters;
    QJsonArray toolFilters;
    for (const QString& tool : toolFilter_) {
        toolFilters.append(tool);
    }
    filters["tools"] = toolFilters;
    
    QJsonArray statusFilters;
    for (const ToolExecutionState& status : statusFilter_) {
        statusFilters.append(static_cast<int>(status));
    }
    filters["statuses"] = statusFilters;
    
    if (timeRangeStart_.isValid()) {
        filters["startTime"] = timeRangeStart_.toString(Qt::ISODate);
    }
    if (timeRangeEnd_.isValid()) {
        filters["endTime"] = timeRangeEnd_.toString(Qt::ISODate);
    }
    state["filters"] = filters;
    
    // Export favorites
    QJsonArray favoritesArray;
    for (const auto& fav : favorites_) {
        QJsonObject favObj;
        favObj["name"] = fav.name;
        favObj["toolName"] = fav.toolName;
        favObj["parameters"] = fav.parameters;
        favoritesArray.append(favObj);
    }
    state["favorites"] = favoritesArray;
    
    // Export tool registry
    QJsonObject toolsObj;
    for (auto it = tools_.begin(); it != tools_.end(); ++it) {
        QJsonObject toolObj;
        toolObj["description"] = it.value().description;
        toolObj["enabled"] = it.value().enabled;
        toolsObj[it.key()] = toolObj;
    }
    state["tools"] = toolsObj;
    
    // Export statistics
    state["completedCount"] = completedCount_;
    state["failedCount"] = failedCount_;
    
    return state;
}

void ToolExecutionDock::importState(const QJsonObject& state) {
    // Clear existing executions
    clearHistory();
    
    // Import executions
    if (state.contains("executions")) {
        QJsonArray executionsArray = state["executions"].toArray();
        for (const QJsonValue& val : executionsArray) {
            QJsonObject execObj = val.toObject();
            
            ToolExecution exec;
            exec.id = QUuid(execObj["id"].toString());
            exec.toolName = execObj["toolName"].toString();
            exec.parameters = execObj["parameters"].toObject();
            exec.state = static_cast<ToolExecutionState>(execObj["state"].toInt());
            exec.output = execObj["output"].toString();
            exec.errorMessage = execObj["error"].toString();
            exec.startTime = QDateTime::fromString(execObj["startTime"].toString(), Qt::ISODate);
            if (execObj.contains("endTime")) {
                exec.endTime = QDateTime::fromString(execObj["endTime"].toString(), Qt::ISODate);
            }
            exec.duration = execObj["duration"].toInt();
            
            executions_.append(exec);
            executionMap_[exec.id] = &executions_.last();
            model_->addExecution(exec);
        }
    }
    
    // Import view state
    if (state.contains("viewMode")) {
        setViewMode(state["viewMode"].toString());
    }
    
    if (state.contains("autoScroll")) {
        autoScroll_ = state["autoScroll"].toBool();
    }
    
    // Import filters
    if (state.contains("filters")) {
        QJsonObject filters = state["filters"].toObject();
        
        if (filters.contains("tools")) {
            QJsonArray toolFilters = filters["tools"].toArray();
            QStringList tools;
            for (const QJsonValue& tool : toolFilters) {
                tools.append(tool.toString());
            }
            setToolFilter(tools);
        }
        
        if (filters.contains("statuses")) {
            QJsonArray statusFilters = filters["statuses"].toArray();
            QList<ToolExecutionState> statuses;
            for (const QJsonValue& status : statusFilters) {
                statuses.append(static_cast<ToolExecutionState>(status.toInt()));
            }
            setStatusFilter(statuses);
        }
        
        QDateTime startTime, endTime;
        if (filters.contains("startTime")) {
            startTime = QDateTime::fromString(filters["startTime"].toString(), Qt::ISODate);
        }
        if (filters.contains("endTime")) {
            endTime = QDateTime::fromString(filters["endTime"].toString(), Qt::ISODate);
        }
        if (startTime.isValid() || endTime.isValid()) {
            setTimeRange(startTime, endTime);
        }
    }
    
    // Import favorites
    if (state.contains("favorites")) {
        favorites_.clear();
        QJsonArray favoritesArray = state["favorites"].toArray();
        for (const QJsonValue& val : favoritesArray) {
            QJsonObject favObj = val.toObject();
            FavoriteExecution fav;
            fav.name = favObj["name"].toString();
            fav.toolName = favObj["toolName"].toString();
            fav.parameters = favObj["parameters"].toObject();
            favorites_.append(fav);
        }
    }
    
    // Import tool registry
    if (state.contains("tools")) {
        QJsonObject toolsObj = state["tools"].toObject();
        for (auto it = toolsObj.begin(); it != toolsObj.end(); ++it) {
            QJsonObject toolObj = it.value().toObject();
            if (tools_.contains(it.key())) {
                tools_[it.key()].enabled = toolObj["enabled"].toBool();
            } else {
                ToolInfo info;
                info.description = toolObj["description"].toString();
                info.enabled = toolObj["enabled"].toBool();
                tools_[it.key()] = info;
            }
        }
    }
    
    // Import statistics
    if (state.contains("completedCount")) {
        completedCount_ = state["completedCount"].toInt();
    }
    if (state.contains("failedCount")) {
        failedCount_ = state["failedCount"].toInt();
    }
    
    updateMetrics();
}


void ToolExecutionDock::clearHistory() {
    auto reply = QMessageBox::question(
        this, tr("Clear History"),
        tr("Clear all execution history?"),
        QMessageBox::Yes | QMessageBox::No
    );
    
    if (reply == QMessageBox::Yes) {
        executions_.clear();
        executionMap_.clear();
        model_->clear();
        
        // Reset tool stats
        for (auto& tool : tools_) {
            tool.executionCount = 0;
            tool.successCount = 0;
            tool.failureCount = 0;
            tool.totalDuration = 0;
        }
        
        updateMetrics();
    }
}

void ToolExecutionDock::updateMetrics() {
    
    // Update chart
    chartWidget_->setExecutions(executions_);
    
    emit metricsUpdated();
}

void ToolExecutionDock::onThemeChanged() {
    // Update views
    update();
}

void ToolExecutionDock::onViewModeChanged(int index) {
    // This function is no longer used since we removed the view mode combo
    // Users can click the tabs directly
    Q_UNUSED(index);
}

void ToolExecutionDock::onExecutionClicked(const QModelIndex& index) {
    if (index.isValid()) {
        QUuid id = index.data(ExecutionModel::IdRole).toUuid();
        showExecution(id);
    }
}

void ToolExecutionDock::onExecutionDoubleClicked(const QModelIndex& index) {
    if (index.isValid()) {
        QUuid id = index.data(ExecutionModel::IdRole).toUuid();
        emit executionStarted(id); // Could be used to navigate to source
    }
}

void ToolExecutionDock::onSelectionChanged() {
    // Update context menu state
    QItemSelectionModel* selModel = nullptr;
    int currentTabIndex = viewTabs_->currentIndex();
    if (currentTabIndex == 0) {
        selModel = treeView_->selectionModel();
    }
    
    if (selModel && selModel->hasSelection()) {
        QModelIndex current = selModel->currentIndex();
        if (current.isValid()) {
            QUuid id = current.data(ExecutionModel::IdRole).toUuid();
            showExecution(id);
        }
    }
}

void ToolExecutionDock::onCustomContextMenu(const QPoint& pos) {
    Q_UNUSED(pos);
    
    if (!selectedExecution_.isNull()) {
        contextMenu_->exec(QCursor::pos());
    }
}


void ToolExecutionDock::onChartDataPointClicked(const QString& label, double value) {
    Q_UNUSED(value);
    
    // Filter by clicked data point
    if (chartWidget_->property("groupBy").toString() == "tool") {
        toolFilter_ = QStringList{label};
        
        // Update combo
        int index = toolFilterCombo_->findText(label);
        if (index >= 0) {
            toolFilterCombo_->setCurrentIndex(index);
        }
        
        applyFilters();
    }
}

void ToolExecutionDock::onFilterChanged() {
    // Update filters from combos
    if (toolFilterCombo_->currentIndex() > 0) {
        toolFilter_ = QStringList{toolFilterCombo_->currentText()};
    } else {
        toolFilter_.clear();
    }
    
    statusFilter_.clear();
    switch (statusFilterCombo_->currentIndex()) {
        case 1: statusFilter_.append(ToolExecutionState::Running); break;
        case 2: statusFilter_.append(ToolExecutionState::Completed); break;
        case 3: statusFilter_.append(ToolExecutionState::Failed); break;
        case 4: statusFilter_.append(ToolExecutionState::Cancelled); break;
    }
    
    applyFilters();
}

void ToolExecutionDock::updateRunningExecutions() {
    // Update duration for running executions
    bool hasUpdates = false;
    
    for (ToolExecution& exec : executions_) {
        if (exec.state == ToolExecutionState::Running) {
            exec.duration = exec.startTime.msecsTo(QDateTime::currentDateTime());
            hasUpdates = true;
            
            // Update model
            model_->updateExecution(exec.id, exec);
            
            // Update detail panel if selected
            if (selectedExecution_ == exec.id) {
                detailDurationLabel_->setText(formatDuration(exec.getDuration()));
            }
        }
    }
    
    if (hasUpdates) {
    }
}

void ToolExecutionDock::autoSave() {
    saveSettings();
}

void ToolExecutionDock::loadSettings() {
    QSettings settings;
    settings.beginGroup("ToolExecutionDock");
    
    setViewMode(settings.value("viewMode", "list").toString());
    autoScroll_ = settings.value("autoScroll", true).toBool();
    autoScrollAction_->setChecked(autoScroll_);
    
    // Load favorites
    int favCount = settings.beginReadArray("favorites");
    for (int i = 0; i < favCount; ++i) {
        settings.setArrayIndex(i);
        FavoriteExecution fav;
        fav.name = settings.value("name").toString();
        fav.toolName = settings.value("toolName").toString();
        fav.parameters = QJsonDocument::fromJson(
            settings.value("parameters").toByteArray()
        ).object();
        favorites_.append(fav);
    }
    settings.endArray();
    
    // Load splitter state
    mainSplitter_->restoreState(settings.value("splitterState").toByteArray());
    
    settings.endGroup();
}

void ToolExecutionDock::saveSettings() {
    QSettings settings;
    settings.beginGroup("ToolExecutionDock");
    
    const QStringList viewModes = {"list", "performance"};
    int currentTab = viewTabs_->currentIndex();
    QString viewMode = (currentTab >= 0 && currentTab < viewModes.size()) ? viewModes[currentTab] : "list";
    settings.setValue("viewMode", viewMode);
    settings.setValue("autoScroll", autoScroll_);
    settings.setValue("splitterState", mainSplitter_->saveState());
    
    // Save favorites
    settings.beginWriteArray("favorites");
    for (int i = 0; i < favorites_.size(); ++i) {
        settings.setArrayIndex(i);
        settings.setValue("name", favorites_[i].name);
        settings.setValue("toolName", favorites_[i].toolName);
        settings.setValue("parameters", QJsonDocument(favorites_[i].parameters).toJson());
    }
    settings.endArray();
    
    settings.endGroup();
}

void ToolExecutionDock::applyFilters() {
    // Apply filters to proxy model
    proxyModel_->setToolFilter(toolFilter_);
    proxyModel_->setStatusFilter(statusFilter_);
    proxyModel_->setTimeRange(timeRangeStart_, timeRangeEnd_);
}

// ExecutionModel implementation

ToolExecutionDock::ExecutionModel::ExecutionModel(QObject* parent)
    : QAbstractItemModel(parent)
{
}

QModelIndex ToolExecutionDock::ExecutionModel::index(int row, int column, const QModelIndex& parent) const {
    Q_UNUSED(parent);
    
    if (row >= 0 && row < executions_.size() && column >= 0 && column < ColumnCount) {
        return createIndex(row, column);
    }
    
    return QModelIndex();
}

QModelIndex ToolExecutionDock::ExecutionModel::parent(const QModelIndex& child) const {
    Q_UNUSED(child);
    return QModelIndex(); // Flat list
}

int ToolExecutionDock::ExecutionModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0;
    }
    return executions_.size();
}

int ToolExecutionDock::ExecutionModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return ColumnCount;
}

QVariant ToolExecutionDock::ExecutionModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= executions_.size()) {
        return QVariant();
    }
    
    const ToolExecution& exec = executions_[index.row()];
    
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case ToolColumn:
                return exec.toolName;
            case StatusColumn:
                switch (exec.state) {
                    case ToolExecutionState::Pending: return tr("Pending");
                    case ToolExecutionState::Running: return tr("Running");
                    case ToolExecutionState::Completed: return tr("Success");
                    case ToolExecutionState::Failed: return tr("Failed");
                    case ToolExecutionState::Cancelled: return tr("Cancelled");
                }
                break;
            case DurationColumn:
                if (exec.getDuration() > 0) {
                    if (exec.getDuration() < 1000) {
                        return QString("%1ms").arg(exec.getDuration());
                    } else {
                        return QString("%1s").arg(exec.getDuration() / 1000.0, 0, 'f', 1);
                    }
                }
                return tr("--");
            case StartTimeColumn:
                return exec.startTime.toString("hh:mm:ss");
            case OutputColumn:
                return exec.output.left(100);
        }
    } else if (role == Qt::DecorationRole && index.column() == StatusColumn) {
        switch (exec.state) {
            case ToolExecutionState::Pending: return ThemeManager::instance().themedIcon("clock");
            case ToolExecutionState::Running: return ThemeManager::instance().themedIcon("media-playback-start");
            case ToolExecutionState::Completed: return ThemeManager::instance().themedIcon("dialog-ok");
            case ToolExecutionState::Failed: return ThemeManager::instance().themedIcon("dialog-error");
            case ToolExecutionState::Cancelled: return ThemeManager::instance().themedIcon("dialog-cancel");
        }
    } else if (role == Qt::ForegroundRole) {
        if (exec.state == ToolExecutionState::Failed) {
            return ThemeColor("statusFailed");
        } else if (exec.state == ToolExecutionState::Completed) {
            return ThemeColor("statusCompleted");
        }
    } else if (role == ExecutionRole) {
        return QVariant::fromValue(exec);
    } else if (role == IdRole) {
        return exec.id;
    } else if (role == StatusRole) {
        return static_cast<int>(exec.state);
    }
    
    return QVariant();
}

QVariant ToolExecutionDock::ExecutionModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    
    switch (section) {
        case ToolColumn: return tr("Tool");
        case StatusColumn: return tr("Status");
        case DurationColumn: return tr("Duration");
        case StartTimeColumn: return tr("Start Time");
        case OutputColumn: return tr("Output");
    }
    
    return QVariant();
}

void ToolExecutionDock::ExecutionModel::setExecutions(const QList<ToolExecution>& executions) {
    beginResetModel();
    executions_ = executions;
    
    indexMap_.clear();
    for (int i = 0; i < executions_.size(); ++i) {
        indexMap_[executions_[i].id] = i;
    }
    
    endResetModel();
}

void ToolExecutionDock::ExecutionModel::addExecution(const ToolExecution& execution) {
    int row = executions_.size();
    beginInsertRows(QModelIndex(), row, row);
    
    executions_.append(execution);
    indexMap_[execution.id] = row;
    
    endInsertRows();
}

void ToolExecutionDock::ExecutionModel::updateExecution(const QUuid& id, const ToolExecution& execution) {
    if (indexMap_.contains(id)) {
        int row = indexMap_[id];
        executions_[row] = execution;
        
        emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    }
}

void ToolExecutionDock::ExecutionModel::removeExecution(const QUuid& id) {
    if (indexMap_.contains(id)) {
        int row = indexMap_[id];
        beginRemoveRows(QModelIndex(), row, row);
        
        executions_.removeAt(row);
        
        // Update index map
        indexMap_.clear();
        for (int i = 0; i < executions_.size(); ++i) {
            indexMap_[executions_[i].id] = i;
        }
        
        endRemoveRows();
    }
}

void ToolExecutionDock::ExecutionModel::clear() {
    beginResetModel();
    executions_.clear();
    indexMap_.clear();
    endResetModel();
}

ToolExecution ToolExecutionDock::ExecutionModel::execution(const QUuid& id) const {
    if (indexMap_.contains(id)) {
        return executions_[indexMap_[id]];
    }
    return ToolExecution();
}

QModelIndex ToolExecutionDock::ExecutionModel::indexForId(const QUuid& id) const {
    if (indexMap_.contains(id)) {
        int row = indexMap_[id];
        return index(row, 0);
    }
    return QModelIndex();
}

// Helper class for dynamic parameter input
class ParameterInputWidget : public QWidget {
public:
    ParameterInputWidget(QWidget* parent = nullptr) : QWidget(parent) {
        mainLayout_ = new QVBoxLayout(this);
        mainLayout_->setSpacing(10);
    }
    
    void setSchema(const QJsonObject& schema) {
        // Clear existing widgets
        QLayoutItem* item;
        while ((item = mainLayout_->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        parameterWidgets_.clear();
        
        // Parse schema
        QJsonObject properties = schema["properties"].toObject();
        QJsonArray required = schema["required"].toArray();
        
        QSet<QString> requiredSet;
        for (const QJsonValue& val : required) {
            requiredSet.insert(val.toString());
        }
        
        // Create sections
        auto* requiredSection = new QGroupBox(tr("Required Parameters"));
        auto* requiredLayout = new QFormLayout(requiredSection);
        
        auto* optionalSection = new QGroupBox(tr("Optional Parameters"));
        auto* optionalLayout = new QFormLayout(optionalSection);
        optionalSection->setCheckable(true);
        optionalSection->setChecked(false);
        
        // Create widgets for each parameter
        for (auto it = properties.begin(); it != properties.end(); ++it) {
            QString name = it.key();
            QJsonObject param = it.value().toObject();
            QString type = param["type"].toString();
            QString description = param["description"].toString();
            
            QWidget* widget = nullptr;
            
            if (type == "integer") {
                if (name == "address" || name.contains("address", Qt::CaseInsensitive)) {
                    // Special handling for addresses - use line edit for hex input
                    auto* lineEdit = new QLineEdit();
                    lineEdit->setPlaceholderText("0x...");
                    widget = lineEdit;
                } else {
                    auto* spinBox = new QSpinBox();
                    spinBox->setRange(INT_MIN, INT_MAX);
                    spinBox->setSpecialValueText("(not set)");
                    spinBox->setValue(spinBox->minimum());
                    widget = spinBox;
                }
                
            } else if (type == "string") {
                auto* lineEdit = new QLineEdit();
                if (!description.isEmpty()) {
                    lineEdit->setPlaceholderText(description.left(50));
                }
                
                // Set default empty string for optional parameters
                if (!requiredSet.contains(name)) {
                    lineEdit->setText("");
                }
                
                widget = lineEdit;
                
            } else if (type == "boolean") {
                auto* checkBox = new QCheckBox();
                checkBox->setChecked(false);  // Default to unchecked
                widget = checkBox;
                
            } else if (type == "array") {
                auto* textEdit = new QTextEdit();
                textEdit->setMaximumHeight(60);
                textEdit->setPlaceholderText("JSON array, e.g., [1, 2, 3]");
                widget = textEdit;
            }
            
            if (widget) {
                widget->setToolTip(description);
                parameterWidgets_[name] = widget;
                
                // Add to appropriate section
                if (requiredSet.contains(name)) {
                    requiredLayout->addRow(name + ":", widget);
                } else {
                    optionalLayout->addRow(name + ":", widget);
                }
            }
        }
        
        // Add sections to main layout
        if (requiredLayout->rowCount() > 0) {
            mainLayout_->addWidget(requiredSection);
        }
        
        if (optionalLayout->rowCount() > 0) {
            mainLayout_->addWidget(optionalSection);
        }
        
        // Add stretch at bottom
        mainLayout_->addStretch();
    }
    
    QJsonObject getParameters() const {
        QJsonObject params;
        
        for (auto it = parameterWidgets_.begin(); it != parameterWidgets_.end(); ++it) {
            QString name = it.key();
            QWidget* widget = it.value();
            
            if (auto* spinBox = qobject_cast<QSpinBox*>(widget)) {
                if (spinBox->value() != spinBox->minimum()) {
                    params[name] = spinBox->value();
                }
            } else if (auto* lineEdit = qobject_cast<QLineEdit*>(widget)) {
                QString text = lineEdit->text().trimmed();
                if (!text.isEmpty()) {
                    // Special handling for address fields - convert hex to integer
                    if (name == "address" || name.contains("address", Qt::CaseInsensitive)) {
                        bool ok;
                        qlonglong value = text.toLongLong(&ok, 0); // 0 means auto-detect base (0x for hex)
                        if (ok) {
                            params[name] = static_cast<qint64>(value);
                        } else if (!text.isEmpty()) {
                            params[name] = text;
                        }
                    } else {
                        params[name] = text;
                    }
                }
            } else if (auto* checkBox = qobject_cast<QCheckBox*>(widget)) {
                // Only include if different from default or is required
                params[name] = checkBox->isChecked();
            } else if (auto* textEdit = qobject_cast<QTextEdit*>(widget)) {
                QString text = textEdit->toPlainText().trimmed();
                if (!text.isEmpty()) {
                    // Parse as JSON array
                    QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
                    if (doc.isArray()) {
                        params[name] = doc.array();
                    }
                }
            }
        }
        
        return params;
    }
    
private:
    QVBoxLayout* mainLayout_;
    QMap<QString, QWidget*> parameterWidgets_;
};

void ToolExecutionDock::onManualExecute() {
    if (!agentController_) {
        QMessageBox::warning(this, tr("Warning"), tr("Agent controller not set. Cannot execute tools manually."));
        return;
    }
    
    // Get available tools
    QJsonArray availableTools = agentController_->getAvailableTools();
    if (availableTools.isEmpty()) {
        QMessageBox::information(this, tr("No Tools"), tr("No tools are available for manual execution."));
        return;
    }
    
    // Create dialog for manual execution
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Manual Tool Execution"));
    dialog.setMinimumSize(700, 500);
    
    auto* layout = new QVBoxLayout(&dialog);
    
    // Tool selection
    auto* toolLayout = new QHBoxLayout();
    toolLayout->addWidget(new QLabel(tr("Tool:"), &dialog));
    
    auto* toolCombo = new QComboBox(&dialog);
    QMap<QString, QJsonObject> toolSchemas;
    
    for (const QJsonValue& toolValue : availableTools) {
        QJsonObject tool = toolValue.toObject();
        QString name = tool["name"].toString();
        QString description = tool["description"].toString();
        toolCombo->addItem(name, name);
        toolCombo->setItemData(toolCombo->count() - 1, description, Qt::ToolTipRole);
        toolSchemas[name] = tool["input_schema"].toObject();
    }
    
    toolLayout->addWidget(toolCombo);
    layout->addLayout(toolLayout);
    
    // Add description label
    auto* descriptionLabel = new QLabel(&dialog);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setStyleSheet("QLabel { color: gray; margin: 5px 0; }");
    layout->addWidget(descriptionLabel);
    
    // Tab widget for form vs JSON view
    auto* tabWidget = new QTabWidget(&dialog);
    
    // Form tab
    auto* formTab = new QWidget();
    auto* formLayout = new QVBoxLayout(formTab);
    
    auto* paramWidget = new ParameterInputWidget(formTab);
    auto* scrollArea = new QScrollArea(formTab);
    scrollArea->setWidget(paramWidget);
    scrollArea->setWidgetResizable(true);
    formLayout->addWidget(scrollArea);
    
    tabWidget->addTab(formTab, tr("Form"));
    
    // JSON tab (for advanced users)
    auto* jsonTab = new QWidget();
    auto* jsonLayout = new QVBoxLayout(jsonTab);
    
    auto* parametersEdit = new QTextEdit(jsonTab);
    parametersEdit->setPlainText("{}");
    parametersEdit->setFont(QFont("Consolas", 10));
    jsonLayout->addWidget(parametersEdit);
    
    tabWidget->addTab(jsonTab, tr("JSON (Advanced)"));
    
    layout->addWidget(tabWidget);
    
    // Update form when tool changes
    auto updateTool = [&]() {
        QString toolName = toolCombo->currentData().toString();
        QString description = toolCombo->currentData(Qt::ToolTipRole).toString();
        descriptionLabel->setText(description);
        
        if (toolSchemas.contains(toolName)) {
            QJsonObject schema = toolSchemas[toolName];
            paramWidget->setSchema(schema);
            
            // Update JSON view with current form values
            QJsonObject params = paramWidget->getParameters();
            parametersEdit->setPlainText(QJsonDocument(params).toJson(QJsonDocument::Indented));
        }
    };
    
    connect(toolCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), updateTool);
    updateTool();
    
    // Sync form and JSON views
    connect(tabWidget, &QTabWidget::currentChanged, [&](int index) {
        if (index == 1) { // Switching to JSON tab
            QJsonObject params = paramWidget->getParameters();
            parametersEdit->setPlainText(QJsonDocument(params).toJson(QJsonDocument::Indented));
        }
    });
    
    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    auto* executeButton = new QPushButton(tr("Execute"), &dialog);
    executeButton->setIcon(ThemeManager::instance().themedIcon("play"));
    
    auto* cancelButton = new QPushButton(tr("Cancel"), &dialog);
    
    buttonLayout->addWidget(executeButton);
    buttonLayout->addWidget(cancelButton);
    layout->addLayout(buttonLayout);
    
    // Connect buttons
    connect(executeButton, &QPushButton::clicked, [&]() {
        QString toolName = toolCombo->currentData().toString();
        QJsonObject parameters;
        
        // Get parameters based on active tab
        if (tabWidget->currentIndex() == 0) { // Form tab
            parameters = paramWidget->getParameters();
        } else { // JSON tab
            QString parametersText = parametersEdit->toPlainText();
            QJsonDocument doc = QJsonDocument::fromJson(parametersText.toUtf8());
            if (doc.isNull() || !doc.isObject()) {
                QMessageBox::warning(&dialog, tr("Invalid JSON"), tr("The parameters must be valid JSON object."));
                return;
            }
            parameters = doc.object();
        }
        
        // Create execution entry
        ToolExecution exec;
        exec.id = QUuid::createUuid();
        exec.toolName = toolName;
        exec.parameters = parameters;
        exec.state = ToolExecutionState::Running;
        exec.source = ToolExecutionSource::Manual;  // Mark as manual execution
        exec.startTime = QDateTime::currentDateTime();
        
        // Add to model
        addExecution(exec);
        
        // Execute the tool
        QJsonObject result = agentController_->executeManualTool(toolName, parameters);
        
        // Update execution with result
        exec.endTime = QDateTime::currentDateTime();
        exec.duration = exec.startTime.msecsTo(exec.endTime);
        
        if (result["success"].toBool()) {
            exec.state = ToolExecutionState::Completed;
            exec.output = QJsonDocument(result).toJson(QJsonDocument::Compact);
        } else {
            exec.state = ToolExecutionState::Failed;
            exec.errorMessage = result["error"].toString();
            exec.output = QJsonDocument(result).toJson(QJsonDocument::Compact);
        }
        
        // Update model
        updateExecution(exec.id, exec);
        
        // Close dialog - user can see results in the execution window
        dialog.accept();
    });
    
    connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    
    dialog.exec();
}

void ToolExecutionDock::addExecution(const ToolExecution& execution) {
    executions_.append(execution);
    executionMap_[execution.id] = &executions_.last();
    
    if (model_) {
        model_->addExecution(execution);
    }
}

void ToolExecutionDock::updateExecution(const QUuid& id, const ToolExecution& execution) {
    if (executionMap_.contains(id)) {
        *executionMap_[id] = execution;
        
        if (model_) {
            model_->updateExecution(id, execution);
        }
    }
}

} // namespace llm_re::ui_v2