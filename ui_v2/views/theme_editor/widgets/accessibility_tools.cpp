#include "../../../core/ui_v2_common.h"
#include "accessibility_tools.h"
#include "../../../core/theme_manager.h"

namespace llm_re::ui_v2 {

// Color blindness transformation matrices
const double ColorBlindnessSimulator::PROTANOPIA_MATRIX[9] = {
    0.567, 0.433, 0.000,
    0.558, 0.442, 0.000,
    0.000, 0.242, 0.758
};

const double ColorBlindnessSimulator::DEUTERANOPIA_MATRIX[9] = {
    0.625, 0.375, 0.000,
    0.700, 0.300, 0.000,
    0.000, 0.300, 0.700
};

const double ColorBlindnessSimulator::TRITANOPIA_MATRIX[9] = {
    0.950, 0.050, 0.000,
    0.000, 0.433, 0.567,
    0.000, 0.475, 0.525
};

// ContrastChecker implementation

ContrastChecker::ContrastChecker(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void ContrastChecker::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    
    // Color selection area
    auto* colorGroup = new QGroupBox("Colors");
    auto* colorLayout = new QHBoxLayout(colorGroup);
    
    // Foreground color
    auto* fgLayout = new QVBoxLayout();
    fgLayout->addWidget(new QLabel("Foreground"));
    foregroundSwatch_ = new QWidget();
    foregroundSwatch_->setFixedSize(80, 80);
    // Use explicit colors from theme instead of palette to avoid inheriting IDA's theme
    const auto& colors = ThemeManager::instance().colors();
    foregroundSwatch_->setStyleSheet(QString("background-color: black; border: 1px solid %1;")
                                     .arg(colors.border.name()));
    fgLayout->addWidget(foregroundSwatch_);
    colorLayout->addLayout(fgLayout);
    
    colorLayout->addSpacing(20);
    
    // Background color
    auto* bgLayout = new QVBoxLayout();
    bgLayout->addWidget(new QLabel("Background"));
    backgroundSwatch_ = new QWidget();
    backgroundSwatch_->setFixedSize(80, 80);
    backgroundSwatch_->setStyleSheet(QString("background-color: white; border: 1px solid %1;")
                                     .arg(colors.border.name()));
    bgLayout->addWidget(backgroundSwatch_);
    colorLayout->addLayout(bgLayout);
    
    colorLayout->addStretch();
    mainLayout->addWidget(colorGroup);
    
    // Contrast ratio display
    auto* ratioGroup = new QGroupBox("Contrast Ratio");
    auto* ratioLayout = new QVBoxLayout(ratioGroup);
    
    ratioLabel_ = new QLabel("1:1");
    ratioLabel_->setStyleSheet("font-size: 24px; font-weight: bold;");
    ratioLabel_->setAlignment(Qt::AlignCenter);
    ratioLayout->addWidget(ratioLabel_);
    
    mainLayout->addWidget(ratioGroup);
    
    // WCAG compliance indicators
    auto* wcagGroup = new QGroupBox("WCAG Compliance");
    auto* wcagLayout = new QGridLayout(wcagGroup);
    
    wcagLayout->addWidget(new QLabel("AA Normal Text (4.5:1)"), 0, 0);
    wcagAANormal_ = new QLabel("❌ Fail");
    wcagLayout->addWidget(wcagAANormal_, 0, 1);
    
    wcagLayout->addWidget(new QLabel("AA Large Text (3:1)"), 1, 0);
    wcagAALarge_ = new QLabel("❌ Fail");
    wcagLayout->addWidget(wcagAALarge_, 1, 1);
    
    wcagLayout->addWidget(new QLabel("AAA Normal Text (7:1)"), 2, 0);
    wcagAAANormal_ = new QLabel("❌ Fail");
    wcagLayout->addWidget(wcagAAANormal_, 2, 1);
    
    wcagLayout->addWidget(new QLabel("AAA Large Text (4.5:1)"), 3, 0);
    wcagAAALarge_ = new QLabel("❌ Fail");
    wcagLayout->addWidget(wcagAAALarge_, 3, 1);
    
    mainLayout->addWidget(wcagGroup);
    
    // Example text
    auto* exampleGroup = new QGroupBox("Preview");
    auto* exampleLayout = new QVBoxLayout(exampleGroup);
    
    exampleText_ = new QTextEdit();
    exampleText_->setPlainText("The quick brown fox jumps over the lazy dog.\n\n"
                              "Normal text: 14px and below\n"
                              "Large text: 18px+ or 14px+ bold");
    exampleText_->setReadOnly(true);
    exampleText_->setMaximumHeight(100);
    exampleLayout->addWidget(exampleText_);
    
    mainLayout->addWidget(exampleGroup);
    mainLayout->addStretch();
    
    // Set initial colors from current theme
    auto& tm = ThemeManager::instance();
    setColors(tm.colors().textPrimary, tm.colors().background);
}

void ContrastChecker::setColors(const QColor& foreground, const QColor& background) {
    foreground_ = foreground;
    background_ = background;
    
    // Update swatches
    const auto& themColors = ThemeManager::instance().colors();
    foregroundSwatch_->setStyleSheet(QString("background-color: %1; border: 1px solid %2;")
                                    .arg(foreground.name())
                                    .arg(themColors.border.name()));
    backgroundSwatch_->setStyleSheet(QString("background-color: %1; border: 1px solid %2;")
                                    .arg(background.name())
                                    .arg(themColors.border.name()));
    
    // Update example text
    exampleText_->setStyleSheet(QString("color: %1; background-color: %2;")
                               .arg(foreground.name())
                               .arg(background.name()));
    
    updateContrast();
    emit colorsChanged();
}

void ContrastChecker::updateContrast() {
    contrastRatio_ = calculateContrastRatio(foreground_, background_);
    
    // Update ratio label
    ratioLabel_->setText(QString("%1:1").arg(contrastRatio_, 0, 'f', 2));
    
    // Update WCAG compliance
    auto updateCompliance = [](QLabel* label, bool passes) {
        label->setText(passes ? "✅ Pass" : "❌ Fail");
        label->setStyleSheet(passes ? "color: green;" : "color: red;");
    };
    
    updateCompliance(wcagAANormal_, contrastRatio_ >= WCAG::AA_NORMAL_TEXT);
    updateCompliance(wcagAALarge_, contrastRatio_ >= WCAG::AA_LARGE_TEXT);
    updateCompliance(wcagAAANormal_, contrastRatio_ >= WCAG::AAA_NORMAL_TEXT);
    updateCompliance(wcagAAALarge_, contrastRatio_ >= WCAG::AAA_LARGE_TEXT);
}

double ContrastChecker::calculateLuminance(const QColor& color) {
    // Convert to linear RGB
    auto toLinear = [](double channel) {
        channel /= 255.0;
        return channel <= 0.03928 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
    };
    
    double r = toLinear(color.red());
    double g = toLinear(color.green());
    double b = toLinear(color.blue());
    
    // Calculate relative luminance
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double ContrastChecker::calculateContrastRatio(const QColor& fg, const QColor& bg) {
    double l1 = calculateLuminance(fg);
    double l2 = calculateLuminance(bg);
    
    // Ensure l1 is the lighter color
    if (l1 < l2) std::swap(l1, l2);
    
    return (l1 + 0.05) / (l2 + 0.05);
}

// ColorBlindnessSimulator implementation

ColorBlindnessSimulator::ColorBlindnessSimulator(QWidget* parent)
    : QWidget(parent) {
    setupUI();
}

void ColorBlindnessSimulator::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    
    // Type selector
    auto* typeLayout = new QHBoxLayout();
    typeLayout->addWidget(new QLabel("Color Blindness Type:"));
    
    typeCombo_ = new QComboBox();
    typeCombo_->addItems({
        "None (Normal Vision)",
        "Protanopia (Red-Blind)",
        "Deuteranopia (Green-Blind)",
        "Tritanopia (Blue-Blind)",
        "Protanomaly (Red-Weak)",
        "Deuteranomaly (Green-Weak)",
        "Tritanomaly (Blue-Weak)",
        "Achromatopsia (Total Color Blindness)",
        "Achromatomaly (Partial Color Blindness)"
    });
    connect(typeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ColorBlindnessSimulator::onTypeChanged);
    typeLayout->addWidget(typeCombo_);
    typeLayout->addStretch();
    
    mainLayout->addLayout(typeLayout);
    
    // Color comparison grid
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    
    auto* gridWidget = new QWidget();
    colorGrid_ = new QGridLayout(gridWidget);
    colorGrid_->setSpacing(10);
    
    // Headers
    auto* originalHeader = new QLabel("Original");
    originalHeader->setStyleSheet("font-weight: bold;");
    colorGrid_->addWidget(originalHeader, 0, 1, Qt::AlignCenter);
    
    auto* simulatedHeader = new QLabel("Simulated");
    simulatedHeader->setStyleSheet("font-weight: bold;");
    colorGrid_->addWidget(simulatedHeader, 0, 2, Qt::AlignCenter);
    
    scrollArea->setWidget(gridWidget);
    mainLayout->addWidget(scrollArea);
}

void ColorBlindnessSimulator::setOriginalColors(const std::map<QString, QColor>& colors) {
    originalColors_ = colors;
    
    // Clear existing swatches
    for (auto& swatch : swatches_) {
        delete swatch.nameLabel;
        delete swatch.originalWidget;
        delete swatch.simulatedWidget;
    }
    swatches_.clear();
    
    // Create new swatches
    int row = 1;
    for (const auto& [name, color] : colors) {
        ColorSwatch swatch;
        swatch.name = name;
        
        // Name label
        swatch.nameLabel = new QLabel(name);
        colorGrid_->addWidget(swatch.nameLabel, row, 0);
        
        // Original color
        swatch.originalWidget = new QWidget();
        swatch.originalWidget->setFixedSize(60, 30);
        swatch.originalWidget->setStyleSheet(QString("background-color: %1; border: 1px solid black;")
                                           .arg(color.name()));
        colorGrid_->addWidget(swatch.originalWidget, row, 1);
        
        // Simulated color
        swatch.simulatedWidget = new QWidget();
        swatch.simulatedWidget->setFixedSize(60, 30);
        colorGrid_->addWidget(swatch.simulatedWidget, row, 2);
        
        swatches_.push_back(swatch);
        row++;
    }
    
    onTypeChanged(typeCombo_->currentIndex());
}

void ColorBlindnessSimulator::onTypeChanged(int index) {
    currentType_ = static_cast<ColorBlindnessType>(index);
    simulatedColors_.clear();
    
    // Simulate colors
    for (const auto& [name, color] : originalColors_) {
        simulatedColors_[name] = simulateColorBlindness(color, currentType_);
    }
    
    // Update swatches
    for (auto& swatch : swatches_) {
        if (simulatedColors_.count(swatch.name)) {
            QColor simColor = simulatedColors_[swatch.name];
            swatch.simulatedWidget->setStyleSheet(
                QString("background-color: %1; border: 1px solid black;").arg(simColor.name()));
        }
    }
    
    emit simulationChanged();
}

QColor ColorBlindnessSimulator::simulateColorBlindness(const QColor& color, ColorBlindnessType type) {
    switch (type) {
        case ColorBlindnessType::None:
            return color;
            
        case ColorBlindnessType::Protanopia:
            return applyColorMatrix(color, PROTANOPIA_MATRIX);
            
        case ColorBlindnessType::Deuteranopia:
            return applyColorMatrix(color, DEUTERANOPIA_MATRIX);
            
        case ColorBlindnessType::Tritanopia:
            return applyColorMatrix(color, TRITANOPIA_MATRIX);
            
        case ColorBlindnessType::Protanomaly: {
            // Partial red-blindness (70% severity)
            QColor normal = color;
            QColor protanopia = applyColorMatrix(color, PROTANOPIA_MATRIX);
            return QColor(
                normal.red() * 0.3 + protanopia.red() * 0.7,
                normal.green() * 0.3 + protanopia.green() * 0.7,
                normal.blue() * 0.3 + protanopia.blue() * 0.7
            );
        }
            
        case ColorBlindnessType::Deuteranomaly: {
            // Partial green-blindness (70% severity)
            QColor normal = color;
            QColor deuteranopia = applyColorMatrix(color, DEUTERANOPIA_MATRIX);
            return QColor(
                normal.red() * 0.3 + deuteranopia.red() * 0.7,
                normal.green() * 0.3 + deuteranopia.green() * 0.7,
                normal.blue() * 0.3 + deuteranopia.blue() * 0.7
            );
        }
            
        case ColorBlindnessType::Tritanomaly: {
            // Partial blue-blindness (70% severity)
            QColor normal = color;
            QColor tritanopia = applyColorMatrix(color, TRITANOPIA_MATRIX);
            return QColor(
                normal.red() * 0.3 + tritanopia.red() * 0.7,
                normal.green() * 0.3 + tritanopia.green() * 0.7,
                normal.blue() * 0.3 + tritanopia.blue() * 0.7
            );
        }
            
        case ColorBlindnessType::Achromatopsia: {
            // Total color blindness - convert to grayscale
            int gray = qGray(color.rgb());
            return QColor(gray, gray, gray);
        }
            
        case ColorBlindnessType::Achromatomaly: {
            // Partial color blindness - reduce saturation
            int gray = qGray(color.rgb());
            return QColor(
                color.red() * 0.3 + gray * 0.7,
                color.green() * 0.3 + gray * 0.7,
                color.blue() * 0.3 + gray * 0.7
            );
        }
    }
    
    return color;
}

QColor ColorBlindnessSimulator::applyColorMatrix(const QColor& color, const double matrix[9]) {
    double r = color.red() / 255.0;
    double g = color.green() / 255.0;
    double b = color.blue() / 255.0;
    
    double newR = matrix[0] * r + matrix[1] * g + matrix[2] * b;
    double newG = matrix[3] * r + matrix[4] * g + matrix[5] * b;
    double newB = matrix[6] * r + matrix[7] * g + matrix[8] * b;
    
    return QColor(
        qBound(0, int(newR * 255), 255),
        qBound(0, int(newG * 255), 255),
        qBound(0, int(newB * 255), 255)
    );
}

// AccessibilityPanel implementation

AccessibilityPanel::AccessibilityPanel(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    
    // Define standard contrast pairs to check
    contrastPairs_ = {
        {"textPrimary", "background", "Primary text", WCAG::AA_NORMAL_TEXT},
        {"textSecondary", "background", "Secondary text", WCAG::AA_NORMAL_TEXT},
        {"textPrimary", "surface", "Text on surface", WCAG::AA_NORMAL_TEXT},
        {"primary", "background", "Primary button", WCAG::AA_LARGE_TEXT},
        {"error", "background", "Error messages", WCAG::AA_NORMAL_TEXT},
        {"warning", "background", "Warning messages", WCAG::AA_NORMAL_TEXT},
        {"success", "background", "Success messages", WCAG::AA_NORMAL_TEXT},
        {"textLink", "background", "Links", WCAG::AA_NORMAL_TEXT},
        {"textInverse", "primary", "Inverse text on primary", WCAG::AA_NORMAL_TEXT}
    };
}

void AccessibilityPanel::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    
    tabWidget_ = new QTabWidget();
    
    // Contrast checker tab
    contrastChecker_ = new ContrastChecker();
    tabWidget_->addTab(contrastChecker_, "Contrast Checker");
    
    // Color blindness simulator tab
    colorBlindSim_ = new ColorBlindnessSimulator();
    tabWidget_->addTab(colorBlindSim_, "Color Blindness");
    
    // Accessibility report tab
    auto* reportWidget = new QWidget();
    auto* reportLayout = new QVBoxLayout(reportWidget);
    
    auto* reportButtonLayout = new QHBoxLayout();
    generateReportBtn_ = new QPushButton("Generate Report");
    connect(generateReportBtn_, &QPushButton::clicked,
            this, &AccessibilityPanel::generateReport);
    reportButtonLayout->addWidget(generateReportBtn_);
    
    autoFixBtn_ = new QPushButton("Auto-Fix Issues");
    connect(autoFixBtn_, &QPushButton::clicked, [this]() {
        checkAllContrasts();
    });
    reportButtonLayout->addWidget(autoFixBtn_);
    reportButtonLayout->addStretch();
    
    reportLayout->addLayout(reportButtonLayout);
    
    reportText_ = new QTextEdit();
    reportText_->setReadOnly(true);
    reportLayout->addWidget(reportText_);
    
    tabWidget_->addTab(reportWidget, "Accessibility Report");
    
    mainLayout->addWidget(tabWidget_);
}

void AccessibilityPanel::updateColors(const std::map<QString, QColor>& colors) {
    currentColors_ = colors;
    colorBlindSim_->setOriginalColors(colors);
}

void AccessibilityPanel::checkAllContrasts() {
    QString issues;
    int failCount = 0;
    
    for (const auto& pair : contrastPairs_) {
        if (currentColors_.count(pair.foregroundName) && 
            currentColors_.count(pair.backgroundName)) {
            
            QColor fg = currentColors_[pair.foregroundName];
            QColor bg = currentColors_[pair.backgroundName];
            
            double ratio = contrastChecker_->calculateContrastRatio(fg, bg);
            
            if (ratio < pair.requiredRatio) {
                failCount++;
                issues += QString("❌ %1: %2:1 (requires %3:1)\n")
                    .arg(pair.usage)
                    .arg(ratio, 0, 'f', 2)
                    .arg(pair.requiredRatio, 0, 'f', 1);
                
                // Suggest a fix
                QColor suggested = suggestAccessibleColor(fg, bg, pair.requiredRatio);
                emit suggestionMade(pair.foregroundName, suggested);
            }
        }
    }
    
    if (failCount > 0) {
        emit accessibilityIssueFound(QString("%1 contrast issues found").arg(failCount));
    }
}

void AccessibilityPanel::generateReport() {
    QString report = "<h2>Accessibility Report</h2>";
    report += QString("<p>Generated: %1</p>").arg(QDateTime::currentDateTime().toString());
    
    // Contrast analysis
    report += "<h3>Contrast Analysis</h3>";
    report += "<table border='1' cellpadding='5'>";
    report += "<tr><th>Usage</th><th>Foreground</th><th>Background</th><th>Ratio</th><th>WCAG AA</th><th>WCAG AAA</th></tr>";
    
    for (const auto& pair : contrastPairs_) {
        if (currentColors_.count(pair.foregroundName) && 
            currentColors_.count(pair.backgroundName)) {
            
            QColor fg = currentColors_[pair.foregroundName];
            QColor bg = currentColors_[pair.backgroundName];
            double ratio = contrastChecker_->calculateContrastRatio(fg, bg);
            
            bool passAA = ratio >= pair.requiredRatio;
            bool passAAA = ratio >= (pair.requiredRatio == WCAG::AA_LARGE_TEXT ? 
                                   WCAG::AAA_LARGE_TEXT : WCAG::AAA_NORMAL_TEXT);
            
            report += QString("<tr><td>%1</td><td style='background:%2;color:%3'>%4</td>"
                            "<td style='background:%5;color:%6'>%7</td><td>%8:1</td>"
                            "<td style='color:%9'>%10</td><td style='color:%11'>%12</td></tr>")
                .arg(pair.usage)
                .arg(fg.name()).arg(bg.name()).arg(pair.foregroundName)
                .arg(bg.name()).arg(fg.name()).arg(pair.backgroundName)
                .arg(ratio, 0, 'f', 2)
                .arg(passAA ? "#4CAF50" : "#F44336").arg(passAA ? "Pass" : "Fail")
                .arg(passAAA ? "#4CAF50" : "#F44336").arg(passAAA ? "Pass" : "Fail");
        }
    }
    
    report += "</table>";
    
    // Color blindness summary
    report += "<h3>Color Blindness Considerations</h3>";
    report += "<p>Approximately 8% of men and 0.5% of women have some form of color vision deficiency.</p>";
    report += "<ul>";
    report += "<li>Ensure important information is not conveyed by color alone</li>";
    report += "<li>Use patterns, icons, or text labels in addition to color</li>";
    report += "<li>Test your theme with the color blindness simulator</li>";
    report += "</ul>";
    
    reportText_->setHtml(report);
}

QColor AccessibilityPanel::suggestAccessibleColor(const QColor& foreground, 
                                                 const QColor& background, 
                                                 double targetRatio) {
    // Try to adjust the foreground color to meet the target ratio
    QColor suggested = foreground;
    double currentRatio = contrastChecker_->calculateContrastRatio(suggested, background);
    
    if (currentRatio < targetRatio) {
        // Need to increase contrast
        int h, s, v;
        suggested.getHsv(&h, &s, &v);
        
        // Try making it lighter or darker
        bool bgIsLight = background.value() > 128;
        
        while (currentRatio < targetRatio && ((bgIsLight && v > 0) || (!bgIsLight && v < 255))) {
            if (bgIsLight) {
                v = std::max(0, v - 10);  // Make darker
            } else {
                v = std::min(255, v + 10);  // Make lighter
            }
            
            suggested.setHsv(h, s, v);
            currentRatio = contrastChecker_->calculateContrastRatio(suggested, background);
        }
    }
    
    return suggested;
}

} // namespace llm_re::ui_v2