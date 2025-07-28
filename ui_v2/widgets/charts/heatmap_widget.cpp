#include "../../core/ui_v2_common.h"
#include "heatmap_widget.h"

namespace llm_re::ui_v2::charts {

HeatmapWidget::HeatmapWidget(QWidget* parent)
    : CustomChartBase(parent) {
    setMinimumSize(400, 300);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Set default theme
    theme_ = HeatmapTheme();  // Use default initialization
    
    // Enable mouse tracking for hover effects
    setMouseTracking(true);
    
    // Generate default color scale
    colorScale_ = generateColorScale();
    
    // Enable interaction by default
    setAcceptDrops(false);
    setFocusPolicy(Qt::StrongFocus);
    
    // Initialize text color from theme
    textColor_ = ThemeManager::instance().colors().textPrimary;
}

HeatmapWidget::~HeatmapWidget() = default;

void HeatmapWidget::setData(const HeatmapData& data) {
    data_ = data;
    
    // Initialize animation values
    animatedValues_.resize(data.values.size());
    targetValues_.resize(data.values.size());
    
    for (size_t i = 0; i < data.values.size(); ++i) {
        animatedValues_[i].resize(data.values[i].size());
        targetValues_[i].resize(data.values[i].size());
        
        for (size_t j = 0; j < data.values[i].size(); ++j) {
            targetValues_[i][j] = data.values[i][j];
            animatedValues_[i][j] = effects_.animationEnabled ? 0.0 : data.values[i][j];
        }
    }
    
    if (autoScale_) {
        // Calculate min/max values
        double minVal = std::numeric_limits<double>::max();
        double maxVal = std::numeric_limits<double>::lowest();
        
        for (const auto& row : data.values) {
            for (double val : row) {
                minVal = std::min(minVal, val);
                maxVal = std::max(maxVal, val);
            }
        }
        
        data_.minValue = minVal;
        data_.maxValue = maxVal;
    }
    
    heatmapCacheDirty_ = true;
    calculateCellLayout();
    
    if (effects_.animationEnabled) {
        startAnimation();
    }
    
    update();
}

void HeatmapWidget::setData(const std::vector<std::vector<double>>& values) {
    HeatmapData data;
    data.values = values;
    
    // Generate default labels
    if (!values.empty()) {
        for (size_t i = 0; i < values.size(); ++i) {
            data.rowLabels.append(QString::number(i));
        }
        
        if (!values[0].empty()) {
            for (size_t j = 0; j < values[0].size(); ++j) {
                data.columnLabels.append(QString::number(j));
            }
        }
    }
    
    setData(data);
}

void HeatmapWidget::updateCell(int row, int col, double value) {
    if (row >= 0 && row < static_cast<int>(data_.values.size()) &&
        col >= 0 && col < static_cast<int>(data_.values[row].size())) {
        
        data_.values[row][col] = value;
        targetValues_[row][col] = value;
        
        if (autoScale_) {
            // Recalculate min/max
            double minVal = std::numeric_limits<double>::max();
            double maxVal = std::numeric_limits<double>::lowest();
            
            for (const auto& rowData : data_.values) {
                for (double val : rowData) {
                    minVal = std::min(minVal, val);
                    maxVal = std::max(maxVal, val);
                }
            }
            
            data_.minValue = minVal;
            data_.maxValue = maxVal;
        }
        
        heatmapCacheDirty_ = true;
        update();
    }
}

void HeatmapWidget::clearData() {
    data_.values.clear();
    data_.rowLabels.clear();
    data_.columnLabels.clear();
    data_.minValue = 0.0;
    data_.maxValue = 1.0;
    
    animatedValues_.clear();
    targetValues_.clear();
    hoveredCell_ = QPoint(-1, -1);
    selectedCell_ = QPoint(-1, -1);
    selection_ = QRect();
    
    heatmapCacheDirty_ = true;
    update();
}

void HeatmapWidget::setRowLabels(const QStringList& labels) {
    data_.rowLabels = labels;
    calculateCellLayout();
    update();
}

void HeatmapWidget::setColumnLabels(const QStringList& labels) {
    data_.columnLabels = labels;
    calculateCellLayout();
    update();
}

void HeatmapWidget::setValueRange(double min, double max) {
    data_.minValue = min;
    data_.maxValue = max;
    autoScale_ = false;
    heatmapCacheDirty_ = true;
    update();
}

void HeatmapWidget::setAutoScale(bool enabled) {
    autoScale_ = enabled;
    if (enabled && !data_.values.empty()) {
        setData(data_); // Recalculate range
    }
}

void HeatmapWidget::setTheme(const HeatmapTheme& theme) {
    theme_ = theme;
    colorScale_ = generateColorScale();
    heatmapCacheDirty_ = true;
    update();
}

void HeatmapWidget::setColorScale(HeatmapTheme::ColorScale scale) {
    theme_.colorScale = scale;
    colorScale_ = generateColorScale();
    heatmapCacheDirty_ = true;
    update();
}

void HeatmapWidget::setCustomColorScale(const std::vector<QColor>& colors) {
    colorScale_ = colors;
    heatmapCacheDirty_ = true;
    update();
}

void HeatmapWidget::setCustomColorStops(const std::vector<std::pair<double, QColor>>& stops) {
    customColorStops_ = stops;
    heatmapCacheDirty_ = true;
    update();
}

void HeatmapWidget::setCellSpacing(float spacing) {
    theme_.cellSpacing = spacing;
    calculateCellLayout();
    heatmapCacheDirty_ = true;
    update();
}

void HeatmapWidget::setCellCornerRadius(float radius) {
    theme_.cellCornerRadius = radius;
    heatmapCacheDirty_ = true;
    update();
}

void HeatmapWidget::setShowGrid(bool show) {
    theme_.showGrid = show;
    update();
}

void HeatmapWidget::setShowValues(bool show) {
    theme_.showValues = show;
    update();
}

void HeatmapWidget::setHighlightOnHover(bool enabled) {
    theme_.highlightOnHover = enabled;
    update();
}

void HeatmapWidget::setSelectionEnabled(bool enabled) {
    selectionEnabled_ = enabled;
    if (!enabled) {
        selection_ = QRect();
        update();
    }
}

void HeatmapWidget::setZoomEnabled(bool enabled) {
    zoomEnabled_ = enabled;
    if (!enabled) {
        zoomLevel_ = 1.0;
        panOffset_ = QPointF();
        calculateCellLayout();
        update();
    }
}

void HeatmapWidget::setPanEnabled(bool enabled) {
    panEnabled_ = enabled;
    if (!enabled) {
        panOffset_ = QPointF();
        calculateCellLayout();
        update();
    }
}

void HeatmapWidget::setMemoryMode(bool enabled) {
    memoryMode_ = enabled;
    heatmapCacheDirty_ = true;
    update();
}

void HeatmapWidget::setAddressRange(quint64 start, quint64 end) {
    memoryStartAddress_ = start;
    memoryEndAddress_ = end;
    update();
}

void HeatmapWidget::setBytesPerCell(int bytes) {
    bytesPerCell_ = bytes;
    update();
}

void HeatmapWidget::enableClustering(bool enabled) {
    clusteringEnabled_ = enabled;
    if (enabled) {
        performClustering();
    } else {
        clusters_.clear();
    }
    update();
}

void HeatmapWidget::setClusterThreshold(double threshold) {
    clusterThreshold_ = threshold;
    if (clusteringEnabled_) {
        performClustering();
        update();
    }
}

void HeatmapWidget::highlightCluster(int clusterIndex) {
    highlightedCluster_ = clusterIndex;
    update();
}

void HeatmapWidget::updateData() {
    calculateCellLayout();
    heatmapCacheDirty_ = true;
    CustomChartBase::updateData();
}

QPoint HeatmapWidget::cellAt(const QPointF& pos) const {
    return pixelToCell(pos);
}

double HeatmapWidget::valueAt(int row, int col) const {
    if (isValidCell(row, col)) {
        return data_.values[row][col];
    }
    return 0.0;
}

QString HeatmapWidget::labelAt(int row, int col) const {
    QString label;
    
    if (row >= 0 && row < data_.rowLabels.size()) {
        label += data_.rowLabels[row];
    }
    
    if (col >= 0 && col < data_.columnLabels.size()) {
        if (!label.isEmpty()) label += ", ";
        label += data_.columnLabels[col];
    }
    
    return label;
}

void HeatmapWidget::drawData(QPainter* painter) {
    if (data_.values.empty()) return;
    
    // Use cached heatmap if available
    if (!heatmapCacheDirty_ && !cachedHeatmap_.isNull() && !effects_.animationEnabled) {
        painter->drawPixmap(layout_.dataRect.toRect(), cachedHeatmap_);
    } else {
        drawCells(painter);
        
        // Cache the result if not animating
        if (!effects_.animationEnabled) {
            cachedHeatmap_ = QPixmap(layout_.dataRect.size().toSize());
            cachedHeatmap_.fill(Qt::transparent);
            
            QPainter cachePainter(&cachedHeatmap_);
            cachePainter.setRenderHint(QPainter::Antialiasing);
            cachePainter.translate(-layout_.dataRect.topLeft());
            drawCells(&cachePainter);
            
            heatmapCacheDirty_ = false;
        }
    }
    
    // Draw overlays
    if (selectionEnabled_ && !selection_.isNull()) {
        drawSelection(painter);
    }
    
    if (theme_.highlightOnHover && hoveredCell_.x() >= 0) {
        drawHighlight(painter);
    }
    
    if (memoryMode_) {
        drawMemoryOverlay(painter);
    }
    
    // Draw color scale
    drawColorScale(painter);
}

void HeatmapWidget::drawAxes(QPainter* painter) {
    if (!showAxes_) return;
    
    painter->save();
    
    // Draw labels
    drawRowLabels(painter);
    drawColumnLabels(painter);
    
    painter->restore();
}

int HeatmapWidget::findNearestDataPoint(const QPointF& pos, int& seriesIndex) {
    QPoint cell = cellAt(pos);
    seriesIndex = 0; // Heatmap doesn't have series
    return cell.x() >= 0 ? cell.y() * data_.values[0].size() + cell.x() : -1;
}

void HeatmapWidget::drawCells(QPainter* painter) {
    painter->save();
    painter->setClipRect(layout_.dataRect);
    
    // Apply zoom and pan transformations
    painter->translate(layout_.dataRect.topLeft() + panOffset_);
    painter->scale(zoomLevel_, zoomLevel_);
    
    int startRow = std::max(0, layout_.startRow);
    int endRow = std::min(static_cast<int>(data_.values.size()), 
                         layout_.startRow + layout_.visibleRows + 1);
    
    int startCol = std::max(0, layout_.startCol);
    int endCol = data_.values.empty() ? 0 : 
                std::min(static_cast<int>(data_.values[0].size()),
                        layout_.startCol + layout_.visibleCols + 1);
    
    for (int row = startRow; row < endRow; ++row) {
        for (int col = startCol; col < endCol; ++col) {
            if (col < static_cast<int>(data_.values[row].size())) {
                QRectF cellRect = calculateCellRect(row, col);
                
                // Translate to visible area
                cellRect.translate(-layout_.startCol * layout_.cellSize.width(),
                                 -layout_.startRow * layout_.cellSize.height());
                
                double value = effects_.animationEnabled ? 
                              animatedValues_[row][col] : data_.values[row][col];
                
                drawCell(painter, cellRect, value, row, col);
            }
        }
    }
    
    painter->restore();
}

void HeatmapWidget::drawCell(QPainter* painter, const QRectF& rect, double value, 
                            int row, int col) {
    if (rect.width() <= 0 || rect.height() <= 0) return;
    
    // Get cell color
    QColor cellColor = valueToColor(value);
    
    // Check if cell is in a highlighted cluster
    if (clusteringEnabled_ && highlightedCluster_ >= 0) {
        int cluster = findCluster(row, col);
        if (cluster != highlightedCluster_) {
            cellColor.setAlpha(100); // Fade non-highlighted clusters
        }
    }
    
    // Check if cell is selected or hovered
    bool isHovered = (hoveredCell_.x() == col && hoveredCell_.y() == row);
    bool isSelected = (selectedCell_.x() == col && selectedCell_.y() == row);
    
    if (isHovered && theme_.highlightOnHover) {
        cellColor = cellColor.lighter(120);
    }
    if (isSelected) {
        cellColor = cellColor.darker(110);
    }
    
    // Draw cell
    QPainterPath cellPath;
    if (theme_.cellCornerRadius > 0) {
        cellPath.addRoundedRect(rect.adjusted(theme_.cellSpacing/2, theme_.cellSpacing/2,
                                             -theme_.cellSpacing/2, -theme_.cellSpacing/2),
                               theme_.cellCornerRadius, theme_.cellCornerRadius);
    } else {
        cellPath.addRect(rect.adjusted(theme_.cellSpacing/2, theme_.cellSpacing/2,
                                      -theme_.cellSpacing/2, -theme_.cellSpacing/2));
    }
    
    // Apply effects
    if (effects_.glowEnabled && isHovered && theme_.highlightOnHover) {
        ChartUtils::drawGlowEffect(painter, cellPath, cellColor.lighter(150), effects_.glowRadius);
    }
    
    painter->fillPath(cellPath, cellColor);
    
    // Draw grid
    if (theme_.showGrid) {
        painter->setPen(QPen(gridColor_, gridWidth_));
        painter->drawPath(cellPath);
    }
    
    // Draw value
    if (theme_.showValues && rect.width() > 30 && rect.height() > 20) {
        drawCellValue(painter, rect, value);
    }
}

void HeatmapWidget::drawCellValue(QPainter* painter, const QRectF& rect, double value) {
    QString text = QString::number(value, 'f', valuePrecision_);
    
    QFont valueFont = font();
    valueFont.setPointSize(static_cast<int>(valueFontSize_));
    painter->setFont(valueFont);
    
    // Choose text color based on cell brightness
    QColor cellColor = valueToColor(value);
    int brightness = (cellColor.red() * 299 + cellColor.green() * 587 + cellColor.blue() * 114) / 1000;
    painter->setPen(brightness > 128 ? Qt::black : Qt::white);
    
    painter->drawText(rect, Qt::AlignCenter, text);
}

void HeatmapWidget::drawRowLabels(QPainter* painter) {
    if (data_.rowLabels.isEmpty()) return;
    
    QFont labelFont = font();
    labelFont.setPointSize(static_cast<int>(labelFontSize_));
    painter->setFont(labelFont);
    painter->setPen(textColor_);
    
    double y = layout_.dataRect.top() + layout_.cellSize.height() / 2;
    
    int startRow = layout_.startRow;
    int endRow = std::min(static_cast<int>(data_.rowLabels.size()),
                         layout_.startRow + layout_.visibleRows);
    
    for (int i = startRow; i < endRow; ++i) {
        if (i < data_.rowLabels.size()) {
            QRectF labelRect(0, y - layout_.cellSize.height() / 2,
                           layout_.labelWidth - 5, layout_.cellSize.height());
            
            painter->drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter,
                            data_.rowLabels[i]);
            
            y += layout_.cellSize.height();
        }
    }
}

void HeatmapWidget::drawColumnLabels(QPainter* painter) {
    if (data_.columnLabels.isEmpty()) return;
    
    QFont labelFont = font();
    labelFont.setPointSize(static_cast<int>(labelFontSize_));
    painter->setFont(labelFont);
    painter->setPen(textColor_);
    
    double x = layout_.dataRect.left() + layout_.cellSize.width() / 2;
    
    int startCol = layout_.startCol;
    int endCol = std::min(static_cast<int>(data_.columnLabels.size()),
                         layout_.startCol + layout_.visibleCols);
    
    for (int i = startCol; i < endCol; ++i) {
        if (i < data_.columnLabels.size()) {
            painter->save();
            painter->translate(x, layout_.dataRect.bottom() + 5);
            
            if (rotateLabels_) {
                painter->rotate(45);
                painter->drawText(QPointF(0, 0), data_.columnLabels[i]);
            } else {
                QRectF labelRect(-layout_.cellSize.width() / 2, 0,
                               layout_.cellSize.width(), layout_.labelHeight);
                painter->drawText(labelRect, Qt::AlignTop | Qt::AlignHCenter,
                                data_.columnLabels[i]);
            }
            
            painter->restore();
            x += layout_.cellSize.width();
        }
    }
}

void HeatmapWidget::drawColorScale(QPainter* painter) {
    if (!showColorScale_) return;
    
    const int scaleHeight = 200;
    const int scaleWidth = 20;
    const int margin = 10;
    
    QRectF scaleRect(width() - scaleWidth - margin * 2,
                    (height() - scaleHeight) / 2,
                    scaleWidth, scaleHeight);
    
    // Draw gradient
    QLinearGradient gradient(scaleRect.topLeft(), scaleRect.bottomLeft());
    
    if (!customColorStops_.empty()) {
        for (const auto& stop : customColorStops_) {
            gradient.setColorAt(1.0 - stop.first, stop.second);
        }
    } else {
        int numColors = colorScale_.size();
        for (int i = 0; i < numColors; ++i) {
            gradient.setColorAt(1.0 - static_cast<double>(i) / (numColors - 1), 
                               colorScale_[i]);
        }
    }
    
    painter->fillRect(scaleRect, gradient);
    painter->setPen(QPen(textColor_, 1));
    painter->drawRect(scaleRect);
    
    // Draw scale labels
    QFont labelFont = font();
    labelFont.setPointSize(static_cast<int>(labelFontSize_) - 2);
    painter->setFont(labelFont);
    
    const int numLabels = 5;
    for (int i = 0; i < numLabels; ++i) {
        double value = data_.minValue + (data_.maxValue - data_.minValue) * i / (numLabels - 1);
        double y = scaleRect.bottom() - scaleRect.height() * i / (numLabels - 1);
        
        QString label = QString::number(value, 'f', valuePrecision_);
        painter->drawText(QPointF(scaleRect.right() + 5, y + 5), label);
    }
}

void HeatmapWidget::drawSelection(QPainter* painter) {
    if (selection_.isNull()) return;
    
    painter->save();
    painter->setClipRect(layout_.dataRect);
    
    // Convert selection to pixel coordinates
    QRectF selectionRect;
    for (int row = selection_.top(); row <= selection_.bottom(); ++row) {
        for (int col = selection_.left(); col <= selection_.right(); ++col) {
            QRectF cellRect = cellToPixel(row, col);
            if (selectionRect.isNull()) {
                selectionRect = cellRect;
            } else {
                selectionRect = selectionRect.united(cellRect);
            }
        }
    }
    
    // Draw selection border
    QPen selectionPen(selectionColor_, 2);
    selectionPen.setStyle(Qt::DashLine);
    painter->setPen(selectionPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(selectionRect);
    
    // Draw selection overlay
    QColor overlayColor = selectionColor_;
    overlayColor.setAlpha(30);
    painter->fillRect(selectionRect, overlayColor);
    
    painter->restore();
}

void HeatmapWidget::drawHighlight(QPainter* painter) {
    if (hoveredCell_.x() < 0 || hoveredCell_.y() < 0) return;
    
    painter->save();
    painter->setClipRect(layout_.dataRect);
    
    // Highlight row
    if (highlightRow_) {
        QRectF rowRect = cellToPixel(hoveredCell_.y(), 0);
        rowRect.setWidth(layout_.dataRect.width());
        
        QColor highlightColor = highlightColor_;
        highlightColor.setAlpha(50);
        painter->fillRect(rowRect, highlightColor);
    }
    
    // Highlight column
    if (highlightColumn_) {
        QRectF colRect = cellToPixel(0, hoveredCell_.x());
        colRect.setHeight(layout_.dataRect.height());
        
        QColor highlightColor = highlightColor_;
        highlightColor.setAlpha(50);
        painter->fillRect(colRect, highlightColor);
    }
    
    // Highlight cell
    QRectF cellRect = cellToPixel(hoveredCell_.y(), hoveredCell_.x());
    QPen highlightPen(highlightColor_, 2);
    painter->setPen(highlightPen);
    painter->setBrush(Qt::NoBrush);
    painter->drawRect(cellRect);
    
    painter->restore();
}

void HeatmapWidget::drawMemoryOverlay(QPainter* painter) {
    if (!memoryMode_ || memoryStartAddress_ == memoryEndAddress_) return;
    
    painter->save();
    
    // Draw address labels
    QFont addressFont = font();
    addressFont.setFamily("Monospace");
    addressFont.setPointSize(static_cast<int>(labelFontSize_) - 2);
    painter->setFont(addressFont);
    painter->setPen(textColor_);
    
    // Draw start address
    QString startAddr = formatAddress(memoryStartAddress_);
    painter->drawText(QPointF(layout_.dataRect.left(), layout_.dataRect.top() - 5), startAddr);
    
    // Draw end address
    QString endAddr = formatAddress(memoryEndAddress_);
    painter->drawText(QPointF(layout_.dataRect.right() - painter->fontMetrics().horizontalAdvance(endAddr),
                             layout_.dataRect.bottom() + 15), endAddr);
    
    painter->restore();
}

QColor HeatmapWidget::valueToColor(double value) const {
    if (customColorStops_.empty()) {
        return interpolateColor(value, data_.minValue, data_.maxValue);
    } else {
        // Use custom color stops
        double normalized = (value - data_.minValue) / (data_.maxValue - data_.minValue);
        normalized = std::max(0.0, std::min(1.0, normalized));
        
        // Find the two stops to interpolate between
        for (size_t i = 1; i < customColorStops_.size(); ++i) {
            if (normalized <= customColorStops_[i].first) {
                double t = (normalized - customColorStops_[i-1].first) / 
                          (customColorStops_[i].first - customColorStops_[i-1].first);
                
                return ChartUtils::lerp(customColorStops_[i-1].second,
                                      customColorStops_[i].second, t);
            }
        }
        
        return customColorStops_.back().second;
    }
}

QColor HeatmapWidget::interpolateColor(double value, double min, double max) const {
    if (max == min) return colorScale_[0];
    
    double normalized = (value - min) / (max - min);
    normalized = std::max(0.0, std::min(1.0, normalized));
    
    int numColors = colorScale_.size();
    if (numColors == 0) return Qt::gray;
    if (numColors == 1) return colorScale_[0];
    
    double scaledValue = normalized * (numColors - 1);
    int lowerIndex = static_cast<int>(scaledValue);
    int upperIndex = std::min(lowerIndex + 1, numColors - 1);
    
    double t = scaledValue - lowerIndex;
    
    return ChartUtils::lerp(colorScale_[lowerIndex], 
                          colorScale_[upperIndex], t);
}

std::vector<QColor> HeatmapWidget::generateColorScale() const {
    std::vector<QColor> colors;
    
    switch (theme_.colorScale) {
        case HeatmapTheme::ColorScale::Viridis:
            colors = {
                QColor(68, 1, 84), QColor(72, 35, 116), QColor(64, 67, 135),
                QColor(52, 94, 141), QColor(41, 120, 142), QColor(32, 144, 140),
                QColor(34, 167, 132), QColor(68, 190, 112), QColor(121, 209, 81),
                QColor(189, 222, 38), QColor(253, 231, 36)
            };
            break;
            
        case HeatmapTheme::ColorScale::Plasma:
            colors = {
                QColor(12, 7, 134), QColor(82, 3, 252), QColor(135, 31, 251),
                QColor(178, 59, 232), QColor(212, 91, 200), QColor(237, 121, 162),
                QColor(252, 152, 122), QColor(254, 187, 90), QColor(246, 222, 73),
                QColor(239, 248, 33)
            };
            break;
            
        case HeatmapTheme::ColorScale::Inferno:
            colors = {
                QColor(0, 0, 3), QColor(20, 14, 54), QColor(58, 25, 94),
                QColor(95, 38, 116), QColor(133, 51, 124), QColor(170, 63, 122),
                QColor(206, 78, 113), QColor(237, 105, 93), QColor(251, 155, 74),
                QColor(252, 206, 37), QColor(252, 255, 164)
            };
            break;
            
        case HeatmapTheme::ColorScale::Magma:
            colors = {
                QColor(0, 0, 3), QColor(20, 13, 53), QColor(54, 24, 89),
                QColor(91, 36, 115), QColor(127, 49, 127), QColor(164, 63, 130),
                QColor(201, 79, 126), QColor(234, 107, 114), QColor(253, 155, 104),
                QColor(254, 205, 141), QColor(252, 253, 191)
            };
            break;
            
        case HeatmapTheme::ColorScale::Turbo:
            colors = {
                QColor(59, 76, 192), QColor(68, 90, 204), QColor(77, 104, 215),
                QColor(87, 117, 225), QColor(98, 130, 234), QColor(108, 142, 241),
                QColor(119, 154, 247), QColor(130, 165, 251), QColor(141, 176, 254),
                QColor(152, 185, 255), QColor(163, 194, 255), QColor(174, 201, 253),
                QColor(184, 208, 249), QColor(194, 213, 244), QColor(204, 217, 238),
                QColor(213, 219, 230), QColor(221, 221, 221)
            };
            break;
            
        case HeatmapTheme::ColorScale::RedBlue:
            colors = {
                QColor(5, 48, 97), QColor(33, 102, 172), QColor(67, 147, 195),
                QColor(146, 197, 222), QColor(209, 229, 240), QColor(247, 247, 247),
                QColor(253, 219, 199), QColor(244, 165, 130), QColor(214, 96, 77),
                QColor(178, 24, 43), QColor(103, 0, 31)
            };
            break;
            
        case HeatmapTheme::ColorScale::GreenRed:
            colors = {
                QColor(0, 104, 55), QColor(26, 152, 80), QColor(102, 189, 99),
                QColor(166, 217, 106), QColor(217, 239, 139), QColor(254, 224, 139),
                QColor(253, 174, 97), QColor(244, 109, 67), QColor(215, 48, 39),
                QColor(165, 0, 38)
            };
            break;
            
        case HeatmapTheme::ColorScale::Custom:
            // Return theme colors if available, otherwise default
            if (!theme_.customColors.empty()) {
                colors = std::vector<QColor>(theme_.customColors.begin(), 
                                           theme_.customColors.end());
            } else {
                colors = {Qt::blue, Qt::cyan, Qt::green, Qt::yellow, Qt::red};
            }
            break;
            
        default:
            // Default to Viridis
            colors = {
                QColor(68, 1, 84), QColor(72, 35, 116), QColor(64, 67, 135),
                QColor(52, 94, 141), QColor(41, 120, 142), QColor(32, 144, 140),
                QColor(34, 167, 132), QColor(68, 190, 112), QColor(121, 209, 81),
                QColor(189, 222, 38), QColor(253, 231, 36)
            };
            break;
    }
    
    return colors;
}

void HeatmapWidget::calculateCellLayout() {
    if (data_.values.empty()) return;
    
    // Calculate available space
    layout_.labelWidth = 80;
    layout_.labelHeight = 50;
    layout_.colorScaleWidth = showColorScale_ ? 60 : 0;
    
    layout_.dataRect = QRectF(
        layout_.labelWidth,
        10,
        width() - layout_.labelWidth - layout_.colorScaleWidth - 20,
        height() - layout_.labelHeight - 20
    );
    
    // Calculate cell size
    int numRows = data_.values.size();
    int numCols = data_.values.empty() ? 0 : data_.values[0].size();
    
    if (numRows > 0 && numCols > 0) {
        layout_.cellSize = QSizeF(
            layout_.dataRect.width() / numCols / zoomLevel_,
            layout_.dataRect.height() / numRows / zoomLevel_
        );
        
        // Calculate visible cells
        layout_.visibleCols = static_cast<int>(layout_.dataRect.width() / 
                                              (layout_.cellSize.width() * zoomLevel_)) + 1;
        layout_.visibleRows = static_cast<int>(layout_.dataRect.height() / 
                                              (layout_.cellSize.height() * zoomLevel_)) + 1;
        
        // Calculate start position based on pan
        layout_.startCol = std::max(0, static_cast<int>(-panOffset_.x() / 
                                                       (layout_.cellSize.width() * zoomLevel_)));
        layout_.startRow = std::max(0, static_cast<int>(-panOffset_.y() / 
                                                       (layout_.cellSize.height() * zoomLevel_)));
    }
}

QRectF HeatmapWidget::calculateCellRect(int row, int col) const {
    return QRectF(
        col * layout_.cellSize.width(),
        row * layout_.cellSize.height(),
        layout_.cellSize.width(),
        layout_.cellSize.height()
    );
}

QSizeF HeatmapWidget::calculateCellSize() const {
    return layout_.cellSize;
}

bool HeatmapWidget::isValidCell(int row, int col) const {
    return row >= 0 && row < static_cast<int>(data_.values.size()) &&
           col >= 0 && col < static_cast<int>(data_.values[row].size());
}

QPoint HeatmapWidget::pixelToCell(const QPointF& pos) const {
    if (!layout_.dataRect.contains(pos)) {
        return QPoint(-1, -1);
    }
    
    QPointF relPos = pos - layout_.dataRect.topLeft() - panOffset_;
    relPos /= zoomLevel_;
    
    int col = static_cast<int>(relPos.x() / layout_.cellSize.width()) + layout_.startCol;
    int row = static_cast<int>(relPos.y() / layout_.cellSize.height()) + layout_.startRow;
    
    if (isValidCell(row, col)) {
        return QPoint(col, row);
    }
    
    return QPoint(-1, -1);
}

QRectF HeatmapWidget::cellToPixel(int row, int col) const {
    QRectF cellRect = calculateCellRect(row - layout_.startRow, col - layout_.startCol);
    cellRect = QRectF(cellRect.topLeft() * zoomLevel_, cellRect.size() * zoomLevel_);
    cellRect.translate(layout_.dataRect.topLeft() + panOffset_);
    return cellRect;
}

void HeatmapWidget::performClustering() {
    // Simple clustering based on value similarity
    clusters_.clear();
    
    if (data_.values.empty()) return;
    
    int numRows = data_.values.size();
    int numCols = data_.values[0].size();
    
    std::vector<std::vector<bool>> visited(numRows, std::vector<bool>(numCols, false));
    
    for (int row = 0; row < numRows; ++row) {
        for (int col = 0; col < numCols; ++col) {
            if (!visited[row][col]) {
                std::vector<int> cluster;
                
                // Flood fill to find connected similar values
                std::queue<std::pair<int, int>> queue;
                queue.push({row, col});
                double baseValue = data_.values[row][col];
                
                while (!queue.empty()) {
                    auto [r, c] = queue.front();
                    queue.pop();
                    
                    if (r < 0 || r >= numRows || c < 0 || c >= numCols || visited[r][c]) {
                        continue;
                    }
                    
                    double value = data_.values[r][c];
                    if (std::abs(value - baseValue) <= clusterThreshold_) {
                        visited[r][c] = true;
                        cluster.push_back(r * numCols + c);
                        
                        // Check neighbors
                        queue.push({r - 1, c});
                        queue.push({r + 1, c});
                        queue.push({r, c - 1});
                        queue.push({r, c + 1});
                    }
                }
                
                if (!cluster.empty()) {
                    clusters_.push_back(cluster);
                }
            }
        }
    }
}

int HeatmapWidget::findCluster(int row, int col) const {
    int cellIndex = row * (data_.values.empty() ? 0 : data_.values[0].size()) + col;
    
    for (size_t i = 0; i < clusters_.size(); ++i) {
        if (std::find(clusters_[i].begin(), clusters_[i].end(), cellIndex) != clusters_[i].end()) {
            return i;
        }
    }
    
    return -1;
}

QString HeatmapWidget::formatAddress(quint64 address) const {
    return QString("0x%1").arg(address, 16, 16, QChar('0'));
}

QColor HeatmapWidget::getMemoryColor(double value, quint64 address) const {
    if (memoryMode_) {
        // Color based on byte value patterns
        if (value == 0) return QColor(50, 50, 50);      // Null bytes
        if (value == 0xFF) return QColor(255, 100, 100); // Full bytes
        if (value >= 0x20 && value <= 0x7E) return QColor(100, 255, 100); // ASCII
        
        // Default to normal color scale
    }
    
    return valueToColor(value);
}

void HeatmapWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QPoint cell = cellAt(event->pos());
        
        if (isValidCell(cell.y(), cell.x())) {
            selectedCell_ = cell;
            
            if (selectionEnabled_) {
                isSelecting_ = true;
                selectionStart_ = cell;
                selection_ = QRect(cell, cell);
            }
            
            emit cellClicked(cell.y(), cell.x());
            update();
        }
    }
    
    CustomChartBase::mousePressEvent(event);
}

void HeatmapWidget::mouseMoveEvent(QMouseEvent* event) {
    QPoint cell = cellAt(event->pos());
    
    if (cell != hoveredCell_) {
        hoveredCell_ = cell;
        
        if (isValidCell(cell.y(), cell.x())) {
            double value = data_.values[cell.y()][cell.x()];
            QString tooltip = QString("%1\nValue: %2")
                .arg(labelAt(cell.y(), cell.x()))
                .arg(value, 0, 'f', valuePrecision_);
            
            if (memoryMode_) {
                quint64 address = memoryStartAddress_ + 
                    (cell.y() * data_.values[0].size() + cell.x()) * bytesPerCell_;
                tooltip += QString("\nAddress: %1").arg(formatAddress(address));
            }
            
            QToolTip::showText(event->globalPos(), tooltip, this);
            emit cellHovered(cell.y(), cell.x());
        } else {
            QToolTip::hideText();
        }
        
        update();
    }
    
    // Handle selection dragging
    if (isSelecting_ && selectionEnabled_) {
        if (isValidCell(cell.y(), cell.x())) {
            selection_ = QRect(
                QPoint(std::min(selectionStart_.x(), cell.x()),
                       std::min(selectionStart_.y(), cell.y())),
                QPoint(std::max(selectionStart_.x(), cell.x()),
                       std::max(selectionStart_.y(), cell.y()))
            );
            update();
        }
    }
    
    // Handle panning
    if (panEnabled_ && event->buttons() & Qt::MiddleButton) {
        QPointF delta = event->pos() - QPointF(event->pos());
        panOffset_ += delta;
        calculateCellLayout();
        update();
    }
    
    CustomChartBase::mouseMoveEvent(event);
}

void HeatmapWidget::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && isSelecting_) {
        isSelecting_ = false;
        emit selectionChanged(selection_);
    }
    
    CustomChartBase::mouseReleaseEvent(event);
}

void HeatmapWidget::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        QPoint cell = cellAt(event->pos());
        if (isValidCell(cell.y(), cell.x())) {
            emit cellDoubleClicked(cell.y(), cell.x());
        }
    }
    
    CustomChartBase::mouseDoubleClickEvent(event);
}

void HeatmapWidget::wheelEvent(QWheelEvent* event) {
    if (zoomEnabled_) {
        double scaleFactor = 1.15;
        if (event->angleDelta().y() > 0) {
            zoomLevel_ *= scaleFactor;
        } else {
            zoomLevel_ /= scaleFactor;
        }
        
        // Limit zoom range
        zoomLevel_ = std::max(0.1, std::min(10.0, zoomLevel_));
        
        calculateCellLayout();
        heatmapCacheDirty_ = true;
        update();
    }
    
    CustomChartBase::wheelEvent(event);
}

} // namespace llm_re::ui_v2::charts