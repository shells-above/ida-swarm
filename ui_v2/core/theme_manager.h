#pragma once

#include "ui_v2_common.h"
#include "ui_constants.h"
#include "theme_constants.h"

namespace llm_re::ui_v2 {

class ThemeManager : public QObject {
    Q_OBJECT

public:
    enum class Theme {
        Dark,
        Light,
        Custom
    };

    // Theme information structure
    struct ThemeInfo {
        QString name;           // Unique identifier (e.g., "dark", "my_theme")
        QString displayName;    // User-friendly name for UI display
        QString filePath;       // Full path to .llmtheme file (empty for built-in)
        bool isBuiltIn;        // true for Dark/Light themes
        bool isModified;       // Has unsaved changes
        ThemeMetadata metadata; // Theme metadata
        
        bool isValid() const { return !name.isEmpty(); }
    };

    // Singleton access
    static ThemeManager& instance();
    
    // Theme directory management
    static QString themesDirectory();
    static void ensureThemesDirectory();
    
    // Core theme operations - NEW ARCHITECTURE
    ThemeInfo createNewTheme(const QString& basedOn = "dark");
    ThemeInfo duplicateTheme(const QString& sourceName, const QString& newName);
    bool saveTheme(const QString& name = "");  // Empty = save current
    bool saveThemeAs(const QString& newName);
    bool loadTheme(const QString& name);
    bool deleteTheme(const QString& name);
    bool renameTheme(const QString& oldName, const QString& newName);
    
    // File operations
    QString importThemeFile(const QString& externalPath);  // Returns theme name
    bool exportThemeFile(const QString& name, const QString& exportPath);
    
    // Theme discovery
    QList<ThemeInfo> getAllThemes() const;
    ThemeInfo getCurrentThemeInfo() const { return currentThemeInfo_; }
    ThemeInfo getThemeInfo(const QString& name) const;
    
    // Validation
    bool isValidThemeName(const QString& name) const;
    QString sanitizeThemeName(const QString& name) const;
    bool themeExists(const QString& name) const;
    bool isBuiltInTheme(const QString& themeName) const;
    
    // State tracking
    bool hasUnsavedChanges() const { return currentThemeModified_; }
    void markModified();
    void clearModified() { currentThemeModified_ = false; }
    
    // File path management
    QString getThemeFilePath(const QString& name) const;
    QString getThemesDirectory() const { return themesDir_; }
    
    // Color access
    const ColorPalette& colors() const { return colors_; }
    QColor color(const QString& colorName) const;
    
    // Chart colors access
    const std::vector<QColor>& chartSeriesColors() const;
    QColor chartSeriesColor(int index) const;
    
    // Chart theme presets
    enum class ChartStyle {
        Modern,      // Clean, minimal with subtle effects
        Neon,        // Vibrant colors with strong glow
        Corporate,   // Professional, muted colors
        Playful,     // Bright, animated with bounce effects
        Terminal,    // Monochrome, ASCII-inspired
        Glass        // Transparent with blur effects
    };
    
    void setChartStyle(ChartStyle style);
    ChartStyle currentChartStyle() const { return chartStyle_; }
    
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
    
    // Internal validation helper
    ThemeError validateThemeFile(const QString& filePath) const;
    
    // Additional settings
    void setCornerRadius(int radius);
    int cornerRadius() const;
    qreal fontScale() const { return fontScale_; }
    int densityMode() const { return densityMode_; }
    
    // Direct color setting
    void setColor(const QString& colorName, const QColor& color);
    void setTypography(const Typography& typography);
    void setCurrentThemeMetadata(const ThemeMetadata& metadata);
    
    // Access to internal maps for tools
    friend class ThemeUndoManager;
    friend class ThemeTemplates;
    std::map<std::string, QColor*> colorMap_;

signals:
    void themeChanged();
    void colorsChanged();
    void fontsChanged();
    // New signals for state management
    void themeLoaded(const ThemeInfo& info);
    void themeSaved(const ThemeInfo& info);
    void themeModified();
    void themeListChanged();
    void unsavedChangesWarning();
    void errorOccurred(const QString& error);

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
    void parseAnimations(const QJsonObject& obj);
    void parseEffects(const QJsonObject& obj);
    void parseCharts(const QJsonObject& obj);
    void parseMetadata(const QJsonObject& obj);
    QColor parseColor(const QJsonValue& value) const;
    
    // JSON generation
    QJsonObject generateThemeJson() const;
    QJsonObject generateMetadataJson(const ThemeMetadata& metadata) const;
    QJsonObject generateColorsJson() const;
    QJsonObject generateTypographyJson() const;
    QJsonObject generateComponentsJson() const;
    QJsonObject generateAnimationsJson() const;
    QJsonObject generateEffectsJson() const;
    QJsonObject generateChartsJson() const;
    
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
    ColorPalette colors_;
    Typography typography_;
    ComponentStyles componentStyles_;
    
    // NEW state management
    ThemeInfo currentThemeInfo_;
    bool currentThemeModified_ = false;
    QString themesDir_;  // Cached themes directory path
    
    qreal fontScale_ = 1.2;  // Increased for better readability
    int densityMode_ = 1;  // 0=Compact, 1=Cozy, 2=Spacious
    bool hotReloadEnabled_ = false;
    ChartStyle chartStyle_ = ChartStyle::Modern;
    
    std::unique_ptr<QFileSystemWatcher> fileWatcher_;
    std::map<QString, QString> componentQssCache_;
    
    // Color name mappings for runtime lookup
    void initializeColorMap();
    
    // New helper methods
    void updateConfigTheme();
    ThemeInfo createThemeInfo(const QString& name) const;
    bool writeThemeFile(const QString& filePath, const ThemeMetadata& metadata) const;

private slots:
    void onThemeFileChanged(const QString& path);
};

// Convenience macros for theme access
#define Theme() ThemeManager::instance()
#define ThemeColor(name) ThemeManager::instance().color(name)
#define ThemeFont(name) ThemeManager::instance().typography().name

} // namespace llm_re::ui_v2