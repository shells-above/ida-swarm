#pragma once

#include "ui_v2_common.h"

namespace llm_re::ui_v2 {

// Color constants that should be used when specific semantic colors are needed
// These will be mapped to theme colors through ThemeManager
struct ColorConstants {
    // Semantic colors
    static constexpr const char* TRANSPARENT = "transparent";
    static constexpr const char* SELECTION = "selection";
    static constexpr const char* FOCUS = "focus";
    static constexpr const char* SHADOW = "shadow";
    
    // State colors
    static constexpr const char* SUCCESS = "success";
    static constexpr const char* WARNING = "warning";
    static constexpr const char* ERROR = "error";
    static constexpr const char* INFO = "info";
    
    // Chart specific
    static constexpr const char* CHART_GRID = "chartGrid";
    static constexpr const char* CHART_AXIS = "chartAxis";
    static constexpr const char* CHART_BACKGROUND = "chartBackground";
    
    // Special purpose
    static constexpr const char* OVERLAY = "overlay";
    static constexpr const char* HIGHLIGHT = "highlight";
    static constexpr const char* DISABLED = "disabled";
    
    // Get color from theme or fallback
    static QColor getThemeColor(const QString& colorName, const QColor& fallback = QColor());
};

} // namespace llm_re::ui_v2