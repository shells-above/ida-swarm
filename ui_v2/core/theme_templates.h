#pragma once

#include "ui_v2_common.h"
#include "theme_constants.h"
#include "theme_manager.h"

namespace llm_re::ui_v2 {

class ThemeTemplates {
public:
    enum class Template {
        Minimal,
        Vibrant,
        Professional,
        Retro,
        HighContrast,
        Nature,
        Ocean,
        Sunset
    };
    
    struct TemplateInfo {
        QString name;
        QString description;
        QString category;
        QPixmap preview;
        ThemeMetadata metadata;
    };
    
    // Get available templates
    static std::vector<TemplateInfo> getAvailableTemplates();
    
    // Apply a template
    static void applyTemplate(Template tmpl);
    
    // Get template details
    static TemplateInfo getTemplateInfo(Template tmpl);
    
    // Create theme from template
    static void createThemeFromTemplate(Template tmpl, const QString& newThemeName);
    
private:
    // Template generators
    static void applyMinimalTemplate();
    static void applyVibrantTemplate();
    static void applyProfessionalTemplate();
    static void applyRetroTemplate();
    static void applyHighContrastTemplate();
    static void applyNatureTemplate();
    static void applyOceanTemplate();
    static void applySunsetTemplate();
    
    // Helper to generate preview
    static QPixmap generatePreview(Template tmpl);
};

} // namespace llm_re::ui_v2