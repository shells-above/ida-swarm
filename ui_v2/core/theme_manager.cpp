#include "ui_v2_common.h"
#include "theme_manager.h"
#include "animation_manager.h"
#include "effects_manager.h"
#include "../../core/config.h"

namespace llm_re::ui_v2 {

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager() {
    initializeColorMap();
    
    // Cache themes directory
    themesDir_ = themesDirectory();
    ensureThemesDirectory();
    
    // Initialize with dark theme
    loadDefaultDarkTheme();
    typography_.setupFonts();
    updateComponentStyles();
    
    // Initialize current theme info
    currentThemeInfo_.name = "dark";
    currentThemeInfo_.displayName = "Dark (Built-in)";
    currentThemeInfo_.isBuiltIn = true;
    currentThemeInfo_.isModified = false;
    currentThemeInfo_.metadata.name = "Dark";
    currentThemeInfo_.metadata.author = "LLM RE";
    currentThemeInfo_.metadata.version = "1.0";
    currentThemeInfo_.metadata.description = "Default dark theme";
    currentThemeInfo_.metadata.baseTheme = ThemeConstants::THEME_DARK;
    
}

void ThemeManager::initializeColorMap() {
    // Map string names to color pointers for runtime lookup
    colorMap_ = {
        {"primary", &colors_.primary},
        {"primaryHover", &colors_.primaryHover},
        {"primaryActive", &colors_.primaryActive},
        {"success", &colors_.success},
        {"warning", &colors_.warning},
        {"error", &colors_.error},
        {"info", &colors_.info},
        {"background", &colors_.background},
        {"surface", &colors_.surface},
        {"surfaceHover", &colors_.surfaceHover},
        {"surfaceActive", &colors_.surfaceActive},
        {"border", &colors_.border},
        {"borderStrong", &colors_.borderStrong},
        {"textPrimary", &colors_.textPrimary},
        {"textSecondary", &colors_.textSecondary},
        {"textTertiary", &colors_.textTertiary},
        {"textInverse", &colors_.textInverse},
        {"textLink", &colors_.textLink},
        {"codeBackground", &colors_.codeBackground},
        {"codeText", &colors_.codeText},
        {"selection", &colors_.selection},
        {"overlay", &colors_.overlay},
        {"shadow", &colors_.shadow},
        {"userMessage", &colors_.userMessage},
        {"assistantMessage", &colors_.assistantMessage},
        {"systemMessage", &colors_.systemMessage},
        {"analysisNote", &colors_.analysisNote},
        {"analysisFinding", &colors_.analysisFinding},
        {"analysisHypothesis", &colors_.analysisHypothesis},
        {"analysisQuestion", &colors_.analysisQuestion},
        {"analysisAnalysis", &colors_.analysisAnalysis},
        {"analysisDeepAnalysis", &colors_.analysisDeepAnalysis},
        {"syntaxKeyword", &colors_.syntaxKeyword},
        {"syntaxString", &colors_.syntaxString},
        {"syntaxNumber", &colors_.syntaxNumber},
        {"syntaxComment", &colors_.syntaxComment},
        {"syntaxFunction", &colors_.syntaxFunction},
        {"syntaxVariable", &colors_.syntaxVariable},
        {"syntaxOperator", &colors_.syntaxOperator},
        // Status colors
        {"statusPending", &colors_.statusPending},
        {"statusRunning", &colors_.statusRunning},
        {"statusCompleted", &colors_.statusCompleted},
        {"statusFailed", &colors_.statusFailed},
        {"statusInterrupted", &colors_.statusInterrupted},
        {"statusUnknown", &colors_.statusUnknown},
        // Notification colors
        {"notificationSuccess", &colors_.notificationSuccess},
        {"notificationWarning", &colors_.notificationWarning},
        {"notificationError", &colors_.notificationError},
        {"notificationInfo", &colors_.notificationInfo},
        // Node confidence colors
        {"confidenceHigh", &colors_.confidenceHigh},
        {"confidenceMedium", &colors_.confidenceMedium},
        {"confidenceLow", &colors_.confidenceLow},
        // Special purpose colors
        {"bookmark", &colors_.bookmark},
        {"searchHighlight", &colors_.searchHighlight},
        {"diffAdd", &colors_.diffAdd},
        {"diffRemove", &colors_.diffRemove},
        {"currentLineHighlight", &colors_.currentLineHighlight},
        // Chart colors
        {"chartGrid", &colors_.chartGrid},
        {"chartAxis", &colors_.chartAxis},
        {"chartLabel", &colors_.chartLabel},
        {"chartTooltipBg", &colors_.chartTooltipBg},
        {"chartTooltipBorder", &colors_.chartTooltipBorder},
        // Memory visualization colors
        {"memoryNullByte", &colors_.memoryNullByte},
        {"memoryFullByte", &colors_.memoryFullByte},
        {"memoryAsciiByte", &colors_.memoryAsciiByte},
        // Glass morphism colors
        {"glassOverlay", &colors_.glassOverlay},
        {"glassBorder", &colors_.glassBorder},
        // Shadow colors
        {"shadowLight", &colors_.shadowLight},
        {"shadowMedium", &colors_.shadowMedium},
        {"shadowDark", &colors_.shadowDark}
    };
}


void ThemeManager::loadDefaultDarkTheme() {
    // Dark theme color palette
    colors_.primary = QColor(0x4A9EFF);
    colors_.primaryHover = QColor(0x6BB2FF);
    colors_.primaryActive = QColor(0x2E7FDB);
    
    colors_.success = QColor(0x4CAF50);
    colors_.warning = QColor(0xFF9800);
    colors_.error = QColor(0xF44336);
    colors_.info = QColor(0x2196F3);
    
    colors_.background = QColor(0x1E1E1E);
    colors_.surface = QColor(0x2D2D2D);
    colors_.surfaceHover = QColor(0x383838);
    colors_.surfaceActive = QColor(0x424242);
    colors_.border = QColor(0x3C3C3C);
    colors_.borderStrong = QColor(0x555555);
    
    colors_.textPrimary = QColor(0xFFFFFF);
    colors_.textSecondary = QColor(0xB0B0B0);
    colors_.textTertiary = QColor(0x808080);
    colors_.textInverse = QColor(0, 0, 0);
    colors_.textLink = QColor(0x4A9EFF);
    
    colors_.codeBackground = QColor(0x252525);
    colors_.codeText = QColor(0xD4D4D4);
    colors_.selection = QColor(0x264F78);
    colors_.overlay = QColor(0, 0, 0, 180);
    colors_.shadow = QColor(0, 0, 0, 60);
    
    colors_.userMessage = QColor(0x1E3A5F);
    colors_.assistantMessage = QColor(0x2D2D2D);
    colors_.systemMessage = QColor(0x3A2D1E);
    
    colors_.analysisNote = QColor(0x606060);
    colors_.analysisFinding = QColor(0xFF6B6B);
    colors_.analysisHypothesis = QColor(0xFFA94D);
    colors_.analysisQuestion = QColor(0x74A9FF);
    colors_.analysisAnalysis = QColor(0x69DB7C);
    colors_.analysisDeepAnalysis = QColor(0xCC5DE8);
    
    colors_.syntaxKeyword = QColor(0x569CD6);
    colors_.syntaxString = QColor(0xCE9178);
    colors_.syntaxNumber = QColor(0xB5CEA8);
    colors_.syntaxComment = QColor(0x6A9955);
    colors_.syntaxFunction = QColor(0xDCDCAA);
    colors_.syntaxVariable = QColor(0x9CDCFE);
    colors_.syntaxOperator = QColor(0xD4D4D4);
    
    // Status colors
    colors_.statusPending = QColor(0x9E9E9E);
    colors_.statusRunning = QColor(0x2196F3);
    colors_.statusCompleted = QColor(0x4CAF50);
    colors_.statusFailed = QColor(0xF44336);
    colors_.statusInterrupted = QColor(0xFF9800);
    colors_.statusUnknown = QColor(0x757575);
    
    // Notification colors
    colors_.notificationSuccess = QColor(0x4CAF50);
    colors_.notificationWarning = QColor(0xFF9800);
    colors_.notificationError = QColor(0xF44336);
    colors_.notificationInfo = QColor(0x2196F3);
    
    // Node confidence colors
    colors_.confidenceHigh = QColor(0x4CAF50);
    colors_.confidenceMedium = QColor(0xFF9800);
    colors_.confidenceLow = QColor(0xF44336);
    
    // Special purpose colors
    colors_.bookmark = QColor(0xFFD700);
    colors_.searchHighlight = QColor(255, 255, 0, 80);
    colors_.diffAdd = QColor(0, 255, 0, 30);
    colors_.diffRemove = QColor(255, 0, 0, 30);
    colors_.currentLineHighlight = QColor(255, 255, 0, 80);
    
    // Chart series colors for dark theme (neon-inspired)
    colors_.chartSeriesColorsDark = {
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
    
    // Use same colors for light theme temporarily (will be updated)
    colors_.chartSeriesColorsLight = colors_.chartSeriesColorsDark;
    
    // Chart specific colors
    colors_.chartGrid = QColor(255, 255, 255, 20);
    colors_.chartAxis = colors_.textSecondary;
    colors_.chartLabel = colors_.textPrimary;
    colors_.chartTooltipBg = colors_.surface;
    colors_.chartTooltipBorder = colors_.border;
    
    // Memory visualization colors
    colors_.memoryNullByte = colors_.textTertiary;      // Dim color for null bytes
    colors_.memoryFullByte = colors_.error;              // Red-ish for 0xFF bytes
    colors_.memoryAsciiByte = colors_.syntaxString;     // String color for ASCII
    
    // Glass morphism colors
    colors_.glassOverlay = QColor(255, 255, 255, 40);   // Semi-transparent white
    colors_.glassBorder = QColor(255, 255, 255, 80);    // More opaque white border
    
    // Shadow colors with different intensities
    colors_.shadowLight = adjustAlpha(colors_.shadow, 30);
    colors_.shadowMedium = adjustAlpha(colors_.shadow, 60);
    colors_.shadowDark = adjustAlpha(colors_.shadow, 80);
}

void ThemeManager::loadDefaultLightTheme() {
    // Light theme color palette
    colors_.primary = QColor(0x1976D2);
    colors_.primaryHover = QColor(0x1565C0);
    colors_.primaryActive = QColor(0x0D47A1);
    
    colors_.success = QColor(0x388E3C);
    colors_.warning = QColor(0xF57C00);
    colors_.error = QColor(0xD32F2F);
    colors_.info = QColor(0x1976D2);
    
    colors_.background = QColor(0xFAFAFA);
    colors_.surface = QColor(0xFFFFFF);
    colors_.surfaceHover = QColor(0xF5F5F5);
    colors_.surfaceActive = QColor(0xEEEEEE);
    colors_.border = QColor(0xE0E0E0);
    colors_.borderStrong = QColor(0xBDBDBD);
    
    colors_.textPrimary = QColor(0x212121);
    colors_.textSecondary = QColor(0x757575);
    colors_.textTertiary = QColor(0x9E9E9E);
    colors_.textInverse = QColor(0xFFFFFF);
    colors_.textLink = QColor(0x1976D2);
    
    colors_.codeBackground = QColor(0xF5F5F5);
    colors_.codeText = QColor(0x383A42);
    colors_.selection = QColor(0xBBDEFB);
    colors_.overlay = QColor(0, 0, 0, 120);
    colors_.shadow = QColor(0, 0, 0, 30);
    
    colors_.userMessage = QColor(0xE3F2FD);
    colors_.assistantMessage = QColor(0xF5F5F5);
    colors_.systemMessage = QColor(0xFFF3E0);
    
    colors_.analysisNote = QColor(0x9E9E9E);
    colors_.analysisFinding = QColor(0xE74C3C);
    colors_.analysisHypothesis = QColor(0xF39C12);
    colors_.analysisQuestion = QColor(0x3498DB);
    colors_.analysisAnalysis = QColor(0x27AE60);
    colors_.analysisDeepAnalysis = QColor(0x9B59B6);
    
    colors_.syntaxKeyword = QColor(0x0000FF);
    colors_.syntaxString = QColor(0xA31515);
    colors_.syntaxNumber = QColor(0x098658);
    colors_.syntaxComment = QColor(0x008000);
    colors_.syntaxFunction = QColor(0x795E26);
    colors_.syntaxVariable = QColor(0x001080);
    colors_.syntaxOperator = QColor(0x383A42);
    
    // Status colors
    colors_.statusPending = QColor(0x757575);
    colors_.statusRunning = QColor(0x1976D2);
    colors_.statusCompleted = QColor(0x388E3C);
    colors_.statusFailed = QColor(0xD32F2F);
    colors_.statusInterrupted = QColor(0xF57C00);
    colors_.statusUnknown = QColor(0x9E9E9E);
    
    // Notification colors
    colors_.notificationSuccess = QColor(0x388E3C);
    colors_.notificationWarning = QColor(0xF57C00);
    colors_.notificationError = QColor(0xD32F2F);
    colors_.notificationInfo = QColor(0x1976D2);
    
    // Node confidence colors
    colors_.confidenceHigh = QColor(0x388E3C);
    colors_.confidenceMedium = QColor(0xF57C00);
    colors_.confidenceLow = QColor(0xD32F2F);
    
    // Special purpose colors
    colors_.bookmark = QColor(0xFFC107);
    colors_.searchHighlight = QColor(255, 235, 59, 100);
    colors_.diffAdd = QColor(76, 175, 80, 30);
    colors_.diffRemove = QColor(244, 67, 54, 30);
    colors_.currentLineHighlight = QColor(255, 235, 59, 60);
    
    // Chart series colors for light theme (professional)
    colors_.chartSeriesColorsLight = {
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
    
    colors_.chartSeriesColorsDark = colors_.chartSeriesColorsLight;
    
    // Chart specific colors
    colors_.chartGrid = QColor(0, 0, 0, 30);
    colors_.chartAxis = colors_.textSecondary;
    colors_.chartLabel = colors_.textPrimary;
    colors_.chartTooltipBg = colors_.surface;
    colors_.chartTooltipBorder = colors_.border;
    
    // Memory visualization colors
    colors_.memoryNullByte = colors_.textTertiary;      // Dim color for null bytes
    colors_.memoryFullByte = colors_.error;              // Red-ish for 0xFF bytes
    colors_.memoryAsciiByte = colors_.syntaxString;     // String color for ASCII
    
    // Glass morphism colors
    colors_.glassOverlay = QColor(0, 0, 0, 10);         // Semi-transparent black for light theme
    colors_.glassBorder = QColor(0, 0, 0, 30);          // More opaque black border
    
    // Shadow colors with different intensities
    colors_.shadowLight = adjustAlpha(colors_.shadow, 30);
    colors_.shadowMedium = adjustAlpha(colors_.shadow, 60);
    colors_.shadowDark = adjustAlpha(colors_.shadow, 80);
}

void ThemeManager::loadThemeFromFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "Failed to open theme file:" << path;
        loadDefaultDarkTheme(); // Fallback
        return;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        qWarning() << "Invalid theme file format:" << path;
        loadDefaultDarkTheme(); // Fallback
        return;
    }
    
    QJsonObject root = doc.object();
    
    if (root.contains(ThemeConstants::KEY_METADATA)) {
        parseMetadata(root[ThemeConstants::KEY_METADATA].toObject());
    }
    
    if (root.contains(ThemeConstants::KEY_COLORS)) {
        parseColorPalette(root[ThemeConstants::KEY_COLORS].toObject());
    }
    
    if (root.contains(ThemeConstants::KEY_TYPOGRAPHY)) {
        parseTypography(root[ThemeConstants::KEY_TYPOGRAPHY].toObject());
    }
    
    if (root.contains(ThemeConstants::KEY_COMPONENTS)) {
        parseComponentStyles(root[ThemeConstants::KEY_COMPONENTS].toObject());
    }
    
    if (root.contains(ThemeConstants::KEY_ANIMATIONS)) {
        parseAnimations(root[ThemeConstants::KEY_ANIMATIONS].toObject());
    }
    
    if (root.contains(ThemeConstants::KEY_EFFECTS)) {
        parseEffects(root[ThemeConstants::KEY_EFFECTS].toObject());
    }
    
    if (root.contains(ThemeConstants::KEY_CHARTS)) {
        parseCharts(root[ThemeConstants::KEY_CHARTS].toObject());
    }
}

void ThemeManager::parseColorPalette(const QJsonObject& obj) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        auto colorIt = colorMap_.find(it.key().toStdString());
        if (colorIt != colorMap_.end()) {
            *colorIt->second = parseColor(it.value());
        }
    }
}

QColor ThemeManager::parseColor(const QJsonValue& value) const {
    if (value.isString()) {
        return QColor(value.toString());
    } else if (value.isObject()) {
        QJsonObject obj = value.toObject();
        int r = obj["r"].toInt(0);
        int g = obj["g"].toInt(0);
        int b = obj["b"].toInt(0);
        int a = obj["a"].toInt(255);
        return QColor(r, g, b, a);
    }
    return QColor();
}

void ThemeManager::parseTypography(const QJsonObject& obj) {
    QString baseFamily = obj["baseFamily"].toString("Segoe UI");
    QString codeFamily = obj["codeFamily"].toString("Consolas");
    typography_.setupFonts(baseFamily, codeFamily);
    
    // Apply custom sizes if specified
    if (obj.contains("sizes")) {
        QJsonObject sizes = obj["sizes"].toObject();
        if (sizes.contains("heading1")) 
            typography_.heading1.setPointSize(sizes["heading1"].toInt());
        if (sizes.contains("heading2")) 
            typography_.heading2.setPointSize(sizes["heading2"].toInt());
        if (sizes.contains("heading3")) 
            typography_.heading3.setPointSize(sizes["heading3"].toInt());
        if (sizes.contains("body")) 
            typography_.body.setPointSize(sizes["body"].toInt());
        if (sizes.contains("code")) 
            typography_.code.setPointSize(sizes["code"].toInt());
    }
}

void ThemeManager::parseComponentStyles(const QJsonObject& obj) {
    if (obj.contains("button")) {
        QJsonObject btn = obj["button"].toObject();
        componentStyles_.button.paddingHorizontal = btn["paddingHorizontal"].toInt(Design::SPACING_MD);
        componentStyles_.button.paddingVertical = btn["paddingVertical"].toInt(Design::SPACING_SM);
        componentStyles_.button.borderRadius = btn["borderRadius"].toInt(Design::RADIUS_MD);
        componentStyles_.button.borderWidth = btn["borderWidth"].toInt(1);
    }
    
    if (obj.contains("input")) {
        QJsonObject input = obj["input"].toObject();
        componentStyles_.input.paddingHorizontal = input["paddingHorizontal"].toInt(Design::SPACING_SM);
        componentStyles_.input.paddingVertical = input["paddingVertical"].toInt(Design::SPACING_SM);
        componentStyles_.input.borderRadius = input["borderRadius"].toInt(Design::RADIUS_SM);
        componentStyles_.input.borderWidth = input["borderWidth"].toInt(1);
    }
    
    if (obj.contains("card")) {
        QJsonObject card = obj["card"].toObject();
        componentStyles_.card.padding = card["padding"].toInt(Design::SPACING_MD);
        componentStyles_.card.borderRadius = card["borderRadius"].toInt(Design::RADIUS_MD);
        componentStyles_.card.borderWidth = card["borderWidth"].toInt(1);
    }
    
    if (obj.contains("message")) {
        QJsonObject msg = obj["message"].toObject();
        componentStyles_.message.padding = msg["padding"].toInt(Design::SPACING_MD);
        componentStyles_.message.borderRadius = msg["borderRadius"].toInt(Design::RADIUS_LG);
        componentStyles_.message.maxWidth = msg["maxWidth"].toInt(600);
    }
    
    if (obj.contains("chart")) {
        QJsonObject chart = obj["chart"].toObject();
        
        // Line chart properties
        componentStyles_.chart.lineWidth = chart["lineWidth"].toDouble(2.5f);
        componentStyles_.chart.pointRadius = chart["pointRadius"].toDouble(4.0f);
        componentStyles_.chart.hoverPointRadius = chart["hoverPointRadius"].toDouble(6.0f);
        componentStyles_.chart.smoothCurves = chart["smoothCurves"].toBool(true);
        componentStyles_.chart.showDataPoints = chart["showDataPoints"].toBool(true);
        componentStyles_.chart.areaOpacity = chart["areaOpacity"].toDouble(0.2f);
        
        // Bar chart properties
        componentStyles_.chart.barSpacing = chart["barSpacing"].toDouble(0.2f);
        componentStyles_.chart.barCornerRadius = chart["barCornerRadius"].toDouble(4.0f);
        componentStyles_.chart.showBarValues = chart["showBarValues"].toBool(true);
        componentStyles_.chart.barGradient = chart["barGradient"].toBool(true);
        componentStyles_.chart.barShadow = chart["barShadow"].toBool(true);
        
        // Pie/Circular chart properties
        componentStyles_.chart.innerRadiusRatio = chart["innerRadiusRatio"].toDouble(0.6f);
        componentStyles_.chart.segmentSpacing = chart["segmentSpacing"].toDouble(2.0f);
        componentStyles_.chart.hoverScale = chart["hoverScale"].toDouble(1.05f);
        componentStyles_.chart.hoverOffset = chart["hoverOffset"].toDouble(10.0f);
        
        // Heatmap properties
        componentStyles_.chart.cellSpacing = chart["cellSpacing"].toDouble(1.0f);
        componentStyles_.chart.cellCornerRadius = chart["cellCornerRadius"].toDouble(2.0f);
        
        // General properties
        componentStyles_.chart.animateOnLoad = chart["animateOnLoad"].toBool(true);
        componentStyles_.chart.animateOnUpdate = chart["animateOnUpdate"].toBool(true);
        componentStyles_.chart.animationDuration = chart["animationDuration"].toInt(800);
        componentStyles_.chart.showTooltips = chart["showTooltips"].toBool(true);
        componentStyles_.chart.showLegend = chart["showLegend"].toBool(true);
        componentStyles_.chart.glowEffects = chart["glowEffects"].toBool(true);
        componentStyles_.chart.glowRadius = chart["glowRadius"].toDouble(15.0f);
    }
    
    // Global border radius
    if (obj.contains("borderRadius")) {
        componentStyles_.borderRadius = obj["borderRadius"].toInt(8);
    }
}

void ThemeManager::updateComponentStyles() {
    if (densityMode_ == 0) {  // Compact mode
        // Reduce spacing in compact mode
        componentStyles_.button.paddingHorizontal = Design::SPACING_SM;
        componentStyles_.button.paddingVertical = Design::SPACING_XS;
        componentStyles_.input.paddingHorizontal = Design::SPACING_SM;
        componentStyles_.input.paddingVertical = Design::SPACING_XS;
        componentStyles_.card.padding = Design::SPACING_SM;
        componentStyles_.message.padding = Design::SPACING_SM;
    } else if (densityMode_ == 1) {  // Cozy mode (default)
        // Use default spacing in normal mode
        componentStyles_.button.paddingHorizontal = Design::SPACING_MD;
        componentStyles_.button.paddingVertical = Design::SPACING_SM;
        componentStyles_.input.paddingHorizontal = Design::SPACING_SM;
        componentStyles_.input.paddingVertical = Design::SPACING_SM;
        componentStyles_.card.padding = Design::SPACING_MD;
        componentStyles_.message.padding = Design::SPACING_MD;
    } else {  // Spacious mode (densityMode_ == 2)
        // Increase spacing in spacious mode
        componentStyles_.button.paddingHorizontal = Design::SPACING_LG;
        componentStyles_.button.paddingVertical = Design::SPACING_MD;
        componentStyles_.input.paddingHorizontal = Design::SPACING_MD;
        componentStyles_.input.paddingVertical = Design::SPACING_MD;
        componentStyles_.card.padding = Design::SPACING_LG;
        componentStyles_.message.padding = Design::SPACING_LG;
    }
}

void ThemeManager::applyThemeToApplication() {
    // CRITICAL: DO NOT apply theme to the entire application!
    // This is a plugin and must not affect IDA Pro's theme
    
    // Only clear component cache to force regeneration of styles
    // that will be applied to individual widgets
    componentQssCache_.clear();
    
    // The palette and stylesheet will be applied per-widget
    // using the applyThemeToWidget() method instead
    
    // Emit signal so all our widgets can update themselves
    emit themeChanged();
}

QString ThemeManager::generateQss() const {
    QString qss;
    
    qss += generateBaseQss();
    qss += generateButtonQss();
    qss += generateInputQss();
    qss += generateScrollBarQss();
    qss += generateMenuQss();
    qss += generateTabQss();
    qss += generateDockQss();
    qss += generateTreeQss();
    qss += generateToolTipQss();
    
    return qss;
}

QString ThemeManager::generateBaseQss() const {
    // IMPORTANT: Use very specific selectors to prevent any possibility
    // of affecting IDA's UI. We use direct property selectors only.
    return QString(R"(
        /* Only style widgets that have our custom property - no descendants */
        QWidget[llm_re_widget="true"] {
            font-family: "%1";
            font-size: %2px;
            background-color: %3;
            color: %4;
        }
        
        /* Direct property selectors for specific widget types */
        QLabel[llm_re_widget="true"] {
            background-color: transparent;
            color: %4;
        }
        
        QGroupBox[llm_re_widget="true"] {
            color: %4;
            border: 1px solid %5;
            border-radius: 4px;
            margin-top: 6px;
            padding-top: 6px;
        }
        
        QGroupBox[llm_re_widget="true"]::title {
            subcontrol-origin: margin;
            left: 8px;
            padding: 0 4px;
        }
        
        /* Frame styling with property selector */
        QFrame[llm_re_widget="true"] {
            background-color: %3;
            color: %4;
        }
    )").arg(typography_.body.family())
       .arg(int(typography_.body.pointSize() * fontScale_))
       .arg(colors_.background.name())
       .arg(colors_.textPrimary.name())
       .arg(colors_.border.name());
}

QString ThemeManager::generateButtonQss() const {
    return QString(R"(
        /* Only style buttons with our property directly - no descendant selectors */
        QPushButton[llm_re_widget="true"] {
            background-color: %1;
            color: %2;
            border: %3px solid %4;
            border-radius: %5px;
            padding: %6px %7px;
            font-weight: 500;
        }
        
        QPushButton[llm_re_widget="true"]:hover {
            background-color: %8;
            border-color: %9;
        }
        
        QPushButton[llm_re_widget="true"]:pressed {
            background-color: %10;
        }
        
        QPushButton[llm_re_widget="true"]:disabled {
            background-color: %11;
            color: %12;
            border-color: %13;
        }
        
        QPushButton[llm_re_widget="true"][primary="true"] {
            background-color: %14;
            color: %15;
            border: none;
        }
        
        QPushButton[llm_re_widget="true"][primary="true"]:hover {
            background-color: %16;
        }
        
        QPushButton[llm_re_widget="true"][primary="true"]:pressed {
            background-color: %17;
        }
        
        /* Tool buttons with property selector */
        QToolButton[llm_re_widget="true"] {
            background-color: transparent;
            border: none;
            padding: 4px;
            border-radius: 4px;
        }
        
        QToolButton[llm_re_widget="true"]:hover {
            background-color: %8;
        }
        
        QToolButton[llm_re_widget="true"]:pressed {
            background-color: %10;
        }
    )").arg(colors_.surface.name())
       .arg(colors_.textPrimary.name())
       .arg(componentStyles_.button.borderWidth)
       .arg(colors_.border.name())
       .arg(componentStyles_.button.borderRadius)
       .arg(componentStyles_.button.paddingVertical)
       .arg(componentStyles_.button.paddingHorizontal)
       .arg(colors_.surfaceHover.name())
       .arg(colors_.borderStrong.name())
       .arg(colors_.surfaceActive.name())
       .arg(colors_.surface.name())
       .arg(colors_.textTertiary.name())
       .arg(colors_.border.name())
       .arg(colors_.primary.name())
       .arg(colors_.textInverse.name())
       .arg(colors_.primaryHover.name())
       .arg(colors_.primaryActive.name());
}

QString ThemeManager::generateInputQss() const {
    return QString(R"(
        /* Only style input widgets with our custom property */
        QLineEdit[llm_re_widget="true"], QTextEdit[llm_re_widget="true"], 
        QPlainTextEdit[llm_re_widget="true"], QSpinBox[llm_re_widget="true"], 
        QDoubleSpinBox[llm_re_widget="true"], QComboBox[llm_re_widget="true"],
        QDateTimeEdit[llm_re_widget="true"], QDateEdit[llm_re_widget="true"], 
        QTimeEdit[llm_re_widget="true"] {
            background-color: %1;
            color: %2;
            border: %3px solid %4;
            border-radius: %5px;
            padding: %6px %7px;
            selection-background-color: %8;
            selection-color: %9;
        }
        
        QLineEdit[llm_re_widget="true"]:focus, QTextEdit[llm_re_widget="true"]:focus, 
        QPlainTextEdit[llm_re_widget="true"]:focus, QSpinBox[llm_re_widget="true"]:focus, 
        QDoubleSpinBox[llm_re_widget="true"]:focus, QComboBox[llm_re_widget="true"]:focus,
        QDateTimeEdit[llm_re_widget="true"]:focus, QDateEdit[llm_re_widget="true"]:focus, 
        QTimeEdit[llm_re_widget="true"]:focus {
            border-color: %10;
            outline: none;
        }
        
        QLineEdit[llm_re_widget="true"]:disabled, QTextEdit[llm_re_widget="true"]:disabled, 
        QPlainTextEdit[llm_re_widget="true"]:disabled, QSpinBox[llm_re_widget="true"]:disabled, 
        QDoubleSpinBox[llm_re_widget="true"]:disabled, QComboBox[llm_re_widget="true"]:disabled,
        QDateTimeEdit[llm_re_widget="true"]:disabled, QDateEdit[llm_re_widget="true"]:disabled, 
        QTimeEdit[llm_re_widget="true"]:disabled {
            background-color: %11;
            color: %12;
        }
        
        QComboBox[llm_re_widget="true"]::drop-down {
            border: none;
            width: 20px;
        }
        
        QComboBox[llm_re_widget="true"]::down-arrow {
            image: none;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-top: 5px solid %2;
            margin-right: 5px;
        }
        
        QComboBox[llm_re_widget="true"] QAbstractItemView {
            background-color: %1;
            border: 1px solid %4;
            selection-background-color: %8;
            outline: none;
        }
    )").arg(colors_.surface.name())
       .arg(colors_.textPrimary.name())
       .arg(componentStyles_.input.borderWidth)
       .arg(colors_.border.name())
       .arg(componentStyles_.input.borderRadius)
       .arg(componentStyles_.input.paddingVertical)
       .arg(componentStyles_.input.paddingHorizontal)
       .arg(colors_.selection.name())
       .arg(colors_.textPrimary.name())
       .arg(colors_.primary.name())
       .arg(colors_.surfaceHover.name())
       .arg(colors_.textTertiary.name());
}

QString ThemeManager::generateScrollBarQss() const {
    return QString(R"(
        /* Style scrollbars with our property directly */
        QScrollBar[llm_re_widget="true"]:vertical {
            background-color: %1;
            width: 12px;
            border: none;
        }
        
        QScrollBar[llm_re_widget="true"]::handle:vertical {
            background-color: %2;
            border-radius: 6px;
            min-height: 20px;
            margin: 2px;
        }
        
        QScrollBar[llm_re_widget="true"]::handle:vertical:hover {
            background-color: %3;
        }
        
        QScrollBar[llm_re_widget="true"]::add-line:vertical,
        QScrollBar[llm_re_widget="true"]::sub-line:vertical {
            height: 0px;
        }
        
        QScrollBar[llm_re_widget="true"]:horizontal {
            background-color: %1;
            height: 12px;
            border: none;
        }
        
        QScrollBar[llm_re_widget="true"]::handle:horizontal {
            background-color: %2;
            border-radius: 6px;
            min-width: 20px;
            margin: 2px;
        }
        
        QScrollBar[llm_re_widget="true"]::handle:horizontal:hover {
            background-color: %3;
        }
        
        QScrollBar[llm_re_widget="true"]::add-line:horizontal,
        QScrollBar[llm_re_widget="true"]::sub-line:horizontal {
            width: 0px;
        }
    )").arg(colors_.background.name())
       .arg(colors_.border.name())
       .arg(colors_.borderStrong.name());
}

QString ThemeManager::generateMenuQss() const {
    return QString(R"(
        /* Only style menus with our custom property */
        QMenuBar[llm_re_widget="true"] {
            background-color: %1;
            color: %2;
            border-bottom: 1px solid %3;
        }
        
        QMenuBar[llm_re_widget="true"]::item:selected {
            background-color: %4;
        }
        
        QMenu[llm_re_widget="true"] {
            background-color: %5;
            color: %2;
            border: 1px solid %3;
            padding: 4px;
        }
        
        QMenu[llm_re_widget="true"]::item {
            padding: 6px 20px;
            border-radius: 4px;
        }
        
        QMenu[llm_re_widget="true"]::item:selected {
            background-color: %4;
        }
        
        QMenu[llm_re_widget="true"]::separator {
            height: 1px;
            background-color: %3;
            margin: 4px 10px;
        }
    )").arg(colors_.surface.name())
       .arg(colors_.textPrimary.name())
       .arg(colors_.border.name())
       .arg(colors_.surfaceHover.name())
       .arg(colors_.surface.name());
}

QString ThemeManager::generateTabQss() const {
    return QString(R"(
        /* Only style tab widgets with our custom property */
        QTabWidget[llm_re_widget="true"]::pane {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 4px;
        }
        
        QTabWidget[llm_re_widget="true"]::tab-bar {
            left: 0px;
        }
        
        QTabBar[llm_re_widget="true"]::tab {
            background-color: %3;
            color: %4;
            padding: 8px 16px;
            margin-right: 2px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        
        QTabBar[llm_re_widget="true"]::tab:selected {
            background-color: %1;
            color: %5;
        }
        
        QTabBar[llm_re_widget="true"]::tab:hover:!selected {
            background-color: %6;
        }
    )").arg(colors_.surface.name())
       .arg(colors_.border.name())
       .arg(colors_.background.name())
       .arg(colors_.textSecondary.name())
       .arg(colors_.textPrimary.name())
       .arg(colors_.surfaceHover.name());
}

QString ThemeManager::generateDockQss() const {
    return QString(R"(
        /* Only style dock widgets with our custom property */
        QDockWidget[llm_re_widget="true"] {
            color: %1;
        }
        
        QDockWidget[llm_re_widget="true"]::title {
            background-color: %2;
            padding: 6px;
            border-bottom: 1px solid %3;
        }
        
        QDockWidget[llm_re_widget="true"]::close-button, 
        QDockWidget[llm_re_widget="true"]::float-button {
            background: transparent;
            border: none;
            padding: 2px;
        }
        
        QDockWidget[llm_re_widget="true"]::close-button:hover, 
        QDockWidget[llm_re_widget="true"]::float-button:hover {
            background-color: %4;
            border-radius: 2px;
        }
    )").arg(colors_.textPrimary.name())
       .arg(colors_.surface.name())
       .arg(colors_.border.name())
       .arg(colors_.surfaceHover.name());
}

QString ThemeManager::generateTreeQss() const {
    return QString(R"(
        /* Only style tree/list widgets with our custom property */
        QTreeView[llm_re_widget="true"], QTreeWidget[llm_re_widget="true"], 
        QListView[llm_re_widget="true"], QListWidget[llm_re_widget="true"] {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            outline: none;
            selection-background-color: %4;
        }
        
        QTreeView[llm_re_widget="true"]::item, QTreeWidget[llm_re_widget="true"]::item, 
        QListView[llm_re_widget="true"]::item, QListWidget[llm_re_widget="true"]::item {
            padding: 4px;
            border-radius: 4px;
        }
        
        QTreeView[llm_re_widget="true"]::item:hover, QTreeWidget[llm_re_widget="true"]::item:hover, 
        QListView[llm_re_widget="true"]::item:hover, QListWidget[llm_re_widget="true"]::item:hover {
            background-color: %5;
        }
        
        QTreeView[llm_re_widget="true"]::item:selected, QTreeWidget[llm_re_widget="true"]::item:selected,
        QListView[llm_re_widget="true"]::item:selected, QListWidget[llm_re_widget="true"]::item:selected {
            background-color: %4;
        }
        
        QTreeView[llm_re_widget="true"]::branch {
            background-color: %1;
        }
        
        QTreeView[llm_re_widget="true"]::branch:has-children:closed {
            image: none;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-top: 5px solid %2;
        }
        
        QTreeView[llm_re_widget="true"]::branch:has-children:open {
            image: none;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-bottom: 5px solid %2;
        }
        
        QHeaderView[llm_re_widget="true"]::section {
            background-color: %6;
            color: %2;
            padding: 6px;
            border: none;
            border-right: 1px solid %3;
            border-bottom: 1px solid %3;
        }
    )").arg(colors_.surface.name())
       .arg(colors_.textPrimary.name())
       .arg(colors_.border.name())
       .arg(colors_.selection.name())
       .arg(colors_.surfaceHover.name())
       .arg(colors_.background.name());
}

QString ThemeManager::generateToolTipQss() const {
    return QString(R"(
        QToolTip {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 4px;
            padding: 4px 8px;
        }
    )").arg(colors_.surface.name())
       .arg(colors_.textPrimary.name())
       .arg(colors_.border.name());
}

QColor ThemeManager::color(const QString& colorName) const {
    auto it = colorMap_.find(colorName.toStdString());
    if (it != colorMap_.end() && it->second) {
        return *it->second;
    }
    return QColor();
}

const std::vector<QColor>& ThemeManager::chartSeriesColors() const {
    auto currentInfo = getCurrentThemeInfo();
    bool isDark = (currentInfo.name == "dark" || currentInfo.metadata.baseTheme == "dark");
    return isDark ? colors_.chartSeriesColorsDark : colors_.chartSeriesColorsLight;
}

QColor ThemeManager::chartSeriesColor(int index) const {
    const auto& colors = chartSeriesColors();
    if (colors.empty()) return QColor();
    return colors[index % colors.size()];
}

void ThemeManager::setChartStyle(ChartStyle style) {
    chartStyle_ = style;
    
    // Update component styles based on chart style
    switch (style) {
    case ChartStyle::Modern:
        componentStyles_.chart.lineWidth = 2.5f;
        componentStyles_.chart.glowEffects = false;
        componentStyles_.chart.animationDuration = 600;
        componentStyles_.chart.barGradient = false;
        break;
        
    case ChartStyle::Neon:
        componentStyles_.chart.lineWidth = 3.0f;
        componentStyles_.chart.glowEffects = true;
        componentStyles_.chart.glowRadius = 20.0f;
        componentStyles_.chart.animationDuration = 1000;
        componentStyles_.chart.barGradient = true;
        break;
        
    case ChartStyle::Corporate:
        componentStyles_.chart.lineWidth = 2.0f;
        componentStyles_.chart.glowEffects = false;
        componentStyles_.chart.animationDuration = 400;
        componentStyles_.chart.barGradient = false;
        componentStyles_.chart.barShadow = false;
        break;
        
    case ChartStyle::Playful:
        componentStyles_.chart.lineWidth = 3.5f;
        componentStyles_.chart.pointRadius = 6.0f;
        componentStyles_.chart.animationDuration = 1200;
        componentStyles_.chart.barCornerRadius = 8.0f;
        componentStyles_.chart.segmentSpacing = 4.0f;
        break;
        
    case ChartStyle::Terminal:
        componentStyles_.chart.lineWidth = 1.0f;
        componentStyles_.chart.smoothCurves = false;
        componentStyles_.chart.glowEffects = false;
        componentStyles_.chart.animationDuration = 0;
        componentStyles_.chart.barGradient = false;
        componentStyles_.chart.barShadow = false;
        break;
        
    case ChartStyle::Glass:
        componentStyles_.chart.lineWidth = 2.0f;
        componentStyles_.chart.areaOpacity = 0.1f;
        componentStyles_.chart.glowEffects = true;
        componentStyles_.chart.glowRadius = 30.0f;
        componentStyles_.chart.animationDuration = 800;
        break;
    }
    
    emit themeChanged();
}

void ThemeManager::setAccentColor(const QColor& color) {
    colors_.primary = color;
    colors_.primaryHover = lighten(color, 20);
    colors_.primaryActive = darken(color, 20);
    colors_.textLink = color;
    
    applyThemeToApplication();
    emit colorsChanged();
}

void ThemeManager::setFontScale(qreal scale) {
    fontScale_ = scale;
    // Reapply fonts with new scale
    emit fontsChanged();
    applyThemeToApplication();
}

void ThemeManager::setDensityMode(int mode) {
    densityMode_ = mode;
    updateComponentStyles();
    applyThemeToApplication();
    emit themeChanged();
}

void ThemeManager::enableHotReload(bool enable) {
    hotReloadEnabled_ = enable;
    
    if (enable && !currentThemeInfo_.isBuiltIn && !currentThemeInfo_.filePath.isEmpty()) {
        if (!fileWatcher_) {
            fileWatcher_ = std::make_unique<QFileSystemWatcher>();
            connect(fileWatcher_.get(), &QFileSystemWatcher::fileChanged,
                    this, &ThemeManager::onThemeFileChanged);
        }
        fileWatcher_->addPath(currentThemeInfo_.filePath);
    } else if (fileWatcher_) {
        fileWatcher_.reset();
    }
}

void ThemeManager::onThemeFileChanged(const QString& path) {
    // Check if this is the current theme file
    if (path != currentThemeInfo_.filePath) {
        return;
    }
    
    // If we have unsaved changes, notify user but don't reload
    if (currentThemeModified_) {
        emit errorOccurred(QString("Theme file '%1' was modified externally but you have unsaved changes. "
                                  "Save or discard your changes to sync with the external file.")
                          .arg(QFileInfo(path).fileName()));
        return;
    }
    
    if (hotReloadEnabled_) {
        // Reload the theme
        loadThemeFromFile(path);
        applyThemeToApplication();
        emit themeChanged();
        emit colorsChanged();
        
        // Re-add the file to the watcher (it gets removed after change)
        fileWatcher_->addPath(path);
        
        // Notify user
        emit errorOccurred(QString("Theme file '%1' was modified externally and reloaded.")
                          .arg(QFileInfo(path).fileName()));
    } else {
        // Just notify user
        emit errorOccurred(QString("Theme file '%1' was modified externally. "
                                  "Enable hot reload or reload the theme manually to see changes.")
                          .arg(QFileInfo(path).fileName()));
    }
}

QString ThemeManager::componentQss(const QString& componentName) const {
    // Check cache first
    auto it = componentQssCache_.find(componentName);
    if (it != componentQssCache_.end()) {
        return it->second;
    }
    
    // Generate component-specific QSS
    QString qss;
    
    if (componentName == "MessageBubble") {
        qss = QString(R"(
            .MessageBubble {
                background-color: %1;
                border-radius: %2px;
                padding: %3px;
            }
            
            .MessageBubble[role="user"] {
                background-color: %4;
                margin-left: 60px;
            }
            
            .MessageBubble[role="assistant"] {
                background-color: %5;
                margin-right: 60px;
            }
            
            .MessageBubble[role="system"] {
                background-color: %6;
                border: 1px solid %7;
            }
        )").arg(colors_.surface.name())
           .arg(componentStyles_.message.borderRadius)
           .arg(componentStyles_.message.padding)
           .arg(colors_.userMessage.name())
           .arg(colors_.assistantMessage.name())
           .arg(colors_.systemMessage.name())
           .arg(colors_.border.name());
    }
    else if (componentName == "Button") {
        qss = generateButtonQss();
    }
    else if (componentName == "Input") {
        qss = generateInputQss();
    }
    else if (componentName == "Card") {
        qss = QString(R"(
            .Card {
                background-color: %1;
                border: %2px solid %3;
                border-radius: %4px;
                padding: %5px;
            }
            
            .Card:hover {
                background-color: %6;
                border-color: %7;
            }
        )").arg(colors_.surface.name())
           .arg(componentStyles_.card.borderWidth)
           .arg(colors_.border.name())
           .arg(componentStyles_.card.borderRadius)
           .arg(componentStyles_.card.padding)
           .arg(colors_.surfaceHover.name())
           .arg(colors_.borderStrong.name());
    }
    else if (componentName == "ScrollBar") {
        qss = generateScrollBarQss();
    }
    else if (componentName == "Menu") {
        qss = generateMenuQss();
    }
    else if (componentName == "Tab") {
        qss = generateTabQss();
    }
    else if (componentName == "Dock") {
        qss = generateDockQss();
    }
    else if (componentName == "Tree") {
        qss = generateTreeQss();
    }
    else if (componentName == "ToolTip") {
        qss = generateToolTipQss();
    }
    
    // Cache the result
    const_cast<ThemeManager*>(this)->componentQssCache_[componentName] = qss;
    return qss;
}

QString ThemeManager::themedIconPath(const QString& iconName) const {
    auto currentInfo = getCurrentThemeInfo();
    bool isLight = (currentInfo.name == "light" || currentInfo.metadata.baseTheme == "light");
    QString themeSuffix = isLight ? "_light" : "_dark";
    return QString(":/icons/%1%2.svg").arg(iconName).arg(themeSuffix);
}

QIcon ThemeManager::themedIcon(const QString& iconName) const {
    return QIcon(themedIconPath(iconName));
}

// Static utility functions
QColor ThemeManager::adjustAlpha(const QColor& color, int alpha) {
    QColor result = color;
    result.setAlpha(alpha);
    return result;
}

QColor ThemeManager::lighten(const QColor& color, int amount) {
    return color.lighter(100 + amount);
}

QColor ThemeManager::darken(const QColor& color, int amount) {
    return color.darker(100 + amount);
}

QColor ThemeManager::mix(const QColor& color1, const QColor& color2, qreal ratio) {
    int r = color1.red() * ratio + color2.red() * (1 - ratio);
    int g = color1.green() * ratio + color2.green() * (1 - ratio);
    int b = color1.blue() * ratio + color2.blue() * (1 - ratio);
    int a = color1.alpha() * ratio + color2.alpha() * (1 - ratio);
    return QColor(r, g, b, a);
}

void ThemeManager::applyThemeToWidget(QWidget* widget) {
    if (!widget) return;
    
    // Mark this widget as a plugin widget so our styles apply to it
    widget->setProperty("llm_re_widget", true);
    
    // Prevent Qt from using the application style for background
    widget->setAttribute(Qt::WA_StyledBackground, false);
    widget->setAutoFillBackground(false);
    
    // CRITICAL: Do NOT use setPalette as it can propagate to child widgets
    // and potentially affect IDA's UI. Use only CSS/QSS for styling.
    // widget->setPalette(widgetPalette());  // REMOVED to prevent theme bleeding
    
    // IMPORTANT: Apply styles only to this specific widget, not all styles globally
    // This prevents our styles from cascading to unintended widgets
    QString widgetQss;
    
    // Apply only the base styles to the widget itself
    if (qobject_cast<QMainWindow*>(widget)) {
        // For main window, apply base styles only
        widgetQss = QString(R"(
            #%1 {
                background-color: %2;
                color: %3;
                font-family: "%4";
                font-size: %5px;
            }
        )").arg(widget->objectName())
           .arg(colors_.background.name())
           .arg(colors_.textPrimary.name())
           .arg(typography_.body.family())
           .arg(int(typography_.body.pointSize() * fontScale_));
    } else {
        // For other widgets, apply the full styles but scoped to this widget
        widgetQss = generateQss();
    }
    
    widget->setStyleSheet(widgetQss);
    
    // Mark direct children only - don't use findChildren which gets all descendants
    for (QObject* child : widget->children()) {
        if (QWidget* childWidget = qobject_cast<QWidget*>(child)) {
            childWidget->setProperty("llm_re_widget", true);
            childWidget->setAttribute(Qt::WA_StyledBackground, false);
            childWidget->setAutoFillBackground(false);
        }
    }
    
    // CRITICAL: Do NOT use style()->polish/unpolish as it causes IDA's theme to be reapplied!
    // The stylesheet alone is sufficient for styling our widgets.
    // widget->style()->unpolish(widget);  // REMOVED - was causing theme bleeding
    // widget->style()->polish(widget);    // REMOVED - was causing theme bleeding
}

QPalette ThemeManager::widgetPalette() const {
    QPalette palette;
    palette.setColor(QPalette::Window, colors_.background);
    palette.setColor(QPalette::WindowText, colors_.textPrimary);
    palette.setColor(QPalette::Base, colors_.surface);
    palette.setColor(QPalette::AlternateBase, colors_.surfaceHover);
    palette.setColor(QPalette::Text, colors_.textPrimary);
    palette.setColor(QPalette::BrightText, colors_.textPrimary);
    palette.setColor(QPalette::Button, colors_.surface);
    palette.setColor(QPalette::ButtonText, colors_.textPrimary);
    palette.setColor(QPalette::Highlight, colors_.selection);
    palette.setColor(QPalette::HighlightedText, colors_.textPrimary);
    palette.setColor(QPalette::Link, colors_.textLink);
    
    return palette;
}

QString ThemeManager::themesDirectory() {
    std::string idaDir = get_user_idadir();
    return QString::fromStdString(idaDir) + "/" + ThemeConstants::THEME_DIR_NAME;
}

void ThemeManager::ensureThemesDirectory() {
    QDir dir(themesDirectory());
    if (!dir.exists()) {
        dir.mkpath(".");
    }
}


ThemeError ThemeManager::validateThemeFile(const QString& filePath) const {
    QFile file(filePath);
    if (!file.exists()) {
        return ThemeError::FileNotFound;
    }
    
    if (!file.open(QIODevice::ReadOnly)) {
        return ThemeError::FileNotFound;
    }
    
    QByteArray data = file.readAll();
    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    
    if (parseError.error != QJsonParseError::NoError) {
        qWarning() << "JSON parse error in theme file:" << filePath 
                   << "Error:" << parseError.errorString() 
                   << "at offset" << parseError.offset;
        return ThemeError::InvalidFormat;
    }
    
    if (!doc.isObject()) {
        return ThemeError::InvalidFormat;
    }
    
    QJsonObject root = doc.object();
    
    // Check for required sections
    if (!root.contains(ThemeConstants::KEY_METADATA)) {
        return ThemeError::MissingMetadata;
    }
    
    if (!root.contains(ThemeConstants::KEY_COLORS)) {
        return ThemeError::InvalidColors;
    }
    
    // Validate metadata
    QJsonObject metadata = root[ThemeConstants::KEY_METADATA].toObject();
    if (!metadata.contains(ThemeConstants::META_NAME) || 
        !metadata.contains(ThemeConstants::META_VERSION)) {
        return ThemeError::MissingMetadata;
    }
    
    // Validate colors
    QJsonObject colors = root[ThemeConstants::KEY_COLORS].toObject();
    
    // Check essential colors exist and are valid
    QStringList essentialColors = {
        "primary", "background", "surface", "textPrimary", "textSecondary",
        "border", "success", "warning", "error"
    };
    
    for (const QString& colorName : essentialColors) {
        if (!colors.contains(colorName)) {
            qWarning() << "Missing essential color in theme:" << colorName;
            // Don't fail validation, just warn - we can use defaults
        } else {
            QString colorValue = colors[colorName].toString();
            if (!QColor::isValidColor(colorValue)) {
                qWarning() << "Invalid color value for" << colorName << ":" << colorValue;
                return ThemeError::InvalidColors;
            }
        }
    }
    
    return ThemeError::None;
}


void ThemeManager::setColor(const QString& colorName, const QColor& color) {
    if (colorMap_.count(colorName.toStdString())) {
        *colorMap_[colorName.toStdString()] = color;
        markModified();  // Mark theme as modified
        emit colorsChanged();
        
        if (hotReloadEnabled_) {
            applyThemeToApplication();
        }
    }
}

void ThemeManager::setTypography(const Typography& typography) {
    typography_ = typography;
    markModified();  // Mark theme as modified
    emit fontsChanged();
    
    if (hotReloadEnabled_) {
        applyThemeToApplication();
    }
}

void ThemeManager::setCurrentThemeMetadata(const ThemeMetadata& metadata) {
    currentThemeInfo_.metadata = metadata;
    markModified();  // Mark theme as modified
}

void ThemeManager::setCornerRadius(int radius) {
    componentStyles_.borderRadius = radius;
    markModified();  // Mark theme as modified
    
    if (hotReloadEnabled_) {
        applyThemeToApplication();
    }
}

int ThemeManager::cornerRadius() const {
    return componentStyles_.borderRadius;
}


void ThemeManager::parseMetadata(const QJsonObject& obj) {
    currentThemeInfo_.metadata.name = obj[ThemeConstants::META_NAME].toString();
    currentThemeInfo_.metadata.author = obj[ThemeConstants::META_AUTHOR].toString();
    currentThemeInfo_.metadata.version = obj[ThemeConstants::META_VERSION].toString();
    currentThemeInfo_.metadata.description = obj[ThemeConstants::META_DESCRIPTION].toString();
    currentThemeInfo_.metadata.baseTheme = obj[ThemeConstants::META_BASE_THEME].toString();
    
    if (obj.contains(ThemeConstants::META_CREATED_DATE)) {
        currentThemeInfo_.metadata.createdDate = QDateTime::fromString(
            obj[ThemeConstants::META_CREATED_DATE].toString(), Qt::ISODate);
    }
    
    if (obj.contains(ThemeConstants::META_MODIFIED_DATE)) {
        currentThemeInfo_.metadata.modifiedDate = QDateTime::fromString(
            obj[ThemeConstants::META_MODIFIED_DATE].toString(), Qt::ISODate);
    }
}

void ThemeManager::parseAnimations(const QJsonObject& obj) {
    auto& am = AnimationManager::instance();
    
    if (obj.contains("enabled")) {
        am.setAnimationsEnabled(obj["enabled"].toBool());
    }
    
    if (obj.contains("globalSpeed")) {
        am.setGlobalSpeed(obj["globalSpeed"].toDouble());
    }
    
    // Parse individual animation settings if needed
}

void ThemeManager::parseEffects(const QJsonObject& obj) {
    auto& em = EffectsManager::instance();
    
    if (obj.contains("enabled")) {
        em.setEffectsEnabled(obj["enabled"].toBool());
    }
    
    if (obj.contains("quality")) {
        em.setEffectQuality(obj["quality"].toInt());
    }
    
    // Parse individual effect settings if needed
}

void ThemeManager::parseCharts(const QJsonObject& obj) {
    if (obj.contains("style")) {
        QString styleStr = obj["style"].toString();
        if (styleStr == "modern") setChartStyle(ChartStyle::Modern);
        else if (styleStr == "neon") setChartStyle(ChartStyle::Neon);
        else if (styleStr == "corporate") setChartStyle(ChartStyle::Corporate);
        else if (styleStr == "playful") setChartStyle(ChartStyle::Playful);
        else if (styleStr == "terminal") setChartStyle(ChartStyle::Terminal);
        else if (styleStr == "glass") setChartStyle(ChartStyle::Glass);
    }
    
    // Parse individual chart properties
    if (obj.contains("properties")) {
        QJsonObject props = obj["properties"].toObject();
        
        // Line chart properties
        if (props.contains("lineWidth"))
            componentStyles_.chart.lineWidth = props["lineWidth"].toDouble();
        if (props.contains("pointRadius"))
            componentStyles_.chart.pointRadius = props["pointRadius"].toDouble();
        if (props.contains("hoverPointRadius"))
            componentStyles_.chart.hoverPointRadius = props["hoverPointRadius"].toDouble();
        if (props.contains("smoothCurves"))
            componentStyles_.chart.smoothCurves = props["smoothCurves"].toBool();
        if (props.contains("showDataPoints"))
            componentStyles_.chart.showDataPoints = props["showDataPoints"].toBool();
        if (props.contains("areaOpacity"))
            componentStyles_.chart.areaOpacity = props["areaOpacity"].toDouble();
            
        // Bar chart properties
        if (props.contains("barSpacing"))
            componentStyles_.chart.barSpacing = props["barSpacing"].toDouble();
        if (props.contains("barCornerRadius"))
            componentStyles_.chart.barCornerRadius = props["barCornerRadius"].toDouble();
        if (props.contains("showBarValues"))
            componentStyles_.chart.showBarValues = props["showBarValues"].toBool();
        if (props.contains("barGradient"))
            componentStyles_.chart.barGradient = props["barGradient"].toBool();
        if (props.contains("barShadow"))
            componentStyles_.chart.barShadow = props["barShadow"].toBool();
            
        // Pie/Circular chart properties
        if (props.contains("innerRadiusRatio"))
            componentStyles_.chart.innerRadiusRatio = props["innerRadiusRatio"].toDouble();
        if (props.contains("segmentSpacing"))
            componentStyles_.chart.segmentSpacing = props["segmentSpacing"].toDouble();
        if (props.contains("hoverScale"))
            componentStyles_.chart.hoverScale = props["hoverScale"].toDouble();
        if (props.contains("hoverOffset"))
            componentStyles_.chart.hoverOffset = props["hoverOffset"].toDouble();
            
        // Heatmap properties
        if (props.contains("cellSpacing"))
            componentStyles_.chart.cellSpacing = props["cellSpacing"].toDouble();
        if (props.contains("cellCornerRadius"))
            componentStyles_.chart.cellCornerRadius = props["cellCornerRadius"].toDouble();
            
        // General properties
        if (props.contains("animateOnLoad"))
            componentStyles_.chart.animateOnLoad = props["animateOnLoad"].toBool();
        if (props.contains("animateOnUpdate"))
            componentStyles_.chart.animateOnUpdate = props["animateOnUpdate"].toBool();
        if (props.contains("animationDuration"))
            componentStyles_.chart.animationDuration = props["animationDuration"].toInt();
        if (props.contains("showTooltips"))
            componentStyles_.chart.showTooltips = props["showTooltips"].toBool();
        if (props.contains("showLegend"))
            componentStyles_.chart.showLegend = props["showLegend"].toBool();
        if (props.contains("glowEffects"))
            componentStyles_.chart.glowEffects = props["glowEffects"].toBool();
        if (props.contains("glowRadius"))
            componentStyles_.chart.glowRadius = props["glowRadius"].toDouble();
    }
}

QJsonObject ThemeManager::generateThemeJson() const {
    QJsonObject root;
    
    root[ThemeConstants::KEY_COLORS] = generateColorsJson();
    root[ThemeConstants::KEY_TYPOGRAPHY] = generateTypographyJson();
    root[ThemeConstants::KEY_COMPONENTS] = generateComponentsJson();
    root[ThemeConstants::KEY_ANIMATIONS] = generateAnimationsJson();
    root[ThemeConstants::KEY_EFFECTS] = generateEffectsJson();
    root[ThemeConstants::KEY_CHARTS] = generateChartsJson();
    
    return root;
}

QJsonObject ThemeManager::generateMetadataJson(const ThemeMetadata& metadata) const {
    QJsonObject obj;
    
    obj[ThemeConstants::META_NAME] = metadata.name;
    obj[ThemeConstants::META_AUTHOR] = metadata.author;
    obj[ThemeConstants::META_VERSION] = metadata.version;
    obj[ThemeConstants::META_DESCRIPTION] = metadata.description;
    obj[ThemeConstants::META_BASE_THEME] = metadata.baseTheme;
    obj[ThemeConstants::META_CREATED_DATE] = QDateTime::currentDateTime().toString(Qt::ISODate);
    obj[ThemeConstants::META_MODIFIED_DATE] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    return obj;
}

QJsonObject ThemeManager::generateColorsJson() const {
    QJsonObject colors;
    
    // Generate color entries for all colors in the palette
    for (const auto& [name, colorPtr] : colorMap_) {
        if (colorPtr) {
            colors[QString::fromStdString(name)] = colorPtr->name();
        }
    }
    
    // Chart series colors need special handling
    QJsonArray darkSeriesColors;
    for (const auto& color : colors_.chartSeriesColorsDark) {
        darkSeriesColors.append(color.name());
    }
    colors["chartSeriesColorsDark"] = darkSeriesColors;
    
    QJsonArray lightSeriesColors;
    for (const auto& color : colors_.chartSeriesColorsLight) {
        lightSeriesColors.append(color.name());
    }
    colors["chartSeriesColorsLight"] = lightSeriesColors;
    
    return colors;
}

QJsonObject ThemeManager::generateTypographyJson() const {
    QJsonObject typography;
    
    typography["baseFamily"] = typography_.body.family();
    typography["codeFamily"] = typography_.code.family();
    
    QJsonObject sizes;
    sizes["heading1"] = typography_.heading1.pointSize();
    sizes["heading2"] = typography_.heading2.pointSize();
    sizes["heading3"] = typography_.heading3.pointSize();
    sizes["body"] = typography_.body.pointSize();
    sizes["bodySmall"] = typography_.bodySmall.pointSize();
    sizes["code"] = typography_.code.pointSize();
    sizes["caption"] = typography_.caption.pointSize();
    
    typography["sizes"] = sizes;
    typography["scale"] = fontScale_;
    
    return typography;
}

QJsonObject ThemeManager::generateComponentsJson() const {
    QJsonObject components;
    
    components["density"] = densityMode_;
    
    // Button styles
    QJsonObject button;
    button["paddingHorizontal"] = componentStyles_.button.paddingHorizontal;
    button["paddingVertical"] = componentStyles_.button.paddingVertical;
    button["borderRadius"] = componentStyles_.button.borderRadius;
    button["borderWidth"] = componentStyles_.button.borderWidth;
    components["button"] = button;
    
    // Input styles
    QJsonObject input;
    input["paddingHorizontal"] = componentStyles_.input.paddingHorizontal;
    input["paddingVertical"] = componentStyles_.input.paddingVertical;
    input["borderRadius"] = componentStyles_.input.borderRadius;
    input["borderWidth"] = componentStyles_.input.borderWidth;
    components["input"] = input;
    
    // Card styles
    QJsonObject card;
    card["padding"] = componentStyles_.card.padding;
    card["borderRadius"] = componentStyles_.card.borderRadius;
    card["borderWidth"] = componentStyles_.card.borderWidth;
    components["card"] = card;
    
    // Message styles
    QJsonObject message;
    message["padding"] = componentStyles_.message.padding;
    message["borderRadius"] = componentStyles_.message.borderRadius;
    message["maxWidth"] = componentStyles_.message.maxWidth;
    components["message"] = message;
    
    // Chart styles
    QJsonObject chart;
    // Line chart properties
    chart["lineWidth"] = componentStyles_.chart.lineWidth;
    chart["pointRadius"] = componentStyles_.chart.pointRadius;
    chart["hoverPointRadius"] = componentStyles_.chart.hoverPointRadius;
    chart["smoothCurves"] = componentStyles_.chart.smoothCurves;
    chart["showDataPoints"] = componentStyles_.chart.showDataPoints;
    chart["areaOpacity"] = componentStyles_.chart.areaOpacity;
    
    // Bar chart properties
    chart["barSpacing"] = componentStyles_.chart.barSpacing;
    chart["barCornerRadius"] = componentStyles_.chart.barCornerRadius;
    chart["showBarValues"] = componentStyles_.chart.showBarValues;
    chart["barGradient"] = componentStyles_.chart.barGradient;
    chart["barShadow"] = componentStyles_.chart.barShadow;
    
    // Pie/Circular chart properties
    chart["innerRadiusRatio"] = componentStyles_.chart.innerRadiusRatio;
    chart["segmentSpacing"] = componentStyles_.chart.segmentSpacing;
    chart["hoverScale"] = componentStyles_.chart.hoverScale;
    chart["hoverOffset"] = componentStyles_.chart.hoverOffset;
    
    // Heatmap properties
    chart["cellSpacing"] = componentStyles_.chart.cellSpacing;
    chart["cellCornerRadius"] = componentStyles_.chart.cellCornerRadius;
    
    // General properties
    chart["animateOnLoad"] = componentStyles_.chart.animateOnLoad;
    chart["animateOnUpdate"] = componentStyles_.chart.animateOnUpdate;
    chart["animationDuration"] = componentStyles_.chart.animationDuration;
    chart["showTooltips"] = componentStyles_.chart.showTooltips;
    chart["showLegend"] = componentStyles_.chart.showLegend;
    chart["glowEffects"] = componentStyles_.chart.glowEffects;
    chart["glowRadius"] = componentStyles_.chart.glowRadius;
    components["chart"] = chart;
    
    // Global border radius
    components["borderRadius"] = componentStyles_.borderRadius;
    
    return components;
}

QJsonObject ThemeManager::generateAnimationsJson() const {
    QJsonObject animations;
    
    const auto& am = AnimationManager::instance();
    animations["enabled"] = am.animationsEnabled();
    animations["globalSpeed"] = am.globalSpeed();
    
    // Add individual animation configurations if needed
    
    return animations;
}

QJsonObject ThemeManager::generateEffectsJson() const {
    QJsonObject effects;
    
    const auto& em = EffectsManager::instance();
    effects["enabled"] = em.effectsEnabled();
    effects["quality"] = em.effectQuality();
    
    // Add individual effect configurations if needed
    
    return effects;
}

QJsonObject ThemeManager::generateChartsJson() const {
    QJsonObject charts;
    
    // Convert chart style enum to string
    QString styleStr;
    switch (chartStyle_) {
        case ChartStyle::Modern: styleStr = "modern"; break;
        case ChartStyle::Neon: styleStr = "neon"; break;
        case ChartStyle::Corporate: styleStr = "corporate"; break;
        case ChartStyle::Playful: styleStr = "playful"; break;
        case ChartStyle::Terminal: styleStr = "terminal"; break;
        case ChartStyle::Glass: styleStr = "glass"; break;
    }
    charts["style"] = styleStr;
    
    // Chart properties
    QJsonObject properties;
    properties["lineWidth"] = componentStyles_.chart.lineWidth;
    properties["animationDuration"] = componentStyles_.chart.animationDuration;
    properties["glowEffects"] = componentStyles_.chart.glowEffects;
    // ... add other properties
    
    charts["properties"] = properties;
    
    return charts;
}

// ============================================================================
// NEW ARCHITECTURE IMPLEMENTATION
// ============================================================================

ThemeManager::ThemeInfo ThemeManager::createNewTheme(const QString& basedOn) {
    // Load the base theme first
    if (!loadTheme(basedOn)) {
        loadTheme("dark");  // Fallback to dark
    }
    
    // Create new theme info
    ThemeInfo info;
    info.name = "";  // Will be set on save
    info.displayName = "Unsaved Theme";
    info.isBuiltIn = false;
    info.isModified = true;
    info.metadata.name = "New Theme";
    info.metadata.author = "";
    info.metadata.version = "1.0";
    info.metadata.description = "";
    info.metadata.baseTheme = basedOn;
    info.metadata.createdDate = QDateTime::currentDateTime();
    info.metadata.modifiedDate = QDateTime::currentDateTime();
    
    currentThemeInfo_ = info;
    currentThemeModified_ = true;
    
    emit themeModified();
    return info;
}

ThemeManager::ThemeInfo ThemeManager::duplicateTheme(const QString& sourceName, const QString& newName) {
    // Load source theme
    if (!loadTheme(sourceName)) {
        emit errorOccurred(QString("Cannot duplicate theme '%1': not found").arg(sourceName));
        return ThemeInfo();
    }
    
    // Create new theme based on current
    ThemeInfo info = currentThemeInfo_;
    info.name = sanitizeThemeName(newName);
    info.displayName = newName;
    info.isBuiltIn = false;
    info.isModified = false;
    info.filePath = getThemeFilePath(info.name);
    info.metadata.name = newName;
    info.metadata.createdDate = QDateTime::currentDateTime();
    info.metadata.modifiedDate = QDateTime::currentDateTime();
    
    // Save the new theme
    if (!writeThemeFile(info.filePath, info.metadata)) {
        emit errorOccurred(QString("Failed to save duplicated theme '%1'").arg(newName));
        return ThemeInfo();
    }
    
    currentThemeInfo_ = info;
    currentThemeModified_ = false;
    
    emit themeSaved(info);
    emit themeListChanged();
    return info;
}

bool ThemeManager::saveTheme(const QString& name) {
    QString targetName = name.isEmpty() ? currentThemeInfo_.name : name;
    
    // Check if we have a valid name
    if (targetName.isEmpty()) {
        emit errorOccurred("Cannot save: theme has no name. Use Save As instead.");
        return false;
    }
    
    // Can't overwrite built-in themes
    if (isBuiltInTheme(targetName)) {
        emit errorOccurred(QString("Cannot overwrite built-in theme '%1'. Use Save As instead.").arg(targetName));
        return false;
    }
    
    QString filePath = getThemeFilePath(targetName);
    
    // Update metadata
    currentThemeInfo_.metadata.modifiedDate = QDateTime::currentDateTime();
    
    // Write the file
    if (!writeThemeFile(filePath, currentThemeInfo_.metadata)) {
        emit errorOccurred(QString("Failed to write theme file: %1").arg(filePath));
        return false;
    }
    
    // Update state
    currentThemeInfo_.name = targetName;
    currentThemeInfo_.filePath = filePath;
    currentThemeInfo_.isModified = false;
    currentThemeModified_ = false;
    
    // Update config
    updateConfigTheme();
    
    emit themeSaved(currentThemeInfo_);
    return true;
}

bool ThemeManager::saveThemeAs(const QString& newName) {
    QString sanitized = sanitizeThemeName(newName);
    
    if (sanitized.isEmpty()) {
        emit errorOccurred("Invalid theme name");
        return false;
    }
    
    // Check if it would overwrite a built-in theme
    if (isBuiltInTheme(sanitized)) {
        emit errorOccurred(QString("Cannot use built-in theme name '%1'").arg(sanitized));
        return false;
    }
    
    // Update current theme info
    currentThemeInfo_.name = sanitized;
    currentThemeInfo_.displayName = newName;
    currentThemeInfo_.isBuiltIn = false;
    currentThemeInfo_.metadata.name = newName;
    
    // Save with the new name
    return saveTheme(sanitized);
}

bool ThemeManager::loadTheme(const QString& name) {
    // Check for unsaved changes
    if (currentThemeModified_ && !currentThemeInfo_.name.isEmpty()) {
        emit unsavedChangesWarning();
        // Note: The UI layer should handle the warning and call this again if user confirms
    }
    
    // Stop watching old file
    if (fileWatcher_) {
        fileWatcher_->removePaths(fileWatcher_->files());
    }
    
    ThemeInfo info;
    info.name = name;
    
    // Handle built-in themes
    if (name == "dark" || name == ThemeConstants::THEME_DARK) {
        loadDefaultDarkTheme();
        info.name = "dark";
        info.displayName = "Dark (Built-in)";
        info.isBuiltIn = true;
        info.isModified = false;
        info.metadata.name = "Dark";
        info.metadata.author = "LLM RE";
        info.metadata.version = "1.0";
        info.metadata.description = "Default dark theme";
        info.metadata.baseTheme = ThemeConstants::THEME_DARK;
    }
    else if (name == "light" || name == ThemeConstants::THEME_LIGHT) {
        loadDefaultLightTheme();
        info.name = "light";
        info.displayName = "Light (Built-in)";
        info.isBuiltIn = true;
        info.isModified = false;
        info.metadata.name = "Light";
        info.metadata.author = "LLM RE";
        info.metadata.version = "1.0";
        info.metadata.description = "Default light theme";
        info.metadata.baseTheme = ThemeConstants::THEME_LIGHT;
    }
    else {
        // Load custom theme from file
        QString filePath = getThemeFilePath(name);
        
        if (!QFile::exists(filePath)) {
            emit errorOccurred(QString("Theme file not found: %1").arg(filePath));
            return false;
        }
        
        loadThemeFromFile(filePath);
        
        info.name = name;
        // Use metadata name if available, otherwise use file name
        info.displayName = currentThemeInfo_.metadata.name.isEmpty() ? 
                          name : currentThemeInfo_.metadata.name;
        info.filePath = filePath;
        info.isBuiltIn = false;
        info.isModified = false;
        info.metadata = currentThemeInfo_.metadata;  // Set by loadThemeFromFile
        
        // Start watching the theme file for external changes
        if (!fileWatcher_) {
            fileWatcher_ = std::make_unique<QFileSystemWatcher>();
            connect(fileWatcher_.get(), &QFileSystemWatcher::fileChanged,
                    this, &ThemeManager::onThemeFileChanged);
        }
        fileWatcher_->addPath(filePath);
    }
    
    currentThemeInfo_ = info;
    currentThemeModified_ = false;
    
    
    // Update component styles
    updateComponentStyles();
    applyThemeToApplication();
    
    // Update config
    updateConfigTheme();
    
    emit themeLoaded(info);
    emit themeChanged();
    emit colorsChanged();
    
    return true;
}

bool ThemeManager::deleteTheme(const QString& name) {
    if (isBuiltInTheme(name)) {
        emit errorOccurred(QString("Cannot delete built-in theme '%1'").arg(name));
        return false;
    }
    
    QString filePath = getThemeFilePath(name);
    
    if (!QFile::exists(filePath)) {
        emit errorOccurred(QString("Theme file not found: %1").arg(filePath));
        return false;
    }
    
    if (!QFile::remove(filePath)) {
        emit errorOccurred(QString("Failed to delete theme file: %1").arg(filePath));
        return false;
    }
    
    // If we just deleted the current theme, load dark theme
    if (currentThemeInfo_.name == name) {
        loadTheme("dark");
    }
    
    emit themeListChanged();
    return true;
}

bool ThemeManager::renameTheme(const QString& oldName, const QString& newName) {
    if (isBuiltInTheme(oldName)) {
        emit errorOccurred(QString("Cannot rename built-in theme '%1'").arg(oldName));
        return false;
    }
    
    QString oldPath = getThemeFilePath(oldName);
    QString newPath = getThemeFilePath(sanitizeThemeName(newName));
    
    if (!QFile::exists(oldPath)) {
        emit errorOccurred(QString("Theme file not found: %1").arg(oldPath));
        return false;
    }
    
    if (QFile::exists(newPath)) {
        emit errorOccurred(QString("Theme '%1' already exists").arg(newName));
        return false;
    }
    
    if (!QFile::rename(oldPath, newPath)) {
        emit errorOccurred(QString("Failed to rename theme file"));
        return false;
    }
    
    // If we renamed the current theme, update its info
    if (currentThemeInfo_.name == oldName) {
        currentThemeInfo_.name = sanitizeThemeName(newName);
        currentThemeInfo_.displayName = newName;
        currentThemeInfo_.filePath = newPath;
        currentThemeInfo_.metadata.name = newName;
        updateConfigTheme();
    }
    
    emit themeListChanged();
    return true;
}

QString ThemeManager::importThemeFile(const QString& externalPath) {
    if (!QFile::exists(externalPath)) {
        emit errorOccurred(QString("File not found: %1").arg(externalPath));
        return QString();
    }
    
    // Validate the theme file
    ThemeError error = validateThemeFile(externalPath);
    if (error != ThemeError::None) {
        QString errorMsg = "Invalid theme file: ";
        switch (error) {
            case ThemeError::InvalidFormat: errorMsg += "Invalid format"; break;
            case ThemeError::MissingMetadata: errorMsg += "Missing metadata"; break;
            case ThemeError::InvalidColors: errorMsg += "Invalid colors"; break;
            default: errorMsg += "Unknown error";
        }
        emit errorOccurred(errorMsg);
        return QString();
    }
    
    // Read metadata to get theme name
    QFile file(externalPath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit errorOccurred("Cannot read theme file");
        return QString();
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    QJsonObject root = doc.object();
    QJsonObject metadata = root[ThemeConstants::KEY_METADATA].toObject();
    QString themeName = metadata[ThemeConstants::META_NAME].toString();
    
    if (themeName.isEmpty()) {
        themeName = QFileInfo(externalPath).baseName();
    }
    
    themeName = sanitizeThemeName(themeName);
    
    // Check if theme already exists
    int counter = 1;
    QString finalName = themeName;
    while (themeExists(finalName)) {
        finalName = QString("%1_%2").arg(themeName).arg(counter++);
    }
    
    // Copy to themes directory
    QString destPath = getThemeFilePath(finalName);
    if (!QFile::copy(externalPath, destPath)) {
        emit errorOccurred(QString("Failed to import theme to %1").arg(destPath));
        return QString();
    }
    
    emit themeListChanged();
    return finalName;
}

bool ThemeManager::exportThemeFile(const QString& name, const QString& exportPath) {
    if (isBuiltInTheme(name)) {
        // For built-in themes, generate the theme file
        ThemeMetadata metadata;
        metadata.name = name;
        metadata.author = "LLM RE";
        metadata.version = "1.0";
        metadata.baseTheme = name;
        
        if (name == "dark") {
            loadDefaultDarkTheme();
            metadata.description = "Default dark theme";
        } else if (name == "light") {
            loadDefaultLightTheme();
            metadata.description = "Default light theme";
        }
        
        return writeThemeFile(exportPath, metadata);
    }
    
    QString sourcePath = getThemeFilePath(name);
    if (!QFile::exists(sourcePath)) {
        emit errorOccurred(QString("Theme file not found: %1").arg(sourcePath));
        return false;
    }
    
    // If exporting current modified theme, save it first
    if (currentThemeInfo_.name == name && currentThemeModified_) {
        if (!saveTheme()) {
            return false;
        }
    }
    
    if (!QFile::copy(sourcePath, exportPath)) {
        emit errorOccurred(QString("Failed to export theme to %1").arg(exportPath));
        return false;
    }
    
    return true;
}

QList<ThemeManager::ThemeInfo> ThemeManager::getAllThemes() const {
    QList<ThemeInfo> themes;
    
    // Add built-in themes
    ThemeInfo darkInfo;
    darkInfo.name = "dark";
    darkInfo.displayName = "Dark (Built-in)";
    darkInfo.isBuiltIn = true;
    darkInfo.metadata.name = "Dark";
    darkInfo.metadata.description = "Default dark theme";
    themes.append(darkInfo);
    
    ThemeInfo lightInfo;
    lightInfo.name = "light";
    lightInfo.displayName = "Light (Built-in)";
    lightInfo.isBuiltIn = true;
    lightInfo.metadata.name = "Light";
    lightInfo.metadata.description = "Default light theme";
    themes.append(lightInfo);
    
    // Add custom themes from directory
    QDir dir(themesDir_);
    QStringList filters;
    filters << QString("*%1").arg(ThemeConstants::THEME_FILE_EXTENSION);
    
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files);
    for (const QFileInfo& fileInfo : files) {
        ThemeInfo info;
        info.name = fileInfo.baseName();
        info.displayName = info.name;
        info.filePath = fileInfo.absoluteFilePath();
        info.isBuiltIn = false;
        
        // Try to load metadata
        QFile file(info.filePath);
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            if (doc.isObject()) {
                QJsonObject root = doc.object();
                if (root.contains(ThemeConstants::KEY_METADATA)) {
                    QJsonObject meta = root[ThemeConstants::KEY_METADATA].toObject();
                    info.metadata.name = meta[ThemeConstants::META_NAME].toString();
                    info.metadata.description = meta[ThemeConstants::META_DESCRIPTION].toString();
                    info.metadata.author = meta[ThemeConstants::META_AUTHOR].toString();
                    info.metadata.version = meta[ThemeConstants::META_VERSION].toString();
                }
            }
        }
        
        themes.append(info);
    }
    
    return themes;
}

ThemeManager::ThemeInfo ThemeManager::getThemeInfo(const QString& name) const {
    QList<ThemeInfo> allThemes = getAllThemes();
    for (const ThemeInfo& info : allThemes) {
        if (info.name == name) {
            return info;
        }
    }
    return ThemeInfo();
}

bool ThemeManager::isValidThemeName(const QString& name) const {
    if (name.isEmpty()) return false;
    
    // Check for invalid characters
    QRegExp invalidChars("[<>:\"/\\|?*]");
    if (name.contains(invalidChars)) return false;
    
    // Check length
    if (name.length() > 50) return false;
    
    return true;
}

QString ThemeManager::sanitizeThemeName(const QString& name) const {
    QString sanitized = name;
    
    // Remove invalid file characters
    sanitized.remove(QRegExp("[<>:\"/\\|?*]"));
    
    // Trim whitespace
    sanitized = sanitized.trimmed();
    
    // Replace consecutive spaces with single underscore
    sanitized.replace(QRegExp("\\s+"), "_");
    
    // Limit length
    if (sanitized.length() > 50) {
        sanitized = sanitized.left(50);
    }
    
    // Ensure not empty
    if (sanitized.isEmpty()) {
        sanitized = QString("theme_%1").arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    }
    
    return sanitized;
}

bool ThemeManager::themeExists(const QString& name) const {
    if (isBuiltInTheme(name)) return true;
    
    QString filePath = getThemeFilePath(name);
    return QFile::exists(filePath);
}

bool ThemeManager::isBuiltInTheme(const QString& themeName) const {
    return themeName == "dark" || 
           themeName == "light" || 
           themeName == ThemeConstants::THEME_DARK ||
           themeName == ThemeConstants::THEME_LIGHT ||
           themeName == ThemeConstants::THEME_DEFAULT;
}

void ThemeManager::markModified() {
    if (!currentThemeModified_) {
        currentThemeModified_ = true;
        currentThemeInfo_.isModified = true;
        emit themeModified();
    }
}

QString ThemeManager::getThemeFilePath(const QString& name) const {
    if (isBuiltInTheme(name)) {
        return QString();  // Built-in themes have no file path
    }
    
    return QString("%1/%2%3")
        .arg(themesDir_)
        .arg(sanitizeThemeName(name))
        .arg(ThemeConstants::THEME_FILE_EXTENSION);
}

void ThemeManager::updateConfigTheme() {
    // Update config with current theme name
    Config::instance().ui.theme_name = currentThemeInfo_.name.toStdString();
    Config::instance().save();
}

ThemeManager::ThemeInfo ThemeManager::createThemeInfo(const QString& name) const {
    return getThemeInfo(name);
}

bool ThemeManager::writeThemeFile(const QString& filePath, const ThemeMetadata& metadata) const {
    try {
        // Ensure directory exists
        QFileInfo fileInfo(filePath);
        QDir dir = fileInfo.dir();
        if (!dir.exists()) {
            dir.mkpath(".");
        }
        
        // Generate theme JSON
        QJsonObject root = generateThemeJson();
        
        // Add metadata
        root[ThemeConstants::KEY_METADATA] = generateMetadataJson(metadata);
        
        // Write file
        QFile file(filePath);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        
        QJsonDocument doc(root);
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
        
        return true;
    } catch (const std::exception& e) {
        qWarning() << "Error writing theme file:" << e.what();
        return false;
    }
}

} // namespace llm_re::ui_v2