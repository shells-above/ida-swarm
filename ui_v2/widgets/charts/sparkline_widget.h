#pragma once

#include "custom_chart_base.h"
#include "chart_theme.h"
#include <deque>
#include <memory>

namespace llm_re::ui_v2::charts {

class SparklineWidget : public CustomChartBase {
    Q_OBJECT
    
public:
    explicit SparklineWidget(QWidget* parent = nullptr);
    ~SparklineWidget() override;
    
    // Sparkline types
    enum SparklineType {
        Line,
        Area,
        Bar,
        WinLoss,
        Discrete,
        Bullet
    };
    
    void setSparklineType(SparklineType type);
    SparklineType sparklineType() const { return sparklineType_; }
    
    // Data management
    void setData(const std::vector<double>& values);
    void appendValue(double value);
    void prependValue(double value);
    void clearData();
    
    // Real-time data
    void setMaxDataPoints(int max);
    int maxDataPoints() const { return maxDataPoints_; }
    
    void setRollingWindow(bool enabled);
    bool rollingWindow() const { return rollingWindow_; }
    
    // Theme
    void setTheme(const SparklineTheme& theme);
    SparklineTheme theme() const { return theme_; }
    
    // Display options
    void setLineWidth(float width);
    float lineWidth() const { return theme_.lineWidth; }
    
    void setFillArea(bool fill);
    bool fillArea() const { return theme_.fillArea; }
    
    void setAreaOpacity(float opacity);
    float areaOpacity() const { return theme_.areaOpacity; }
    
    void setShowMinMax(bool show);
    bool showMinMax() const { return theme_.showMinMax; }
    
    void setShowLastValue(bool show);
    bool showLastValue() const { return theme_.showLastValue; }
    
    void setShowThresholds(bool show);
    bool showThresholds() const { return showThresholds_; }
    
    // Value range
    void setAutoScale(bool enabled);
    bool autoScale() const { return autoScale_; }
    
    void setValueRange(double min, double max);
    void getValueRange(double& min, double& max) const;
    
    // Thresholds and bands
    void addThreshold(double value, const QColor& color, const QString& label = QString());
    void removeThreshold(double value);
    void clearThresholds();
    
    void addBand(double min, double max, const QColor& color, const QString& label = QString());
    void removeBand(int index);
    void clearBands();
    
    // Reference line
    void setReferenceLine(double value);
    void clearReferenceLine();
    bool hasReferenceLine() const { return hasReferenceLine_; }
    
    // Bullet chart specific
    void setBulletTarget(double target);
    void setBulletPerformance(double performance);
    void setBulletRanges(const std::vector<std::pair<double, QColor>>& ranges);
    
    // Colors
    void setLineColor(const QColor& color);
    QColor lineColor() const { return lineColor_; }
    
    void setFillColor(const QColor& color);
    QColor fillColor() const { return fillColor_; }
    
    void setPositiveColor(const QColor& color);
    QColor positiveColor() const { return positiveColor_; }
    
    void setNegativeColor(const QColor& color);
    QColor negativeColor() const { return negativeColor_; }
    
    // Animation
    void setAnimateOnUpdate(bool animate);
    bool animateOnUpdate() const { return theme_.animateOnUpdate; }
    
    // Size hint
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;
    
    // Data update
    void updateData() override;
    
    // Statistics
    double minimum() const;
    double maximum() const;
    double average() const;
    double lastValue() const;
    int dataPointCount() const { return data_.size(); }
    
signals:
    void valueAdded(double value);
    void dataChanged();
    void clicked(const QPointF& position);
    
protected:
    void drawData(QPainter* painter) override;
    int findNearestDataPoint(const QPointF& pos, int& seriesIndex) override;
    
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    
private:
    // Drawing methods
    void drawLineSparkline(QPainter* painter);
    void drawAreaSparkline(QPainter* painter);
    void drawBarSparkline(QPainter* painter);
    void drawWinLossSparkline(QPainter* painter);
    void drawDiscreteSparkline(QPainter* painter);
    void drawBulletChart(QPainter* painter);
    
    void drawMinMaxMarkers(QPainter* painter);
    void drawLastValueLabel(QPainter* painter);
    void drawThresholds(QPainter* painter);
    void drawBands(QPainter* painter);
    void drawReferenceLine(QPainter* painter);
    
    // Calculation helpers
    void updateValueRange();
    std::vector<QPointF> calculatePoints() const;
    QPointF valueToPoint(int index, double value) const;
    double normalizeValue(double value) const;
    
    // Animation helpers
    std::vector<double> getAnimatedValues() const;
    
private:
    // Data
    std::deque<double> data_;
    int maxDataPoints_ = 100;
    bool rollingWindow_ = true;
    
    // Configuration
    SparklineType sparklineType_ = Line;
    SparklineTheme theme_;
    
    // Value range
    bool autoScale_ = true;
    double minValue_ = 0.0;
    double maxValue_ = 1.0;
    double calculatedMin_ = 0.0;
    double calculatedMax_ = 1.0;
    
    // Colors
    QColor lineColor_;
    QColor fillColor_;
    QColor positiveColor_;
    QColor negativeColor_;
    
    // Thresholds and bands
    struct Threshold {
        double value;
        QColor color;
        QString label;
    };
    std::vector<Threshold> thresholds_;
    bool showThresholds_ = false;
    
    struct Band {
        double min;
        double max;
        QColor color;
        QString label;
    };
    std::vector<Band> bands_;
    
    // Reference line
    bool hasReferenceLine_ = false;
    double referenceLineValue_ = 0.0;
    
    // Bullet chart
    double bulletTarget_ = 0.0;
    double bulletPerformance_ = 0.0;
    std::vector<std::pair<double, QColor>> bulletRanges_;
    
    // Animation
    std::deque<double> previousData_;
    float dataAnimationProgress_ = 1.0f;
    
    // Min/Max tracking
    int minIndex_ = -1;
    int maxIndex_ = -1;
    
    // Optimization
    mutable bool needsRecalculation_ = true;
};

// Inline sparkline for embedding in other widgets
class InlineSparkline : public SparklineWidget {
    Q_OBJECT
    
public:
    explicit InlineSparkline(QWidget* parent = nullptr);
    
    // Simplified interface for inline usage
    void setCompactMode(bool compact);
    bool compactMode() const { return compactMode_; }
    
    // Quick setup methods
    void setupAsMetric(const QString& label, const QString& suffix = QString());
    void setupAsProgress(double min, double max, double target);
    void setupAsTrend(int dataPoints = 20);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    bool compactMode_ = true;
    QString label_;
    QString suffix_;
};

} // namespace llm_re::ui_v2::charts