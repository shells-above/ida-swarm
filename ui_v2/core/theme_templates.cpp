#include "ui_v2_common.h"
#include "theme_templates.h"

#include "animation_manager.h"
#include "effects_manager.h"
#include "theme_manager.h"
#include "ui_utils.h"

namespace llm_re::ui_v2 {

std::vector<ThemeTemplates::TemplateInfo> ThemeTemplates::getAvailableTemplates() {
    return {
        {
            "Minimal",
            "Clean and distraction-free with subtle colors and minimal effects",
            "Modern",
            generatePreview(Template::Minimal),
            ThemeMetadata{
                "Minimal", "LLM RE Team", "1.0",
                "A clean, minimal theme focused on clarity and simplicity",
                "light", QDateTime::currentDateTime(), QDateTime::currentDateTime()
            }
        },
        {
            "Vibrant",
            "Bold colors with modern effects and animations",
            "Modern",
            generatePreview(Template::Vibrant),
            ThemeMetadata{
                "Vibrant", "LLM RE Team", "1.0",
                "A vibrant theme with bold colors and modern effects",
                "dark", QDateTime::currentDateTime(), QDateTime::currentDateTime()
            }
        },
        {
            "Professional",
            "Business-oriented theme with muted colors",
            "Corporate",
            generatePreview(Template::Professional),
            ThemeMetadata{
                "Professional", "LLM RE Team", "1.0",
                "A professional theme suitable for business environments",
                "light", QDateTime::currentDateTime(), QDateTime::currentDateTime()
            }
        },
        {
            "Retro",
            "Terminal-inspired theme with classic green-on-black",
            "Classic",
            generatePreview(Template::Retro),
            ThemeMetadata{
                "Retro", "LLM RE Team", "1.0",
                "A retro terminal-inspired theme",
                "dark", QDateTime::currentDateTime(), QDateTime::currentDateTime()
            }
        },
        {
            "High Contrast",
            "Accessibility-focused theme with maximum contrast",
            "Accessibility",
            generatePreview(Template::HighContrast),
            ThemeMetadata{
                "High Contrast", "LLM RE Team", "1.0",
                "High contrast theme for improved accessibility",
                "dark", QDateTime::currentDateTime(), QDateTime::currentDateTime()
            }
        },
        {
            "Nature",
            "Earth tones inspired by nature",
            "Creative",
            generatePreview(Template::Nature),
            ThemeMetadata{
                "Nature", "LLM RE Team", "1.0",
                "A calming theme inspired by nature",
                "light", QDateTime::currentDateTime(), QDateTime::currentDateTime()
            }
        },
        {
            "Ocean",
            "Deep blues and aqua colors",
            "Creative",
            generatePreview(Template::Ocean),
            ThemeMetadata{
                "Ocean", "LLM RE Team", "1.0",
                "An ocean-inspired theme with calming blues",
                "dark", QDateTime::currentDateTime(), QDateTime::currentDateTime()
            }
        },
        {
            "Sunset",
            "Warm oranges and purples",
            "Creative",
            generatePreview(Template::Sunset),
            ThemeMetadata{
                "Sunset", "LLM RE Team", "1.0",
                "A warm theme inspired by sunset colors",
                "dark", QDateTime::currentDateTime(), QDateTime::currentDateTime()
            }
        }
    };
}

void ThemeTemplates::applyTemplate(Template tmpl) {
    switch (tmpl) {
        case Template::Minimal:
            applyMinimalTemplate();
            break;
        case Template::Vibrant:
            applyVibrantTemplate();
            break;
        case Template::Professional:
            applyProfessionalTemplate();
            break;
        case Template::Retro:
            applyRetroTemplate();
            break;
        case Template::HighContrast:
            applyHighContrastTemplate();
            break;
        case Template::Nature:
            applyNatureTemplate();
            break;
        case Template::Ocean:
            applyOceanTemplate();
            break;
        case Template::Sunset:
            applySunsetTemplate();
            break;
    }
}

ThemeTemplates::TemplateInfo ThemeTemplates::getTemplateInfo(Template tmpl) {
    auto templates = getAvailableTemplates();
    return templates[static_cast<int>(tmpl)];
}

void ThemeTemplates::createThemeFromTemplate(Template tmpl, const QString& newThemeName) {
    // Apply the template
    applyTemplate(tmpl);
    
    // Create and save theme using new architecture
    auto& tm = ThemeManager::instance();
    
    // Mark current state as modified
    tm.markModified();
    
    // Save with the new name
    if (!tm.saveThemeAs(newThemeName)) {
        qWarning() << "Failed to create theme from template:" << newThemeName;
    }
}

void ThemeTemplates::applyMinimalTemplate() {
    auto& tm = ThemeManager::instance();
    auto& am = AnimationManager::instance();
    auto& em = EffectsManager::instance();
    
    // Colors - light and minimal
    tm.setColor("primary", QColor("#2196F3"));
    tm.setColor("primaryHover", QColor("#1976D2"));
    tm.setColor("primaryActive", QColor("#1565C0"));
    
    tm.setColor("background", QColor("#FAFAFA"));
    tm.setColor("surface", QColor("#FFFFFF"));
    tm.setColor("surfaceHover", QColor("#F5F5F5"));
    tm.setColor("border", QColor("#E0E0E0"));
    
    tm.setColor("textPrimary", QColor("#212121"));
    tm.setColor("textSecondary", QColor("#757575"));
    
    tm.setColor("success", QColor("#4CAF50"));
    tm.setColor("warning", QColor("#FF9800"));
    tm.setColor("error", QColor("#F44336"));
    
    // Typography - clean and readable
    tm.setFontScale(1.0);
    
    // Animations - subtle
    am.setGlobalSpeed(1.2); // Slightly faster animations for minimal theme
    am.setAnimationsEnabled(true);
    
    // Effects - minimal
    em.setEffectsEnabled(true);
    
    // Component styles
    tm.setDensityMode(1); // Cozy
    tm.setCornerRadius(4);
}

void ThemeTemplates::applyVibrantTemplate() {
    auto& tm = ThemeManager::instance();
    auto& am = AnimationManager::instance();
    auto& em = EffectsManager::instance();
    
    // Colors - vibrant and bold
    tm.setColor("primary", QColor("#FF4081"));
    tm.setColor("primaryHover", QColor("#F50057"));
    tm.setColor("primaryActive", QColor("#C51162"));
    
    tm.setColor("background", QColor("#0A0E27"));
    tm.setColor("surface", QColor("#151837"));
    tm.setColor("surfaceHover", QColor("#1F2347"));
    tm.setColor("border", QColor("#2A2E57"));
    
    tm.setColor("textPrimary", QColor("#FFFFFF"));
    tm.setColor("textSecondary", QColor("#B0B9FF"));
    
    tm.setColor("success", QColor("#00E676"));
    tm.setColor("warning", QColor("#FFEA00"));
    tm.setColor("error", QColor("#FF5252"));
    
    // Accent colors for highlights
    tm.setColor("accent", QColor("#00BCD4"));
    tm.setColor("accent2", QColor("#7C4DFF"));
    
    // Typography - modern
    tm.setFontScale(1.05);
    
    // Animations - smooth and noticeable
    am.setGlobalSpeed(0.8); // Slower animations for more dramatic effect
    am.setAnimationsEnabled(true);
    
    // Effects - modern with glow
    em.setEffectsEnabled(true);
    em.setEffectQuality(100); // Maximum quality for vibrant theme
    
    // Component styles
    tm.setDensityMode(2); // Spacious
    tm.setCornerRadius(8);
    
    // Chart styles
    tm.setChartStyle(ThemeManager::ChartStyle::Neon);
}

void ThemeTemplates::applyProfessionalTemplate() {
    auto& tm = ThemeManager::instance();
    auto& am = AnimationManager::instance();
    auto& em = EffectsManager::instance();
    
    // Colors - muted and professional
    tm.setColor("primary", QColor("#37474F"));
    tm.setColor("primaryHover", QColor("#455A64"));
    tm.setColor("primaryActive", QColor("#263238"));
    
    tm.setColor("background", QColor("#F5F5F5"));
    tm.setColor("surface", QColor("#FFFFFF"));
    tm.setColor("surfaceHover", QColor("#FAFAFA"));
    tm.setColor("border", QColor("#CFD8DC"));
    
    tm.setColor("textPrimary", QColor("#263238"));
    tm.setColor("textSecondary", QColor("#607D8B"));
    
    tm.setColor("success", QColor("#43A047"));
    tm.setColor("warning", QColor("#FB8C00"));
    tm.setColor("error", QColor("#E53935"));
    
    // Typography - professional
    tm.setFontScale(0.95);
    
    // Animations - quick and subtle
    am.setGlobalSpeed(1.5); // Faster animations for professional look
    am.setAnimationsEnabled(true);
    
    // Effects - minimal
    em.setEffectsEnabled(true);
    em.setEffectQuality(80); // Good balance of performance and quality
    
    // Component styles
    tm.setDensityMode(0); // Compact
    tm.setCornerRadius(2);
    
    // Chart styles
    tm.setChartStyle(ThemeManager::ChartStyle::Corporate);
}

void ThemeTemplates::applyRetroTemplate() {
    auto& tm = ThemeManager::instance();
    auto& am = AnimationManager::instance();
    auto& em = EffectsManager::instance();
    
    // Colors - classic terminal
    tm.setColor("primary", QColor("#00FF00"));
    tm.setColor("primaryHover", QColor("#33FF33"));
    tm.setColor("primaryActive", QColor("#00CC00"));
    
    tm.setColor("background", QColor("#000000"));
    tm.setColor("surface", QColor("#0A0A0A"));
    tm.setColor("surfaceHover", QColor("#1A1A1A"));
    tm.setColor("border", QColor("#00FF00"));
    
    tm.setColor("textPrimary", QColor("#00FF00"));
    tm.setColor("textSecondary", QColor("#00CC00"));
    
    tm.setColor("success", QColor("#00FF00"));
    tm.setColor("warning", QColor("#FFFF00"));
    tm.setColor("error", QColor("#FF0000"));
    
    // Code colors
    tm.setColor("codeBackground", QColor("#000000"));
    tm.setColor("codeText", QColor("#00FF00"));
    
    // Typography - monospace
    tm.setFontScale(1.0);
    // Would set font family to monospace here
    
    // Animations - instant
    am.setAnimationsEnabled(false); // No animations for retro terminal look
    
    // Effects - none
    em.setEffectsEnabled(false);
    
    // Component styles
    tm.setDensityMode(1); // Cozy
    tm.setCornerRadius(0);
    
    // Chart styles
    tm.setChartStyle(ThemeManager::ChartStyle::Terminal);
}

void ThemeTemplates::applyHighContrastTemplate() {
    auto& tm = ThemeManager::instance();
    auto& am = AnimationManager::instance();
    auto& em = EffectsManager::instance();
    
    // Colors - maximum contrast
    tm.setColor("primary", QColor("#FFFF00"));
    tm.setColor("primaryHover", QColor("#FFFF33"));
    tm.setColor("primaryActive", QColor("#CCCC00"));
    
    tm.setColor("background", QColor("#000000"));
    tm.setColor("surface", QColor("#000000"));
    tm.setColor("surfaceHover", QColor("#1A1A1A"));
    tm.setColor("border", QColor("#FFFFFF"));
    
    tm.setColor("textPrimary", QColor("#FFFFFF"));
    tm.setColor("textSecondary", QColor("#FFFF00"));
    
    tm.setColor("success", QColor("#00FF00"));
    tm.setColor("warning", QColor("#FFFF00"));
    tm.setColor("error", QColor("#FF0000"));
    
    // High contrast for UI elements
    tm.setColor("selection", QColor("#FFFF00"));
    tm.setColor("focus", QColor("#00FFFF"));
    
    // Typography - larger
    tm.setFontScale(1.2);
    
    // Animations - disabled for accessibility
    am.setAnimationsEnabled(false); // No animations for better accessibility
    
    // Effects - strong borders only
    em.setEffectsEnabled(false);
    
    // Component styles
    tm.setDensityMode(2); // Spacious
    tm.setCornerRadius(0);
}

void ThemeTemplates::applyNatureTemplate() {
    auto& tm = ThemeManager::instance();
    auto& am = AnimationManager::instance();
    auto& em = EffectsManager::instance();
    
    // Colors - earth tones
    tm.setColor("primary", QColor("#4CAF50"));
    tm.setColor("primaryHover", QColor("#66BB6A"));
    tm.setColor("primaryActive", QColor("#388E3C"));
    
    tm.setColor("background", QColor("#F1F8E9"));
    tm.setColor("surface", QColor("#FFFFFF"));
    tm.setColor("surfaceHover", QColor("#E8F5E9"));
    tm.setColor("border", QColor("#C8E6C9"));
    
    tm.setColor("textPrimary", QColor("#1B5E20"));
    tm.setColor("textSecondary", QColor("#558B2F"));
    
    tm.setColor("success", QColor("#8BC34A"));
    tm.setColor("warning", QColor("#FFC107"));
    tm.setColor("error", QColor("#795548"));
    
    // Accent nature colors
    tm.setColor("accent", QColor("#795548")); // Brown
    tm.setColor("accent2", QColor("#FF6F00")); // Amber
    
    // Typography - organic feel
    tm.setFontScale(1.0);
    
    // Animations - smooth and natural
    am.setGlobalSpeed(1.0); // Standard animation speed
    am.setAnimationsEnabled(true);
    
    // Effects - soft shadows
    em.setEffectsEnabled(true);
    em.setEffectQuality(90); // High quality effects
    
    // Component styles
    tm.setDensityMode(1); // Cozy
    tm.setCornerRadius(12); // Organic rounded corners
}

void ThemeTemplates::applyOceanTemplate() {
    auto& tm = ThemeManager::instance();
    auto& am = AnimationManager::instance();
    auto& em = EffectsManager::instance();
    
    // Colors - ocean blues
    tm.setColor("primary", QColor("#0288D1"));
    tm.setColor("primaryHover", QColor("#039BE5"));
    tm.setColor("primaryActive", QColor("#0277BD"));
    
    tm.setColor("background", QColor("#001529"));
    tm.setColor("surface", QColor("#002744"));
    tm.setColor("surfaceHover", QColor("#003459"));
    tm.setColor("border", QColor("#004A7C"));
    
    tm.setColor("textPrimary", QColor("#E3F2FD"));
    tm.setColor("textSecondary", QColor("#90CAF9"));
    
    tm.setColor("success", QColor("#00BCD4"));
    tm.setColor("warning", QColor("#FFB74D"));
    tm.setColor("error", QColor("#EF5350"));
    
    // Accent ocean colors
    tm.setColor("accent", QColor("#00ACC1")); // Cyan
    tm.setColor("accent2", QColor("#26C6DA")); // Light cyan
    
    // Typography
    tm.setFontScale(1.0);
    
    // Animations - flowing like water
    am.setGlobalSpeed(0.6); // Slower animations for ocean flow
    am.setAnimationsEnabled(true);
    
    // Effects - water-like
    em.setEffectsEnabled(true);
    em.setEffectQuality(100); // Maximum quality for ocean theme
    
    // Component styles
    tm.setDensityMode(1); // Cozy
    tm.setCornerRadius(16); // Smooth like water
    
    // Chart styles
    tm.setChartStyle(ThemeManager::ChartStyle::Glass);
}

void ThemeTemplates::applySunsetTemplate() {
    auto& tm = ThemeManager::instance();
    auto& am = AnimationManager::instance();
    auto& em = EffectsManager::instance();
    
    // Colors - sunset gradient
    tm.setColor("primary", QColor("#FF6B6B"));
    tm.setColor("primaryHover", QColor("#FF5252"));
    tm.setColor("primaryActive", QColor("#F44336"));
    
    tm.setColor("background", QColor("#1A0033"));
    tm.setColor("surface", QColor("#2D1B69"));
    tm.setColor("surfaceHover", QColor("#3D2B79"));
    tm.setColor("border", QColor("#4D3B89"));
    
    tm.setColor("textPrimary", QColor("#FFE0E0"));
    tm.setColor("textSecondary", QColor("#FFB3B3"));
    
    tm.setColor("success", QColor("#4ECDC4"));
    tm.setColor("warning", QColor("#FFE66D"));
    tm.setColor("error", QColor("#FF6B6B"));
    
    // Accent sunset colors
    tm.setColor("accent", QColor("#FF8C42")); // Orange
    tm.setColor("accent2", QColor("#FFD23F")); // Yellow
    
    // Typography
    tm.setFontScale(1.05);
    
    // Animations - warm and smooth
    am.setGlobalSpeed(0.7); // Slower animations for fantasy feel
    am.setAnimationsEnabled(true);
    
    // Effects - warm glow
    em.setEffectsEnabled(true);
    em.setEffectQuality(100); // Maximum quality for fantasy theme
    
    // Component styles
    tm.setDensityMode(2); // Spacious
    tm.setCornerRadius(20); // Very rounded
    
    // Chart styles
    tm.setChartStyle(ThemeManager::ChartStyle::Neon);
}

QPixmap ThemeTemplates::generatePreview(Template tmpl) {
    QPixmap preview(200, 120);
    preview.fill(Qt::transparent);
    
    QPainter painter(&preview);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw a simple preview based on template
    QColor bgColor, fgColor, accentColor;
    
    switch (tmpl) {
        case Template::Minimal:
            bgColor = QColor("#FAFAFA");
            fgColor = QColor("#212121");
            accentColor = QColor("#2196F3");
            break;
        case Template::Vibrant:
            bgColor = QColor("#0A0E27");
            fgColor = QColor("#FFFFFF");
            accentColor = QColor("#FF4081");
            break;
        case Template::Professional:
            bgColor = QColor("#F5F5F5");
            fgColor = QColor("#263238");
            accentColor = QColor("#37474F");
            break;
        case Template::Retro:
            bgColor = QColor("#000000");
            fgColor = QColor("#00FF00");
            accentColor = QColor("#00FF00");
            break;
        case Template::HighContrast:
            bgColor = QColor("#000000");
            fgColor = QColor("#FFFFFF");
            accentColor = QColor("#FFFF00");
            break;
        case Template::Nature:
            bgColor = QColor("#F1F8E9");
            fgColor = QColor("#1B5E20");
            accentColor = QColor("#4CAF50");
            break;
        case Template::Ocean:
            bgColor = QColor("#001529");
            fgColor = QColor("#E3F2FD");
            accentColor = QColor("#0288D1");
            break;
        case Template::Sunset:
            bgColor = QColor("#1A0033");
            fgColor = QColor("#FFE0E0");
            accentColor = QColor("#FF6B6B");
            break;
    }
    
    // Background
    painter.fillRect(preview.rect(), bgColor);
    
    // Draw sample UI elements
    painter.setPen(QPen(fgColor, 1));
    
    // Header bar
    QRect headerRect(0, 0, preview.width(), 30);
    painter.fillRect(headerRect, accentColor);
    
    // Sample text - use contrasting color for text on accent background
    painter.setPen(UIUtils::contrastColor(accentColor));
    painter.drawText(headerRect, Qt::AlignCenter, "Theme Preview");
    
    // Sample buttons
    painter.setPen(fgColor);
    QRect button1(10, 40, 60, 25);
    QRect button2(80, 40, 60, 25);
    
    painter.setBrush(accentColor);
    painter.drawRoundedRect(button1, 3, 3);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(button2, 3, 3);
    
    // Sample content area
    painter.setPen(QPen(fgColor.lighter(150), 1));
    painter.drawLine(10, 75, preview.width() - 10, 75);
    painter.drawLine(10, 85, preview.width() - 40, 85);
    painter.drawLine(10, 95, preview.width() - 60, 95);
    painter.drawLine(10, 105, preview.width() - 30, 105);
    
    return preview;
}

} // namespace llm_re::ui_v2