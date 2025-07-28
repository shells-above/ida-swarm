#pragma once

#include "../../core/ui_v2_common.h"
#include "custom_chart_base.h"
#include "chart_theme.h"

namespace llm_re::ui_v2::charts {

class BarChart : public CustomChartBase {
    Q_OBJECT
    
public:
    explicit BarChart(QWidget* parent = nullptr);
    ~BarChart() override;
    
    // Chart types
    enum ChartType {
        Vertical,
        Horizontal,
        Grouped,
        Stacked,
        Waterfall,
        Range
    };
    
    void setChartType(ChartType type);
    ChartType chartType() const { return chartType_; }
    
    // Data management
    void setCategories(const QStringList& categories);
    QStringList categories() const { return categories_; }
    
    void addSeries(const ChartSeries& series);
    void addSeries(const QString& name, const std::vector<double>& values);
    void updateSeries(int index, const ChartSeries& series);
    void removeSeries(int index);
    void clearSeries();
    
    // Bar specific data
    void setData(const QString& category, const QString& series, double value);
    double getData(const QString& category, const QString& series) const;
    
    // Configuration
    void setTheme(const BarChartTheme& theme);
    BarChartTheme theme() const { return theme_; }
    
    // Display options
    void setBarSpacing(float spacing);
    float barSpacing() const { return theme_.barSpacing; }
    
    void setCornerRadius(float radius);
    float cornerRadius() const { return theme_.cornerRadius; }
    
    void setShowValues(bool show);
    bool showValues() const { return theme_.showValues; }
    
    void setHorizontal(bool horizontal);
    bool isHorizontal() const { return theme_.horizontal; }
    
    void setGradient(bool enabled);
    bool gradient() const { return theme_.gradient; }
    
    void setStacked(bool stacked);
    bool isStacked() const { return chartType_ == Stacked; }
    
    // Value formatting
    void setValueFormat(const QString& format);
    QString valueFormat() const { return valueFormat_; }
    
    void setValuePrefix(const QString& prefix);
    QString valuePrefix() const { return valuePrefix_; }
    
    void setValueSuffix(const QString& suffix);
    QString valueSuffix() const { return valueSuffix_; }
    
    // Animation
    void setGrowthAnimation(bool enabled);
    bool growthAnimation() const { return theme_.animateGrowth; }
    
    // Data update
    void updateData() override;
    
    // Get bar info
    int barAt(const QPointF& pos) const;
    QString categoryAt(const QPointF& pos) const;
    int seriesAt(const QPointF& pos) const;
    
signals:
    void barClicked(const QString& category, int seriesIndex);
    void barHovered(const QString& category, int seriesIndex);
    void categoryClicked(const QString& category);
    
protected:
    void drawData(QPainter* painter) override;
    void drawLegend(QPainter* painter) override;
    void drawAxes(QPainter* painter) override;
    int findNearestDataPoint(const QPointF& pos, int& seriesIndex) override;
    
private:
    // Drawing methods
    void drawVerticalBars(QPainter* painter);
    void drawHorizontalBars(QPainter* painter);
    void drawGroupedBars(QPainter* painter);
    void drawStackedBars(QPainter* painter);
    void drawWaterfallChart(QPainter* painter);
    void drawRangeChart(QPainter* painter);
    
    void drawBar(QPainter* painter, const QRectF& rect, double value, 
                const QColor& color, int categoryIndex, int seriesIndex);
    void drawBarValue(QPainter* painter, const QRectF& barRect, double value);
    void drawCategoryLabel(QPainter* painter, const QString& category, 
                          const QPointF& position, bool rotated = false);
    
    // Calculation helpers
    void calculateBarLayout();
    QRectF calculateBarRect(int categoryIndex, int seriesIndex, double value);
    double calculateBarHeight(double value) const;
    double calculateStackedHeight(int categoryIndex, int seriesIndex) const;
    QColor getBarColor(int seriesIndex, int categoryIndex) const;
    
    // Animation helpers
    double getAnimatedHeight(double targetHeight, int categoryIndex, int seriesIndex);
    
    // Hit testing
    struct BarInfo {
        QRectF rect;
        int categoryIndex;
        int seriesIndex;
        double value;
    };
    bool isPointInBar(const QPointF& point, const BarInfo& bar) const;
    
private:
    // Data
    QStringList categories_;
    std::vector<ChartSeries> series_;
    std::map<std::pair<QString, QString>, double> dataMap_; // (category, series) -> value
    
    // Configuration
    ChartType chartType_ = Vertical;
    BarChartTheme theme_;
    QString valueFormat_ = "%.1f";
    QString valuePrefix_;
    QString valueSuffix_;
    
    // Layout
    struct BarLayout {
        double categoryWidth = 0.0;
        double barWidth = 0.0;
        double groupWidth = 0.0;
        double totalWidth = 0.0;
        double totalHeight = 0.0;
        std::vector<std::vector<BarInfo>> bars; // [category][series]
    } layout_;
    
    // Interaction
    int hoveredCategory_ = -1;
    int hoveredSeries_ = -1;
    int selectedCategory_ = -1;
    int selectedSeries_ = -1;
    
    // Animation state
    std::vector<std::vector<double>> animatedHeights_;
    std::vector<std::vector<double>> targetHeights_;
    
    // Range chart specific
    struct RangeData {
        double min;
        double max;
    };
    std::map<std::pair<QString, QString>, RangeData> rangeData_;
    
    // Waterfall specific
    std::vector<double> waterfallTotals_;
    std::vector<bool> waterfallIncreases_;
};

} // namespace llm_re::ui_v2::charts