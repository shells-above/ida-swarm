#include "../../core/ui_v2_common.h"
#include "chart_theme.h"
#include "../../core/theme_manager.h"

namespace llm_re::ui_v2::charts {

void ChartTheme::applyTheme(ThemeManager::Theme theme, AxisConfig& xAxis, AxisConfig& yAxis,
                           LegendConfig& legend, TooltipConfig& tooltip, EffectsConfig& effects) {
    // Apply colors based on theme
    xAxis.lineColor = getAxisColor(theme);
    xAxis.gridColor = getGridColor(theme);
    xAxis.textColor = getTextColor(theme);
    
    yAxis.lineColor = getAxisColor(theme);
    yAxis.gridColor = getGridColor(theme);
    yAxis.textColor = getTextColor(theme);
    
    legend.backgroundColor = getBackgroundColor(theme);
    legend.borderColor = getBorderColor(theme);
    legend.textColor = getTextColor(theme);
    
    tooltip.backgroundColor = ThemeManager::instance().colors().surface;
    tooltip.borderColor = ThemeManager::instance().colors().border;
    tooltip.textColor = ThemeManager::instance().colors().textPrimary;
    
    // Apply theme-specific effects
    if (theme == ThemeManager::Theme::Dark) {
        effects = getDarkThemeEffects();
    } else {
        effects = getLightThemeEffects();
    }
}

QColor ChartTheme::getBackgroundColor(ThemeManager::Theme theme) {
    Q_UNUSED(theme);
    return ThemeManager::instance().colors().background;
}

QColor ChartTheme::getGridColor(ThemeManager::Theme theme) {
    Q_UNUSED(theme);
    QColor gridColor = ThemeManager::instance().colors().border;
    gridColor.setAlpha(50);
    return gridColor;
}

QColor ChartTheme::getAxisColor(ThemeManager::Theme theme) {
    Q_UNUSED(theme);
    return ThemeManager::instance().colors().textPrimary;
}

QColor ChartTheme::getTextColor(ThemeManager::Theme theme) {
    Q_UNUSED(theme);
    return ThemeManager::instance().colors().textPrimary;
}

QColor ChartTheme::getTextSecondaryColor(ThemeManager::Theme theme) {
    Q_UNUSED(theme);
    return ThemeManager::instance().colors().textSecondary;
}

QColor ChartTheme::getBorderColor(ThemeManager::Theme theme) {
    Q_UNUSED(theme);
    return ThemeManager::instance().colors().border;
}

std::vector<QColor> ChartTheme::getSeriesColors(ThemeManager::Theme theme) {
    if (theme == ThemeManager::Theme::Dark) {
        // Neon-inspired colors for dark theme
        return {
            QColor(0, 255, 255),      // Cyan
            QColor(255, 0, 255),      // Magenta
            QColor(0, 255, 127),      // Spring green
            QColor(255, 127, 0),      // Orange
            QColor(127, 0, 255),      // Blue violet
            QColor(255, 255, 0),      // Yellow
            QColor(255, 0, 127),      // Hot pink
            QColor(0, 127, 255),      // Sky blue
            QColor(127, 255, 0),      // Chartreuse
            QColor(255, 127, 255)     // Light pink
        };
    } else {
        // Professional colors for light theme
        return {
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
    }
}

QColor ChartTheme::getSeriesColor(ThemeManager::Theme theme, int index) {
    auto colors = getSeriesColors(theme);
    return colors[index % colors.size()];
}

QLinearGradient ChartTheme::getBackgroundGradient(ThemeManager::Theme theme, const QRectF& rect) {
    QLinearGradient gradient(rect.topLeft(), rect.bottomRight());
    
    if (theme == ThemeManager::Theme::Dark) {
        gradient.setColorAt(0, QColor(20, 20, 30));
        gradient.setColorAt(1, QColor(10, 10, 20));
    } else {
        gradient.setColorAt(0, QColor(250, 250, 252));
        gradient.setColorAt(1, QColor(240, 240, 245));
    }
    
    return gradient;
}

QLinearGradient ChartTheme::getSeriesGradient(ThemeManager::Theme theme, int index, const QRectF& rect) {
    QColor baseColor = getSeriesColor(theme, index);
    QLinearGradient gradient(rect.topLeft(), rect.bottomLeft());
    
    gradient.setColorAt(0, baseColor);
    QColor endColor = baseColor;
    endColor.setAlpha(theme == ThemeManager::Theme::Dark ? 50 : 100);
    gradient.setColorAt(1, endColor);
    
    return gradient;
}

EffectsConfig ChartTheme::getDarkThemeEffects() {
    EffectsConfig effects;
    
    // Enhanced glow for dark theme
    effects.glowEnabled = true;
    effects.glowRadius = 15.0f;
    effects.glowIntensity = 0.7f;
    
    // Subtle shadow
    effects.shadowEnabled = true;
    effects.shadowOffsetX = 0.0f;
    effects.shadowOffsetY = 2.0f;
    effects.shadowBlur = 8.0f;
    effects.shadowColor = QColor(0, 0, 0, 100);
    
    // Smooth animations
    effects.animationEnabled = true;
    effects.animationDuration = 600;
    effects.animationType = AnimationType::EaseInOut;
    
    // Interactive hover
    effects.hoverEnabled = true;
    effects.hoverScale = 1.15f;
    effects.hoverGlow = 3.0f;
    
    // Glass morphism for modern look
    effects.glassMorphism = true;
    effects.glassOpacity = 0.1f;
    effects.blurRadius = 20.0f;
    
    return effects;
}

EffectsConfig ChartTheme::getLightThemeEffects() {
    EffectsConfig effects;
    
    // Subtle glow for light theme
    effects.glowEnabled = true;
    effects.glowRadius = 8.0f;
    effects.glowIntensity = 0.3f;
    
    // Pronounced shadow
    effects.shadowEnabled = true;
    effects.shadowOffsetX = 2.0f;
    effects.shadowOffsetY = 4.0f;
    effects.shadowBlur = 6.0f;
    effects.shadowColor = QColor(0, 0, 0, 30);
    
    // Smooth animations
    effects.animationEnabled = true;
    effects.animationDuration = 500;
    effects.animationType = AnimationType::EaseOut;
    
    // Subtle hover
    effects.hoverEnabled = true;
    effects.hoverScale = 1.08f;
    effects.hoverGlow = 1.5f;
    
    // No glass morphism for light theme
    effects.glassMorphism = false;
    
    return effects;
}

EffectsConfig ChartTheme::getHighContrastEffects() {
    EffectsConfig effects;
    
    // No glow for high contrast
    effects.glowEnabled = false;
    
    // Strong shadow for depth
    effects.shadowEnabled = true;
    effects.shadowOffsetX = 3.0f;
    effects.shadowOffsetY = 3.0f;
    effects.shadowBlur = 0.0f;
    effects.shadowColor = QColor(0, 0, 0, 255);
    
    // Fast animations
    effects.animationEnabled = true;
    effects.animationDuration = 200;
    effects.animationType = AnimationType::Linear;
    
    // Clear hover indication
    effects.hoverEnabled = true;
    effects.hoverScale = 1.2f;
    effects.hoverGlow = 0.0f;
    
    // No glass effects
    effects.glassMorphism = false;
    
    return effects;
}

void ChartTheme::applyStyle(ChartStyle style, EffectsConfig& effects, 
                           std::vector<QColor>& colors) {
    switch (style) {
        case Modern:
            effects.glowEnabled = true;
            effects.glowRadius = 10.0f;
            effects.glowIntensity = 0.4f;
            effects.shadowEnabled = true;
            effects.shadowBlur = 4.0f;
            effects.animationType = AnimationType::EaseInOut;
            colors = ColorPalette::getDefaultPalette();
            break;
            
        case Neon:
            effects.glowEnabled = true;
            effects.glowRadius = 20.0f;
            effects.glowIntensity = 0.9f;
            effects.shadowEnabled = false;
            effects.animationType = AnimationType::Elastic;
            effects.hoverGlow = 5.0f;
            colors = ColorPalette::getVibrantPalette();
            break;
            
        case Corporate:
            effects.glowEnabled = false;
            effects.shadowEnabled = true;
            effects.shadowBlur = 2.0f;
            effects.animationType = AnimationType::EaseOut;
            effects.animationDuration = 300;
            colors = {
                QColor(44, 62, 107),    // Navy
                QColor(109, 135, 188),  // Steel blue
                QColor(170, 184, 214),  // Light steel
                QColor(217, 133, 59),   // Copper
                QColor(242, 177, 65),   // Gold
                QColor(124, 181, 236),  // Sky
                QColor(67, 124, 186),   // Ocean
                QColor(92, 155, 213),   // Powder
                QColor(142, 68, 173),   // Wisteria
                QColor(192, 57, 43)     // Pomegranate
            };
            break;
            
        case Playful:
            effects.glowEnabled = true;
            effects.glowRadius = 15.0f;
            effects.glowIntensity = 0.6f;
            effects.animationType = AnimationType::Bounce;
            effects.animationDuration = 800;
            effects.hoverScale = 1.3f;
            colors = ColorPalette::getPastelPalette();
            break;
            
        case Terminal:
            effects.glowEnabled = true;
            effects.glowRadius = 5.0f;
            effects.glowIntensity = 1.0f;
            effects.shadowEnabled = false;
            effects.animationType = AnimationType::Linear;
            effects.animationDuration = 100;
            colors = {
                QColor(0, 255, 0),      // Terminal green
                QColor(0, 255, 255),    // Cyan
                QColor(255, 255, 255),  // White
                QColor(255, 127, 0),    // Orange
                QColor(255, 0, 0),      // Red
                QColor(255, 255, 0),    // Yellow
                QColor(127, 255, 0),    // Lime
                QColor(0, 127, 255),    // Blue
                QColor(255, 0, 255),    // Magenta
                QColor(127, 127, 127)   // Gray
            };
            break;
            
        case Glass:
            effects.glowEnabled = true;
            effects.glowRadius = 12.0f;
            effects.glowIntensity = 0.5f;
            effects.shadowEnabled = true;
            effects.shadowBlur = 10.0f;
            effects.glassMorphism = true;
            effects.glassOpacity = 0.7f;
            effects.blurRadius = 15.0f;
            effects.animationType = AnimationType::EaseInOut;
            colors = ColorPalette::getDefaultPalette();
            break;
    }
}

// Chart theme presets implementation
bool ChartThemePresets::loadPreset(const QString& name, LineChartTheme& theme) {
    if (name == "smooth") {
        theme.lineWidth = 3.0f;
        theme.pointRadius = 5.0f;
        theme.smoothCurves = true;
        theme.fillArea = true;
        theme.areaOpacity = 0.15f;
        theme.animateDrawing = true;
        theme.drawingDuration = 1200;
        return true;
    } else if (name == "sharp") {
        theme.lineWidth = 2.0f;
        theme.pointRadius = 4.0f;
        theme.smoothCurves = false;
        theme.fillArea = false;
        theme.showDataPoints = true;
        theme.animateDrawing = true;
        theme.drawingDuration = 600;
        return true;
    } else if (name == "minimal") {
        theme.lineWidth = 1.5f;
        theme.pointRadius = 0.0f;
        theme.smoothCurves = true;
        theme.fillArea = false;
        theme.showDataPoints = false;
        theme.animateDrawing = false;
        theme.glowOnHover = false;
        return true;
    }
    return false;
}

bool ChartThemePresets::loadPreset(const QString& name, CircularChartTheme& theme) {
    if (name == "donut") {
        theme.innerRadiusRatio = 0.65f;
        theme.segmentSpacing = 3.0f;
        theme.hoverScale = 1.08f;
        theme.showLabels = true;
        theme.showPercentages = true;
        theme.animateRotation = true;
        return true;
    } else if (name == "pie") {
        theme.innerRadiusRatio = 0.0f;
        theme.segmentSpacing = 1.0f;
        theme.hoverScale = 1.1f;
        theme.hoverOffset = 15.0f;
        theme.showLabels = true;
        theme.animateRotation = true;
        return true;
    } else if (name == "gauge") {
        theme.innerRadiusRatio = 0.75f;
        theme.segmentSpacing = 0.0f;
        theme.startAngle = -225.0f;
        theme.animateRotation = true;
        theme.innerShadow = true;
        theme.outerGlow = true;
        return true;
    }
    return false;
}

bool ChartThemePresets::loadPreset(const QString& name, BarChartTheme& theme) {
    if (name == "grouped") {
        theme.barSpacing = 0.1f;
        theme.cornerRadius = 4.0f;
        theme.showValues = true;
        theme.gradient = true;
        theme.animateGrowth = true;
        return true;
    } else if (name == "stacked") {
        theme.barSpacing = 0.0f;
        theme.cornerRadius = 0.0f;
        theme.showValues = false;
        theme.gradient = true;
        theme.shadow = false;
        return true;
    } else if (name == "horizontal") {
        theme.horizontal = true;
        theme.barSpacing = 0.2f;
        theme.cornerRadius = 3.0f;
        theme.showValues = true;
        return true;
    }
    return false;
}

bool ChartThemePresets::loadPreset(const QString& name, HeatmapTheme& theme) {
    if (name == "viridis") {
        theme.colorScale = HeatmapTheme::Viridis;
        theme.cellSpacing = 1.0f;
        theme.showGrid = true;
        theme.highlightOnHover = true;
        return true;
    } else if (name == "temperature") {
        theme.colorScale = HeatmapTheme::RedBlue;
        theme.cellSpacing = 0.5f;
        theme.cellCornerRadius = 0.0f;
        theme.showValues = true;
        return true;
    } else if (name == "matrix") {
        theme.colorScale = HeatmapTheme::GreenRed;
        theme.cellSpacing = 2.0f;
        theme.cellCornerRadius = 4.0f;
        theme.showGrid = false;
        theme.highlightOnHover = true;
        theme.hoverScale = 1.2f;
        return true;
    }
    return false;
}

bool ChartThemePresets::loadPreset(const QString& name, SparklineTheme& theme) {
    if (name == "inline") {
        theme.lineWidth = 1.0f;
        theme.fillArea = false;
        theme.showMinMax = false;
        theme.showLastValue = false;
        theme.height = 16.0f;
        theme.animateOnUpdate = false;
        return true;
    } else if (name == "detailed") {
        theme.lineWidth = 2.0f;
        theme.fillArea = true;
        theme.areaOpacity = 0.2f;
        theme.showMinMax = true;
        theme.showLastValue = true;
        theme.height = 30.0f;
        return true;
    }
    return false;
}

QStringList ChartThemePresets::availablePresets() {
    return {
        "smooth", "sharp", "minimal",           // Line charts
        "donut", "pie", "gauge",               // Circular charts
        "grouped", "stacked", "horizontal",     // Bar charts
        "viridis", "temperature", "matrix",     // Heatmaps
        "inline", "detailed"                    // Sparklines
    };
}

void ChartThemePresets::savePreset(const QString& name, const LineChartTheme& theme) {
    Q_UNUSED(name)
    Q_UNUSED(theme)
    // TODO: Implement preset saving to JSON
}

void ChartThemePresets::savePreset(const QString& name, const CircularChartTheme& theme) {
    Q_UNUSED(name)
    Q_UNUSED(theme)
    // TODO: Implement preset saving to JSON
}

void ChartThemePresets::savePreset(const QString& name, const BarChartTheme& theme) {
    Q_UNUSED(name)
    Q_UNUSED(theme)
    // TODO: Implement preset saving to JSON
}

void ChartThemePresets::savePreset(const QString& name, const HeatmapTheme& theme) {
    Q_UNUSED(name)
    Q_UNUSED(theme)
    // TODO: Implement preset saving to JSON
}

void ChartThemePresets::savePreset(const QString& name, const SparklineTheme& theme) {
    Q_UNUSED(name)
    Q_UNUSED(theme)
    // TODO: Implement preset saving to JSON
}

} // namespace llm_re::ui_v2::charts