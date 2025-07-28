#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"
#include "../models/conversation_model.h"
#include "markdown_viewer.h"

namespace llm_re::ui_v2 {

// Forward declarations
class TypingIndicator;

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
    
    explicit MessageBubble(Message* message, QWidget* parent = nullptr);
    ~MessageBubble() override;
    
    // Message access
    Message* message() { return message_; }
    const Message* message() const { return message_; }
    void updateMessage();
    
    // Appearance
    void setBubbleStyle(BubbleStyle style);
    BubbleStyle bubbleStyle() const { return bubbleStyle_; }
    
    void setMaxWidth(int width) { maxWidth_ = width; }
    int maxWidth() const { return maxWidth_; }
    
    
    void setShowTimestamp(bool show);
    bool showTimestamp() const { return showTimestamp_; }
    
    void setCompactMode(bool compact);
    bool isCompactMode() const { return compactMode_; }
    
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
    
    // Tool execution display
    void updateToolExecution();
    void setToolOutputVisible(bool visible);
    bool isToolOutputVisible() const { return toolOutputVisible_; }
    
    // Search
    void setSearchHighlight(const QString& text);
    void clearSearchHighlight();
    
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
    void contextMenuRequested(const QPoint& pos);
    void linkClicked(const QUrl& url);
    void editRequested();
    void deleteRequested();
    void attachmentClicked(const MessageAttachment& attachment);
    void toolOutputToggled();
    void copyRequested();
    void selectionChanged(bool selected);
    void expansionChanged(bool expanded);
    void animationFinished();
    
public slots:
    void updateTheme();
    void refresh();
    
protected:
    void paintContent(QPainter* painter) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void onThemeChanged() override;
    
private slots:
    void onContentLinkClicked(const QUrl& url);
    void onCopyAction();
    void onEditAction();
    void onDeleteAction();
    void onPinAction();
    void onBookmarkAction();
    void onAnimationFinished();
    void onToolProgressChanged();
    
private:
    void setupUI();
    void createHeader();
    void createContent();
    void createFooter();
    void createToolExecutionWidget();
    void createAnalysisWidget();
    void createAttachmentsWidget();
    void createContextMenu();
    void updateLayout();
    void updateContentDisplay();
    void updateToolExecutionDisplay();
    void updateAnalysisDisplay();
    void updateAttachmentsDisplay();
    void applyBubbleStyle();
    void startAnimation();
    QRect calculateBubbleRect() const;
    void paintStatusIndicator(QPainter* painter, const QRect& rect);
    void paintSelectionOverlay(QPainter* painter);
    
    // Core data
    Message* message_;
    BubbleStyle bubbleStyle_ = BubbleStyle::Modern;
    AnimationType animationType_ = AnimationType::FadeIn;
    
    // Layout components
    QWidget* headerWidget_ = nullptr;
    QWidget* contentWidget_ = nullptr;
    QWidget* footerWidget_ = nullptr;
    QWidget* toolWidget_ = nullptr;
    QWidget* analysisWidget_ = nullptr;
    QWidget* attachmentsWidget_ = nullptr;
    
    // Header components
    QLabel* nameLabel_ = nullptr;
    QLabel* roleLabel_ = nullptr;
    QLabel* timestampLabel_ = nullptr;
    QToolButton* menuButton_ = nullptr;
    
    // Content components
    MarkdownViewer* contentViewer_ = nullptr;
    QLabel* plainTextLabel_ = nullptr;
    
    // Tool execution components
    QLabel* toolNameLabel_ = nullptr;
    QLabel* toolStatusLabel_ = nullptr;
    QProgressBar* toolProgress_ = nullptr;
    QTextEdit* toolOutputEdit_ = nullptr;
    QToolButton* toolOutputToggle_ = nullptr;
    
    // Footer components
    QToolButton* shareButton_ = nullptr;
    
    // State
    bool isSelected_ = false;
    bool isHighlighted_ = false;
    bool isExpanded_ = true;
    bool showTimestamp_ = true;
    bool compactMode_ = false;
    bool interactive_ = true;
    bool toolOutputVisible_ = false;
    int maxWidth_ = 600;
    QString searchHighlight_;
    
    // Animation
    qreal expandProgress_ = 1.0;
    qreal fadeProgress_ = 1.0;
    int typewriterPosition_ = -1;
    QPropertyAnimation* currentAnimation_ = nullptr;
    
    // Context menu
    QMenu* contextMenu_ = nullptr;
    QAction* copyAction_ = nullptr;
    QAction* editAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* pinAction_ = nullptr;
    QAction* bookmarkAction_ = nullptr;
    
    // Metrics cache
    mutable QSize cachedSize_;
    mutable bool sizeCacheDirty_ = true;
};

// Container widget for multiple message bubbles with optimized rendering
class MessageBubbleContainer : public QWidget {
    Q_OBJECT
    
public:
    explicit MessageBubbleContainer(QWidget* parent = nullptr);
    
    // Message management
    void addMessage(Message* message, bool animated = true);
    void insertMessage(int index, Message* message, bool animated = true);
    void removeMessage(const QUuid& id, bool animated = true);
    void clearMessages(bool animated = false);
    
    // Typing indicator
    void showTypingIndicator(const QString& user = QString());
    void hideTypingIndicator();
    
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
    
    // Search
    void setSearchFilter(const QString& text);
    void clearSearchFilter();
    void highlightNextMatch();
    void highlightPreviousMatch();
    
    // Appearance
    void setBubbleStyle(MessageBubble::BubbleStyle style);
    void setAnimationType(MessageBubble::AnimationType type);
    void setCompactMode(bool compact);
    void setMaxBubbleWidth(int width);
    void setSpacing(int spacing) { spacing_ = spacing; updateLayout(); }
    
    // Batch operations
    void beginBatchUpdate();
    void endBatchUpdate();
    
signals:
    void bubbleClicked(const QUuid& id);
    void bubbleDoubleClicked(const QUuid& id);
    void bubbleContextMenu(const QUuid& id, const QPoint& pos);
    void selectionChanged();
    void linkClicked(const QUrl& url);
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
    
    // State
    MessageBubble::BubbleStyle bubbleStyle_ = MessageBubble::BubbleStyle::Modern;
    MessageBubble::AnimationType animationType_ = MessageBubble::AnimationType::FadeIn;
    bool compactMode_ = false;
    int maxBubbleWidth_ = 600;
    int spacing_ = Design::SPACING_MD;
    QString searchFilter_;
    int currentSearchMatch_ = -1;
    
    // Typing indicator
    TypingIndicator* typingIndicator_ = nullptr;
    
    // Batch update
    int batchUpdateCount_ = 0;
    bool layoutPending_ = false;
    
    // Performance
    QRect visibleRect_;
    QSet<MessageBubble*> visibleBubbles_;
    QTimer* layoutTimer_ = nullptr;
};

// Typing indicator widget
class TypingIndicator : public BaseStyledWidget {
    Q_OBJECT
    Q_PROPERTY(qreal animationPhase READ animationPhase WRITE setAnimationPhase)
    
public:
    explicit TypingIndicator(QWidget* parent = nullptr);
    
    void setTypingUser(const QString& user);
    QString typingUser() const { return typingUser_; }
    
    void startAnimation();
    void stopAnimation();
    bool isAnimating() const { return animationTimer_ != nullptr; }
    
    qreal animationPhase() const { return animationPhase_; }
    void setAnimationPhase(qreal phase);
    
    QSize sizeHint() const override;
    
protected:
    void paintContent(QPainter* painter) override;
    
private:
    QString typingUser_;
    qreal animationPhase_ = 0.0;
    QTimer* animationTimer_ = nullptr;
    QPropertyAnimation* phaseAnimation_ = nullptr;
};


} // namespace llm_re::ui_v2