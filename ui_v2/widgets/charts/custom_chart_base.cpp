#include "../../core/ui_v2_common.h"
#include "custom_chart_base.h"
#include "../../core/color_constants.h"

namespace llm_re::ui_v2::charts {


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

void CustomChartBase::updateData() {
    backgroundCacheDirty_ = true;
    update();
}

void CustomChartBase::clearData() {
    backgroundCacheDirty_ = true;
    update();
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
        interaction_.dragStartPoint = event->pos();
        
        // Check if clicking on data point
        int seriesIndex, pointIndex;
        pointIndex = findNearestDataPoint(event->pos(), seriesIndex);
        if (pointIndex >= 0) {
            emit dataPointClicked(seriesIndex, pointIndex);
        } else {
            emit chartClicked(mapFromChart(event->pos()));
        }
    } else if (event->button() == Qt::RightButton) {
        interaction_.isSelecting = true;
        interaction_.selectionRect.setTopLeft(event->pos());
        interaction_.selectionRect.setBottomRight(event->pos());
    }
    
    update();
}

void CustomChartBase::mouseMoveEvent(QMouseEvent* event) {
    QPointF pos = event->pos();
    
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

void CustomChartBase::enterEvent(QEvent* event) {
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
            cachePainter.fillRect(rect(), ThemeManager::instance().colors().background);
        }
        
        // Draw border
        QPen borderPen(ThemeManager::instance().colors().border);
        borderPen.setWidth(1);
        cachePainter.setPen(borderPen);
        cachePainter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);
        
        backgroundCacheDirty_ = false;
    }
    
    painter->drawPixmap(0, 0, cachedBackground_);
}

void CustomChartBase::drawAxes(QPainter* painter) {
    painter->save();
    
    QPen axisPen(xAxis_.lineColor.isValid() ? xAxis_.lineColor : ThemeManager::instance().colors().textPrimary);
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
    QPen gridPen(xAxis_.gridColor.isValid() ? xAxis_.gridColor : ThemeManager::instance().colors().border);
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

void CustomChartBase::drawLegend(QPainter* painter) {
    // Default implementation - derived classes can override
    Q_UNUSED(painter)
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
        painter->setPen(ThemeManager::instance().colors().textPrimary);
        
        QRectF textRect = painter->fontMetrics().boundingRect(title_);
        painter->drawText(QPointF(width() / 2.0 - textRect.width() / 2.0, y), title_);
        
        y += textRect.height() + 5;
    }
    
    // Draw subtitle
    if (!subtitle_.isEmpty()) {
        QFont subtitleFont = font();
        subtitleFont.setPointSize(12);
        painter->setFont(subtitleFont);
        painter->setPen(ThemeManager::instance().colors().textSecondary);
        
        QRectF textRect = painter->fontMetrics().boundingRect(subtitle_);
        painter->drawText(QPointF(width() / 2.0 - textRect.width() / 2.0, y), subtitle_);
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
                    tooltip_.backgroundColor : ThemeManager::instance().colors().surface;
    bgColor.setAlphaF(tooltip_.backgroundOpacity);
    painter->fillRect(textRect, bgColor);
    
    // Draw tooltip border
    QPen borderPen(tooltip_.borderColor.isValid() ? 
                  tooltip_.borderColor : ThemeManager::instance().colors().border);
    borderPen.setWidthF(tooltip_.borderWidth);
    painter->setPen(borderPen);
    painter->drawRoundedRect(textRect, tooltip_.borderRadius, tooltip_.borderRadius);
    
    // Draw tooltip text
    painter->setPen(tooltip_.textColor.isValid() ? 
                   tooltip_.textColor : ThemeManager::instance().colors().textPrimary);
    painter->drawText(textRect, Qt::AlignCenter, tooltipText_);
    
    painter->restore();
}

void CustomChartBase::drawSelection(QPainter* painter) {
    painter->save();
    
    // Draw selection rectangle
    QColor selectionColor = ThemeManager::instance().colors().primary;
    selectionColor.setAlpha(30);
    painter->fillRect(interaction_.selectionRect.normalized(), selectionColor);
    
    QPen selectionPen(ThemeManager::instance().colors().primary);
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
    // Calculate data bounds for auto-scaling
    // This should be implemented by derived classes that need auto-scaling
    // Default implementation does nothing to avoid recursion
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
    const auto& colors = ThemeManager::instance().colors();
    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    gradient.setColorAt(0, colors.glassOverlay);
    gradient.setColorAt(0.5, ThemeManager::adjustAlpha(colors.glassOverlay, colors.glassOverlay.alpha() / 2));
    gradient.setColorAt(1, ThemeManager::adjustAlpha(colors.glassOverlay, colors.glassOverlay.alpha() / 4));
    painter->fillRect(rect, gradient);
    
    // Draw glass border
    QPen borderPen(colors.glassBorder);
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