#pragma once

#include "../../../core/ui_v2_common.h"
#include "../../../core/animation_manager.h"

namespace llm_re::ui_v2 {

class AnimationConfigWidget : public QWidget {
    Q_OBJECT

public:
    explicit AnimationConfigWidget(QWidget* parent = nullptr);
    ~AnimationConfigWidget() = default;

    // Load current settings
    void loadSettings();
    
    // Get current settings
    bool animationsEnabled() const;
    qreal globalSpeed() const;
    std::map<AnimationManager::AnimationType, bool> animationStates() const;
    std::map<AnimationManager::AnimationType, int> animationDurations() const;

signals:
    void settingChanged();

private slots:
    void onEnableToggled(bool enabled);
    void onSpeedChanged(int value);
    void onAnimationToggled();
    void onDurationChanged();
    void onEasingChanged();
    void onTestAnimation();
    void updateEasingPreview();

private:
    void setupUI();
    void createGlobalSettings();
    void createAnimationList();
    void createEasingPreview();
    void createTestArea();
    
    // Easing curve painter
    class EasingCurveWidget : public QWidget {
    public:
        explicit EasingCurveWidget(QWidget* parent = nullptr);
        void setEasingType(AnimationManager::EasingType type);
        
    protected:
        void paintEvent(QPaintEvent* event) override;
        
    private:
        AnimationManager::EasingType easingType_ = AnimationManager::EasingType::Linear;
        void drawCurve(QPainter& painter, const QEasingCurve& curve);
    };
    
    // Global settings
    QCheckBox* enableCheck_ = nullptr;
    QSlider* speedSlider_ = nullptr;
    QLabel* speedLabel_ = nullptr;
    
    // Animation-specific settings
    struct AnimationConfig {
        QCheckBox* enabledCheck = nullptr;
        QSpinBox* durationSpin = nullptr;
        QComboBox* easingCombo = nullptr;
        QPushButton* testButton = nullptr;
    };
    
    std::map<AnimationManager::AnimationType, AnimationConfig> animationConfigs_;
    
    // Easing preview
    EasingCurveWidget* easingPreview_ = nullptr;
    QComboBox* easingTypeCombo_ = nullptr;
    
    // Test widgets
    QWidget* testWidget_ = nullptr;
    QPushButton* testFadeButton_ = nullptr;
    QPushButton* testSlideButton_ = nullptr;
    QPushButton* testBounceButton_ = nullptr;
    QPushButton* testAllButton_ = nullptr;
    
    // Current selected animation for preview
    AnimationManager::AnimationType selectedAnimation_ = AnimationManager::AnimationType::FadeIn;
};

} // namespace llm_re::ui_v2