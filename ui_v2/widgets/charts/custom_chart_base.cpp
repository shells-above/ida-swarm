#include "custom_chart_base.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QGraphicsBlurEffect>
#include <cmath>

namespace llm_re::ui_v2::charts {

// Animation easing functions
float AnimationState::getEasedProgress() const {
    switch (type) {
        case AnimationType::Linear:
            return progress;
            
        case AnimationType::EaseIn:
            return progress * progress;
            
        case AnimationType::EaseOut:
            return 1.0f - (1.0f - progress) * (1.0f - progress);
            
        case AnimationType::EaseInOut:
            if (progress < 0.5f) {
                return 2.0f * progress * progress;
            } else {
                return 1.0f - 2.0f * (1.0f - progress) * (1.0f - progress);
            }
            
        case AnimationType::Bounce: {
            const float p = progress;
            if (p < 0.36364f) {
                return 7.5625f * p * p;
            } else if (p < 0.72727f) {
                float p2 = p - 0.54545f;
                return 7.5625f * p2 * p2 + 0.75f;
            } else if (p < 0.90909f) {
                float p2 = p - 0.81818f;
                return 7.5625f * p2 * p2 + 0.9375f;
            } else {
                float p2 = p - 0.95455f;
                return 7.5625f * p2 * p2 + 0.98438f;
            }
        }
            
        case AnimationType::Elastic: {
            if (progress == 0 || progress == 1) return progress;
            const float p = progress - 1.0f;
            return -std::pow(2.0f, 10.0f * p) * std::sin((p - 0.075f) * 2.0f * M_PI / 0.3f);
        }
            
        case AnimationType::Back: {
            const float c1 = 1.70158f;
            const float c3 = c1 + 1.0f;
            return 1.0f + c3 * std::pow(progress - 1.0f, 3.0f) + c1 * std::pow(progress - 1.0f, 2.0f);
        }
            
        default:
            return progress;
    }
}

// Chart utilities implementation
namespace ChartUtils {

double valueToPixel(double value, double min, double max, double pixelRange, bool invert) {
    if (max == min) return pixelRange / 2.0;
    double normalized = (value - min) / (max - min);
    if (invert) normalized = 1.0 - normalized;
    return normalized * pixelRange;
}

double pixelToValue(double pixel, double min, double max, double pixelRange, bool invert) {
    if (pixelRange == 0) return min;
    double normalized = pixel / pixelRange;
    if (invert) normalized = 1.0 - normalized;
    return min + normalized * (max - min);
}

QString formatValue(double value, const QString& format) {
    if (!format.isEmpty()) {
        return QString::asprintf(format.toStdString().c_str(), value);
    }
    
    // Auto format based on magnitude
    if (std::abs(value) >= 1000000) {
        return QString::number(value / 1000000.0, 'f', 1) + "M";
    } else if (std::abs(value) >= 1000) {
        return QString::number(value / 1000.0, 'f', 1) + "K";
    } else if (std::abs(value) < 0.01 && value != 0) {
        return QString::number(value, 'e', 2);
    } else {
        return QString::number(value, 'f', 2);
    }
}

QString formatDateTime(const QDateTime& dt, const QString& format) {
    return dt.toString(format.isEmpty() ? "MMM dd hh:mm" : format);
}

void calculateNiceScale(double min, double max, double& niceMin, double& niceMax, double& tickInterval) {
    double range = max - min;
    if (range == 0) {
        niceMin = min - 1;
        niceMax = max + 1;
        tickInterval = 0.5;
        return;
    }
    
    // Calculate order of magnitude
    double magnitude = std::pow(10, std::floor(std::log10(range)));
    double normalizedRange = range / magnitude;
    
    // Determine nice tick interval
    if (normalizedRange <= 1.5) {
        tickInterval = 0.2 * magnitude;
    } else if (normalizedRange <= 3) {
        tickInterval = 0.5 * magnitude;
    } else if (normalizedRange <= 7) {
        tickInterval = 1.0 * magnitude;
    } else {
        tickInterval = 2.0 * magnitude;
    }
    
    // Calculate nice min and max
    niceMin = std::floor(min / tickInterval) * tickInterval;
    niceMax = std::ceil(max / tickInterval) * tickInterval;
}

double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

QPointF lerp(const QPointF& a, const QPointF& b, double t) {
    return QPointF(lerp(a.x(), b.x(), t), lerp(a.y(), b.y(), t));
}

QColor lerp(const QColor& a, const QColor& b, double t) {
    return QColor(
        static_cast<int>(lerp(a.red(), b.red(), t)),
        static_cast<int>(lerp(a.green(), b.green(), t)),
        static_cast<int>(lerp(a.blue(), b.blue(), t)),
        static_cast<int>(lerp(a.alpha(), b.alpha(), t))
    );
}

QPointF calculateBezierPoint(const QPointF& p0, const QPointF& p1, 
                           const QPointF& p2, const QPointF& p3, double t) {
    double t2 = t * t;
    double t3 = t2 * t;
    double mt = 1 - t;
    double mt2 = mt * mt;
    double mt3 = mt2 * mt;
    
    double x = mt3 * p0.x() + 3 * mt2 * t * p1.x() + 3 * mt * t2 * p2.x() + t3 * p3.x();
    double y = mt3 * p0.y() + 3 * mt2 * t * p1.y() + 3 * mt * t2 * p2.y() + t3 * p3.y();
    
    return QPointF(x, y);
}

std::vector<QPointF> generateSmoothCurve(const std::vector<QPointF>& points, int segments) {
    if (points.size() < 2) return points;
    if (points.size() == 2) return points;
    
    std::vector<QPointF> smoothPoints;
    
    for (size_t i = 0; i < points.size() - 1; ++i) {
        QPointF p0 = (i > 0) ? points[i-1] : points[i];
        QPointF p1 = points[i];
        QPointF p2 = points[i+1];
        QPointF p3 = (i < points.size() - 2) ? points[i+2] : points[i+1];
        
        // Calculate control points
        QPointF cp1 = p1 + (p2 - p0) * 0.25;
        QPointF cp2 = p2 - (p3 - p1) * 0.25;
        
        // Generate bezier curve segments
        for (int j = 0; j < segments; ++j) {
            double t = static_cast<double>(j) / segments;
            smoothPoints.push_back(calculateBezierPoint(p1, cp1, cp2, p2, t));
        }
    }
    
    smoothPoints.push_back(points.back());
    return smoothPoints;
}

bool pointInCircle(const QPointF& point, const QPointF& center, double radius) {
    double dx = point.x() - center.x();
    double dy = point.y() - center.y();
    return (dx * dx + dy * dy) <= (radius * radius);
}

bool pointNearLine(const QPointF& point, const QPointF& lineStart, const QPointF& lineEnd, double threshold) {
    double A = point.x() - lineStart.x();
    double B = point.y() - lineStart.y();
    double C = lineEnd.x() - lineStart.x();
    double D = lineEnd.y() - lineStart.y();
    
    double dot = A * C + B * D;
    double lenSq = C * C + D * D;
    double param = -1;
    
    if (lenSq != 0) {
        param = dot / lenSq;
    }
    
    double xx, yy;
    
    if (param < 0) {
        xx = lineStart.x();
        yy = lineStart.y();
    } else if (param > 1) {
        xx = lineEnd.x();
        yy = lineEnd.y();
    } else {
        xx = lineStart.x() + param * C;
        yy = lineStart.y() + param * D;
    }
    
    double dx = point.x() - xx;
    double dy = point.y() - yy;
    return std::sqrt(dx * dx + dy * dy) <= threshold;
}

void drawGlowEffect(QPainter* painter, const QPainterPath& path, const QColor& glowColor, float radius) {
    painter->save();
    
    // Draw multiple layers of glow
    for (int i = 5; i > 0; --i) {
        QColor color = glowColor;
        color.setAlphaF(0.1f * (6 - i) / 5.0f);
        
        QPen pen(color);
        pen.setWidthF(radius * i / 5.0f);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        
        painter->setPen(pen);
        painter->drawPath(path);
    }
    
    painter->restore();
}

void drawShadow(QPainter* painter, const QPainterPath& path, const EffectsConfig& effects) {
    if (!effects.shadowEnabled) return;
    
    painter->save();
    
    // Create shadow by drawing the path multiple times with offset and blur
    QColor shadowColor = effects.shadowColor;
    int steps = static_cast<int>(effects.shadowBlur);
    
    for (int i = steps; i > 0; --i) {
        float factor = static_cast<float>(i) / steps;
        shadowColor.setAlphaF(shadowColor.alphaF() * factor * 0.5f);
        
        painter->translate(effects.shadowOffsetX * factor / steps, 
                         effects.shadowOffsetY * factor / steps);
        
        QPen pen(shadowColor);
        pen.setWidthF(effects.shadowBlur * factor);
        painter->setPen(pen);
        painter->fillPath(path, shadowColor);
    }
    
    painter->restore();
}

void drawGlassMorphism(QPainter* painter, const QRectF& rect, const EffectsConfig& effects) {
    if (!effects.glassMorphism) return;
    
    painter->save();
    
    // Draw blurred background
    QColor glassColor(255, 255, 255, static_cast<int>(effects.glassOpacity * 50));
    painter->fillRect(rect, glassColor);
    
    // Draw glass border
    QPen borderPen(QColor(255, 255, 255, 100));
    borderPen.setWidthF(1.0f);
    painter->setPen(borderPen);
    painter->drawRoundedRect(rect, 8, 8);
    
    // Draw glass gradient
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    gradient.setColorAt(0, QColor(255, 255, 255, 30));
    gradient.setColorAt(1, QColor(255, 255, 255, 10));
    painter->fillRect(rect, gradient);
    
    painter->restore();
}

} // namespace ChartUtils

// Color palette implementation
const std::vector<QColor>& ColorPalette::getDefaultPalette() {
    static std::vector<QColor> palette = {
        QColor(59, 130, 246),   // Blue
        QColor(16, 185, 129),   // Green
        QColor(251, 146, 60),   // Orange
        QColor(244, 63, 94),    // Red
        QColor(147, 51, 234),   // Purple
        QColor(250, 204, 21),   // Yellow
        QColor(14, 165, 233),   // Sky
        QColor(236, 72, 153),   // Pink
        QColor(34, 197, 94),    // Emerald
        QColor(168, 85, 247)    // Violet
    };
    return palette;
}

const std::vector<QColor>& ColorPalette::getVibrantPalette() {
    static std::vector<QColor> palette = {
        QColor(255, 0, 127),    // Hot pink
        QColor(0, 255, 255),    // Cyan
        QColor(255, 255, 0),    // Yellow
        QColor(0, 255, 127),    // Spring green
        QColor(255, 0, 255),    // Magenta
        QColor(127, 0, 255),    // Blue violet
        QColor(255, 127, 0),    // Orange
        QColor(0, 127, 255),    // Sky blue
        QColor(127, 255, 0),    // Chartreuse
        QColor(255, 0, 0)       // Red
    };
    return palette;
}

const std::vector<QColor>& ColorPalette::getPastelPalette() {
    static std::vector<QColor> palette = {
        QColor(199, 210, 254),  // Lavender
        QColor(254, 202, 202),  // Pink
        QColor(254, 249, 195),  // Cream
        QColor(209, 250, 229),  // Mint
        QColor(254, 226, 226),  // Blush
        QColor(221, 214, 254),  // Lilac
        QColor(254, 215, 170),  // Peach
        QColor(187, 247, 208),  // Seafoam
        QColor(251, 207, 232),  // Rose
        QColor(190, 227, 219)   // Teal
    };
    return palette;
}

const std::vector<QColor>& ColorPalette::getMonochromaticPalette(const QColor& base) {
    static std::vector<QColor> palette;
    palette.clear();
    
    for (int i = 0; i < 10; ++i) {
        float factor = 0.3f + (i * 0.7f / 9.0f);
        palette.push_back(QColor(
            static_cast<int>(base.red() * factor),
            static_cast<int>(base.green() * factor),
            static_cast<int>(base.blue() * factor)
        ));
    }
    
    return palette;
}

QColor ColorPalette::getColorAt(int index, const std::vector<QColor>& palette) {
    if (palette.empty()) return Qt::black;
    return palette[index % palette.size()];
}

QLinearGradient ColorPalette::createGradient(const QColor& start, const QColor& end, 
                                            const QRectF& rect, bool vertical) {
    QLinearGradient gradient;
    if (vertical) {
        gradient = QLinearGradient(rect.topLeft(), rect.bottomLeft());
    } else {
        gradient = QLinearGradient(rect.topLeft(), rect.topRight());
    }
    gradient.setColorAt(0, start);
    gradient.setColorAt(1, end);
    return gradient;
}

QRadialGradient ColorPalette::createRadialGradient(const QColor& center, const QColor& edge,
                                                  const QPointF& centerPoint, float radius) {
    QRadialGradient gradient(centerPoint, radius);
    gradient.setColorAt(0, center);
    gradient.setColorAt(1, edge);
    return gradient;
}

// CustomChartBase implementation
CustomChartBase::CustomChartBase(QWidget* parent)
    : BaseStyledWidget(parent) {
    
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    
    // Initialize tooltip timer
    tooltipTimer_ = new QTimer(this);
    tooltipTimer_->setSingleShot(true);
    tooltipTimer_->setInterval(500);
    connect(tooltipTimer_, &QTimer::timeout, [this]() {
        showTooltip_ = true;
        update();
    });
    
    // Initialize animation
    initializeAnimation();
    
    // Set default colors based on theme
    updateThemeColors();
}

CustomChartBase::~CustomChartBase() {
    if (animation_) {
        animation_->stop();
        delete animation_;
    }
}

void CustomChartBase::setTitle(const QString& title) {
    if (title_ != title) {
        title_ = title;
        backgroundCacheDirty_ = true;
        update();
    }
}

void CustomChartBase::setSubtitle(const QString& subtitle) {
    if (subtitle_ != subtitle) {
        subtitle_ = subtitle;
        backgroundCacheDirty_ = true;
        update();
    }
}

void CustomChartBase::setMargins(const ChartMargins& margins) {
    margins_ = margins;
    calculateChartRect();
    backgroundCacheDirty_ = true;
    update();
}

void CustomChartBase::setXAxisConfig(const AxisConfig& config) {
    xAxis_ = config;
    updateAxesRange();
    backgroundCacheDirty_ = true;
    update();
}

void CustomChartBase::setYAxisConfig(const AxisConfig& config) {
    yAxis_ = config;
    updateAxesRange();
    backgroundCacheDirty_ = true;
    update();
}

void CustomChartBase::setLegendConfig(const LegendConfig& config) {
    legend_ = config;
    calculateChartRect();
    update();
}

void CustomChartBase::setTooltipConfig(const TooltipConfig& config) {
    tooltip_ = config;
    update();
}

void CustomChartBase::setEffectsConfig(const EffectsConfig& config) {
    effects_ = config;
    if (effects_.animationEnabled) {
        animation_->setDuration(effects_.animationDuration);
    }
    update();
}

void CustomChartBase::setAnimationProgress(float progress) {
    animationState_.progress = qBound(0.0f, progress, 1.0f);
    update();
}

void CustomChartBase::startAnimation() {
    if (!effects_.animationEnabled) {
        animationState_.progress = 1.0f;
        update();
        return;
    }
    
    animationState_.isAnimating = true;
    animationState_.progress = 0.0f;
    animation_->start();
}

void CustomChartBase::stopAnimation() {
    animation_->stop();
    animationState_.isAnimating = false;
}

void CustomChartBase::clearData() {
    backgroundCacheDirty_ = true;
    update();
}

QPixmap CustomChartBase::toPixmap(const QSize& size) {
    QSize pixmapSize = size.isValid() ? size : this->size();
    QPixmap pixmap(pixmapSize);
    pixmap.fill(Qt::transparent);
    
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Temporarily disable animation for export
    float savedProgress = animationState_.progress;
    animationState_.progress = 1.0f;
    
    // Scale if needed
    if (size.isValid() && size != this->size()) {
        painter.scale(static_cast<double>(size.width()) / width(),
                     static_cast<double>(size.height()) / height());
    }
    
    // Draw everything
    paintEvent(nullptr);
    
    // Restore animation state
    animationState_.progress = savedProgress;
    
    return pixmap;
}

bool CustomChartBase::saveToFile(const QString& filename, const QSize& size) {
    return toPixmap(size).save(filename);
}

void CustomChartBase::refresh() {
    updateData();
    backgroundCacheDirty_ = true;
    update();
}

void CustomChartBase::resetView() {
    zoomFactor_ = 1.0;
    panOffset_ = QPointF();
    updateAxesRange();
    backgroundCacheDirty_ = true;
    update();
}

void CustomChartBase::zoomIn() {
    zoomFactor_ *= 1.2;
    updateAxesRange();
    backgroundCacheDirty_ = true;
    update();
}

void CustomChartBase::zoomOut() {
    zoomFactor_ /= 1.2;
    updateAxesRange();
    backgroundCacheDirty_ = true;
    update();
}

void CustomChartBase::fitToView() {
    resetView();
    updateData();
}

void CustomChartBase::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    
    // Draw background
    drawBackground(&painter);
    
    // Draw grid
    drawGrid(&painter);
    
    // Draw axes
    drawAxes(&painter);
    
    // Draw data
    painter.save();
    painter.setClipRect(chartRect_);
    drawData(&painter);
    painter.restore();
    
    // Draw title
    drawTitle(&painter);
    
    // Draw legend
    drawLegend(&painter);
    
    // Draw selection
    if (interaction_.isSelecting) {
        drawSelection(&painter);
    }
    
    // Draw tooltip
    if (showTooltip_ && tooltip_.enabled) {
        drawTooltip(&painter);
    }
}

void CustomChartBase::resizeEvent(QResizeEvent* event) {
    BaseStyledWidget::resizeEvent(event);
    calculateChartRect();
    backgroundCacheDirty_ = true;
}

void CustomChartBase::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        interaction_.isDragging = true;
        interaction_.dragStartPoint = event->position();
        
        // Check if clicking on data point
        int seriesIndex, pointIndex;
        pointIndex = findNearestDataPoint(event->position(), seriesIndex);
        if (pointIndex >= 0) {
            emit dataPointClicked(seriesIndex, pointIndex);
        } else {
            emit chartClicked(mapFromChart(event->position()));
        }
    } else if (event->button() == Qt::RightButton) {
        interaction_.isSelecting = true;
        interaction_.selectionRect.setTopLeft(event->position());
        interaction_.selectionRect.setBottomRight(event->position());
    }
    
    update();
}

void CustomChartBase::mouseMoveEvent(QMouseEvent* event) {
    QPointF pos = event->position();
    
    // Update hover state
    int seriesIndex, pointIndex;
    pointIndex = findNearestDataPoint(pos, seriesIndex);
    
    if (pointIndex != interaction_.hoveredPointIndex || 
        seriesIndex != interaction_.hoveredSeriesIndex) {
        interaction_.hoveredPointIndex = pointIndex;
        interaction_.hoveredSeriesIndex = seriesIndex;
        
        if (pointIndex >= 0) {
            emit dataPointHovered(seriesIndex, pointIndex);
            updateTooltip(pos);
        } else {
            hideTooltip();
        }
        
        update();
    }
    
    // Handle dragging
    if (interaction_.isDragging && event->buttons() & Qt::LeftButton) {
        QPointF delta = pos - interaction_.dragStartPoint;
        panOffset_ += delta;
        interaction_.dragStartPoint = pos;
        updateAxesRange();
        backgroundCacheDirty_ = true;
        update();
    }
    
    // Handle selection
    if (interaction_.isSelecting) {
        interaction_.selectionRect.setBottomRight(pos);
        update();
    }
}

void CustomChartBase::mouseReleaseEvent(QMouseEvent* event) {
    Q_UNUSED(event)
    
    if (interaction_.isSelecting) {
        emit selectionChanged(interaction_.selectionRect.normalized());
    }
    
    interaction_.isDragging = false;
    interaction_.isSelecting = false;
    update();
}

void CustomChartBase::mouseDoubleClickEvent(QMouseEvent* event) {
    Q_UNUSED(event)
    resetView();
}

void CustomChartBase::wheelEvent(QWheelEvent* event) {
    double delta = event->angleDelta().y() / 120.0;
    double scaleFactor = std::pow(1.1, delta);
    
    zoomFactor_ *= scaleFactor;
    zoomFactor_ = qBound(0.1, zoomFactor_, 10.0);
    
    updateAxesRange();
    backgroundCacheDirty_ = true;
    update();
}

void CustomChartBase::enterEvent(QEnterEvent* event) {
    Q_UNUSED(event)
    interaction_.isHovering = true;
    update();
}

void CustomChartBase::leaveEvent(QEvent* event) {
    Q_UNUSED(event)
    interaction_.isHovering = false;
    hideTooltip();
    update();
}

void CustomChartBase::drawBackground(QPainter* painter) {
    // Check if we need to redraw the cached background
    if (backgroundCacheDirty_ || cachedBackground_.size() != size()) {
        cachedBackground_ = QPixmap(size());
        cachedBackground_.fill(Qt::transparent);
        
        QPainter cachePainter(&cachedBackground_);
        cachePainter.setRenderHint(QPainter::Antialiasing);
        
        // Draw background with glass morphism if enabled
        if (effects_.glassMorphism) {
            ChartUtils::drawGlassMorphism(&cachePainter, rect(), effects_);
        } else {
            // Draw solid background
            cachePainter.fillRect(rect(), themeColor(ThemeColor::Background));
        }
        
        // Draw border
        QPen borderPen(themeColor(ThemeColor::Border));
        borderPen.setWidth(1);
        cachePainter.setPen(borderPen);
        cachePainter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
        
        backgroundCacheDirty_ = false;
    }
    
    painter->drawPixmap(0, 0, cachedBackground_);
}

void CustomChartBase::drawAxes(QPainter* painter) {
    painter->save();
    
    QPen axisPen(xAxis_.lineColor.isValid() ? xAxis_.lineColor : themeColor(ThemeColor::Text));
    axisPen.setWidth(2);
    painter->setPen(axisPen);
    
    // Draw X axis
    if (xAxis_.visible) {
        painter->drawLine(chartRect_.bottomLeft(), chartRect_.bottomRight());
        
        // Draw X axis ticks and labels
        if (xAxis_.showLabels) {
            double range = xAxis_.max - xAxis_.min;
            int tickCount = static_cast<int>(range / xAxis_.tickInterval) + 1;
            
            QFont labelFont = font();
            labelFont.setPointSize(9);
            painter->setFont(labelFont);
            
            for (int i = 0; i < tickCount; ++i) {
                double value = xAxis_.min + i * xAxis_.tickInterval;
                double x = chartRect_.left() + ChartUtils::valueToPixel(
                    value, xAxis_.min, xAxis_.max, chartRect_.width());
                
                // Draw tick
                painter->drawLine(QPointF(x, chartRect_.bottom()), 
                                QPointF(x, chartRect_.bottom() + 5));
                
                // Draw label
                QString label = ChartUtils::formatValue(value, xAxis_.labelFormat);
                QRectF textRect = painter->fontMetrics().boundingRect(label);
                painter->drawText(QPointF(x - textRect.width() / 2, 
                                        chartRect_.bottom() + 5 + textRect.height()), 
                                label);
            }
        }
        
        // Draw X axis title
        if (!xAxis_.title.isEmpty()) {
            QFont titleFont = font();
            titleFont.setPointSize(11);
            titleFont.setBold(true);
            painter->setFont(titleFont);
            
            QRectF textRect = painter->fontMetrics().boundingRect(xAxis_.title);
            painter->drawText(QPointF(chartRect_.center().x() - textRect.width() / 2,
                                    height() - 10), xAxis_.title);
        }
    }
    
    // Draw Y axis
    if (yAxis_.visible) {
        painter->drawLine(chartRect_.topLeft(), chartRect_.bottomLeft());
        
        // Draw Y axis ticks and labels
        if (yAxis_.showLabels) {
            double range = yAxis_.max - yAxis_.min;
            int tickCount = static_cast<int>(range / yAxis_.tickInterval) + 1;
            
            QFont labelFont = font();
            labelFont.setPointSize(9);
            painter->setFont(labelFont);
            
            for (int i = 0; i < tickCount; ++i) {
                double value = yAxis_.min + i * yAxis_.tickInterval;
                double y = chartRect_.bottom() - ChartUtils::valueToPixel(
                    value, yAxis_.min, yAxis_.max, chartRect_.height());
                
                // Draw tick
                painter->drawLine(QPointF(chartRect_.left() - 5, y), 
                                QPointF(chartRect_.left(), y));
                
                // Draw label
                QString label = ChartUtils::formatValue(value, yAxis_.labelFormat);
                QRectF textRect = painter->fontMetrics().boundingRect(label);
                painter->drawText(QPointF(chartRect_.left() - 10 - textRect.width(), 
                                        y + textRect.height() / 4), label);
            }
        }
        
        // Draw Y axis title
        if (!yAxis_.title.isEmpty()) {
            painter->save();
            QFont titleFont = font();
            titleFont.setPointSize(11);
            titleFont.setBold(true);
            painter->setFont(titleFont);
            
            painter->translate(15, chartRect_.center().y());
            painter->rotate(-90);
            
            QRectF textRect = painter->fontMetrics().boundingRect(yAxis_.title);
            painter->drawText(QPointF(-textRect.width() / 2, 0), yAxis_.title);
            painter->restore();
        }
    }
    
    painter->restore();
}

void CustomChartBase::drawGrid(QPainter* painter) {
    painter->save();
    
    // Set grid pen
    QPen gridPen(xAxis_.gridColor.isValid() ? xAxis_.gridColor : themeColor(ThemeColor::Border));
    gridPen.setStyle(Qt::DotLine);
    gridPen.setWidth(1);
    gridPen.setColor(gridPen.color().lighter(150));
    painter->setPen(gridPen);
    
    // Draw vertical grid lines
    if (xAxis_.showGrid) {
        double range = xAxis_.max - xAxis_.min;
        int tickCount = static_cast<int>(range / xAxis_.tickInterval) + 1;
        
        for (int i = 1; i < tickCount - 1; ++i) {
            double value = xAxis_.min + i * xAxis_.tickInterval;
            double x = chartRect_.left() + ChartUtils::valueToPixel(
                value, xAxis_.min, xAxis_.max, chartRect_.width());
            
            painter->drawLine(QPointF(x, chartRect_.top()), 
                            QPointF(x, chartRect_.bottom()));
        }
    }
    
    // Draw horizontal grid lines
    if (yAxis_.showGrid) {
        double range = yAxis_.max - yAxis_.min;
        int tickCount = static_cast<int>(range / yAxis_.tickInterval) + 1;
        
        for (int i = 1; i < tickCount - 1; ++i) {
            double value = yAxis_.min + i * yAxis_.tickInterval;
            double y = chartRect_.bottom() - ChartUtils::valueToPixel(
                value, yAxis_.min, yAxis_.max, chartRect_.height());
            
            painter->drawLine(QPointF(chartRect_.left(), y), 
                            QPointF(chartRect_.right(), y));
        }
    }
    
    painter->restore();
}

void CustomChartBase::drawTitle(QPainter* painter) {
    if (title_.isEmpty() && subtitle_.isEmpty()) return;
    
    painter->save();
    
    int y = margins_.top / 2;
    
    // Draw main title
    if (!title_.isEmpty()) {
        QFont titleFont = font();
        titleFont.setPointSize(16);
        titleFont.setBold(true);
        painter->setFont(titleFont);
        painter->setPen(themeColor(ThemeColor::Text));
        
        QRectF textRect = painter->fontMetrics().boundingRect(title_);
        painter->drawText(QPointF(width() / 2 - textRect.width() / 2, y), title_);
        
        y += textRect.height() + 5;
    }
    
    // Draw subtitle
    if (!subtitle_.isEmpty()) {
        QFont subtitleFont = font();
        subtitleFont.setPointSize(12);
        painter->setFont(subtitleFont);
        painter->setPen(themeColor(ThemeColor::TextSecondary));
        
        QRectF textRect = painter->fontMetrics().boundingRect(subtitle_);
        painter->drawText(QPointF(width() / 2 - textRect.width() / 2, y), subtitle_);
    }
    
    painter->restore();
}

void CustomChartBase::drawTooltip(QPainter* painter) {
    if (tooltipText_.isEmpty()) return;
    
    painter->save();
    
    // Calculate tooltip rect
    QFont tooltipFont = font();
    tooltipFont.setPointSize(10);
    painter->setFont(tooltipFont);
    
    QRectF textRect = painter->fontMetrics().boundingRect(tooltipText_);
    textRect.adjust(-tooltip_.padding, -tooltip_.padding, 
                   tooltip_.padding, tooltip_.padding);
    
    // Position tooltip
    QPointF pos = tooltipPos_;
    if (pos.x() + textRect.width() > width()) {
        pos.setX(width() - textRect.width() - 10);
    }
    if (pos.y() + textRect.height() > height()) {
        pos.setY(pos.y() - textRect.height() - 20);
    }
    
    textRect.moveTopLeft(pos);
    
    // Draw tooltip background with shadow
    if (effects_.shadowEnabled) {
        QPainterPath path;
        path.addRoundedRect(textRect, tooltip_.borderRadius, tooltip_.borderRadius);
        ChartUtils::drawShadow(painter, path, effects_);
    }
    
    // Draw tooltip background
    QColor bgColor = tooltip_.backgroundColor.isValid() ? 
                    tooltip_.backgroundColor : themeColor(ThemeColor::BackgroundElevated);
    bgColor.setAlphaF(tooltip_.backgroundOpacity);
    painter->fillRect(textRect, bgColor);
    
    // Draw tooltip border
    QPen borderPen(tooltip_.borderColor.isValid() ? 
                  tooltip_.borderColor : themeColor(ThemeColor::Border));
    borderPen.setWidthF(tooltip_.borderWidth);
    painter->setPen(borderPen);
    painter->drawRoundedRect(textRect, tooltip_.borderRadius, tooltip_.borderRadius);
    
    // Draw tooltip text
    painter->setPen(tooltip_.textColor.isValid() ? 
                   tooltip_.textColor : themeColor(ThemeColor::Text));
    painter->drawText(textRect, Qt::AlignCenter, tooltipText_);
    
    painter->restore();
}

void CustomChartBase::drawSelection(QPainter* painter) {
    painter->save();
    
    // Draw selection rectangle
    QColor selectionColor = themeColor(ThemeColor::Primary);
    selectionColor.setAlpha(30);
    painter->fillRect(interaction_.selectionRect.normalized(), selectionColor);
    
    QPen selectionPen(themeColor(ThemeColor::Primary));
    selectionPen.setStyle(Qt::DashLine);
    painter->setPen(selectionPen);
    painter->drawRect(interaction_.selectionRect.normalized());
    
    painter->restore();
}

void CustomChartBase::calculateChartRect() {
    chartRect_ = rect().adjusted(margins_.left, margins_.top, 
                               -margins_.right, -margins_.bottom);
    
    // Adjust for legend if visible
    if (legend_.visible) {
        switch (legend_.position) {
            case LegendConfig::Right:
                chartRect_.setRight(chartRect_.right() - 150);
                break;
            case LegendConfig::Left:
                chartRect_.setLeft(chartRect_.left() + 150);
                break;
            case LegendConfig::Top:
                chartRect_.setTop(chartRect_.top() + 80);
                break;
            case LegendConfig::Bottom:
                chartRect_.setBottom(chartRect_.bottom() - 80);
                break;
            default:
                break;
        }
    }
}

void CustomChartBase::updateAxesRange() {
    if (xAxis_.autoScale) {
        updateData();
    }
    if (yAxis_.autoScale) {
        updateData();
    }
}

QPointF CustomChartBase::mapToChart(const QPointF& point) const {
    double x = ChartUtils::pixelToValue(point.x() - chartRect_.left(), 
                                       xAxis_.min, xAxis_.max, chartRect_.width());
    double y = ChartUtils::pixelToValue(chartRect_.bottom() - point.y(), 
                                       yAxis_.min, yAxis_.max, chartRect_.height());
    return QPointF(x, y);
}

QPointF CustomChartBase::mapFromChart(const QPointF& chartPoint) const {
    double x = chartRect_.left() + ChartUtils::valueToPixel(
        chartPoint.x(), xAxis_.min, xAxis_.max, chartRect_.width());
    double y = chartRect_.bottom() - ChartUtils::valueToPixel(
        chartPoint.y(), yAxis_.min, yAxis_.max, chartRect_.height());
    return QPointF(x, y);
}

void CustomChartBase::updateAnimation() {
    animationState_.elapsed += 16; // ~60fps
    if (animationState_.elapsed >= animationState_.duration) {
        animationState_.progress = 1.0f;
        animationState_.isAnimating = false;
        emit animationFinished();
    } else {
        animationState_.progress = static_cast<float>(animationState_.elapsed) / 
                                 animationState_.duration;
    }
}

float CustomChartBase::getAnimatedValue(float from, float to) const {
    float easedProgress = animationState_.getEasedProgress();
    return ChartUtils::lerp(from, to, easedProgress);
}

QPointF CustomChartBase::getAnimatedPoint(const QPointF& from, const QPointF& to) const {
    float easedProgress = animationState_.getEasedProgress();
    return ChartUtils::lerp(from, to, easedProgress);
}

QColor CustomChartBase::getAnimatedColor(const QColor& from, const QColor& to) const {
    float easedProgress = animationState_.getEasedProgress();
    return ChartUtils::lerp(from, to, easedProgress);
}

void CustomChartBase::drawGlowingLine(QPainter* painter, const QPointF& start, const QPointF& end, 
                                    const QColor& color, float width, float glowRadius) {
    if (!effects_.glowEnabled) {
        QPen pen(color);
        pen.setWidthF(width);
        painter->setPen(pen);
        painter->drawLine(start, end);
        return;
    }
    
    // Draw glow
    QPainterPath path;
    path.moveTo(start);
    path.lineTo(end);
    
    QColor glowColor = color;
    glowColor.setAlphaF(effects_.glowIntensity);
    ChartUtils::drawGlowEffect(painter, path, glowColor, glowRadius);
    
    // Draw main line
    QPen pen(color);
    pen.setWidthF(width);
    pen.setCapStyle(Qt::RoundCap);
    painter->setPen(pen);
    painter->drawLine(start, end);
}

void CustomChartBase::drawGlowingPoint(QPainter* painter, const QPointF& center, float radius,
                                     const QColor& color, float glowRadius) {
    if (effects_.glowEnabled && glowRadius > 0) {
        // Draw glow
        QRadialGradient gradient(center, radius + glowRadius);
        QColor glowColor = color;
        glowColor.setAlphaF(effects_.glowIntensity);
        gradient.setColorAt(0, glowColor);
        gradient.setColorAt(1, Qt::transparent);
        
        painter->setPen(Qt::NoPen);
        painter->setBrush(gradient);
        painter->drawEllipse(center, radius + glowRadius, radius + glowRadius);
    }
    
    // Draw main point
    painter->setPen(Qt::NoPen);
    painter->setBrush(color);
    painter->drawEllipse(center, radius, radius);
}

void CustomChartBase::drawGlassRect(QPainter* painter, const QRectF& rect, const QColor& color,
                                  float opacity, float blurRadius) {
    Q_UNUSED(blurRadius)
    
    painter->save();
    
    // Draw base color with opacity
    QColor baseColor = color;
    baseColor.setAlphaF(opacity);
    painter->fillRect(rect, baseColor);
    
    // Draw glass gradient overlay
    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    gradient.setColorAt(0, QColor(255, 255, 255, 40));
    gradient.setColorAt(0.5, QColor(255, 255, 255, 20));
    gradient.setColorAt(1, QColor(255, 255, 255, 10));
    painter->fillRect(rect, gradient);
    
    // Draw glass border
    QPen borderPen(QColor(255, 255, 255, 80));
    borderPen.setWidth(1);
    painter->setPen(borderPen);
    painter->drawRect(rect);
    
    painter->restore();
}

void CustomChartBase::initializeAnimation() {
    animation_ = new QPropertyAnimation(this, "animationProgress");
    animation_->setDuration(effects_.animationDuration);
    animation_->setStartValue(0.0f);
    animation_->setEndValue(1.0f);
    
    switch (effects_.animationType) {
        case AnimationType::EaseIn:
            animation_->setEasingCurve(QEasingCurve::InQuad);
            break;
        case AnimationType::EaseOut:
            animation_->setEasingCurve(QEasingCurve::OutQuad);
            break;
        case AnimationType::EaseInOut:
            animation_->setEasingCurve(QEasingCurve::InOutQuad);
            break;
        case AnimationType::Bounce:
            animation_->setEasingCurve(QEasingCurve::OutBounce);
            break;
        case AnimationType::Elastic:
            animation_->setEasingCurve(QEasingCurve::OutElastic);
            break;
        case AnimationType::Back:
            animation_->setEasingCurve(QEasingCurve::OutBack);
            break;
        default:
            animation_->setEasingCurve(QEasingCurve::Linear);
            break;
    }
    
    connect(animation_, &QPropertyAnimation::finished, [this]() {
        animationState_.isAnimating = false;
        emit animationFinished();
    });
}

void CustomChartBase::updateTooltip(const QPointF& pos) {
    tooltipPos_ = pos + QPointF(10, 10);
    tooltipTimer_->start();
}

void CustomChartBase::hideTooltip() {
    tooltipTimer_->stop();
    showTooltip_ = false;
    tooltipText_.clear();
    update();
}

} // namespace llm_re::ui_v2::charts