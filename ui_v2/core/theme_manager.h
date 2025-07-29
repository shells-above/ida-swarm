#pragma once

#include "ui_v2_common.h"
#include "ui_constants.h"

namespace llm_re::ui_v2 {

class ThemeManager : public QObject {
    Q_OBJECT

public:
    enum class Theme {
        Dark,
        Light,
        Custom
    };

    // Singleton access
    static ThemeManager& instance();
    
    // Theme management
    void setTheme(Theme theme);
    void setCustomTheme(const QString& themePath);
    Theme currentTheme() const { return currentTheme_; }
    QString currentThemeName() const;
    
    // Color access
    const ColorPalette& colors() const { return colors_; }
    QColor color(const QString& colorName) const;
    
    // Typography access
    const Typography& typography() const { return typography_; }
    
    // Component styles
    const ComponentStyles& componentStyles() const { return componentStyles_; }
    
    // QSS generation
    QString generateQss() const;
    QString componentQss(const QString& componentName) const;
    
    // Live reload
    void enableHotReload(bool enable);
    bool isHotReloadEnabled() const { return hotReloadEnabled_; }
    
    // Theme customization
    void setAccentColor(const QColor& color);
    void setFontScale(qreal scale);
    void setDensityMode(int mode);  // 0=Compact, 1=Cozy, 2=Spacious
    
    // Widget theming
    void applyThemeToWidget(QWidget* widget);
    QPalette widgetPalette() const;
    
    // Utility functions
    static QColor adjustAlpha(const QColor& color, int alpha);
    static QColor lighten(const QColor& color, int amount = 20);
    static QColor darken(const QColor& color, int amount = 20);
    static QColor mix(const QColor& color1, const QColor& color2, qreal ratio = 0.5);
    
    // Icon management
    QString themedIconPath(const QString& iconName) const;
    QIcon themedIcon(const QString& iconName) const;

signals:
    void themeChanged();
    void colorsChanged();
    void fontsChanged();

private:
    ThemeManager();
    ~ThemeManager() = default;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    void loadTheme(Theme theme);
    void loadThemeFromFile(const QString& path);
    void loadDefaultDarkTheme();
    void loadDefaultLightTheme();
    void applyThemeToApplication();
    void updateComponentStyles();
    
    // JSON parsing
    void parseColorPalette(const QJsonObject& obj);
    void parseTypography(const QJsonObject& obj);
    void parseComponentStyles(const QJsonObject& obj);
    QColor parseColor(const QJsonValue& value) const;
    
    // QSS generation helpers
    QString generateBaseQss() const;
    QString generateButtonQss() const;
    QString generateInputQss() const;
    QString generateScrollBarQss() const;
    QString generateMenuQss() const;
    QString generateTabQss() const;
    QString generateDockQss() const;
    QString generateTreeQss() const;
    QString generateToolTipQss() const;
    
    // Member variables
    Theme currentTheme_ = Theme::Dark;
    QString customThemePath_;
    ColorPalette colors_;
    Typography typography_;
    ComponentStyles componentStyles_;
    
    qreal fontScale_ = 1.0;
    int densityMode_ = 1;  // 0=Compact, 1=Cozy, 2=Spacious
    bool hotReloadEnabled_ = false;
    
    std::unique_ptr<QFileSystemWatcher> fileWatcher_;
    std::map<QString, QString> componentQssCache_;
    
    // Color name mappings for runtime lookup
    std::map<QString, QColor*> colorMap_;
    void initializeColorMap();

private slots:
    void onThemeFileChanged(const QString& path);
};

// Convenience macros for theme access
#define Theme() ThemeManager::instance()
#define ThemeColor(name) ThemeManager::instance().color(name)
#define ThemeFont(name) ThemeManager::instance().typography().name

} // namespace llm_re::ui_v2