#include "settings_manager.h"
#include "theme_manager.h"

namespace llm_re::ui_v2 {

SettingsManager& SettingsManager::instance() {
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager() : QObject(nullptr) {
    // Default settings path
    settings_path_ = QString::fromStdString(get_user_idadir()) + "/llm_re_config.json";
    
    // Setup auto-save
    setupAutoSave();
}

bool SettingsManager::loadSettings() {
    std::string path = settings_path_.toStdString();
    
    if (!config_.load_from_file(path)) {
        QString error = QString("Failed to load settings from: %1").arg(settings_path_);
        emit settingsLoadError(error);
        return false;
    }
    
    // Apply loaded settings
    applyUISettings();
    emit settingsChanged();
    
    return true;
}

bool SettingsManager::saveSettings() {
    std::string path = settings_path_.toStdString();
    
    if (!config_.save_to_file(path)) {
        QString error = QString("Failed to save settings to: %1").arg(settings_path_);
        emit settingsSaveError(error);
        return false;
    }
    
    return true;
}

QString SettingsManager::settingsPath() const {
    return settings_path_;
}

void SettingsManager::setSettingsPath(const QString& path) {
    settings_path_ = path;
}

void SettingsManager::applyUISettings() {
    // Apply theme
    ThemeManager& tm = ThemeManager::instance();
    switch (config_.ui.theme) {
        case 0: // Default
            tm.setTheme(ThemeManager::Theme::Dark);
            break;
        case 1: // Dark
            tm.setTheme(ThemeManager::Theme::Dark);
            break;
        case 2: // Light
            tm.setTheme(ThemeManager::Theme::Light);
            break;
    }
    
    // Apply other UI settings
    // Font size, timestamps, etc. would be applied here
}

void SettingsManager::setupAutoSave() {
    auto_save_timer_ = new QTimer(this);
    auto_save_timer_->setInterval(30000); // Auto-save every 30 seconds
    
    connect(auto_save_timer_, &QTimer::timeout, this, &SettingsManager::onAutoSave);
    
    // Connect to settings changes to start auto-save timer
    connect(this, &SettingsManager::settingsChanged, [this]() {
        if (auto_save_enabled_) {
            auto_save_timer_->start();
        }
    });
}

void SettingsManager::onAutoSave() {
    saveSettings();
    auto_save_timer_->stop(); // Stop until next change
}

} // namespace llm_re::ui_v2