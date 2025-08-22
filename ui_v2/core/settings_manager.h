#pragma once

#include "theme_manager.h"
#include "ui_v2_common.h"
#include "../../core/config.h"

namespace llm_re::ui_v2 {

// Settings manager singleton for ui_v2
class SettingsManager : public QObject {
    Q_OBJECT

public:
    // Singleton access
    static SettingsManager& instance();
    
    // Prevent copies
    SettingsManager(const SettingsManager&) = delete;
    SettingsManager& operator=(const SettingsManager&) = delete;
    
    // Configuration access
    const Config& config() const { return config_; }
    Config& config() { return config_; }
    
    // Load/save settings
    bool loadSettings();
    bool saveSettings();
    
    // Settings file path
    QString settingsPath() const;
    void setSettingsPath(const QString& path);
    
    // Apply settings to UI
    void applyUISettings();
    
    // Connect UI components to settings
    void connectConversationView(QObject* view);
    void connectMainWindow(QObject* mainWindow);
    
    // Convenience accessors
    QString apiKey() const { return QString::fromStdString(config_.api.api_key); }
    void setApiKey(const QString& key) { 
        config_.api.api_key = key.toStdString();
        emit apiKeyChanged(key);
    }
    
    QString themeName() const { return QString::fromStdString(config_.ui.theme_name); }
    void setThemeName(const QString& themeName) { 
        // Let ThemeManager handle everything, it will update config
        ThemeManager::instance().loadTheme(themeName);
        emit themeChanged(themeName);
    }
    
    // UI Settings accessors
    int fontSize() const { return config_.ui.font_size; }
    void setFontSize(int size) { 
        config_.ui.font_size = size;
        emit settingChanged("font_size", size);
        applyUISettings();
    }
    
    bool showTimestamps() const { return config_.ui.show_timestamps; }
    void setShowTimestamps(bool show) { 
        config_.ui.show_timestamps = show;
        emit settingChanged("show_timestamps", show);
    }
    
    bool autoScroll() const { return config_.ui.auto_scroll; }
    void setAutoScroll(bool scroll) { 
        config_.ui.auto_scroll = scroll;
        emit settingChanged("auto_scroll", scroll);
    }
    
    bool showToolDetails() const { return config_.ui.show_tool_details; }
    void setShowToolDetails(bool show) { 
        config_.ui.show_tool_details = show;
        emit settingChanged("show_tool_details", show);
    }
    
    int densityMode() const { return config_.ui.density_mode; }
    void setDensityMode(int mode) { 
        config_.ui.density_mode = mode;
        emit settingChanged("density_mode", mode);
    }
    
    // Window management

signals:
    // Settings change notifications
    void settingsChanged();
    void apiKeyChanged(const QString& key);
    void themeChanged(const QString& themeName);
    void settingsLoadError(const QString& error);
    void settingsSaveError(const QString& error);
    
    // Individual setting changes
    void settingChanged(const QString& key, const QVariant& value);

private:
    SettingsManager();
    ~SettingsManager() = default;
    
    QString settings_path_;
    Config& config_ = Config::instance();
    
    // Auto-save timer
    QTimer* auto_save_timer_ = nullptr;
    bool auto_save_enabled_ = true;
    
    void setupAutoSave();
    
private slots:
    void onAutoSave();
};

} // namespace llm_re::ui_v2