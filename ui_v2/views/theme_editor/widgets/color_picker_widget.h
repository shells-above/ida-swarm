#pragma once

#include "../../../core/ui_v2_common.h"

namespace llm_re::ui_v2 {

// Color wheel widget for color selection
class ColorWheel : public QWidget {
    Q_OBJECT
    
public:
    explicit ColorWheel(QWidget* parent = nullptr);
    
    QColor color() const;
    void setColor(const QColor& color);
    
signals:
    void colorChanged(const QColor& color);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    
private:
    QImage wheelImage_;
    QPoint selectedPoint_;
    qreal hue_ = 0;
    qreal saturation_ = 1.0;
    qreal value_ = 1.0;
    
    void generateWheel();
    void updateFromPoint(const QPoint& point);
    QColor colorAt(const QPoint& point) const;
};

class ColorPickerWidget : public QWidget {
    Q_OBJECT

public:
    explicit ColorPickerWidget(QWidget* parent = nullptr);
    ~ColorPickerWidget() = default;

    // Get/set current color
    QColor color() const { return currentColor_; }
    void setColor(const QColor& color);

    // Enable/disable alpha channel
    void setAlphaEnabled(bool enabled) { alphaEnabled_ = enabled; }
    bool alphaEnabled() const { return alphaEnabled_; }

signals:
    void colorChanged(const QColor& color);
    void colorSelected(const QColor& color);  // Emitted on final selection

private slots:
    void onColorButtonClicked();
    void onHexEditingFinished();
    void onRgbValueChanged();
    void onHslValueChanged();
    void updateColorDisplay();
    void blockSignals(bool block);

private:
    void setupUI();
    void updateFromColor(const QColor& color);
    
    // UI elements
    QPushButton* colorButton_ = nullptr;
    QLineEdit* hexEdit_ = nullptr;
    
    // RGB controls
    QSpinBox* redSpin_ = nullptr;
    QSpinBox* greenSpin_ = nullptr;
    QSpinBox* blueSpin_ = nullptr;
    QSpinBox* alphaSpin_ = nullptr;
    
    // HSL controls
    QSpinBox* hueSpin_ = nullptr;
    QSpinBox* satSpin_ = nullptr;
    QSpinBox* lightSpin_ = nullptr;
    
    // Recent colors
    QList<QColor> recentColors_;
    static constexpr int MAX_RECENT_COLORS = 10;
    
    // State
    QColor currentColor_;
    bool alphaEnabled_ = true;
    bool updatingUI_ = false;
};

// Advanced color picker dialog
class ColorPickerDialog : public QDialog {
    Q_OBJECT

public:
    explicit ColorPickerDialog(const QColor& initial, QWidget* parent = nullptr);
    ~ColorPickerDialog() = default;

    QColor selectedColor() const { return selectedColor_; }

signals:
    void colorChanged(const QColor& color);

private slots:
    void onWheelColorChanged();
    void onSliderValueChanged();
    void onColorHarmonyChanged();
    void updatePreview();

private:
    void setupUI();
    void generateHarmonyColors();
    
    // UI elements
    ColorWheel* colorWheel_ = nullptr;
    QSlider* valueSlider_ = nullptr;
    QSlider* alphaSlider_ = nullptr;
    
    // Color display
    QWidget* previewWidget_ = nullptr;
    QLabel* oldColorLabel_ = nullptr;
    QLabel* newColorLabel_ = nullptr;
    
    // Harmony colors
    QComboBox* harmonyCombo_ = nullptr;
    QWidget* harmonyWidget_ = nullptr;
    QList<QLabel*> harmonyLabels_;
    
    // Hex/RGB/HSL inputs
    ColorPickerWidget* detailPicker_ = nullptr;
    
    // State
    QColor initialColor_;
    QColor selectedColor_;
    
    // Color harmony types
    enum HarmonyType {
        None,
        Complementary,
        Analogous,
        Triadic,
        Tetradic,
        SplitComplementary,
        Monochromatic
    };
    
    HarmonyType currentHarmony_ = None;
};

} // namespace llm_re::ui_v2