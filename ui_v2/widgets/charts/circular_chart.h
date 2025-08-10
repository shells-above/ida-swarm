#pragma once

#include "../../core/ui_v2_common.h"
#include "custom_chart_base.h"
#include "chart_theme.h"

namespace llm_re::ui_v2::charts {

class CircularChart : public CustomChartBase {
    Q_OBJECT
    
public:
    explicit CircularChart(QWidget* parent = nullptr);
    ~CircularChart() override;
    
    // Chart types
    enum ChartType {
        Pie,
        Donut,
        Gauge,
        RadialBar
    };
    
    void setChartType(ChartType type);
    ChartType chartType() const { return chartType_; }
    
    // Data management
    void setData(const std::vector<ChartDataPoint>& data);
    void addDataPoint(const ChartDataPoint& point);
    void updateDataPoint(int index, const ChartDataPoint& point);
    void removeDataPoint(int index);
    void clearData() override;
    
    // Configuration
    void setTheme(const CircularChartTheme& theme);
    CircularChartTheme theme() const { return theme_; }
    
    // Donut specific
    void setInnerRadius(float ratio);
    float innerRadius() const { return theme_.innerRadiusRatio; }
    
    void setCenterText(const QString& text);
    QString centerText() const { return centerText_; }
    
    void setCenterValue(double value, const QString& suffix = "");
    
    // Display options
    void setShowLabels(bool show);
    bool showLabels() const { return theme_.showLabels; }
    
    void setShowPercentages(bool show);
    bool showPercentages() const { return theme_.showPercentages; }
    
    void setSegmentSpacing(float spacing);
    float segmentSpacing() const { return theme_.segmentSpacing; }
    
    // Gauge specific
    void setGaugeRange(double min, double max);
    void setGaugeValue(double value);
    void setGaugeThresholds(const std::vector<std::pair<double, QColor>>& thresholds);
    
    // Colors
    void setColorPalette(const std::vector<QColor>& colors);
    std::vector<QColor> colorPalette() const { return colors_; }
    
    // Animation
    void setRotationAnimation(bool enabled);
    bool rotationAnimation() const { return theme_.animateRotation; }
    
    void setStartAngle(float angle);
    float startAngle() const { return theme_.startAngle; }
    
    // Data update
    void updateData() override;
    
    // Get segment info
    int segmentAt(const QPointF& pos) const;
    double getTotal() const;
    double getPercentage(int index) const;
    
signals:
    void segmentClicked(int index);
    void segmentHovered(int index);
    void segmentSelected(int index);
    
protected:
    void drawData(QPainter* painter) override;
    void drawLegend(QPainter* painter) override;
    int findNearestDataPoint(const QPointF& pos, int& seriesIndex) override;
    
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    
private:
    // Drawing methods
    void drawPieChart(QPainter* painter);
    void drawDonutChart(QPainter* painter);
    void drawGaugeChart(QPainter* painter);
    void drawRadialBarChart(QPainter* painter);
    
    void drawSegment(QPainter* painter, const QRectF& rect, double startAngle, 
                    double spanAngle, const QColor& color, int index);
    void drawSegmentLabel(QPainter* painter, const QRectF& rect, double startAngle,
                         double spanAngle, const ChartDataPoint& data, int index);
    void drawCenterContent(QPainter* painter);
    void drawGaugeNeedle(QPainter* painter, double value);
    void drawGaugeScale(QPainter* painter);
    
    // Calculation helpers
    void calculateSegments();
    QPointF getSegmentLabelPosition(const QRectF& rect, double angle, double radius);
    QPainterPath createSegmentPath(const QRectF& rect, double startAngle, 
                                  double spanAngle, bool donut = false);
    double normalizeAngle(double angle) const;
    
    // Animation helpers
    double getAnimatedAngle(double targetAngle);
    float getSegmentScale(int index) const;
    QPointF getSegmentOffset(int index) const;
    
    // Hit testing
    bool isPointInSegment(const QPointF& point, int index) const;
    
private:
    // Data
    std::vector<ChartDataPoint> data_;
    
    // Configuration
    ChartType chartType_ = Donut;
    CircularChartTheme theme_;
    std::vector<QColor> colors_;
    
    // Center content
    QString centerText_;
    QString centerValue_;
    QString centerSuffix_;
    
    // Gauge specific
    double gaugeMin_ = 0.0;
    double gaugeMax_ = 100.0;
    double gaugeValue_ = 0.0;
    std::vector<std::pair<double, QColor>> gaugeThresholds_;
    
    // Calculated data
    struct SegmentInfo {
        double startAngle;
        double spanAngle;
        double value;
        double percentage;
        QColor color;
        QPainterPath path;
        QRectF boundingRect;
    };
    std::vector<SegmentInfo> segments_;
    double total_ = 0.0;
    
    // Interaction
    int hoveredSegment_ = -1;
    int selectedSegment_ = -1;
    std::vector<float> segmentAnimationProgress_;
    
    // Animation
    float rotationAngle_ = 0.0f;
    float previousRotationAngle_ = 0.0f;
    
    // Layout
    QRectF chartCircleRect_;
    float outerRadius_ = 0.0f;
    float innerRadius_ = 0.0f;
};

} // namespace llm_re::ui_v2::charts