#pragma once

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
    
    // Convenience accessors
    QString apiKey() const { return QString::fromStdString(config_.api.api_key); }
    void setApiKey(const QString& key) { 
        config_.api.api_key = key.toStdString(); 
        emit apiKeyChanged(key);
    }
    
    int theme() const { return config_.ui.theme; }
    void setTheme(int theme) { 
        config_.ui.theme = theme; 
        emit themeChanged(theme);
    }
    
    bool debugMode() const { return config_.debug_mode; }
    void setDebugMode(bool enabled) { 
        config_.debug_mode = enabled; 
        emit debugModeChanged(enabled);
    }

signals:
    // Settings change notifications
    void settingsChanged();
    void apiKeyChanged(const QString& key);
    void themeChanged(int theme);
    void debugModeChanged(bool enabled);
    void settingsLoadError(const QString& error);
    void settingsSaveError(const QString& error);

private:
    SettingsManager();
    ~SettingsManager() = default;
    
    Config config_;
    QString settings_path_;
    
    // Auto-save timer
    QTimer* auto_save_timer_ = nullptr;
    bool auto_save_enabled_ = true;
    
    void setupAutoSave();
    
private slots:
    void onAutoSave();
};

} // namespace llm_re::ui_v2