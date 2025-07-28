#include "../../core/ui_v2_common.h"
#include "circular_chart.h"
#include "../../core/theme_manager.h"

namespace llm_re::ui_v2::charts {

CircularChart::CircularChart(QWidget* parent)
    : CustomChartBase(parent) {
    
    // Set default theme
    ChartThemePresets::loadPreset("donut", theme_);
    
    // Set default colors
    colors_ = ColorPalette::getDefaultPalette();
    
    // Hide axes for circular charts
    xAxis_.visible = false;
    yAxis_.visible = false;
    xAxis_.showGrid = false;
    yAxis_.showGrid = false;
    
    setTitle("Circular Chart");
}

CircularChart::~CircularChart() = default;

void CircularChart::setChartType(ChartType type) {
    if (chartType_ != type) {
        chartType_ = type;
        
        // Adjust theme based on type
        switch (type) {
            case Pie:
                theme_.innerRadiusRatio = 0.0f;
                break;
            case Donut:
                theme_.innerRadiusRatio = 0.6f;
                break;
            case Gauge:
                theme_.innerRadiusRatio = 0.75f;
                theme_.startAngle = -225.0f;
                break;
            case RadialBar:
                theme_.innerRadiusRatio = 0.4f;
                break;
        }
        
        calculateSegments();
        update();
    }
}

void CircularChart::setData(const std::vector<ChartDataPoint>& data) {
    data_ = data;
    
    // Assign colors if not set
    for (size_t i = 0; i < data_.size(); ++i) {
        if (!data_[i].color.isValid()) {
            data_[i].color = colors_[i % colors_.size()];
        }
    }
    
    // Initialize animation states
    segmentAnimationProgress_.resize(data_.size(), 0.0f);
    
    calculateSegments();
    
    if (effects_.animationEnabled && theme_.animateRotation) {
        startAnimation();
    }
    
    update();
}

void CircularChart::addDataPoint(const ChartDataPoint& point) {
    ChartDataPoint newPoint = point;
    if (!newPoint.color.isValid()) {
        newPoint.color = colors_[data_.size() % colors_.size()];
    }
    
    data_.push_back(newPoint);
    segmentAnimationProgress_.push_back(0.0f);
    
    calculateSegments();
    
    if (effects_.animationEnabled) {
        segmentAnimationProgress_.back() = 0.0f;
        startAnimation();
    }
    
    update();
}

void CircularChart::updateDataPoint(int index, const ChartDataPoint& point) {
    if (index < 0 || index >= static_cast<int>(data_.size())) return;
    
    data_[index] = point;
    if (!data_[index].color.isValid()) {
        data_[index].color = colors_[index % colors_.size()];
    }
    
    calculateSegments();
    update();
}

void CircularChart::removeDataPoint(int index) {
    if (index < 0 || index >= static_cast<int>(data_.size())) return;
    
    data_.erase(data_.begin() + index);
    segmentAnimationProgress_.erase(segmentAnimationProgress_.begin() + index);
    
    calculateSegments();
    update();
}

void CircularChart::clearData() {
    data_.clear();
    segments_.clear();
    segmentAnimationProgress_.clear();
    hoveredSegment_ = -1;
    selectedSegment_ = -1;
    total_ = 0.0;
    update();
}

void CircularChart::setTheme(const CircularChartTheme& theme) {
    theme_ = theme;
    calculateSegments();
    update();
}

void CircularChart::setInnerRadius(float ratio) {
    theme_.innerRadiusRatio = qBound(0.0f, ratio, 0.9f);
    calculateSegments();
    update();
}

void CircularChart::setCenterText(const QString& text) {
    centerText_ = text;
    update();
}

void CircularChart::setCenterValue(double value, const QString& suffix) {
    centerValue_ = QString::number(value, 'f', 1);
    centerSuffix_ = suffix;
    update();
}

void CircularChart::setShowLabels(bool show) {
    theme_.showLabels = show;
    update();
}

void CircularChart::setShowPercentages(bool show) {
    theme_.showPercentages = show;
    update();
}

void CircularChart::setSegmentSpacing(float spacing) {
    theme_.segmentSpacing = spacing;
    calculateSegments();
    update();
}

void CircularChart::setGaugeRange(double min, double max) {
    gaugeMin_ = min;
    gaugeMax_ = max;
    update();
}

void CircularChart::setGaugeValue(double value) {
    gaugeValue_ = qBound(gaugeMin_, value, gaugeMax_);
    
    if (effects_.animationEnabled) {
        startAnimation();
    }
    
    update();
}

void CircularChart::setGaugeThresholds(const std::vector<std::pair<double, QColor>>& thresholds) {
    gaugeThresholds_ = thresholds;
    update();
}

void CircularChart::setColorPalette(const std::vector<QColor>& colors) {
    colors_ = colors;
    
    // Update data point colors
    for (size_t i = 0; i < data_.size(); ++i) {
        data_[i].color = colors_[i % colors_.size()];
    }
    
    calculateSegments();
    update();
}

void CircularChart::setRotationAnimation(bool enabled) {
    theme_.animateRotation = enabled;
}

void CircularChart::setStartAngle(float angle) {
    theme_.startAngle = angle;
    calculateSegments();
    update();
}

void CircularChart::updateData() {
    calculateSegments();
    update();
}

int CircularChart::segmentAt(const QPointF& pos) const {
    for (size_t i = 0; i < segments_.size(); ++i) {
        if (isPointInSegment(pos, static_cast<int>(i))) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

double CircularChart::getTotal() const {
    return total_;
}

double CircularChart::getPercentage(int index) const {
    if (index < 0 || index >= static_cast<int>(segments_.size()) || total_ == 0) {
        return 0.0;
    }
    return segments_[index].percentage;
}

void CircularChart::drawData(QPainter* painter) {
    switch (chartType_) {
        case Pie:
            drawPieChart(painter);
            break;
        case Donut:
            drawDonutChart(painter);
            break;
        case Gauge:
            drawGaugeChart(painter);
            break;
        case RadialBar:
            drawRadialBarChart(painter);
            break;
    }
}

void CircularChart::drawLegend(QPainter* painter) {
    if (!legend_.visible || data_.empty()) return;
    
    CustomChartBase::drawLegend(painter);
}

int CircularChart::findNearestDataPoint(const QPointF& pos, int& seriesIndex) {
    seriesIndex = 0; // Single series for circular charts
    int segment = segmentAt(pos);
    
    if (segment >= 0 && segment < static_cast<int>(data_.size())) {
        // Update tooltip
        const auto& point = data_[segment];
        QString tooltipText = QString("%1\nValue: %2\nPercentage: %3%")
            .arg(point.label.isEmpty() ? QString("Segment %1").arg(segment + 1) : point.label)
            .arg(point.y, 0, 'f', 2)
            .arg(getPercentage(segment), 0, 'f', 1);
        
        tooltipText_ = tooltipText;
    }
    
    return segment;
}

void CircularChart::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        int segment = segmentAt(event->pos());
        if (segment >= 0) {
            selectedSegment_ = segment;
            emit segmentClicked(segment);
            update();
        }
    }
    
    CustomChartBase::mousePressEvent(event);
}

void CircularChart::mouseMoveEvent(QMouseEvent* event) {
    int segment = segmentAt(event->pos());
    
    if (segment != hoveredSegment_) {
        hoveredSegment_ = segment;
        
        if (segment >= 0) {
            emit segmentHovered(segment);
        }
        
        update();
    }
    
    CustomChartBase::mouseMoveEvent(event);
}

void CircularChart::mouseReleaseEvent(QMouseEvent* event) {
    CustomChartBase::mouseReleaseEvent(event);
}

void CircularChart::drawPieChart(QPainter* painter) {
    drawDonutChart(painter); // Same as donut with inner radius = 0
}

void CircularChart::drawDonutChart(QPainter* painter) {
    if (segments_.empty()) return;
    
    painter->save();
    
    // Draw segments
    for (size_t i = 0; i < segments_.size(); ++i) {
        double startAngle = getAnimatedAngle(segments_[i].startAngle);
        double spanAngle = segments_[i].spanAngle * animationState_.getEasedProgress();
        
        if (effects_.animationEnabled && i < segmentAnimationProgress_.size()) {
            spanAngle *= segmentAnimationProgress_[i];
        }
        
        drawSegment(painter, chartCircleRect_, startAngle, spanAngle, 
                   segments_[i].color, static_cast<int>(i));
    }
    
    // Draw center content for donut
    if (chartType_ == Donut && theme_.innerRadiusRatio > 0) {
        drawCenterContent(painter);
    }
    
    // Draw labels
    if (theme_.showLabels) {
        for (size_t i = 0; i < segments_.size(); ++i) {
            if (segments_[i].spanAngle > 5.0) { // Only show label if segment is large enough
                drawSegmentLabel(painter, chartCircleRect_, 
                               segments_[i].startAngle, segments_[i].spanAngle,
                               data_[i], static_cast<int>(i));
            }
        }
    }
    
    painter->restore();
}

void CircularChart::drawGaugeChart(QPainter* painter) {
    painter->save();
    
    // Draw background arc
    QPen arcPen(ThemeManager::instance().colors().border);
    arcPen.setWidth(20);
    arcPen.setCapStyle(Qt::RoundCap);
    painter->setPen(arcPen);
    painter->setBrush(Qt::NoBrush);
    
    double totalSpan = 270.0; // Gauge typically spans 270 degrees
    painter->drawArc(chartCircleRect_, static_cast<int>(theme_.startAngle * 16), static_cast<int>(totalSpan * 16));
    
    // Draw colored segments based on thresholds
    if (!gaugeThresholds_.empty()) {
        for (size_t i = 0; i < gaugeThresholds_.size(); ++i) {
            double startVal = (i == 0) ? gaugeMin_ : gaugeThresholds_[i-1].first;
            double endVal = gaugeThresholds_[i].first;
            
            double startAngle = theme_.startAngle + (startVal - gaugeMin_) / 
                              (gaugeMax_ - gaugeMin_) * totalSpan;
            double endAngle = theme_.startAngle + (endVal - gaugeMin_) / 
                            (gaugeMax_ - gaugeMin_) * totalSpan;
            
            QPen segmentPen(gaugeThresholds_[i].second);
            segmentPen.setWidth(20);
            segmentPen.setCapStyle(Qt::RoundCap);
            painter->setPen(segmentPen);
            
            painter->drawArc(chartCircleRect_, static_cast<int>(startAngle * 16), 
                           static_cast<int>((endAngle - startAngle) * 16));
        }
    }
    
    // Draw value arc
    double valueAngle = (gaugeValue_ - gaugeMin_) / (gaugeMax_ - gaugeMin_) * totalSpan;
    valueAngle *= animationState_.getEasedProgress();
    
    // Determine color based on value
    QColor valueColor = ThemeManager::instance().colors().primary;
    for (const auto& threshold : gaugeThresholds_) {
        if (gaugeValue_ <= threshold.first) {
            valueColor = threshold.second;
            break;
        }
    }
    
    // Draw value arc with glow
    if (effects_.glowEnabled) {
        QPen glowPen(valueColor);
        glowPen.setWidth(25);
        glowPen.setCapStyle(Qt::RoundCap);
        valueColor.setAlphaF(0.3f);
        glowPen.setColor(valueColor);
        painter->setPen(glowPen);
        painter->drawArc(chartCircleRect_.adjusted(-2, -2, 2, 2), 
                        static_cast<int>(theme_.startAngle * 16), static_cast<int>(valueAngle * 16));
    }
    
    QPen valuePen(valueColor);
    valuePen.setWidth(15);
    valuePen.setCapStyle(Qt::RoundCap);
    painter->setPen(valuePen);
    painter->drawArc(chartCircleRect_, static_cast<int>(theme_.startAngle * 16), static_cast<int>(valueAngle * 16));
    
    // Draw needle
    drawGaugeNeedle(painter, gaugeValue_);
    
    // Draw scale
    drawGaugeScale(painter);
    
    // Draw center value
    painter->setPen(ThemeManager::instance().colors().textPrimary);
    QFont valueFont = font();
    valueFont.setPointSize(24);
    valueFont.setBold(true);
    painter->setFont(valueFont);
    
    QString valueText = QString::number(gaugeValue_, 'f', 1);
    painter->drawText(chartCircleRect_, Qt::AlignCenter, valueText);
    
    painter->restore();
}

void CircularChart::drawRadialBarChart(QPainter* painter) {
    if (data_.empty()) return;
    
    painter->save();
    
    // Calculate bar width and spacing
    float barWidth = (outerRadius_ - innerRadius_) / static_cast<float>(data_.size());
    float spacing = barWidth * 0.1f;
    
    // Find max value for scaling
    double maxValue = 0.0;
    for (const auto& point : data_) {
        maxValue = std::max(maxValue, point.y);
    }
    
    // Draw radial bars
    for (size_t i = 0; i < data_.size(); ++i) {
        float currentInner = innerRadius_ + static_cast<float>(i) * barWidth + spacing;
        float currentOuter = currentInner + barWidth - 2 * spacing;
        
        // Calculate angle based on value
        double angle = 360.0 * (data_[i].y / maxValue);
        angle *= animationState_.getEasedProgress();
        
        // Draw background ring
        QPen bgPen(ThemeManager::instance().colors().border);
        bgPen.setWidth(static_cast<int>(currentOuter - currentInner));
        bgPen.setCapStyle(Qt::RoundCap);
        painter->setPen(bgPen);
        painter->setBrush(Qt::NoBrush);
        
        QRectF barRect = QRectF(
            chartCircleRect_.center().x() - (currentInner + currentOuter) / 2,
            chartCircleRect_.center().y() - (currentInner + currentOuter) / 2,
            currentInner + currentOuter,
            currentInner + currentOuter);
        
        painter->drawArc(barRect, static_cast<int>(theme_.startAngle * 16), 360 * 16);
        
        // Draw value arc
        QColor barColor = data_[i].color;
        if (hoveredSegment_ == static_cast<int>(i)) {
            barColor = barColor.lighter(120);
        }
        
        QPen barPen(barColor);
        barPen.setWidth(static_cast<int>(currentOuter - currentInner));
        barPen.setCapStyle(Qt::RoundCap);
        painter->setPen(barPen);
        
        painter->drawArc(barRect, static_cast<int>(theme_.startAngle * 16), static_cast<int>(angle * 16));
        
        // Draw label
        if (theme_.showLabels && !data_[i].label.isEmpty()) {
            painter->setPen(ThemeManager::instance().colors().textPrimary);
            QFont labelFont = font();
            labelFont.setPointSize(10);
            painter->setFont(labelFont);
            
            double midAngle = theme_.startAngle + angle / 2;
            double labelRadius = (currentInner + currentOuter) / 2;
            QPointF labelPos = chartCircleRect_.center() + 
                QPointF(labelRadius * cos(midAngle * M_PI / 180),
                       labelRadius * sin(midAngle * M_PI / 180));
            
            painter->drawText(labelPos, data_[i].label);
        }
    }
    
    painter->restore();
}

void CircularChart::drawSegment(QPainter* painter, const QRectF& rect, double startAngle, 
                              double spanAngle, const QColor& color, int index) {
    painter->save();
    
    // Calculate animation effects
    float scale = getSegmentScale(index);
    QPointF offset = getSegmentOffset(index);
    
    // Apply transformations
    painter->translate(rect.center() + offset);
    painter->scale(scale, scale);
    painter->translate(-rect.center());
    
    // Create segment path
    QPainterPath path = createSegmentPath(rect, startAngle, spanAngle, 
                                        theme_.innerRadiusRatio > 0);
    
    // Draw shadow if enabled
    if (effects_.shadowEnabled && (hoveredSegment_ == index || selectedSegment_ == index)) {
        ChartUtils::drawShadow(painter, path, effects_);
    }
    
    // Draw segment with gradient
    QRadialGradient gradient(rect.center(), outerRadius_);
    QColor lightColor = color.lighter(110);
    QColor darkColor = color.darker(110);
    
    if (hoveredSegment_ == index) {
        lightColor = lightColor.lighter(110);
        darkColor = darkColor.lighter(110);
    }
    
    gradient.setColorAt(theme_.innerRadiusRatio, lightColor);
    gradient.setColorAt(1.0, darkColor);
    
    painter->fillPath(path, gradient);
    
    // Draw inner shadow for donut
    if (theme_.innerRadiusRatio > 0 && theme_.innerShadow) {
        QPainterPath innerPath;
        QRectF innerRect = rect.adjusted(
            innerRadius_, innerRadius_, -innerRadius_, -innerRadius_);
        innerPath.addEllipse(innerRect);
        
        QRadialGradient innerGradient(rect.center(), innerRadius_);
        innerGradient.setColorAt(0.8, Qt::transparent);
        innerGradient.setColorAt(1.0, QColor(0, 0, 0, 50));
        
        painter->setClipPath(path);
        painter->fillPath(innerPath, innerGradient);
    }
    
    // Draw glow effect for hovered segment
    if (hoveredSegment_ == index && effects_.glowEnabled) {
        QColor glowColor = color;
        glowColor.setAlphaF(0.5f);
        ChartUtils::drawGlowEffect(painter, path, glowColor, theme_.glowRadius);
    }
    
    painter->restore();
}

void CircularChart::drawSegmentLabel(QPainter* painter, const QRectF& rect, double startAngle,
                                   double spanAngle, const ChartDataPoint& data, int index) {
    painter->save();
    
    // Calculate label position
    double midAngle = startAngle + spanAngle / 2;
    double labelRadius = (outerRadius_ + (theme_.innerRadiusRatio > 0 ? innerRadius_ : 0)) / 2;
    
    if (hoveredSegment_ == index) {
        labelRadius += theme_.hoverOffset / 2;
    }
    
    QPointF labelPos = getSegmentLabelPosition(rect, midAngle, labelRadius);
    
    // Prepare label text
    QString labelText = data.label;
    if (labelText.isEmpty()) {
        labelText = QString("Segment %1").arg(index + 1);
    }
    
    if (theme_.showPercentages) {
        labelText += QString("\n%1%").arg(getPercentage(index), 0, 'f', 1);
    }
    
    // Draw label background
    QFont labelFont = font();
    labelFont.setPointSize(10);
    painter->setFont(labelFont);
    
    QRectF textRect = painter->fontMetrics().boundingRect(labelText);
    textRect.moveCenter(labelPos);
    textRect.adjust(-5, -2, 5, 2);
    
    QColor bgColor = ThemeManager::instance().colors().background;
    bgColor.setAlpha(200);
    painter->fillRect(textRect, bgColor);
    
    // Draw label text
    painter->setPen(ThemeManager::instance().colors().textPrimary);
    painter->drawText(textRect, Qt::AlignCenter, labelText);
    
    painter->restore();
}

void CircularChart::drawCenterContent(QPainter* painter) {
    painter->save();
    
    // Draw center circle background
    QRectF centerRect = chartCircleRect_.adjusted(
        innerRadius_, innerRadius_, -innerRadius_, -innerRadius_);
    
    if (effects_.glassMorphism) {
        ChartUtils::drawGlassMorphism(painter, centerRect, effects_);
    } else {
        QColor centerBg = ThemeManager::instance().colors().background;
        centerBg.setAlpha(240);
        painter->setBrush(centerBg);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(centerRect);
    }
    
    // Draw center text
    painter->setPen(ThemeManager::instance().colors().textPrimary);
    
    if (!centerValue_.isEmpty()) {
        // Draw value
        QFont valueFont = font();
        valueFont.setPointSize(24);
        valueFont.setBold(true);
        painter->setFont(valueFont);
        
        QString fullValue = centerValue_ + centerSuffix_;
        painter->drawText(centerRect, Qt::AlignCenter, fullValue);
        
        // Draw label below value
        if (!centerText_.isEmpty()) {
            QFont labelFont = font();
            labelFont.setPointSize(12);
            painter->setFont(labelFont);
            painter->setPen(ThemeManager::instance().colors().textSecondary);
            
            QRectF labelRect = centerRect;
            labelRect.moveTop(centerRect.center().y() + 10);
            painter->drawText(labelRect, Qt::AlignHCenter | Qt::AlignTop, centerText_);
        }
    } else if (!centerText_.isEmpty()) {
        // Draw only text
        QFont textFont = font();
        textFont.setPointSize(14);
        painter->setFont(textFont);
        painter->drawText(centerRect, Qt::AlignCenter, centerText_);
    }
    
    painter->restore();
}

void CircularChart::drawGaugeNeedle(QPainter* painter, double value) {
    painter->save();
    
    // Calculate needle angle
    double totalSpan = 270.0;
    double valueRatio = (value - gaugeMin_) / (gaugeMax_ - gaugeMin_);
    double needleAngle = theme_.startAngle + valueRatio * totalSpan;
    
    // Animate needle
    needleAngle = theme_.startAngle + (needleAngle - theme_.startAngle) * 
                  animationState_.getEasedProgress();
    
    // Draw needle
    painter->translate(chartCircleRect_.center());
    painter->rotate(needleAngle + 90); // +90 because 0 degrees points right
    
    // Create needle shape
    QPainterPath needle;
    needle.moveTo(0, -innerRadius_ * 0.1);
    needle.lineTo(-innerRadius_ * 0.05, 0);
    needle.lineTo(0, outerRadius_ * 0.9);
    needle.lineTo(innerRadius_ * 0.05, 0);
    needle.closeSubpath();
    
    // Draw needle with gradient
    QLinearGradient needleGradient(0, 0, 0, outerRadius_);
    needleGradient.setColorAt(0, ThemeManager::instance().colors().textPrimary);
    needleGradient.setColorAt(1, ThemeManager::instance().colors().primary);
    
    painter->fillPath(needle, needleGradient);
    
    // Draw center cap
    painter->setBrush(ThemeManager::instance().colors().textPrimary);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(QPointF(0, 0), innerRadius_ * 0.15, innerRadius_ * 0.15);
    
    painter->restore();
}

void CircularChart::drawGaugeScale(QPainter* painter) {
    painter->save();
    
    QPen scalePen(ThemeManager::instance().colors().textSecondary);
    scalePen.setWidth(1);
    painter->setPen(scalePen);
    
    QFont scaleFont = font();
    scaleFont.setPointSize(9);
    painter->setFont(scaleFont);
    
    // Draw scale marks and labels
    int numTicks = 11; // 0, 10, 20, ..., 100 for default range
    double totalSpan = 270.0;
    
    for (int i = 0; i < numTicks; ++i) {
        double value = gaugeMin_ + (gaugeMax_ - gaugeMin_) * i / (numTicks - 1);
        double angle = theme_.startAngle + totalSpan * i / (numTicks - 1);
        double radian = angle * M_PI / 180.0;
        
        // Calculate tick positions
        QPointF innerPoint = chartCircleRect_.center() + 
            QPointF(cos(radian) * (outerRadius_ - 30), 
                   sin(radian) * (outerRadius_ - 30));
        QPointF outerPoint = chartCircleRect_.center() + 
            QPointF(cos(radian) * (outerRadius_ - 20), 
                   sin(radian) * (outerRadius_ - 20));
        
        // Draw tick
        painter->drawLine(innerPoint, outerPoint);
        
        // Draw label
        QString label = QString::number(value, 'f', 0);
        QRectF labelRect(0, 0, 40, 20);
        labelRect.moveCenter(chartCircleRect_.center() + 
            QPointF(cos(radian) * (outerRadius_ - 45), 
                   sin(radian) * (outerRadius_ - 45)));
        
        painter->drawText(labelRect, Qt::AlignCenter, label);
    }
    
    painter->restore();
}

void CircularChart::calculateSegments() {
    segments_.clear();
    
    if (data_.empty()) {
        total_ = 0.0;
        return;
    }
    
    // Calculate total
    total_ = std::accumulate(data_.begin(), data_.end(), 0.0,
        [](double sum, const ChartDataPoint& point) { return sum + point.y; });
    
    if (total_ == 0.0) return;
    
    // Calculate chart circle rect
    float margin = 50;
    float diameter = std::min(width(), height()) - 2 * margin;
    chartCircleRect_ = QRectF(
        (width() - diameter) / 2,
        (height() - diameter) / 2,
        diameter, diameter);
    
    outerRadius_ = diameter / 2;
    innerRadius_ = outerRadius_ * theme_.innerRadiusRatio;
    
    // Calculate segments
    double currentAngle = theme_.startAngle;
    
    for (size_t i = 0; i < data_.size(); ++i) {
        SegmentInfo segment;
        segment.value = data_[i].y;
        segment.percentage = (data_[i].y / total_) * 100.0;
        segment.color = data_[i].color;
        
        // Calculate angles
        segment.spanAngle = (data_[i].y / total_) * 360.0;
        
        // Apply spacing
        if (theme_.segmentSpacing > 0 && data_.size() > 1) {
            segment.spanAngle -= theme_.segmentSpacing;
        }
        
        segment.startAngle = currentAngle;
        currentAngle += segment.spanAngle + theme_.segmentSpacing;
        
        // Create segment path for hit testing
        segment.path = createSegmentPath(chartCircleRect_, segment.startAngle, 
                                       segment.spanAngle, theme_.innerRadiusRatio > 0);
        segment.boundingRect = segment.path.boundingRect();
        
        segments_.push_back(segment);
    }
}

QPointF CircularChart::getSegmentLabelPosition(const QRectF& rect, double angle, double radius) {
    double radian = angle * M_PI / 180.0;
    return rect.center() + QPointF(cos(radian) * radius, sin(radian) * radius);
}

QPainterPath CircularChart::createSegmentPath(const QRectF& rect, double startAngle, 
                                            double spanAngle, bool donut) {
    QPainterPath path;
    
    if (spanAngle == 0) return path;
    
    // Outer arc
    path.moveTo(rect.center());
    path.arcTo(rect, startAngle, spanAngle);
    
    if (donut && innerRadius_ > 0) {
        // Inner arc (reverse direction)
        QRectF innerRect = rect.adjusted(
            innerRadius_, innerRadius_, -innerRadius_, -innerRadius_);
        
        // Move to end of inner arc
        double endAngle = startAngle + spanAngle;
        double endRadian = endAngle * M_PI / 180.0;
        QPointF innerEnd = rect.center() + 
            QPointF(cos(endRadian) * innerRadius_, sin(endRadian) * innerRadius_);
        
        path.lineTo(innerEnd);
        path.arcTo(innerRect, endAngle, -spanAngle);
    }
    
    path.closeSubpath();
    return path;
}

double CircularChart::normalizeAngle(double angle) const {
    while (angle < 0) angle += 360;
    while (angle >= 360) angle -= 360;
    return angle;
}

double CircularChart::getAnimatedAngle(double targetAngle) {
    if (!effects_.animationEnabled || !theme_.animateRotation) {
        return targetAngle;
    }
    
    return previousRotationAngle_ + (targetAngle - previousRotationAngle_) * 
           animationState_.getEasedProgress();
}

float CircularChart::getSegmentScale(int index) const {
    float scale = 1.0f;
    
    if (hoveredSegment_ == index) {
        scale = theme_.hoverScale;
    }
    
    if (selectedSegment_ == index) {
        scale *= 1.05f;
    }
    
    // Animation scale
    if (effects_.animationEnabled && index < static_cast<int>(segmentAnimationProgress_.size())) {
        scale *= 0.8f + 0.2f * segmentAnimationProgress_[index];
    }
    
    return scale;
}

QPointF CircularChart::getSegmentOffset(int index) const {
    QPointF offset(0, 0);
    
    if (hoveredSegment_ == index || selectedSegment_ == index) {
        double midAngle = segments_[index].startAngle + segments_[index].spanAngle / 2;
        double radian = midAngle * M_PI / 180.0;
        
        float distance = hoveredSegment_ == index ? theme_.hoverOffset : 5.0f;
        offset = QPointF(cos(radian) * distance, sin(radian) * distance);
    }
    
    return offset;
}

bool CircularChart::isPointInSegment(const QPointF& point, int index) const {
    if (index < 0 || index >= static_cast<int>(segments_.size())) return false;
    
    // Check if point is in segment path
    return segments_[index].path.contains(point);
}

} // namespace llm_re::ui_v2::charts