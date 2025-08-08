#include "../../../core/ui_v2_common.h"
#include "chart_theme_widget.h"
#include "color_picker_widget.h"
#include "../../../widgets/charts/chart_types.h"
#include "../../../core/theme_manager.h"

namespace llm_re::ui_v2 {

ChartThemeWidget::ChartThemeWidget(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    loadSettings();
}

void ChartThemeWidget::setupUI() {
    auto* mainLayout = new QHBoxLayout(this);
    
    // Left side: Settings
    auto* settingsWidget = new QWidget();
    auto* settingsLayout = new QVBoxLayout(settingsWidget);
    
    createStyleSelector(settingsWidget);
    
    // Tab widget for different property groups
    auto* propTabs = new QTabWidget();
    
    // Line chart properties
    auto* lineWidget = new QWidget();
    auto* lineLayout = new QVBoxLayout(lineWidget);
    createPropertySettings(lineWidget);
    propTabs->addTab(lineWidget, "Line Charts");
    
    // Series colors tab
    auto* colorsWidget = new QWidget();
    auto* colorsLayout = new QVBoxLayout(colorsWidget);
    createSeriesColors(colorsWidget);
    propTabs->addTab(colorsWidget, "Series Colors");
    
    settingsLayout->addWidget(propTabs);
    settingsLayout->addStretch();
    
    // Right side: Preview
    auto* previewWidget = new QWidget();
    auto* previewLayout = new QVBoxLayout(previewWidget);
    
    previewLayout->addWidget(new QLabel("Chart Previews"));
    createChartPreviews();
    previewLayout->addWidget(previewTabs_);
    
    mainLayout->addWidget(settingsWidget, 3);
    mainLayout->addWidget(previewWidget, 2);
}

void ChartThemeWidget::createStyleSelector(QWidget* parent) {
    auto* layout = static_cast<QVBoxLayout*>(parent->layout());
    
    auto* group = new QGroupBox("Chart Style Preset");
    auto* groupLayout = new QVBoxLayout(group);
    
    styleCombo_ = new QComboBox();
    styleCombo_->addItems({
        "Modern", "Neon", "Corporate", "Playful", "Terminal", "Glass"
    });
    connect(styleCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ChartThemeWidget::onStyleChanged);
    groupLayout->addWidget(styleCombo_);
    
    styleDescription_ = new QTextEdit();
    styleDescription_->setReadOnly(true);
    styleDescription_->setMaximumHeight(60);
    styleDescription_->setFrameStyle(QFrame::NoFrame);
    groupLayout->addWidget(styleDescription_);
    
    auto* resetButton = new QPushButton("Reset to Style Defaults");
    connect(resetButton, &QPushButton::clicked, this, &ChartThemeWidget::resetToDefaults);
    groupLayout->addWidget(resetButton);
    
    layout->addWidget(group);
}

void ChartThemeWidget::createPropertySettings(QWidget* parent) {
    auto* layout = static_cast<QVBoxLayout*>(parent->layout());
    
    // Line chart properties
    auto* lineGroup = new QGroupBox("Line Chart Properties");
    auto* lineLayout = new QFormLayout(lineGroup);
    
    lineWidthSpin_ = new QDoubleSpinBox();
    lineWidthSpin_->setRange(0.5, 10.0);
    lineWidthSpin_->setSingleStep(0.5);
    lineWidthSpin_->setSuffix(" px");
    connect(lineWidthSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ChartThemeWidget::onPropertyChanged);
    lineLayout->addRow("Line Width:", lineWidthSpin_);
    
    pointRadiusSpin_ = new QDoubleSpinBox();
    pointRadiusSpin_->setRange(0, 10.0);
    pointRadiusSpin_->setSingleStep(0.5);
    pointRadiusSpin_->setSuffix(" px");
    connect(pointRadiusSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ChartThemeWidget::onPropertyChanged);
    lineLayout->addRow("Point Radius:", pointRadiusSpin_);
    
    smoothCurvesCheck_ = new QCheckBox("Smooth Curves");
    connect(smoothCurvesCheck_, &QCheckBox::toggled,
            this, &ChartThemeWidget::onPropertyChanged);
    lineLayout->addRow(smoothCurvesCheck_);
    
    showDataPointsCheck_ = new QCheckBox("Show Data Points");
    connect(showDataPointsCheck_, &QCheckBox::toggled,
            this, &ChartThemeWidget::onPropertyChanged);
    lineLayout->addRow(showDataPointsCheck_);
    
    auto* areaLayout = new QHBoxLayout();
    areaLayout->addWidget(new QLabel("Area Opacity:"));
    areaOpacitySlider_ = new QSlider(Qt::Horizontal);
    areaOpacitySlider_->setRange(0, 100);
    connect(areaOpacitySlider_, &QSlider::valueChanged,
            this, &ChartThemeWidget::onPropertyChanged);
    areaLayout->addWidget(areaOpacitySlider_);
    auto* areaLabel = new QLabel("20%");
    connect(areaOpacitySlider_, &QSlider::valueChanged,
            [areaLabel](int value) { areaLabel->setText(QString("%1%").arg(value)); });
    areaLayout->addWidget(areaLabel);
    lineLayout->addRow(areaLayout);
    
    layout->addWidget(lineGroup);
    
    // Bar chart properties
    auto* barGroup = new QGroupBox("Bar Chart Properties");
    auto* barLayout = new QFormLayout(barGroup);
    
    auto* spacingLayout = new QHBoxLayout();
    spacingLayout->addWidget(new QLabel("Bar Spacing:"));
    barSpacingSlider_ = new QSlider(Qt::Horizontal);
    barSpacingSlider_->setRange(0, 50);
    connect(barSpacingSlider_, &QSlider::valueChanged,
            this, &ChartThemeWidget::onPropertyChanged);
    spacingLayout->addWidget(barSpacingSlider_);
    barLayout->addRow(spacingLayout);
    
    barRadiusSpin_ = new QDoubleSpinBox();
    barRadiusSpin_->setRange(0, 20.0);
    barRadiusSpin_->setSuffix(" px");
    connect(barRadiusSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ChartThemeWidget::onPropertyChanged);
    barLayout->addRow("Corner Radius:", barRadiusSpin_);
    
    barGradientCheck_ = new QCheckBox("Bar Gradient");
    connect(barGradientCheck_, &QCheckBox::toggled,
            this, &ChartThemeWidget::onPropertyChanged);
    barLayout->addRow(barGradientCheck_);
    
    barShadowCheck_ = new QCheckBox("Bar Shadow");
    connect(barShadowCheck_, &QCheckBox::toggled,
            this, &ChartThemeWidget::onPropertyChanged);
    barLayout->addRow(barShadowCheck_);
    
    showBarValuesCheck_ = new QCheckBox("Show Values");
    connect(showBarValuesCheck_, &QCheckBox::toggled,
            this, &ChartThemeWidget::onPropertyChanged);
    barLayout->addRow(showBarValuesCheck_);
    
    layout->addWidget(barGroup);
    
    // General properties
    auto* generalGroup = new QGroupBox("General Properties");
    auto* generalLayout = new QFormLayout(generalGroup);
    
    animateOnLoadCheck_ = new QCheckBox("Animate on Load");
    connect(animateOnLoadCheck_, &QCheckBox::toggled,
            this, &ChartThemeWidget::onPropertyChanged);
    generalLayout->addRow(animateOnLoadCheck_);
    
    animateOnUpdateCheck_ = new QCheckBox("Animate on Update");
    connect(animateOnUpdateCheck_, &QCheckBox::toggled,
            this, &ChartThemeWidget::onPropertyChanged);
    generalLayout->addRow(animateOnUpdateCheck_);
    
    animationDurationSpin_ = new QSpinBox();
    animationDurationSpin_->setRange(0, 5000);
    animationDurationSpin_->setSuffix(" ms");
    connect(animationDurationSpin_, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ChartThemeWidget::onPropertyChanged);
    generalLayout->addRow("Animation Duration:", animationDurationSpin_);
    
    glowEffectsCheck_ = new QCheckBox("Glow Effects");
    connect(glowEffectsCheck_, &QCheckBox::toggled,
            this, &ChartThemeWidget::onPropertyChanged);
    generalLayout->addRow(glowEffectsCheck_);
    
    glowRadiusSpin_ = new QDoubleSpinBox();
    glowRadiusSpin_->setRange(0, 50.0);
    glowRadiusSpin_->setSuffix(" px");
    connect(glowRadiusSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ChartThemeWidget::onPropertyChanged);
    generalLayout->addRow("Glow Radius:", glowRadiusSpin_);
    
    layout->addWidget(generalGroup);
    layout->addStretch();
}

void ChartThemeWidget::createSeriesColors(QWidget* parent) {
    auto* layout = static_cast<QVBoxLayout*>(parent->layout());
    
    auto* group = new QGroupBox("Chart Series Colors");
    auto* groupLayout = new QVBoxLayout(group);
    
    // Color list
    colorsList_ = new QListWidget();
    colorsList_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(colorsList_, &QListWidget::currentRowChanged,
            this, &ChartThemeWidget::onSeriesColorChanged);
    groupLayout->addWidget(colorsList_);
    
    // Color picker
    auto* pickerLayout = new QHBoxLayout();
    pickerLayout->addWidget(new QLabel("Selected Color:"));
    colorPicker_ = new ColorPickerWidget();
    connect(colorPicker_, &ColorPickerWidget::colorChanged,
            [this](const QColor& color) {
                int row = colorsList_->currentRow();
                if (row >= 0 && row < currentSeriesColors_.size()) {
                    currentSeriesColors_[row] = color;
                    updateColorListItem(row);
                    updatePreview();
                    emit settingChanged();
                }
            });
    pickerLayout->addWidget(colorPicker_);
    pickerLayout->addStretch();
    groupLayout->addLayout(pickerLayout);
    
    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    
    addColorButton_ = new QPushButton("Add");
    connect(addColorButton_, &QPushButton::clicked, this, &ChartThemeWidget::addSeriesColor);
    buttonLayout->addWidget(addColorButton_);
    
    removeColorButton_ = new QPushButton("Remove");
    connect(removeColorButton_, &QPushButton::clicked, this, &ChartThemeWidget::removeSeriesColor);
    buttonLayout->addWidget(removeColorButton_);
    
    moveUpButton_ = new QPushButton("Move Up");
    connect(moveUpButton_, &QPushButton::clicked, this, &ChartThemeWidget::moveColorUp);
    buttonLayout->addWidget(moveUpButton_);
    
    moveDownButton_ = new QPushButton("Move Down");
    connect(moveDownButton_, &QPushButton::clicked, this, &ChartThemeWidget::moveColorDown);
    buttonLayout->addWidget(moveDownButton_);
    
    buttonLayout->addStretch();
    groupLayout->addLayout(buttonLayout);
    
    layout->addWidget(group);
}

void ChartThemeWidget::createChartPreviews() {
    previewTabs_ = new QTabWidget();
    
    linePreview_ = new MiniChartPreview(MiniChartPreview::Line);
    previewTabs_->addTab(linePreview_, "Line Chart");
    
    barPreview_ = new MiniChartPreview(MiniChartPreview::Bar);
    previewTabs_->addTab(barPreview_, "Bar Chart");
    
    piePreview_ = new MiniChartPreview(MiniChartPreview::Pie);
    previewTabs_->addTab(piePreview_, "Pie Chart");
    
    heatmapPreview_ = new MiniChartPreview(MiniChartPreview::Heatmap);
    previewTabs_->addTab(heatmapPreview_, "Heatmap");
}

void ChartThemeWidget::loadSettings() {
    auto& tm = ThemeManager::instance();
    
    currentStyle_ = tm.currentChartStyle();
    currentSettings_ = tm.componentStyles().chart;
    currentSeriesColors_ = tm.chartSeriesColors();
    
    // Update UI
    styleCombo_->setCurrentIndex(static_cast<int>(currentStyle_));
    
    lineWidthSpin_->setValue(currentSettings_.lineWidth);
    pointRadiusSpin_->setValue(currentSettings_.pointRadius);
    smoothCurvesCheck_->setChecked(currentSettings_.smoothCurves);
    showDataPointsCheck_->setChecked(currentSettings_.showDataPoints);
    areaOpacitySlider_->setValue(currentSettings_.areaOpacity * 100);
    
    barSpacingSlider_->setValue(currentSettings_.barSpacing * 100);
    barRadiusSpin_->setValue(currentSettings_.barCornerRadius);
    barGradientCheck_->setChecked(currentSettings_.barGradient);
    barShadowCheck_->setChecked(currentSettings_.barShadow);
    showBarValuesCheck_->setChecked(currentSettings_.showBarValues);
    
    animateOnLoadCheck_->setChecked(currentSettings_.animateOnLoad);
    animateOnUpdateCheck_->setChecked(currentSettings_.animateOnUpdate);
    animationDurationSpin_->setValue(currentSettings_.animationDuration);
    glowEffectsCheck_->setChecked(currentSettings_.glowEffects);
    glowRadiusSpin_->setValue(currentSettings_.glowRadius);
    
    // Update color list
    updateColorList();
    
    updatePreview();
}

void ChartThemeWidget::onStyleChanged() {
    currentStyle_ = static_cast<ThemeManager::ChartStyle>(styleCombo_->currentIndex());
    
    // Update description
    QString desc;
    switch (currentStyle_) {
        case ThemeManager::ChartStyle::Modern:
            desc = "Clean, minimal design with subtle effects";
            break;
        case ThemeManager::ChartStyle::Neon:
            desc = "Vibrant colors with strong glow effects";
            break;
        case ThemeManager::ChartStyle::Corporate:
            desc = "Professional, muted colors without effects";
            break;
        case ThemeManager::ChartStyle::Playful:
            desc = "Bright colors with bounce animations";
            break;
        case ThemeManager::ChartStyle::Terminal:
            desc = "Monochrome, ASCII-inspired look";
            break;
        case ThemeManager::ChartStyle::Glass:
            desc = "Transparent with blur effects";
            break;
    }
    styleDescription_->setText(desc);
    
    emit settingChanged();
}

void ChartThemeWidget::onPropertyChanged() {
    // Update current settings from UI
    currentSettings_.lineWidth = lineWidthSpin_->value();
    currentSettings_.pointRadius = pointRadiusSpin_->value();
    currentSettings_.smoothCurves = smoothCurvesCheck_->isChecked();
    currentSettings_.showDataPoints = showDataPointsCheck_->isChecked();
    currentSettings_.areaOpacity = areaOpacitySlider_->value() / 100.0f;
    
    currentSettings_.barSpacing = barSpacingSlider_->value() / 100.0f;
    currentSettings_.barCornerRadius = barRadiusSpin_->value();
    currentSettings_.barGradient = barGradientCheck_->isChecked();
    currentSettings_.barShadow = barShadowCheck_->isChecked();
    currentSettings_.showBarValues = showBarValuesCheck_->isChecked();
    
    currentSettings_.animateOnLoad = animateOnLoadCheck_->isChecked();
    currentSettings_.animateOnUpdate = animateOnUpdateCheck_->isChecked();
    currentSettings_.animationDuration = animationDurationSpin_->value();
    currentSettings_.glowEffects = glowEffectsCheck_->isChecked();
    currentSettings_.glowRadius = glowRadiusSpin_->value();
    
    updatePreview();
    emit settingChanged();
}

void ChartThemeWidget::onSeriesColorChanged() {
    int row = colorsList_->currentRow();
    if (row >= 0 && row < currentSeriesColors_.size()) {
        colorPicker_->setColor(currentSeriesColors_[row]);
    }
}

void ChartThemeWidget::addSeriesColor() {
    QColor newColor = ThemeManager::instance().colors().primary;
    currentSeriesColors_.push_back(newColor);
    updateColorList();
    colorsList_->setCurrentRow(currentSeriesColors_.size() - 1);
    updatePreview();
    emit settingChanged();
}

void ChartThemeWidget::removeSeriesColor() {
    int row = colorsList_->currentRow();
    if (row >= 0 && row < currentSeriesColors_.size() && currentSeriesColors_.size() > 1) {
        currentSeriesColors_.erase(currentSeriesColors_.begin() + row);
        updateColorList();
        updatePreview();
        emit settingChanged();
    }
}

void ChartThemeWidget::moveColorUp() {
    int row = colorsList_->currentRow();
    if (row > 0 && row < currentSeriesColors_.size()) {
        std::swap(currentSeriesColors_[row], currentSeriesColors_[row - 1]);
        updateColorList();
        colorsList_->setCurrentRow(row - 1);
        updatePreview();
        emit settingChanged();
    }
}

void ChartThemeWidget::moveColorDown() {
    int row = colorsList_->currentRow();
    if (row >= 0 && row < currentSeriesColors_.size() - 1) {
        std::swap(currentSeriesColors_[row], currentSeriesColors_[row + 1]);
        updateColorList();
        colorsList_->setCurrentRow(row + 1);
        updatePreview();
        emit settingChanged();
    }
}

void ChartThemeWidget::resetToDefaults() {
    applyStylePreset(currentStyle_);
    loadSettings();
}

void ChartThemeWidget::updatePreview() {
    linePreview_->updateSettings(currentSettings_, currentSeriesColors_);
    barPreview_->updateSettings(currentSettings_, currentSeriesColors_);
    piePreview_->updateSettings(currentSettings_, currentSeriesColors_);
    heatmapPreview_->updateSettings(currentSettings_, currentSeriesColors_);
}

void ChartThemeWidget::updateColorList() {
    colorsList_->clear();
    for (int i = 0; i < currentSeriesColors_.size(); ++i) {
        updateColorListItem(i);
    }
}

void ChartThemeWidget::updateColorListItem(int index) {
    if (index >= 0 && index < currentSeriesColors_.size()) {
        const QColor& color = currentSeriesColors_[index];
        
        auto* item = colorsList_->item(index);
        if (!item) {
            item = new QListWidgetItem();
            colorsList_->addItem(item);
        }
        
        item->setText(QString("Series %1: %2").arg(index + 1).arg(color.name()));
        
        // Create color icon
        QPixmap pixmap(16, 16);
        pixmap.fill(color);
        item->setIcon(QIcon(pixmap));
    }
}

void ChartThemeWidget::applyStylePreset(ThemeManager::ChartStyle style) {
    // This would normally be done in ThemeManager
    // For now, just update some defaults
    switch (style) {
        case ThemeManager::ChartStyle::Modern:
            currentSettings_.lineWidth = 2.5f;
            currentSettings_.glowEffects = false;
            currentSettings_.animationDuration = 600;
            break;
        case ThemeManager::ChartStyle::Neon:
            currentSettings_.lineWidth = 3.0f;
            currentSettings_.glowEffects = true;
            currentSettings_.glowRadius = 20.0f;
            currentSettings_.animationDuration = 1000;
            break;
        // ... other presets
    }
}

// MiniChartPreview implementation

ChartThemeWidget::MiniChartPreview::MiniChartPreview(ChartType type, QWidget* parent)
    : QWidget(parent), chartType_(type) {
    setMinimumSize(300, 200);
    // Use explicit colors from theme instead of palette to avoid inheriting IDA's theme
    const auto& colors = ThemeManager::instance().colors();
    setStyleSheet(QString("background-color: %1; border: 1px solid %2;")
                 .arg(colors.surface.name())
                 .arg(colors.border.name()));
}

void ChartThemeWidget::MiniChartPreview::updateSettings(const ComponentStyles::Chart& settings,
                                                       const std::vector<QColor>& colors) {
    settings_ = settings;
    seriesColors_ = colors;
    update();
}

void ChartThemeWidget::MiniChartPreview::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Clear background
    const auto& colors = ThemeManager::instance().colors();
    painter.fillRect(rect(), colors.surface);
    
    // Draw based on chart type
    switch (chartType_) {
        case Line:
            drawLineChart(painter);
            break;
        case Bar:
            drawBarChart(painter);
            break;
        case Pie:
            drawPieChart(painter);
            break;
        case Heatmap:
            drawHeatmap(painter);
            break;
    }
}

void ChartThemeWidget::MiniChartPreview::drawLineChart(QPainter& painter) {
    QRectF chartRect = rect().adjusted(20, 20, -20, -20);
    
    // Draw axes
    painter.setPen(QPen(ThemeManager::instance().colors().textSecondary, 1));
    painter.drawLine(chartRect.bottomLeft(), chartRect.bottomRight());
    painter.drawLine(chartRect.bottomLeft(), chartRect.topLeft());
    
    // Generate sample data
    const int dataPoints = 10;
    auto data1 = generateSampleData(dataPoints);
    auto data2 = generateSampleData(dataPoints);
    
    // Draw lines
    auto drawSeries = [&](const std::vector<qreal>& data, const QColor& color, int seriesIndex) {
        QPainterPath path;
        
        for (int i = 0; i < data.size(); ++i) {
            qreal x = chartRect.left() + (i / qreal(data.size() - 1)) * chartRect.width();
            qreal y = chartRect.bottom() - data[i] * chartRect.height();
            
            if (i == 0) {
                path.moveTo(x, y);
            } else {
                if (settings_.smoothCurves) {
                    // Simple bezier curve
                    qreal prevX = chartRect.left() + ((i-1) / qreal(data.size() - 1)) * chartRect.width();
                    qreal prevY = chartRect.bottom() - data[i-1] * chartRect.height();
                    qreal ctrlX = (prevX + x) / 2;
                    path.quadTo(ctrlX, prevY, x, y);
                } else {
                    path.lineTo(x, y);
                }
            }
        }
        
        // Draw glow effect if enabled
        if (settings_.glowEffects) {
            painter.setPen(QPen(color.lighter(150), settings_.lineWidth + settings_.glowRadius, 
                               Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.setOpacity(0.3);
            painter.drawPath(path);
            painter.setOpacity(1.0);
        }
        
        // Draw the line
        painter.setPen(QPen(color, settings_.lineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
        
        // Draw data points if enabled
        if (settings_.showDataPoints) {
            painter.setBrush(color);
            for (int i = 0; i < data.size(); ++i) {
                qreal x = chartRect.left() + (i / qreal(data.size() - 1)) * chartRect.width();
                qreal y = chartRect.bottom() - data[i] * chartRect.height();
                painter.drawEllipse(QPointF(x, y), settings_.pointRadius, settings_.pointRadius);
            }
        }
    };
    
    // Draw series
    if (seriesColors_.size() >= 2) {
        drawSeries(data1, seriesColors_[0], 0);
        drawSeries(data2, seriesColors_[1], 1);
    }
}

void ChartThemeWidget::MiniChartPreview::drawBarChart(QPainter& painter) {
    QRectF chartRect = rect().adjusted(20, 20, -20, -20);
    
    // Draw axes
    painter.setPen(QPen(ThemeManager::instance().colors().textSecondary, 1));
    painter.drawLine(chartRect.bottomLeft(), chartRect.bottomRight());
    painter.drawLine(chartRect.bottomLeft(), chartRect.topLeft());
    
    // Generate sample data
    const int barCount = 6;
    auto data = generateSampleData(barCount);
    
    // Calculate bar width
    qreal totalSpacing = chartRect.width() * settings_.barSpacing;
    qreal barWidth = (chartRect.width() - totalSpacing) / barCount;
    
    // Draw bars
    for (int i = 0; i < data.size(); ++i) {
        qreal x = chartRect.left() + i * (barWidth + totalSpacing / (barCount - 1));
        qreal barHeight = data[i] * chartRect.height();
        qreal y = chartRect.bottom() - barHeight;
        
        QRectF barRect(x, y, barWidth, barHeight);
        
        // Apply corner radius
        QPainterPath barPath;
        barPath.addRoundedRect(barRect, settings_.barCornerRadius, settings_.barCornerRadius);
        
        QColor barColor = seriesColors_[i % seriesColors_.size()];
        
        // Draw shadow if enabled
        if (settings_.barShadow) {
            painter.fillPath(barPath.translated(2, 2), ThemeManager::instance().colors().shadow);
        }
        
        // Fill with gradient if enabled
        if (settings_.barGradient) {
            QLinearGradient gradient(barRect.topLeft(), barRect.bottomLeft());
            gradient.setColorAt(0, barColor.lighter(120));
            gradient.setColorAt(1, barColor);
            painter.fillPath(barPath, gradient);
        } else {
            painter.fillPath(barPath, barColor);
        }
        
        // Draw value label if enabled
        if (settings_.showBarValues) {
            painter.setPen(ThemeManager::instance().colors().textPrimary);
            painter.drawText(barRect.adjusted(0, -20, 0, -5), Qt::AlignCenter,
                           QString::number(int(data[i] * 100)));
        }
    }
}

void ChartThemeWidget::MiniChartPreview::drawPieChart(QPainter& painter) {
    QRectF chartRect = rect().adjusted(40, 40, -40, -40);
    const auto& colors = ThemeManager::instance().colors();
    
    // Generate sample data
    std::vector<qreal> data = {0.3, 0.25, 0.2, 0.15, 0.1};
    
    qreal startAngle = 0;
    qreal innerRadius = chartRect.width() / 2 * settings_.innerRadiusRatio;
    
    for (int i = 0; i < data.size(); ++i) {
        qreal sweepAngle = data[i] * 360;
        
        QPainterPath path;
        path.moveTo(chartRect.center());
        path.arcTo(chartRect, startAngle, sweepAngle);
        path.closeSubpath();
        
        // Create donut if inner radius > 0
        if (innerRadius > 0) {
            QRectF innerRect = chartRect.adjusted(
                chartRect.width() / 2 - innerRadius,
                chartRect.height() / 2 - innerRadius,
                -(chartRect.width() / 2 - innerRadius),
                -(chartRect.height() / 2 - innerRadius)
            );
            
            QPainterPath innerPath;
            innerPath.moveTo(chartRect.center());
            innerPath.arcTo(innerRect, startAngle, sweepAngle);
            innerPath.closeSubpath();
            
            path = path.subtracted(innerPath);
        }
        
        QColor segmentColor = seriesColors_[i % seriesColors_.size()];
        
        // Apply glow effect if enabled
        if (settings_.glowEffects) {
            painter.setPen(QPen(segmentColor.lighter(150), settings_.glowRadius));
            painter.setOpacity(0.3);
            painter.drawPath(path);
            painter.setOpacity(1.0);
        }
        
        painter.fillPath(path, segmentColor);
        
        // Add segment spacing
        if (settings_.segmentSpacing > 0) {
            painter.setPen(QPen(colors.surface, settings_.segmentSpacing));
            painter.drawPath(path);
        }
        
        startAngle += sweepAngle;
    }
}

void ChartThemeWidget::MiniChartPreview::drawHeatmap(QPainter& painter) {
    QRectF chartRect = rect().adjusted(20, 20, -20, -20);
    
    const int rows = 8;
    const int cols = 10;
    
    qreal cellWidth = (chartRect.width() - (cols - 1) * settings_.cellSpacing) / cols;
    qreal cellHeight = (chartRect.height() - (rows - 1) * settings_.cellSpacing) / rows;
    
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            qreal value = (std::sin(row * 0.5) + std::cos(col * 0.3) + 2) / 4;
            
            qreal x = chartRect.left() + col * (cellWidth + settings_.cellSpacing);
            qreal y = chartRect.top() + row * (cellHeight + settings_.cellSpacing);
            
            QRectF cellRect(x, y, cellWidth, cellHeight);
            
            // Interpolate color based on value using theme colors
            QColor cellColor;
            const auto& colors = ThemeManager::instance().colors();
            if (value < 0.5) {
                // Interpolate from info (blue) to success (green)
                cellColor = charts::ChartUtils::interpolateColor(colors.info, colors.success, value * 2);
            } else {
                // Interpolate from success (green) to error (red)
                cellColor = charts::ChartUtils::interpolateColor(colors.success, colors.error, (value - 0.5) * 2);
            }
            
            if (settings_.cellCornerRadius > 0) {
                QPainterPath cellPath;
                cellPath.addRoundedRect(cellRect, settings_.cellCornerRadius, settings_.cellCornerRadius);
                painter.fillPath(cellPath, cellColor);
            } else {
                painter.fillRect(cellRect, cellColor);
            }
        }
    }
}

std::vector<qreal> ChartThemeWidget::MiniChartPreview::generateSampleData(int count) {
    std::vector<qreal> data;
    for (int i = 0; i < count; ++i) {
        qreal value = 0.3 + 0.5 * std::sin(i * 0.8) * std::cos(i * 0.3) + 0.2 * (rand() / qreal(RAND_MAX));
        data.push_back(qBound(0.1, value, 0.9));
    }
    return data;
}

ThemeManager::ChartStyle ChartThemeWidget::selectedStyle() const {
    return currentStyle_;
}

ComponentStyles::Chart ChartThemeWidget::getChartSettings() const {
    return currentSettings_;
}

std::vector<QColor> ChartThemeWidget::getSeriesColors() const {
    return currentSeriesColors_;
}

} // namespace llm_re::ui_v2