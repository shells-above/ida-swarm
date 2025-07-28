#include "tool_execution_dock.h"
#include "../core/theme_manager.h"
#include "../core/ui_constants.h"
#include "../core/ui_utils.h"
#include <QToolBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeView>
#include <QTableView>
#include <QTextEdit>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QMenu>
#include <QAction>
#include <QTimer>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonArray>
#include <QSettings>
#include <QClipboard>
#include <QtMath>
#include <algorithm>

namespace llm_re::ui_v2 {

// ExecutionTimelineWidget implementation

ExecutionTimelineWidget::ExecutionTimelineWidget(QWidget* parent)
    : BaseStyledWidget(parent)
{
    setMouseTracking(true);
    setMinimumHeight(200);
}

void ExecutionTimelineWidget::setExecutions(const QList<ToolExecution>& executions) {
    items_.clear();
    
    // Find time range
    if (!executions.isEmpty()) {
        startTime_ = executions.first().startTime;
        endTime_ = executions.first().endTime;
        
        for (const ToolExecution& exec : executions) {
            if (exec.startTime < startTime_) {
                startTime_ = exec.startTime;
            }
            if (exec.endTime > endTime_) {
                endTime_ = exec.endTime;
            }
            
            TimelineItem item;
            item.execution = exec;
            item.row = 0; // Will be calculated in layout
            items_.append(item);
        }
        
        // Add some padding
        qint64 range = startTime_.msecsTo(endTime_);
        startTime_ = startTime_.addMSecs(-range * 0.05);
        endTime_ = endTime_.addMSecs(range * 0.05);
    }
    
    calculateLayout();
    update();
}

void ExecutionTimelineWidget::highlightExecution(const QUuid& id) {
    highlightedId_ = id;
    update();
}

void ExecutionTimelineWidget::setTimeRange(const QDateTime& start, const QDateTime& end) {
    startTime_ = start;
    endTime_ = end;
    calculateLayout();
    update();
    emit timeRangeChanged(start, end);
}

void ExecutionTimelineWidget::setZoomLevel(int level) {
    zoomLevel_ = qBound(10, level, 500);
    calculateLayout();
    update();
}

void ExecutionTimelineWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Background
    painter.fillRect(rect(), ThemeManager::instance()->color(ThemeManager::Surface));
    
    // Calculate visible area
    QRectF visibleRect = rect().translated(-viewOffset_);
    
    // Draw time axis
    painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurfaceVariant));
    painter.drawLine(margin_, headerHeight_, width() - margin_, headerHeight_);
    
    // Draw time labels
    qint64 totalMs = startTime_.msecsTo(endTime_);
    int labelCount = width() / 100; // Approximate label spacing
    
    for (int i = 0; i <= labelCount; ++i) {
        qreal x = margin_ + (width() - 2 * margin_) * i / labelCount;
        qint64 ms = totalMs * i / labelCount;
        QDateTime time = startTime_.addMSecs(ms);
        
        painter.drawLine(x, headerHeight_ - 5, x, headerHeight_ + 5);
        
        QRectF labelRect(x - 50, 5, 100, 20);
        painter.drawText(labelRect, Qt::AlignCenter, time.toString("hh:mm:ss"));
    }
    
    // Draw executions
    for (const TimelineItem& item : items_) {
        if (!visibleRect.intersects(item.rect)) {
            continue; // Skip items outside visible area
        }
        
        // Execution bar
        QColor color = statusColor(item.execution.status);
        if (item.execution.id == highlightedId_) {
            color = color.lighter(120);
        }
        if (item.execution.id == hoveredId_) {
            color = color.lighter(110);
        }
        
        painter.fillRect(item.rect, color);
        
        // Progress for running executions
        if (item.execution.status == ToolExecution::Running) {
            QRectF progressRect = item.rect;
            progressRect.setWidth(item.rect.width() * item.execution.progress / 100.0);
            painter.fillRect(progressRect, color.darker(120));
        }
        
        // Border
        painter.setPen(color.darker(150));
        painter.drawRect(item.rect);
        
        // Label
        if (item.rect.width() > 50) {
            painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
            painter.setFont(QFont("Sans", 9));
            
            QString label = item.execution.toolName;
            if (item.rect.width() > 100) {
                label += QString(" (%1)").arg(formatDuration(item.execution.duration));
            }
            
            QRectF textRect = item.rect.adjusted(5, 0, -5, 0);
            painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
        }
        
        // Sub-tasks
        if (showSubTasks_ && !item.execution.subTasks.isEmpty()) {
            qreal subTaskY = item.rect.bottom() + 2;
            for (const ToolExecution::SubTask& subTask : item.execution.subTasks) {
                QRectF subRect(item.rect.x(), subTaskY, 
                             item.rect.width() * subTask.progress / 100.0, 5);
                painter.fillRect(subRect, color.darker(120));
                subTaskY += 7;
            }
        }
    }
    
    // Draw dependencies
    if (showDependencies_) {
        painter.setPen(QPen(ThemeManager::instance()->color(ThemeManager::Primary), 2));
        for (const TimelineItem& item : items_) {
            for (const QUuid& depId : item.execution.dependencies) {
                // Find dependency item
                for (const TimelineItem& depItem : items_) {
                    if (depItem.execution.id == depId) {
                        // Draw arrow from dependency to this item
                        QPointF start(depItem.rect.right(), depItem.rect.center().y());
                        QPointF end(item.rect.left(), item.rect.center().y());
                        
                        painter.drawLine(start, end);
                        
                        // Arrow head
                        qreal angle = std::atan2(end.y() - start.y(), end.x() - start.x());
                        QPointF arrowP1 = end - QPointF(8 * cos(angle - M_PI/6), 
                                                       8 * sin(angle - M_PI/6));
                        QPointF arrowP2 = end - QPointF(8 * cos(angle + M_PI/6), 
                                                       8 * sin(angle + M_PI/6));
                        
                        QPolygonF arrow;
                        arrow << end << arrowP1 << arrowP2;
                        painter.setBrush(painter.pen().color());
                        painter.drawPolygon(arrow);
                        
                        break;
                    }
                }
            }
        }
    }
    
    // Tooltip
    if (!hoveredId_.isNull()) {
        for (const TimelineItem& item : items_) {
            if (item.execution.id == hoveredId_) {
                QString tooltip = QString("%1\nStatus: %2\nDuration: %3")
                    .arg(item.execution.toolName)
                    .arg([&]() {
                        switch (item.execution.status) {
                        case ToolExecution::Pending: return "Pending";
                        case ToolExecution::Running: return "Running";
                        case ToolExecution::Success: return "Success";
                        case ToolExecution::Failed: return "Failed";
                        case ToolExecution::Cancelled: return "Cancelled";
                        default: return "Unknown";
                        }
                    }())
                    .arg(formatDuration(item.execution.duration));
                
                if (item.execution.status == ToolExecution::Running) {
                    tooltip += QString("\nProgress: %1%").arg(item.execution.progress);
                }
                
                QFontMetrics fm(painter.font());
                QRect tooltipRect = fm.boundingRect(tooltip);
                tooltipRect.adjust(-5, -5, 5, 5);
                tooltipRect.moveTopLeft(QCursor::pos() - mapToGlobal(QPoint(0, 0)) + QPoint(10, 10));
                
                painter.fillRect(tooltipRect, ThemeManager::instance()->color(ThemeManager::SurfaceVariant));
                painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
                painter.drawText(tooltipRect, Qt::AlignCenter, tooltip);
                
                break;
            }
        }
    }
}

void ExecutionTimelineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        // Check for execution click
        for (const TimelineItem& item : items_) {
            if (item.rect.contains(event->pos())) {
                emit executionClicked(item.execution.id);
                break;
            }
        }
    } else if (event->button() == Qt::MiddleButton) {
        isPanning_ = true;
        panStartPos_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void ExecutionTimelineWidget::mouseMoveEvent(QMouseEvent* event) {
    if (isPanning_) {
        QPointF delta = event->pos() - panStartPos_;
        panStartPos_ = event->pos();
        viewOffset_ -= delta;
        update();
    } else {
        // Update hover
        QUuid oldHovered = hoveredId_;
        hoveredId_ = QUuid();
        
        for (const TimelineItem& item : items_) {
            if (item.rect.contains(event->pos())) {
                hoveredId_ = item.execution.id;
                break;
            }
        }
        
        if (hoveredId_ != oldHovered) {
            update();
        }
    }
}

void ExecutionTimelineWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        isPanning_ = false;
        setCursor(Qt::ArrowCursor);
    }
}

void ExecutionTimelineWidget::wheelEvent(QWheelEvent* event) {
    // Zoom with mouse wheel
    int delta = event->angleDelta().y();
    if (delta > 0) {
        setZoomLevel(zoomLevel_ + 10);
    } else {
        setZoomLevel(zoomLevel_ - 10);
    }
}

void ExecutionTimelineWidget::resizeEvent(QResizeEvent* event) {
    BaseStyledWidget::resizeEvent(event);
    calculateLayout();
}

void ExecutionTimelineWidget::calculateLayout() {
    if (items_.isEmpty() || !startTime_.isValid() || !endTime_.isValid()) {
        return;
    }
    
    qint64 totalMs = startTime_.msecsTo(endTime_);
    if (totalMs <= 0) {
        return;
    }
    
    qreal scale = (width() - 2 * margin_) * zoomLevel_ / 100.0 / totalMs;
    
    // Sort by start time if grouping is disabled
    if (!groupByTool_) {
        std::sort(items_.begin(), items_.end(), 
                 [](const TimelineItem& a, const TimelineItem& b) {
            return a.execution.startTime < b.execution.startTime;
        });
    }
    
    // Assign rows to avoid overlaps
    QHash<QString, int> toolRows;
    int maxRow = 0;
    
    for (TimelineItem& item : items_) {
        qint64 startMs = startTime_.msecsTo(item.execution.startTime);
        qint64 endMs = startTime_.msecsTo(item.execution.endTime);
        
        qreal x = margin_ + startMs * scale;
        qreal width = (endMs - startMs) * scale;
        
        // Find available row
        if (groupByTool_) {
            // Group by tool name
            if (!toolRows.contains(item.execution.toolName)) {
                toolRows[item.execution.toolName] = maxRow++;
            }
            item.row = toolRows[item.execution.toolName];
        } else {
            // Find first available row
            item.row = 0;
            bool found = false;
            while (!found) {
                found = true;
                for (const TimelineItem& other : items_) {
                    if (&other == &item) break;
                    if (other.row == item.row && 
                        other.rect.left() < x + width && 
                        other.rect.right() > x) {
                        found = false;
                        item.row++;
                        break;
                    }
                }
            }
            maxRow = std::max(maxRow, item.row);
        }
        
        qreal y = headerHeight_ + 10 + item.row * (rowHeight_ + 5);
        item.rect = QRectF(x, y, width, rowHeight_);
    }
    
    // Update minimum height
    int requiredHeight = headerHeight_ + 20 + (maxRow + 1) * (rowHeight_ + 5);
    setMinimumHeight(requiredHeight);
}

QColor ExecutionTimelineWidget::statusColor(ToolExecution::Status status) const {
    switch (status) {
    case ToolExecution::Pending:
        return QColor("#9E9E9E");
    case ToolExecution::Running:
        return QColor("#2196F3");
    case ToolExecution::Success:
        return QColor("#4CAF50");
    case ToolExecution::Failed:
        return QColor("#F44336");
    case ToolExecution::Cancelled:
        return QColor("#FF9800");
    default:
        return QColor("#757575");
    }
}

QString ExecutionTimelineWidget::formatDuration(qint64 ms) const {
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

void PerformanceChartWidget::exportChart(const QString& format) {
    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Export Chart"),
        QString("performance_chart.%1").arg(format),
        tr("%1 Files (*.%2)").arg(format.toUpper()).arg(format)
    );
    
    if (!fileName.isEmpty()) {
        QImage image(size(), QImage::Format_ARGB32);
        image.fill(Qt::white);
        
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        
        // Render chart
        paintEvent(nullptr);
        render(&painter);
        
        image.save(fileName);
    }
}

void PerformanceChartWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Background
    painter.fillRect(rect(), ThemeManager::instance()->color(ThemeManager::Surface));
    
    // Calculate layout
    chartRect_ = rect().adjusted(60, 20, -100, -60);
    legendRect_ = QRectF(chartRect_.right() + 20, chartRect_.top(), 80, chartRect_.height());
    
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
    
    // Draw legend
    drawLegend(&painter);
    
    // Tooltip
    if (hoveredPoint_ >= 0 && hoveredPoint_ < dataPoints_.size()) {
        const DataPoint& point = dataPoints_[hoveredPoint_];
        
        QString tooltip = QString("%1: %2").arg(point.label).arg(point.value);
        
        QFontMetrics fm(painter.font());
        QRect tooltipRect = fm.boundingRect(tooltip);
        tooltipRect.adjust(-5, -5, 5, 5);
        tooltipRect.moveTopLeft(QCursor::pos() - mapToGlobal(QPoint(0, 0)) + QPoint(10, 10));
        
        painter.fillRect(tooltipRect, ThemeManager::instance()->color(ThemeManager::SurfaceVariant));
        painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
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
            switch (exec.status) {
            case ToolExecution::Success: group = "Success"; break;
            case ToolExecution::Failed: group = "Failed"; break;
            case ToolExecution::Cancelled: group = "Cancelled"; break;
            default: group = "Other"; break;
            }
        } else if (groupBy_ == "hour") {
            group = exec.startTime.toString("yyyy-MM-dd HH:00");
        }
        
        double value = 0;
        switch (metric_) {
        case ExecutionTime:
            value = exec.duration;
            break;
        case SuccessRate:
            value = (exec.status == ToolExecution::Success) ? 100 : 0;
            break;
        case ThroughputRate:
            value = 1; // Count
            break;
        case ErrorRate:
            value = (exec.status == ToolExecution::Failed) ? 100 : 0;
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
    QList<QColor> colors = {
        "#2196F3", "#4CAF50", "#FF9800", "#F44336", "#9C27B0",
        "#00BCD4", "#8BC34A", "#FFC107", "#E91E63", "#3F51B5"
    };
    
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
    painter->setPen(QPen(ThemeManager::instance()->color(ThemeManager::Primary), 2));
    painter->drawPath(path);
    
    // Draw points
    painter->setBrush(ThemeManager::instance()->color(ThemeManager::Primary));
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
            painter->setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
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
    painter->setPen(ThemeManager::instance()->color(ThemeManager::OnSurfaceVariant));
    
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
            label = formatDuration(value);
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

void PerformanceChartWidget::drawLegend(QPainter* painter) {
    if (chartType_ == PieChart || dataPoints_.size() > 10) {
        // Draw legend for pie chart or when many data points
        qreal y = legendRect_.top();
        
        for (int i = 0; i < std::min(10, dataPoints_.size()); ++i) {
            // Color box
            QRectF colorRect(legendRect_.left(), y, 12, 12);
            painter->fillRect(colorRect, dataPoints_[i].color);
            painter->setPen(dataPoints_[i].color.darker(120));
            painter->drawRect(colorRect);
            
            // Label
            painter->setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
            QRectF labelRect(colorRect.right() + 5, y, legendRect_.width() - 17, 12);
            painter->drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, dataPoints_[i].label);
            
            y += 20;
        }
        
        if (dataPoints_.size() > 10) {
            painter->drawText(legendRect_.left(), y, "...");
        }
    }
}

QString PerformanceChartWidget::formatDuration(double ms) const {
    if (ms < 1000) {
        return QString("%1ms").arg(static_cast<int>(ms));
    } else if (ms < 60000) {
        return QString("%1s").arg(ms / 1000.0, 0, 'f', 1);
    } else {
        return QString("%1m").arg(static_cast<int>(ms / 60000));
    }
}

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
    
    // View mode
    auto* viewLabel = new QLabel(tr("View:"), this);
    toolBar_->addWidget(viewLabel);
    
    viewModeCombo_ = new QComboBox(this);
    viewModeCombo_->addItems({tr("List"), tr("Table"), tr("Timeline"), tr("Performance")});
    toolBar_->addWidget(viewModeCombo_);
    
    toolBar_->addSeparator();
    
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
    autoScrollAction_ = toolBar_->addAction(UIUtils::icon("auto-scroll"), tr("Auto Scroll"));
    autoScrollAction_->setCheckable(true);
    autoScrollAction_->setChecked(autoScroll_);
    
    clearHistoryAction_ = toolBar_->addAction(UIUtils::icon("edit-clear"), tr("Clear History"));
    
    exportAction_ = toolBar_->addAction(UIUtils::icon("document-export"), tr("Export"));
}

void ToolExecutionDock::createViews() {
    viewTabs_ = new QTabWidget(this);
    viewTabs_->setTabPosition(QTabWidget::South);
    
    // Create model
    model_ = new ExecutionModel(this);
    proxyModel_ = new QSortFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);
    
    // List view (tree)
    treeView_ = new QTreeView(this);
    treeView_->setModel(proxyModel_);
    treeView_->setAlternatingRowColors(true);
    treeView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    treeView_->setContextMenuPolicy(Qt::CustomContextMenu);
    treeView_->header()->setStretchLastSection(true);
    viewTabs_->addTab(treeView_, tr("List"));
    
    // Table view
    tableView_ = new QTableView(this);
    tableView_->setModel(proxyModel_);
    tableView_->setAlternatingRowColors(true);
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    tableView_->setContextMenuPolicy(Qt::CustomContextMenu);
    tableView_->setSortingEnabled(true);
    tableView_->horizontalHeader()->setStretchLastSection(true);
    viewTabs_->addTab(tableView_, tr("Table"));
    
    // Timeline view
    timelineWidget_ = new ExecutionTimelineWidget(this);
    viewTabs_->addTab(timelineWidget_, tr("Timeline"));
    
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
    
    // Progress
    detailProgressBar_ = new QProgressBar(this);
    detailProgressBar_->setTextVisible(true);
    layout->addWidget(detailProgressBar_);
    
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
    
    // Actions
    auto* actionsLayout = new QHBoxLayout();
    
    retryButton_ = new QPushButton(UIUtils::icon("view-refresh"), tr("Retry"), this);
    retryButton_->setEnabled(false);
    actionsLayout->addWidget(retryButton_);
    
    cancelButton_ = new QPushButton(UIUtils::icon("process-stop"), tr("Cancel"), this);
    cancelButton_->setEnabled(false);
    actionsLayout->addWidget(cancelButton_);
    
    actionsLayout->addStretch();
    
    layout->addLayout(actionsLayout);
    layout->addStretch();
}

void ToolExecutionDock::createContextMenu() {
    contextMenu_ = new QMenu(this);
    
    contextMenu_->addAction(UIUtils::icon("view-refresh"), tr("Retry"),
                           [this]() {
        if (!selectedExecution_.isNull()) {
            retryExecution(selectedExecution_);
        }
    });
    
    contextMenu_->addAction(UIUtils::icon("process-stop"), tr("Cancel"),
                           [this]() {
        if (!selectedExecution_.isNull()) {
            cancelExecution(selectedExecution_);
        }
    });
    
    contextMenu_->addSeparator();
    
    contextMenu_->addAction(UIUtils::icon("edit-copy"), tr("Copy Tool Name"),
                           [this]() {
        if (!selectedExecution_.isNull()) {
            QApplication::clipboard()->setText(execution(selectedExecution_).toolName);
        }
    });
    
    contextMenu_->addAction(UIUtils::icon("edit-copy"), tr("Copy Parameters"),
                           [this]() {
        if (!selectedExecution_.isNull()) {
            QJsonDocument doc(execution(selectedExecution_).parameters);
            QApplication::clipboard()->setText(doc.toJson(QJsonDocument::Indented));
        }
    });
    
    contextMenu_->addAction(UIUtils::icon("edit-copy"), tr("Copy Output"),
                           [this]() {
        if (!selectedExecution_.isNull()) {
            QApplication::clipboard()->setText(execution(selectedExecution_).output);
        }
    });
    
    contextMenu_->addSeparator();
    
    contextMenu_->addAction(UIUtils::icon("bookmark"), tr("Add to Favorites"),
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
    // View mode
    connect(viewModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolExecutionDock::onViewModeChanged);
    
    // Filters
    connect(toolFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolExecutionDock::onFilterChanged);
    
    connect(statusFilterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolExecutionDock::onFilterChanged);
    
    // Actions
    connect(autoScrollAction_, &QAction::toggled,
            [this](bool checked) { autoScroll_ = checked; });
    
    connect(clearHistoryAction_, &QAction::triggered,
            this, &ToolExecutionDock::clearHistory);
    
    connect(exportAction_, &QAction::triggered,
            this, &ToolExecutionDock::onExportClicked);
    
    // Tree/Table views
    connect(treeView_, &QTreeView::clicked,
            this, &ToolExecutionDock::onExecutionClicked);
    
    connect(treeView_, &QTreeView::doubleClicked,
            this, &ToolExecutionDock::onExecutionDoubleClicked);
    
    connect(tableView_, &QTableView::clicked,
            this, &ToolExecutionDock::onExecutionClicked);
    
    connect(tableView_, &QTableView::doubleClicked,
            this, &ToolExecutionDock::onExecutionDoubleClicked);
    
    connect(treeView_, &QWidget::customContextMenuRequested,
            this, &ToolExecutionDock::onCustomContextMenu);
    
    connect(tableView_, &QWidget::customContextMenuRequested,
            this, &ToolExecutionDock::onCustomContextMenu);
    
    connect(treeView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ToolExecutionDock::onSelectionChanged);
    
    connect(tableView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &ToolExecutionDock::onSelectionChanged);
    
    // Timeline view
    connect(timelineWidget_, &ExecutionTimelineWidget::executionClicked,
            this, &ToolExecutionDock::onTimelineExecutionClicked);
    
    connect(timelineWidget_, &ExecutionTimelineWidget::executionDoubleClicked,
            this, &ToolExecutionDock::showExecution);
    
    // Chart view
    connect(chartWidget_, &PerformanceChartWidget::dataPointClicked,
            this, &ToolExecutionDock::onChartDataPointClicked);
    
    // Detail panel buttons
    connect(retryButton_, &QPushButton::clicked,
            [this]() {
        if (!selectedExecution_.isNull()) {
            retryExecution(selectedExecution_);
        }
    });
    
    connect(cancelButton_, &QPushButton::clicked,
            [this]() {
        if (!selectedExecution_.isNull()) {
            cancelExecution(selectedExecution_);
        }
    });
}

QUuid ToolExecutionDock::startExecution(const QString& toolName, const QJsonObject& parameters) {
    ToolExecution exec;
    exec.id = QUuid::createUuid();
    exec.toolName = toolName;
    exec.parameters = parameters;
    exec.startTime = QDateTime::currentDateTime();
    exec.status = ToolExecution::Running;
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

void ToolExecutionDock::updateProgress(const QUuid& id, int progress, const QString& message) {
    if (executionMap_.contains(id)) {
        ToolExecution& exec = *executionMap_[id];
        exec.progress = qBound(0, progress, 100);
        if (!message.isEmpty()) {
            exec.progressMessage = message;
        }
        
        // Update model
        model_->updateExecution(id, exec);
        
        // Update detail panel if selected
        if (selectedExecution_ == id) {
            detailProgressBar_->setValue(progress);
            if (!message.isEmpty()) {
                detailProgressBar_->setFormat(message + " %p%");
            }
        }
        
        emit executionProgress(id, progress);
    }
}

void ToolExecutionDock::completeExecution(const QUuid& id, bool success, const QString& output) {
    if (executionMap_.contains(id)) {
        ToolExecution& exec = *executionMap_[id];
        exec.endTime = QDateTime::currentDateTime();
        exec.duration = exec.startTime.msecsTo(exec.endTime);
        exec.status = success ? ToolExecution::Success : ToolExecution::Failed;
        exec.output = output;
        exec.progress = 100;
        
        if (!success && !output.isEmpty()) {
            exec.errorMessage = output;
        }
        
        // Update model
        model_->updateExecution(id, exec);
        
        // Update tool stats
        if (tools_.contains(exec.toolName)) {
            tools_[exec.toolName].totalDuration += exec.duration;
            if (success) {
                tools_[exec.toolName].successCount++;
            } else {
                tools_[exec.toolName].failureCount++;
            }
        }
        
        // Update detail panel if selected
        if (selectedExecution_ == id) {
            detailStatusLabel_->setText(success ? tr("Success") : tr("Failed"));
            detailDurationLabel_->setText(timelineWidget_->formatDuration(exec.duration));
            detailProgressBar_->setValue(100);
            detailOutputEdit_->setPlainText(output);
            retryButton_->setEnabled(!success && exec.canRetry);
            cancelButton_->setEnabled(false);
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
        if (exec.status == ToolExecution::Running) {
            exec.endTime = QDateTime::currentDateTime();
            exec.duration = exec.startTime.msecsTo(exec.endTime);
            exec.status = ToolExecution::Cancelled;
            exec.output = tr("Execution cancelled by user");
            
            // Update model
            model_->updateExecution(id, exec);
            
            // Update detail panel if selected
            if (selectedExecution_ == id) {
                detailStatusLabel_->setText(tr("Cancelled"));
                detailDurationLabel_->setText(timelineWidget_->formatDuration(exec.duration));
                detailProgressBar_->setValue(exec.progress);
                detailOutputEdit_->setPlainText(exec.output);
                retryButton_->setEnabled(exec.canRetry);
                cancelButton_->setEnabled(false);
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
        if (exec.status == ToolExecution::Running) {
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
        switch (exec.status) {
        case ToolExecution::Pending: statusText = tr("Pending"); break;
        case ToolExecution::Running: statusText = tr("Running"); break;
        case ToolExecution::Success: statusText = tr("Success"); break;
        case ToolExecution::Failed: statusText = tr("Failed"); break;
        case ToolExecution::Cancelled: statusText = tr("Cancelled"); break;
        }
        detailStatusLabel_->setText(statusText);
        
        if (exec.duration > 0) {
            detailDurationLabel_->setText(timelineWidget_->formatDuration(exec.duration));
        } else {
            detailDurationLabel_->setText(tr("In progress..."));
        }
        
        detailProgressBar_->setValue(exec.progress);
        if (!exec.progressMessage.isEmpty()) {
            detailProgressBar_->setFormat(exec.progressMessage + " %p%");
        } else {
            detailProgressBar_->setFormat("%p%");
        }
        
        QJsonDocument doc(exec.parameters);
        detailParametersEdit_->setPlainText(doc.toJson(QJsonDocument::Indented));
        
        detailOutputEdit_->setPlainText(exec.output);
        
        retryButton_->setEnabled(exec.status == ToolExecution::Failed && exec.canRetry);
        cancelButton_->setEnabled(exec.status == ToolExecution::Running);
        
        // Show in current view
        if (viewModeCombo_->currentIndex() < 2) {
            // Tree or table view
            QModelIndex index = model_->indexForId(id);
            if (index.isValid()) {
                QModelIndex proxyIndex = proxyModel_->mapFromSource(index);
                if (viewModeCombo_->currentIndex() == 0) {
                    treeView_->scrollTo(proxyIndex);
                    treeView_->setCurrentIndex(proxyIndex);
                } else {
                    tableView_->scrollTo(proxyIndex);
                    tableView_->setCurrentIndex(proxyIndex);
                }
            }
        } else if (viewModeCombo_->currentIndex() == 2) {
            // Timeline view
            timelineWidget_->highlightExecution(id);
        }
    }
}

void ToolExecutionDock::setViewMode(const QString& mode) {
    int index = 0;
    if (mode == "table") index = 1;
    else if (mode == "timeline") index = 2;
    else if (mode == "performance") index = 3;
    
    viewModeCombo_->setCurrentIndex(index);
}

void ToolExecutionDock::setTimeRange(const QDateTime& start, const QDateTime& end) {
    timelineWidget_->setTimeRange(start, end);
    chartWidget_->setTimeRange(start, end);
}

void ToolExecutionDock::setToolFilter(const QStringList& tools) {
    toolFilter_ = tools;
    applyFilters();
}

void ToolExecutionDock::setStatusFilter(const QList<ToolExecution::Status>& statuses) {
    statusFilter_ = statuses;
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
    favorites_.removeIf([&name](const FavoriteExecution& fav) {
        return fav.name == name;
    });
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

void ToolExecutionDock::retryExecution(const QUuid& id) {
    if (executionMap_.contains(id)) {
        ToolExecution& exec = *executionMap_[id];
        if (exec.status == ToolExecution::Failed && exec.canRetry) {
            exec.retryCount++;
            
            // Start new execution with same parameters
            QUuid newId = startExecution(exec.toolName, exec.parameters);
            
            // Link as retry
            if (executionMap_.contains(newId)) {
                executionMap_[newId]->dependencies.append(id);
                exec.dependents.append(newId);
            }
            
            emit retryRequested(id);
        }
    }
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
    // Update timeline
    timelineWidget_->setExecutions(executions_);
    
    // Update chart
    chartWidget_->setExecutions(executions_);
    
    emit metricsUpdated();
}

void ToolExecutionDock::onThemeChanged() {
    // Update views
    update();
}

void ToolExecutionDock::onViewModeChanged(int index) {
    viewTabs_->setCurrentIndex(index);
    
    // Update detail panel visibility
    detailPanel_->setVisible(index < 2); // Only for list/table views
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
    if (viewModeCombo_->currentIndex() == 0) {
        selModel = treeView_->selectionModel();
    } else if (viewModeCombo_->currentIndex() == 1) {
        selModel = tableView_->selectionModel();
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

void ToolExecutionDock::onTimelineExecutionClicked(const QUuid& id) {
    showExecution(id);
}

void ToolExecutionDock::onChartDataPointClicked(const QString& label, double value) {
    Q_UNUSED(value);
    
    // Filter by clicked data point
    if (chartWidget_->property("groupBy").toString() == "tool") {
        toolFilter_ = {label};
        
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
        toolFilter_ = {toolFilterCombo_->currentText()};
    } else {
        toolFilter_.clear();
    }
    
    statusFilter_.clear();
    switch (statusFilterCombo_->currentIndex()) {
    case 1: statusFilter_.append(ToolExecution::Running); break;
    case 2: statusFilter_.append(ToolExecution::Success); break;
    case 3: statusFilter_.append(ToolExecution::Failed); break;
    case 4: statusFilter_.append(ToolExecution::Cancelled); break;
    }
    
    applyFilters();
}

void ToolExecutionDock::onExportClicked() {
    auto* menu = new QMenu(this);
    
    menu->addAction(tr("Export Data as JSON"), [this]() { exportData("json"); });
    menu->addAction(tr("Export Data as CSV"), [this]() { exportData("csv"); });
    menu->addSeparator();
    menu->addAction(tr("Export Metrics as CSV"), [this]() { exportMetrics("csv"); });
    menu->addSeparator();
    menu->addAction(tr("Export Timeline as Image"), [this]() { 
        timelineWidget_->grab().save(
            QFileDialog::getSaveFileName(this, tr("Export Timeline"), 
                                       "timeline.png", tr("PNG Files (*.png)"))
        );
    });
    menu->addAction(tr("Export Chart as Image"), [this]() { chartWidget_->exportChart("png"); });
    
    menu->exec(QCursor::pos());
    menu->deleteLater();
}

void ToolExecutionDock::updateRunningExecutions() {
    // Update duration for running executions
    bool hasUpdates = false;
    
    for (ToolExecution& exec : executions_) {
        if (exec.status == ToolExecution::Running) {
            exec.duration = exec.startTime.msecsTo(QDateTime::currentDateTime());
            hasUpdates = true;
            
            // Update model
            model_->updateExecution(exec.id, exec);
            
            // Update detail panel if selected
            if (selectedExecution_ == exec.id) {
                detailDurationLabel_->setText(timelineWidget_->formatDuration(exec.duration));
            }
        }
    }
    
    if (hasUpdates) {
        // Update timeline
        timelineWidget_->update();
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
    
    settings.setValue("viewMode", viewModeCombo_->currentText().toLower());
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
    proxyModel_->setFilterFixedString(""); // Reset
    
    // Custom filter logic would go here
    // For now, just emit signal
    emit executionStarted(QUuid()); // Trigger update
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
            switch (exec.status) {
            case ToolExecution::Pending: return tr("Pending");
            case ToolExecution::Running: return tr("Running");
            case ToolExecution::Success: return tr("Success");
            case ToolExecution::Failed: return tr("Failed");
            case ToolExecution::Cancelled: return tr("Cancelled");
            }
            break;
        case ProgressColumn:
            return QString("%1%").arg(exec.progress);
        case DurationColumn:
            if (exec.duration > 0) {
                if (exec.duration < 1000) {
                    return QString("%1ms").arg(exec.duration);
                } else {
                    return QString("%1s").arg(exec.duration / 1000.0, 0, 'f', 1);
                }
            }
            return tr("--");
        case StartTimeColumn:
            return exec.startTime.toString("hh:mm:ss");
        case OutputColumn:
            return exec.output.left(100);
        }
    } else if (role == Qt::DecorationRole && index.column() == StatusColumn) {
        switch (exec.status) {
        case ToolExecution::Pending: return UIUtils::icon("clock");
        case ToolExecution::Running: return UIUtils::icon("media-playback-start");
        case ToolExecution::Success: return UIUtils::icon("dialog-ok");
        case ToolExecution::Failed: return UIUtils::icon("dialog-error");
        case ToolExecution::Cancelled: return UIUtils::icon("dialog-cancel");
        }
    } else if (role == Qt::ForegroundRole) {
        if (exec.status == ToolExecution::Failed) {
            return QColor("#F44336");
        } else if (exec.status == ToolExecution::Success) {
            return QColor("#4CAF50");
        }
    } else if (role == ExecutionRole) {
        return QVariant::fromValue(exec);
    } else if (role == IdRole) {
        return exec.id;
    } else if (role == StatusRole) {
        return static_cast<int>(exec.status);
    } else if (role == ProgressRole) {
        return exec.progress;
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
    case ProgressColumn: return tr("Progress");
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

} // namespace llm_re::ui_v2