#pragma once

#include "color_picker_widget.h"
#include "../../../core/ui_v2_common.h"
#include "../../../core/theme_manager.h"
#include "../../../widgets/charts/chart_types.h"

namespace llm_re::ui_v2 {

class ChartThemeWidget : public QWidget {
    Q_OBJECT

public:
    explicit ChartThemeWidget(QWidget* parent = nullptr);
    ~ChartThemeWidget() = default;

    // Load current settings
    void loadSettings();
    
    // Get current settings
    ThemeManager::ChartStyle selectedStyle() const;
    ComponentStyles::Chart getChartSettings() const;
    std::vector<QColor> getSeriesColors() const;

signals:
    void settingChanged();

private slots:
    void onStyleChanged();
    void onPropertyChanged();
    void onSeriesColorChanged();
    void addSeriesColor();
    void removeSeriesColor();
    void moveColorUp();
    void moveColorDown();
    void resetToDefaults();
    void updatePreview();

    void updateColorList();

    void updateColorListItem(int index);

private:
    void setupUI();
    void createStyleSelector(QWidget* parent);
    void createPropertySettings(QWidget* parent);
    void createSeriesColors(QWidget* parent);
    void createChartPreviews();
    
    // Chart preview widgets
    class MiniChartPreview : public QWidget {
    public:
        enum ChartType { Line, Bar, Pie, Heatmap };
        
        explicit MiniChartPreview(ChartType type, QWidget* parent = nullptr);
        
        void updateSettings(const ComponentStyles::Chart& settings,
                          const std::vector<QColor>& colors);
        
    protected:
        void paintEvent(QPaintEvent* event) override;
        
    private:
        ChartType chartType_;
        ComponentStyles::Chart settings_;
        std::vector<QColor> seriesColors_;
        
        void drawLineChart(QPainter& painter);
        void drawBarChart(QPainter& painter);
        void drawPieChart(QPainter& painter);
        void drawHeatmap(QPainter& painter);
        
        // Sample data
        std::vector<qreal> generateSampleData(int count);
    };
    
    // Style selector
    QComboBox* styleCombo_ = nullptr;
    QTextEdit* styleDescription_ = nullptr;
    
    // Property settings
    struct PropertyControl {
        QLabel* label = nullptr;
        QWidget* control = nullptr;
        std::function<void()> updateFunc;
    };
    std::map<QString, PropertyControl> propertyControls_;
    
    // Line chart properties
    QDoubleSpinBox* lineWidthSpin_ = nullptr;
    QDoubleSpinBox* pointRadiusSpin_ = nullptr;
    QCheckBox* smoothCurvesCheck_ = nullptr;
    QCheckBox* showDataPointsCheck_ = nullptr;
    QSlider* areaOpacitySlider_ = nullptr;
    
    // Bar chart properties
    QSlider* barSpacingSlider_ = nullptr;
    QDoubleSpinBox* barRadiusSpin_ = nullptr;
    QCheckBox* barGradientCheck_ = nullptr;
    QCheckBox* barShadowCheck_ = nullptr;
    QCheckBox* showBarValuesCheck_ = nullptr;
    
    // Pie chart properties
    QSlider* innerRadiusSlider_ = nullptr;
    QDoubleSpinBox* segmentSpacingSpin_ = nullptr;
    QDoubleSpinBox* hoverScaleSpin_ = nullptr;
    QDoubleSpinBox* hoverOffsetSpin_ = nullptr;
    
    // General properties
    QCheckBox* animateOnLoadCheck_ = nullptr;
    QCheckBox* animateOnUpdateCheck_ = nullptr;
    QSpinBox* animationDurationSpin_ = nullptr;
    QCheckBox* showTooltipsCheck_ = nullptr;
    QCheckBox* showLegendCheck_ = nullptr;
    QCheckBox* glowEffectsCheck_ = nullptr;
    QDoubleSpinBox* glowRadiusSpin_ = nullptr;
    
    // Series colors
    QListWidget* colorsList_ = nullptr;
    QPushButton* addColorButton_ = nullptr;
    QPushButton* removeColorButton_ = nullptr;
    QPushButton* moveUpButton_ = nullptr;
    QPushButton* moveDownButton_ = nullptr;
    ColorPickerWidget* colorPicker_ = nullptr;
    
    // Preview area
    QTabWidget* previewTabs_ = nullptr;
    MiniChartPreview* linePreview_ = nullptr;
    MiniChartPreview* barPreview_ = nullptr;
    MiniChartPreview* piePreview_ = nullptr;
    MiniChartPreview* heatmapPreview_ = nullptr;
    
    // Current settings
    ThemeManager::ChartStyle currentStyle_ = ThemeManager::ChartStyle::Modern;
    ComponentStyles::Chart currentSettings_;
    std::vector<QColor> currentSeriesColors_;
    
    void updatePropertyControls();
    void applyStylePreset(ThemeManager::ChartStyle style);
};

} // namespace llm_re::ui_v2