#include "bar_chart.h"
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QToolTip>
#include <cmath>
#include <algorithm>

namespace llm_re::ui_v2::charts {

BarChart::BarChart(QWidget* parent)
    : CustomChartBase(parent) {
    setMinimumSize(300, 200);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    
    // Set default theme
    theme_ = ChartTheme::getBarChartTheme(ChartTheme::Style::Modern);
    
    // Enable mouse tracking for hover effects
    setMouseTracking(true);
}

BarChart::~BarChart() = default;

void BarChart::setChartType(ChartType type) {
    if (chartType_ != type) {
        chartType_ = type;
        theme_.horizontal = (type == Horizontal);
        animatedHeights_.clear();
        targetHeights_.clear();
        calculateBarLayout();
        update();
    }
}

void BarChart::setCategories(const QStringList& categories) {
    categories_ = categories;
    dataMap_.clear();
    animatedHeights_.clear();
    targetHeights_.clear();
    calculateBarLayout();
    update();
}

void BarChart::addSeries(const ChartSeries& series) {
    series_.push_back(series);
    
    // Initialize animation state
    animatedHeights_.resize(categories_.size());
    targetHeights_.resize(categories_.size());
    for (int i = 0; i < categories_.size(); ++i) {
        animatedHeights_[i].resize(series_.size(), 0.0);
        targetHeights_[i].resize(series_.size(), 0.0);
    }
    
    calculateBarLayout();
    update();
}

void BarChart::addSeries(const QString& name, const std::vector<double>& values) {
    ChartSeries series(name);
    for (size_t i = 0; i < values.size() && i < static_cast<size_t>(categories_.size()); ++i) {
        series.addDataPoint(i, values[i], categories_[i]);
        setData(categories_[i], name, values[i]);
    }
    addSeries(series);
}

void BarChart::updateSeries(int index, const ChartSeries& series) {
    if (index >= 0 && index < static_cast<int>(series_.size())) {
        series_[index] = series;
        calculateBarLayout();
        update();
    }
}

void BarChart::removeSeries(int index) {
    if (index >= 0 && index < static_cast<int>(series_.size())) {
        series_.erase(series_.begin() + index);
        animatedHeights_.clear();
        targetHeights_.clear();
        calculateBarLayout();
        update();
    }
}

void BarChart::clearSeries() {
    series_.clear();
    dataMap_.clear();
    animatedHeights_.clear();
    targetHeights_.clear();
    calculateBarLayout();
    update();
}

void BarChart::setData(const QString& category, const QString& series, double value) {
    dataMap_[{category, series}] = value;
    
    // Update target heights for animation
    int categoryIdx = categories_.indexOf(category);
    if (categoryIdx >= 0) {
        for (size_t seriesIdx = 0; seriesIdx < series_.size(); ++seriesIdx) {
            if (series_[seriesIdx].name == series) {
                if (static_cast<size_t>(categoryIdx) < targetHeights_.size() &&
                    seriesIdx < targetHeights_[categoryIdx].size()) {
                    targetHeights_[categoryIdx][seriesIdx] = value;
                }
                break;
            }
        }
    }
    
    if (isAnimationEnabled()) {
        startAnimation();
    } else {
        animatedHeights_ = targetHeights_;
    }
    
    update();
}

double BarChart::getData(const QString& category, const QString& series) const {
    auto it = dataMap_.find({category, series});
    return it != dataMap_.end() ? it->second : 0.0;
}

void BarChart::setTheme(const BarChartTheme& theme) {
    theme_ = theme;
    update();
}

void BarChart::setBarSpacing(float spacing) {
    theme_.barSpacing = spacing;
    calculateBarLayout();
    update();
}

void BarChart::setCornerRadius(float radius) {
    theme_.cornerRadius = radius;
    update();
}

void BarChart::setShowValues(bool show) {
    theme_.showValues = show;
    update();
}

void BarChart::setHorizontal(bool horizontal) {
    theme_.horizontal = horizontal;
    if (horizontal) {
        chartType_ = Horizontal;
    } else if (chartType_ == Horizontal) {
        chartType_ = Vertical;
    }
    calculateBarLayout();
    update();
}

void BarChart::setGradient(bool enabled) {
    theme_.gradient = enabled;
    update();
}

void BarChart::setStacked(bool stacked) {
    chartType_ = stacked ? Stacked : Vertical;
    calculateBarLayout();
    update();
}

void BarChart::setValueFormat(const QString& format) {
    valueFormat_ = format;
    update();
}

void BarChart::setValuePrefix(const QString& prefix) {
    valuePrefix_ = prefix;
    update();
}

void BarChart::setValueSuffix(const QString& suffix) {
    valueSuffix_ = suffix;
    update();
}

void BarChart::setGrowthAnimation(bool enabled) {
    theme_.animateGrowth = enabled;
}

void BarChart::updateData() {
    calculateBarLayout();
    CustomChartBase::updateData();
}

int BarChart::barAt(const QPointF& pos) const {
    for (const auto& categoryBars : layout_.bars) {
        for (const auto& bar : categoryBars) {
            if (isPointInBar(pos, bar)) {
                return bar.categoryIndex;
            }
        }
    }
    return -1;
}

QString BarChart::categoryAt(const QPointF& pos) const {
    int idx = barAt(pos);
    return (idx >= 0 && idx < categories_.size()) ? categories_[idx] : QString();
}

int BarChart::seriesAt(const QPointF& pos) const {
    for (const auto& categoryBars : layout_.bars) {
        for (const auto& bar : categoryBars) {
            if (isPointInBar(pos, bar)) {
                return bar.seriesIndex;
            }
        }
    }
    return -1;
}

void BarChart::drawData(QPainter* painter) {
    switch (chartType_) {
        case Vertical:
            drawVerticalBars(painter);
            break;
        case Horizontal:
            drawHorizontalBars(painter);
            break;
        case Grouped:
            drawGroupedBars(painter);
            break;
        case Stacked:
            drawStackedBars(painter);
            break;
        case Waterfall:
            drawWaterfallChart(painter);
            break;
        case Range:
            drawRangeChart(painter);
            break;
    }
}

void BarChart::drawLegend(QPainter* painter) {
    if (!theme_.showLegend || series_.empty()) return;
    
    const int legendItemHeight = 20;
    const int legendItemSpacing = 5;
    const int colorBoxSize = 12;
    const int textOffset = 20;
    
    QPointF legendPos(chartRect().right() + 20, chartRect().top());
    
    for (size_t i = 0; i < series_.size(); ++i) {
        if (!series_[i].visible) continue;
        
        // Color box
        QRectF colorBox(legendPos.x(), legendPos.y() + 4, colorBoxSize, colorBoxSize);
        QColor color = series_[i].color.isValid() ? series_[i].color : theme_.colors[i % theme_.colors.size()];
        
        if (theme_.gradient) {
            QLinearGradient gradient(colorBox.topLeft(), colorBox.bottomRight());
            gradient.setColorAt(0, color.lighter(120));
            gradient.setColorAt(1, color);
            painter->fillRect(colorBox, gradient);
        } else {
            painter->fillRect(colorBox, color);
        }
        
        // Text
        painter->setPen(theme_.textColor);
        painter->drawText(QPointF(legendPos.x() + textOffset, legendPos.y() + 14), series_[i].name);
        
        legendPos.setY(legendPos.y() + legendItemHeight + legendItemSpacing);
    }
}

void BarChart::drawAxes(QPainter* painter) {
    if (!theme_.showAxes) return;
    
    painter->setPen(QPen(theme_.axisColor, 1));
    
    // Draw axes lines
    painter->drawLine(chartRect().bottomLeft(), chartRect().bottomRight());
    painter->drawLine(chartRect().bottomLeft(), chartRect().topLeft());
    
    // Draw category labels
    if (!categories_.empty()) {
        QFont labelFont = font();
        labelFont.setPointSize(theme_.labelFontSize);
        painter->setFont(labelFont);
        
        if (theme_.horizontal) {
            // Draw on Y axis
            double categoryHeight = chartRect().height() / categories_.size();
            for (int i = 0; i < categories_.size(); ++i) {
                QPointF pos(chartRect().left() - 10, 
                           chartRect().bottom() - (i + 0.5) * categoryHeight);
                painter->drawText(pos - QPointF(painter->fontMetrics().horizontalAdvance(categories_[i]), -5), 
                                categories_[i]);
            }
        } else {
            // Draw on X axis
            double categoryWidth = chartRect().width() / categories_.size();
            for (int i = 0; i < categories_.size(); ++i) {
                QPointF pos(chartRect().left() + (i + 0.5) * categoryWidth, 
                           chartRect().bottom() + 20);
                
                if (theme_.rotateLabels) {
                    painter->save();
                    painter->translate(pos);
                    painter->rotate(-45);
                    painter->drawText(QPointF(0, 0), categories_[i]);
                    painter->restore();
                } else {
                    painter->drawText(pos - QPointF(painter->fontMetrics().horizontalAdvance(categories_[i])/2, 0), 
                                    categories_[i]);
                }
            }
        }
    }
    
    // Draw value axis labels
    double minVal = 0, maxVal = 0;
    for (const auto& series : series_) {
        for (const auto& point : series.dataPoints) {
            maxVal = std::max(maxVal, point.y);
            minVal = std::min(minVal, point.y);
        }
    }
    
    const int numTicks = 5;
    for (int i = 0; i <= numTicks; ++i) {
        double value = minVal + (maxVal - minVal) * i / numTicks;
        QString label = QString(valueFormat_).arg(value);
        if (!valuePrefix_.isEmpty()) label.prepend(valuePrefix_);
        if (!valueSuffix_.isEmpty()) label.append(valueSuffix_);
        
        if (theme_.horizontal) {
            double x = chartRect().left() + chartRect().width() * i / numTicks;
            painter->drawText(QPointF(x - painter->fontMetrics().horizontalAdvance(label)/2, 
                                    chartRect().bottom() + 20), label);
        } else {
            double y = chartRect().bottom() - chartRect().height() * i / numTicks;
            painter->drawText(QPointF(chartRect().left() - painter->fontMetrics().horizontalAdvance(label) - 10, 
                                    y + 5), label);
        }
    }
}

int BarChart::findNearestDataPoint(const QPointF& pos, int& seriesIndex) {
    int categoryIdx = barAt(pos);
    seriesIndex = seriesAt(pos);
    return categoryIdx;
}

void BarChart::drawVerticalBars(QPainter* painter) {
    if (categories_.empty() || series_.empty()) return;
    
    const double categoryWidth = chartRect().width() / categories_.size();
    const double barGroupWidth = categoryWidth * (1.0 - theme_.barSpacing);
    const double barWidth = barGroupWidth / series_.size();
    
    for (int catIdx = 0; catIdx < categories_.size(); ++catIdx) {
        double groupX = chartRect().left() + catIdx * categoryWidth + 
                       categoryWidth * theme_.barSpacing / 2;
        
        for (size_t seriesIdx = 0; seriesIdx < series_.size(); ++seriesIdx) {
            if (!series_[seriesIdx].visible) continue;
            
            double value = getData(categories_[catIdx], series_[seriesIdx].name);
            double animatedValue = getAnimatedHeight(value, catIdx, seriesIdx);
            double barHeight = calculateBarHeight(animatedValue);
            
            QRectF barRect(groupX + seriesIdx * barWidth, 
                          chartRect().bottom() - barHeight,
                          barWidth * 0.8,
                          barHeight);
            
            QColor color = getBarColor(seriesIdx, catIdx);
            drawBar(painter, barRect, animatedValue, color, catIdx, seriesIdx);
        }
    }
}

void BarChart::drawHorizontalBars(QPainter* painter) {
    if (categories_.empty() || series_.empty()) return;
    
    const double categoryHeight = chartRect().height() / categories_.size();
    const double barGroupHeight = categoryHeight * (1.0 - theme_.barSpacing);
    const double barHeight = barGroupHeight / series_.size();
    
    for (int catIdx = 0; catIdx < categories_.size(); ++catIdx) {
        double groupY = chartRect().top() + catIdx * categoryHeight + 
                       categoryHeight * theme_.barSpacing / 2;
        
        for (size_t seriesIdx = 0; seriesIdx < series_.size(); ++seriesIdx) {
            if (!series_[seriesIdx].visible) continue;
            
            double value = getData(categories_[catIdx], series_[seriesIdx].name);
            double animatedValue = getAnimatedHeight(value, catIdx, seriesIdx);
            double barWidth = calculateBarHeight(animatedValue);
            
            QRectF barRect(chartRect().left(), 
                          groupY + seriesIdx * barHeight,
                          barWidth,
                          barHeight * 0.8);
            
            QColor color = getBarColor(seriesIdx, catIdx);
            drawBar(painter, barRect, animatedValue, color, catIdx, seriesIdx);
        }
    }
}

void BarChart::drawGroupedBars(QPainter* painter) {
    // Grouped bars are the same as vertical bars
    drawVerticalBars(painter);
}

void BarChart::drawStackedBars(QPainter* painter) {
    if (categories_.empty() || series_.empty()) return;
    
    const double categoryWidth = chartRect().width() / categories_.size();
    const double barWidth = categoryWidth * (1.0 - theme_.barSpacing);
    
    for (int catIdx = 0; catIdx < categories_.size(); ++catIdx) {
        double barX = chartRect().left() + catIdx * categoryWidth + 
                     categoryWidth * theme_.barSpacing / 2;
        double currentY = chartRect().bottom();
        
        for (size_t seriesIdx = 0; seriesIdx < series_.size(); ++seriesIdx) {
            if (!series_[seriesIdx].visible) continue;
            
            double value = getData(categories_[catIdx], series_[seriesIdx].name);
            double animatedValue = getAnimatedHeight(value, catIdx, seriesIdx);
            double segmentHeight = calculateBarHeight(animatedValue);
            
            QRectF barRect(barX, currentY - segmentHeight, barWidth, segmentHeight);
            
            QColor color = getBarColor(seriesIdx, catIdx);
            drawBar(painter, barRect, animatedValue, color, catIdx, seriesIdx);
            
            currentY -= segmentHeight;
        }
    }
}

void BarChart::drawWaterfallChart(QPainter* painter) {
    if (categories_.empty() || series_.empty()) return;
    
    const double categoryWidth = chartRect().width() / categories_.size();
    const double barWidth = categoryWidth * (1.0 - theme_.barSpacing);
    
    double runningTotal = 0;
    waterfallTotals_.clear();
    waterfallIncreases_.clear();
    
    for (int catIdx = 0; catIdx < categories_.size(); ++catIdx) {
        double barX = chartRect().left() + catIdx * categoryWidth + 
                     categoryWidth * theme_.barSpacing / 2;
        
        double value = 0;
        if (!series_.empty()) {
            value = getData(categories_[catIdx], series_[0].name);
        }
        
        double previousTotal = runningTotal;
        runningTotal += value;
        
        waterfallTotals_.push_back(runningTotal);
        waterfallIncreases_.push_back(value >= 0);
        
        double barBottom = chartRect().bottom() - calculateBarHeight(previousTotal);
        double barTop = chartRect().bottom() - calculateBarHeight(runningTotal);
        
        QRectF barRect(barX, std::min(barTop, barBottom), 
                      barWidth, std::abs(barTop - barBottom));
        
        QColor color = value >= 0 ? theme_.positiveColor : theme_.negativeColor;
        drawBar(painter, barRect, value, color, catIdx, 0);
        
        // Draw connector line
        if (catIdx > 0) {
            painter->setPen(QPen(theme_.connectorColor, 1, Qt::DashLine));
            double prevX = chartRect().left() + (catIdx - 1) * categoryWidth + 
                          categoryWidth * theme_.barSpacing / 2 + barWidth;
            painter->drawLine(QPointF(prevX, barBottom), QPointF(barX, barBottom));
        }
    }
}

void BarChart::drawRangeChart(QPainter* painter) {
    if (categories_.empty() || series_.empty()) return;
    
    const double categoryWidth = chartRect().width() / categories_.size();
    const double barWidth = categoryWidth * (1.0 - theme_.barSpacing);
    
    for (int catIdx = 0; catIdx < categories_.size(); ++catIdx) {
        double barX = chartRect().left() + catIdx * categoryWidth + 
                     categoryWidth * theme_.barSpacing / 2;
        
        for (size_t seriesIdx = 0; seriesIdx < series_.size(); ++seriesIdx) {
            if (!series_[seriesIdx].visible) continue;
            
            auto it = rangeData_.find({categories_[catIdx], series_[seriesIdx].name});
            if (it == rangeData_.end()) continue;
            
            double minHeight = calculateBarHeight(it->second.min);
            double maxHeight = calculateBarHeight(it->second.max);
            
            QRectF barRect(barX + seriesIdx * (barWidth / series_.size()), 
                          chartRect().bottom() - maxHeight,
                          barWidth / series_.size() * 0.8,
                          maxHeight - minHeight);
            
            QColor color = getBarColor(seriesIdx, catIdx);
            drawBar(painter, barRect, it->second.max - it->second.min, color, catIdx, seriesIdx);
        }
    }
}

void BarChart::drawBar(QPainter* painter, const QRectF& rect, double value, 
                      const QColor& color, int categoryIndex, int seriesIndex) {
    if (rect.height() <= 0 || rect.width() <= 0) return;
    
    painter->save();
    
    // Check if this bar is hovered
    bool isHovered = (categoryIndex == hoveredCategory_ && seriesIndex == hoveredSeries_);
    bool isSelected = (categoryIndex == selectedCategory_ && seriesIndex == selectedSeries_);
    
    QColor fillColor = color;
    if (isHovered) {
        fillColor = fillColor.lighter(110);
    }
    if (isSelected) {
        fillColor = fillColor.darker(110);
    }
    
    // Draw bar with effects
    QPainterPath barPath;
    if (theme_.cornerRadius > 0) {
        // Only round top corners for vertical bars
        if (theme_.horizontal) {
            barPath.addRoundedRect(rect, theme_.cornerRadius, theme_.cornerRadius);
        } else {
            QRectF fullRect = rect;
            fullRect.setBottom(chartRect().bottom());
            barPath.addRoundedRect(fullRect, theme_.cornerRadius, theme_.cornerRadius);
            
            // Clip bottom part to make it flat
            QPainterPath clipPath;
            clipPath.addRect(QRectF(rect.left(), rect.bottom() - theme_.cornerRadius, 
                                   rect.width(), theme_.cornerRadius + 1));
            barPath = barPath.subtracted(clipPath);
        }
    } else {
        barPath.addRect(rect);
    }
    
    // Apply effects
    if (theme_.effects.enabled) {
        // Shadow
        if (theme_.effects.dropShadow) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor(0, 0, 0, 30));
            painter->translate(2, 2);
            painter->drawPath(barPath);
            painter->translate(-2, -2);
        }
        
        // Glow for hovered bars
        if (isHovered && theme_.effects.glow) {
            GlowEffects::drawGlowPath(painter, barPath, fillColor.lighter(150), 5);
        }
    }
    
    // Fill bar
    if (theme_.gradient) {
        QLinearGradient gradient;
        if (theme_.horizontal) {
            gradient = QLinearGradient(rect.topLeft(), rect.topRight());
        } else {
            gradient = QLinearGradient(rect.topLeft(), rect.bottomLeft());
        }
        gradient.setColorAt(0, fillColor.lighter(120));
        gradient.setColorAt(1, fillColor);
        painter->fillPath(barPath, gradient);
    } else {
        painter->fillPath(barPath, fillColor);
    }
    
    // Draw border
    if (theme_.barBorderWidth > 0) {
        painter->setPen(QPen(fillColor.darker(120), theme_.barBorderWidth));
        painter->drawPath(barPath);
    }
    
    // Draw value label
    if (theme_.showValues) {
        drawBarValue(painter, rect, value);
    }
    
    // Store bar info for hit testing
    if (categoryIndex < static_cast<int>(layout_.bars.size()) &&
        seriesIndex < static_cast<int>(layout_.bars[categoryIndex].size())) {
        layout_.bars[categoryIndex][seriesIndex] = {rect, categoryIndex, seriesIndex, value};
    }
    
    painter->restore();
}

void BarChart::drawBarValue(QPainter* painter, const QRectF& barRect, double value) {
    QString label = QString(valueFormat_).arg(value);
    if (!valuePrefix_.isEmpty()) label.prepend(valuePrefix_);
    if (!valueSuffix_.isEmpty()) label.append(valueSuffix_);
    
    QFont valueFont = font();
    valueFont.setPointSize(theme_.valueFontSize);
    painter->setFont(valueFont);
    
    QFontMetrics fm(valueFont);
    QRectF textRect = fm.boundingRect(label);
    
    QPointF textPos;
    if (theme_.horizontal) {
        textPos = QPointF(barRect.right() + 5, 
                         barRect.center().y() + textRect.height() / 2);
    } else {
        if (theme_.valuePosition == BarChartTheme::ValuePosition::Inside && 
            barRect.height() > textRect.height() + 10) {
            textPos = QPointF(barRect.center().x() - textRect.width() / 2,
                             barRect.top() + textRect.height() + 5);
        } else {
            textPos = QPointF(barRect.center().x() - textRect.width() / 2,
                             barRect.top() - 5);
        }
    }
    
    // Draw text background for better readability
    if (theme_.effects.enabled) {
        QRectF bgRect = textRect.translated(textPos - QPointF(0, textRect.height()));
        bgRect.adjust(-2, -1, 2, 1);
        painter->fillRect(bgRect, QColor(255, 255, 255, 200));
    }
    
    painter->setPen(theme_.valueFontColor);
    painter->drawText(textPos, label);
}

void BarChart::drawCategoryLabel(QPainter* painter, const QString& category, 
                                const QPointF& position, bool rotated) {
    painter->save();
    
    if (rotated) {
        painter->translate(position);
        painter->rotate(-45);
        painter->drawText(QPointF(0, 0), category);
    } else {
        painter->drawText(position, category);
    }
    
    painter->restore();
}

void BarChart::calculateBarLayout() {
    layout_.bars.clear();
    layout_.bars.resize(categories_.size());
    
    for (int i = 0; i < categories_.size(); ++i) {
        layout_.bars[i].resize(series_.size());
    }
    
    // Calculate total dimensions
    if (theme_.horizontal) {
        layout_.totalWidth = chartRect().width();
        layout_.totalHeight = chartRect().height();
        layout_.categoryWidth = layout_.totalHeight / categories_.size();
    } else {
        layout_.totalWidth = chartRect().width();
        layout_.totalHeight = chartRect().height();
        layout_.categoryWidth = layout_.totalWidth / categories_.size();
    }
    
    layout_.barWidth = layout_.categoryWidth * (1.0 - theme_.barSpacing);
    layout_.groupWidth = layout_.barWidth;
}

QRectF BarChart::calculateBarRect(int categoryIndex, int seriesIndex, double value) {
    if (categoryIndex < 0 || categoryIndex >= categories_.size() ||
        seriesIndex < 0 || seriesIndex >= static_cast<int>(series_.size())) {
        return QRectF();
    }
    
    double height = calculateBarHeight(value);
    
    if (theme_.horizontal) {
        double y = chartRect().top() + categoryIndex * layout_.categoryWidth + 
                  layout_.categoryWidth * theme_.barSpacing / 2;
        double barHeight = layout_.barWidth / series_.size();
        
        return QRectF(chartRect().left(), 
                     y + seriesIndex * barHeight,
                     height,
                     barHeight * 0.8);
    } else {
        double x = chartRect().left() + categoryIndex * layout_.categoryWidth + 
                  layout_.categoryWidth * theme_.barSpacing / 2;
        double barWidth = layout_.barWidth / series_.size();
        
        return QRectF(x + seriesIndex * barWidth, 
                     chartRect().bottom() - height,
                     barWidth * 0.8,
                     height);
    }
}

double BarChart::calculateBarHeight(double value) const {
    double minVal = 0, maxVal = 0;
    
    // Find data range
    for (const auto& series : series_) {
        for (const auto& point : series.dataPoints) {
            maxVal = std::max(maxVal, point.y);
            minVal = std::min(minVal, point.y);
        }
    }
    
    // Add some padding to the range
    double range = maxVal - minVal;
    if (range == 0) range = 1;
    maxVal += range * 0.1;
    
    double availableHeight = theme_.horizontal ? chartRect().width() : chartRect().height();
    return (value - minVal) / (maxVal - minVal) * availableHeight;
}

double BarChart::calculateStackedHeight(int categoryIndex, int seriesIndex) const {
    double total = 0;
    for (int i = 0; i <= seriesIndex; ++i) {
        if (series_[i].visible) {
            total += getData(categories_[categoryIndex], series_[i].name);
        }
    }
    return calculateBarHeight(total);
}

QColor BarChart::getBarColor(int seriesIndex, int categoryIndex) const {
    if (seriesIndex < static_cast<int>(series_.size()) && series_[seriesIndex].color.isValid()) {
        return series_[seriesIndex].color;
    }
    
    if (seriesIndex < theme_.colors.size()) {
        return theme_.colors[seriesIndex];
    }
    
    // Generate color based on index
    return ChartUtils::generateColorPalette(series_.size())[seriesIndex];
}

double BarChart::getAnimatedHeight(double targetHeight, int categoryIndex, int seriesIndex) {
    if (!isAnimationEnabled() || !theme_.animateGrowth) {
        return targetHeight;
    }
    
    // Ensure animation arrays are properly sized
    if (static_cast<size_t>(categoryIndex) >= animatedHeights_.size()) {
        return targetHeight;
    }
    if (static_cast<size_t>(seriesIndex) >= animatedHeights_[categoryIndex].size()) {
        return targetHeight;
    }
    
    // Update animated value
    double current = animatedHeights_[categoryIndex][seriesIndex];
    double diff = targetHeight - current;
    
    if (std::abs(diff) < 0.01) {
        animatedHeights_[categoryIndex][seriesIndex] = targetHeight;
        return targetHeight;
    }
    
    // Smooth animation
    animatedHeights_[categoryIndex][seriesIndex] = current + diff * 0.1;
    return animatedHeights_[categoryIndex][seriesIndex];
}

bool BarChart::isPointInBar(const QPointF& point, const BarInfo& bar) const {
    return bar.rect.contains(point);
}

void BarChart::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        int category = barAt(event->pos());
        int series = seriesAt(event->pos());
        
        if (category >= 0) {
            selectedCategory_ = category;
            selectedSeries_ = series;
            
            if (series >= 0) {
                emit barClicked(categories_[category], series);
            }
            emit categoryClicked(categories_[category]);
            
            update();
        }
    }
    
    CustomChartBase::mousePressEvent(event);
}

void BarChart::mouseMoveEvent(QMouseEvent* event) {
    int category = barAt(event->pos());
    int series = seriesAt(event->pos());
    
    if (category != hoveredCategory_ || series != hoveredSeries_) {
        hoveredCategory_ = category;
        hoveredSeries_ = series;
        
        if (category >= 0 && series >= 0) {
            double value = getData(categories_[category], series_[series].name);
            QString tooltip = QString("%1\n%2: %3%4%5")
                .arg(categories_[category])
                .arg(series_[series].name)
                .arg(valuePrefix_)
                .arg(value, 0, 'f', 2)
                .arg(valueSuffix_);
            
            QToolTip::showText(event->globalPos(), tooltip, this);
            emit barHovered(categories_[category], series);
        } else {
            QToolTip::hideText();
        }
        
        update();
    }
    
    CustomChartBase::mouseMoveEvent(event);
}

void BarChart::mouseReleaseEvent(QMouseEvent* event) {
    CustomChartBase::mouseReleaseEvent(event);
}

void BarChart::mouseDoubleClickEvent(QMouseEvent* event) {
    CustomChartBase::mouseDoubleClickEvent(event);
}

void BarChart::wheelEvent(QWheelEvent* event) {
    CustomChartBase::wheelEvent(event);
}

} // namespace llm_re::ui_v2::charts