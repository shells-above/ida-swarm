#include "../../../core/ui_v2_common.h"
#include "animation_config_widget.h"
#include "../../../core/theme_manager.h"

namespace llm_re::ui_v2 {

AnimationConfigWidget::AnimationConfigWidget(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    loadSettings();
}

void AnimationConfigWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    
    // Create sections
    createGlobalSettings();
    createAnimationList();
    createEasingPreview();
    createTestArea();
    
    mainLayout->addStretch();
}

void AnimationConfigWidget::createGlobalSettings() {
    auto* group = new QGroupBox("Global Animation Settings");
    auto* layout = new QVBoxLayout(group);
    
    // Enable/disable all animations
    enableCheck_ = new QCheckBox("Enable Animations");
    connect(enableCheck_, &QCheckBox::toggled, this, &AnimationConfigWidget::onEnableToggled);
    layout->addWidget(enableCheck_);
    
    // Global speed multiplier
    auto* speedLayout = new QHBoxLayout();
    speedLayout->addWidget(new QLabel("Global Speed:"));
    
    speedSlider_ = new QSlider(Qt::Horizontal);
    speedSlider_->setRange(10, 300);  // 0.1x to 3.0x
    speedSlider_->setValue(100);      // 1.0x default
    speedSlider_->setTickPosition(QSlider::TicksBelow);
    speedSlider_->setTickInterval(50);
    
    speedLabel_ = new QLabel("1.0x");
    speedLabel_->setMinimumWidth(50);
    
    connect(speedSlider_, &QSlider::valueChanged, this, &AnimationConfigWidget::onSpeedChanged);
    
    speedLayout->addWidget(speedSlider_);
    speedLayout->addWidget(speedLabel_);
    layout->addLayout(speedLayout);
    
    static_cast<QVBoxLayout*>(this->layout())->addWidget(group);
}

void AnimationConfigWidget::createAnimationList() {
    auto* group = new QGroupBox("Animation Types");
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    
    auto* widget = new QWidget();
    auto* layout = new QGridLayout(widget);
    
    // Animation type names
    const std::vector<std::pair<AnimationManager::AnimationType, QString>> animations = {
        {AnimationManager::AnimationType::FadeIn, "Fade In"},
        {AnimationManager::AnimationType::FadeOut, "Fade Out"},
        {AnimationManager::AnimationType::SlideIn, "Slide In"},
        {AnimationManager::AnimationType::SlideOut, "Slide Out"},
        {AnimationManager::AnimationType::Scale, "Scale"},
        {AnimationManager::AnimationType::Bounce, "Bounce"},
        {AnimationManager::AnimationType::Shake, "Shake"},
        {AnimationManager::AnimationType::Pulse, "Pulse"},
        {AnimationManager::AnimationType::TypeWriter, "TypeWriter"},
        {AnimationManager::AnimationType::Elastic, "Elastic"},
        {AnimationManager::AnimationType::Back, "Back"},
        {AnimationManager::AnimationType::Rotate, "Rotate"},
        {AnimationManager::AnimationType::Flip, "Flip"},
        {AnimationManager::AnimationType::Glow, "Glow"}
    };
    
    int row = 0;
    for (const auto& [type, name] : animations) {
        AnimationConfig config;
        
        // Enable checkbox
        config.enabledCheck = new QCheckBox(name);
        config.enabledCheck->setChecked(true);
        connect(config.enabledCheck, &QCheckBox::toggled, 
                this, &AnimationConfigWidget::onAnimationToggled);
        layout->addWidget(config.enabledCheck, row, 0);
        
        // Duration spinner
        config.durationSpin = new QSpinBox();
        config.durationSpin->setRange(50, 5000);
        config.durationSpin->setSuffix(" ms");
        config.durationSpin->setValue(AnimationManager::standardDuration(type));
        connect(config.durationSpin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, &AnimationConfigWidget::onDurationChanged);
        layout->addWidget(config.durationSpin, row, 1);
        
        // Easing curve selector
        config.easingCombo = new QComboBox();
        config.easingCombo->addItems({
            "Linear", "InSine", "OutSine", "InOutSine",
            "InQuad", "OutQuad", "InOutQuad",
            "InCubic", "OutCubic", "InOutCubic",
            "InQuart", "OutQuart", "InOutQuart",
            "InExpo", "OutExpo", "InOutExpo",
            "InCirc", "OutCirc", "InOutCirc",
            "InElastic", "OutElastic", "InOutElastic",
            "InBack", "OutBack", "InOutBack",
            "InBounce", "OutBounce", "InOutBounce"
        });
        config.easingCombo->setCurrentText("OutCubic");
        connect(config.easingCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &AnimationConfigWidget::onEasingChanged);
        layout->addWidget(config.easingCombo, row, 2);
        
        // Test button
        config.testButton = new QPushButton("Test");
        config.testButton->setMaximumWidth(60);
        connect(config.testButton, &QPushButton::clicked, 
                [this, type]() {
                    selectedAnimation_ = type;
                    onTestAnimation();
                });
        layout->addWidget(config.testButton, row, 3);
        
        animationConfigs_[type] = config;
        row++;
    }
    
    scrollArea->setWidget(widget);
    
    auto* groupLayout = new QVBoxLayout(group);
    groupLayout->addWidget(scrollArea);
    
    static_cast<QVBoxLayout*>(this->layout())->addWidget(group);
}

void AnimationConfigWidget::createEasingPreview() {
    auto* group = new QGroupBox("Easing Curve Preview");
    auto* layout = new QVBoxLayout(group);
    
    // Easing type selector
    auto* selectorLayout = new QHBoxLayout();
    selectorLayout->addWidget(new QLabel("Easing Type:"));
    
    easingTypeCombo_ = new QComboBox();
    easingTypeCombo_->addItems({
        "Linear", "InSine", "OutSine", "InOutSine",
        "InQuad", "OutQuad", "InOutQuad",
        "InCubic", "OutCubic", "InOutCubic",
        "InQuart", "OutQuart", "InOutQuart",
        "InQuint", "OutQuint", "InOutQuint",
        "InExpo", "OutExpo", "InOutExpo",
        "InCirc", "OutCirc", "InOutCirc",
        "InElastic", "OutElastic", "InOutElastic",
        "InBack", "OutBack", "InOutBack",
        "InBounce", "OutBounce", "InOutBounce"
    });
    connect(easingTypeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AnimationConfigWidget::updateEasingPreview);
    selectorLayout->addWidget(easingTypeCombo_);
    selectorLayout->addStretch();
    layout->addLayout(selectorLayout);
    
    // Easing curve visualization
    easingPreview_ = new EasingCurveWidget();
    easingPreview_->setFixedHeight(150);
    layout->addWidget(easingPreview_);
    
    static_cast<QVBoxLayout*>(this->layout())->addWidget(group);
}

void AnimationConfigWidget::createTestArea() {
    auto* group = new QGroupBox("Animation Test");
    auto* layout = new QVBoxLayout(group);
    
    // Test widget
    testWidget_ = new QWidget();
    testWidget_->setFixedSize(200, 100);
    // Use explicit colors from theme instead of palette to avoid inheriting IDA's theme
    const auto& colors = ThemeManager::instance().colors();
    testWidget_->setStyleSheet(QString("background-color: %1; border-radius: 8px;")
                              .arg(colors.primary.name()));
    
    auto* testContainer = new QWidget();
    testContainer->setFixedHeight(120);
    auto* containerLayout = new QHBoxLayout(testContainer);
    containerLayout->addStretch();
    containerLayout->addWidget(testWidget_);
    containerLayout->addStretch();
    
    layout->addWidget(testContainer);
    
    // Test buttons
    auto* buttonLayout = new QHBoxLayout();
    
    testFadeButton_ = new QPushButton("Test Fade");
    connect(testFadeButton_, &QPushButton::clicked, [this]() {
        AnimationManager::fadeOut(testWidget_, 300, [this]() {
            AnimationManager::fadeIn(testWidget_, 300);
        });
    });
    buttonLayout->addWidget(testFadeButton_);
    
    testSlideButton_ = new QPushButton("Test Slide");
    connect(testSlideButton_, &QPushButton::clicked, [this]() {
        AnimationManager::slideOut(testWidget_, AnimationManager::SlideDirection::Right, 300, [this]() {
            AnimationManager::slideIn(testWidget_, AnimationManager::SlideDirection::Right, 300);
        });
    });
    buttonLayout->addWidget(testSlideButton_);
    
    testBounceButton_ = new QPushButton("Test Bounce");
    connect(testBounceButton_, &QPushButton::clicked, [this]() {
        AnimationManager::bounce(testWidget_);
    });
    buttonLayout->addWidget(testBounceButton_);
    
    testAllButton_ = new QPushButton("Test Sequence");
    connect(testAllButton_, &QPushButton::clicked, [this]() {
        // Create a sequence of animations
        AnimationManager::scale(testWidget_, 1.0, 1.2, 200, [this]() {
            AnimationManager::rotate(testWidget_, 360, 400, [this]() {
                AnimationManager::scale(testWidget_, 1.2, 1.0, 200);
            });
        });
    });
    buttonLayout->addWidget(testAllButton_);
    
    layout->addLayout(buttonLayout);
    
    static_cast<QVBoxLayout*>(this->layout())->addWidget(group);
}

void AnimationConfigWidget::loadSettings() {
    auto& am = AnimationManager::instance();
    
    enableCheck_->setChecked(am.animationsEnabled());
    speedSlider_->setValue(am.globalSpeed() * 100);
    
    // Load individual animation settings
    // In a real implementation, these would be stored in theme
}

void AnimationConfigWidget::onEnableToggled(bool enabled) {
    AnimationManager::instance().setAnimationsEnabled(enabled);
    
    // Enable/disable all animation controls
    speedSlider_->setEnabled(enabled);
    for (auto& [type, config] : animationConfigs_) {
        config.durationSpin->setEnabled(enabled && config.enabledCheck->isChecked());
        config.easingCombo->setEnabled(enabled && config.enabledCheck->isChecked());
        config.testButton->setEnabled(enabled && config.enabledCheck->isChecked());
    }
    
    emit settingChanged();
}

void AnimationConfigWidget::onSpeedChanged(int value) {
    qreal speed = value / 100.0;
    speedLabel_->setText(QString("%1x").arg(speed, 0, 'f', 1));
    AnimationManager::instance().setGlobalSpeed(speed);
    emit settingChanged();
}

void AnimationConfigWidget::onAnimationToggled() {
    auto* check = qobject_cast<QCheckBox*>(sender());
    if (!check) return;
    
    // Find which animation this belongs to
    for (auto& [type, config] : animationConfigs_) {
        if (config.enabledCheck == check) {
            bool enabled = check->isChecked() && enableCheck_->isChecked();
            config.durationSpin->setEnabled(enabled);
            config.easingCombo->setEnabled(enabled);
            config.testButton->setEnabled(enabled);
            break;
        }
    }
    
    emit settingChanged();
}

void AnimationConfigWidget::onDurationChanged() {
    emit settingChanged();
}

void AnimationConfigWidget::onEasingChanged() {
    emit settingChanged();
}

void AnimationConfigWidget::onTestAnimation() {
    if (!animationConfigs_.contains(selectedAnimation_)) return;
    
    const auto& config = animationConfigs_[selectedAnimation_];
    int duration = config.durationSpin->value();
    
    // Get easing type from combo
    auto easingType = static_cast<AnimationManager::EasingType>(config.easingCombo->currentIndex());
    
    // Test the selected animation
    switch (selectedAnimation_) {
        case AnimationManager::AnimationType::FadeIn:
            testWidget_->hide();
            AnimationManager::fadeIn(testWidget_, duration);
            break;
        case AnimationManager::AnimationType::FadeOut:
            AnimationManager::fadeOut(testWidget_, duration, [this]() {
                testWidget_->show();
            });
            break;
        case AnimationManager::AnimationType::Bounce:
            AnimationManager::bounce(testWidget_, 20, duration);
            break;
        case AnimationManager::AnimationType::Shake:
            AnimationManager::shake(testWidget_, 10, duration);
            break;
        case AnimationManager::AnimationType::Pulse:
            AnimationManager::pulse(testWidget_, 1.2, duration);
            break;
        case AnimationManager::AnimationType::Rotate:
            AnimationManager::rotate(testWidget_, 360, duration);
            break;
        // Add other animation tests...
    }
}

void AnimationConfigWidget::updateEasingPreview() {
    auto easingType = static_cast<AnimationManager::EasingType>(easingTypeCombo_->currentIndex());
    easingPreview_->setEasingType(easingType);
}

// EasingCurveWidget implementation

AnimationConfigWidget::EasingCurveWidget::EasingCurveWidget(QWidget* parent)
    : QWidget(parent) {
    setMinimumHeight(100);
}

void AnimationConfigWidget::EasingCurveWidget::setEasingType(AnimationManager::EasingType type) {
    easingType_ = type;
    update();
}

void AnimationConfigWidget::EasingCurveWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Background
    painter.fillRect(rect(), ThemeManager::instance().colors().surface);
    
    // Grid
    painter.setPen(QPen(ThemeManager::instance().colors().border, 1, Qt::DotLine));
    for (int i = 0; i <= 10; ++i) {
        int x = rect().width() * i / 10;
        int y = rect().height() * i / 10;
        painter.drawLine(x, 0, x, rect().height());
        painter.drawLine(0, y, rect().width(), y);
    }
    
    // Draw the easing curve
    QEasingCurve curve = AnimationManager::easingCurve(easingType_);
    drawCurve(painter, curve);
    
    // Draw axes
    painter.setPen(QPen(ThemeManager::instance().colors().textPrimary, 2));
    painter.drawLine(0, rect().height(), rect().width(), rect().height());
    painter.drawLine(0, 0, 0, rect().height());
    
    // Labels
    painter.setPen(ThemeManager::instance().colors().textSecondary);
    painter.drawText(5, rect().height() - 5, "0");
    painter.drawText(rect().width() - 15, rect().height() - 5, "1");
    painter.drawText(5, 15, "1");
}

void AnimationConfigWidget::EasingCurveWidget::drawCurve(QPainter& painter, const QEasingCurve& curve) {
    QPainterPath path;
    
    const int steps = 100;
    for (int i = 0; i <= steps; ++i) {
        qreal t = i / qreal(steps);
        qreal value = curve.valueForProgress(t);
        
        int x = t * rect().width();
        int y = rect().height() - value * rect().height();
        
        if (i == 0) {
            path.moveTo(x, y);
        } else {
            path.lineTo(x, y);
        }
    }
    
    painter.setPen(QPen(ThemeManager::instance().colors().primary, 3));
    painter.drawPath(path);
}

} // namespace llm_re::ui_v2