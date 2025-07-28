#include "../core/ui_v2_common.h"
#include "tool_execution_visualizer.h"
#include "../core/theme_manager.h"
#include "../core/ui_constants.h"
#include "../core/ui_utils.h"

namespace llm_re::ui_v2 {

// ToolExecutionVisualizer implementation

ToolExecutionVisualizer::ToolExecutionVisualizer(QWidget* parent)
    : BaseStyledWidget(parent)
{
    setupUI();
    
    // Setup animation timer
    animationTimer_ = new QTimer(this);
    animationTimer_->setInterval(50); // 20 FPS
    connect(animationTimer_, &QTimer::timeout, this, &ToolExecutionVisualizer::updateAnimation);
    animationTimer_->start();
}

ToolExecutionVisualizer::~ToolExecutionVisualizer() {
    clearExecutions();
}

void ToolExecutionVisualizer::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    // Create graphics scene and view
    scene_ = new QGraphicsScene(this);
    scene_->setSceneRect(-500, -500, 1000, 1000);
    
    view_ = new QGraphicsView(scene_, this);
    view_->setRenderHint(QPainter::Antialiasing);
    view_->setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
    view_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    view_->setDragMode(QGraphicsView::RubberBandDrag);
    view_->setBackgroundBrush(Qt::transparent);
    
    layout->addWidget(view_);
    
    // Apply theme
    onThemeChanged();
}

void ToolExecutionVisualizer::addExecution(const ToolExecution& execution) {
    if (nodes_.find(execution.id) != nodes_.end()) {
        updateExecution(execution.id, execution);
        return;
    }
    
    createNode(execution);
    
    if (autoArrange_) {
        arrangeNodes();
    }
    
    calculateGlobalProgress();
    emit progressChanged(globalProgress_);
}

void ToolExecutionVisualizer::updateExecution(const QUuid& id, const ToolExecution& execution) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return;
    }
    
    updateNode(id, execution);
    calculateGlobalProgress();
    emit progressChanged(globalProgress_);
}

void ToolExecutionVisualizer::removeExecution(const QUuid& id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) {
        return;
    }
    
    removeNode(id);
    
    if (autoArrange_) {
        arrangeNodes();
    }
    
    calculateGlobalProgress();
    emit progressChanged(globalProgress_);
}

void ToolExecutionVisualizer::clearExecutions() {
    // Remove all connections
    for (auto* connection : connections_) {
        scene_->removeItem(connection);
        delete connection;
    }
    connections_.clear();
    
    // Remove all nodes
    for (auto& [id, node] : nodes_) {
        animateNodeRemoval(node);
    }
    nodes_.clear();
    
    globalProgress_ = 0.0;
    emit progressChanged(globalProgress_);
}

void ToolExecutionVisualizer::setMode(VisualizationMode mode) {
    if (mode_ == mode) return;
    
    mode_ = mode;
    updateVisualization();
    
    if (autoArrange_) {
        arrangeNodes();
    }
}

void ToolExecutionVisualizer::setGlobalProgress(qreal progress) {
    globalProgress_ = qBound(0.0, progress, 1.0);
    update();
}

void ToolExecutionVisualizer::zoomIn() {
    view_->scale(1.2, 1.2);
}

void ToolExecutionVisualizer::zoomOut() {
    view_->scale(0.8, 0.8);
}

void ToolExecutionVisualizer::resetZoom() {
    view_->resetTransform();
}

void ToolExecutionVisualizer::fitInView() {
    if (scene_->items().isEmpty()) return;
    
    QRectF bounds = scene_->itemsBoundingRect();
    bounds.adjust(-50, -50, 50, 50);
    view_->fitInView(bounds, Qt::KeepAspectRatio);
}

void ToolExecutionVisualizer::arrangeNodes() {
    switch (mode_) {
        case CircularProgress:
            arrangeCircular();
            break;
        case FlowDiagram:
            arrangeFlow();
            break;
        case Timeline:
            arrangeTimeline();
            break;
        case RadialTree:
            arrangeRadial();
            break;
        case Grid:
            arrangeGrid();
            break;
    }
    
    updateConnections();
}

void ToolExecutionVisualizer::highlightExecution(const QUuid& id) {
    unhighlightAll();
    
    auto it = nodes_.find(id);
    if (it != nodes_.end()) {
        it->second->setHighlighted(true);
        highlightedId_ = id;
        
        // Center on highlighted node
        view_->centerOn(it->second);
    }
}

void ToolExecutionVisualizer::unhighlightAll() {
    for (auto& [id, node] : nodes_) {
        node->setHighlighted(false);
    }
    highlightedId_ = QUuid();
}

void ToolExecutionVisualizer::resizeEvent(QResizeEvent* event) {
    BaseStyledWidget::resizeEvent(event);
    fitInView();
}

void ToolExecutionVisualizer::onThemeChanged() {
    BaseStyledWidget::onThemeChanged();
    
    // Update view background
    view_->setStyleSheet(QString("QGraphicsView { background-color: %1; border: none; }")
        .arg(ThemeManager::instance().color("background").name()));
    
    // Update all nodes
    for (auto& [id, node] : nodes_) {
        node->update();
    }
    
    // Update all connections
    for (auto* connection : connections_) {
        connection->update();
    }
}

void ToolExecutionVisualizer::updateAnimation() {
    animationFrame_++;
    
    // Update running nodes
    for (auto& [id, node] : nodes_) {
        if (node->execution().state == ToolExecutionState::Running) {
            node->update();
        }
    }
    
    // Update animated connections
    for (auto* connection : connections_) {
        connection->setProgress((animationFrame_ % 20) / 20.0);
    }
}

void ToolExecutionVisualizer::onNodeClicked(const QUuid& id) {
    emit executionClicked(id);
}

void ToolExecutionVisualizer::onNodeDoubleClicked(const QUuid& id) {
    emit executionDoubleClicked(id);
}

void ToolExecutionVisualizer::onNodeHovered(const QUuid& id, bool hovered) {
    if (hovered) {
        emit executionHovered(id);
    }
}

void ToolExecutionVisualizer::createNode(const ToolExecution& execution) {
    auto* node = new ToolExecutionNode(execution);
    
    connect(node, &ToolExecutionNode::clicked, this, &ToolExecutionVisualizer::onNodeClicked);
    connect(node, &ToolExecutionNode::doubleClicked, this, &ToolExecutionVisualizer::onNodeDoubleClicked);
    connect(node, &ToolExecutionNode::hovered, this, &ToolExecutionVisualizer::onNodeHovered);
    
    scene_->addItem(node);
    nodes_[execution.id] = node;
    
    animateNodeAppearance(node);
}

void ToolExecutionVisualizer::updateNode(const QUuid& id, const ToolExecution& execution) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return;
    
    it->second->updateExecution(execution);
    animateNodeUpdate(it->second);
}

void ToolExecutionVisualizer::removeNode(const QUuid& id) {
    auto it = nodes_.find(id);
    if (it == nodes_.end()) return;
    
    animateNodeRemoval(it->second);
    nodes_.erase(it);
}

void ToolExecutionVisualizer::updateVisualization() {
    // Update node appearances based on mode
    for (auto& [id, node] : nodes_) {
        node->update();
    }
    
    updateConnections();
}

void ToolExecutionVisualizer::updateConnections() {
    // Clear existing connections
    for (auto* connection : connections_) {
        scene_->removeItem(connection);
        delete connection;
    }
    connections_.clear();
    
    if (!showConnections_) return;
    
    // Create connections based on dependencies
    for (auto& [id, node] : nodes_) {
        for (const QUuid& depId : node->execution().dependencyIds) {
            auto depIt = nodes_.find(depId);
            if (depIt != nodes_.end()) {
                auto* connection = new ConnectionLine(depIt->second, node);
                scene_->addItem(connection);
                connections_.push_back(connection);
                animateConnection(connection);
            }
        }
    }
}

void ToolExecutionVisualizer::calculateGlobalProgress() {
    if (nodes_.empty()) {
        globalProgress_ = 0.0;
        return;
    }
    
    qreal totalProgress = 0.0;
    int completedCount = 0;
    
    for (const auto& [id, node] : nodes_) {
        const ToolExecution& exec = node->execution();
        
        switch (exec.state) {
            case ToolExecutionState::Completed:
                totalProgress += 100.0;
                completedCount++;
                break;
            case ToolExecutionState::Failed:
            case ToolExecutionState::Cancelled:
                completedCount++;
                break;
            case ToolExecutionState::Running:
                totalProgress += exec.progress;
                break;
            default:
                break;
        }
    }
    
    globalProgress_ = totalProgress / (nodes_.size() * 100.0);
}

void ToolExecutionVisualizer::arrangeCircular() {
    if (nodes_.empty()) return;
    
    qreal radius = 200.0;
    qreal angleStep = 2 * M_PI / nodes_.size();
    int index = 0;
    
    for (auto& [id, node] : nodes_) {
        qreal angle = index * angleStep;
        qreal x = radius * cos(angle);
        qreal y = radius * sin(angle);
        
        QPropertyAnimation* anim = new QPropertyAnimation(node, "pos");
        anim->setDuration(Design::ANIM_NORMAL * animationSpeed_ / 5);
        anim->setEndValue(QPointF(x, y));
        anim->setEasingCurve(QEasingCurve::InOutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
        
        index++;
    }
}

void ToolExecutionVisualizer::arrangeFlow() {
    if (nodes_.empty()) return;
    
    // Simple left-to-right flow layout
    std::vector<std::vector<ToolExecutionNode*>> levels;
    std::unordered_map<QUuid, int> nodeLevel;
    
    // Calculate levels based on dependencies
    for (auto& [id, node] : nodes_) {
        int level = 0;
        for (const QUuid& depId : node->execution().dependencyIds) {
            auto it = nodeLevel.find(depId);
            if (it != nodeLevel.end()) {
                level = std::max(level, it->second + 1);
            }
        }
        nodeLevel[id] = level;
        
        if (level >= levels.size()) {
            levels.resize(level + 1);
        }
        levels[level].push_back(node);
    }
    
    // Position nodes
    qreal xSpacing = 150.0;
    qreal ySpacing = 100.0;
    
    for (int level = 0; level < levels.size(); ++level) {
        qreal x = level * xSpacing - (levels.size() - 1) * xSpacing / 2;
        
        for (int i = 0; i < levels[level].size(); ++i) {
            qreal y = i * ySpacing - (levels[level].size() - 1) * ySpacing / 2;
            
            QPropertyAnimation* anim = new QPropertyAnimation(levels[level][i], "pos");
            anim->setDuration(Design::ANIM_NORMAL * animationSpeed_ / 5);
            anim->setEndValue(QPointF(x, y));
            anim->setEasingCurve(QEasingCurve::InOutCubic);
            anim->start(QAbstractAnimation::DeleteWhenStopped);
        }
    }
}

void ToolExecutionVisualizer::arrangeTimeline() {
    if (nodes_.empty()) return;
    
    // Sort nodes by start time
    std::vector<ToolExecutionNode*> sortedNodes;
    for (auto& [id, node] : nodes_) {
        sortedNodes.push_back(node);
    }
    
    std::sort(sortedNodes.begin(), sortedNodes.end(), 
        [](ToolExecutionNode* a, ToolExecutionNode* b) {
            return a->execution().startTime < b->execution().startTime;
        });
    
    // Position along horizontal timeline
    qreal xSpacing = 120.0;
    qreal yBase = 0.0;
    
    for (int i = 0; i < sortedNodes.size(); ++i) {
        qreal x = i * xSpacing - (sortedNodes.size() - 1) * xSpacing / 2;
        qreal y = yBase + (i % 2) * 50; // Alternate heights for overlap
        
        QPropertyAnimation* anim = new QPropertyAnimation(sortedNodes[i], "pos");
        anim->setDuration(Design::ANIM_NORMAL * animationSpeed_ / 5);
        anim->setEndValue(QPointF(x, y));
        anim->setEasingCurve(QEasingCurve::InOutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void ToolExecutionVisualizer::arrangeRadial() {
    if (nodes_.empty()) return;
    
    // Find root nodes (no dependencies)
    std::vector<ToolExecutionNode*> roots;
    for (auto& [id, node] : nodes_) {
        if (node->execution().dependencyIds.isEmpty()) {
            roots.push_back(node);
        }
    }
    
    if (roots.empty()) {
        // If no roots, use circular arrangement
        arrangeCircular();
        return;
    }
    
    // Position root at center
    if (roots.size() == 1) {
        roots[0]->setPos(0, 0);
    } else {
        // Multiple roots in small circle
        qreal rootRadius = 50.0;
        qreal angleStep = 2 * M_PI / roots.size();
        for (int i = 0; i < roots.size(); ++i) {
            qreal angle = i * angleStep;
            roots[i]->setPos(rootRadius * cos(angle), rootRadius * sin(angle));
        }
    }
    
    // Position other nodes in concentric circles
    std::unordered_set<QUuid> positioned;
    for (auto* root : roots) {
        positioned.insert(root->id());
    }
    
    qreal radius = 150.0;
    std::vector<ToolExecutionNode*> currentLevel = roots;
    
    while (!currentLevel.empty()) {
        std::vector<ToolExecutionNode*> nextLevel;
        
        for (auto& [id, node] : nodes_) {
            if (positioned.find(id) != positioned.end()) continue;
            
            // Check if all dependencies are positioned
            bool canPosition = true;
            for (const QUuid& depId : node->execution().dependencyIds) {
                if (positioned.find(depId) == positioned.end()) {
                    canPosition = false;
                    break;
                }
            }
            
            if (canPosition) {
                nextLevel.push_back(node);
                positioned.insert(id);
            }
        }
        
        if (!nextLevel.empty()) {
            qreal angleStep = 2 * M_PI / nextLevel.size();
            for (int i = 0; i < nextLevel.size(); ++i) {
                qreal angle = i * angleStep;
                qreal x = radius * cos(angle);
                qreal y = radius * sin(angle);
                
                QPropertyAnimation* anim = new QPropertyAnimation(nextLevel[i], "pos");
                anim->setDuration(Design::ANIM_NORMAL * animationSpeed_ / 5);
                anim->setEndValue(QPointF(x, y));
                anim->setEasingCurve(QEasingCurve::InOutCubic);
                anim->start(QAbstractAnimation::DeleteWhenStopped);
            }
        }
        
        currentLevel = nextLevel;
        radius += 100.0;
    }
}

void ToolExecutionVisualizer::arrangeGrid() {
    if (nodes_.empty()) return;
    
    int cols = std::ceil(std::sqrt(nodes_.size()));
    int rows = std::ceil(static_cast<double>(nodes_.size()) / cols);
    
    qreal xSpacing = 120.0;
    qreal ySpacing = 120.0;
    
    int index = 0;
    for (auto& [id, node] : nodes_) {
        int row = index / cols;
        int col = index % cols;
        
        qreal x = col * xSpacing - (cols - 1) * xSpacing / 2;
        qreal y = row * ySpacing - (rows - 1) * ySpacing / 2;
        
        QPropertyAnimation* anim = new QPropertyAnimation(node, "pos");
        anim->setDuration(Design::ANIM_NORMAL * animationSpeed_ / 5);
        anim->setEndValue(QPointF(x, y));
        anim->setEasingCurve(QEasingCurve::InOutCubic);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
        
        index++;
    }
}

void ToolExecutionVisualizer::animateNodeAppearance(ToolExecutionNode* node) {
    node->setScale(0.0);
    node->setOpacity(0.0);
    
    auto* scaleAnim = new QPropertyAnimation(node, "scale");
    scaleAnim->setDuration(Design::ANIM_NORMAL);
    scaleAnim->setStartValue(0.0);
    scaleAnim->setEndValue(1.0);
    scaleAnim->setEasingCurve(QEasingCurve::OutBack);
    
    auto* opacityAnim = new QPropertyAnimation(node, "opacity");
    opacityAnim->setDuration(Design::ANIM_NORMAL);
    opacityAnim->setStartValue(0.0);
    opacityAnim->setEndValue(1.0);
    
    auto* group = new QParallelAnimationGroup();
    group->addAnimation(scaleAnim);
    group->addAnimation(opacityAnim);
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToolExecutionVisualizer::animateNodeRemoval(ToolExecutionNode* node) {
    auto* scaleAnim = new QPropertyAnimation(node, "scale");
    scaleAnim->setDuration(Design::ANIM_FAST);
    scaleAnim->setEndValue(0.0);
    scaleAnim->setEasingCurve(QEasingCurve::InBack);
    
    auto* opacityAnim = new QPropertyAnimation(node, "opacity");
    opacityAnim->setDuration(Design::ANIM_FAST);
    opacityAnim->setEndValue(0.0);
    
    auto* group = new QParallelAnimationGroup();
    group->addAnimation(scaleAnim);
    group->addAnimation(opacityAnim);
    
    connect(group, &QParallelAnimationGroup::finished, [this, node]() {
        scene_->removeItem(node);
        node->deleteLater();
    });
    
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void ToolExecutionVisualizer::animateNodeUpdate(ToolExecutionNode* node) {
    if (node->execution().state == ToolExecutionState::Running) {
        node->startPulseAnimation();
    } else {
        node->stopPulseAnimation();
    }
    
    // Animate progress change
    node->startProgressAnimation(node->execution().progress, Design::ANIM_NORMAL);
}

void ToolExecutionVisualizer::animateConnection(ConnectionLine* line) {
    line->setAnimated(true);
}

// ToolExecutionNode implementation

ToolExecutionNode::ToolExecutionNode(const ToolExecution& execution)
    : execution_(execution)
{
    setAcceptHoverEvents(true);
    setFlag(ItemIsSelectable);
    
    updateColors();
    
    if (execution.state == ToolExecutionState::Running) {
        startPulseAnimation();
    }
}

void ToolExecutionNode::updateExecution(const ToolExecution& execution) {
    execution_ = execution;
    updateColors();
    update();
}

void ToolExecutionNode::setProgress(qreal progress) {
    progress_ = qBound(0.0, progress, 100.0);
    update();
}

void ToolExecutionNode::setScale(qreal scale) {
    scale_ = scale;
    setTransform(QTransform::fromScale(scale_, scale_));
}

void ToolExecutionNode::setOpacity(qreal opacity) {
    opacity_ = opacity;
    setGraphicsEffect(nullptr);
    
    if (opacity < 1.0) {
        auto* effect = new QGraphicsOpacityEffect();
        effect->setOpacity(opacity);
        setGraphicsEffect(effect);
    }
}

void ToolExecutionNode::setPulseScale(qreal scale) {
    pulseScale_ = scale;
    update();
}

void ToolExecutionNode::setHighlighted(bool highlighted) {
    highlighted_ = highlighted;
    updateColors();
    update();
}

void ToolExecutionNode::startPulseAnimation() {
    if (pulseAnimation_) return;
    
    pulseAnimation_ = new QPropertyAnimation(this, "pulseScale");
    pulseAnimation_->setDuration(1000);
    pulseAnimation_->setStartValue(1.0);
    pulseAnimation_->setEndValue(1.1);
    pulseAnimation_->setEasingCurve(QEasingCurve::InOutSine);
    pulseAnimation_->setLoopCount(-1);
    pulseAnimation_->start();
}

void ToolExecutionNode::stopPulseAnimation() {
    if (pulseAnimation_) {
        pulseAnimation_->stop();
        pulseAnimation_->deleteLater();
        pulseAnimation_ = nullptr;
        pulseScale_ = 1.0;
        update();
    }
}

void ToolExecutionNode::startProgressAnimation(qreal targetProgress, int duration) {
    if (progressAnimation_) {
        progressAnimation_->stop();
        progressAnimation_->deleteLater();
    }
    
    progressAnimation_ = new QPropertyAnimation(this, "progress");
    progressAnimation_->setDuration(duration);
    progressAnimation_->setStartValue(progress_);
    progressAnimation_->setEndValue(targetProgress);
    progressAnimation_->setEasingCurve(QEasingCurve::InOutQuad);
    progressAnimation_->start(QAbstractAnimation::DeleteWhenStopped);
}

QRectF ToolExecutionNode::boundingRect() const {
    qreal size = radius_ * 2 * pulseScale_;
    return QRectF(-size/2, -size/2, size, size);
}

void ToolExecutionNode::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    QRectF rect = boundingRect();
    
    // Draw glow effect for running/highlighted nodes
    if (execution_.state == ToolExecutionState::Running || highlighted_ || hovered_) {
        drawGlow(painter, rect);
    }
    
    // Draw main circle
    drawCircularProgress(painter, rect.adjusted(10, 10, -10, -10));
    
    // Draw tool icon
    drawToolIcon(painter, rect);
    
    // Draw status icon
    drawStatusIcon(painter, rect);
    
    // Draw labels
    if (scene() && scene()->views().first()) {
        QGraphicsView* view = scene()->views().first();
        qreal scaleFactor = view->transform().m11();
        if (scaleFactor > 0.5) { // Only show labels when zoomed in enough
            drawLabels(painter, rect);
        }
    }
}

QPainterPath ToolExecutionNode::shape() const {
    QPainterPath path;
    path.addEllipse(boundingRect());
    return path;
}

void ToolExecutionNode::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    Q_UNUSED(event);
    hovered_ = true;
    updateColors();
    update();
    emit hovered(execution_.id, true);
}

void ToolExecutionNode::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    Q_UNUSED(event);
    hovered_ = false;
    updateColors();
    update();
    emit hovered(execution_.id, false);
}

void ToolExecutionNode::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked(execution_.id);
    }
}

void ToolExecutionNode::mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit doubleClicked(execution_.id);
    }
}

void ToolExecutionNode::updateColors() {
    auto& theme = ThemeManager::instance();
    
    switch (execution_.state) {
        case ToolExecutionState::Pending:
            primaryColor_ = theme.color("textTertiary");
            secondaryColor_ = theme.color("surface");
            break;
        case ToolExecutionState::Running:
            primaryColor_ = theme.color("info");
            secondaryColor_ = theme.color("primary");
            glowColor_ = theme.color("primary").lighter(150);
            break;
        case ToolExecutionState::Completed:
            primaryColor_ = theme.color("success");
            secondaryColor_ = theme.color("success").darker(150);
            break;
        case ToolExecutionState::Failed:
            primaryColor_ = theme.color("error");
            secondaryColor_ = theme.color("error").darker(150);
            break;
        case ToolExecutionState::Cancelled:
            primaryColor_ = theme.color("warning");
            secondaryColor_ = theme.color("warning").darker(150);
            break;
    }
    
    if (highlighted_) {
        primaryColor_ = primaryColor_.lighter(120);
        glowColor_ = primaryColor_.lighter(150);
    }
    
    if (hovered_) {
        primaryColor_ = primaryColor_.lighter(110);
    }
}

void ToolExecutionNode::drawCircularProgress(QPainter* painter, const QRectF& rect) {
    // Background circle
    painter->setPen(QPen(ThemeManager::instance().color("border"), 2));
    painter->setBrush(ThemeManager::instance().color("surface"));
    painter->drawEllipse(rect);
    
    // Progress arc
    if (progress_ > 0) {
        painter->setPen(QPen(primaryColor_, 4, Qt::SolidLine, Qt::RoundCap));
        painter->setBrush(Qt::NoBrush);
        
        int startAngle = 90 * 16; // Start from top
        int spanAngle = -progress_ * 360 * 16 / 100; // Clockwise
        
        painter->drawArc(rect, startAngle, spanAngle);
    }
    
    // Inner circle
    QRectF innerRect = rect.adjusted(8, 8, -8, -8);
    painter->setPen(Qt::NoPen);
    painter->setBrush(secondaryColor_.darker(150));
    painter->drawEllipse(innerRect);
}

void ToolExecutionNode::drawStatusIcon(QPainter* painter, const QRectF& rect) {
    QRectF iconRect = rect.adjusted(20, 20, -20, -20);
    
    painter->setPen(QPen(ThemeManager::instance().color("textPrimary"), 2));
    painter->setBrush(Qt::NoBrush);
    
    switch (execution_.state) {
        case ToolExecutionState::Pending:
            // Draw clock icon
            painter->drawEllipse(iconRect);
            painter->drawLine(iconRect.center(), iconRect.center() + QPointF(0, -iconRect.height()/4));
            painter->drawLine(iconRect.center(), iconRect.center() + QPointF(iconRect.width()/4, 0));
            break;
            
        case ToolExecutionState::Running:
            // Draw play icon (animated)
            {
                QPolygonF triangle;
                QPointF center = iconRect.center();
                qreal size = iconRect.width() / 3;
                triangle << center + QPointF(-size/2, -size/2)
                        << center + QPointF(-size/2, size/2)
                        << center + QPointF(size/2, 0);
                painter->setBrush(ThemeManager::instance().color("textPrimary"));
                painter->drawPolygon(triangle);
            }
            break;
            
        case ToolExecutionState::Completed:
            // Draw checkmark
            {
                QPainterPath checkPath;
                QPointF center = iconRect.center();
                qreal size = iconRect.width() / 3;
                checkPath.moveTo(center + QPointF(-size/2, 0));
                checkPath.lineTo(center + QPointF(-size/4, size/3));
                checkPath.lineTo(center + QPointF(size/2, -size/3));
                painter->setPen(QPen(ThemeManager::instance().color("textPrimary"), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                painter->drawPath(checkPath);
            }
            break;
            
        case ToolExecutionState::Failed:
            // Draw X
            {
                QPointF center = iconRect.center();
                qreal size = iconRect.width() / 4;
                painter->setPen(QPen(ThemeManager::instance().color("textPrimary"), 3));
                painter->drawLine(center + QPointF(-size, -size), center + QPointF(size, size));
                painter->drawLine(center + QPointF(-size, size), center + QPointF(size, -size));
            }
            break;
            
        case ToolExecutionState::Cancelled:
            // Draw stop square
            {
                QRectF stopRect = iconRect.adjusted(15, 15, -15, -15);
                painter->setBrush(ThemeManager::instance().color("textPrimary"));
                painter->drawRect(stopRect);
            }
            break;
    }
}

void ToolExecutionNode::drawToolIcon(QPainter* painter, const QRectF& rect) {
    // Tool name abbreviation in bottom right
    QString abbrev = execution_.toolName.left(2).toUpper();
    
    QRectF textRect(rect.right() - 30, rect.bottom() - 30, 25, 25);
    painter->setPen(Qt::NoPen);
    painter->setBrush(primaryColor_.darker(120));
    painter->drawEllipse(textRect);
    
    painter->setPen(ThemeManager::instance().color("textInverse"));
    painter->setFont(QFont("Sans", 10, QFont::Bold));
    painter->drawText(textRect, Qt::AlignCenter, abbrev);
}

void ToolExecutionNode::drawLabels(QPainter* painter, const QRectF& rect) {
    painter->setPen(ThemeManager::instance().color("textPrimary"));
    painter->setFont(QFont("Sans", 10));
    
    // Tool name below
    QRectF nameRect(rect.left() - 50, rect.bottom() + 5, rect.width() + 100, 20);
    painter->drawText(nameRect, Qt::AlignCenter, execution_.toolName);
    
    // Progress percentage for running tasks
    if (execution_.state == ToolExecutionState::Running) {
        painter->setFont(QFont("Sans", 8));
        painter->setPen(ThemeManager::instance().color("textSecondary"));
        QRectF progressRect(rect.left() - 50, rect.bottom() + 25, rect.width() + 100, 20);
        painter->drawText(progressRect, Qt::AlignCenter, QString("%1%").arg(execution_.progress));
    }
}

void ToolExecutionNode::drawGlow(QPainter* painter, const QRectF& rect) {
    QRadialGradient gradient(rect.center(), rect.width() / 2);
    gradient.setColorAt(0, glowColor_.lighter(150));
    gradient.setColorAt(0.5, glowColor_);
    gradient.setColorAt(1, Qt::transparent);
    
    painter->setPen(Qt::NoPen);
    painter->setBrush(gradient);
    painter->drawEllipse(rect.adjusted(-10, -10, 10, 10));
}

// ConnectionLine implementation

ConnectionLine::ConnectionLine(ToolExecutionNode* start, ToolExecutionNode* end)
    : startNode_(start), endNode_(end)
{
    setZValue(-1); // Draw below nodes
    updatePosition();
}

void ConnectionLine::updatePosition() {
    prepareGeometryChange();
    
    startPoint_ = startNode_->scenePos();
    endPoint_ = endNode_->scenePos();
    
    // Calculate path
    path_ = QPainterPath();
    path_.moveTo(startPoint_);
    
    // Bezier curve
    QPointF ctrl1 = startPoint_ + QPointF((endPoint_.x() - startPoint_.x()) / 3, 0);
    QPointF ctrl2 = endPoint_ + QPointF((startPoint_.x() - endPoint_.x()) / 3, 0);
    path_.cubicTo(ctrl1, ctrl2, endPoint_);
}

QRectF ConnectionLine::boundingRect() const {
    return path_.boundingRect().adjusted(-5, -5, 5, 5);
}

void ConnectionLine::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    // Draw line
    QColor lineColor = ThemeManager::instance().color("border");
    painter->setPen(QPen(lineColor, 2, Qt::DashLine));
    
    if (animated_) {
        // Animated dashed line
        QVector<qreal> dashes;
        dashes << 10 << 5;
        QPen pen(lineColor, 2);
        pen.setDashPattern(dashes);
        pen.setDashOffset(progress_ * 15);
        painter->setPen(pen);
    }
    
    painter->drawPath(path_);
    
    // Draw arrow head
    QPointF arrowEnd = endPoint_;
    QPointF arrowStart = path_.pointAtPercent(0.9);
    
    qreal angle = std::atan2(arrowEnd.y() - arrowStart.y(), arrowEnd.x() - arrowStart.x());
    QPointF arrowP1 = arrowEnd - QPointF(10 * cos(angle - M_PI/6), 10 * sin(angle - M_PI/6));
    QPointF arrowP2 = arrowEnd - QPointF(10 * cos(angle + M_PI/6), 10 * sin(angle + M_PI/6));
    
    QPolygonF arrowHead;
    arrowHead << arrowEnd << arrowP1 << arrowP2;
    
    painter->setPen(Qt::NoPen);
    painter->setBrush(lineColor);
    painter->drawPolygon(arrowHead);
}

} // namespace llm_re::ui_v2