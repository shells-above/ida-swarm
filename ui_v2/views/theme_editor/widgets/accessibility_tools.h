#pragma once

#include "../../../core/ui_v2_common.h"

namespace llm_re::ui_v2 {

// WCAG contrast ratio requirements
namespace WCAG {
    constexpr double AA_NORMAL_TEXT = 4.5;
    constexpr double AA_LARGE_TEXT = 3.0;
    constexpr double AAA_NORMAL_TEXT = 7.0;
    constexpr double AAA_LARGE_TEXT = 4.5;
}

// Color blindness types
enum class ColorBlindnessType {
    None,
    Protanopia,      // Red-blind
    Deuteranopia,    // Green-blind
    Tritanopia,      // Blue-blind
    Protanomaly,     // Red-weak
    Deuteranomaly,   // Green-weak
    Tritanomaly,     // Blue-weak
    Achromatopsia,   // Total color blindness
    Achromatomaly    // Partial color blindness
};

class ContrastChecker : public QWidget {
    Q_OBJECT

public:
    explicit ContrastChecker(QWidget* parent = nullptr);
    ~ContrastChecker() = default;

    void setColors(const QColor& foreground, const QColor& background);
    double getContrastRatio() const { return contrastRatio_; }
    double calculateContrastRatio(const QColor& fg, const QColor& bg);
    
signals:
    void colorsChanged();

private:
    void setupUI();
    void updateContrast();
    double calculateLuminance(const QColor& color);
    
    // UI elements
    QWidget* foregroundSwatch_ = nullptr;
    QWidget* backgroundSwatch_ = nullptr;
    QLabel* ratioLabel_ = nullptr;
    QLabel* wcagAANormal_ = nullptr;
    QLabel* wcagAALarge_ = nullptr;
    QLabel* wcagAAANormal_ = nullptr;
    QLabel* wcagAAALarge_ = nullptr;
    QTextEdit* exampleText_ = nullptr;
    
    // Current colors
    QColor foreground_;
    QColor background_;
    double contrastRatio_ = 1.0;
};

class ColorBlindnessSimulator : public QWidget {
    Q_OBJECT

public:
    explicit ColorBlindnessSimulator(QWidget* parent = nullptr);
    ~ColorBlindnessSimulator() = default;

    void setOriginalColors(const std::map<QString, QColor>& colors);
    std::map<QString, QColor> getSimulatedColors() const { return simulatedColors_; }

signals:
    void simulationChanged();

private slots:
    void onTypeChanged(int index);

private:
    void setupUI();
    QColor simulateColorBlindness(const QColor& color, ColorBlindnessType type);
    
    // Daltonization matrices for different types
    static const double PROTANOPIA_MATRIX[9];
    static const double DEUTERANOPIA_MATRIX[9];
    static const double TRITANOPIA_MATRIX[9];
    
    QColor applyColorMatrix(const QColor& color, const double matrix[9]);
    
    // UI elements
    QComboBox* typeCombo_ = nullptr;
    QWidget* originalPreview_ = nullptr;
    QWidget* simulatedPreview_ = nullptr;
    QGridLayout* colorGrid_ = nullptr;
    
    // Color swatches
    struct ColorSwatch {
        QString name;
        QWidget* originalWidget;
        QWidget* simulatedWidget;
        QLabel* nameLabel;
    };
    std::vector<ColorSwatch> swatches_;
    
    // Current state
    ColorBlindnessType currentType_ = ColorBlindnessType::None;
    std::map<QString, QColor> originalColors_;
    std::map<QString, QColor> simulatedColors_;
};

class AccessibilityPanel : public QWidget {
    Q_OBJECT

public:
    explicit AccessibilityPanel(QWidget* parent = nullptr);
    ~AccessibilityPanel() = default;

    void updateColors(const std::map<QString, QColor>& colors);

signals:
    void accessibilityIssueFound(const QString& issue);
    void suggestionMade(const QString& colorName, const QColor& suggestedColor);

private:
    void setupUI();
    void checkAllContrasts();
    void generateReport();
    QColor suggestAccessibleColor(const QColor& foreground, const QColor& background, 
                                 double targetRatio);
    
    // UI elements
    QTabWidget* tabWidget_ = nullptr;
    ContrastChecker* contrastChecker_ = nullptr;
    ColorBlindnessSimulator* colorBlindSim_ = nullptr;
    QTextEdit* reportText_ = nullptr;
    QPushButton* generateReportBtn_ = nullptr;
    QPushButton* autoFixBtn_ = nullptr;
    
    // Current colors
    std::map<QString, QColor> currentColors_;
    
    // Contrast pairs to check
    struct ContrastPair {
        QString foregroundName;
        QString backgroundName;
        QString usage;
        double requiredRatio;
    };
    std::vector<ContrastPair> contrastPairs_;
};

} // namespace llm_re::ui_v2