#include "line_chart.h"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <limits>

namespace llm_re::ui_v2::charts {

LineChart::LineChart(QWidget* parent)
    : CustomChartBase(parent) {
    
    // Set default theme
    ChartThemePresets::loadPreset("smooth", theme_);
    
    // Configure default axes for line chart
    xAxis_.type = AxisConfig::Linear;
    yAxis_.type = AxisConfig::Linear;
    
    setTitle("Line Chart");
}

LineChart::~LineChart() = default;

void LineChart::addSeries(const ChartSeries& series) {
    series_.push_back(series);
    
    // Assign color if not set
    if (!series_.back().color.isValid()) {
        series_.back().color = ColorPalette::getColorAt(series_.size() - 1, 
                                                       ColorPalette::getDefaultPalette());
    }
    
    // Set fill color based on main color
    if (!series_.back().fillColor.isValid() && series_.back().fillArea) {
        series_.back().fillColor = series_.back().color;
        series_.back().fillColor.setAlphaF(theme_.areaOpacity);
    }
    
    // Initialize animation state
    previousPoints_.resize(series_.size());
    seriesAnimationProgress_.resize(series_.size(), 0.0f);
    
    updateAxisRanges();
    screenPointsCacheDirty_ = true;
    
    emit seriesAdded(series_.size() - 1);
    
    if (effects_.animationEnabled) {
        startAnimation();
    }
    
    update();
}

void LineChart::addSeries(const QString& name, const std::vector<ChartDataPoint>& points) {
    ChartSeries series(name);
    series.points = points;
    addSeries(series);
}

void LineChart::updateSeries(int index, const ChartSeries& series) {
    if (index < 0 || index >= static_cast<int>(series_.size())) return;
    
    // Store previous points for animation
    if (effects_.animationEnabled && !series_[index].points.empty()) {
        previousPoints_[index] = dataPointsToScreen(series_[index]);
    }
    
    series_[index] = series;
    updateAxisRanges();
    screenPointsCacheDirty_ = true;
    
    if (effects_.animationEnabled) {
        seriesAnimationProgress_[index] = 0.0f;
        startAnimation();
    }
    
    update();
}

void LineChart::updateSeries(const QString& name, const std::vector<ChartDataPoint>& points) {
    for (size_t i = 0; i < series_.size(); ++i) {
        if (series_[i].name == name) {
            ChartSeries updated = series_[i];
            updated.points = points;
            updateSeries(i, updated);
            return;
        }
    }
}

void LineChart::removeSeries(int index) {
    if (index < 0 || index >= static_cast<int>(series_.size())) return;
    
    series_.erase(series_.begin() + index);
    previousPoints_.erase(previousPoints_.begin() + index);
    seriesAnimationProgress_.erase(seriesAnimationProgress_.begin() + index);
    
    updateAxisRanges();
    screenPointsCacheDirty_ = true;
    
    emit seriesRemoved(index);
    update();
}

void LineChart::removeSeries(const QString& name) {
    for (size_t i = 0; i < series_.size(); ++i) {
        if (series_[i].name == name) {
            removeSeries(i);
            return;
        }
    }
}

void LineChart::clearSeries() {
    series_.clear();
    previousPoints_.clear();
    seriesAnimationProgress_.clear();
    cachedScreenPoints_.clear();
    screenPointsCacheDirty_ = true;
    update();
}

ChartSeries* LineChart::series(int index) {
    if (index < 0 || index >= static_cast<int>(series_.size())) return nullptr;
    return &series_[index];
}

const ChartSeries* LineChart::series(int index) const {
    if (index < 0 || index >= static_cast<int>(series_.size())) return nullptr;
    return &series_[index];
}

ChartSeries* LineChart::series(const QString& name) {
    for (auto& s : series_) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

const ChartSeries* LineChart::series(const QString& name) const {
    for (const auto& s : series_) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

void LineChart::setTheme(const LineChartTheme& theme) {
    theme_ = theme;
    update();
}

void LineChart::setSmoothing(bool smooth) {
    theme_.smoothCurves = smooth;
    screenPointsCacheDirty_ = true;
    update();
}

void LineChart::setShowDataPoints(bool show) {
    theme_.showDataPoints = show;
    update();
}

void LineChart::setFillArea(bool fill) {
    theme_.fillArea = fill;
    
    // Update fill colors for all series
    for (auto& s : series_) {
        s.fillArea = fill;
        if (fill && !s.fillColor.isValid()) {
            s.fillColor = s.color;
            s.fillColor.setAlphaF(theme_.areaOpacity);
        }
    }
    
    update();
}

void LineChart::setAreaOpacity(float opacity) {
    theme_.areaOpacity = opacity;
    
    // Update fill colors
    for (auto& s : series_) {
        if (s.fillArea && s.fillColor.isValid()) {
            s.fillColor.setAlphaF(opacity);
        }
    }
    
    update();
}

void LineChart::setTimeSeriesMode(bool enabled) {
    timeSeriesMode_ = enabled;
    
    if (enabled) {
        xAxis_.type = AxisConfig::DateTime;
    } else {
        xAxis_.type = AxisConfig::Linear;
    }
    
    updateAxisRanges();
    update();
}

void LineChart::setTimeFormat(const QString& format) {
    timeFormat_ = format;
    if (timeSeriesMode_) {
        update();
    }
}

void LineChart::setAutoScroll(bool enabled) {
    autoScroll_ = enabled;
}

void LineChart::setMaxDataPoints(int max) {
    maxDataPoints_ = max;
    
    // Trim existing series if needed
    for (auto& s : series_) {
        if (static_cast<int>(s.points.size()) > max) {
            s.points.erase(s.points.begin(), 
                          s.points.begin() + (s.points.size() - max));
        }
    }
    
    updateAxisRanges();
    update();
}

void LineChart::updateData() {
    updateAxisRanges();
    screenPointsCacheDirty_ = true;
    update();
}

void LineChart::appendDataPoint(int seriesIndex, const ChartDataPoint& point) {
    if (seriesIndex < 0 || seriesIndex >= static_cast<int>(series_.size())) return;
    
    auto& s = series_[seriesIndex];
    s.points.push_back(point);
    
    // Trim old points if needed
    if (static_cast<int>(s.points.size()) > maxDataPoints_) {
        s.points.erase(s.points.begin());
    }
    
    // Auto-scroll if enabled
    if (autoScroll_ && timeSeriesMode_) {
        // Update x-axis to show latest data
        double range = xAxis_.max - xAxis_.min;
        xAxis_.max = point.timestamp.toMSecsSinceEpoch() / 1000.0;
        xAxis_.min = xAxis_.max - range;
    }
    
    updateAxisRanges();
    screenPointsCacheDirty_ = true;
    
    emit dataPointAdded(seriesIndex, point);
    
    if (effects_.animationEnabled && theme_.animateOnUpdate) {
        seriesAnimationProgress_[seriesIndex] = 0.8f; // Start partially through for smooth update
        startAnimation();
    }
    
    update();
}

void LineChart::appendDataPoint(const QString& seriesName, const ChartDataPoint& point) {
    for (size_t i = 0; i < series_.size(); ++i) {
        if (series_[i].name == seriesName) {
            appendDataPoint(i, point);
            return;
        }
    }
}

void LineChart::drawData(QPainter* painter) {
    if (series_.empty()) return;
    
    // Update cached screen points if needed
    if (screenPointsCacheDirty_) {
        cachedScreenPoints_.clear();
        cachedScreenPoints_.reserve(series_.size());
        
        for (const auto& s : series_) {
            cachedScreenPoints_.push_back(dataPointsToScreen(s));
        }
        
        screenPointsCacheDirty_ = false;
    }
    
    // Draw in reverse order so first series is on top
    for (int i = series_.size() - 1; i >= 0; --i) {
        if (series_[i].visible) {
            drawSeries(painter, series_[i], i);
        }
    }
}

void LineChart::drawLegend(QPainter* painter) {
    if (!legend_.visible || series_.empty()) return;
    
    painter->save();
    
    // Calculate legend size
    QFont legendFont = font();
    legendFont.setPointSize(10);
    painter->setFont(legendFont);
    
    QFontMetrics fm(legendFont);
    int itemHeight = std::max(legend_.iconSize, fm.height()) + legend_.spacing;
    int maxItemWidth = 0;
    
    for (const auto& s : series_) {
        int itemWidth = legend_.iconSize + legend_.spacing + fm.horizontalAdvance(s.name);
        maxItemWidth = std::max(maxItemWidth, itemWidth);
    }
    
    // Calculate layout
    legendLayout_.itemRects.clear();
    QRectF legendRect;
    
    switch (legend_.position) {
        case LegendConfig::Right:
            legendRect = QRectF(chartRect_.right() + 20, chartRect_.top(),
                              maxItemWidth + 2 * legend_.padding,
                              series_.size() * itemHeight + 2 * legend_.padding);
            legendLayout_.columns = 1;
            legendLayout_.rows = series_.size();
            break;
            
        case LegendConfig::Bottom:
            legendLayout_.columns = std::max(1, static_cast<int>(width() / (maxItemWidth + 20)));
            legendLayout_.rows = (series_.size() + legendLayout_.columns - 1) / legendLayout_.columns;
            legendRect = QRectF(chartRect_.left(), chartRect_.bottom() + 20,
                              chartRect_.width(),
                              legendLayout_.rows * itemHeight + 2 * legend_.padding);
            break;
            
        case LegendConfig::Top:
            legendLayout_.columns = std::max(1, static_cast<int>(width() / (maxItemWidth + 20)));
            legendLayout_.rows = (series_.size() + legendLayout_.columns - 1) / legendLayout_.columns;
            legendRect = QRectF(chartRect_.left(), 10,
                              chartRect_.width(),
                              legendLayout_.rows * itemHeight + 2 * legend_.padding);
            break;
            
        default:
            legendRect = QRectF(chartRect_.right() + 20, chartRect_.top(),
                              maxItemWidth + 2 * legend_.padding,
                              series_.size() * itemHeight + 2 * legend_.padding);
            break;
    }
    
    legendLayout_.boundingRect = legendRect;
    
    // Draw legend background
    if (legend_.backgroundColor.isValid()) {
        painter->fillRect(legendRect, legend_.backgroundColor);
    } else {
        QColor bgColor = themeColor(ThemeColor::BackgroundElevated);
        bgColor.setAlpha(200);
        painter->fillRect(legendRect, bgColor);
    }
    
    // Draw legend border
    if (legend_.borderWidth > 0) {
        QPen borderPen(legend_.borderColor.isValid() ? 
                      legend_.borderColor : themeColor(ThemeColor::Border));
        borderPen.setWidthF(legend_.borderWidth);
        painter->setPen(borderPen);
        painter->drawRect(legendRect);
    }
    
    // Draw legend items
    int col = 0, row = 0;
    for (size_t i = 0; i < series_.size(); ++i) {
        QRectF itemRect;
        
        if (legend_.position == LegendConfig::Right || legend_.position == LegendConfig::Left) {
            itemRect = QRectF(legendRect.left() + legend_.padding,
                            legendRect.top() + legend_.padding + i * itemHeight,
                            legendRect.width() - 2 * legend_.padding,
                            itemHeight - legend_.spacing);
        } else {
            itemRect = QRectF(legendRect.left() + legend_.padding + col * (maxItemWidth + 20),
                            legendRect.top() + legend_.padding + row * itemHeight,
                            maxItemWidth,
                            itemHeight - legend_.spacing);
            
            col++;
            if (col >= legendLayout_.columns) {
                col = 0;
                row++;
            }
        }
        
        legendLayout_.itemRects.push_back(itemRect);
        drawLegendItem(painter, itemRect, series_[i]);
    }
    
    painter->restore();
}

int LineChart::findNearestDataPoint(const QPointF& pos, int& seriesIndex) {
    seriesIndex = -1;
    int pointIndex = -1;
    double minDistance = std::numeric_limits<double>::max();
    
    for (size_t i = 0; i < series_.size(); ++i) {
        if (!series_[i].visible || series_[i].points.empty()) continue;
        
        const auto& screenPoints = cachedScreenPoints_[i];
        
        for (size_t j = 0; j < screenPoints.size(); ++j) {
            double distance = QLineF(pos, screenPoints[j]).length();
            
            if (distance < minDistance && distance < 10.0) { // 10 pixel threshold
                minDistance = distance;
                seriesIndex = i;
                pointIndex = j;
            }
        }
    }
    
    // Update tooltip if point found
    if (pointIndex >= 0) {
        const auto& point = series_[seriesIndex].points[pointIndex];
        QString tooltipText = QString("%1\n%2: %3")
            .arg(series_[seriesIndex].name)
            .arg(timeSeriesMode_ ? point.timestamp.toString(timeFormat_) : 
                 QString::number(point.x, 'f', 2))
            .arg(QString::number(point.y, 'f', 2));
        
        tooltipText_ = tooltipText;
    }
    
    return pointIndex;
}

void LineChart::drawSeries(QPainter* painter, const ChartSeries& series, int seriesIndex) {
    if (series.points.empty()) return;
    
    const auto& screenPoints = getAnimatedPoints(cachedScreenPoints_[seriesIndex], seriesIndex);
    
    if (screenPoints.empty()) return;
    
    // Draw area fill first (behind line)
    if (series.fillArea || theme_.fillArea) {
        drawAreaFill(painter, screenPoints, series);
    }
    
    // Draw line
    if (series.showLine) {
        if (theme_.smoothCurves && screenPoints.size() > 2) {
            drawSmoothLine(painter, screenPoints, series);
        } else {
            drawStraightLine(painter, screenPoints, series);
        }
    }
    
    // Draw data points
    if (series.showPoints || theme_.showDataPoints) {
        drawDataPoints(painter, screenPoints, series, seriesIndex);
    }
}

void LineChart::drawSmoothLine(QPainter* painter, const std::vector<QPointF>& points, 
                              const ChartSeries& series) {
    if (points.size() < 2) return;
    
    painter->save();
    
    // Generate smooth curve
    std::vector<QPointF> smoothPoints = ChartUtils::generateSmoothCurve(points, 10);
    
    // Create path
    QPainterPath path;
    path.moveTo(smoothPoints[0]);
    for (size_t i = 1; i < smoothPoints.size(); ++i) {
        path.lineTo(smoothPoints[i]);
    }
    
    // Draw glow effect if enabled
    if (effects_.glowEnabled) {
        QColor glowColor = series.color;
        glowColor.setAlphaF(effects_.glowIntensity);
        ChartUtils::drawGlowEffect(painter, path, glowColor, effects_.glowRadius);
    }
    
    // Draw main line
    QPen pen(series.color);
    pen.setWidthF(series.lineWidth);
    pen.setStyle(series.lineStyle);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    
    // Hover effect
    if (interaction_.hoveredSeriesIndex == static_cast<int>(std::distance(
            &series_[0], &series))) {
        pen.setWidthF(theme_.hoverLineWidth);
        if (theme_.glowOnHover) {
            QColor hoverGlow = series.color;
            hoverGlow.setAlphaF(0.5f);
            ChartUtils::drawGlowEffect(painter, path, hoverGlow, theme_.hoverGlowRadius);
        }
    }
    
    painter->setPen(pen);
    painter->drawPath(path);
    
    painter->restore();
}

void LineChart::drawStraightLine(QPainter* painter, const std::vector<QPointF>& points,
                               const ChartSeries& series) {
    if (points.size() < 2) return;
    
    painter->save();
    
    QPen pen(series.color);
    pen.setWidthF(series.lineWidth);
    pen.setStyle(series.lineStyle);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    
    // Hover effect
    if (interaction_.hoveredSeriesIndex == static_cast<int>(std::distance(
            &series_[0], &series))) {
        pen.setWidthF(theme_.hoverLineWidth);
    }
    
    painter->setPen(pen);
    
    // Draw lines with glow if enabled
    for (size_t i = 1; i < points.size(); ++i) {
        if (effects_.glowEnabled) {
            drawGlowingLine(painter, points[i-1], points[i], series.color, 
                          series.lineWidth, effects_.glowRadius);
        } else {
            painter->drawLine(points[i-1], points[i]);
        }
    }
    
    painter->restore();
}

void LineChart::drawAreaFill(QPainter* painter, const std::vector<QPointF>& points,
                           const ChartSeries& series) {
    if (points.empty()) return;
    
    painter->save();
    
    // Create area path
    QPainterPath path;
    path.moveTo(points[0].x(), chartRect_.bottom());
    
    for (const auto& point : points) {
        path.lineTo(point);
    }
    
    path.lineTo(points.back().x(), chartRect_.bottom());
    path.closeSubpath();
    
    // Fill with gradient
    QLinearGradient gradient(0, chartRect_.top(), 0, chartRect_.bottom());
    QColor fillColor = series.fillColor.isValid() ? series.fillColor : series.color;
    fillColor.setAlphaF(theme_.areaOpacity);
    
    gradient.setColorAt(0, fillColor);
    fillColor.setAlphaF(0.0f);
    gradient.setColorAt(1, fillColor);
    
    painter->fillPath(path, gradient);
    
    painter->restore();
}

void LineChart::drawDataPoints(QPainter* painter, const std::vector<QPointF>& points,
                             const ChartSeries& series, int seriesIndex) {
    painter->save();
    
    float radius = series.pointRadius > 0 ? series.pointRadius : theme_.pointRadius;
    
    for (size_t i = 0; i < points.size(); ++i) {
        bool isHovered = (interaction_.hoveredSeriesIndex == seriesIndex && 
                         interaction_.hoveredPointIndex == static_cast<int>(i));
        
        float currentRadius = radius;
        if (isHovered) {
            currentRadius = theme_.hoverPointRadius;
        }
        
        // Draw point with glow
        drawGlowingPoint(painter, points[i], currentRadius, series.color,
                        isHovered ? effects_.hoverGlow * effects_.glowRadius : 0);
        
        // Draw white center for better visibility
        painter->setPen(Qt::NoPen);
        painter->setBrush(Qt::white);
        painter->drawEllipse(points[i], currentRadius * 0.6f, currentRadius * 0.6f);
        
        // Draw colored ring
        painter->setBrush(series.color);
        painter->drawEllipse(points[i], currentRadius * 0.4f, currentRadius * 0.4f);
    }
    
    painter->restore();
}

void LineChart::drawLegendItem(QPainter* painter, const QRectF& rect, const ChartSeries& series) {
    painter->save();
    
    // Draw line sample
    QRectF iconRect(rect.left(), rect.center().y() - legend_.iconSize / 2.0,
                   legend_.iconSize, legend_.iconSize);
    
    if (series.showLine) {
        QPen pen(series.color);
        pen.setWidthF(series.lineWidth);
        pen.setStyle(series.lineStyle);
        painter->setPen(pen);
        painter->drawLine(iconRect.left(), iconRect.center().y(),
                         iconRect.right(), iconRect.center().y());
    }
    
    // Draw point sample
    if (series.showPoints || theme_.showDataPoints) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(series.color);
        painter->drawEllipse(iconRect.center(), 3, 3);
    }
    
    // Draw series name
    painter->setPen(legend_.textColor.isValid() ? 
                   legend_.textColor : themeColor(ThemeColor::Text));
    QRectF textRect(iconRect.right() + legend_.spacing, rect.top(),
                   rect.width() - iconRect.width() - legend_.spacing, rect.height());
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, series.name);
    
    // Draw visibility indicator if series is hidden
    if (!series.visible) {
        painter->setPen(QPen(themeColor(ThemeColor::TextSecondary), 1, Qt::DashLine));
        painter->drawLine(rect.topLeft(), rect.bottomRight());
    }
    
    painter->restore();
}

std::vector<QPointF> LineChart::getAnimatedPoints(const std::vector<QPointF>& targetPoints,
                                                 int seriesIndex) {
    if (!effects_.animationEnabled || !animationState_.isAnimating) {
        return targetPoints;
    }
    
    float progress = animationState_.getEasedProgress();
    
    // Series-specific animation progress
    if (seriesIndex < static_cast<int>(seriesAnimationProgress_.size())) {
        progress = seriesAnimationProgress_[seriesIndex];
    }
    
    if (progress >= 1.0f) {
        return targetPoints;
    }
    
    std::vector<QPointF> animatedPoints;
    
    // Drawing animation - reveal from left to right
    if (theme_.animateDrawing && previousPoints_[seriesIndex].empty()) {
        int visiblePoints = static_cast<int>(targetPoints.size() * progress);
        animatedPoints.reserve(visiblePoints);
        
        for (int i = 0; i < visiblePoints; ++i) {
            animatedPoints.push_back(targetPoints[i]);
        }
        
        // Add partially revealed point
        if (visiblePoints < static_cast<int>(targetPoints.size())) {
            float partialProgress = (targetPoints.size() * progress) - visiblePoints;
            QPointF partial = targetPoints[visiblePoints];
            if (visiblePoints > 0) {
                partial = ChartUtils::lerp(targetPoints[visiblePoints - 1], 
                                         targetPoints[visiblePoints], partialProgress);
            }
            animatedPoints.push_back(partial);
        }
    } else if (!previousPoints_[seriesIndex].empty()) {
        // Update animation - morph from previous to new
        animatedPoints.reserve(targetPoints.size());
        
        for (size_t i = 0; i < targetPoints.size(); ++i) {
            QPointF from = (i < previousPoints_[seriesIndex].size()) ? 
                          previousPoints_[seriesIndex][i] : targetPoints[i];
            animatedPoints.push_back(ChartUtils::lerp(from, targetPoints[i], progress));
        }
    } else {
        // Growth animation - scale from bottom
        animatedPoints.reserve(targetPoints.size());
        
        for (const auto& point : targetPoints) {
            QPointF from(point.x(), chartRect_.bottom());
            animatedPoints.push_back(ChartUtils::lerp(from, point, progress));
        }
    }
    
    return animatedPoints;
}

void LineChart::updateAxisRanges() {
    if (series_.empty()) return;
    
    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    
    for (const auto& s : series_) {
        if (!s.visible || s.points.empty()) continue;
        
        for (const auto& point : s.points) {
            double x = timeSeriesMode_ ? 
                      point.timestamp.toMSecsSinceEpoch() / 1000.0 : point.x;
            
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, point.y);
            maxY = std::max(maxY, point.y);
        }
    }
    
    // Apply nice scale
    if (xAxis_.autoScale && minX != std::numeric_limits<double>::max()) {
        ChartUtils::calculateNiceScale(minX, maxX, xAxis_.min, xAxis_.max, xAxis_.tickInterval);
    }
    
    if (yAxis_.autoScale && minY != std::numeric_limits<double>::max()) {
        ChartUtils::calculateNiceScale(minY, maxY, yAxis_.min, yAxis_.max, yAxis_.tickInterval);
        
        // Add some padding to Y axis
        double range = yAxis_.max - yAxis_.min;
        yAxis_.min -= range * 0.05;
        yAxis_.max += range * 0.05;
    }
}

std::vector<QPointF> LineChart::dataPointsToScreen(const ChartSeries& series) {
    std::vector<QPointF> screenPoints;
    screenPoints.reserve(series.points.size());
    
    for (const auto& point : series.points) {
        screenPoints.push_back(dataPointToScreen(point));
    }
    
    return screenPoints;
}

QPointF LineChart::dataPointToScreen(const ChartDataPoint& point) {
    double x = timeSeriesMode_ ? 
              point.timestamp.toMSecsSinceEpoch() / 1000.0 : point.x;
    
    double screenX = chartRect_.left() + ChartUtils::valueToPixel(
        x, xAxis_.min, xAxis_.max, chartRect_.width());
    double screenY = chartRect_.bottom() - ChartUtils::valueToPixel(
        point.y, yAxis_.min, yAxis_.max, chartRect_.height());
    
    return QPointF(screenX, screenY);
}

bool LineChart::isPointNearMouse(const QPointF& point, const QPointF& mousePos) {
    return QLineF(point, mousePos).length() < 10.0;
}

} // namespace llm_re::ui_v2::charts