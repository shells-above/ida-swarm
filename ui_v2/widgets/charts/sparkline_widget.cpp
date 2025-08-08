#include "../../core/ui_v2_common.h"
#include "sparkline_widget.h"
#include "../../core/theme_manager.h"
#include "../../core/color_constants.h"

namespace llm_re::ui_v2::charts {

SparklineWidget::SparklineWidget(QWidget* parent)
    : CustomChartBase(parent),
      sparklineType_(Line),
      maxDataPoints_(100),
      rollingWindow_(true),
      autoScale_(true),
      minValue_(0.0),
      maxValue_(1.0),
      calculatedMin_(0.0),
      calculatedMax_(1.0),
      showThresholds_(false),
      hasReferenceLine_(false),
      referenceLineValue_(0.0),
      bulletTarget_(0.0),
      bulletPerformance_(0.0),
      dataAnimationProgress_(1.0f),
      minIndex_(-1),
      maxIndex_(-1),
      needsRecalculation_(true) {
    
    // Set default theme
    theme_ = SparklineTheme(); // Use default initialization
    
    // Set default colors from ThemeManager
    auto& colors = ThemeManager::instance().colors();
    lineColor_ = colors.primary;
    fillColor_ = colors.primary;
    fillColor_.setAlphaF(theme_.areaOpacity);
    positiveColor_ = colors.success;
    negativeColor_ = colors.error;
    neutralColor_ = colors.textSecondary;
    targetColor_ = colors.warning;
    referenceLineColor_ = colors.border;
    
    // Sparklines are typically small
    setMinimumSize(60, 20);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    
    // Less margins for sparklines
    ChartMargins margins;
    margins.left = 2;
    margins.top = 2;
    margins.right = 2;
    margins.bottom = 2;
    setMargins(margins);
    
    // Disable most decorations by default
    xAxis_.visible = false;
    yAxis_.visible = false;
    xAxis_.showGrid = false;
    yAxis_.showGrid = false;
    legend_.visible = false;
}

SparklineWidget::~SparklineWidget() = default;

void SparklineWidget::setSparklineType(SparklineType type) {
    if (sparklineType_ != type) {
        sparklineType_ = type;
        update();
    }
}

void SparklineWidget::setData(const std::vector<double>& values) {
    previousData_ = std::deque<double>(data_.begin(), data_.end());
    data_.clear();
    
    for (double value : values) {
        data_.push_back(value);
    }
    
    // Limit to max data points
    while (data_.size() > static_cast<size_t>(maxDataPoints_)) {
        data_.pop_front();
    }
    
    needsRecalculation_ = true;
    updateValueRange();
    
    if (effects_.animationEnabled && theme_.animateOnUpdate) {
        dataAnimationProgress_ = 0.0f;
        startAnimation();
    }
    
    emit dataChanged();
    update();
}

void SparklineWidget::appendValue(double value) {
    previousData_ = std::deque<double>(data_.begin(), data_.end());
    data_.push_back(value);
    
    if (rollingWindow_ && data_.size() > static_cast<size_t>(maxDataPoints_)) {
        data_.pop_front();
    }
    
    needsRecalculation_ = true;
    updateValueRange();
    
    if (effects_.animationEnabled && theme_.animateOnUpdate) {
        dataAnimationProgress_ = 0.0f;
        startAnimation();
    }
    
    emit valueAdded(value);
    emit dataChanged();
    update();
}

void SparklineWidget::prependValue(double value) {
    previousData_ = std::deque<double>(data_.begin(), data_.end());
    data_.push_front(value);
    
    if (rollingWindow_ && data_.size() > static_cast<size_t>(maxDataPoints_)) {
        data_.pop_back();
    }
    
    needsRecalculation_ = true;
    updateValueRange();
    
    if (effects_.animationEnabled && theme_.animateOnUpdate) {
        dataAnimationProgress_ = 0.0f;
        startAnimation();
    }
    
    emit valueAdded(value);
    emit dataChanged();
    update();
}

void SparklineWidget::clearData() {
    previousData_.clear();
    data_.clear();
    minIndex_ = -1;
    maxIndex_ = -1;
    needsRecalculation_ = true;
    
    emit dataChanged();
    update();
}

void SparklineWidget::setMaxDataPoints(int max) {
    maxDataPoints_ = max;
    
    // Trim data if necessary
    while (data_.size() > static_cast<size_t>(maxDataPoints_)) {
        data_.pop_front();
    }
    
    needsRecalculation_ = true;
    update();
}

void SparklineWidget::setRollingWindow(bool enabled) {
    rollingWindow_ = enabled;
    update();
}

void SparklineWidget::setTheme(const SparklineTheme& theme) {
    theme_ = theme;
    update();
}

void SparklineWidget::setLineWidth(float width) {
    theme_.lineWidth = width;
    update();
}

void SparklineWidget::setFillArea(bool fill) {
    theme_.fillArea = fill;
    update();
}

void SparklineWidget::setAreaOpacity(float opacity) {
    theme_.areaOpacity = opacity;
    update();
}

void SparklineWidget::setShowMinMax(bool show) {
    theme_.showMinMax = show;
    update();
}

void SparklineWidget::setShowLastValue(bool show) {
    theme_.showLastValue = show;
    update();
}

void SparklineWidget::setShowThresholds(bool show) {
    showThresholds_ = show;
    update();
}

void SparklineWidget::setAutoScale(bool enabled) {
    autoScale_ = enabled;
    if (enabled) {
        updateValueRange();
    }
}

void SparklineWidget::setValueRange(double min, double max) {
    minValue_ = min;
    maxValue_ = max;
    autoScale_ = false;
    needsRecalculation_ = true;
    update();
}

void SparklineWidget::getValueRange(double& min, double& max) const {
    if (autoScale_) {
        min = calculatedMin_;
        max = calculatedMax_;
    } else {
        min = minValue_;
        max = maxValue_;
    }
}

void SparklineWidget::addThreshold(double value, const QColor& color, const QString& label) {
    thresholds_.push_back({value, color, label});
    update();
}

void SparklineWidget::removeThreshold(double value) {
    thresholds_.erase(
        std::remove_if(thresholds_.begin(), thresholds_.end(),
                      [value](const Threshold& t) { return t.value == value; }),
        thresholds_.end()
    );
    update();
}

void SparklineWidget::clearThresholds() {
    thresholds_.clear();
    update();
}

void SparklineWidget::addBand(double min, double max, const QColor& color, const QString& label) {
    bands_.push_back({min, max, color, label});
    update();
}

void SparklineWidget::removeBand(int index) {
    if (index >= 0 && index < static_cast<int>(bands_.size())) {
        bands_.erase(bands_.begin() + index);
        update();
    }
}

void SparklineWidget::clearBands() {
    bands_.clear();
    update();
}

void SparklineWidget::setReferenceLine(double value) {
    hasReferenceLine_ = true;
    referenceLineValue_ = value;
    update();
}

void SparklineWidget::clearReferenceLine() {
    hasReferenceLine_ = false;
    update();
}

void SparklineWidget::setBulletTarget(double target) {
    bulletTarget_ = target;
    if (sparklineType_ == Bullet) {
        update();
    }
}

void SparklineWidget::setBulletPerformance(double performance) {
    bulletPerformance_ = performance;
    if (sparklineType_ == Bullet) {
        update();
    }
}

void SparklineWidget::setBulletRanges(const std::vector<std::pair<double, QColor>>& ranges) {
    bulletRanges_ = ranges;
    if (sparklineType_ == Bullet) {
        update();
    }
}

void SparklineWidget::setLineColor(const QColor& color) {
    lineColor_ = color;
    update();
}

void SparklineWidget::setFillColor(const QColor& color) {
    fillColor_ = color;
    update();
}

void SparklineWidget::setPositiveColor(const QColor& color) {
    positiveColor_ = color;
    update();
}

void SparklineWidget::setNegativeColor(const QColor& color) {
    negativeColor_ = color;
    update();
}

void SparklineWidget::setAnimateOnUpdate(bool animate) {
    theme_.animateOnUpdate = animate;
}

QSize SparklineWidget::minimumSizeHint() const {
    return QSize(60, 20);
}

QSize SparklineWidget::sizeHint() const {
    return QSize(150, 30);
}

void SparklineWidget::updateData() {
    needsRecalculation_ = true;
    updateValueRange();
    CustomChartBase::updateData();
}

double SparklineWidget::minimum() const {
    if (data_.empty()) return 0.0;
    return *std::min_element(data_.begin(), data_.end());
}

double SparklineWidget::maximum() const {
    if (data_.empty()) return 0.0;
    return *std::max_element(data_.begin(), data_.end());
}

double SparklineWidget::average() const {
    if (data_.empty()) return 0.0;
    double sum = std::accumulate(data_.begin(), data_.end(), 0.0);
    return sum / data_.size();
}

double SparklineWidget::lastValue() const {
    return data_.empty() ? 0.0 : data_.back();
}

void SparklineWidget::drawData(QPainter* painter) {
    if (data_.empty()) return;
    
    // Draw bands first (background)
    drawBands(painter);
    
    // Draw main sparkline
    switch (sparklineType_) {
        case Line:
            drawLineSparkline(painter);
            break;
        case Area:
            drawAreaSparkline(painter);
            break;
        case Bar:
            drawBarSparkline(painter);
            break;
        case WinLoss:
            drawWinLossSparkline(painter);
            break;
        case Discrete:
            drawDiscreteSparkline(painter);
            break;
        case Bullet:
            drawBulletChart(painter);
            break;
    }
    
    // Draw overlays
    if (hasReferenceLine_) {
        drawReferenceLine(painter);
    }
    
    if (showThresholds_) {
        drawThresholds(painter);
    }
    
    if (theme_.showMinMax) {
        drawMinMaxMarkers(painter);
    }
    
    if (theme_.showLastValue) {
        drawLastValueLabel(painter);
    }
}

int SparklineWidget::findNearestDataPoint(const QPointF& pos, int& seriesIndex) {
    seriesIndex = 0; // Sparklines have only one series
    
    if (data_.empty()) return -1;
    
    std::vector<QPointF> points = calculatePoints();
    
    double minDistance = std::numeric_limits<double>::max();
    int nearestIndex = -1;
    
    for (size_t i = 0; i < points.size(); ++i) {
        double distance = QLineF(pos, points[i]).length();
        if (distance < minDistance) {
            minDistance = distance;
            nearestIndex = i;
        }
    }
    
    return (minDistance < 10.0) ? nearestIndex : -1;
}

void SparklineWidget::paintEvent(QPaintEvent* event) {
    // Update animation
    if (effects_.animationEnabled && dataAnimationProgress_ < 1.0f) {
        dataAnimationProgress_ = std::min(1.0f, dataAnimationProgress_ + 0.05f);
        QTimer::singleShot(16, this, QOverload<>::of(&SparklineWidget::update));
    }
    
    CustomChartBase::paintEvent(event);
}

void SparklineWidget::resizeEvent(QResizeEvent* event) {
    needsRecalculation_ = true;
    CustomChartBase::resizeEvent(event);
}

void SparklineWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked(event->pos());
    }
    CustomChartBase::mousePressEvent(event);
}

void SparklineWidget::drawLineSparkline(QPainter* painter) {
    std::vector<QPointF> points = calculatePoints();
    if (points.size() < 2) return;
    
    std::vector<double> animatedValues = getAnimatedValues();
    
    // Update points with animated values
    for (size_t i = 0; i < points.size() && i < animatedValues.size(); ++i) {
        points[i].setY(valueToPoint(i, animatedValues[i]).y());
    }
    
    painter->save();
    
    // Draw area fill if enabled
    if (theme_.fillArea) {
        QPainterPath areaPath;
        areaPath.moveTo(points[0].x(), chartRect_.bottom());
        
        for (const auto& point : points) {
            areaPath.lineTo(point);
        }
        
        areaPath.lineTo(points.back().x(), chartRect_.bottom());
        areaPath.closeSubpath();
        
        QColor areaColor = fillColor_;
        areaColor.setAlpha(theme_.areaOpacity * 255);
        painter->fillPath(areaPath, areaColor);
    }
    
    // Draw line
    QPen linePen(lineColor_, theme_.lineWidth);
    linePen.setCapStyle(Qt::RoundCap);
    linePen.setJoinStyle(Qt::RoundJoin);
    
    if (effects_.glowEnabled) {
        // Draw glow effect
        for (int i = 3; i > 0; --i) {
            QPen glowPen = linePen;
            glowPen.setWidth(theme_.lineWidth + i * 2);
            QColor glowColor = lineColor_;
            glowColor.setAlpha(30 / i);
            glowPen.setColor(glowColor);
            painter->setPen(glowPen);
            painter->drawPolyline(points.data(), points.size());
        }
    }
    
    painter->setPen(linePen);
    painter->drawPolyline(points.data(), points.size());
    
    painter->restore();
}

void SparklineWidget::drawAreaSparkline(QPainter* painter) {
    // Area sparkline is just a line sparkline with fill forced on
    bool oldFillArea = theme_.fillArea;
    theme_.fillArea = true;
    drawLineSparkline(painter);
    theme_.fillArea = oldFillArea;
}

void SparklineWidget::drawBarSparkline(QPainter* painter) {
    if (data_.empty()) return;
    
    std::vector<double> animatedValues = getAnimatedValues();
    double barWidth = chartRect_.width() / animatedValues.size();
    
    painter->save();
    
    double min, max;
    getValueRange(min, max);
    double zeroY = chartRect_.bottom() - normalizeValue(0) * chartRect_.height();
    
    for (size_t i = 0; i < animatedValues.size(); ++i) {
        double value = animatedValues[i];
        QPointF point = valueToPoint(i, value);
        
        QRectF barRect;
        if (value >= 0) {
            barRect = QRectF(
                point.x() - barWidth * 0.4,
                point.y(),
                barWidth * 0.8,
                zeroY - point.y()
            );
        } else {
            barRect = QRectF(
                point.x() - barWidth * 0.4,
                zeroY,
                barWidth * 0.8,
                point.y() - zeroY
            );
        }
        
        QColor barColor = value >= 0 ? positiveColor_ : negativeColor_;
        
        // Draw with rounded corners if space allows
        if (barRect.width() > 4 && barRect.height() > 4) {
            QPainterPath barPath;
            barPath.addRoundedRect(barRect, 2, 2);
            painter->fillPath(barPath, barColor);
        } else {
            painter->fillRect(barRect, barColor);
        }
    }
    
    painter->restore();
}

void SparklineWidget::drawWinLossSparkline(QPainter* painter) {
    if (data_.empty()) return;
    
    double barWidth = chartRect_.width() / data_.size();
    double midY = chartRect_.center().y();
    double barHeight = chartRect_.height() * 0.4;
    
    painter->save();
    
    for (size_t i = 0; i < data_.size(); ++i) {
        double value = data_[i];
        
        QRectF barRect;
        QColor barColor;
        
        if (value > 0) {
            barRect = QRectF(
                chartRect_.left() + i * barWidth + barWidth * 0.1,
                midY - barHeight,
                barWidth * 0.8,
                barHeight
            );
            barColor = positiveColor_;
        } else if (value < 0) {
            barRect = QRectF(
                chartRect_.left() + i * barWidth + barWidth * 0.1,
                midY,
                barWidth * 0.8,
                barHeight
            );
            barColor = negativeColor_;
        } else {
            // Draw neutral line for zero values
            barRect = QRectF(
                chartRect_.left() + i * barWidth + barWidth * 0.1,
                midY - 1,
                barWidth * 0.8,
                2
            );
            barColor = neutralColor_;
        }
        
        painter->fillRect(barRect, barColor);
    }
    
    painter->restore();
}

void SparklineWidget::drawDiscreteSparkline(QPainter* painter) {
    std::vector<QPointF> points = calculatePoints();
    if (points.empty()) return;
    
    painter->save();
    
    // Draw dots
    painter->setPen(Qt::NoPen);
    painter->setBrush(lineColor_);
    
    for (size_t i = 0; i < points.size(); ++i) {
        double radius = 2.0;
        
        // Highlight min/max
        if (theme_.showMinMax) {
            if (static_cast<int>(i) == minIndex_) {
                painter->setBrush(negativeColor_);
                radius = 3.0;
            } else if (static_cast<int>(i) == maxIndex_) {
                painter->setBrush(positiveColor_);
                radius = 3.0;
            } else {
                painter->setBrush(lineColor_);
            }
        }
        
        painter->drawEllipse(points[i], radius, radius);
    }
    
    painter->restore();
}

void SparklineWidget::drawBulletChart(QPainter* painter) {
    painter->save();
    
    QRectF bulletRect = chartRect_;
    double bulletHeight = bulletRect.height() * 0.6;
    bulletRect.setTop(bulletRect.center().y() - bulletHeight / 2);
    bulletRect.setHeight(bulletHeight);
    
    // Draw ranges
    double lastX = bulletRect.left();
    for (const auto& range : bulletRanges_) {
        double rangeWidth = (range.first / maxValue_) * bulletRect.width();
        QRectF rangeRect(lastX, bulletRect.top(), rangeWidth, bulletRect.height());
        
        QColor rangeColor = range.second;
        rangeColor.setAlpha(100);
        painter->fillRect(rangeRect, rangeColor);
        
        lastX += rangeWidth;
    }
    
    // Draw performance bar
    double perfWidth = (bulletPerformance_ / maxValue_) * bulletRect.width();
    QRectF perfRect(bulletRect.left(), 
                   bulletRect.top() + bulletRect.height() * 0.25,
                   perfWidth,
                   bulletRect.height() * 0.5);
    
    painter->fillRect(perfRect, lineColor_);
    
    // Draw target line
    double targetX = bulletRect.left() + (bulletTarget_ / maxValue_) * bulletRect.width();
    painter->setPen(QPen(targetColor_, 3));
    painter->drawLine(QPointF(targetX, bulletRect.top() - 5),
                     QPointF(targetX, bulletRect.bottom() + 5));
    
    painter->restore();
}

void SparklineWidget::drawMinMaxMarkers(QPainter* painter) {
    if (data_.size() < 2) return;
    
    std::vector<QPointF> points = calculatePoints();
    
    // Update min/max indices
    auto minIt = std::min_element(data_.begin(), data_.end());
    auto maxIt = std::max_element(data_.begin(), data_.end());
    minIndex_ = std::distance(data_.begin(), minIt);
    maxIndex_ = std::distance(data_.begin(), maxIt);
    
    painter->save();
    
    // Draw min marker
    if (minIndex_ >= 0 && minIndex_ < static_cast<int>(points.size())) {
        painter->setPen(QPen(negativeColor_, 2));
        painter->setBrush(negativeColor_);
        painter->drawEllipse(points[minIndex_], 3, 3);
        
        if (chartRect_.height() > 40) {
            QFont smallFont = font();
            smallFont.setPointSize(8);
            painter->setFont(smallFont);
            painter->drawText(points[minIndex_] + QPointF(5, -5), 
                            QString::number(*minIt, 'f', 1));
        }
    }
    
    // Draw max marker
    if (maxIndex_ >= 0 && maxIndex_ < static_cast<int>(points.size())) {
        painter->setPen(QPen(positiveColor_, 2));
        painter->setBrush(positiveColor_);
        painter->drawEllipse(points[maxIndex_], 3, 3);
        
        if (chartRect_.height() > 40) {
            QFont smallFont = font();
            smallFont.setPointSize(8);
            painter->setFont(smallFont);
            painter->drawText(points[maxIndex_] + QPointF(5, 15), 
                            QString::number(*maxIt, 'f', 1));
        }
    }
    
    painter->restore();
}

void SparklineWidget::drawLastValueLabel(QPainter* painter) {
    if (data_.empty()) return;
    
    painter->save();
    
    double lastVal = data_.back();
    QString label = QString::number(lastVal, 'f', valuePrecision_);
    
    QFont labelFont = font();
    labelFont.setPointSize(valueFontSize_);
    labelFont.setBold(true);
    painter->setFont(labelFont);
    
    QFontMetrics fm(labelFont);
    QRectF textRect = fm.boundingRect(label);
    
    // Position at the end of the sparkline
    QPointF lastPoint = calculatePoints().back();
    QPointF textPos(chartRect_.right() + 5, 
                   lastPoint.y() + textRect.height() / 2);
    
    // Draw background for readability
    QRectF bgRect = textRect.translated(textPos - QPointF(0, textRect.height()));
    bgRect.adjust(-2, -1, 2, 1);
    painter->fillRect(bgRect, ThemeManager::instance().colors().chartTooltipBg);
    
    painter->setPen(lastVal >= 0 ? positiveColor_ : negativeColor_);
    painter->drawText(textPos, label);
    
    painter->restore();
}

void SparklineWidget::drawThresholds(QPainter* painter) {
    painter->save();
    
    for (const auto& threshold : thresholds_) {
        double y = valueToPoint(0, threshold.value).y();
        
        if (y >= chartRect_.top() && y <= chartRect_.bottom()) {
            QPen thresholdPen(threshold.color, 1, Qt::DashLine);
            painter->setPen(thresholdPen);
            painter->drawLine(QPointF(chartRect_.left(), y),
                            QPointF(chartRect_.right(), y));
            
            // Draw label if there's space
            if (!threshold.label.isEmpty() && chartRect_.height() > 30) {
                QFont smallFont = font();
                smallFont.setPointSize(8);
                painter->setFont(smallFont);
                painter->setPen(threshold.color);
                painter->drawText(QPointF(chartRect_.left(), y - 2), threshold.label);
            }
        }
    }
    
    painter->restore();
}

void SparklineWidget::drawBands(QPainter* painter) {
    painter->save();
    
    for (const auto& band : bands_) {
        double topY = valueToPoint(0, band.max).y();
        double bottomY = valueToPoint(0, band.min).y();
        
        QRectF bandRect(chartRect_.left(), topY,
                       chartRect_.width(), bottomY - topY);
        
        QColor bandColor = band.color;
        bandColor.setAlpha(30);
        painter->fillRect(bandRect, bandColor);
    }
    
    painter->restore();
}

void SparklineWidget::drawReferenceLine(QPainter* painter) {
    double y = valueToPoint(0, referenceLineValue_).y();
    
    if (y >= chartRect_.top() && y <= chartRect_.bottom()) {
        painter->save();
        
        QPen refPen(referenceLineColor_, 1, Qt::DotLine);
        painter->setPen(refPen);
        painter->drawLine(QPointF(chartRect_.left(), y),
                        QPointF(chartRect_.right(), y));
        
        painter->restore();
    }
}

void SparklineWidget::updateValueRange() {
    if (data_.empty()) {
        calculatedMin_ = 0.0;
        calculatedMax_ = 1.0;
        return;
    }
    
    if (autoScale_) {
        calculatedMin_ = *std::min_element(data_.begin(), data_.end());
        calculatedMax_ = *std::max_element(data_.begin(), data_.end());
        
        // Add some padding
        double range = calculatedMax_ - calculatedMin_;
        if (range == 0) {
            calculatedMin_ -= 0.5;
            calculatedMax_ += 0.5;
        } else {
            calculatedMin_ -= range * 0.1;
            calculatedMax_ += range * 0.1;
        }
        
        // Include zero if close
        if (calculatedMin_ > 0 && calculatedMin_ < calculatedMax_ * 0.2) {
            calculatedMin_ = 0;
        }
        if (calculatedMax_ < 0 && calculatedMax_ > calculatedMin_ * 0.2) {
            calculatedMax_ = 0;
        }
    } else {
        calculatedMin_ = minValue_;
        calculatedMax_ = maxValue_;
    }
}

std::vector<QPointF> SparklineWidget::calculatePoints() const {
    std::vector<QPointF> points;
    
    if (data_.empty()) return points;
    
    points.reserve(data_.size());
    
    for (size_t i = 0; i < data_.size(); ++i) {
        points.push_back(valueToPoint(i, data_[i]));
    }
    
    return points;
}

QPointF SparklineWidget::valueToPoint(int index, double value) const {
    double x = chartRect_.left() + 
               (index * chartRect_.width() / std::max(1, static_cast<int>(data_.size()) - 1));
    
    if (data_.size() == 1) {
        x = chartRect_.center().x();
    }
    
    double y = chartRect_.bottom() - normalizeValue(value) * chartRect_.height();
    
    return QPointF(x, y);
}

double SparklineWidget::normalizeValue(double value) const {
    double min, max;
    getValueRange(min, max);
    
    if (max == min) return 0.5;
    
    return (value - min) / (max - min);
}

std::vector<double> SparklineWidget::getAnimatedValues() const {
    if (!effects_.animationEnabled || dataAnimationProgress_ >= 1.0f) {
        return std::vector<double>(data_.begin(), data_.end());
    }
    
    std::vector<double> result;
    result.reserve(data_.size());
    
    for (size_t i = 0; i < data_.size(); ++i) {
        double currentValue = data_[i];
        double previousValue = (i < previousData_.size()) ? previousData_[i] : currentValue;
        
        double animatedValue = previousValue + 
                              (currentValue - previousValue) * dataAnimationProgress_;
        result.push_back(animatedValue);
    }
    
    return result;
}

// InlineSparkline implementation
InlineSparkline::InlineSparkline(QWidget* parent)
    : SparklineWidget(parent) {
    setCompactMode(true);
}

void InlineSparkline::setCompactMode(bool compact) {
    compactMode_ = compact;
    
    if (compact) {
        ChartMargins compactMargins;
        compactMargins.left = 1;
        compactMargins.top = 1;
        compactMargins.right = 1;
        compactMargins.bottom = 1;
        setMargins(compactMargins);
        setShowMinMax(false);
        setShowLastValue(false);
        setMinimumSize(40, 16);
        setMaximumHeight(20);
    } else {
        ChartMargins normalMargins;
        normalMargins.left = 2;
        normalMargins.top = 2;
        normalMargins.right = 2;
        normalMargins.bottom = 2;
        setMargins(normalMargins);
        setMinimumSize(60, 20);
        setMaximumHeight(30);
    }
}

void InlineSparkline::setupAsMetric(const QString& label, const QString& suffix) {
    label_ = label;
    suffix_ = suffix;
    setShowLastValue(true);
    setSparklineType(Line);
    setFillArea(true);
}

void InlineSparkline::setupAsProgress(double min, double max, double target) {
    setSparklineType(Bullet);
    setValueRange(min, max);
    setBulletTarget(target);
}

void InlineSparkline::setupAsTrend(int dataPoints) {
    setMaxDataPoints(dataPoints);
    setSparklineType(Area);
    setAutoScale(true);
}

void InlineSparkline::paintEvent(QPaintEvent* event) {
    SparklineWidget::paintEvent(event);
    
    if (!label_.isEmpty() && !compactMode_) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        
        QFont labelFont = font();
        labelFont.setPointSize(8);
        painter.setFont(labelFont);
        
        const auto& colors = ThemeManager::instance().colors();
        painter.setPen(colors.textPrimary);
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, label_);
        
        if (!suffix_.isEmpty() && dataPointCount() > 0) {
            QString value = QString::number(lastValue(), 'f', 1) + suffix_;
            painter.drawText(rect(), Qt::AlignRight | Qt::AlignVCenter, value);
        }
    }
}

} // namespace llm_re::ui_v2::charts