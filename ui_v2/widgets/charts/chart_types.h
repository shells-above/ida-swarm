#pragma once

#include "../../core/ui_v2_common.h"

namespace llm_re::ui_v2::charts {

// Chart animation types
enum class AnimationType {
    None,
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    Bounce,
    Elastic,
    Back
};

// Data point for charts
struct ChartDataPoint {
    double x = 0.0;
    double y = 0.0;
    QString label;
    QColor color;
    QJsonObject metadata;
    
    // For time series
    QDateTime timestamp;
    
    // For categorical data
    QString category;
    
    ChartDataPoint() = default;
    ChartDataPoint(double xVal, double yVal, const QString& lbl = QString())
        : x(xVal), y(yVal), label(lbl) {}
    
    QPointF toPoint() const { return QPointF(x, y); }
};

// Data series for line/area charts
struct ChartSeries {
    QString name;
    std::vector<ChartDataPoint> points;
    QColor color;
    QColor fillColor;
    bool visible = true;
    bool showPoints = true;
    bool showLine = true;
    bool fillArea = false;
    float lineWidth = 2.0f;
    float pointRadius = 4.0f;
    Qt::PenStyle lineStyle = Qt::SolidLine;
    
    ChartSeries() = default;
    explicit ChartSeries(const QString& seriesName) : name(seriesName) {}
};

// Axis configuration
struct AxisConfig {
    QString title;
    double min = 0.0;
    double max = 100.0;
    double tickInterval = 10.0;
    bool autoScale = true;
    bool visible = true;
    bool showGrid = true;
    bool showLabels = true;
    QColor lineColor;
    QColor gridColor;
    QColor textColor;
    int labelPrecision = 1;
    QString labelFormat; // e.g., "%.2f", "%d", custom format
    
    enum Type {
        Linear,
        Logarithmic,
        DateTime,
        Category
    } type = Linear;
};

// Legend configuration
struct LegendConfig {
    enum Position {
        None,
        Top,
        Right,
        Bottom,
        Left,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    } position = Right;
    
    bool visible = true;
    QColor backgroundColor;
    QColor borderColor;
    QColor textColor;
    float borderWidth = 1.0f;
    int padding = 10;
    int spacing = 5;
    int iconSize = 16;
};

// Tooltip configuration
struct TooltipConfig {
    bool enabled = true;
    QColor backgroundColor;
    QColor borderColor;
    QColor textColor;
    float borderWidth = 1.0f;
    int padding = 8;
    int borderRadius = 4;
    float backgroundOpacity = 0.9f;
    
    enum DisplayMode {
        Single,      // Show only one series
        All,         // Show all series at x position
        Nearest      // Show nearest point
    } displayMode = Nearest;
};

// Chart margins
struct ChartMargins {
    int left = 60;
    int top = 40;
    int right = 40;
    int bottom = 60;
    
    ChartMargins() = default;
    ChartMargins(int l, int t, int r, int b) 
        : left(l), top(t), right(r), bottom(b) {}
};

// Visual effects configuration
struct EffectsConfig {
    // Glow effect
    bool glowEnabled = true;
    float glowRadius = 10.0f;
    float glowIntensity = 0.5f;
    
    // Shadow effect
    bool shadowEnabled = true;
    float shadowOffsetX = 2.0f;
    float shadowOffsetY = 2.0f;
    float shadowBlur = 4.0f;
    QColor shadowColor;  // Initialized from theme
    
    EffectsConfig();
    
    // Animation
    bool animationEnabled = true;
    int animationDuration = 500; // milliseconds
    AnimationType animationType = AnimationType::EaseInOut;
    
    // Hover effects
    bool hoverEnabled = true;
    float hoverScale = 1.1f;
    float hoverGlow = 2.0f;
    
    // Glass morphism
    bool glassMorphism = false;
    float glassOpacity = 0.8f;
    float blurRadius = 10.0f;
};

// Color palette for automatic color assignment
class ColorPalette {
public:
    static const std::vector<QColor>& getDefaultPalette();
    static const std::vector<QColor>& getVibrantPalette();
    static const std::vector<QColor>& getPastelPalette();
    static const std::vector<QColor>& getMonochromaticPalette(const QColor& base);
    static QColor getColorAt(int index, const std::vector<QColor>& palette);
    
    // Generate gradients
    static QLinearGradient createGradient(const QColor& start, const QColor& end, 
                                         const QRectF& rect, bool vertical = true);
    static QRadialGradient createRadialGradient(const QColor& center, const QColor& edge,
                                               const QPointF& centerPoint, float radius);
};

// Chart interaction state
struct InteractionState {
    bool isHovering = false;
    bool isDragging = false;
    bool isSelecting = false;
    QPointF hoverPoint;
    QPointF dragStartPoint;
    QRectF selectionRect;
    int hoveredSeriesIndex = -1;
    int hoveredPointIndex = -1;
    
    void reset() {
        isHovering = false;
        isDragging = false;
        isSelecting = false;
        hoveredSeriesIndex = -1;
        hoveredPointIndex = -1;
    }
};

// Animation state
struct AnimationState {
    float progress = 0.0f; // 0.0 to 1.0
    bool isAnimating = false;
    AnimationType type = AnimationType::EaseInOut;
    int duration = 500;
    int elapsed = 0;
    
    float getEasedProgress() const;
};

// Utility functions
namespace ChartUtils {
    // Convert value to pixel coordinate
    double valueToPixel(double value, double min, double max, double pixelRange, bool invert = false);
    
    // Convert pixel to value
    double pixelToValue(double pixel, double min, double max, double pixelRange, bool invert = false);
    
    // Format value for display
    QString formatValue(double value, const QString& format = QString());
    QString formatDateTime(const QDateTime& dt, const QString& format = "MMM dd hh:mm");
    
    // Calculate nice axis bounds
    void calculateNiceScale(double min, double max, double& niceMin, double& niceMax, double& tickInterval);
    
    // Interpolation
    double lerp(double a, double b, double t);
    QPointF lerp(const QPointF& a, const QPointF& b, double t);
    QColor lerp(const QColor& a, const QColor& b, double t);
    QColor interpolateColor(const QColor& from, const QColor& to, double t);
    
    // Bezier curve calculation
    QPointF calculateBezierPoint(const QPointF& p0, const QPointF& p1, 
                                const QPointF& p2, const QPointF& p3, double t);
    std::vector<QPointF> generateSmoothCurve(const std::vector<QPointF>& points, int segments = 20);
    
    // Hit testing
    bool pointInCircle(const QPointF& point, const QPointF& center, double radius);
    bool pointNearLine(const QPointF& point, const QPointF& lineStart, const QPointF& lineEnd, double threshold);
    
    // Drawing helpers
    void drawGlowEffect(QPainter* painter, const QPainterPath& path, const QColor& glowColor, float radius);
    void drawShadow(QPainter* painter, const QPainterPath& path, const EffectsConfig& effects);
    void drawGlassMorphism(QPainter* painter, const QRectF& rect, const EffectsConfig& effects);
}

} // namespace llm_re::ui_v2::charts