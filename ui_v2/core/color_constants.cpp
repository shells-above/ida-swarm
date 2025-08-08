#include "color_constants.h"
#include "theme_manager.h"

namespace llm_re::ui_v2 {

QColor ColorConstants::getThemeColor(const QString& colorName, const QColor& fallback) {
    const auto& colors = ThemeManager::instance().colors();
    
    // Map semantic names to theme colors
    if (colorName == TRANSPARENT) {
        return Qt::transparent;
    } else if (colorName == SELECTION) {
        return colors.selection;
    } else if (colorName == FOCUS) {
        return colors.primary;
    } else if (colorName == SHADOW) {
        QColor shadow = colors.textPrimary;
        shadow.setAlpha(30);
        return shadow;
    } else if (colorName == SUCCESS) {
        return colors.success;
    } else if (colorName == WARNING) {
        return colors.warning;
    } else if (colorName == ERROR) {
        return colors.error;
    } else if (colorName == INFO) {
        return colors.info;
    } else if (colorName == CHART_GRID) {
        return colors.border;
    } else if (colorName == CHART_AXIS) {
        return colors.textPrimary;
    } else if (colorName == CHART_BACKGROUND) {
        return colors.surface;
    } else if (colorName == OVERLAY) {
        QColor overlay = colors.background;
        overlay.setAlpha(200);
        return overlay;
    } else if (colorName == HIGHLIGHT) {
        return colors.searchHighlight;
    } else if (colorName == DISABLED) {
        return colors.textTertiary;
    }
    
    return fallback.isValid() ? fallback : colors.textPrimary;
}

} // namespace llm_re::ui_v2