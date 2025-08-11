#include "../../../core/ui_v2_common.h"
#include "color_picker_widget.h"
#include "../../../core/theme_manager.h"

namespace llm_re::ui_v2 {

ColorPickerWidget::ColorPickerWidget(QWidget* parent)
    : QWidget(parent), currentColor_(ThemeManager::instance().colors().primary) {
    setupUI();
    updateFromColor(currentColor_);
}

void ColorPickerWidget::setupUI() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    
    // Color button
    colorButton_ = new QPushButton();
    colorButton_->setFixedSize(40, 30);
    colorButton_->setFlat(true);
    colorButton_->setStyleSheet(QString("border: 1px solid %1; border-radius: 4px;")
                               .arg(ThemeManager::instance().colors().border.name()));
    connect(colorButton_, &QPushButton::clicked, this, &ColorPickerWidget::onColorButtonClicked);
    layout->addWidget(colorButton_);
    
    // Hex input
    hexEdit_ = new QLineEdit();
    hexEdit_->setMaximumWidth(80);
    hexEdit_->setPlaceholderText("#000000");
    connect(hexEdit_, &QLineEdit::editingFinished, this, &ColorPickerWidget::onHexEditingFinished);
    layout->addWidget(hexEdit_);
    
    // Expandable section for RGB/HSL
    auto* detailButton = new QToolButton();
    detailButton->setArrowType(Qt::RightArrow);
    detailButton->setCheckable(true);
    layout->addWidget(detailButton);
    
    // Detail widget (hidden by default)
    auto* detailWidget = new QWidget();
    auto* detailLayout = new QHBoxLayout(detailWidget);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    
    // RGB inputs
    auto* rgbLayout = new QHBoxLayout();
    rgbLayout->setSpacing(2);
    
    redSpin_ = new QSpinBox();
    redSpin_->setRange(0, 255);
    redSpin_->setPrefix("R:");
    redSpin_->setMaximumWidth(60);
    connect(redSpin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &ColorPickerWidget::onRgbValueChanged);
    rgbLayout->addWidget(redSpin_);
    
    greenSpin_ = new QSpinBox();
    greenSpin_->setRange(0, 255);
    greenSpin_->setPrefix("G:");
    greenSpin_->setMaximumWidth(60);
    connect(greenSpin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &ColorPickerWidget::onRgbValueChanged);
    rgbLayout->addWidget(greenSpin_);
    
    blueSpin_ = new QSpinBox();
    blueSpin_->setRange(0, 255);
    blueSpin_->setPrefix("B:");
    blueSpin_->setMaximumWidth(60);
    connect(blueSpin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &ColorPickerWidget::onRgbValueChanged);
    rgbLayout->addWidget(blueSpin_);
    
    alphaSpin_ = new QSpinBox();
    alphaSpin_->setRange(0, 255);
    alphaSpin_->setPrefix("A:");
    alphaSpin_->setMaximumWidth(60);
    connect(alphaSpin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &ColorPickerWidget::onRgbValueChanged);
    rgbLayout->addWidget(alphaSpin_);
    
    detailLayout->addLayout(rgbLayout);
    detailLayout->addStretch();
    
    detailWidget->setVisible(false);
    layout->addWidget(detailWidget);
    
    // Connect detail button
    connect(detailButton, &QToolButton::toggled, [detailButton, detailWidget](bool checked) {
        detailButton->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        detailWidget->setVisible(checked);
    });
    
    layout->addStretch();
}

void ColorPickerWidget::setColor(const QColor& color) {
    if (currentColor_ == color) return;
    
    currentColor_ = color;
    updateFromColor(color);
    emit colorChanged(color);
}

void ColorPickerWidget::onColorButtonClicked() {
    ColorPickerDialog dialog(currentColor_, this);
    
    connect(&dialog, &ColorPickerDialog::colorChanged, [this](const QColor& color) {
        setColor(color);
    });
    
    if (dialog.exec() == QDialog::Accepted) {
        setColor(dialog.selectedColor());
        emit colorSelected(currentColor_);
        
        // Add to recent colors
        recentColors_.removeAll(currentColor_);
        recentColors_.prepend(currentColor_);
        if (recentColors_.size() > MAX_RECENT_COLORS) {
            recentColors_.removeLast();
        }
    }
}

void ColorPickerWidget::onHexEditingFinished() {
    QString hex = hexEdit_->text();
    if (!hex.startsWith('#')) {
        hex = '#' + hex;
    }
    
    QColor color(hex);
    if (color.isValid()) {
        setColor(color);
    } else {
        // Revert to current color
        hexEdit_->setText(currentColor_.name(QColor::HexArgb));
    }
}

void ColorPickerWidget::onRgbValueChanged() {
    if (updatingUI_) return;
    
    QColor color(redSpin_->value(), greenSpin_->value(), blueSpin_->value(), alphaSpin_->value());
    setColor(color);
}

void ColorPickerWidget::onHslValueChanged() {
    if (updatingUI_) return;
    
    QColor color = QColor::fromHsl(hueSpin_->value(), satSpin_->value(), lightSpin_->value());
    color.setAlpha(alphaSpin_->value());
    setColor(color);
}

void ColorPickerWidget::updateFromColor(const QColor& color) {
    updatingUI_ = true;
    
    // Update color button
    QString style = QString("background-color: %1; border: 1px solid %2; border-radius: 4px;")
                    .arg(color.name(QColor::HexArgb))
                    .arg(ThemeManager::instance().colors().border.name());
    colorButton_->setStyleSheet(style);
    
    // Update hex edit
    hexEdit_->setText(color.name(alphaEnabled_ ? QColor::HexArgb : QColor::HexRgb));
    
    // Update RGB spins
    redSpin_->setValue(color.red());
    greenSpin_->setValue(color.green());
    blueSpin_->setValue(color.blue());
    alphaSpin_->setValue(color.alpha());
    alphaSpin_->setVisible(alphaEnabled_);
    
    updatingUI_ = false;
}

void ColorPickerWidget::blockSignals(bool block) {
    hexEdit_->blockSignals(block);
    redSpin_->blockSignals(block);
    greenSpin_->blockSignals(block);
    blueSpin_->blockSignals(block);
    alphaSpin_->blockSignals(block);
}

void ColorPickerWidget::updateColorDisplay() {
    // Update the color display when values change
    updateFromColor(currentColor_);
}

// ColorPickerDialog implementation

ColorPickerDialog::ColorPickerDialog(const QColor& initial, QWidget* parent)
    : QDialog(parent), initialColor_(initial), selectedColor_(initial) {
    setWindowTitle("Color Picker");
    setModal(true);
    resize(600, 400);
    
    setupUI();
    colorWheel_->setColor(initial);
    updatePreview();
}

void ColorPickerDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);
    
    auto* mainLayout = new QHBoxLayout();
    
    // Left side: Color wheel and sliders
    auto* leftWidget = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftWidget);
    
    colorWheel_ = new ColorWheel();
    connect(colorWheel_, &ColorWheel::colorChanged, this, &ColorPickerDialog::onWheelColorChanged);
    leftLayout->addWidget(colorWheel_);
    
    // Value slider
    auto* valueLayout = new QHBoxLayout();
    valueLayout->addWidget(new QLabel("Value:"));
    valueSlider_ = new QSlider(Qt::Horizontal);
    valueSlider_->setRange(0, 255);
    valueSlider_->setValue(255);
    connect(valueSlider_, &QSlider::valueChanged, this, &ColorPickerDialog::onSliderValueChanged);
    valueLayout->addWidget(valueSlider_);
    leftLayout->addLayout(valueLayout);
    
    // Alpha slider
    auto* alphaLayout = new QHBoxLayout();
    alphaLayout->addWidget(new QLabel("Alpha:"));
    alphaSlider_ = new QSlider(Qt::Horizontal);
    alphaSlider_->setRange(0, 255);
    alphaSlider_->setValue(255);
    connect(alphaSlider_, &QSlider::valueChanged, this, &ColorPickerDialog::onSliderValueChanged);
    alphaLayout->addWidget(alphaSlider_);
    leftLayout->addLayout(alphaLayout);
    
    mainLayout->addWidget(leftWidget);
    
    // Right side: Preview and details
    auto* rightWidget = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightWidget);
    
    // Color preview
    auto* previewGroup = new QGroupBox("Preview");
    auto* previewLayout = new QHBoxLayout(previewGroup);
    
    oldColorLabel_ = new QLabel();
    oldColorLabel_->setFixedSize(60, 60);
    oldColorLabel_->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    oldColorLabel_->setStyleSheet(QString("background-color: %1;").arg(initialColor_.name()));
    previewLayout->addWidget(oldColorLabel_);
    
    previewLayout->addWidget(new QLabel("→"));
    
    newColorLabel_ = new QLabel();
    newColorLabel_->setFixedSize(60, 60);
    newColorLabel_->setFrameStyle(QFrame::Panel | QFrame::Sunken);
    previewLayout->addWidget(newColorLabel_);
    
    rightLayout->addWidget(previewGroup);
    
    // Color harmony
    auto* harmonyGroup = new QGroupBox("Color Harmony");
    auto* harmonyLayout = new QVBoxLayout(harmonyGroup);
    
    harmonyCombo_ = new QComboBox();
    harmonyCombo_->addItems({"None", "Complementary", "Analogous", "Triadic", 
                            "Tetradic", "Split Complementary", "Monochromatic"});
    connect(harmonyCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ColorPickerDialog::onColorHarmonyChanged);
    harmonyLayout->addWidget(harmonyCombo_);
    
    harmonyWidget_ = new QWidget();
    auto* harmonyColorsLayout = new QHBoxLayout(harmonyWidget_);
    // Harmony colors will be added dynamically
    harmonyLayout->addWidget(harmonyWidget_);
    
    rightLayout->addWidget(harmonyGroup);
    
    // Detailed picker
    detailPicker_ = new ColorPickerWidget();
    connect(detailPicker_, &ColorPickerWidget::colorChanged, [this](const QColor& color) {
        selectedColor_ = color;
        colorWheel_->setColor(color);
        updatePreview();
        emit colorChanged(color);
    });
    rightLayout->addWidget(detailPicker_);
    
    rightLayout->addStretch();
    
    mainLayout->addWidget(rightWidget);
    layout->addLayout(mainLayout);
    
    // Dialog buttons
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);
}

void ColorPickerDialog::onWheelColorChanged() {
    selectedColor_ = colorWheel_->color();
    selectedColor_.setAlpha(alphaSlider_->value());
    
    detailPicker_->QWidget::blockSignals(true);
    detailPicker_->setColor(selectedColor_);
    detailPicker_->QWidget::blockSignals(false);
    
    updatePreview();
    generateHarmonyColors();
    emit colorChanged(selectedColor_);
}

void ColorPickerDialog::onSliderValueChanged() {
    QColor color = colorWheel_->color();
    
    // Update value from slider
    int h, s, v;
    color.getHsv(&h, &s, &v);
    color.setHsv(h, s, valueSlider_->value());
    color.setAlpha(alphaSlider_->value());
    
    selectedColor_ = color;
    
    detailPicker_->QWidget::blockSignals(true);
    detailPicker_->setColor(selectedColor_);
    detailPicker_->QWidget::blockSignals(false);
    
    updatePreview();
    emit colorChanged(selectedColor_);
}

void ColorPickerDialog::onColorHarmonyChanged() {
    currentHarmony_ = static_cast<HarmonyType>(harmonyCombo_->currentIndex());
    generateHarmonyColors();
}

void ColorPickerDialog::updatePreview() {
    newColorLabel_->setStyleSheet(QString("background-color: %1;").arg(selectedColor_.name(QColor::HexArgb)));
}

void ColorPickerDialog::generateHarmonyColors() {
    // Clear existing harmony labels
    for (auto* label : harmonyLabels_) {
        delete label;
    }
    harmonyLabels_.clear();
    
    if (currentHarmony_ == None) {
        harmonyWidget_->hide();
        return;
    }
    
    harmonyWidget_->show();
    
    QList<QColor> harmonyColors;
    int h, s, v;
    selectedColor_.getHsv(&h, &s, &v);
    
    switch (currentHarmony_) {
        case None:
            // No harmony colors to generate
            break;
            
        case Complementary:
            harmonyColors << QColor::fromHsv((h + 180) % 360, s, v);
            break;
            
        case Analogous:
            harmonyColors << QColor::fromHsv((h + 30) % 360, s, v);
            harmonyColors << QColor::fromHsv((h + 330) % 360, s, v);
            break;
            
        case Triadic:
            harmonyColors << QColor::fromHsv((h + 120) % 360, s, v);
            harmonyColors << QColor::fromHsv((h + 240) % 360, s, v);
            break;
            
        case Tetradic:
            harmonyColors << QColor::fromHsv((h + 90) % 360, s, v);
            harmonyColors << QColor::fromHsv((h + 180) % 360, s, v);
            harmonyColors << QColor::fromHsv((h + 270) % 360, s, v);
            break;
            
        case SplitComplementary:
            harmonyColors << QColor::fromHsv((h + 150) % 360, s, v);
            harmonyColors << QColor::fromHsv((h + 210) % 360, s, v);
            break;
            
        case Monochromatic:
            harmonyColors << QColor::fromHsv(h, s * 0.3, v);
            harmonyColors << QColor::fromHsv(h, s * 0.6, v);
            harmonyColors << QColor::fromHsv(h, s, v * 0.7);
            harmonyColors << QColor::fromHsv(h, s, v * 0.4);
            break;
    }
    
    // Create labels for harmony colors
    auto* layout = qobject_cast<QHBoxLayout*>(harmonyWidget_->layout());
    
    for (const QColor& color : harmonyColors) {
        auto* label = new QLabel();
        label->setFixedSize(30, 30);
        label->setFrameStyle(QFrame::Panel | QFrame::Sunken);
        label->setStyleSheet(QString("background-color: %1;").arg(color.name()));
        label->setCursor(Qt::PointingHandCursor);
        
        // Make clickable
        label->installEventFilter(this);
        label->setProperty("harmonyColor", color);
        
        layout->addWidget(label);
        harmonyLabels_ << label;
    }
    
    layout->addStretch();
}

// ColorWheel implementation

ColorWheel::ColorWheel(QWidget* parent)
    : QWidget(parent) {
    setFixedSize(200, 200);
    generateWheel();
}

void ColorWheel::generateWheel() {
    wheelImage_ = QImage(size(), QImage::Format_ARGB32);
    wheelImage_.fill(Qt::transparent);
    
    QPainter painter(&wheelImage_);
    painter.setRenderHint(QPainter::Antialiasing);
    
    QPoint center = rect().center();
    int radius = width() / 2 - 5;
    
    // Draw HSV color wheel
    for (int y = 0; y < height(); ++y) {
        for (int x = 0; x < width(); ++x) {
            QPoint point(x, y);
            int dx = point.x() - center.x();
            int dy = point.y() - center.y();
            double distance = std::sqrt(dx * dx + dy * dy);
            
            if (distance <= radius) {
                double angle = std::atan2(dy, dx);
                double hue = (angle + M_PI) / (2 * M_PI) * 360;
                double saturation = distance / radius;
                
                QColor color = QColor::fromHsvF(hue / 360.0, saturation, 1.0);
                painter.setPen(color);
                painter.drawPoint(point);
            }
        }
    }
}

void ColorWheel::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.drawImage(0, 0, wheelImage_);
    
    // Draw selection indicator
    if (!selectedPoint_.isNull()) {
        auto& tm = ThemeManager::instance();
        painter.setPen(QPen(tm.colors().textPrimary, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(selectedPoint_, 5, 5);
        
        painter.setPen(QPen(tm.colors().background, 1));
        painter.drawEllipse(selectedPoint_, 5, 5);
    }
}

void ColorWheel::mousePressEvent(QMouseEvent* event) {
    updateFromPoint(event->pos());
}

void ColorWheel::mouseMoveEvent(QMouseEvent* event) {
    if (event->buttons() & Qt::LeftButton) {
        updateFromPoint(event->pos());
    }
}

void ColorWheel::updateFromPoint(const QPoint& point) {
    QPoint center = rect().center();
    int dx = point.x() - center.x();
    int dy = point.y() - center.y();
    double distance = std::sqrt(dx * dx + dy * dy);
    int radius = width() / 2 - 5;
    
    if (distance <= radius) {
        selectedPoint_ = point;
        
        double angle = std::atan2(dy, dx);
        hue_ = (angle + M_PI) / (2 * M_PI);
        saturation_ = distance / radius;
        
        update();
        emit colorChanged(color());
    }
}

QColor ColorWheel::color() const {
    return QColor::fromHsvF(hue_, saturation_, value_);
}

void ColorWheel::setColor(const QColor& color) {
    int h, s, v;
    color.getHsv(&h, &s, &v);
    
    hue_ = h / 360.0;
    saturation_ = s / 255.0;
    value_ = v / 255.0;
    
    // Calculate point from HSV
    QPoint center = rect().center();
    int radius = width() / 2 - 5;
    
    double angle = hue_ * 2 * M_PI - M_PI;
    double distance = saturation_ * radius;
    
    int x = center.x() + distance * std::cos(angle);
    int y = center.y() + distance * std::sin(angle);
    
    selectedPoint_ = QPoint(x, y);
    update();
}

} // namespace llm_re::ui_v2