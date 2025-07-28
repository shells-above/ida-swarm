#include "ui_v2_common.h"
#include "theme_manager.h"

namespace llm_re::ui_v2 {

ThemeManager& ThemeManager::instance() {
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager() {
    initializeColorMap();
    loadDefaultDarkTheme(); // Start with dark theme
    typography_.setupFonts();
    updateComponentStyles();
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
        {"syntaxOperator", &colors_.syntaxOperator}
    };
}

void ThemeManager::setTheme(Theme theme) {
    if (currentTheme_ == theme && theme != Theme::Custom) return;
    
    currentTheme_ = theme;
    loadTheme(theme);
    applyThemeToApplication();
    
    emit themeChanged();
    emit colorsChanged();
}

void ThemeManager::setCustomTheme(const QString& themePath) {
    customThemePath_ = themePath;
    currentTheme_ = Theme::Custom;
    loadThemeFromFile(themePath);
    applyThemeToApplication();
    
    emit themeChanged();
    emit colorsChanged();
}

QString ThemeManager::currentThemeName() const {
    switch (currentTheme_) {
        case Theme::Dark: return "Dark";
        case Theme::Light: return "Light";
        case Theme::Custom: return "Custom";
    }
    return "Unknown";
}

void ThemeManager::loadTheme(Theme theme) {
    switch (theme) {
        case Theme::Dark:
            loadDefaultDarkTheme();
            break;
        case Theme::Light:
            loadDefaultLightTheme();
            break;
        case Theme::Custom:
            if (!customThemePath_.isEmpty()) {
                loadThemeFromFile(customThemePath_);
            }
            break;
    }
    updateComponentStyles();
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
    
    if (root.contains("colors")) {
        parseColorPalette(root["colors"].toObject());
    }
    
    if (root.contains("typography")) {
        parseTypography(root["typography"].toObject());
    }
    
    if (root.contains("components")) {
        parseComponentStyles(root["components"].toObject());
    }
}

void ThemeManager::parseColorPalette(const QJsonObject& obj) {
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        auto colorIt = colorMap_.find(it.key());
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
    
    // Similar parsing for other components...
}

void ThemeManager::updateComponentStyles() {
    if (compactMode_) {
        // Reduce spacing in compact mode
        componentStyles_.button.paddingHorizontal = Design::SPACING_SM;
        componentStyles_.button.paddingVertical = Design::SPACING_XS;
        componentStyles_.input.paddingHorizontal = Design::SPACING_SM;
        componentStyles_.input.paddingVertical = Design::SPACING_XS;
        componentStyles_.card.padding = Design::SPACING_SM;
        componentStyles_.message.padding = Design::SPACING_SM;
    }
}

void ThemeManager::applyThemeToApplication() {
    // Apply to Qt application palette
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
    
    QApplication::setPalette(palette);
    
    // Apply global stylesheet
    QString globalQss = generateQss();
    qApp->setStyleSheet(globalQss);
    
    // Clear component cache to force regeneration
    componentQssCache_.clear();
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
    return QString(R"(
        * {
            font-family: "%1";
            font-size: %2px;
        }
        
        QWidget {
            background-color: %3;
            color: %4;
        }
        
        QMainWindow {
            background-color: %3;
        }
    )").arg(typography_.body.family())
       .arg(int(typography_.body.pointSize() * fontScale_))
       .arg(colors_.background.name())
       .arg(colors_.textPrimary.name());
}

QString ThemeManager::generateButtonQss() const {
    return QString(R"(
        QPushButton {
            background-color: %1;
            color: %2;
            border: %3px solid %4;
            border-radius: %5px;
            padding: %6px %7px;
            font-weight: 500;
        }
        
        QPushButton:hover {
            background-color: %8;
            border-color: %9;
        }
        
        QPushButton:pressed {
            background-color: %10;
        }
        
        QPushButton:disabled {
            background-color: %11;
            color: %12;
            border-color: %13;
        }
        
        QPushButton[primary="true"] {
            background-color: %14;
            color: %15;
            border: none;
        }
        
        QPushButton[primary="true"]:hover {
            background-color: %16;
        }
        
        QPushButton[primary="true"]:pressed {
            background-color: %17;
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
        QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
            background-color: %1;
            color: %2;
            border: %3px solid %4;
            border-radius: %5px;
            padding: %6px %7px;
            selection-background-color: %8;
            selection-color: %9;
        }
        
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, 
        QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
            border-color: %10;
            outline: none;
        }
        
        QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled,
        QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {
            background-color: %11;
            color: %12;
        }
        
        QComboBox::drop-down {
            border: none;
            width: 20px;
        }
        
        QComboBox::down-arrow {
            image: none;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-top: 5px solid %2;
            margin-right: 5px;
        }
        
        QComboBox QAbstractItemView {
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
        QScrollBar:vertical {
            background-color: %1;
            width: 12px;
            border: none;
        }
        
        QScrollBar::handle:vertical {
            background-color: %2;
            border-radius: 6px;
            min-height: 20px;
            margin: 2px;
        }
        
        QScrollBar::handle:vertical:hover {
            background-color: %3;
        }
        
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
        
        QScrollBar:horizontal {
            background-color: %1;
            height: 12px;
            border: none;
        }
        
        QScrollBar::handle:horizontal {
            background-color: %2;
            border-radius: 6px;
            min-width: 20px;
            margin: 2px;
        }
        
        QScrollBar::handle:horizontal:hover {
            background-color: %3;
        }
        
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {
            width: 0px;
        }
    )").arg(colors_.background.name())
       .arg(colors_.border.name())
       .arg(colors_.borderStrong.name());
}

QString ThemeManager::generateMenuQss() const {
    return QString(R"(
        QMenuBar {
            background-color: %1;
            color: %2;
            border-bottom: 1px solid %3;
        }
        
        QMenuBar::item:selected {
            background-color: %4;
        }
        
        QMenu {
            background-color: %5;
            color: %2;
            border: 1px solid %3;
            padding: 4px;
        }
        
        QMenu::item {
            padding: 6px 20px;
            border-radius: 4px;
        }
        
        QMenu::item:selected {
            background-color: %4;
        }
        
        QMenu::separator {
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
        QTabWidget::pane {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 4px;
        }
        
        QTabWidget::tab-bar {
            left: 0px;
        }
        
        QTabBar::tab {
            background-color: %3;
            color: %4;
            padding: 8px 16px;
            margin-right: 2px;
            border-top-left-radius: 4px;
            border-top-right-radius: 4px;
        }
        
        QTabBar::tab:selected {
            background-color: %1;
            color: %5;
        }
        
        QTabBar::tab:hover:!selected {
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
        QDockWidget {
            color: %1;
        }
        
        QDockWidget::title {
            background-color: %2;
            padding: 6px;
            border-bottom: 1px solid %3;
        }
        
        QDockWidget::close-button, QDockWidget::float-button {
            background: transparent;
            border: none;
            padding: 2px;
        }
        
        QDockWidget::close-button:hover, QDockWidget::float-button:hover {
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
        QTreeView, QTreeWidget, QListView, QListWidget {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            outline: none;
            selection-background-color: %4;
        }
        
        QTreeView::item, QTreeWidget::item, QListView::item, QListWidget::item {
            padding: 4px;
            border-radius: 4px;
        }
        
        QTreeView::item:hover, QTreeWidget::item:hover, 
        QListView::item:hover, QListWidget::item:hover {
            background-color: %5;
        }
        
        QTreeView::item:selected, QTreeWidget::item:selected,
        QListView::item:selected, QListWidget::item:selected {
            background-color: %4;
        }
        
        QTreeView::branch {
            background-color: %1;
        }
        
        QTreeView::branch:has-children:closed {
            image: none;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-top: 5px solid %2;
        }
        
        QTreeView::branch:has-children:open {
            image: none;
            border-left: 5px solid transparent;
            border-right: 5px solid transparent;
            border-bottom: 5px solid %2;
        }
        
        QHeaderView::section {
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
    auto it = colorMap_.find(colorName);
    if (it != colorMap_.end() && it->second) {
        return *it->second;
    }
    return QColor();
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

void ThemeManager::setCompactMode(bool compact) {
    compactMode_ = compact;
    updateComponentStyles();
    applyThemeToApplication();
    emit themeChanged();
}

void ThemeManager::enableHotReload(bool enable) {
    hotReloadEnabled_ = enable;
    
    if (enable && currentTheme_ == Theme::Custom && !customThemePath_.isEmpty()) {
        if (!fileWatcher_) {
            fileWatcher_ = std::make_unique<QFileSystemWatcher>();
            connect(fileWatcher_.get(), &QFileSystemWatcher::fileChanged,
                    this, &ThemeManager::onThemeFileChanged);
        }
        fileWatcher_->addPath(customThemePath_);
    } else if (fileWatcher_) {
        fileWatcher_.reset();
    }
}

void ThemeManager::onThemeFileChanged(const QString& path) {
    if (hotReloadEnabled_ && path == customThemePath_) {
        // Reload theme
        loadThemeFromFile(path);
        applyThemeToApplication();
        emit themeChanged();
        
        // Re-add the file to the watcher (it gets removed after change)
        fileWatcher_->addPath(path);
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
    // Add more component-specific styles...
    
    // Cache the result
    const_cast<ThemeManager*>(this)->componentQssCache_[componentName] = qss;
    return qss;
}

QString ThemeManager::themedIconPath(const QString& iconName) const {
    QString themeSuffix = (currentTheme_ == Theme::Light) ? "_light" : "_dark";
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

} // namespace llm_re::ui_v2