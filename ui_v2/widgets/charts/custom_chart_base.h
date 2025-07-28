#pragma once

#include "../../core/base_styled_widget.h"
#include "chart_types.h"
#include <QPropertyAnimation>
#include <QTimer>
#include <memory>

namespace llm_re::ui_v2::charts {

class CustomChartBase : public BaseStyledWidget {
    Q_OBJECT
    Q_PROPERTY(float animationProgress READ animationProgress WRITE setAnimationProgress)
    
public:
    explicit CustomChartBase(QWidget* parent = nullptr);
    ~CustomChartBase() override;
    
    // Chart configuration
    void setTitle(const QString& title);
    QString title() const { return title_; }
    
    void setSubtitle(const QString& subtitle);
    QString subtitle() const { return subtitle_; }
    
    // Margins
    void setMargins(const ChartMargins& margins);
    ChartMargins margins() const { return margins_; }
    
    // Axes
    void setXAxisConfig(const AxisConfig& config);
    AxisConfig xAxisConfig() const { return xAxis_; }
    
    void setYAxisConfig(const AxisConfig& config);
    AxisConfig yAxisConfig() const { return yAxis_; }
    
    // Legend
    void setLegendConfig(const LegendConfig& config);
    LegendConfig legendConfig() const { return legend_; }
    
    // Tooltip
    void setTooltipConfig(const TooltipConfig& config);
    TooltipConfig tooltipConfig() const { return tooltip_; }
    
    // Effects
    void setEffectsConfig(const EffectsConfig& config);
    EffectsConfig effectsConfig() const { return effects_; }
    
    // Animation
    float animationProgress() const { return animationState_.progress; }
    void setAnimationProgress(float progress);
    void startAnimation();
    void stopAnimation();
    bool isAnimating() const { return animationState_.isAnimating; }
    
    // Data update
    virtual void updateData() = 0;
    virtual void clearData();
    
    // Export
    QPixmap toPixmap(const QSize& size = QSize());
    bool saveToFile(const QString& filename, const QSize& size = QSize());
    
signals:
    void chartClicked(const QPointF& point);
    void dataPointHovered(int seriesIndex, int pointIndex);
    void dataPointClicked(int seriesIndex, int pointIndex);
    void selectionChanged(const QRectF& selection);
    void animationFinished();
    
public slots:
    void refresh();
    void resetView();
    void zoomIn();
    void zoomOut();
    void fitToView();
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    
    // Drawing methods (to be implemented by derived classes)
    virtual void drawBackground(QPainter* painter);
    virtual void drawAxes(QPainter* painter);
    virtual void drawGrid(QPainter* painter);
    virtual void drawData(QPainter* painter) = 0;
    virtual void drawLegend(QPainter* painter);
    virtual void drawTitle(QPainter* painter);
    virtual void drawTooltip(QPainter* painter);
    virtual void drawSelection(QPainter* painter);
    
    // Helper methods
    void calculateChartRect();
    void updateAxesRange();
    QPointF mapToChart(const QPointF& point) const;
    QPointF mapFromChart(const QPointF& chartPoint) const;
    
    // Hit testing (to be implemented by derived classes)
    virtual int findNearestDataPoint(const QPointF& pos, int& seriesIndex) = 0;
    
    // Animation helpers
    void updateAnimation();
    float getAnimatedValue(float from, float to) const;
    QPointF getAnimatedPoint(const QPointF& from, const QPointF& to) const;
    QColor getAnimatedColor(const QColor& from, const QColor& to) const;
    
    // Drawing helpers
    void drawGlowingLine(QPainter* painter, const QPointF& start, const QPointF& end, 
                        const QColor& color, float width, float glowRadius);
    void drawGlowingPoint(QPainter* painter, const QPointF& center, float radius,
                         const QColor& color, float glowRadius);
    void drawGlassRect(QPainter* painter, const QRectF& rect, const QColor& color,
                      float opacity, float blurRadius);
    
protected:
    // Chart properties
    QString title_;
    QString subtitle_;
    ChartMargins margins_;
    QRectF chartRect_;
    
    // Axes
    AxisConfig xAxis_;
    AxisConfig yAxis_;
    
    // Visual configuration
    LegendConfig legend_;
    TooltipConfig tooltip_;
    EffectsConfig effects_;
    
    // Interaction state
    InteractionState interaction_;
    
    // Animation
    AnimationState animationState_;
    QPropertyAnimation* animation_ = nullptr;
    
    // Tooltip
    QString tooltipText_;
    QPointF tooltipPos_;
    bool showTooltip_ = false;
    QTimer* tooltipTimer_ = nullptr;
    
    // Zoom and pan
    double zoomFactor_ = 1.0;
    QPointF panOffset_;
    
    // Cache for performance
    mutable QPixmap cachedBackground_;
    mutable bool backgroundCacheDirty_ = true;
    
private:
    void initializeAnimation();
    void updateTooltip(const QPointF& pos);
    void hideTooltip();
};

} // namespace llm_re::ui_v2::charts