#include "../../../core/ui_v2_common.h"
#include "effects_config_widget.h"
#include "color_picker_widget.h"
#include "../../../core/theme_manager.h"

namespace llm_re::ui_v2 {

EffectsConfigWidget::EffectsConfigWidget(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    loadSettings();
    loadPresets();
}

void EffectsConfigWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    
    createGlobalSettings();
    
    // Tab widget for different effect types
    auto* tabWidget = new QTabWidget();
    
    // Shadow tab
    auto* shadowWidget = new QWidget();
    auto* shadowLayout = new QVBoxLayout(shadowWidget);
    createShadowSettings(shadowWidget);
    tabWidget->addTab(shadowWidget, "Shadows");
    
    // Glow tab
    auto* glowWidget = new QWidget();
    auto* glowLayout = new QVBoxLayout(glowWidget);
    createGlowSettings(glowWidget);
    tabWidget->addTab(glowWidget, "Glow");
    
    // Blur & Glass tab
    auto* blurWidget = new QWidget();
    auto* blurLayout = new QVBoxLayout(blurWidget);
    createBlurSettings(blurWidget);
    createGlassMorphismSettings(blurWidget);
    tabWidget->addTab(blurWidget, "Blur & Glass");
    
    // Presets tab
    auto* presetsWidget = new QWidget();
    auto* presetsLayout = new QVBoxLayout(presetsWidget);
    createEffectPresets(presetsWidget);
    tabWidget->addTab(presetsWidget, "Presets");
    
    mainLayout->addWidget(tabWidget);
    
    createTestArea();
    mainLayout->addStretch();
}

void EffectsConfigWidget::createGlobalSettings() {
    auto* group = new QGroupBox("Global Effect Settings");
    auto* layout = new QVBoxLayout(group);
    
    // Enable/disable all effects
    enableCheck_ = new QCheckBox("Enable Visual Effects");
    connect(enableCheck_, &QCheckBox::toggled, this, &EffectsConfigWidget::onEnableToggled);
    layout->addWidget(enableCheck_);
    
    // Effect quality
    auto* qualityLayout = new QHBoxLayout();
    qualityLayout->addWidget(new QLabel("Effect Quality:"));
    
    qualitySlider_ = new QSlider(Qt::Horizontal);
    qualitySlider_->setRange(0, 100);
    qualitySlider_->setValue(100);
    qualitySlider_->setTickPosition(QSlider::TicksBelow);
    qualitySlider_->setTickInterval(25);
    
    qualityLabel_ = new QLabel("100%");
    qualityLabel_->setMinimumWidth(50);
    
    connect(qualitySlider_, &QSlider::valueChanged, this, &EffectsConfigWidget::onQualityChanged);
    
    qualityLayout->addWidget(qualitySlider_);
    qualityLayout->addWidget(qualityLabel_);
    layout->addLayout(qualityLayout);
    
    auto* qualityNote = new QLabel("Lower quality improves performance on slower systems");
    qualityNote->setWordWrap(true);
    const auto& colors = ThemeManager::instance().colors();
    qualityNote->setStyleSheet(QString("color: %1;").arg(colors.textSecondary.name()));
    layout->addWidget(qualityNote);
    
    static_cast<QVBoxLayout*>(this->layout())->addWidget(group);
}

void EffectsConfigWidget::createShadowSettings(QWidget* parent) {
    auto* layout = static_cast<QVBoxLayout*>(parent->layout());
    
    auto* group = new QGroupBox("Shadow Configuration");
    auto* groupLayout = new QVBoxLayout(group);
    
    // Shadow style
    auto* styleLayout = new QHBoxLayout();
    styleLayout->addWidget(new QLabel("Shadow Style:"));
    
    shadowStyleCombo_ = new QComboBox();
    shadowStyleCombo_->addItems({
        "None", "Subtle", "Elevated", "Floating", "Inset", "Colored"
    });
    shadowStyleCombo_->setCurrentIndex(2); // Elevated
    connect(shadowStyleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EffectsConfigWidget::onShadowStyleChanged);
    styleLayout->addWidget(shadowStyleCombo_);
    styleLayout->addStretch();
    groupLayout->addLayout(styleLayout);
    
    // Custom shadow settings
    auto* customGroup = new QGroupBox("Custom Shadow");
    auto* customLayout = new QFormLayout(customGroup);
    
    shadowColorPicker_ = new ColorPickerWidget();
    shadowColorPicker_->setColor(ThemeManager::instance().colors().shadow);
    connect(shadowColorPicker_, &ColorPickerWidget::colorChanged,
            [this]() { updateShadowPreview(); emit settingChanged(); });
    customLayout->addRow("Color:", shadowColorPicker_);
    
    shadowBlurSlider_ = new QSlider(Qt::Horizontal);
    shadowBlurSlider_->setRange(0, 50);
    shadowBlurSlider_->setValue(10);
    connect(shadowBlurSlider_, &QSlider::valueChanged,
            [this]() { updateShadowPreview(); emit settingChanged(); });
    customLayout->addRow("Blur Radius:", shadowBlurSlider_);
    
    auto* offsetLayout = new QHBoxLayout();
    shadowOffsetXSpin_ = new QSpinBox();
    shadowOffsetXSpin_->setRange(-20, 20);
    shadowOffsetXSpin_->setValue(0);
    shadowOffsetXSpin_->setPrefix("X: ");
    
    shadowOffsetYSpin_ = new QSpinBox();
    shadowOffsetYSpin_->setRange(-20, 20);
    shadowOffsetYSpin_->setValue(2);
    shadowOffsetYSpin_->setPrefix("Y: ");
    
    connect(shadowOffsetXSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            [this]() { updateShadowPreview(); emit settingChanged(); });
    connect(shadowOffsetYSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            [this]() { updateShadowPreview(); emit settingChanged(); });
    
    offsetLayout->addWidget(shadowOffsetXSpin_);
    offsetLayout->addWidget(shadowOffsetYSpin_);
    offsetLayout->addStretch();
    customLayout->addRow("Offset:", offsetLayout);
    
    groupLayout->addWidget(customGroup);
    
    // Shadow preview
    shadowPreview_ = new ShadowPreviewWidget();
    shadowPreview_->setFixedHeight(100);
    groupLayout->addWidget(new QLabel("Preview:"));
    groupLayout->addWidget(shadowPreview_);
    
    layout->addWidget(group);
    layout->addStretch();
}

void EffectsConfigWidget::createGlowSettings(QWidget* parent) {
    auto* layout = static_cast<QVBoxLayout*>(parent->layout());
    
    auto* group = new QGroupBox("Glow Configuration");
    auto* groupLayout = new QVBoxLayout(group);
    
    // Glow style
    auto* styleLayout = new QHBoxLayout();
    styleLayout->addWidget(new QLabel("Glow Style:"));
    
    glowStyleCombo_ = new QComboBox();
    glowStyleCombo_->addItems({
        "Soft", "Neon", "Pulse", "Rainbow", "Halo"
    });
    connect(glowStyleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &EffectsConfigWidget::onGlowSettingChanged);
    styleLayout->addWidget(glowStyleCombo_);
    styleLayout->addStretch();
    groupLayout->addLayout(styleLayout);
    
    // Glow color
    auto* colorLayout = new QHBoxLayout();
    colorLayout->addWidget(new QLabel("Glow Color:"));
    
    glowColorPicker_ = new ColorPickerWidget();
    glowColorPicker_->setColor(ThemeManager::instance().colors().primary);
    connect(glowColorPicker_, &ColorPickerWidget::colorChanged,
            [this]() { updateGlowPreview(); emit settingChanged(); });
    colorLayout->addWidget(glowColorPicker_);
    colorLayout->addStretch();
    groupLayout->addLayout(colorLayout);
    
    // Glow intensity
    auto* intensityLayout = new QHBoxLayout();
    intensityLayout->addWidget(new QLabel("Intensity:"));
    
    glowIntensitySlider_ = new QSlider(Qt::Horizontal);
    glowIntensitySlider_->setRange(0, 100);
    glowIntensitySlider_->setValue(50);
    connect(glowIntensitySlider_, &QSlider::valueChanged,
            this, &EffectsConfigWidget::onGlowSettingChanged);
    intensityLayout->addWidget(glowIntensitySlider_);
    groupLayout->addLayout(intensityLayout);
    
    // Glow radius
    auto* radiusLayout = new QHBoxLayout();
    radiusLayout->addWidget(new QLabel("Radius:"));
    
    glowRadiusSlider_ = new QSlider(Qt::Horizontal);
    glowRadiusSlider_->setRange(5, 50);
    glowRadiusSlider_->setValue(20);
    connect(glowRadiusSlider_, &QSlider::valueChanged,
            this, &EffectsConfigWidget::onGlowSettingChanged);
    radiusLayout->addWidget(glowRadiusSlider_);
    groupLayout->addLayout(radiusLayout);
    
    // Glow preview
    glowPreview_ = new GlowPreviewWidget();
    glowPreview_->setFixedHeight(100);
    groupLayout->addWidget(new QLabel("Preview:"));
    groupLayout->addWidget(glowPreview_);
    
    layout->addWidget(group);
    layout->addStretch();
}

void EffectsConfigWidget::createBlurSettings(QWidget* parent) {
    auto* layout = static_cast<QVBoxLayout*>(parent->layout());
    
    auto* group = new QGroupBox("Blur Effects");
    auto* groupLayout = new QVBoxLayout(group);
    
    blurEnabledCheck_ = new QCheckBox("Enable Blur Effects");
    connect(blurEnabledCheck_, &QCheckBox::toggled,
            this, &EffectsConfigWidget::onBlurSettingChanged);
    groupLayout->addWidget(blurEnabledCheck_);
    
    auto* radiusLayout = new QHBoxLayout();
    radiusLayout->addWidget(new QLabel("Blur Radius:"));
    
    blurRadiusSlider_ = new QSlider(Qt::Horizontal);
    blurRadiusSlider_->setRange(0, 50);
    blurRadiusSlider_->setValue(10);
    
    blurRadiusLabel_ = new QLabel("10px");
    blurRadiusLabel_->setMinimumWidth(50);
    
    connect(blurRadiusSlider_, &QSlider::valueChanged, [this](int value) {
        blurRadiusLabel_->setText(QString("%1px").arg(value));
        emit settingChanged();
    });
    
    radiusLayout->addWidget(blurRadiusSlider_);
    radiusLayout->addWidget(blurRadiusLabel_);
    groupLayout->addLayout(radiusLayout);
    
    layout->addWidget(group);
}

void EffectsConfigWidget::createGlassMorphismSettings(QWidget* parent) {
    auto* layout = static_cast<QVBoxLayout*>(parent->layout());
    
    auto* group = new QGroupBox("Glass Morphism");
    auto* groupLayout = new QVBoxLayout(group);
    
    glassMorphismCheck_ = new QCheckBox("Enable Glass Morphism");
    connect(glassMorphismCheck_, &QCheckBox::toggled,
            this, &EffectsConfigWidget::onGlassMorphismChanged);
    groupLayout->addWidget(glassMorphismCheck_);
    
    // Blur amount
    auto* blurLayout = new QHBoxLayout();
    blurLayout->addWidget(new QLabel("Background Blur:"));
    
    glassBlurSlider_ = new QSlider(Qt::Horizontal);
    glassBlurSlider_->setRange(0, 50);
    glassBlurSlider_->setValue(20);
    
    glassBlurLabel_ = new QLabel("20px");
    glassBlurLabel_->setMinimumWidth(50);
    
    connect(glassBlurSlider_, &QSlider::valueChanged, [this](int value) {
        glassBlurLabel_->setText(QString("%1px").arg(value));
        emit settingChanged();
    });
    
    blurLayout->addWidget(glassBlurSlider_);
    blurLayout->addWidget(glassBlurLabel_);
    groupLayout->addLayout(blurLayout);
    
    // Opacity
    auto* opacityLayout = new QHBoxLayout();
    opacityLayout->addWidget(new QLabel("Glass Opacity:"));
    
    glassOpacitySlider_ = new QSlider(Qt::Horizontal);
    glassOpacitySlider_->setRange(0, 100);
    glassOpacitySlider_->setValue(80);
    
    glassOpacityLabel_ = new QLabel("80%");
    glassOpacityLabel_->setMinimumWidth(50);
    
    connect(glassOpacitySlider_, &QSlider::valueChanged, [this](int value) {
        glassOpacityLabel_->setText(QString("%1%").arg(value));
        emit settingChanged();
    });
    
    opacityLayout->addWidget(glassOpacitySlider_);
    opacityLayout->addWidget(glassOpacityLabel_);
    groupLayout->addLayout(opacityLayout);
    
    layout->addWidget(group);
}

void EffectsConfigWidget::createEffectPresets(QWidget* parent) {
    auto* layout = static_cast<QVBoxLayout*>(parent->layout());
    
    auto* splitter = new QSplitter(Qt::Horizontal);
    
    // Preset list
    presetsList_ = new QListWidget();
    connect(presetsList_, &QListWidget::currentRowChanged,
            this, &EffectsConfigWidget::onPresetSelected);
    splitter->addWidget(presetsList_);
    
    // Preset details
    auto* detailsWidget = new QWidget();
    auto* detailsLayout = new QVBoxLayout(detailsWidget);
    
    auto* descLabel = new QLabel("Select a preset to see details");
    descLabel->setWordWrap(true);
    detailsLayout->addWidget(descLabel);
    
    auto* applyButton = new QPushButton("Apply Selected Preset");
    connect(applyButton, &QPushButton::clicked, [this]() {
        int row = presetsList_->currentRow();
        if (row >= 0 && row < presets_.size()) {
            // Apply preset settings
            const auto& preset = presets_[row];
            EffectsManager::applyEffectSet(testWidget_, preset.effects);
            emit settingChanged();
        }
    });
    detailsLayout->addWidget(applyButton);
    
    detailsLayout->addStretch();
    splitter->addWidget(detailsWidget);
    
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);
    
    layout->addWidget(splitter);
}

void EffectsConfigWidget::createTestArea() {
    auto* group = new QGroupBox("Effect Testing");
    auto* layout = new QVBoxLayout(group);
    
    // Test widget
    testWidget_ = new QWidget();
    testWidget_->setFixedSize(300, 150);
    const auto& tColors = ThemeManager::instance().colors();
    testWidget_->setStyleSheet(QString("background-color: %1; border-radius: 8px;")
                              .arg(tColors.primary.name()));
    
    auto* testLabel = new QLabel("Test Widget", testWidget_);
    testLabel->setAlignment(Qt::AlignCenter);
    testLabel->setStyleSheet(QString("color: %1; font-size: 16px; font-weight: bold;")
                            .arg(ThemeManager::instance().colors().textInverse.name()));
    testLabel->setGeometry(testWidget_->rect());
    
    auto* testContainer = new QWidget();
    testContainer->setFixedHeight(200);
    testContainer->setStyleSheet(QString("background-color: %1;")
                                .arg(tColors.surface.name()));
    auto* containerLayout = new QHBoxLayout(testContainer);
    containerLayout->addStretch();
    containerLayout->addWidget(testWidget_);
    containerLayout->addStretch();
    
    layout->addWidget(testContainer);
    
    // Test buttons
    auto* buttonLayout = new QHBoxLayout();
    
    applyEffectsButton_ = new QPushButton("Apply Current Settings");
    connect(applyEffectsButton_, &QPushButton::clicked, 
            this, &EffectsConfigWidget::applyToTestWidget);
    buttonLayout->addWidget(applyEffectsButton_);
    
    clearEffectsButton_ = new QPushButton("Clear All Effects");
    connect(clearEffectsButton_, &QPushButton::clicked, [this]() {
        EffectsManager::removeAllEffects(testWidget_);
    });
    buttonLayout->addWidget(clearEffectsButton_);
    
    layout->addLayout(buttonLayout);
    
    static_cast<QVBoxLayout*>(this->layout())->addWidget(group);
}

void EffectsConfigWidget::loadSettings() {
    auto& em = EffectsManager::instance();
    
    enableCheck_->setChecked(em.effectsEnabled());
    qualitySlider_->setValue(em.effectQuality());
    
    onEnableToggled(em.effectsEnabled());
}

void EffectsConfigWidget::loadPresets() {
    presets_ = {
        {"Minimal", "Clean look with subtle shadows", 
         {EffectsManager::ShadowStyle::Subtle}},
        {"Material Design", "Google's Material Design shadows and effects",
         {EffectsManager::ShadowStyle::Elevated}},
        {"Neumorphism", "Soft UI with inset shadows",
         {EffectsManager::ShadowStyle::Inset}},
        {"Glassmorphism", "Frosted glass effect with blur",
         {EffectsManager::ShadowStyle::Subtle, EffectsManager::GlowStyle::Soft, 
          20.0, true}},
        {"Neon", "Bright neon glow effects",
         {EffectsManager::ShadowStyle::None, EffectsManager::GlowStyle::Neon}},
        {"Floating", "Elements that appear to float",
         {EffectsManager::ShadowStyle::Floating}},
        {"No Effects", "Disable all visual effects",
         {EffectsManager::ShadowStyle::None}}
    };
    
    for (const auto& preset : presets_) {
        presetsList_->addItem(preset.name);
    }
}

void EffectsConfigWidget::onEnableToggled(bool enabled) {
    EffectsManager::instance().setEffectsEnabled(enabled);
    
    // Enable/disable all controls
    qualitySlider_->setEnabled(enabled);
    shadowStyleCombo_->setEnabled(enabled);
    shadowColorPicker_->setEnabled(enabled);
    shadowBlurSlider_->setEnabled(enabled);
    shadowOffsetXSpin_->setEnabled(enabled);
    shadowOffsetYSpin_->setEnabled(enabled);
    glowStyleCombo_->setEnabled(enabled);
    glowColorPicker_->setEnabled(enabled);
    glowIntensitySlider_->setEnabled(enabled);
    glowRadiusSlider_->setEnabled(enabled);
    blurEnabledCheck_->setEnabled(enabled);
    blurRadiusSlider_->setEnabled(enabled);
    glassMorphismCheck_->setEnabled(enabled);
    glassBlurSlider_->setEnabled(enabled);
    glassOpacitySlider_->setEnabled(enabled);
    
    emit settingChanged();
}

void EffectsConfigWidget::onQualityChanged(int value) {
    qualityLabel_->setText(QString("%1%").arg(value));
    EffectsManager::instance().setEffectQuality(value);
    emit settingChanged();
}

void EffectsConfigWidget::onShadowStyleChanged() {
    auto style = static_cast<EffectsManager::ShadowStyle>(shadowStyleCombo_->currentIndex());
    shadowPreview_->setShadowStyle(style);
    
    // Enable/disable custom controls based on style
    bool enableCustom = (style != EffectsManager::ShadowStyle::None);
    shadowColorPicker_->setEnabled(enableCustom);
    shadowBlurSlider_->setEnabled(enableCustom);
    shadowOffsetXSpin_->setEnabled(enableCustom);
    shadowOffsetYSpin_->setEnabled(enableCustom);
    
    emit settingChanged();
}

void EffectsConfigWidget::updateShadowPreview() {
    shadowPreview_->setCustomShadow(
        shadowColorPicker_->color(),
        shadowBlurSlider_->value(),
        QPointF(shadowOffsetXSpin_->value(), shadowOffsetYSpin_->value())
    );
}

void EffectsConfigWidget::updateGlowPreview() {
    auto style = static_cast<EffectsManager::GlowStyle>(glowStyleCombo_->currentIndex());
    glowPreview_->setGlowStyle(style, glowColorPicker_->color());
}

void EffectsConfigWidget::onGlowSettingChanged() {
    updateGlowPreview();
    emit settingChanged();
}

void EffectsConfigWidget::onBlurSettingChanged() {
    blurRadiusSlider_->setEnabled(blurEnabledCheck_->isChecked());
    emit settingChanged();
}

void EffectsConfigWidget::onGlassMorphismChanged() {
    bool enabled = glassMorphismCheck_->isChecked();
    glassBlurSlider_->setEnabled(enabled);
    glassOpacitySlider_->setEnabled(enabled);
    emit settingChanged();
}

void EffectsConfigWidget::onPresetSelected() {
    int row = presetsList_->currentRow();
    if (row >= 0 && row < presets_.size()) {
        const auto& preset = presets_[row];
        
        // Update description
        auto* descLabel = qobject_cast<QLabel*>(
            static_cast<QSplitter*>(
                static_cast<QTabWidget*>(this->layout()->itemAt(1)->widget())->widget(3)->layout()->itemAt(0)->widget()
            )->widget(1)->layout()->itemAt(0)->widget()
        );
        
        if (descLabel) {
            descLabel->setText(QString("<b>%1</b><br><br>%2")
                              .arg(preset.name)
                              .arg(preset.description));
        }
    }
}

void EffectsConfigWidget::applyToTestWidget() {
    // Build effect set from current settings
    EffectsManager::EffectSet effects;
    
    effects.shadow = static_cast<EffectsManager::ShadowStyle>(shadowStyleCombo_->currentIndex());
    effects.glow = static_cast<EffectsManager::GlowStyle>(glowStyleCombo_->currentIndex());
    effects.blurRadius = blurEnabledCheck_->isChecked() ? blurRadiusSlider_->value() : 0;
    effects.glassMorphism = glassMorphismCheck_->isChecked();
    
    EffectsManager::applyEffectSet(testWidget_, effects);
}

// ShadowPreviewWidget implementation

EffectsConfigWidget::ShadowPreviewWidget::ShadowPreviewWidget(QWidget* parent)
    : QWidget(parent) {
    const auto& colors = ThemeManager::instance().colors();
    setStyleSheet(QString("background-color: %1;").arg(colors.surface.name()));
    
    innerWidget_ = new QWidget(this);
    innerWidget_->setStyleSheet(QString("background-color: %1; border-radius: 8px;")
                               .arg(colors.primary.name()));
    
    setShadowStyle(EffectsManager::ShadowStyle::Elevated);
}

void EffectsConfigWidget::ShadowPreviewWidget::setShadowStyle(EffectsManager::ShadowStyle style) {
    currentStyle_ = style;
    EffectsManager::applyShadow(innerWidget_, style);
}

void EffectsConfigWidget::ShadowPreviewWidget::setCustomShadow(const QColor& color, qreal blur, const QPointF& offset) {
    auto* shadow = EffectsManager::createShadow(currentStyle_, color, blur, offset);
    if (shadow) {
        innerWidget_->setGraphicsEffect(shadow);
    }
}

void EffectsConfigWidget::ShadowPreviewWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    
    // Center the inner widget
    int margin = 20;
    innerWidget_->setGeometry(margin, margin, 
                             width() - 2 * margin, 
                             height() - 2 * margin);
}

// GlowPreviewWidget implementation

EffectsConfigWidget::GlowPreviewWidget::GlowPreviewWidget(QWidget* parent)
    : QWidget(parent) {
    const auto& colors = ThemeManager::instance().colors();
    setStyleSheet(QString("background-color: %1;").arg(colors.surface.name()));
}

void EffectsConfigWidget::GlowPreviewWidget::setGlowStyle(EffectsManager::GlowStyle style, const QColor& color) {
    glowStyle_ = style;
    glowColor_ = color;
    update();
}

void EffectsConfigWidget::GlowPreviewWidget::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw background
    const auto& colors = ThemeManager::instance().colors();
    painter.fillRect(rect(), colors.surface);
    
    // Draw glowing rectangle
    QRectF glowRect = rect().adjusted(30, 30, -30, -30);
    
    // Apply glow effect
    qreal glowRadius = 20.0;
    qreal intensity = 1.0;
    
    switch (glowStyle_) {
        case EffectsManager::GlowStyle::Soft:
            glowRadius = 15.0;
            intensity = 0.5;
            break;
        case EffectsManager::GlowStyle::Neon:
            glowRadius = 30.0;
            intensity = 1.5;
            break;
        case EffectsManager::GlowStyle::Halo:
            glowRadius = 40.0;
            intensity = 0.8;
            break;
    }
    
    EffectsManager::paintGlow(&painter, glowRect, glowColor_, glowRadius, intensity);
    
    // Draw the actual rectangle
    painter.fillRect(glowRect, colors.primary);
}

} // namespace llm_re::ui_v2