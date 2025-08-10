#pragma once

#include "../../core/ui_v2_common.h"
#include "custom_chart_base.h"
#include "chart_theme.h"

namespace llm_re::ui_v2::charts {

class HeatmapWidget : public CustomChartBase {
    Q_OBJECT
    
public:
    explicit HeatmapWidget(QWidget* parent = nullptr);
    ~HeatmapWidget() override;
    
    // Data structure for heatmap
    struct HeatmapData {
        std::vector<std::vector<double>> values;
        QStringList rowLabels;
        QStringList columnLabels;
        double minValue = 0.0;
        double maxValue = 1.0;
    };
    
    // Data management
    void setData(const HeatmapData& data);
    void setData(const std::vector<std::vector<double>>& values);
    void updateCell(int row, int col, double value);
    void clearData() override;
    
    // Labels
    void setRowLabels(const QStringList& labels);
    void setColumnLabels(const QStringList& labels);
    QStringList rowLabels() const { return data_.rowLabels; }
    QStringList columnLabels() const { return data_.columnLabels; }
    
    // Value range
    void setValueRange(double min, double max);
    void setAutoScale(bool enabled);
    bool autoScale() const { return autoScale_; }
    
    // Theme and appearance
    void setTheme(const HeatmapTheme& theme);
    HeatmapTheme theme() const { return theme_; }
    
    // Color scale
    void setColorScale(HeatmapTheme::ColorScale scale);
    HeatmapTheme::ColorScale colorScale() const { return theme_.colorScale; }
    
    void setCustomColorScale(const std::vector<QColor>& colors);
    void setCustomColorStops(const std::vector<std::pair<double, QColor>>& stops);
    
    // Cell appearance
    void setCellSpacing(float spacing);
    float cellSpacing() const { return theme_.cellSpacing; }
    
    void setCellCornerRadius(float radius);
    float cellCornerRadius() const { return theme_.cellCornerRadius; }
    
    void setShowGrid(bool show);
    bool showGrid() const { return theme_.showGrid; }
    
    void setShowValues(bool show);
    bool showValues() const { return theme_.showValues; }
    
    // Interaction
    void setHighlightOnHover(bool enabled);
    bool highlightOnHover() const { return theme_.highlightOnHover; }
    
    void setSelectionEnabled(bool enabled);
    bool selectionEnabled() const { return selectionEnabled_; }
    
    // Zoom and pan
    void setZoomEnabled(bool enabled);
    bool zoomEnabled() const { return zoomEnabled_; }
    
    void setPanEnabled(bool enabled);
    bool panEnabled() const { return panEnabled_; }
    
    // Memory visualization specific
    void setMemoryMode(bool enabled);
    bool memoryMode() const { return memoryMode_; }
    
    void setAddressRange(quint64 start, quint64 end);
    void setBytesPerCell(int bytes);
    int bytesPerCell() const { return bytesPerCell_; }
    
    // Clustering and grouping
    void enableClustering(bool enabled);
    void setClusterThreshold(double threshold);
    void highlightCluster(int clusterIndex);
    
    // Data update
    void updateData() override;
    
    // Get cell info
    QPoint cellAt(const QPointF& pos) const;
    double valueAt(int row, int col) const;
    QString labelAt(int row, int col) const;
    
signals:
    void cellClicked(int row, int col);
    void cellHovered(int row, int col);
    void cellSelected(int row, int col);
    void selectionChanged(const QRect& selection);
    void cellDoubleClicked(int row, int col);
    
protected:
    void drawData(QPainter* painter) override;
    void drawAxes(QPainter* painter) override;
    int findNearestDataPoint(const QPointF& pos, int& seriesIndex) override;
    
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    
private:
    // Drawing methods
    void drawCells(QPainter* painter);
    void drawCell(QPainter* painter, const QRectF& rect, double value, 
                 int row, int col);
    void drawCellValue(QPainter* painter, const QRectF& rect, double value);
    void drawRowLabels(QPainter* painter);
    void drawColumnLabels(QPainter* painter);
    void drawColorScale(QPainter* painter);
    void drawSelection(QPainter* painter) override;
    void drawHighlight(QPainter* painter);
    void drawMemoryOverlay(QPainter* painter);
    
    // Color calculation
    QColor valueToColor(double value) const;
    QColor interpolateColor(double value, double min, double max) const;
    std::vector<QColor> generateColorScale() const;
    
    // Layout calculation
    void calculateCellLayout();
    QRectF calculateCellRect(int row, int col) const;
    QSizeF calculateCellSize() const;
    
    // Hit testing
    bool isValidCell(int row, int col) const;
    QPoint pixelToCell(const QPointF& pos) const;
    QRectF cellToPixel(int row, int col) const;
    
    // Clustering
    void performClustering();
    int findCluster(int row, int col) const;
    
    // Memory mode helpers
    QString formatAddress(quint64 address) const;
    QColor getMemoryColor(double value, quint64 address) const;
    
private:
    // Data
    HeatmapData data_;
    bool autoScale_ = true;
    
    // Theme
    HeatmapTheme theme_;
    std::vector<QColor> colorScale_;
    std::vector<std::pair<double, QColor>> customColorStops_;
    
    // Layout
    struct CellLayout {
        QRectF dataRect;
        QSizeF cellSize;
        float labelWidth = 80.0f;
        float labelHeight = 30.0f;
        float colorScaleWidth = 30.0f;
        int visibleRows = 0;
        int visibleCols = 0;
        int startRow = 0;
        int startCol = 0;
    } layout_;
    
    // Interaction
    QPoint hoveredCell_{-1, -1};
    QPoint selectedCell_{-1, -1};
    QRect selection_;
    bool selectionEnabled_ = true;
    bool isSelecting_ = false;
    QPoint selectionStart_;
    
    // Zoom and pan
    bool zoomEnabled_ = true;
    bool panEnabled_ = true;
    double zoomLevel_ = 1.0;
    QPointF panOffset_;
    
    // Memory mode
    bool memoryMode_ = false;
    quint64 memoryStartAddress_ = 0;
    quint64 memoryEndAddress_ = 0;
    int bytesPerCell_ = 1;
    
    // Clustering
    bool clusteringEnabled_ = false;
    double clusterThreshold_ = 0.1;
    std::vector<std::vector<int>> clusters_;
    int highlightedCluster_ = -1;
    
    // Performance optimization
    mutable QPixmap cachedHeatmap_;
    mutable bool heatmapCacheDirty_ = true;
    
    // Animation
    std::vector<std::vector<double>> animatedValues_;
    std::vector<std::vector<double>> targetValues_;
    
    // Additional display properties not in HeatmapTheme
    bool showAxes_ = true;
    QColor gridColor_;  // Initialized from theme in constructor
    float gridWidth_ = 1.0f;
    int valuePrecision_ = 2;
    float valueFontSize_ = 10.0f;
    float labelFontSize_ = 10.0f;
    QColor textColor_;  // Will be set from ThemeManager
    bool rotateLabels_ = false;
    bool showColorScale_ = true;
    QColor selectionColor_;  // Initialized from theme in constructor
    bool highlightRow_ = false;
    bool highlightColumn_ = false;
    QColor highlightColor_;  // Initialized from theme in constructor
};

} // namespace llm_re::ui_v2::charts