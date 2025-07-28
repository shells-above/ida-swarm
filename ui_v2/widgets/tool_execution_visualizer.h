#pragma once

#include "../core/base_styled_widget.h"
#include "../views/tool_execution_dock.h"
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QSequentialAnimationGroup>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <memory>
#include <unordered_map>

class QGraphicsScene;
class QGraphicsView;
class QGraphicsItem;
class QGraphicsTextItem;
class QGraphicsEllipseItem;
class QGraphicsRectItem;
class QGraphicsPathItem;

namespace llm_re::ui_v2 {

// Forward declarations
class ToolExecutionNode;
class ConnectionLine;

// Main tool execution visualizer widget
class ToolExecutionVisualizer : public BaseStyledWidget {
    Q_OBJECT
    Q_PROPERTY(qreal globalProgress READ globalProgress WRITE setGlobalProgress)
    
public:
    enum VisualizationMode {
        CircularProgress,    // Circular progress indicators
        FlowDiagram,        // Flow diagram with connections
        Timeline,           // Horizontal timeline
        RadialTree,         // Radial tree layout
        Grid               // Grid layout
    };
    
    explicit ToolExecutionVisualizer(QWidget* parent = nullptr);
    ~ToolExecutionVisualizer() override;
    
    // Execution management
    void addExecution(const ToolExecution& execution);
    void updateExecution(const QUuid& id, const ToolExecution& execution);
    void removeExecution(const QUuid& id);
    void clearExecutions();
    
    // Visualization control
    void setMode(VisualizationMode mode);
    VisualizationMode mode() const { return mode_; }
    
    void setAutoArrange(bool enabled) { autoArrange_ = enabled; }
    bool autoArrange() const { return autoArrange_; }
    
    void setShowLabels(bool show) { showLabels_ = show; updateVisualization(); }
    void setShowConnections(bool show) { showConnections_ = show; updateVisualization(); }
    void setShowProgress(bool show) { showProgress_ = show; updateVisualization(); }
    void setAnimationSpeed(int speed) { animationSpeed_ = qBound(1, speed, 10); }
    
    // Global progress
    qreal globalProgress() const { return globalProgress_; }
    void setGlobalProgress(qreal progress);
    
    // View control
    void zoomIn();
    void zoomOut();
    void resetZoom();
    void fitInView();
    
signals:
    void executionClicked(const QUuid& id);
    void executionDoubleClicked(const QUuid& id);
    void executionHovered(const QUuid& id);
    void progressChanged(qreal progress);
    
public slots:
    void arrangeNodes();
    void highlightExecution(const QUuid& id);
    void unhighlightAll();
    
protected:
    void resizeEvent(QResizeEvent* event) override;
    void onThemeChanged() override;
    
private slots:
    void updateAnimation();
    void onNodeClicked(const QUuid& id);
    void onNodeDoubleClicked(const QUuid& id);
    void onNodeHovered(const QUuid& id, bool hovered);
    
private:
    void setupUI();
    void createNode(const ToolExecution& execution);
    void updateNode(const QUuid& id, const ToolExecution& execution);
    void removeNode(const QUuid& id);
    void updateVisualization();
    void updateConnections();
    void calculateGlobalProgress();
    
    // Layout algorithms
    void arrangeCircular();
    void arrangeFlow();
    void arrangeTimeline();
    void arrangeRadial();
    void arrangeGrid();
    
    // Animation helpers
    void animateNodeAppearance(ToolExecutionNode* node);
    void animateNodeRemoval(ToolExecutionNode* node);
    void animateNodeUpdate(ToolExecutionNode* node);
    void animateConnection(ConnectionLine* line);
    
    // Graphics components
    QGraphicsScene* scene_ = nullptr;
    QGraphicsView* view_ = nullptr;
    
    // Nodes and connections
    std::unordered_map<QUuid, ToolExecutionNode*> nodes_;
    std::vector<ConnectionLine*> connections_;
    
    // State
    VisualizationMode mode_ = CircularProgress;
    bool autoArrange_ = true;
    bool showLabels_ = true;
    bool showConnections_ = true;
    bool showProgress_ = true;
    int animationSpeed_ = 5;
    qreal globalProgress_ = 0.0;
    
    // Animation
    QTimer* animationTimer_ = nullptr;
    int animationFrame_ = 0;
    
    // Highlighted node
    QUuid highlightedId_;
};

// Tool execution node representation
class ToolExecutionNode : public QObject, public QGraphicsItem {
    Q_OBJECT
    Q_PROPERTY(qreal progress READ progress WRITE setProgress)
    Q_PROPERTY(qreal scale READ scale WRITE setScale)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
    Q_PROPERTY(qreal pulseScale READ pulseScale WRITE setPulseScale)
    
public:
    explicit ToolExecutionNode(const ToolExecution& execution);
    
    // Update execution data
    void updateExecution(const ToolExecution& execution);
    
    // Visual properties
    qreal progress() const { return progress_; }
    void setProgress(qreal progress);
    
    qreal scale() const { return scale_; }
    void setScale(qreal scale);
    
    qreal opacity() const { return opacity_; }
    void setOpacity(qreal opacity);
    
    qreal pulseScale() const { return pulseScale_; }
    void setPulseScale(qreal scale);
    
    // Highlighting
    void setHighlighted(bool highlighted);
    bool isHighlighted() const { return highlighted_; }
    
    // Animation
    void startPulseAnimation();
    void stopPulseAnimation();
    void startProgressAnimation(qreal targetProgress, int duration);
    
    // Node properties
    QUuid id() const { return execution_.id; }
    const ToolExecution& execution() const { return execution_; }
    
    // QGraphicsItem interface
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QPainterPath shape() const override;
    
signals:
    void clicked(const QUuid& id);
    void doubleClicked(const QUuid& id);
    void hovered(const QUuid& id, bool hovered);
    
protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    
private:
    void updateColors();
    void drawCircularProgress(QPainter* painter, const QRectF& rect);
    void drawStatusIcon(QPainter* painter, const QRectF& rect);
    void drawToolIcon(QPainter* painter, const QRectF& rect);
    void drawLabels(QPainter* painter, const QRectF& rect);
    void drawGlow(QPainter* painter, const QRectF& rect);
    
    ToolExecution execution_;
    qreal progress_ = 0.0;
    qreal scale_ = 1.0;
    qreal opacity_ = 1.0;
    qreal pulseScale_ = 1.0;
    bool highlighted_ = false;
    bool hovered_ = false;
    
    // Visual properties
    QColor primaryColor_;
    QColor secondaryColor_;
    QColor glowColor_;
    qreal radius_ = 40.0;
    
    // Animations
    QPropertyAnimation* progressAnimation_ = nullptr;
    QPropertyAnimation* pulseAnimation_ = nullptr;
    QParallelAnimationGroup* animationGroup_ = nullptr;
};

// Connection line between nodes
class ConnectionLine : public QGraphicsItem {
public:
    ConnectionLine(ToolExecutionNode* start, ToolExecutionNode* end);
    
    void updatePosition();
    void setAnimated(bool animated) { animated_ = animated; }
    void setProgress(qreal progress) { progress_ = progress; update(); }
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
private:
    ToolExecutionNode* startNode_;
    ToolExecutionNode* endNode_;
    bool animated_ = false;
    qreal progress_ = 1.0;
    QPointF startPoint_;
    QPointF endPoint_;
    QPainterPath path_;
};

} // namespace llm_re::ui_v2