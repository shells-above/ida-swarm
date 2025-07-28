#pragma once

#include "../../core/ui_v2_common.h"
#include "custom_chart_base.h"
#include "chart_theme.h"

namespace llm_re::ui_v2::charts {

class LineChart : public CustomChartBase {
    Q_OBJECT
    
public:
    explicit LineChart(QWidget* parent = nullptr);
    ~LineChart() override;
    
    // Data management
    void addSeries(const ChartSeries& series);
    void addSeries(const QString& name, const std::vector<ChartDataPoint>& points);
    void updateSeries(int index, const ChartSeries& series);
    void updateSeries(const QString& name, const std::vector<ChartDataPoint>& points);
    void removeSeries(int index);
    void removeSeries(const QString& name);
    void clearSeries();
    
    // Series configuration
    int seriesCount() const { return series_.size(); }
    ChartSeries* series(int index);
    const ChartSeries* series(int index) const;
    ChartSeries* series(const QString& name);
    const ChartSeries* series(const QString& name) const;
    
    // Chart configuration
    void setTheme(const LineChartTheme& theme);
    LineChartTheme theme() const { return theme_; }
    
    // Display options
    void setSmoothing(bool smooth);
    bool smoothing() const { return theme_.smoothCurves; }
    
    void setShowDataPoints(bool show);
    bool showDataPoints() const { return theme_.showDataPoints; }
    
    void setFillArea(bool fill);
    bool fillArea() const { return theme_.fillArea; }
    
    void setAreaOpacity(float opacity);
    float areaOpacity() const { return theme_.areaOpacity; }
    
    // Time series specific
    void setTimeSeriesMode(bool enabled);
    bool isTimeSeriesMode() const { return timeSeriesMode_; }
    
    void setTimeFormat(const QString& format);
    QString timeFormat() const { return timeFormat_; }
    
    // Auto-scrolling for real-time data
    void setAutoScroll(bool enabled);
    bool autoScroll() const { return autoScroll_; }
    
    void setMaxDataPoints(int max);
    int maxDataPoints() const { return maxDataPoints_; }
    
    // Data update
    void updateData() override;
    
    // Real-time data
    void appendDataPoint(int seriesIndex, const ChartDataPoint& point);
    void appendDataPoint(const QString& seriesName, const ChartDataPoint& point);
    
signals:
    void seriesAdded(int index);
    void seriesRemoved(int index);
    void dataPointAdded(int seriesIndex, const ChartDataPoint& point);
    
protected:
    void drawData(QPainter* painter) override;
    void drawLegend(QPainter* painter) override;
    int findNearestDataPoint(const QPointF& pos, int& seriesIndex) override;
    
private:
    // Drawing helpers
    void drawSeries(QPainter* painter, const ChartSeries& series, int seriesIndex);
    void drawSmoothLine(QPainter* painter, const std::vector<QPointF>& points, 
                       const ChartSeries& series);
    void drawStraightLine(QPainter* painter, const std::vector<QPointF>& points,
                         const ChartSeries& series);
    void drawAreaFill(QPainter* painter, const std::vector<QPointF>& points,
                     const ChartSeries& series);
    void drawDataPoints(QPainter* painter, const std::vector<QPointF>& points,
                       const ChartSeries& series, int seriesIndex);
    void drawLegendItem(QPainter* painter, const QRectF& rect, const ChartSeries& series);
    
    // Animation helpers
    std::vector<QPointF> getAnimatedPoints(const std::vector<QPointF>& targetPoints,
                                          int seriesIndex);
    
    // Data processing
    void updateAxisRanges();
    std::vector<QPointF> dataPointsToScreen(const ChartSeries& series);
    QPointF dataPointToScreen(const ChartDataPoint& point);
    
    // Hit testing
    bool isPointNearMouse(const QPointF& point, const QPointF& mousePos);
    
private:
    // Data
    std::vector<ChartSeries> series_;
    
    // Theme
    LineChartTheme theme_;
    
    // Configuration
    bool timeSeriesMode_ = false;
    QString timeFormat_ = "hh:mm:ss";
    bool autoScroll_ = false;
    int maxDataPoints_ = 1000;
    
    // Animation state
    std::vector<std::vector<QPointF>> previousPoints_;
    std::vector<float> seriesAnimationProgress_;
    
    // Legend layout
    struct LegendLayout {
        QRectF boundingRect;
        std::vector<QRectF> itemRects;
        int columns = 1;
        int rows = 1;
    } legendLayout_;
    
    // Performance optimization
    mutable std::vector<std::vector<QPointF>> cachedScreenPoints_;
    mutable bool screenPointsCacheDirty_ = true;
};

} // namespace llm_re::ui_v2::charts