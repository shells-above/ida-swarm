#include "message_group.h"
#include "../core/theme_manager.h"
#include "../core/ui_utils.h"

namespace llm_re::ui_v2 {

MessageGroup::MessageGroup(UIMessage* firstMessage, QWidget* parent)
    : BaseStyledWidget(parent) {
    
    if (firstMessage) {
        role_ = firstMessage->role();
        author_ = firstMessage->metadata.author.isEmpty() ? 
                  firstMessage->roleString() : firstMessage->metadata.author;
        firstTimestamp_ = firstMessage->metadata.timestamp;
        lastTimestamp_ = firstTimestamp_;
    }
    
    setupUI();
    
    if (firstMessage) {
        addMessage(firstMessage);
    }
}

MessageGroup::~MessageGroup() = default;

void MessageGroup::setupUI() {
    setShadowEnabled(false);
    setBorderWidth(0);
    setBackgroundColor(Qt::transparent);
    setHoverEnabled(false);  // Disable hover effects
    
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    // Create header
    createHeader();
    if (headerWidget_) {
        mainLayout->addWidget(headerWidget_);
    }
    
    // Create messages container
    auto* messagesWidget = new QWidget(this);
    messagesLayout_ = new QVBoxLayout(messagesWidget);
    messagesLayout_->setContentsMargins(0, 0, 0, 0);
    updateSpacing();
    
    mainLayout->addWidget(messagesWidget);
}

void MessageGroup::createHeader() {
    headerWidget_ = new QWidget(this);
    auto* layout = new QHBoxLayout(headerWidget_);
    layout->setSpacing(Design::SPACING_SM);
    layout->setContentsMargins(0, 0, 0, Design::SPACING_XS);
    
    // Author label
    authorLabel_ = new QLabel(author_, this);
    authorLabel_->setFont(ThemeManager::instance().typography().bodySmall);
    
    // Style based on role
    const auto& colors = ThemeManager::instance().colors();
    QColor authorColor;
    switch (role_) {
        case Role::User:
            authorColor = colors.primary;
            break;
        case Role::Assistant:
            authorColor = colors.textPrimary;
            break;
        case Role::System:
            authorColor = colors.textSecondary;
            break;
    }
    authorLabel_->setStyleSheet(QString("color: %1; font-weight: 600;").arg(authorColor.name()));
    layout->addWidget(authorLabel_);
    
    // Separator dot
    auto* separator = new QLabel("・", this);
    separator->setStyleSheet(QString("color: %1;").arg(colors.textTertiary.name()));
    layout->addWidget(separator);
    
    // Timestamp
    timestampLabel_ = new QLabel(this);
    timestampLabel_->setFont(ThemeManager::instance().typography().caption);
    timestampLabel_->setStyleSheet(QString("color: %1;").arg(colors.textTertiary.name()));
    layout->addWidget(timestampLabel_);
    
    layout->addStretch();
    
    // Menu button removed to prevent hover layout shifts
    menuButton_ = nullptr;
    
    updateHeader();
}

void MessageGroup::updateHeader() {
    if (!timestampLabel_) return;
    
    // Update timestamp to show relative time
    auto timestamp = std::chrono::system_clock::from_time_t(
        firstTimestamp_.toSecsSinceEpoch());
    timestampLabel_->setText(UIUtils::formatRelativeTime(timestamp));
    timestampLabel_->setVisible(showTimestamp_);
}

bool MessageGroup::canAddMessage(UIMessage* message) const {
    if (!message || messages_.isEmpty()) return false;
    
    // Check if same sender
    if (message->role() != role_) return false;
    
    QString messageAuthor = message->metadata.author.isEmpty() ? 
                           message->roleString() : message->metadata.author;
    if (messageAuthor != author_) return false;
    
    // Check if within time window
    qint64 timeDiff = lastTimestamp_.secsTo(message->metadata.timestamp);
    return timeDiff >= 0 && timeDiff <= GROUP_TIMEOUT_SECONDS;
}

void MessageGroup::addMessage(UIMessage* message) {
    if (!message) return;
    
    messages_.append(message);
    lastTimestamp_ = message->metadata.timestamp;
    
    // Create bubble without header (group already has header)
    auto* bubble = new MessageBubble(message, this);
    bubble->setShowHeader(false); // Group header shows author/timestamp
    bubble->setMaxWidth(maxWidth_);
    bubble->setBubbleStyle(MessageBubble::BubbleStyle::Minimal);
    
    // Connect signals
    connect(bubble, &MessageBubble::clicked, [this, message]() {
        emit messageClicked(message->id());
    });
    connect(bubble, &MessageBubble::doubleClicked, [this, message]() {
        emit messageDoubleClicked(message->id());
    });

    bubbleMap_[message->id()] = bubble;
    messagesLayout_->addWidget(bubble);
    
    // Update header timestamp if this is a later message
    updateHeader();
}

void MessageGroup::removeMessage(const QUuid& id) {
    auto it = std::find_if(messages_.begin(), messages_.end(),
                          [&id](UIMessage* msg) { return msg->id() == id; });
    
    if (it != messages_.end()) {
        messages_.erase(it);
        
        if (auto* bubble = bubbleMap_.take(id)) {
            bubble->deleteLater();
        }
        
        // Update timestamps
        if (!messages_.isEmpty()) {
            firstTimestamp_ = messages_.first()->metadata.timestamp;
            lastTimestamp_ = messages_.last()->metadata.timestamp;
            updateHeader();
        }
    }
}

QList<UIMessage*> MessageGroup::messages() const {
    return messages_;
}

void MessageGroup::setMaxWidth(int width) {
    if (maxWidth_ != width) {
        maxWidth_ = width;
        for (auto* bubble : bubbleMap_) {
            bubble->setMaxWidth(width);
        }
    }
}

void MessageGroup::setShowTimestamp(bool show) {
    if (showTimestamp_ != show) {
        showTimestamp_ = show;
        updateHeader();
    }
}

void MessageGroup::setSelected(bool selected) {
    if (isSelected_ != selected) {
        isSelected_ = selected;
        for (auto* bubble : bubbleMap_) {
            bubble->setSelected(selected);
        }
        update();
        emit selectionChanged();
    }
}

bool MessageGroup::hasSelectedMessages() const {
    return std::any_of(bubbleMap_.begin(), bubbleMap_.end(),
                      [](MessageBubble* bubble) { return bubble->isSelected(); });
}

QList<QUuid> MessageGroup::selectedMessageIds() const {
    QList<QUuid> ids;
    for (auto it = bubbleMap_.begin(); it != bubbleMap_.end(); ++it) {
        if (it.value()->isSelected()) {
            ids.append(it.key());
        }
    }
    return ids;
}

void MessageGroup::updateSpacing() {
    if (!messagesLayout_) return;
    
    int spacing = Design::SPACING_XS; // Default compact
    switch (densityMode_) {
        case 0: // Compact
            spacing = Design::SPACING_XS;
            break;
        case 1: // Cozy
            spacing = Design::SPACING_SM;
            break;
        case 2: // Spacious
            spacing = Design::SPACING_MD;
            break;
    }
    
    messagesLayout_->setSpacing(spacing);
}

void MessageGroup::updateLayout() {
    // Force layout update
    if (layout()) {
        layout()->invalidate();
        layout()->activate();
    }
    updateGeometry();
}

MessageBubble* MessageGroup::getBubbleAt(const QPoint& pos) const {
    for (auto* bubble : bubbleMap_) {
        if (bubble->geometry().contains(pos)) {
            return bubble;
        }
    }
    return nullptr;
}

QSize MessageGroup::sizeHint() const {
    int width = maxWidth_;
    int height = 0;
    
    // Header height
    if (headerWidget_ && headerWidget_->isVisible()) {
        height += headerWidget_->sizeHint().height();
    }
    
    // Messages height
    for (auto* bubble : bubbleMap_) {
        height += bubble->sizeHint().height();
    }
    
    // Add spacing
    if (messagesLayout_ && bubbleMap_.size() > 1) {
        height += messagesLayout_->spacing() * (bubbleMap_.size() - 1);
    }
    
    return QSize(width, height);
}

QSize MessageGroup::minimumSizeHint() const {
    return QSize(200, 50);
}

void MessageGroup::paintEvent(QPaintEvent* event) {
    BaseStyledWidget::paintEvent(event);
    
    // Additional painting if needed
}

void MessageGroup::mousePressEvent(QMouseEvent* event) {
    // Let Qt handle event propagation naturally
    BaseStyledWidget::mousePressEvent(event);
}

void MessageGroup::mouseReleaseEvent(QMouseEvent* event) {
    // Let Qt handle event propagation naturally
    BaseStyledWidget::mouseReleaseEvent(event);
}

void MessageGroup::mouseDoubleClickEvent(QMouseEvent* event) {
    // Let Qt handle event propagation naturally
    BaseStyledWidget::mouseDoubleClickEvent(event);
}

void MessageGroup::contextMenuEvent(QContextMenuEvent* event) {
    // Let Qt handle event propagation naturally
    BaseStyledWidget::contextMenuEvent(event);
}

void MessageGroup::enterEvent(QEvent* event) {
    // Disable hover effects - don't show/hide menu button
    Q_UNUSED(event);
}

void MessageGroup::leaveEvent(QEvent* event) {
    // Disable hover effects - don't show/hide menu button
    Q_UNUSED(event);
}

} // namespace llm_re::ui_v2