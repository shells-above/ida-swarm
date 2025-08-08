#pragma once

#include "../../core/ui_v2_common.h"
#include "../../core/base_styled_widget.h"
#include "../../core/theme_constants.h"
#include "../../core/theme_templates.h"

namespace llm_re::ui_v2 {

// Forward declarations
class ColorPickerWidget;
class ThemePreviewWidget;
class AnimationConfigWidget;
class EffectsConfigWidget;
class ChartThemeWidget;
class ThemeTemplateSelector;
class AccessibilityPanel;

class ThemeEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ThemeEditorDialog(QWidget* parent = nullptr);
    ~ThemeEditorDialog() = default;

    // Load current theme into editor
    void loadCurrentTheme();
    
    // Set a specific theme to edit
    void loadTheme(const QString& themeName);

signals:
    void themeApplied();
    void themeSaved(const QString& themeName);

private slots:
    void onColorChanged(const QString& colorName, const QColor& color);
    void onAnimationSettingChanged();
    void onEffectSettingChanged();
    void onChartSettingChanged();
    void onTypographyChanged();
    void onComponentStyleChanged();
    
    void onSaveTheme();
    void onSaveThemeAs();
    void onLoadTheme();
    void onExportTheme();
    void onImportTheme();
    void onResetTheme();
    void onApplyTheme();
    
    void onThemeNameChanged(const QString& name);
    void updatePreview();
    
    // Template selection
    void onTemplateSelected(ThemeTemplates::Template tmpl);
    void onCreateFromTemplate(ThemeTemplates::Template tmpl, const QString& name);
    
private:
    void setupUI();
    void createMenuBar();
    void createToolBar();
    void createTabs();
    void createTemplatesTab();
    void createColorsTab();
    void createTypographyTab();
    void createComponentsTab();
    void createAnimationsTab();
    void createEffectsTab();
    void createChartsTab();
    void createAccessibilityTab();
    QWidget* createMetadataWidget();
    
    void applyThemeChanges();
    void updateStatusBar();
    
    void showColorPreview(const QString& colorName);
    void showComponentPreview(const QString& componentName);
    
    bool hasUnsavedChanges() const { return hasChanges_; }
    void setHasChanges(bool hasChanges);
    
    void updateWindowTitle();
    QString generateThemeName() const;
    
    // UI Components
    QTabWidget* tabWidget_ = nullptr;
    ThemePreviewWidget* previewWidget_ = nullptr;
    QSplitter* mainSplitter_ = nullptr;
    
    // Metadata
    QLineEdit* themeNameEdit_ = nullptr;
    QLineEdit* authorEdit_ = nullptr;
    QLineEdit* versionEdit_ = nullptr;
    QTextEdit* descriptionEdit_ = nullptr;
    QComboBox* baseThemeCombo_ = nullptr;
    
    // Color editing
    std::map<QString, ColorPickerWidget*> colorPickers_;
    
    // Typography
    QFontComboBox* baseFontCombo_ = nullptr;
    QFontComboBox* codeFontCombo_ = nullptr;
    QSlider* fontScaleSlider_ = nullptr;
    QLabel* fontScaleLabel_ = nullptr;
    std::map<QString, QSpinBox*> fontSizeSpins_;
    
    // Components
    QComboBox* densityCombo_ = nullptr;
    std::map<QString, QWidget*> componentEditors_;
    
    // Animations
    AnimationConfigWidget* animationConfig_ = nullptr;
    
    // Effects
    EffectsConfigWidget* effectsConfig_ = nullptr;
    
    // Charts
    ChartThemeWidget* chartConfig_ = nullptr;
    
    // Templates
    ThemeTemplateSelector* templateSelector_ = nullptr;
    
    // Accessibility
    AccessibilityPanel* accessibilityPanel_ = nullptr;
    
    // Actions
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* loadAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* importAction_ = nullptr;
    QAction* resetAction_ = nullptr;
    QAction* applyAction_ = nullptr;
    
    // State - simplified, all state is in ThemeManager now
    bool hasChanges_ = false;
    
    // Status bar for showing theme info
    QStatusBar* statusBar_ = nullptr;
    
    // Preview popup
    QWidget* previewPopup_ = nullptr;
    QTimer* previewTimer_ = nullptr;
    
    // Hot reload
    bool hotReloadEnabled_ = true;
    QCheckBox* hotReloadCheck_ = nullptr;
};

} // namespace llm_re::ui_v2