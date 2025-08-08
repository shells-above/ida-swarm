#pragma once

namespace llm_re::ui_v2 {

// Theme-related constants
namespace ThemeConstants {
    // File extension for theme files
    constexpr const char* THEME_FILE_EXTENSION = ".llmtheme";
    
    // Theme directory name relative to IDA home
    constexpr const char* THEME_DIR_NAME = "llm_re_themes";
    
    // Built-in theme names
    constexpr const char* THEME_DEFAULT = "default";
    constexpr const char* THEME_DARK = "dark";
    constexpr const char* THEME_LIGHT = "light";
    
    // Theme metadata keys
    constexpr const char* META_NAME = "name";
    constexpr const char* META_AUTHOR = "author";
    constexpr const char* META_VERSION = "version";
    constexpr const char* META_DESCRIPTION = "description";
    constexpr const char* META_BASE_THEME = "baseTheme";
    constexpr const char* META_CREATED_DATE = "createdDate";
    constexpr const char* META_MODIFIED_DATE = "modifiedDate";
    
    // Theme structure keys
    constexpr const char* KEY_METADATA = "metadata";
    constexpr const char* KEY_COLORS = "colors";
    constexpr const char* KEY_TYPOGRAPHY = "typography";
    constexpr const char* KEY_COMPONENTS = "components";
    constexpr const char* KEY_ANIMATIONS = "animations";
    constexpr const char* KEY_EFFECTS = "effects";
    constexpr const char* KEY_CHARTS = "charts";
    
    // Color category keys
    constexpr const char* COLOR_CAT_BRAND = "brand";
    constexpr const char* COLOR_CAT_SEMANTIC = "semantic";
    constexpr const char* COLOR_CAT_UI = "ui";
    constexpr const char* COLOR_CAT_TEXT = "text";
    constexpr const char* COLOR_CAT_SYNTAX = "syntax";
    constexpr const char* COLOR_CAT_STATUS = "status";
    constexpr const char* COLOR_CAT_CHART = "chart";
    constexpr const char* COLOR_CAT_SPECIAL = "special";
}

// Theme metadata structure
struct ThemeMetadata {
    QString name;
    QString author;
    QString version;
    QString description;
    QString baseTheme;  // Which built-in theme this is based on
    QDateTime createdDate;
    QDateTime modifiedDate;
    
    // Validation
    bool isValid() const {
        return !name.isEmpty() && !version.isEmpty();
    }
};

// Theme validation error codes
enum class ThemeError {
    None,
    FileNotFound,
    InvalidFormat,
    MissingMetadata,
    InvalidColors,
    InvalidVersion,
    CorruptedData
};

} // namespace llm_re::ui_v2