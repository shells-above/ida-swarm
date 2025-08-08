#pragma once

#include "color_picker_widget.h"
#include "../../../core/ui_v2_common.h"
#include "../../../core/effects_manager.h"

namespace llm_re::ui_v2 {

class EffectsConfigWidget : public QWidget {
    Q_OBJECT

public:
    explicit EffectsConfigWidget(QWidget* parent = nullptr);
    ~EffectsConfigWidget() = default;

    // Load current settings
    void loadSettings();
    
    // Get current settings
    bool effectsEnabled() const;
    int effectQuality() const;
    std::map<QString, QVariant> getEffectSettings() const;

signals:
    void settingChanged();

private slots:
    void onEnableToggled(bool enabled);
    void onQualityChanged(int value);
    void onShadowStyleChanged();
    void onGlowSettingChanged();
    void onBlurSettingChanged();
    void onGlassMorphismChanged();
    void onPresetSelected();
    void applyToTestWidget();

private:
    void setupUI();
    void createGlobalSettings();
    void createShadowSettings(QWidget* parent);
    void createGlowSettings(QWidget* parent);
    void createBlurSettings(QWidget* parent);
    void createGlassMorphismSettings(QWidget* parent);
    void createEffectPresets(QWidget* parent);
    void createTestArea();
    
    // Shadow preview widget
    class ShadowPreviewWidget : public QWidget {
    public:
        explicit ShadowPreviewWidget(QWidget* parent = nullptr);
        void setShadowStyle(EffectsManager::ShadowStyle style);
        void setCustomShadow(const QColor& color, qreal blur, const QPointF& offset);
        
    protected:
        void resizeEvent(QResizeEvent* event) override;
        
    private:
        QWidget* innerWidget_ = nullptr;
        EffectsManager::ShadowStyle currentStyle_ = EffectsManager::ShadowStyle::Elevated;
    };
    
    // Glow preview widget
    class GlowPreviewWidget : public QWidget {
    public:
        explicit GlowPreviewWidget(QWidget* parent = nullptr);
        void setGlowStyle(EffectsManager::GlowStyle style, const QColor& color);
        
    protected:
        void paintEvent(QPaintEvent* event) override;
        
    private:
        EffectsManager::GlowStyle glowStyle_ = EffectsManager::GlowStyle::Soft;
        QColor glowColor_;
    };
    
    // Global settings
    QCheckBox* enableCheck_ = nullptr;
    QSlider* qualitySlider_ = nullptr;
    QLabel* qualityLabel_ = nullptr;
    
    // Shadow settings
    QComboBox* shadowStyleCombo_ = nullptr;
    ColorPickerWidget* shadowColorPicker_ = nullptr;
    QSlider* shadowBlurSlider_ = nullptr;
    QSpinBox* shadowOffsetXSpin_ = nullptr;
    QSpinBox* shadowOffsetYSpin_ = nullptr;
    ShadowPreviewWidget* shadowPreview_ = nullptr;
    
    // Glow settings
    QComboBox* glowStyleCombo_ = nullptr;
    ColorPickerWidget* glowColorPicker_ = nullptr;
    QSlider* glowIntensitySlider_ = nullptr;
    QSlider* glowRadiusSlider_ = nullptr;
    GlowPreviewWidget* glowPreview_ = nullptr;
    
    // Blur settings
    QCheckBox* blurEnabledCheck_ = nullptr;
    QSlider* blurRadiusSlider_ = nullptr;
    QLabel* blurRadiusLabel_ = nullptr;
    
    // Glass morphism settings
    QCheckBox* glassMorphismCheck_ = nullptr;
    QSlider* glassBlurSlider_ = nullptr;
    QSlider* glassOpacitySlider_ = nullptr;
    QLabel* glassBlurLabel_ = nullptr;
    QLabel* glassOpacityLabel_ = nullptr;
    
    // Effect presets
    QListWidget* presetsList_ = nullptr;
    struct EffectPreset {
        QString name;
        QString description;
        EffectsManager::EffectSet effects;
    };
    std::vector<EffectPreset> presets_;
    
    // Test area
    QWidget* testWidget_ = nullptr;
    QPushButton* applyEffectsButton_ = nullptr;
    QPushButton* clearEffectsButton_ = nullptr;
    
    void loadPresets();
    void updateShadowPreview();
    void updateGlowPreview();
};

} // namespace llm_re::ui_v2