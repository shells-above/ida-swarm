#pragma once

#include "ui_v2_common.h"

namespace llm_re::ui_v2 {

// Design system constants
namespace Design {
    // Spacing system (8px base unit)
    constexpr int SPACING_NONE = 0;
    constexpr int SPACING_XS = 4;
    constexpr int SPACING_SM = 8;
    constexpr int SPACING_MD = 16;
    constexpr int SPACING_LG = 24;
    constexpr int SPACING_XL = 32;
    constexpr int SPACING_XXL = 48;

    // Border radius
    constexpr int RADIUS_SM = 4;
    constexpr int RADIUS_MD = 8;
    constexpr int RADIUS_LG = 12;
    constexpr int RADIUS_FULL = 9999;

    // Animation durations (ms)
    constexpr int ANIM_INSTANT = 0;
    constexpr int ANIM_FAST = 150;
    constexpr int ANIM_NORMAL = 250;
    constexpr int ANIM_SLOW = 400;

    // Font sizes - increased for better readability
    constexpr int FONT_SIZE_XS = 12;
    constexpr int FONT_SIZE_SM = 13;
    constexpr int FONT_SIZE_MD = 14;
    constexpr int FONT_SIZE_LG = 16;
    constexpr int FONT_SIZE_XL = 20;
    constexpr int FONT_SIZE_XXL = 24;

    // Icon sizes
    constexpr int ICON_SIZE_SM = 16;
    constexpr int ICON_SIZE_MD = 20;
    constexpr int ICON_SIZE_LG = 24;
    constexpr int ICON_SIZE_XL = 32;

    // Layout widths
    constexpr int MIN_PANEL_WIDTH = 200;
    constexpr int SIDEBAR_WIDTH = 280;
    constexpr int CONTENT_MAX_WIDTH = 800;
}

// Color palette interface - actual colors defined in themes
struct ColorPalette {
    // Brand colors
    QColor primary;
    QColor primaryHover;
    QColor primaryActive;
    
    // Semantic colors
    QColor success;
    QColor warning;
    QColor error;
    QColor info;
    
    // Neutral colors
    QColor background;
    QColor surface;
    QColor surfaceHover;
    QColor surfaceActive;
    QColor border;
    QColor borderStrong;
    
    // Text colors
    QColor textPrimary;
    QColor textSecondary;
    QColor textTertiary;
    QColor textInverse;
    QColor textLink;
    
    // Special purpose
    QColor codeBackground;
    QColor codeText;
    QColor selection;
    QColor overlay;
    QColor shadow;
    
    // Message type colors
    QColor userMessage;
    QColor assistantMessage;
    QColor systemMessage;
    
    // Analysis type colors
    QColor analysisNote;
    QColor analysisFinding;
    QColor analysisHypothesis;
    QColor analysisQuestion;
    QColor analysisAnalysis;
    QColor analysisDeepAnalysis;
    
    // Syntax highlighting
    QColor syntaxKeyword;
    QColor syntaxString;
    QColor syntaxNumber;
    QColor syntaxComment;
    QColor syntaxFunction;
    QColor syntaxVariable;
    QColor syntaxOperator;
    
    // Status colors
    QColor statusPending;
    QColor statusRunning;
    QColor statusCompleted;
    QColor statusFailed;
    QColor statusInterrupted;
    QColor statusUnknown;
    
    // Notification colors
    QColor notificationSuccess;
    QColor notificationWarning;
    QColor notificationError;
    QColor notificationInfo;
    
    // Node confidence colors
    QColor confidenceHigh;
    QColor confidenceMedium;
    QColor confidenceLow;
    
    // Special purpose colors
    QColor bookmark;
    QColor searchHighlight;
    QColor diffAdd;
    QColor diffRemove;
    QColor currentLineHighlight;
    
    // Chart colors
    std::vector<QColor> chartSeriesColorsDark;
    std::vector<QColor> chartSeriesColorsLight;
    QColor chartGrid;
    QColor chartAxis;
    QColor chartLabel;
    QColor chartTooltipBg;
    QColor chartTooltipBorder;
    
    // Memory visualization colors
    QColor memoryNullByte;
    QColor memoryFullByte;
    QColor memoryAsciiByte;
    
    // Glass morphism colors
    QColor glassOverlay;
    QColor glassBorder;
    
    // Shadow colors with different intensities
    QColor shadowLight;
    QColor shadowMedium;
    QColor shadowDark;
};

// Typography definitions
struct Typography {
    QFont heading1;
    QFont heading2;
    QFont heading3;
    QFont body;
    QFont bodySmall;
    QFont code;
    QFont caption;
    
    void setupFonts(const QString& baseFamily = "Segoe UI", 
                   const QString& codeFamily = "Consolas") {
        // Headings
        heading1 = QFont(baseFamily, Design::FONT_SIZE_XXL, QFont::Bold);
        heading2 = QFont(baseFamily, Design::FONT_SIZE_XL, QFont::DemiBold);
        heading3 = QFont(baseFamily, Design::FONT_SIZE_LG, QFont::DemiBold);
        
        // Body text
        body = QFont(baseFamily, Design::FONT_SIZE_MD);
        bodySmall = QFont(baseFamily, Design::FONT_SIZE_SM);
        
        // Code
        code = QFont(codeFamily, Design::FONT_SIZE_MD);
        code.setStyleHint(QFont::Monospace);
        
        // Caption
        caption = QFont(baseFamily, Design::FONT_SIZE_XS);
    }
};

// Component style definitions
struct ComponentStyles {
    // Buttons
    struct Button {
        int paddingHorizontal = Design::SPACING_MD;
        int paddingVertical = Design::SPACING_SM;
        int borderRadius = Design::RADIUS_MD;
        int borderWidth = 1;
    };
    
    // Input fields
    struct Input {
        int paddingHorizontal = Design::SPACING_SM;
        int paddingVertical = Design::SPACING_SM;
        int borderRadius = Design::RADIUS_SM;
        int borderWidth = 1;
    };
    
    // Cards/Panels
    struct Card {
        int padding = Design::SPACING_MD;
        int borderRadius = Design::RADIUS_MD;
        int borderWidth = 1;
    };
    
    // Messages
    struct Message {
        int padding = Design::SPACING_MD;
        int borderRadius = Design::RADIUS_LG;
        int maxWidth = 600;
    };
    
    // Charts
    struct Chart {
        // Line charts
        float lineWidth = 2.5f;
        float pointRadius = 4.0f;
        float hoverPointRadius = 6.0f;
        bool smoothCurves = true;
        bool showDataPoints = true;
        float areaOpacity = 0.2f;
        
        // Bar charts
        float barSpacing = 0.2f;
        float barCornerRadius = 4.0f;
        bool showBarValues = true;
        bool barGradient = true;
        bool barShadow = true;
        
        // Pie/Circular charts
        float innerRadiusRatio = 0.6f;
        float segmentSpacing = 2.0f;
        float hoverScale = 1.05f;
        float hoverOffset = 10.0f;
        
        // Heatmaps
        float cellSpacing = 1.0f;
        float cellCornerRadius = 2.0f;
        
        // General
        bool animateOnLoad = true;
        bool animateOnUpdate = true;
        int animationDuration = 800;
        bool showTooltips = true;
        bool showLegend = true;
        bool glowEffects = true;
        float glowRadius = 15.0f;
    };
    
    Button button;
    Input input;
    Card card;
    Message message;
    Chart chart;
    
    // Global border radius setting
    int borderRadius = 8;
};

// Z-index layers
namespace ZIndex {
    constexpr int BASE = 0;
    constexpr int CARD = 1;
    constexpr int DROPDOWN = 10;
    constexpr int MODAL_BACKDROP = 100;
    constexpr int MODAL = 101;
    constexpr int TOOLTIP = 200;
    constexpr int NOTIFICATION = 300;
}

} // namespace llm_re::ui_v2