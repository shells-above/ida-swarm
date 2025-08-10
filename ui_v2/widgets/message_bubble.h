#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"
#include "../models/conversation_model.h"
#include "markdown_viewer.h"

namespace llm_re::ui_v2 {

// Modern message bubble widget with animations and rich content support
class MessageBubble : public CardWidget {
    Q_OBJECT
    Q_PROPERTY(qreal expandProgress READ expandProgress WRITE setExpandProgress)
    Q_PROPERTY(qreal fadeProgress READ fadeProgress WRITE setFadeProgress)
    Q_PROPERTY(int typewriterPosition READ typewriterPosition WRITE setTypewriterPosition)

public:
    enum AnimationType {
        NoAnimation,
        FadeIn,
        SlideIn,
        TypeWriter,
        Bounce
    };
    
    enum BubbleStyle {
        Classic,      // Traditional chat bubble
        Modern,       // Flat with subtle shadow
        Minimal,      // Just text with light background
        Terminal,     // Monospace with terminal styling
        Paper         // Note paper style
    };
    
    explicit MessageBubble(UIMessage* message, QWidget* parent = nullptr);
    ~MessageBubble() override;
    
    // Message access
    UIMessage* message() { return message_; }
    const UIMessage* message() const { return message_; }
    
    // Appearance
    void setBubbleStyle(BubbleStyle style);
    BubbleStyle bubbleStyle() const { return bubbleStyle_; }
    
    void setMaxWidth(int width) { maxWidth_ = width; }
    int maxWidth() const { return maxWidth_; }
    
    void setShowTimestamp(bool show);
    bool showTimestamp() const { return showTimestamp_; }
    
    void setShowHeader(bool show);
    bool showHeader() const { return showHeader_; }
    
    
    // Animation
    void setAnimationType(AnimationType type) { animationType_ = type; }
    AnimationType animationType() const { return animationType_; }
    
    void animateIn();
    void animateOut();
    void stopAnimation();
    bool isAnimating() const { return currentAnimation_ != nullptr; }
    
    // Interaction
    void setSelected(bool selected);
    bool isSelected() const { return isSelected_; }
    
    void setHighlighted(bool highlighted);
    bool isHighlighted() const { return isHighlighted_; }
    
    void setInteractive(bool interactive) { interactive_ = interactive; }
    bool isInteractive() const { return interactive_; }
    
    // Expansion
    void setExpanded(bool expanded, bool animated = true);
    bool isExpanded() const { return isExpanded_; }
    void toggleExpanded() { setExpanded(!isExpanded_); }
    
    // Text access
    QString toPlainText() const;
    
    // Animation properties
    qreal expandProgress() const { return expandProgress_; }
    void setExpandProgress(qreal progress);
    
    qreal fadeProgress() const { return fadeProgress_; }
    void setFadeProgress(qreal progress);
    
    int typewriterPosition() const { return typewriterPosition_; }
    void setTypewriterPosition(int position);
    
    // Size hint
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;
    
signals:
    void clicked();
    void doubleClicked();
    void copyRequested();
    void selectionChanged(bool selected);
    void expansionChanged(bool expanded);
    void animationFinished();
    
public slots:
    void updateTheme();

protected:
    void paintEvent(QPaintEvent* event) override;
    void paintContent(QPainter* painter) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void onThemeChanged() override;
    
private slots:
    void onCopyAction();
    void onAnimationFinished();
    
private:
    void setupUI();
    void createHeader();
    void createContent();
    void updateLayout();
    void applyBubbleStyle();
    void paintSelectionOverlay(QPainter* painter);
    
    // Core data
    UIMessage* message_;
    BubbleStyle bubbleStyle_ = BubbleStyle::Modern;
    AnimationType animationType_ = AnimationType::FadeIn;
    
    // Layout components
    QWidget* headerWidget_ = nullptr;
    QWidget* contentWidget_ = nullptr;

    // Header components
    QLabel* nameLabel_ = nullptr;
    QLabel* timestampLabel_ = nullptr;

    // Content components
    MarkdownViewer* contentViewer_ = nullptr;
    QLabel* plainTextLabel_ = nullptr;
    
    // State
    bool isSelected_ = false;
    bool isHighlighted_ = false;
    bool isExpanded_ = true;
    bool showTimestamp_ = true;
    bool showHeader_ = true;
    bool interactive_ = true;
    int maxWidth_ = 600;

    // Animation
    qreal expandProgress_ = 1.0;
    qreal fadeProgress_ = 1.0;
    int typewriterPosition_ = -1;
    QPropertyAnimation* currentAnimation_ = nullptr;
};

// Forward declaration
class MessageGroup;

// Container widget for multiple message bubbles with optimized rendering
class MessageBubbleContainer : public QWidget {
    Q_OBJECT
    
public:
    explicit MessageBubbleContainer(QWidget* parent = nullptr);
    
    // Message management
    void addMessage(UIMessage* message, bool animated = true);
    void insertMessage(int index, UIMessage* message, bool animated = true);
    void clearMessages(bool animated = false);
    
    MessageBubble* getBubble(const QUuid& id) const;
    QList<MessageBubble*> getAllBubbles() const;
    QList<MessageBubble*> getSelectedBubbles() const;
    
    // Selection
    void selectBubble(const QUuid& id, bool exclusive = true);
    void selectAll();
    void clearSelection();
    
    // Scrolling
    void scrollToMessage(const QUuid& id, bool animated = true);
    void scrollToBottom(bool animated = true);
    void scrollToTop(bool animated = true);
    
    // Appearance
    void setBubbleStyle(MessageBubble::BubbleStyle style);
    void setAnimationType(MessageBubble::AnimationType type);
    void setMaxBubbleWidth(int width);
    void setSpacing(int spacing) { spacing_ = spacing; updateLayout(); }
    void setDensityMode(int mode); // 0=Compact, 1=Cozy, 2=Spacious
    int densityMode() const { return densityMode_; }
    
    // Batch operations
    void beginBatchUpdate();
    void endBatchUpdate();
    
signals:
    void bubbleClicked(const QUuid& id);
    void bubbleDoubleClicked(const QUuid& id);
    void bubbleContextMenu(const QUuid& id, const QPoint& pos);
    void selectionChanged();
    void scrollRequested();
    
protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    
private slots:
    void onBubbleClicked();
    void onBubbleDoubleClicked();
    void onBubbleContextMenu(const QPoint& pos);
    void onBubbleSelectionChanged(bool selected);
    void updateLayout();
    void cleanupBubble(MessageBubble* bubble);
    
private:
    void setupBubble(MessageBubble* bubble);
    void animateInsertion(MessageBubble* bubble, int index);
    void animateRemoval(MessageBubble* bubble);
    QRect calculateBubbleGeometry(MessageBubble* bubble, int y) const;
    void updateVisibleBubbles();
    void performLayout();
    
    // Storage
    QList<MessageBubble*> bubbles_;
    QHash<QUuid, MessageBubble*> bubbleMap_;
    QSet<MessageBubble*> selectedBubbles_;
    
    // Message groups
    QList<MessageGroup*> groups_;
    MessageGroup* currentGroup_ = nullptr;
    
    // State
    MessageBubble::BubbleStyle bubbleStyle_ = MessageBubble::BubbleStyle::Modern;
    MessageBubble::AnimationType animationType_ = MessageBubble::AnimationType::FadeIn;
    int densityMode_ = 1; // 0=Compact, 1=Cozy, 2=Spacious
    int maxBubbleWidth_ = 600;
    int spacing_ = Design::SPACING_MD;

    // Batch update
    int batchUpdateCount_ = 0;
    bool layoutPending_ = false;
    
    // Performance
    QRect visibleRect_;
    QSet<MessageBubble*> visibleBubbles_;
    QTimer* layoutTimer_ = nullptr;
};
    
} // namespace llm_re::ui_v2