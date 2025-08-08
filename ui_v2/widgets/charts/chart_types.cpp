#include "../../core/ui_v2_common.h"
#include "chart_types.h"
#include "../../core/theme_manager.h"
#include "../../core/color_constants.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace llm_re::ui_v2::charts {

// EffectsConfig constructor
EffectsConfig::EffectsConfig() {
    // Initialize shadow color from theme or use default
    const auto& colors = ThemeManager::instance().colors();
    shadowColor = colors.shadow;
}

// AnimationState implementation
float AnimationState::getEasedProgress() const {
    // Simple easing implementation based on type
    switch (type) {
        case AnimationType::None:
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
            
        case AnimationType::Bounce:
            if (progress < 0.36364f) {
                return 7.5625f * progress * progress;
            } else if (progress < 0.72727f) {
                float t = progress - 0.54545f;
                return 7.5625f * t * t + 0.75f;
            } else if (progress < 0.90909f) {
                float t = progress - 0.81818f;
                return 7.5625f * t * t + 0.9375f;
            } else {
                float t = progress - 0.95454f;
                return 7.5625f * t * t + 0.984375f;
            }
            
        case AnimationType::Elastic: {
            if (progress == 0.0f || progress == 1.0f) return progress;
            float p = 0.3f;
            float s = p / 4.0f;
            float t = progress - 1.0f;
            return -std::pow(2.0f, 10.0f * t) * std::sin((t - s) * 2.0f * M_PI / p);
        }
            
        case AnimationType::Back: {
            float s = 1.70158f;
            return progress * progress * ((s + 1.0f) * progress - s);
        }
    }
    
    return progress;
}

// ColorPalette implementation
const std::vector<QColor>& ColorPalette::getDefaultPalette() {
    // Use theme's primary colors to generate a palette
    static std::vector<QColor> themeBasedPalette;
    if (themeBasedPalette.empty()) {
        const auto& colors = ThemeManager::instance().colors();
        themeBasedPalette = {
            colors.primary,
            colors.success,
            colors.warning,
            colors.error,
            colors.info,
            colors.syntaxKeyword,
            colors.syntaxString,
            colors.syntaxFunction,
            colors.syntaxVariable,
            colors.syntaxOperator
        };
    }
    if (!themeBasedPalette.empty()) {
        return themeBasedPalette;
    }
    
    // Fallback to hardcoded palette only if theme doesn't provide colors
    static const std::vector<QColor> palette = {
        QColor(59, 130, 246),     // Blue
        QColor(16, 185, 129),     // Green
        QColor(251, 146, 60),     // Orange
        QColor(244, 63, 94),      // Red
        QColor(147, 51, 234),     // Purple
        QColor(250, 204, 21),     // Yellow
        QColor(14, 165, 233),     // Sky
        QColor(236, 72, 153),     // Pink
        QColor(34, 197, 94),      // Emerald
        QColor(168, 85, 247)      // Violet
    };
    return palette;
}

const std::vector<QColor>& ColorPalette::getVibrantPalette() {
    // For vibrant palette, we'll modify the theme colors to be more saturated
    static std::vector<QColor> vibratedThemeColors;
    const auto& themeColors = getDefaultPalette();
    
    if (!themeColors.empty() && vibratedThemeColors.empty()) {
        vibratedThemeColors.reserve(themeColors.size());
        for (const auto& color : themeColors) {
            // Increase saturation for vibrant effect
            QColor vibrant = color.toHsv();
            vibrant.setHsv(vibrant.hue(), 255, vibrant.value());
            vibratedThemeColors.push_back(vibrant.toRgb());
        }
        return vibratedThemeColors;
    }
    
    // Fallback palette
    static const std::vector<QColor> palette = {
        QColor(255, 0, 102),      // Hot pink
        QColor(0, 255, 102),      // Lime
        QColor(102, 0, 255),      // Blue violet
        QColor(255, 102, 0),      // Orange
        QColor(0, 102, 255),      // Sky blue
        QColor(255, 255, 0),      // Yellow
        QColor(255, 0, 255),      // Magenta
        QColor(0, 255, 255),      // Cyan
        QColor(102, 255, 0),      // Chartreuse
        QColor(255, 102, 255)     // Light pink
    };
    return palette;
}

const std::vector<QColor>& ColorPalette::getPastelPalette() {
    // For pastel palette, we'll modify the theme colors to be lighter
    static std::vector<QColor> pastelThemeColors;
    const auto& themeColors = getDefaultPalette();
    
    if (!themeColors.empty() && pastelThemeColors.empty()) {
        pastelThemeColors.reserve(themeColors.size());
        for (const auto& color : themeColors) {
            // Create pastel version by mixing with white
            QColor pastel = QColor(
                (color.red() + 255 * 2) / 3,
                (color.green() + 255 * 2) / 3,
                (color.blue() + 255 * 2) / 3
            );
            pastelThemeColors.push_back(pastel);
        }
        return pastelThemeColors;
    }
    
    // Fallback palette
    static const std::vector<QColor> palette = {
        QColor(255, 179, 186),    // Pastel pink
        QColor(186, 255, 201),    // Pastel green
        QColor(186, 225, 255),    // Pastel blue
        QColor(255, 223, 186),    // Pastel orange
        QColor(225, 186, 255),    // Pastel purple
        QColor(255, 255, 186),    // Pastel yellow
        QColor(255, 186, 225),    // Pastel magenta
        QColor(186, 255, 255),    // Pastel cyan
        QColor(201, 255, 186),    // Pastel lime
        QColor(255, 201, 186)     // Pastel coral
    };
    return palette;
}

const std::vector<QColor>& ColorPalette::getMonochromaticPalette(const QColor& base) {
    static std::vector<QColor> palette;
    palette.clear();
    
    // Generate shades and tints of the base color
    for (int i = 0; i < 10; ++i) {
        float factor = 0.3f + (i * 0.07f);
        if (i < 5) {
            // Darker shades
            palette.push_back(base.darker(150 + (5 - i) * 30));
        } else {
            // Lighter tints
            palette.push_back(base.lighter(100 + (i - 4) * 20));
        }
    }
    
    return palette;
}

QColor ColorPalette::getColorAt(int index, const std::vector<QColor>& palette) {
    if (palette.empty()) return ThemeManager::instance().colors().textPrimary;
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

// ChartUtils implementation
double ChartUtils::valueToPixel(double value, double min, double max, double pixelRange, bool invert) {
    if (max == min) return pixelRange / 2;
    double normalized = (value - min) / (max - min);
    return invert ? pixelRange * (1.0 - normalized) : pixelRange * normalized;
}

double ChartUtils::pixelToValue(double pixel, double min, double max, double pixelRange, bool invert) {
    if (pixelRange == 0) return min;
    double normalized = pixel / pixelRange;
    if (invert) normalized = 1.0 - normalized;
    return min + normalized * (max - min);
}

QString ChartUtils::formatValue(double value, const QString& format) {
    if (!format.isEmpty()) {
        return QString::asprintf(format.toStdString().c_str(), value);
    }
    
    // Auto format based on value magnitude
    if (std::abs(value) >= 1e9) {
        return QString::number(value / 1e9, 'f', 2) + "B";
    } else if (std::abs(value) >= 1e6) {
        return QString::number(value / 1e6, 'f', 2) + "M";
    } else if (std::abs(value) >= 1e3) {
        return QString::number(value / 1e3, 'f', 2) + "K";
    } else if (std::abs(value) < 0.01 && value != 0) {
        return QString::number(value, 'e', 2);
    } else {
        return QString::number(value, 'f', 2);
    }
}

QString ChartUtils::formatDateTime(const QDateTime& dt, const QString& format) {
    return dt.toString(format);
}

void ChartUtils::calculateNiceScale(double min, double max, double& niceMin, double& niceMax, double& tickInterval) {
    double range = max - min;
    if (range == 0) {
        niceMin = min - 1;
        niceMax = max + 1;
        tickInterval = 0.5;
        return;
    }
    
    // Calculate a nice tick interval
    double roughInterval = range / 5.0; // Aim for about 5 ticks
    double magnitude = std::pow(10, std::floor(std::log10(roughInterval)));
    double normalizedInterval = roughInterval / magnitude;
    
    // Choose a nice interval
    if (normalizedInterval <= 1.0) {
        tickInterval = magnitude;
    } else if (normalizedInterval <= 2.0) {
        tickInterval = 2.0 * magnitude;
    } else if (normalizedInterval <= 5.0) {
        tickInterval = 5.0 * magnitude;
    } else {
        tickInterval = 10.0 * magnitude;
    }
    
    // Calculate nice min and max
    niceMin = std::floor(min / tickInterval) * tickInterval;
    niceMax = std::ceil(max / tickInterval) * tickInterval;
}

double ChartUtils::lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

QPointF ChartUtils::lerp(const QPointF& a, const QPointF& b, double t) {
    return QPointF(lerp(a.x(), b.x(), t), lerp(a.y(), b.y(), t));
}

QColor ChartUtils::lerp(const QColor& a, const QColor& b, double t) {
    return QColor(
        static_cast<int>(lerp(a.red(), b.red(), t)),
        static_cast<int>(lerp(a.green(), b.green(), t)),
        static_cast<int>(lerp(a.blue(), b.blue(), t)),
        static_cast<int>(lerp(a.alpha(), b.alpha(), t))
    );
}

QColor ChartUtils::interpolateColor(const QColor& from, const QColor& to, double t) {
    // Same as lerp, but provided as an alias for clarity
    return lerp(from, to, t);
}

QPointF ChartUtils::calculateBezierPoint(const QPointF& p0, const QPointF& p1, 
                                        const QPointF& p2, const QPointF& p3, double t) {
    double u = 1.0 - t;
    double tt = t * t;
    double uu = u * u;
    double uuu = uu * u;
    double ttt = tt * t;
    
    QPointF p = p0 * uuu;
    p += p1 * (3 * uu * t);
    p += p2 * (3 * u * tt);
    p += p3 * ttt;
    
    return p;
}

std::vector<QPointF> ChartUtils::generateSmoothCurve(const std::vector<QPointF>& points, int segments) {
    std::vector<QPointF> smoothPoints;
    
    if (points.size() < 2) return points;
    if (points.size() == 2) {
        // Simple line
        for (int i = 0; i <= segments; ++i) {
            double t = static_cast<double>(i) / segments;
            smoothPoints.push_back(lerp(points[0], points[1], t));
        }
        return smoothPoints;
    }
    
    // Generate control points for cubic Bezier curves
    std::vector<QPointF> p1Points, p2Points;
    
    for (size_t i = 0; i < points.size() - 1; ++i) {
        double dx = points[i + 1].x() - points[i].x();
        double dy = points[i + 1].y() - points[i].y();
        
        QPointF p1, p2;
        
        if (i == 0) {
            p1 = QPointF(points[i].x() + dx * 0.25, points[i].y() + dy * 0.25);
        } else {
            double prevDx = points[i].x() - points[i - 1].x();
            double prevDy = points[i].y() - points[i - 1].y();
            p1 = QPointF(points[i].x() + prevDx * 0.25, points[i].y() + prevDy * 0.25);
        }
        
        if (i == points.size() - 2) {
            p2 = QPointF(points[i + 1].x() - dx * 0.25, points[i + 1].y() - dy * 0.25);
        } else {
            double nextDx = points[i + 2].x() - points[i + 1].x();
            double nextDy = points[i + 2].y() - points[i + 1].y();
            p2 = QPointF(points[i + 1].x() - nextDx * 0.25, points[i + 1].y() - nextDy * 0.25);
        }
        
        p1Points.push_back(p1);
        p2Points.push_back(p2);
    }
    
    // Generate smooth curve
    for (size_t i = 0; i < points.size() - 1; ++i) {
        for (int j = 0; j <= segments; ++j) {
            double t = static_cast<double>(j) / segments;
            QPointF point = calculateBezierPoint(points[i], p1Points[i], p2Points[i], points[i + 1], t);
            smoothPoints.push_back(point);
        }
    }
    
    return smoothPoints;
}

bool ChartUtils::pointInCircle(const QPointF& point, const QPointF& center, double radius) {
    double dx = point.x() - center.x();
    double dy = point.y() - center.y();
    return (dx * dx + dy * dy) <= (radius * radius);
}

bool ChartUtils::pointNearLine(const QPointF& point, const QPointF& lineStart, const QPointF& lineEnd, double threshold) {
    // Calculate distance from point to line segment
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
    double distance = std::sqrt(dx * dx + dy * dy);
    
    return distance <= threshold;
}

void ChartUtils::drawGlowEffect(QPainter* painter, const QPainterPath& path, const QColor& glowColor, float radius) {
    painter->save();
    
    // Draw multiple layers of glow
    for (int i = static_cast<int>(radius); i > 0; i -= 2) {
        QColor layerColor = glowColor;
        int alpha = static_cast<int>(20 * (1.0f - static_cast<float>(i) / radius));
        layerColor.setAlpha(alpha);
        
        QPen glowPen(layerColor);
        glowPen.setWidth(i);
        glowPen.setCapStyle(Qt::RoundCap);
        glowPen.setJoinStyle(Qt::RoundJoin);
        
        painter->setPen(glowPen);
        painter->drawPath(path);
    }
    
    painter->restore();
}

void ChartUtils::drawShadow(QPainter* painter, const QPainterPath& path, const EffectsConfig& effects) {
    if (!effects.shadowEnabled) return;
    
    painter->save();
    
    // Draw shadow
    painter->translate(effects.shadowOffsetX, effects.shadowOffsetY);
    
    // Blur effect simulation with multiple layers
    int blurSteps = static_cast<int>(effects.shadowBlur);
    for (int i = blurSteps; i > 0; i -= 2) {
        QColor shadowLayer = effects.shadowColor;
        int alpha = shadowLayer.alpha() * (blurSteps - i) / blurSteps;
        shadowLayer.setAlpha(alpha);
        
        painter->fillPath(path, shadowLayer);
    }
    
    painter->restore();
}

void ChartUtils::drawGlassMorphism(QPainter* painter, const QRectF& rect, const EffectsConfig& effects) {
    if (!effects.glassMorphism) return;
    
    painter->save();
    
    // Create glass effect with semi-transparent background
    QColor glassColor = ThemeManager::instance().colors().surface;
    glassColor.setAlpha(static_cast<int>(effects.glassOpacity * 255));
    
    // Draw background with blur effect simulation
    QPainterPath path;
    path.addRoundedRect(rect, 8, 8);
    
    // Multiple layers for blur effect
    int blurLayers = static_cast<int>(effects.blurRadius / 5);
    for (int i = blurLayers; i > 0; --i) {
        QColor layerColor = glassColor;
        layerColor.setAlpha(10);
        painter->fillPath(path, layerColor);
    }
    
    // Main glass layer
    painter->fillPath(path, glassColor);
    
    // Glass border
    QPen borderPen(ThemeManager::instance().colors().border);
    borderPen.setWidth(1);
    painter->setPen(borderPen);
    painter->drawPath(path);
    
    painter->restore();
}

} // namespace llm_re::ui_v2::charts