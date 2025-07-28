#include "memory_dock.h"
#include "../core/theme_manager.h"
#include "../core/ui_constants.h"
#include "../core/ui_utils.h"
#include <QToolBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeView>
#include <QTableView>
#include <QHeaderView>
#include <QStackedWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QMenu>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QPainter>
#include <QWheelEvent>
#include <QTimer>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QtMath>
#include <QDateTimeEdit>
#include <QListWidget>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSettings>
#include <QClipboard>
#include <algorithm>
#include <cmath>

namespace llm_re::ui_v2 {

// MemoryGraphNode implementation

MemoryGraphNode::MemoryGraphNode(const MemoryEntry& entry)
    : entry_(entry)
{
    setAcceptHoverEvents(true);
    setFlag(ItemIsSelectable);
    setFlag(ItemIsMovable);
}

QRectF MemoryGraphNode::boundingRect() const {
    return QRectF(-nodeRadius_, -nodeRadius_, nodeRadius_ * 2, nodeRadius_ * 2);
}

void MemoryGraphNode::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    // Node color based on confidence
    QColor nodeColor;
    if (entry_.confidence >= 80) {
        nodeColor = QColor("#4CAF50");
    } else if (entry_.confidence >= 50) {
        nodeColor = QColor("#FF9800");
    } else {
        nodeColor = QColor("#F44336");
    }
    
    if (highlighted_) {
        nodeColor = nodeColor.lighter(120);
    }
    
    if (hovered_) {
        nodeColor = nodeColor.lighter(110);
    }
    
    // Shadow
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0, 0, 0, 50));
    painter->drawEllipse(QPointF(2, 2), nodeRadius_ - 2, nodeRadius_ - 2);
    
    // Node
    painter->setBrush(nodeColor);
    painter->setPen(QPen(nodeColor.darker(120), 2));
    painter->drawEllipse(QPointF(0, 0), nodeRadius_, nodeRadius_);
    
    // Bookmark indicator
    if (entry_.isBookmarked) {
        painter->setBrush(QColor("#FFD700"));
        painter->setPen(Qt::NoPen);
        QPolygonF star;
        for (int i = 0; i < 10; ++i) {
            qreal radius = (i % 2 == 0) ? 8 : 4;
            qreal angle = i * M_PI / 5;
            star << QPointF(radius * cos(angle), radius * sin(angle));
        }
        painter->drawPolygon(star);
    }
    
    // Label
    if (isSelected() || hovered_) {
        painter->setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
        painter->setFont(QFont("Sans", 9));
        QString label = entry_.function.isEmpty() ? entry_.address : entry_.function;
        QRectF textRect = painter->fontMetrics().boundingRect(label);
        textRect.moveCenter(QPointF(0, nodeRadius_ + 15));
        painter->drawText(textRect, Qt::AlignCenter, label);
    }
}

QPointF MemoryGraphNode::centerPos() const {
    return pos();
}

void MemoryGraphNode::addEdge(MemoryGraphNode* target) {
    if (!edges_.contains(target)) {
        edges_.append(target);
    }
}

void MemoryGraphNode::removeEdge(MemoryGraphNode* target) {
    edges_.removeOne(target);
}

void MemoryGraphNode::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    Q_UNUSED(event);
    // Handled by scene
}

void MemoryGraphNode::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    Q_UNUSED(event);
    // Emit signal through scene
}

void MemoryGraphNode::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    Q_UNUSED(event);
    hovered_ = true;
    update();
}

void MemoryGraphNode::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    Q_UNUSED(event);
    hovered_ = false;
    update();
}

// MemoryGraphEdge implementation

MemoryGraphEdge::MemoryGraphEdge(MemoryGraphNode* source, MemoryGraphNode* target)
    : source_(source)
    , target_(target)
{
    setFlag(ItemIsSelectable, false);
    setZValue(-1); // Behind nodes
}

QRectF MemoryGraphEdge::boundingRect() const {
    if (!source_ || !target_) {
        return QRectF();
    }
    
    QPointF sourcePoint = source_->centerPos();
    QPointF targetPoint = target_->centerPos();
    
    return QRectF(sourcePoint, targetPoint).normalized()
        .adjusted(-5, -5, 5, 5);
}

void MemoryGraphEdge::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    
    if (!source_ || !target_) {
        return;
    }
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    QPointF sourcePoint = source_->centerPos();
    QPointF targetPoint = target_->centerPos();
    
    // Calculate edge points on node boundaries
    QLineF line(sourcePoint, targetPoint);
    qreal angle = std::atan2(line.dy(), line.dx());
    
    QPointF sourceEdge = sourcePoint + QPointF(30 * cos(angle), 30 * sin(angle));
    QPointF targetEdge = targetPoint - QPointF(30 * cos(angle), 30 * sin(angle));
    
    // Draw edge
    QPen pen(ThemeManager::instance()->color(ThemeManager::OnSurfaceVariant), 2);
    pen.setStyle(Qt::SolidLine);
    painter->setPen(pen);
    
    // Curved edge
    QPainterPath path;
    path.moveTo(sourceEdge);
    QPointF control1 = sourceEdge + QPointF(0, line.length() * 0.2);
    QPointF control2 = targetEdge - QPointF(0, line.length() * 0.2);
    path.cubicTo(control1, control2, targetEdge);
    painter->drawPath(path);
    
    // Arrow
    qreal arrowSize = 10;
    QPointF arrowP1 = targetEdge - QPointF(
        arrowSize * cos(angle - M_PI / 6),
        arrowSize * sin(angle - M_PI / 6)
    );
    QPointF arrowP2 = targetEdge - QPointF(
        arrowSize * cos(angle + M_PI / 6),
        arrowSize * sin(angle + M_PI / 6)
    );
    
    painter->setBrush(pen.color());
    QPolygonF arrow;
    arrow << targetEdge << arrowP1 << arrowP2;
    painter->drawPolygon(arrow);
}

void MemoryGraphEdge::updatePosition() {
    prepareGeometryChange();
}

// MemoryGraphView implementation

MemoryGraphView::MemoryGraphView(QWidget* parent)
    : QGraphicsView(parent)
{
    scene_ = new QGraphicsScene(this);
    setScene(scene_);
    
    setRenderHint(QPainter::Antialiasing);
    setViewportUpdateMode(BoundingRectViewportUpdate);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
    
    // Enable dragging
    setDragMode(RubberBandDrag);
}

void MemoryGraphView::setEntries(const QList<MemoryEntry>& entries) {
    // Clear existing
    scene_->clear();
    nodes_.clear();
    edges_.clear();
    
    // Create nodes
    for (const MemoryEntry& entry : entries) {
        auto* node = new MemoryGraphNode(entry);
        scene_->addItem(node);
        nodes_[entry.id] = node;
    }
    
    // Create edges
    for (const MemoryEntry& entry : entries) {
        MemoryGraphNode* sourceNode = nodes_[entry.id];
        for (const QUuid& refId : entry.references) {
            if (nodes_.contains(refId)) {
                MemoryGraphNode* targetNode = nodes_[refId];
                sourceNode->addEdge(targetNode);
                
                auto* edge = new MemoryGraphEdge(sourceNode, targetNode);
                scene_->addItem(edge);
                edges_.append(edge);
            }
        }
    }
    
    // Perform layout
    performLayout();
}

void MemoryGraphView::highlightEntry(const QUuid& id) {
    // Clear previous highlights
    for (auto* node : nodes_) {
        node->setHighlighted(false);
    }
    
    // Highlight new
    if (nodes_.contains(id)) {
        nodes_[id]->setHighlighted(true);
    }
}

void MemoryGraphView::centerOnEntry(const QUuid& id) {
    if (nodes_.contains(id)) {
        if (animated_) {
            // Smooth animation to center
            QPointF targetPos = nodes_[id]->pos();
            
            auto* animation = new QPropertyAnimation(this, "");
            animation->setDuration(300);
            animation->setStartValue(mapToScene(viewport()->rect().center()));
            animation->setEndValue(targetPos);
            animation->setEasingCurve(QEasingCurve::InOutQuad);
            
            connect(animation, &QPropertyAnimation::valueChanged,
                    [this](const QVariant& value) {
                centerOn(value.toPointF());
            });
            
            animation->start(QAbstractAnimation::DeleteWhenStopped);
        } else {
            centerOn(nodes_[id]);
        }
    }
}

void MemoryGraphView::setLayoutAlgorithm(const QString& algorithm) {
    layoutAlgorithm_ = algorithm;
    performLayout();
}

void MemoryGraphView::setEdgeStyle(const QString& style) {
    edgeStyle_ = style;
    scene_->update();
}

void MemoryGraphView::setShowLabels(bool show) {
    showLabels_ = show;
    scene_->update();
}

void MemoryGraphView::setAnimated(bool animated) {
    animated_ = animated;
}


void MemoryGraphView::zoomIn() {
    scale(1.2, 1.2);
    currentScale_ *= 1.2;
}

void MemoryGraphView::zoomOut() {
    scale(0.8, 0.8);
    currentScale_ *= 0.8;
}

void MemoryGraphView::fitInView() {
    QGraphicsView::fitInView(scene_->itemsBoundingRect(), Qt::KeepAspectRatio);
    currentScale_ = 1.0;
}

void MemoryGraphView::resetZoom() {
    resetTransform();
    currentScale_ = 1.0;
}

void MemoryGraphView::wheelEvent(QWheelEvent* event) {
    // Zoom with mouse wheel
    const qreal scaleFactor = 1.15;
    
    if (event->angleDelta().y() > 0) {
        scale(scaleFactor, scaleFactor);
        currentScale_ *= scaleFactor;
    } else {
        scale(1.0 / scaleFactor, 1.0 / scaleFactor);
        currentScale_ /= scaleFactor;
    }
}

void MemoryGraphView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        isPanning_ = true;
        lastMousePos_ = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    } else {
        QGraphicsView::mousePressEvent(event);
        
        // Check for node click
        QGraphicsItem* item = itemAt(event->pos());
        if (auto* node = dynamic_cast<MemoryGraphNode*>(item)) {
            emit entryClicked(node->entry().id);
        }
    }
}

void MemoryGraphView::mouseMoveEvent(QMouseEvent* event) {
    if (isPanning_) {
        QPoint delta = event->pos() - lastMousePos_;
        lastMousePos_ = event->pos();
        
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        
        event->accept();
    } else {
        QGraphicsView::mouseMoveEvent(event);
    }
}

void MemoryGraphView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        isPanning_ = false;
        setCursor(Qt::ArrowCursor);
        event->accept();
    } else {
        QGraphicsView::mouseReleaseEvent(event);
    }
}

void MemoryGraphView::drawBackground(QPainter* painter, const QRectF& rect) {
    // Grid background
    painter->fillRect(rect, ThemeManager::instance()->color(ThemeManager::Surface));
    
    if (currentScale_ > 0.5) {
        QPen gridPen(ThemeManager::instance()->color(ThemeManager::OnSurfaceVariant));
        gridPen.setStyle(Qt::DotLine);
        gridPen.setWidthF(0.5);
        painter->setPen(gridPen);
        
        const int gridSize = 50;
        qreal left = int(rect.left()) - (int(rect.left()) % gridSize);
        qreal top = int(rect.top()) - (int(rect.top()) % gridSize);
        
        for (qreal x = left; x < rect.right(); x += gridSize) {
            painter->drawLine(x, rect.top(), x, rect.bottom());
        }
        for (qreal y = top; y < rect.bottom(); y += gridSize) {
            painter->drawLine(rect.left(), y, rect.right(), y);
        }
    }
}

void MemoryGraphView::performLayout() {
    if (layoutAlgorithm_ == "force-directed") {
        performForceDirectedLayout();
    } else if (layoutAlgorithm_ == "hierarchical") {
        performHierarchicalLayout();
    } else if (layoutAlgorithm_ == "circular") {
        performCircularLayout();
    }
    
    // Update edges
    for (auto* edge : edges_) {
        edge->updatePosition();
    }
    
    // Fit view
    fitInView();
}

void MemoryGraphView::performForceDirectedLayout() {
    if (nodes_.isEmpty()) return;
    
    // Initialize random positions
    QList<MemoryGraphNode*> nodeList = nodes_.values();
    for (auto* node : nodeList) {
        node->setPos(
            (qrand() % 1000) - 500,
            (qrand() % 1000) - 500
        );
    }
    
    // Force-directed simulation
    const int iterations = 100;
    const qreal k = 100.0; // Ideal spring length
    const qreal c_rep = 10000.0; // Repulsion constant
    const qreal c_spring = 0.1; // Spring constant
    const qreal damping = 0.9;
    
    QHash<MemoryGraphNode*, QPointF> velocities;
    for (auto* node : nodeList) {
        velocities[node] = QPointF(0, 0);
    }
    
    for (int iter = 0; iter < iterations; ++iter) {
        // Calculate forces
        QHash<MemoryGraphNode*, QPointF> forces;
        
        // Repulsion between all nodes
        for (int i = 0; i < nodeList.size(); ++i) {
            QPointF force(0, 0);
            
            for (int j = 0; j < nodeList.size(); ++j) {
                if (i == j) continue;
                
                QPointF delta = nodeList[i]->pos() - nodeList[j]->pos();
                qreal distance = sqrt(delta.x() * delta.x() + delta.y() * delta.y());
                if (distance < 0.01) distance = 0.01;
                
                qreal repulsion = c_rep / (distance * distance);
                force += (delta / distance) * repulsion;
            }
            
            forces[nodeList[i]] = force;
        }
        
        // Spring forces for connected nodes
        for (auto* edge : edges_) {
            // This is simplified - would need access to edge endpoints
        }
        
        // Apply forces
        for (auto* node : nodeList) {
            QPointF velocity = velocities[node] + forces[node];
            velocity *= damping;
            velocities[node] = velocity;
            
            if (animated_) {
                // Animate to new position
                QPointF newPos = node->pos() + velocity;
                
                auto* animation = new QPropertyAnimation(node, "pos");
                animation->setDuration(50);
                animation->setEndValue(newPos);
                animation->start(QAbstractAnimation::DeleteWhenStopped);
            } else {
                node->setPos(node->pos() + velocity);
            }
        }
    }
}

void MemoryGraphView::performHierarchicalLayout() {
    if (nodes_.isEmpty()) return;
    
    // Simple hierarchical layout based on reference count
    QList<MemoryGraphNode*> nodeList = nodes_.values();
    
    // Calculate levels based on incoming references
    QHash<MemoryGraphNode*, int> levels;
    QList<QList<MemoryGraphNode*>> levelNodes;
    
    // Find root nodes (no incoming references)
    for (auto* node : nodeList) {
        bool hasIncoming = false;
        for (auto* other : nodeList) {
            if (other->edges().contains(node)) {
                hasIncoming = true;
                break;
            }
        }
        if (!hasIncoming) {
            levels[node] = 0;
        }
    }
    
    // Assign levels
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto* node : nodeList) {
            if (!levels.contains(node)) {
                for (auto* other : nodeList) {
                    if (levels.contains(other) && other->edges().contains(node)) {
                        levels[node] = levels[other] + 1;
                        changed = true;
                    }
                }
            }
        }
    }
    
    // Group by level
    int maxLevel = 0;
    for (auto level : levels.values()) {
        maxLevel = std::max(maxLevel, level);
    }
    
    levelNodes.resize(maxLevel + 1);
    for (auto it = levels.begin(); it != levels.end(); ++it) {
        levelNodes[it.value()].append(it.key());
    }
    
    // Position nodes
    const qreal levelHeight = 150;
    const qreal nodeSpacing = 100;
    
    for (int level = 0; level <= maxLevel; ++level) {
        qreal y = level * levelHeight;
        qreal totalWidth = levelNodes[level].size() * nodeSpacing;
        qreal x = -totalWidth / 2;
        
        for (auto* node : levelNodes[level]) {
            if (animated_) {
                auto* animation = new QPropertyAnimation(node, "pos");
                animation->setDuration(500);
                animation->setEndValue(QPointF(x, y));
                animation->setEasingCurve(QEasingCurve::InOutQuad);
                animation->start(QAbstractAnimation::DeleteWhenStopped);
            } else {
                node->setPos(x, y);
            }
            x += nodeSpacing;
        }
    }
}

void MemoryGraphView::performCircularLayout() {
    if (nodes_.isEmpty()) return;
    
    QList<MemoryGraphNode*> nodeList = nodes_.values();
    const int count = nodeList.size();
    const qreal radius = count * 30;
    
    for (int i = 0; i < count; ++i) {
        qreal angle = (2 * M_PI * i) / count;
        qreal x = radius * cos(angle);
        qreal y = radius * sin(angle);
        
        if (animated_) {
            auto* animation = new QPropertyAnimation(nodeList[i], "pos");
            animation->setDuration(500);
            animation->setEndValue(QPointF(x, y));
            animation->setEasingCurve(QEasingCurve::InOutQuad);
            animation->start(QAbstractAnimation::DeleteWhenStopped);
        } else {
            nodeList[i]->setPos(x, y);
        }
    }
}

// MemoryHeatmapView implementation

MemoryHeatmapView::MemoryHeatmapView(QWidget* parent)
    : BaseStyledWidget(parent)
{
    setMouseTracking(true);
}

void MemoryHeatmapView::setEntries(const QList<MemoryEntry>& entries) {
    entries_ = entries;
    calculateLayout();
    update();
}

void MemoryHeatmapView::setColorScheme(const QString& scheme) {
    colorScheme_ = scheme;
    update();
}

void MemoryHeatmapView::setGroupBy(const QString& field) {
    groupBy_ = field;
    calculateLayout();
    update();
}

void MemoryHeatmapView::setMetric(const QString& metric) {
    metric_ = metric;
    calculateLayout();
    update();
}


void MemoryHeatmapView::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Background
    painter.fillRect(rect(), ThemeManager::instance()->color(ThemeManager::Surface));
    
    // Draw cells
    for (int i = 0; i < cells_.size(); ++i) {
        const HeatmapCell& cell = cells_[i];
        
        // Cell color
        QColor cellColor = valueToColor(cell.value);
        if (selectedCells_.contains(i)) {
            cellColor = cellColor.lighter(120);
        }
        if (i == hoveredCell_) {
            cellColor = cellColor.lighter(110);
        }
        
        painter.fillRect(cell.rect, cellColor);
        
        // Cell border
        painter.setPen(ThemeManager::instance()->color(ThemeManager::Surface));
        painter.drawRect(cell.rect);
    }
    
    // Labels
    QFont font = painter.font();
    font.setPointSize(9);
    painter.setFont(font);
    painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
    
    // Y-axis labels (groups)
    QStringList groups;
    for (const auto& cell : cells_) {
        if (!groups.contains(cell.group)) {
            groups.append(cell.group);
        }
    }
    
    for (int i = 0; i < groups.size(); ++i) {
        QRectF labelRect(0, margin_ + i * (cellSize_ + spacing_), margin_ - 5, cellSize_);
        painter.drawText(labelRect, Qt::AlignRight | Qt::AlignVCenter, groups[i]);
    }
    
    // Tooltip
    if (hoveredCell_ >= 0 && hoveredCell_ < cells_.size()) {
        const HeatmapCell& cell = cells_[hoveredCell_];
        
        QString tooltip = QString("%1 / %2\n%3: %4")
            .arg(cell.group)
            .arg(cell.subgroup)
            .arg(metric_)
            .arg(formatValue(cell.value));
        
        QFontMetrics fm(painter.font());
        QRect tooltipRect = fm.boundingRect(tooltip);
        tooltipRect.adjust(-5, -5, 5, 5);
        tooltipRect.moveTopLeft(QCursor::pos() - mapToGlobal(QPoint(0, 0)) + QPoint(10, 10));
        
        painter.fillRect(tooltipRect, ThemeManager::instance()->color(ThemeManager::SurfaceVariant));
        painter.setPen(ThemeManager::instance()->color(ThemeManager::OnSurface));
        painter.drawText(tooltipRect, Qt::AlignCenter, tooltip);
    }
}

void MemoryHeatmapView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        for (int i = 0; i < cells_.size(); ++i) {
            if (cells_[i].rect.contains(event->pos())) {
                if (event->modifiers() & Qt::ControlModifier) {
                    // Toggle selection
                    if (selectedCells_.contains(i)) {
                        selectedCells_.remove(i);
                    } else {
                        selectedCells_.insert(i);
                    }
                } else {
                    // Single selection
                    selectedCells_.clear();
                    selectedCells_.insert(i);
                }
                
                emit cellClicked(cells_[i].group, cells_[i].subgroup);
                
                QStringList selectedGroups;
                for (int idx : selectedCells_) {
                    QString groupId = cells_[idx].group + "/" + cells_[idx].subgroup;
                    if (!selectedGroups.contains(groupId)) {
                        selectedGroups.append(groupId);
                    }
                }
                emit selectionChanged(selectedGroups);
                
                update();
                break;
            }
        }
    }
}

void MemoryHeatmapView::mouseMoveEvent(QMouseEvent* event) {
    int oldHovered = hoveredCell_;
    hoveredCell_ = -1;
    
    for (int i = 0; i < cells_.size(); ++i) {
        if (cells_[i].rect.contains(event->pos())) {
            hoveredCell_ = i;
            break;
        }
    }
    
    if (hoveredCell_ != oldHovered) {
        update();
    }
}

void MemoryHeatmapView::leaveEvent(QEvent* event) {
    Q_UNUSED(event);
    hoveredCell_ = -1;
    update();
}

void MemoryHeatmapView::resizeEvent(QResizeEvent* event) {
    BaseStyledWidget::resizeEvent(event);
    calculateLayout();
}

void MemoryHeatmapView::calculateLayout() {
    cells_.clear();
    
    if (entries_.isEmpty()) return;
    
    // Group data
    QHash<QString, QHash<QString, double>> groupedData;
    QHash<QString, QHash<QString, int>> groupedCounts;
    
    for (const MemoryEntry& entry : entries_) {
        QString group;
        QString subgroup = "default";
        
        if (groupBy_ == "function") {
            group = entry.function.isEmpty() ? "Unknown" : entry.function;
        } else if (groupBy_ == "module") {
            group = entry.module.isEmpty() ? "Unknown" : entry.module;
        } else if (groupBy_ == "tag") {
            group = entry.tags.isEmpty() ? "Untagged" : entry.tags.first();
        }
        
        double value = 0;
        if (metric_ == "count") {
            value = 1;
        } else if (metric_ == "confidence") {
            value = entry.confidence;
        }
        
        groupedData[group][subgroup] += value;
        groupedCounts[group][subgroup]++;
    }
    
    // Calculate average for confidence metric
    if (metric_ == "confidence") {
        for (auto& group : groupedData) {
            for (auto it = group.begin(); it != group.end(); ++it) {
                it.value() /= groupedCounts[it.key()][it.key()];
            }
        }
    }
    
    // Create cells
    int row = 0;
    for (auto groupIt = groupedData.begin(); groupIt != groupedData.end(); ++groupIt) {
        int col = 0;
        for (auto subIt = groupIt.value().begin(); subIt != groupIt.value().end(); ++subIt) {
            HeatmapCell cell;
            cell.group = groupIt.key();
            cell.subgroup = subIt.key();
            cell.value = subIt.value();
            cell.count = groupedCounts[groupIt.key()][subIt.key()];
            cell.rect = QRectF(
                margin_ + col * (cellSize_ + spacing_),
                margin_ + row * (cellSize_ + spacing_),
                cellSize_,
                cellSize_
            );
            
            cells_.append(cell);
            col++;
        }
        row++;
    }
}

QColor MemoryHeatmapView::valueToColor(double value) {
    // Normalize value to 0-1
    double minVal = 0;
    double maxVal = 100;
    if (metric_ == "count") {
        maxVal = 0;
        for (const auto& cell : cells_) {
            maxVal = std::max(maxVal, cell.value);
        }
    }
    
    double normalized = (value - minVal) / (maxVal - minVal);
    normalized = qBound(0.0, normalized, 1.0);
    
    // Color schemes
    if (colorScheme_ == "viridis") {
        // Simplified viridis
        if (normalized < 0.25) {
            return QColor::fromRgbF(0.267, 0.005, 0.329);
        } else if (normalized < 0.5) {
            return QColor::fromRgbF(0.128, 0.565, 0.551);
        } else if (normalized < 0.75) {
            return QColor::fromRgbF(0.153, 0.682, 0.377);
        } else {
            return QColor::fromRgbF(0.993, 0.906, 0.144);
        }
    } else if (colorScheme_ == "heat") {
        // Red to yellow
        return QColor::fromRgbF(1.0, normalized, 0);
    } else if (colorScheme_ == "cool") {
        // Blue to green
        return QColor::fromRgbF(0, normalized, 1.0 - normalized);
    }
    
    return QColor::fromRgbF(normalized, normalized, normalized);
}

QString MemoryHeatmapView::formatValue(double value) {
    if (metric_ == "count") {
        return QString::number(static_cast<int>(value));
    } else if (metric_ == "confidence") {
        return QString("%1%").arg(static_cast<int>(value));
    }
    return QString::number(value, 'f', 2);
}

// MemoryDock implementation

MemoryDock::MemoryDock(QWidget* parent)
    : BaseStyledWidget(parent)
{
    setupUI();
    connectSignals();
    loadSettings();
}

MemoryDock::~MemoryDock() {
    saveSettings();
}

void MemoryDock::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    // Create toolbar
    createToolBar();
    layout->addWidget(toolBar_);
    
    // Create views
    createViews();
    layout->addWidget(viewStack_);
    
    // Create status bar
    createStatusBar();
    
    // Create context menu
    createContextMenu();
}

void MemoryDock::createToolBar() {
    toolBar_ = new QToolBar(this);
    toolBar_->setIconSize(QSize(16, 16));
    
    // Search
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(tr("Search memory entries..."));
    searchEdit_->setClearButtonEnabled(true);
    toolBar_->addWidget(searchEdit_);
    
    // View mode
    toolBar_->addSeparator();
    auto* viewLabel = new QLabel(tr("View:"), this);
    toolBar_->addWidget(viewLabel);
    
    viewModeCombo_ = new QComboBox(this);
    viewModeCombo_->addItems({tr("Tree"), tr("Table"), tr("Graph"), tr("Heatmap")});
    toolBar_->addWidget(viewModeCombo_);
    
    // Group by
    auto* groupLabel = new QLabel(tr("Group:"), this);
    toolBar_->addWidget(groupLabel);
    
    groupByCombo_ = new QComboBox(this);
    groupByCombo_->addItems({tr("Module"), tr("Function"), tr("Tag"), tr("None")});
    toolBar_->addWidget(groupByCombo_);
    
    // Actions
    toolBar_->addSeparator();
    
    refreshAction_ = toolBar_->addAction(UIUtils::icon("view-refresh"), tr("Refresh"));
    refreshAction_->setShortcut(QKeySequence::Refresh);
    
    filterAction_ = toolBar_->addAction(UIUtils::icon("view-filter"), tr("Advanced Filter"));
    
    toolBar_->addSeparator();
    
    bookmarkAction_ = toolBar_->addAction(UIUtils::icon("bookmark"), tr("Bookmark"));
    bookmarkAction_->setCheckable(true);
    
    deleteAction_ = toolBar_->addAction(UIUtils::icon("edit-delete"), tr("Delete"));
    deleteAction_->setShortcut(QKeySequence::Delete);
    
    toolBar_->addSeparator();
    
    importAction_ = toolBar_->addAction(UIUtils::icon("document-import"), tr("Import"));
}

void MemoryDock::createViews() {
    viewStack_ = new QStackedWidget(this);
    
    // Create model
    model_ = new MemoryModel(this);
    proxyModel_ = new QSortFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);
    proxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxyModel_->setRecursiveFilteringEnabled(true);
    
    // Tree view
    treeView_ = new QTreeView(this);
    treeView_->setModel(proxyModel_);
    treeView_->setAlternatingRowColors(true);
    treeView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    treeView_->setContextMenuPolicy(Qt::CustomContextMenu);
    treeView_->header()->setStretchLastSection(true);
    viewStack_->addWidget(treeView_);
    
    // Table view
    tableView_ = new QTableView(this);
    tableView_->setModel(proxyModel_);
    tableView_->setAlternatingRowColors(true);
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    tableView_->setContextMenuPolicy(Qt::CustomContextMenu);
    tableView_->setSortingEnabled(true);
    tableView_->horizontalHeader()->setStretchLastSection(true);
    viewStack_->addWidget(tableView_);
    
    // Graph view
    graphView_ = new MemoryGraphView(this);
    viewStack_->addWidget(graphView_);
    
    // Heatmap view
    heatmapView_ = new MemoryHeatmapView(this);
    viewStack_->addWidget(heatmapView_);
}

void MemoryDock::createStatusBar() {
    auto* statusLayout = new QHBoxLayout();
    statusLayout->setContentsMargins(5, 2, 5, 2);
    
    statusLabel_ = new QLabel(this);
    statusLayout->addWidget(statusLabel_);
    statusLayout->addStretch();
    
    auto* statusWidget = new QWidget(this);
    statusWidget->setLayout(statusLayout);
    statusWidget->setMaximumHeight(25);
    
    layout()->addWidget(statusWidget);
    
    updateStatusBar();
}

void MemoryDock::connectSignals() {
    // View mode
    connect(viewModeCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MemoryDock::onViewModeChanged);
    
    // Search
    connect(searchEdit_, &QLineEdit::textChanged,
            this, &MemoryDock::onSearchTextChanged);
    
    // Actions
    connect(refreshAction_, &QAction::triggered,
            this, &MemoryDock::refreshView);
    
    connect(filterAction_, &QAction::triggered,
            this, &MemoryDock::onAdvancedFilterClicked);
    
    connect(importAction_, &QAction::triggered,
            this, &MemoryDock::onImportClicked);
    
    
    connect(bookmarkAction_, &QAction::triggered,
            [this]() { bookmarkSelection(bookmarkAction_->isChecked()); });
    
    connect(deleteAction_, &QAction::triggered,
            [this]() { deleteSelection(); });
    
    // Tree/Table views
    connect(treeView_, &QTreeView::activated,
            this, &MemoryDock::onEntryActivated);
    
    connect(tableView_, &QTableView::activated,
            this, &MemoryDock::onEntryActivated);
    
    connect(treeView_, &QWidget::customContextMenuRequested,
            this, &MemoryDock::onCustomContextMenu);
    
    connect(tableView_, &QWidget::customContextMenuRequested,
            this, &MemoryDock::onCustomContextMenu);
    
    connect(treeView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MemoryDock::onSelectionChanged);
    
    connect(tableView_->selectionModel(), &QItemSelectionModel::selectionChanged,
            this, &MemoryDock::onSelectionChanged);
    
    // Graph view
    connect(graphView_, &MemoryGraphView::entryClicked,
            this, &MemoryDock::onGraphEntryClicked);
    
    connect(graphView_, &MemoryGraphView::entryDoubleClicked,
            this, &MemoryDock::entryDoubleClicked);
    
    // Heatmap view
    connect(heatmapView_, &MemoryHeatmapView::cellClicked,
            this, &MemoryDock::onHeatmapCellClicked);
    
    // Model
    connect(model_, &MemoryModel::entryAdded,
            [this]() { updateStatusBar(); });
    
    connect(model_, &MemoryModel::entryRemoved,
            [this]() { updateStatusBar(); });
    
    connect(model_, &MemoryModel::modelReset,
            [this]() { updateStatusBar(); });
    
    // Group by
    connect(groupByCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
                QStringList fields = {"module", "function", "tag", ""};
                if (index < fields.size()) {
                    model_->setGroupBy(fields[index]);
                }
            });
}

void MemoryDock::createContextMenu() {
    contextMenu_ = new QMenu(this);
    
    contextMenu_->addAction(UIUtils::icon("go-jump"), tr("Navigate to Address"),
                           [this]() {
        if (!selectedEntries_.isEmpty()) {
            emit navigateToAddress(entry(selectedEntries_.first()).address);
        }
    });
    
    contextMenu_->addAction(UIUtils::icon("view-refresh"), tr("Re-analyze"),
                           [this]() {
        if (!selectedEntries_.isEmpty()) {
            emit analyzeRequested(selectedEntries_.first());
        }
    });
    
    contextMenu_->addSeparator();
    
    auto* bookmarkAction = contextMenu_->addAction(UIUtils::icon("bookmark"), tr("Bookmark"));
    bookmarkAction->setCheckable(true);
    connect(bookmarkAction, &QAction::triggered,
            [this, bookmarkAction]() { bookmarkSelection(bookmarkAction->isChecked()); });
    
    contextMenu_->addSeparator();
    
    auto* tagMenu = contextMenu_->addMenu(UIUtils::icon("tag"), tr("Tags"));
    
    // Add tag actions dynamically
    connect(tagMenu, &QMenu::aboutToShow, [this, tagMenu]() {
        tagMenu->clear();
        
        for (const QString& tag : model_->allTags()) {
            auto* action = tagMenu->addAction(tag);
            action->setCheckable(true);
            
            // Check if all selected have this tag
            bool allHaveTag = true;
            for (const QUuid& id : selectedEntries_) {
                if (!entry(id).tags.contains(tag)) {
                    allHaveTag = false;
                    break;
                }
            }
            action->setChecked(allHaveTag);
            
            connect(action, &QAction::triggered, [this, tag, action]() {
                if (action->isChecked()) {
                    tagSelection({tag});
                } else {
                    untagSelection({tag});
                }
            });
        }
        
        tagMenu->addSeparator();
        tagMenu->addAction(tr("Add New Tag..."), [this]() {
            bool ok;
            QString tag = QInputDialog::getText(
                this, tr("Add Tag"),
                tr("Tag name:"),
                QLineEdit::Normal, "", &ok
            );
            if (ok && !tag.isEmpty()) {
                tagSelection({tag});
            }
        });
    });
    
    contextMenu_->addSeparator();
    
    contextMenu_->addAction(UIUtils::icon("edit-copy"), tr("Copy Address"),
                           [this]() {
        if (!selectedEntries_.isEmpty()) {
            QApplication::clipboard()->setText(entry(selectedEntries_.first()).address);
        }
    });
    
    contextMenu_->addAction(UIUtils::icon("edit-copy"), tr("Copy Analysis"),
                           [this]() {
        if (!selectedEntries_.isEmpty()) {
            QApplication::clipboard()->setText(entry(selectedEntries_.first()).analysis);
        }
    });
    
    contextMenu_->addSeparator();
    
    contextMenu_->addAction(UIUtils::icon("edit-delete"), tr("Delete"),
                           [this]() { deleteSelection(); });
}

void MemoryDock::addEntry(const MemoryEntry& entry) {
    model_->addEntry(entry);
    
    // Update views
    if (currentViewMode_ == "graph") {
        graphView_->setEntries(model_->entries());
    } else if (currentViewMode_ == "heatmap") {
        heatmapView_->setEntries(model_->entries());
    }
}

void MemoryDock::updateEntry(const QUuid& id, const MemoryEntry& entry) {
    model_->updateEntry(id, entry);
    
    // Update views
    if (currentViewMode_ == "graph") {
        graphView_->setEntries(model_->entries());
    } else if (currentViewMode_ == "heatmap") {
        heatmapView_->setEntries(model_->entries());
    }
}

void MemoryDock::removeEntry(const QUuid& id) {
    model_->removeEntry(id);
    selectedEntries_.removeOne(id);
    
    // Update views
    if (currentViewMode_ == "graph") {
        graphView_->setEntries(model_->entries());
    } else if (currentViewMode_ == "heatmap") {
        heatmapView_->setEntries(model_->entries());
    }
}

void MemoryDock::clearEntries() {
    model_->clearEntries();
    selectedEntries_.clear();
    
    // Update views
    if (currentViewMode_ == "graph") {
        graphView_->setEntries({});
    } else if (currentViewMode_ == "heatmap") {
        heatmapView_->setEntries({});
    }
}

QList<MemoryEntry> MemoryDock::entries() const {
    return model_->entries();
}

MemoryEntry MemoryDock::entry(const QUuid& id) const {
    return model_->entry(id);
}

void MemoryDock::setViewMode(const QString& mode) {
    int index = 0;
    if (mode == "table") index = 1;
    else if (mode == "graph") index = 2;
    else if (mode == "heatmap") index = 3;
    
    viewModeCombo_->setCurrentIndex(index);
}

void MemoryDock::showEntry(const QUuid& id) {
    if (currentViewMode_ == "tree" || currentViewMode_ == "table") {
        // Find in model
        for (int row = 0; row < proxyModel_->rowCount(); ++row) {
            QModelIndex index = proxyModel_->index(row, 0);
            if (index.data(MemoryModel::IdRole).toUuid() == id) {
                if (currentViewMode_ == "tree") {
                    treeView_->scrollTo(index);
                    treeView_->setCurrentIndex(index);
                } else {
                    tableView_->scrollTo(index);
                    tableView_->setCurrentIndex(index);
                }
                break;
            }
        }
    } else if (currentViewMode_ == "graph") {
        graphView_->centerOnEntry(id);
        graphView_->highlightEntry(id);
    }
}

void MemoryDock::selectEntry(const QUuid& id) {
    selectedEntries_.clear();
    selectedEntries_.append(id);
    showEntry(id);
    emit selectionChanged(selectedEntries_);
}

void MemoryDock::selectEntries(const QList<QUuid>& ids) {
    selectedEntries_ = ids;
    
    if (currentViewMode_ == "tree" || currentViewMode_ == "table") {
        QItemSelectionModel* selModel = currentViewMode_ == "tree" 
            ? treeView_->selectionModel() 
            : tableView_->selectionModel();
        
        selModel->clear();
        
        for (const QUuid& id : ids) {
            for (int row = 0; row < proxyModel_->rowCount(); ++row) {
                QModelIndex index = proxyModel_->index(row, 0);
                if (index.data(MemoryModel::IdRole).toUuid() == id) {
                    selModel->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
                    break;
                }
            }
        }
    }
    
    emit selectionChanged(selectedEntries_);
}

void MemoryDock::setFilter(const QString& text) {
    searchEdit_->setText(text);
}

void MemoryDock::setTagFilter(const QStringList& tags) {
    tagFilters_ = tags;
    applyFilters();
}

void MemoryDock::setDateRangeFilter(const QDateTime& start, const QDateTime& end) {
    startDateFilter_ = start;
    endDateFilter_ = end;
    applyFilters();
}

void MemoryDock::clearFilters() {
    searchEdit_->clear();
    tagFilters_.clear();
    startDateFilter_ = QDateTime();
    endDateFilter_ = QDateTime();
    applyFilters();
}

void MemoryDock::saveQuery(const QString& name) {
    QJsonObject query;
    query["search"] = searchText_;
    query["tags"] = QJsonArray::fromStringList(tagFilters_);
    query["startDate"] = startDateFilter_.toString(Qt::ISODate);
    query["endDate"] = endDateFilter_.toString(Qt::ISODate);
    
    savedQueries_[name] = query;
    saveSettings();
}

void MemoryDock::loadQuery(const QString& name) {
    if (savedQueries_.contains(name)) {
        QJsonObject query = savedQueries_[name];
        
        searchEdit_->setText(query["search"].toString());
        
        tagFilters_.clear();
        for (const QJsonValue& tag : query["tags"].toArray()) {
            tagFilters_.append(tag.toString());
        }
        
        startDateFilter_ = QDateTime::fromString(query["startDate"].toString(), Qt::ISODate);
        endDateFilter_ = QDateTime::fromString(query["endDate"].toString(), Qt::ISODate);
        
        applyFilters();
    }
}

QStringList MemoryDock::savedQueries() const {
    return savedQueries_.keys();
}

void MemoryDock::deleteQuery(const QString& name) {
    savedQueries_.remove(name);
    saveSettings();
}


void MemoryDock::tagSelection(const QStringList& tags) {
    for (const QUuid& id : selectedEntries_) {
        MemoryEntry e = entry(id);
        for (const QString& tag : tags) {
            if (!e.tags.contains(tag)) {
                e.tags.append(tag);
            }
        }
        updateEntry(id, e);
    }
}

void MemoryDock::untagSelection(const QStringList& tags) {
    for (const QUuid& id : selectedEntries_) {
        MemoryEntry e = entry(id);
        for (const QString& tag : tags) {
            e.tags.removeAll(tag);
        }
        updateEntry(id, e);
    }
}

void MemoryDock::deleteSelection() {
    if (selectedEntries_.isEmpty()) return;
    
    auto reply = QMessageBox::question(
        this, tr("Delete Entries"),
        tr("Delete %1 selected entries?").arg(selectedEntries_.size()),
        QMessageBox::Yes | QMessageBox::No
    );
    
    if (reply == QMessageBox::Yes) {
        for (const QUuid& id : selectedEntries_) {
            removeEntry(id);
        }
    }
}

void MemoryDock::bookmarkSelection(bool bookmark) {
    for (const QUuid& id : selectedEntries_) {
        MemoryEntry e = entry(id);
        e.isBookmarked = bookmark;
        updateEntry(id, e);
    }
}

void MemoryDock::refreshView() {
    // Refresh current view
    if (currentViewMode_ == "graph") {
        graphView_->setEntries(model_->entries());
    } else if (currentViewMode_ == "heatmap") {
        heatmapView_->setEntries(model_->entries());
    }
    
    updateStatusBar();
}

void MemoryDock::importData(const QString& path) {
    QString fileName = path;
    if (fileName.isEmpty()) {
        fileName = QFileDialog::getOpenFileName(
            this, tr("Import Memory Data"),
            "",
            tr("JSON Files (*.json);;CSV Files (*.csv);;All Files (*)")
        );
    }
    
    if (!fileName.isEmpty()) {
        QFile file(fileName);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QMessageBox::warning(this, tr("Import Failed"),
                               tr("Could not open file for reading."));
            return;
        }
        
        QTextStream stream(&file);
        QString content = stream.readAll();
        file.close();
        
        if (fileName.endsWith(".json")) {
            QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
            if (doc.isArray()) {
                for (const QJsonValue& val : doc.array()) {
                    QJsonObject obj = val.toObject();
                    
                    MemoryEntry entry;
                    entry.id = QUuid(obj["id"].toString());
                    if (entry.id.isNull()) {
                        entry.id = QUuid::createUuid();
                    }
                    entry.address = obj["address"].toString();
                    entry.function = obj["function"].toString();
                    entry.module = obj["module"].toString();
                    entry.analysis = obj["analysis"].toString();
                    
                    for (const QJsonValue& tag : obj["tags"].toArray()) {
                        entry.tags.append(tag.toString());
                    }
                    
                    entry.timestamp = QDateTime::fromString(obj["timestamp"].toString(), Qt::ISODate);
                    entry.confidence = obj["confidence"].toInt();
                    entry.isBookmarked = obj["isBookmarked"].toBool();
                    entry.metadata = obj["metadata"].toObject();
                    
                    addEntry(entry);
                }
            }
        }
        
        // Add to recent imports
        recentImports_.removeAll(fileName);
        recentImports_.prepend(fileName);
        while (recentImports_.size() > 10) {
            recentImports_.removeLast();
        }
        saveSettings();
    }
}

void MemoryDock::onThemeChanged() {
    // Update views
    updateStatusBar();
}

void MemoryDock::onViewModeChanged(int index) {
    QStringList modes = {"tree", "table", "graph", "heatmap"};
    if (index < modes.size()) {
        currentViewMode_ = modes[index];
        viewStack_->setCurrentIndex(index);
        
        // Update group by visibility
        groupByCombo_->setVisible(index < 2); // Only for tree/table
        
        // Update data for graph/heatmap views
        if (currentViewMode_ == "graph") {
            graphView_->setEntries(model_->entries());
        } else if (currentViewMode_ == "heatmap") {
            heatmapView_->setEntries(model_->entries());
        }
        
        emit viewModeChanged(currentViewMode_);
    }
}

void MemoryDock::onSearchTextChanged(const QString& text) {
    searchText_ = text;
    applyFilters();
}

void MemoryDock::onAdvancedFilterClicked() {
    auto* dialog = new MemoryFilterDialog(this);
    dialog->setFilters(searchText_, tagFilters_, startDateFilter_, endDateFilter_);
    dialog->setAvailableTags(model_->allTags());
    
    if (dialog->exec() == QDialog::Accepted) {
        searchText_ = dialog->searchText();
        tagFilters_ = dialog->selectedTags();
        startDateFilter_ = dialog->startDate();
        endDateFilter_ = dialog->endDate();
        
        searchEdit_->setText(searchText_);
        applyFilters();
    }
    
    dialog->deleteLater();
}


void MemoryDock::onImportClicked() {
    importData();
}

void MemoryDock::onEntryActivated(const QModelIndex& index) {
    if (index.isValid()) {
        QUuid id = index.data(MemoryModel::IdRole).toUuid();
        emit entryDoubleClicked(id);
    }
}

void MemoryDock::onSelectionChanged() {
    selectedEntries_.clear();
    
    QItemSelectionModel* selModel = nullptr;
    if (currentViewMode_ == "tree") {
        selModel = treeView_->selectionModel();
    } else if (currentViewMode_ == "table") {
        selModel = tableView_->selectionModel();
    }
    
    if (selModel) {
        for (const QModelIndex& index : selModel->selectedRows()) {
            QUuid id = index.data(MemoryModel::IdRole).toUuid();
            if (!id.isNull()) {
                selectedEntries_.append(id);
            }
        }
    }
    
    // Update bookmark action state
    if (!selectedEntries_.isEmpty()) {
        bool allBookmarked = true;
        for (const QUuid& id : selectedEntries_) {
            if (!entry(id).isBookmarked) {
                allBookmarked = false;
                break;
            }
        }
        bookmarkAction_->setChecked(allBookmarked);
    }
    
    emit selectionChanged(selectedEntries_);
    updateStatusBar();
}

void MemoryDock::onCustomContextMenu(const QPoint& pos) {
    Q_UNUSED(pos);
    
    if (!selectedEntries_.isEmpty()) {
        // Update bookmark state
        bool allBookmarked = true;
        for (const QUuid& id : selectedEntries_) {
            if (!entry(id).isBookmarked) {
                allBookmarked = false;
                break;
            }
        }
        
        for (QAction* action : contextMenu_->actions()) {
            if (action->text() == tr("Bookmark")) {
                action->setChecked(allBookmarked);
                break;
            }
        }
        
        contextMenu_->exec(QCursor::pos());
    }
}

void MemoryDock::onGraphEntryClicked(const QUuid& id) {
    selectEntry(id);
    emit entryClicked(id);
}

void MemoryDock::onHeatmapCellClicked(const QString& group, const QString& subgroup) {
    Q_UNUSED(subgroup);
    
    // Filter entries by group
    QList<QUuid> matchingIds;
    for (const MemoryEntry& entry : model_->entries()) {
        QString entryGroup;
        
        if (groupBy_ == "function") {
            entryGroup = entry.function.isEmpty() ? "Unknown" : entry.function;
        } else if (groupBy_ == "module") {
            entryGroup = entry.module.isEmpty() ? "Unknown" : entry.module;
        } else if (groupBy_ == "tag") {
            entryGroup = entry.tags.isEmpty() ? "Untagged" : entry.tags.first();
        }
        
        if (entryGroup == group) {
            matchingIds.append(entry.id);
        }
    }
    
    selectEntries(matchingIds);
}

void MemoryDock::updateStatusBar() {
    QString status = tr("Total: %1 entries").arg(model_->totalEntries());
    
    if (!selectedEntries_.isEmpty()) {
        status += tr(" | Selected: %1").arg(selectedEntries_.size());
    }
    
    int bookmarked = model_->bookmarkedCount();
    if (bookmarked > 0) {
        status += tr(" | Bookmarked: %1").arg(bookmarked);
    }
    
    statusLabel_->setText(status);
}

void MemoryDock::applyFilters() {
    // Text filter
    proxyModel_->setFilterFixedString(searchText_);
    
    // Additional filters would be implemented here
    // For now, just emit signal
    emit filterChanged();
}

void MemoryDock::saveSettings() {
    QSettings settings;
    settings.beginGroup("MemoryDock");
    
    settings.setValue("viewMode", currentViewMode_);
    settings.setValue("groupBy", groupByCombo_->currentText());
    settings.setValue("recentImports", recentImports_);
    
    // Save queries
    QJsonObject queries;
    for (auto it = savedQueries_.begin(); it != savedQueries_.end(); ++it) {
        queries[it.key()] = it.value();
    }
    settings.setValue("savedQueries", QJsonDocument(queries).toJson());
    
    settings.endGroup();
}

void MemoryDock::loadSettings() {
    QSettings settings;
    settings.beginGroup("MemoryDock");
    
    setViewMode(settings.value("viewMode", "tree").toString());
    
    QString groupBy = settings.value("groupBy", "Module").toString();
    int index = groupByCombo_->findText(groupBy);
    if (index >= 0) {
        groupByCombo_->setCurrentIndex(index);
    }
    
    recentImports_ = settings.value("recentImports").toStringList();
    
    // Load queries
    QJsonDocument doc = QJsonDocument::fromJson(settings.value("savedQueries").toByteArray());
    if (doc.isObject()) {
        QJsonObject queries = doc.object();
        for (auto it = queries.begin(); it != queries.end(); ++it) {
            savedQueries_[it.key()] = it.value().toObject();
        }
    }
    
    settings.endGroup();
}

// MemoryFilterDialog implementation

MemoryFilterDialog::MemoryFilterDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Advanced Filter"));
    setModal(true);
    resize(400, 500);
    
    setupUI();
}

void MemoryFilterDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);
    
    // Search text
    auto* searchGroup = new QGroupBox(tr("Search Text"), this);
    auto* searchLayout = new QVBoxLayout(searchGroup);
    
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(tr("Enter search text..."));
    searchLayout->addWidget(searchEdit_);
    
    layout->addWidget(searchGroup);
    
    // Tags
    auto* tagsGroup = new QGroupBox(tr("Tags"), this);
    auto* tagsLayout = new QVBoxLayout(tagsGroup);
    
    tagsList_ = new QListWidget(this);
    tagsList_->setSelectionMode(QAbstractItemView::MultiSelection);
    tagsLayout->addWidget(tagsList_);
    
    layout->addWidget(tagsGroup);
    
    // Date range
    auto* dateGroup = new QGroupBox(tr("Date Range"), this);
    auto* dateLayout = new QFormLayout(dateGroup);
    
    startDateEdit_ = new QDateTimeEdit(this);
    startDateEdit_->setCalendarPopup(true);
    startDateEdit_->setDateTime(QDateTime::currentDateTime().addMonths(-1));
    dateLayout->addRow(tr("From:"), startDateEdit_);
    
    endDateEdit_ = new QDateTimeEdit(this);
    endDateEdit_->setCalendarPopup(true);
    endDateEdit_->setDateTime(QDateTime::currentDateTime());
    dateLayout->addRow(tr("To:"), endDateEdit_);
    
    layout->addWidget(dateGroup);
    
    // Additional filters
    auto* additionalGroup = new QGroupBox(tr("Additional Filters"), this);
    auto* additionalLayout = new QFormLayout(additionalGroup);
    
    confidenceCombo_ = new QComboBox(this);
    confidenceCombo_->addItems({tr("Any"), tr(">= 80%"), tr(">= 50%"), tr("< 50%")});
    additionalLayout->addRow(tr("Confidence:"), confidenceCombo_);
    
    bookmarkedOnlyCheck_ = new QCheckBox(tr("Bookmarked only"), this);
    additionalLayout->addRow(bookmarkedOnlyCheck_);
    
    layout->addWidget(additionalGroup);
    
    // Buttons
    auto* buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        this
    );
    layout->addWidget(buttonBox);
    
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void MemoryFilterDialog::setFilters(const QString& text, const QStringList& tags,
                                   const QDateTime& startDate, const QDateTime& endDate) {
    searchEdit_->setText(text);
    
    // Select tags
    for (int i = 0; i < tagsList_->count(); ++i) {
        QListWidgetItem* item = tagsList_->item(i);
        item->setSelected(tags.contains(item->text()));
    }
    
    if (startDate.isValid()) {
        startDateEdit_->setDateTime(startDate);
    }
    
    if (endDate.isValid()) {
        endDateEdit_->setDateTime(endDate);
    }
}

QString MemoryFilterDialog::searchText() const {
    return searchEdit_->text();
}

QStringList MemoryFilterDialog::selectedTags() const {
    QStringList tags;
    for (QListWidgetItem* item : tagsList_->selectedItems()) {
        tags.append(item->text());
    }
    return tags;
}

QDateTime MemoryFilterDialog::startDate() const {
    return startDateEdit_->dateTime();
}

QDateTime MemoryFilterDialog::endDate() const {
    return endDateEdit_->dateTime();
}

void MemoryFilterDialog::setAvailableTags(const QStringList& tags) {
    tagsList_->clear();
    tagsList_->addItems(tags);
}

// MemoryModel implementation

MemoryModel::MemoryModel(QObject* parent)
    : QAbstractItemModel(parent)
{
    rootNode_ = new TreeNode;
    rootNode_->name = "Root";
}

MemoryModel::~MemoryModel() {
    clearTree();
    delete rootNode_;
}

QModelIndex MemoryModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }
    
    TreeNode* parentNode = nodeForIndex(parent);
    if (!parentNode) {
        parentNode = rootNode_;
    }
    
    if (row < parentNode->children.size()) {
        return createIndex(row, column, parentNode->children[row]);
    }
    
    return QModelIndex();
}

QModelIndex MemoryModel::parent(const QModelIndex& child) const {
    if (!child.isValid()) {
        return QModelIndex();
    }
    
    TreeNode* childNode = static_cast<TreeNode*>(child.internalPointer());
    TreeNode* parentNode = childNode->parent;
    
    if (!parentNode || parentNode == rootNode_) {
        return QModelIndex();
    }
    
    // Find row of parent
    TreeNode* grandParent = parentNode->parent;
    if (grandParent) {
        int row = grandParent->children.indexOf(parentNode);
        return createIndex(row, 0, parentNode);
    }
    
    return QModelIndex();
}

int MemoryModel::rowCount(const QModelIndex& parent) const {
    TreeNode* parentNode = nodeForIndex(parent);
    if (!parentNode) {
        parentNode = rootNode_;
    }
    
    return parentNode->children.size();
}

int MemoryModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return ColumnCount;
}

QVariant MemoryModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid()) {
        return QVariant();
    }
    
    TreeNode* node = static_cast<TreeNode*>(index.internalPointer());
    if (!node) {
        return QVariant();
    }
    
    if (node->isGroup) {
        // Group node
        if (role == Qt::DisplayRole) {
            if (index.column() == 0) {
                return QString("%1 (%2)").arg(node->name).arg(node->children.size());
            }
        } else if (role == Qt::FontRole) {
            QFont font;
            font.setBold(true);
            return font;
        }
        return QVariant();
    }
    
    // Entry node
    if (!node->entry) {
        return QVariant();
    }
    
    const MemoryEntry& entry = *node->entry;
    
    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case AddressColumn:
            return entry.address;
        case FunctionColumn:
            return entry.function;
        case ModuleColumn:
            return entry.module;
        case TagsColumn:
            return entry.tags.join(", ");
        case TimestampColumn:
            return entry.timestamp.toString("yyyy-MM-dd hh:mm");
        case ConfidenceColumn:
            return QString("%1%").arg(entry.confidence);
        }
    } else if (role == Qt::DecorationRole && index.column() == 0) {
        if (entry.isBookmarked) {
            return UIUtils::icon("bookmark");
        }
    } else if (role == Qt::ForegroundRole) {
        if (entry.confidence < 50) {
            return QColor("#F44336");
        }
    } else if (role == EntryRole) {
        return QVariant::fromValue(entry);
    } else if (role == IdRole) {
        return entry.id;
    } else if (role == BookmarkedRole) {
        return entry.isBookmarked;
    } else if (role == ConfidenceRole) {
        return entry.confidence;
    }
    
    return QVariant();
}

QVariant MemoryModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    
    switch (section) {
    case AddressColumn:
        return tr("Address");
    case FunctionColumn:
        return tr("Function");
    case ModuleColumn:
        return tr("Module");
    case TagsColumn:
        return tr("Tags");
    case TimestampColumn:
        return tr("Timestamp");
    case ConfidenceColumn:
        return tr("Confidence");
    }
    
    return QVariant();
}

Qt::ItemFlags MemoryModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    
    TreeNode* node = static_cast<TreeNode*>(index.internalPointer());
    if (node && node->isGroup) {
        return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    }
    
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable;
}

bool MemoryModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }
    
    TreeNode* node = static_cast<TreeNode*>(index.internalPointer());
    if (!node || !node->entry) {
        return false;
    }
    
    // Handle editing based on column
    switch (index.column()) {
    case FunctionColumn:
        node->entry->function = value.toString();
        break;
    case TagsColumn:
        node->entry->tags = value.toString().split(",", Qt::SkipEmptyParts);
        break;
    default:
        return false;
    }
    
    emit dataChanged(index, index);
    return true;
}

void MemoryModel::addEntry(const MemoryEntry& entry) {
    beginResetModel();
    
    entries_.append(entry);
    entryMap_[entry.id] = &entries_.last();
    
    rebuildTree();
    
    endResetModel();
    emit entryAdded(entry.id);
}

void MemoryModel::updateEntry(const QUuid& id, const MemoryEntry& entry) {
    if (entryMap_.contains(id)) {
        beginResetModel();
        
        *entryMap_[id] = entry;
        rebuildTree();
        
        endResetModel();
        emit entryUpdated(id);
    }
}

void MemoryModel::removeEntry(const QUuid& id) {
    if (entryMap_.contains(id)) {
        beginResetModel();
        
        entries_.removeOne(*entryMap_[id]);
        entryMap_.remove(id);
        rebuildTree();
        
        endResetModel();
        emit entryRemoved(id);
    }
}

void MemoryModel::clearEntries() {
    beginResetModel();
    
    entries_.clear();
    entryMap_.clear();
    clearTree();
    
    endResetModel();
    emit modelReset();
}

MemoryEntry MemoryModel::entry(const QUuid& id) const {
    if (entryMap_.contains(id)) {
        return *entryMap_[id];
    }
    return MemoryEntry();
}

void MemoryModel::setGroupBy(const QString& field) {
    if (groupBy_ != field) {
        beginResetModel();
        groupBy_ = field;
        rebuildTree();
        endResetModel();
    }
}

int MemoryModel::bookmarkedCount() const {
    int count = 0;
    for (const MemoryEntry& entry : entries_) {
        if (entry.isBookmarked) {
            count++;
        }
    }
    return count;
}

QStringList MemoryModel::allTags() const {
    QStringList tags;
    for (const MemoryEntry& entry : entries_) {
        for (const QString& tag : entry.tags) {
            if (!tags.contains(tag)) {
                tags.append(tag);
            }
        }
    }
    tags.sort();
    return tags;
}

QStringList MemoryModel::allModules() const {
    QStringList modules;
    for (const MemoryEntry& entry : entries_) {
        if (!entry.module.isEmpty() && !modules.contains(entry.module)) {
            modules.append(entry.module);
        }
    }
    modules.sort();
    return modules;
}

QStringList MemoryModel::allFunctions() const {
    QStringList functions;
    for (const MemoryEntry& entry : entries_) {
        if (!entry.function.isEmpty() && !functions.contains(entry.function)) {
            functions.append(entry.function);
        }
    }
    functions.sort();
    return functions;
}

void MemoryModel::rebuildTree() {
    clearTree();
    
    if (groupBy_.isEmpty()) {
        // No grouping - flat list
        for (MemoryEntry& entry : entries_) {
            TreeNode* node = new TreeNode;
            node->parent = rootNode_;
            node->entry = &entry;
            rootNode_->children.append(node);
        }
    } else {
        // Group entries
        QHash<QString, TreeNode*> groups;
        
        for (MemoryEntry& entry : entries_) {
            QString groupName;
            
            if (groupBy_ == "module") {
                groupName = entry.module.isEmpty() ? tr("Unknown") : entry.module;
            } else if (groupBy_ == "function") {
                groupName = entry.function.isEmpty() ? tr("Unknown") : entry.function;
            } else if (groupBy_ == "tag") {
                groupName = entry.tags.isEmpty() ? tr("Untagged") : entry.tags.first();
            }
            
            if (!groups.contains(groupName)) {
                TreeNode* groupNode = new TreeNode;
                groupNode->name = groupName;
                groupNode->parent = rootNode_;
                groupNode->isGroup = true;
                rootNode_->children.append(groupNode);
                groups[groupName] = groupNode;
            }
            
            TreeNode* node = new TreeNode;
            node->parent = groups[groupName];
            node->entry = &entry;
            groups[groupName]->children.append(node);
        }
    }
}

void MemoryModel::clearTree() {
    // Delete all nodes except root
    std::function<void(TreeNode*)> deleteNode = [&deleteNode](TreeNode* node) {
        for (TreeNode* child : node->children) {
            deleteNode(child);
        }
        node->children.clear();
    };
    
    deleteNode(rootNode_);
}

MemoryModel::TreeNode* MemoryModel::nodeForIndex(const QModelIndex& index) const {
    if (index.isValid()) {
        return static_cast<TreeNode*>(index.internalPointer());
    }
    return nullptr;
}

QModelIndex MemoryModel::indexForNode(TreeNode* node) const {
    if (!node || node == rootNode_) {
        return QModelIndex();
    }
    
    TreeNode* parent = node->parent;
    if (parent) {
        int row = parent->children.indexOf(node);
        if (row >= 0) {
            return createIndex(row, 0, node);
        }
    }
    
    return QModelIndex();
}

} // namespace llm_re::ui_v2