#include "chart_types.h"
#include <QPainter>
#include <QLinearGradient>
#include <QRadialGradient>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <cmath>

namespace llm_re::ui_v2::charts {

// ChartDataPoint implementation
ChartDataPoint::ChartDataPoint(double x, double y, const QString& label)
    : x(x), y(y), label(label) {}

// ChartSeries implementation
ChartSeries::ChartSeries(const QString& name)
    : name(name), visible(true), lineWidth(2.0f), symbolSize(8.0f),
      lineStyle(Qt::SolidLine), symbolType(ChartDataPoint::Circle) {}

void ChartSeries::addDataPoint(const ChartDataPoint& point) {
    dataPoints.push_back(point);
}

void ChartSeries::addDataPoint(double x, double y, const QString& label) {
    dataPoints.emplace_back(x, y, label);
}

void ChartSeries::clearData() {
    dataPoints.clear();
}

double ChartSeries::minX() const {
    if (dataPoints.empty()) return 0.0;
    return std::min_element(dataPoints.begin(), dataPoints.end(),
                          [](const auto& a, const auto& b) { return a.x < b.x; })->x;
}

double ChartSeries::maxX() const {
    if (dataPoints.empty()) return 1.0;
    return std::max_element(dataPoints.begin(), dataPoints.end(),
                          [](const auto& a, const auto& b) { return a.x < b.x; })->x;
}

double ChartSeries::minY() const {
    if (dataPoints.empty()) return 0.0;
    return std::min_element(dataPoints.begin(), dataPoints.end(),
                          [](const auto& a, const auto& b) { return a.y < b.y; })->y;
}

double ChartSeries::maxY() const {
    if (dataPoints.empty()) return 1.0;
    return std::max_element(dataPoints.begin(), dataPoints.end(),
                          [](const auto& a, const auto& b) { return a.y < b.y; })->y;
}

// AnimationHelper implementation
AnimationHelper::AnimationHelper()
    : duration(500), easing(QEasingCurve::InOutCubic), progress(0.0f) {}

void AnimationHelper::start() {
    startTime = std::chrono::steady_clock::now();
    progress = 0.0f;
}

void AnimationHelper::update() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime).count();
    progress = std::min(1.0f, static_cast<float>(elapsed) / duration);
    progress = easing.valueForProgress(progress);
}

bool AnimationHelper::isFinished() const {
    return progress >= 1.0f;
}

float AnimationHelper::value() const {
    return progress;
}

// GlowEffects implementation
QGraphicsEffect* GlowEffects::createGlow(const QColor& color, int radius) {
    auto* effect = new QGraphicsDropShadowEffect();
    effect->setColor(color);
    effect->setBlurRadius(radius);
    effect->setOffset(0, 0);
    return effect;
}

QGraphicsEffect* GlowEffects::createNeonGlow(const QColor& color) {
    auto* effect = new QGraphicsDropShadowEffect();
    QColor glowColor = color.lighter(150);
    glowColor.setAlpha(200);
    effect->setColor(glowColor);
    effect->setBlurRadius(20);
    effect->setOffset(0, 0);
    return effect;
}

void GlowEffects::drawGlowPath(QPainter* painter, const QPainterPath& path, 
                               const QColor& color, int glowRadius) {
    painter->save();
    
    // Draw multiple layers of glow
    for (int i = glowRadius; i > 0; i -= 2) {
        QColor glowColor = color;
        glowColor.setAlpha(20 - (i / 2));
        
        QPen glowPen(glowColor);
        glowPen.setWidth(i);
        glowPen.setCapStyle(Qt::RoundCap);
        glowPen.setJoinStyle(Qt::RoundJoin);
        
        painter->setPen(glowPen);
        painter->drawPath(path);
    }
    
    // Draw the main path
    painter->setPen(QPen(color, 2));
    painter->drawPath(path);
    
    painter->restore();
}

void GlowEffects::drawGlowText(QPainter* painter, const QString& text, 
                              const QPointF& pos, const QColor& color, int glowRadius) {
    painter->save();
    
    // Draw glow layers
    for (int i = glowRadius; i > 0; i -= 2) {
        QColor glowColor = color;
        glowColor.setAlpha(30 - (i / 2));
        painter->setPen(glowColor);
        
        for (int dx = -i/2; dx <= i/2; dx += i/4) {
            for (int dy = -i/2; dy <= i/2; dy += i/4) {
                if (dx != 0 || dy != 0) {
                    painter->drawText(pos + QPointF(dx, dy), text);
                }
            }
        }
    }
    
    // Draw main text
    painter->setPen(color);
    painter->drawText(pos, text);
    
    painter->restore();
}

// GradientHelper implementation
QLinearGradient GradientHelper::createVerticalGradient(const QRectF& rect, 
                                                       const QColor& topColor, 
                                                       const QColor& bottomColor) {
    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    gradient.setColorAt(0, topColor);
    gradient.setColorAt(1, bottomColor);
    return gradient;
}

QLinearGradient GradientHelper::createHorizontalGradient(const QRectF& rect, 
                                                         const QColor& leftColor, 
                                                         const QColor& rightColor) {
    QLinearGradient gradient(rect.topLeft(), rect.topRight());
    gradient.setColorAt(0, leftColor);
    gradient.setColorAt(1, rightColor);
    return gradient;
}

QRadialGradient GradientHelper::createRadialGradient(const QPointF& center, 
                                                      qreal radius, 
                                                      const QColor& centerColor, 
                                                      const QColor& edgeColor) {
    QRadialGradient gradient(center, radius);
    gradient.setColorAt(0, centerColor);
    gradient.setColorAt(1, edgeColor);
    return gradient;
}

QLinearGradient GradientHelper::createMultiStopGradient(const QRectF& rect, 
                                                        const QList<QPair<qreal, QColor>>& stops, 
                                                        bool vertical) {
    QLinearGradient gradient;
    if (vertical) {
        gradient = QLinearGradient(rect.topLeft(), rect.bottomLeft());
    } else {
        gradient = QLinearGradient(rect.topLeft(), rect.topRight());
    }
    
    for (const auto& stop : stops) {
        gradient.setColorAt(stop.first, stop.second);
    }
    
    return gradient;
}

QConicalGradient GradientHelper::createConicalGradient(const QPointF& center, 
                                                       qreal startAngle,
                                                       const QList<QPair<qreal, QColor>>& stops) {
    QConicalGradient gradient(center, startAngle);
    
    for (const auto& stop : stops) {
        gradient.setColorAt(stop.first, stop.second);
    }
    
    return gradient;
}

// ChartUtils implementation
QString ChartUtils::formatNumber(double value, int precision) {
    if (std::abs(value) >= 1e9) {
        return QString::number(value / 1e9, 'f', precision) + "B";
    } else if (std::abs(value) >= 1e6) {
        return QString::number(value / 1e6, 'f', precision) + "M";
    } else if (std::abs(value) >= 1e3) {
        return QString::number(value / 1e3, 'f', precision) + "K";
    } else {
        return QString::number(value, 'f', precision);
    }
}

QString ChartUtils::formatPercentage(double value, int precision) {
    return QString::number(value * 100, 'f', precision) + "%";
}

QString ChartUtils::formatTime(const QDateTime& time, const QString& format) {
    return time.toString(format.isEmpty() ? "hh:mm:ss" : format);
}

QString ChartUtils::formatDuration(qint64 milliseconds) {
    qint64 seconds = milliseconds / 1000;
    qint64 minutes = seconds / 60;
    qint64 hours = minutes / 60;
    qint64 days = hours / 24;
    
    if (days > 0) {
        return QString("%1d %2h").arg(days).arg(hours % 24);
    } else if (hours > 0) {
        return QString("%1h %2m").arg(hours).arg(minutes % 60);
    } else if (minutes > 0) {
        return QString("%1m %2s").arg(minutes).arg(seconds % 60);
    } else {
        return QString("%1s").arg(seconds);
    }
}

QString ChartUtils::formatBytes(qint64 bytes) {
    const QStringList units = {"B", "KB", "MB", "GB", "TB", "PB"};
    int unitIndex = 0;
    double size = bytes;
    
    while (size >= 1024 && unitIndex < units.size() - 1) {
        size /= 1024;
        unitIndex++;
    }
    
    return QString("%1 %2").arg(size, 0, 'f', unitIndex > 0 ? 2 : 0).arg(units[unitIndex]);
}

QColor ChartUtils::interpolateColor(const QColor& from, const QColor& to, float t) {
    return QColor(
        from.red() + static_cast<int>((to.red() - from.red()) * t),
        from.green() + static_cast<int>((to.green() - from.green()) * t),
        from.blue() + static_cast<int>((to.blue() - from.blue()) * t),
        from.alpha() + static_cast<int>((to.alpha() - from.alpha()) * t)
    );
}

QList<QColor> ChartUtils::generateColorPalette(int count, ColorPalette palette) {
    QList<QColor> colors;
    
    switch (palette) {
        case ColorPalette::Modern:
            colors = {
                QColor("#FF6B6B"), QColor("#4ECDC4"), QColor("#45B7D1"),
                QColor("#96CEB4"), QColor("#FECA57"), QColor("#DDA0DD"),
                QColor("#70A1FF"), QColor("#5F9EA0"), QColor("#FFB6C1")
            };
            break;
            
        case ColorPalette::Pastel:
            colors = {
                QColor("#FFE5E5"), QColor("#E5F3FF"), QColor("#E5FFE5"),
                QColor("#FFF5E5"), QColor("#F5E5FF"), QColor("#E5FFF5"),
                QColor("#FFE5F5"), QColor("#F5FFE5"), QColor("#E5E5FF")
            };
            break;
            
        case ColorPalette::Vibrant:
            colors = {
                QColor("#FF0066"), QColor("#00FF66"), QColor("#0066FF"),
                QColor("#FF6600"), QColor("#66FF00"), QColor("#6600FF"),
                QColor("#FF0099"), QColor("#00FF99"), QColor("#0099FF")
            };
            break;
            
        case ColorPalette::Dark:
            colors = {
                QColor("#2C3E50"), QColor("#34495E"), QColor("#7F8C8D"),
                QColor("#95A5A6"), QColor("#16A085"), QColor("#27AE60"),
                QColor("#2980B9"), QColor("#8E44AD"), QColor("#C0392B")
            };
            break;
            
        case ColorPalette::Warm:
            colors = {
                QColor("#FF6B6B"), QColor("#FF8E53"), QColor("#FE6B8B"),
                QColor("#FF6F91"), QColor("#FFA07A"), QColor("#FA8072"),
                QColor("#F08080"), QColor("#CD5C5C"), QColor("#DC143C")
            };
            break;
            
        case ColorPalette::Cool:
            colors = {
                QColor("#4ECDC4"), QColor("#45B7D1"), QColor("#5DADE2"),
                QColor("#48C9B0"), QColor("#52BE80"), QColor("#45B39D"),
                QColor("#40E0D0"), QColor("#20B2AA"), QColor("#3498DB")
            };
            break;
    }
    
    // If we need more colors, interpolate between existing ones
    while (colors.size() < count) {
        int size = colors.size();
        for (int i = 0; i < size - 1 && colors.size() < count; ++i) {
            colors.insert(i * 2 + 1, interpolateColor(colors[i * 2], colors[i * 2 + 1], 0.5));
        }
    }
    
    return colors.mid(0, count);
}

double ChartUtils::mapValue(double value, double fromMin, double fromMax, 
                           double toMin, double toMax) {
    if (fromMax == fromMin) return toMin;
    return toMin + (value - fromMin) * (toMax - toMin) / (fromMax - fromMin);
}

QPointF ChartUtils::polarToCartesian(double angle, double radius, const QPointF& center) {
    double radians = angle * M_PI / 180.0;
    return QPointF(
        center.x() + radius * std::cos(radians),
        center.y() + radius * std::sin(radians)
    );
}

// Glass morphism effect helper
void ChartUtils::applyGlassMorphism(QPainter* painter, const QRectF& rect, 
                                   const EffectsConfig& config) {
    if (!config.glassMorphism) return;
    
    painter->save();
    
    // Create glass effect with semi-transparent background
    QColor glassColor = Qt::white;
    glassColor.setAlpha(config.glassOpacity * 255);
    
    // Draw background with blur effect simulation
    QPainterPath path;
    path.addRoundedRect(rect, config.cornerRadius, config.cornerRadius);
    
    // Multiple layers for blur effect
    for (int i = 5; i > 0; --i) {
        QColor layerColor = glassColor;
        layerColor.setAlpha(10);
        painter->fillPath(path, layerColor);
    }
    
    // Main glass layer
    painter->fillPath(path, glassColor);
    
    // Glass border
    QPen borderPen(Qt::white);
    borderPen.setWidth(1);
    borderPen.setColor(QColor(255, 255, 255, 50));
    painter->setPen(borderPen);
    painter->drawPath(path);
    
    painter->restore();
}

} // namespace llm_re::ui_v2::charts