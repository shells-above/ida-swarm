#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"
#include "../models/conversation_model.h"
#include "message_bubble.h"

namespace llm_re::ui_v2 {

// Groups consecutive messages from the same sender for compact display
class MessageGroup : public BaseStyledWidget {
    Q_OBJECT

public:
    explicit MessageGroup(UIMessage* firstMessage, QWidget* parent = nullptr);
    ~MessageGroup() override;

    // Message management
    bool canAddMessage(UIMessage* message) const;
    void addMessage(UIMessage* message);
    void removeMessage(const QUuid& id);
    QList<UIMessage*> messages() const;
    
    // Get the sender info
    Role role() const { return role_; }
    QString author() const { return author_; }
    QDateTime firstTimestamp() const { return firstTimestamp_; }
    QDateTime lastTimestamp() const { return lastTimestamp_; }
    
    // Appearance
    void setDensityMode(int mode) { densityMode_ = mode; updateSpacing(); }
    int densityMode() const { return densityMode_; }
    
    void setMaxWidth(int width);
    int maxWidth() const { return maxWidth_; }
    
    void setShowTimestamp(bool show);
    bool showTimestamp() const { return showTimestamp_; }
    
    // Selection
    void setSelected(bool selected);
    bool isSelected() const { return isSelected_; }
    bool hasSelectedMessages() const;
    QList<QUuid> selectedMessageIds() const;
    
    // Size hint
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

signals:
    void messageClicked(const QUuid& id);
    void messageDoubleClicked(const QUuid& id);
    void contextMenuRequested(const QUuid& id, const QPoint& pos);
    void linkClicked(const QUrl& url);
    void selectionChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void setupUI();
    void createHeader();
    void updateHeader();
    void updateSpacing();
    void updateLayout();
    MessageBubble* getBubbleAt(const QPoint& pos) const;
    
    // Message data
    QList<UIMessage*> messages_;
    QHash<QUuid, MessageBubble*> bubbleMap_;
    Role role_;
    QString author_;
    QDateTime firstTimestamp_;
    QDateTime lastTimestamp_;
    
    // UI components
    QWidget* headerWidget_ = nullptr;
    QLabel* authorLabel_ = nullptr;
    QLabel* timestampLabel_ = nullptr;
    QToolButton* menuButton_ = nullptr;
    QVBoxLayout* messagesLayout_ = nullptr;
    
    // State
    bool isSelected_ = false;
    bool isHovered_ = false;
    bool showTimestamp_ = true;
    int densityMode_ = 1; // 0=Compact, 1=Cozy, 2=Spacious
    int maxWidth_ = 600;
    
    // Constants for group timeouts (messages within this time are grouped)
    static constexpr int GROUP_TIMEOUT_SECONDS = 300; // 5 minutes
};

} // namespace llm_re::ui_v2