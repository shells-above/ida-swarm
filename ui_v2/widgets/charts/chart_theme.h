#pragma once

#include "chart_types.h"
#include "../../core/theme_manager.h"

namespace llm_re::ui_v2::charts {

// Chart theme configuration
class ChartTheme {
public:
    // Get theme for specific chart type
    static void applyTheme(const Theme& theme, AxisConfig& xAxis, AxisConfig& yAxis,
                          LegendConfig& legend, TooltipConfig& tooltip, EffectsConfig& effects);
    
    // Get themed colors
    static QColor getBackgroundColor(const Theme& theme);
    static QColor getGridColor(const Theme& theme);
    static QColor getAxisColor(const Theme& theme);
    static QColor getTextColor(const Theme& theme);
    static QColor getTextSecondaryColor(const Theme& theme);
    static QColor getBorderColor(const Theme& theme);
    
    // Get series colors based on theme
    static std::vector<QColor> getSeriesColors(const Theme& theme);
    static QColor getSeriesColor(const Theme& theme, int index);
    
    // Get gradient colors
    static QLinearGradient getBackgroundGradient(const Theme& theme, const QRectF& rect);
    static QLinearGradient getSeriesGradient(const Theme& theme, int index, const QRectF& rect);
    
    // Theme-specific effects
    static EffectsConfig getDarkThemeEffects();
    static EffectsConfig getLightThemeEffects();
    static EffectsConfig getHighContrastEffects();
    
    // Predefined chart styles
    enum ChartStyle {
        Modern,      // Clean, minimal with subtle effects
        Neon,        // Vibrant colors with strong glow
        Corporate,   // Professional, muted colors
        Playful,     // Bright, animated with bounce effects
        Terminal,    // Monochrome, ASCII-inspired
        Glass        // Transparent with blur effects
    };
    
    static void applyStyle(ChartStyle style, EffectsConfig& effects, 
                          std::vector<QColor>& colors);
};

// Chart-specific theme configurations
struct LineChartTheme {
    float lineWidth = 2.5f;
    float pointRadius = 4.0f;
    float hoverPointRadius = 6.0f;
    bool smoothCurves = true;
    bool fillArea = false;
    float areaOpacity = 0.2f;
    bool showDataPoints = true;
    
    // Animation
    bool animateDrawing = true;
    int drawingDuration = 1000;
    
    // Hover effects
    bool glowOnHover = true;
    float hoverGlowRadius = 15.0f;
    float hoverLineWidth = 3.5f;
};

struct CircularChartTheme {
    float innerRadiusRatio = 0.6f;  // For donut charts
    float segmentSpacing = 2.0f;
    float hoverScale = 1.05f;
    float hoverOffset = 10.0f;
    bool showLabels = true;
    bool showPercentages = true;
    
    // Animation
    bool animateRotation = true;
    int rotationDuration = 800;
    float startAngle = -90.0f;
    
    // Effects
    bool innerShadow = true;
    bool outerGlow = true;
    float glowRadius = 20.0f;
};

struct BarChartTheme {
    float barSpacing = 0.2f;  // Ratio of bar width
    float cornerRadius = 4.0f;
    bool showValues = true;
    bool horizontal = false;
    
    // Animation
    bool animateGrowth = true;
    int growthDuration = 600;
    AnimationType growthAnimation = AnimationType::EaseOut;
    
    // Effects
    bool gradient = true;
    bool shadow = true;
    float shadowOffset = 3.0f;
};

struct HeatmapTheme {
    // Color scales
    enum ColorScale {
        Viridis,
        Plasma,
        Inferno,
        Magma,
        Turbo,
        RedBlue,
        GreenRed,
        Custom
    };
    
    ColorScale colorScale = Viridis;
    std::vector<QColor> customColors;
    
    float cellSpacing = 1.0f;
    float cellCornerRadius = 2.0f;
    bool showGrid = true;
    bool showValues = false;
    
    // Interactive
    bool highlightOnHover = true;
    float hoverScale = 1.1f;
    bool showTooltipValue = true;
};

struct SparklineTheme {
    float lineWidth = 1.5f;
    bool fillArea = true;
    float areaOpacity = 0.3f;
    bool showMinMax = true;
    bool showLastValue = true;
    float height = 20.0f;
    
    // Minimal style
    bool showAxes = false;
    bool showGrid = false;
    
    // Animation
    bool animateOnUpdate = true;
    int updateDuration = 300;
};

// Theme preset manager
class ChartThemePresets {
public:
    // Get preset by name
    static bool loadPreset(const QString& name, LineChartTheme& theme);
    static bool loadPreset(const QString& name, CircularChartTheme& theme);
    static bool loadPreset(const QString& name, BarChartTheme& theme);
    static bool loadPreset(const QString& name, HeatmapTheme& theme);
    static bool loadPreset(const QString& name, SparklineTheme& theme);
    
    // Available presets
    static QStringList availablePresets();
    
    // Save custom preset
    static void savePreset(const QString& name, const LineChartTheme& theme);
    static void savePreset(const QString& name, const CircularChartTheme& theme);
    static void savePreset(const QString& name, const BarChartTheme& theme);
    static void savePreset(const QString& name, const HeatmapTheme& theme);
    static void savePreset(const QString& name, const SparklineTheme& theme);
};

} // namespace llm_re::ui_v2::charts