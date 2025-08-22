#include "settings_manager.h"
#include "theme_manager.h"

namespace llm_re::ui_v2 {

SettingsManager& SettingsManager::instance() {
    static SettingsManager instance;
    return instance;
}

SettingsManager::SettingsManager() : QObject(nullptr), config_(Config::instance()) {
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
    
    if (!Config::instance().save_to_file(path)) {
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
    // Apply theme using the new architecture
    ThemeManager& tm = ThemeManager::instance();
    const std::string& themeName = Config::instance().ui.theme_name;
    
    // Load theme by name - ThemeManager handles built-in vs custom
    if (!tm.loadTheme(QString::fromStdString(themeName))) {
        // Fallback to dark if theme not found
        tm.loadTheme("dark");
        Config::instance().ui.theme_name = "dark";
    }
    
    // CRITICAL: Do NOT apply font settings globally!
    // This would affect IDA Pro's UI which is unacceptable.
    // Fonts are applied through theme styles to widgets marked with llm_re_widget property.
    // QApplication::setFont() and iterating through all widgets MUST NOT be used.
    
    // Apply window management settings
    
    // Apply conversation view settings
    // These would be applied through signals to the ConversationView
    emit settingChanged("show_timestamps", config_.ui.show_timestamps);
    emit settingChanged("auto_scroll", config_.ui.auto_scroll);
    emit settingChanged("show_tool_details", config_.ui.show_tool_details);
    emit settingChanged("density_mode", config_.ui.density_mode);
    emit settingChanged("auto_save_conversations", config_.ui.auto_save_conversations);
    emit settingChanged("auto_save_interval", config_.ui.auto_save_interval);
    
    // Apply log buffer size
    emit settingChanged("log_buffer_size", config_.ui.log_buffer_size);
    
    // Notify that all settings have been applied
    emit settingsChanged();
}

void SettingsManager::setupAutoSave() {
    auto_save_timer_ = new QTimer(this);
    // Use auto-save interval from config (in seconds), convert to milliseconds
    int interval = config_.ui.auto_save_interval > 0 ? config_.ui.auto_save_interval * 1000 : 30000;
    auto_save_timer_->setInterval(interval);
    
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

void SettingsManager::connectConversationView(QObject* view) {
    if (!view) return;
    
    // Connect settings to ConversationView slots
    connect(this, &SettingsManager::settingChanged, view, [view](const QString& key, const QVariant& value) {
        if (key == "show_timestamps") {
            QMetaObject::invokeMethod(view, "setShowTimestamps", Q_ARG(bool, value.toBool()));
        } else if (key == "density_mode") {
            QMetaObject::invokeMethod(view, "setDensityMode", Q_ARG(int, value.toInt()));
        } else if (key == "auto_save_conversations") {
            QMetaObject::invokeMethod(view, "setAutoSaveEnabled", Q_ARG(bool, value.toBool()));
        } else if (key == "auto_save_interval") {
            QMetaObject::invokeMethod(view, "setAutoSaveInterval", Q_ARG(int, value.toInt()));
        }
    });
    
    // Apply current settings
    QMetaObject::invokeMethod(view, "setShowTimestamps", Q_ARG(bool, config_.ui.show_timestamps));
    QMetaObject::invokeMethod(view, "setDensityMode", Q_ARG(int, config_.ui.density_mode));
    QMetaObject::invokeMethod(view, "setAutoSaveEnabled", Q_ARG(bool, config_.ui.auto_save_conversations));
    QMetaObject::invokeMethod(view, "setAutoSaveInterval", Q_ARG(int, config_.ui.auto_save_interval));
}


void SettingsManager::connectMainWindow(QObject* mainWindow) {
    if (!mainWindow) return;
    
    // Connect settings to MainWindow slots
    connect(this, &SettingsManager::settingChanged, mainWindow, [mainWindow](const QString& key, const QVariant& value) {
        if (key == "log_buffer_size") {
            QMetaObject::invokeMethod(mainWindow, "setLogBufferSize", Q_ARG(int, value.toInt()));
        } else if (key == "auto_scroll") {
            QMetaObject::invokeMethod(mainWindow, "setAutoScroll", Q_ARG(bool, value.toBool()));
        }
    });
    
    // Apply current settings
    QMetaObject::invokeMethod(mainWindow, "setLogBufferSize", Q_ARG(int, config_.ui.log_buffer_size));
    QMetaObject::invokeMethod(mainWindow, "setAutoScroll", Q_ARG(bool, config_.ui.auto_scroll));
    
    // Handle window state restoration
    if (config_.ui.remember_window_state) {
        QMetaObject::invokeMethod(mainWindow, "restoreWindowState");
    }
    
    if (config_.ui.start_minimized) {
        QMetaObject::invokeMethod(mainWindow, "showMinimized");
    }
}

} // namespace llm_re::ui_v2