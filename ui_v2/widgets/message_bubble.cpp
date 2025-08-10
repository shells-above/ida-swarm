#include "../core/ui_v2_common.h"
#include "message_bubble.h"
#include "message_group.h"
#include "../core/theme_manager.h"
#include "../core/ui_utils.h"

namespace llm_re::ui_v2 {

// MessageBubble implementation

MessageBubble::MessageBubble(UIMessage* message, QWidget* parent)
    : CardWidget(parent), message_(message) {
    
    setupUI();
    applyBubbleStyle();
    
    // Set initial content from message
    if (message_) {
        // Set header information
        if (nameLabel_) {
            nameLabel_->setText(message_->roleString());
        }
        if (timestampLabel_) {
            timestampLabel_->setText(message_->metadata.timestamp.toString("hh:mm"));
        }
        
        // Set message content
        QString content = message_->getDisplayText();
        if (!content.isEmpty()) {
            if (contentViewer_) {
                contentViewer_->setMarkdown(content);
            } else if (plainTextLabel_) {
                plainTextLabel_->setText(content);
                plainTextLabel_->setVisible(true);
            }
        }
    }
    
    // Set initial properties
    setFocusPolicy(Qt::NoFocus);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    
    // Set size policy to expand vertically with content
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    
    // Disable hover effects to prevent resizing
    setHoverEnabled(false);
    
    // Remove border to prevent focus highlight
    setBorderWidth(0);
}

MessageBubble::~MessageBubble() {
    if (currentAnimation_) {
        currentAnimation_->stop();
        currentAnimation_->deleteLater();
    }
}

void MessageBubble::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(Design::SPACING_SM);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    // Create components
    createHeader();
    createContent();

    // Add to layout
    if (headerWidget_) mainLayout->addWidget(headerWidget_);
    if (contentWidget_) mainLayout->addWidget(contentWidget_);
}

void MessageBubble::createHeader() {
    headerWidget_ = new QWidget(this);
    auto* layout = new QHBoxLayout(headerWidget_);
    layout->setSpacing(Design::SPACING_SM);
    layout->setContentsMargins(Design::SPACING_MD, Design::SPACING_SM, 
                              Design::SPACING_MD, Design::SPACING_SM);
    
    
    // Author name
    nameLabel_ = new QLabel(this);
    nameLabel_->setFont(ThemeManager::instance().typography().body);
    layout->addWidget(nameLabel_);
    
    layout->addStretch();
    
    // Timestamp
    timestampLabel_ = new QLabel(this);
    timestampLabel_->setFont(ThemeManager::instance().typography().caption);
    timestampLabel_->setStyleSheet(QString("color: %1;").arg(
        ThemeManager::instance().colors().textTertiary.name()));
    layout->addWidget(timestampLabel_);
}

void MessageBubble::createContent() {
    contentWidget_ = new QWidget(this);
    auto* layout = new QVBoxLayout(contentWidget_);
    layout->setContentsMargins(Design::SPACING_MD, 0, Design::SPACING_MD, 0);
    
    // Create markdown viewer for rich content
    contentViewer_ = new MarkdownViewer(this);
    contentViewer_->setReadOnly(true);
    contentViewer_->setShadowEnabled(false);
    contentViewer_->setBorderWidth(0);
    contentViewer_->setBackgroundColor(Qt::transparent);
    
    layout->addWidget(contentViewer_);
    
    // Alternative plain text label for simple messages
    plainTextLabel_ = new QLabel(this);
    plainTextLabel_->setWordWrap(true);
    plainTextLabel_->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    plainTextLabel_->setVisible(false);
    layout->addWidget(plainTextLabel_);
}

void MessageBubble::setBubbleStyle(BubbleStyle style) {
    if (bubbleStyle_ != style) {
        bubbleStyle_ = style;
        applyBubbleStyle();
        update();
    }
}

void MessageBubble::applyBubbleStyle() {
    const auto& colors = ThemeManager::instance().colors();
    
    switch (bubbleStyle_) {
        case BubbleStyle::Classic:
            setBorderRadius(Design::RADIUS_LG);
            setShadowEnabled(true);
            setShadowBlur(10);
            break;
            
        case BubbleStyle::Modern:
            setBorderRadius(Design::RADIUS_MD);
            setShadowEnabled(true);
            setShadowBlur(4);
            setElevation(1);
            break;
            
        case BubbleStyle::Minimal:
            setBorderRadius(Design::RADIUS_SM);
            setShadowEnabled(false);
            setBorderWidth(0);
            setBackgroundColor(Qt::transparent);
            break;
            
        case BubbleStyle::Terminal:
            setBorderRadius(0);
            setShadowEnabled(false);
            setBorderWidth(1);
            setBorderColor(colors.success);
            if (contentViewer_) {
                contentViewer_->setDefaultCodeLanguage("bash");
            }
            break;
            
        case BubbleStyle::Paper:
            setBorderRadius(0);
            setShadowEnabled(true);
            setShadowBlur(8);
            setShadowOffset(QPointF(2, 4));
            break;
    }
}


void MessageBubble::setShowTimestamp(bool show) {
    if (showTimestamp_ != show) {
        showTimestamp_ = show;
        if (timestampLabel_) {
            timestampLabel_->setVisible(show);
        }
        updateLayout();
    }
}

void MessageBubble::setShowHeader(bool show) {
    if (showHeader_ != show) {
        showHeader_ = show;
        if (headerWidget_) {
            headerWidget_->setVisible(show);
        }
        updateLayout();
    }
}

void MessageBubble::animateIn() {
    if (currentAnimation_) {
        currentAnimation_->stop();
        currentAnimation_->deleteLater();
    }
    
    switch (animationType_) {
        case AnimationType::FadeIn: {
            setFadeProgress(0.0);
            auto* anim = new QPropertyAnimation(this, "fadeProgress", this);
            anim->setDuration(Design::ANIM_NORMAL);
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
            anim->setEasingCurve(QEasingCurve::OutCubic);
            connect(anim, &QPropertyAnimation::finished,
                    this, &MessageBubble::onAnimationFinished);
            currentAnimation_ = anim;
            anim->start(QAbstractAnimation::DeleteWhenStopped);
            break;
        }
        
        case AnimationType::SlideIn: {
            // Slide from right for user, left for others
            int startX = message_ && message_->role() == Role::User ?
                        qobject_cast<QWidget*>(parent())->width() : -(this->width());
            move(startX, y());
            
            auto* anim = new QPropertyAnimation(this, "pos", this);
            anim->setDuration(Design::ANIM_NORMAL);
            anim->setEndValue(pos());
            anim->setEasingCurve(QEasingCurve::OutCubic);
            connect(anim, &QPropertyAnimation::finished,
                    this, &MessageBubble::onAnimationFinished);
            currentAnimation_ = anim;
            anim->start(QAbstractAnimation::DeleteWhenStopped);
            break;
        }
        
        case AnimationType::TypeWriter: {
            if (contentViewer_) {
                // Animate text appearing character by character
                setTypewriterPosition(0);
                auto* anim = new QPropertyAnimation(this, "typewriterPosition", this);
                anim->setDuration(message_->getDisplayText().length() * 20); // 20ms per char
                anim->setStartValue(0);
                anim->setEndValue(message_->getDisplayText().length());
                anim->setEasingCurve(QEasingCurve::Linear);
                connect(anim, &QPropertyAnimation::finished,
                        this, &MessageBubble::onAnimationFinished);
                currentAnimation_ = anim;
                anim->start(QAbstractAnimation::DeleteWhenStopped);
            }
            break;
        }
        
        case AnimationType::Bounce: {
            // Scale animation with bounce
            auto* anim = new QPropertyAnimation(this, "scale", this);
            anim->setDuration(Design::ANIM_NORMAL);
            anim->setStartValue(0.0);
            anim->setEndValue(1.0);
            anim->setEasingCurve(QEasingCurve::OutBounce);
            connect(anim, &QPropertyAnimation::finished,
                    this, &MessageBubble::onAnimationFinished);
            currentAnimation_ = anim;
            anim->start(QAbstractAnimation::DeleteWhenStopped);
            break;
        }
        
        default:
            onAnimationFinished();
            break;
    }
}

void MessageBubble::animateOut() {
    if (currentAnimation_) {
        currentAnimation_->stop();
        currentAnimation_->deleteLater();
    }
    
    auto* anim = new QPropertyAnimation(this, "fadeProgress", this);
    anim->setDuration(Design::ANIM_FAST);
    anim->setStartValue(fadeProgress_);
    anim->setEndValue(0.0);
    anim->setEasingCurve(QEasingCurve::InCubic);
    connect(anim, &QPropertyAnimation::finished, [this]() {
        hide();
        emit animationFinished();
    });
    currentAnimation_ = anim;
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MessageBubble::stopAnimation() {
    if (currentAnimation_) {
        currentAnimation_->stop();
        currentAnimation_->deleteLater();
        currentAnimation_ = nullptr;
    }
    
    // Reset to final state
    setFadeProgress(1.0);
    setExpandProgress(1.0);
    setTypewriterPosition(-1);
}

void MessageBubble::setSelected(bool selected) {
    if (isSelected_ != selected) {
        isSelected_ = selected;
        update();
        emit selectionChanged(selected);
    }
}

void MessageBubble::setHighlighted(bool highlighted) {
    if (isHighlighted_ != highlighted) {
        isHighlighted_ = highlighted;
        update();
    }
}

void MessageBubble::setExpanded(bool expanded, bool animated) {
    if (isExpanded_ == expanded) return;
    
    isExpanded_ = expanded;
    
    if (animated) {
        auto* anim = new QPropertyAnimation(this, "expandProgress", this);
        anim->setDuration(Design::ANIM_FAST);
        anim->setStartValue(expandProgress_);
        anim->setEndValue(expanded ? 1.0 : 0.0);
        anim->setEasingCurve(QEasingCurve::InOutQuad);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        setExpandProgress(expanded ? 1.0 : 0.0);
    }
    
    emit expansionChanged(expanded);
}

QString MessageBubble::toPlainText() const {
    if (!message_) return QString();
    return message_->getDisplayText();
}


void MessageBubble::setExpandProgress(qreal progress) {
    expandProgress_ = progress;

    updateLayout();
    update();
}

void MessageBubble::setFadeProgress(qreal progress) {
    fadeProgress_ = progress;
    setWindowOpacity(progress);
    update();
}

void MessageBubble::setTypewriterPosition(int position) {
    typewriterPosition_ = position;
    
    if (position >= 0 && message_) {
        QString visibleText = message_->getDisplayText().left(position);
        if (contentViewer_) {
            contentViewer_->setMarkdown(visibleText);
        } else if (plainTextLabel_) {
            plainTextLabel_->setText(visibleText);
        }
    }
}

QSize MessageBubble::sizeHint() const {
    QSize size = CardWidget::sizeHint();
    
    // Limit width
    if (size.width() > maxWidth_) {
        size.setWidth(maxWidth_);
    }

    return size;
}

QSize MessageBubble::minimumSizeHint() const {
    return QSize(200, 50);
}

void MessageBubble::updateTheme() {
    applyBubbleStyle();
}

void MessageBubble::paintContent(QPainter* painter) {
    CardWidget::paintContent(painter);
    
    // Paint selection overlay
    if (isSelected_) {
        paintSelectionOverlay(painter);
    }
}

void MessageBubble::resizeEvent(QResizeEvent* event) {
    CardWidget::resizeEvent(event);
    updateLayout();
}

void MessageBubble::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && interactive_) {
        setSelected(!isSelected_);
        emit clicked();
    }
    CardWidget::mousePressEvent(event);
}

void MessageBubble::mouseReleaseEvent(QMouseEvent* event) {
    CardWidget::mouseReleaseEvent(event);
}

void MessageBubble::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && interactive_) {
        emit doubleClicked();
    }
    CardWidget::mouseDoubleClickEvent(event);
}

void MessageBubble::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Paint background with no hover effects
    QPainterPath path;
    path.addRoundedRect(rect(), borderRadius(), borderRadius());
    painter.fillPath(path, backgroundColor());
    
    // Paint content
    paintContent(&painter);
    
    // Paint selection overlay if selected
    if (isSelected_) {
        paintSelectionOverlay(&painter);
    }
}

void MessageBubble::enterEvent(QEvent* event) {
    // Completely ignore hover events
    Q_UNUSED(event);
}

void MessageBubble::leaveEvent(QEvent* event) {
    // Completely ignore hover events
    Q_UNUSED(event);
}

void MessageBubble::onThemeChanged() {
    CardWidget::onThemeChanged();
    updateTheme();
}

void MessageBubble::onCopyAction() {
    QApplication::clipboard()->setText(toPlainText());
    emit copyRequested();
}

void MessageBubble::onAnimationFinished() {
    currentAnimation_ = nullptr;
    emit animationFinished();
}


void MessageBubble::updateLayout() {
    // Force layout update
    if (layout()) {
        layout()->invalidate();
        layout()->activate();
    }
    
    // Update size hint
    updateGeometry();
}

void MessageBubble::paintSelectionOverlay(QPainter* painter) {
    // Draw selection border
    const auto& colors = ThemeManager::instance().colors();
    
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(colors.primary, 2));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(rect().adjusted(1, 1, -1, -1), 
                           borderRadius(), borderRadius());
}

// MessageBubbleContainer implementation

MessageBubbleContainer::MessageBubbleContainer(QWidget* parent)
    : QWidget(parent) {
    
    auto* vboxLayout = new QVBoxLayout(this);
    vboxLayout->setSpacing(spacing_);
    vboxLayout->setContentsMargins(0, 0, 0, 0);
    // Remove alignment to allow natural expansion
    setLayout(vboxLayout);
    
    // Set size policy to expand with content
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    
    // Setup layout timer for batched updates
    layoutTimer_ = new QTimer(this);
    layoutTimer_->setSingleShot(true);
    layoutTimer_->setInterval(50);
    connect(layoutTimer_, &QTimer::timeout, this, &MessageBubbleContainer::performLayout);
}

void MessageBubbleContainer::addMessage(UIMessage* message, bool animated) {
    // Check if we can add to current group
    if (currentGroup_ && currentGroup_->canAddMessage(message)) {
        currentGroup_->addMessage(message);
        
        // Still need to track the bubble for compatibility
        if (auto* bubble = currentGroup_->findChild<MessageBubble*>(
                QString(), Qt::FindDirectChildrenOnly)) {
            if (bubble->message() && bubble->message()->id() == message->id()) {
                bubbles_.append(bubble);
                bubbleMap_[message->id()] = bubble;
            }
        }
    } else {
        // Create new group
        auto* group = new MessageGroup(message, this);
        groups_.append(group);
        currentGroup_ = group;
        
        // Configure group
        group->setDensityMode(densityMode_);
        group->setMaxWidth(maxBubbleWidth_);
        group->setShowTimestamp(true);
        
        // Connect group signals
        connect(group, &MessageGroup::messageClicked, [this](const QUuid& id) {
            emit bubbleClicked(id);
        });
        connect(group, &MessageGroup::messageDoubleClicked, [this](const QUuid& id) {
            emit bubbleDoubleClicked(id);
        });
        connect(group, &MessageGroup::contextMenuRequested, [this](const QUuid& id, const QPoint& pos) {
            emit bubbleContextMenu(id, pos);
        });

        // Add to layout with spacing
        if (groups_.size() > 1) {
            // Add spacing between groups
            auto* spacer = new QWidget(this);
            spacer->setFixedHeight(densityMode_ == 0 ? 12 : densityMode_ == 1 ? 16 : 24);
            layout()->addWidget(spacer);
        }
        
        layout()->addWidget(group);
        
        // Track the bubble for compatibility
        if (auto* bubble = group->findChild<MessageBubble*>(
                QString(), Qt::FindDirectChildrenOnly)) {
            if (bubble->message() && bubble->message()->id() == message->id()) {
                bubbles_.append(bubble);
                bubbleMap_[message->id()] = bubble;
            }
        }
    }
    
    if (!batchUpdateCount_) {
        updateLayout();
    }
}

void MessageBubbleContainer::insertMessage(int index, UIMessage* message, bool animated) {
    auto* bubble = new MessageBubble(message, this);
    setupBubble(bubble);
    
    bubbles_.insert(index, bubble);
    bubbleMap_[message->id()] = bubble;
    
    if (animated && !batchUpdateCount_) {
        animateInsertion(bubble, index);
    } else {
        qobject_cast<QVBoxLayout*>(layout())->insertWidget(index, bubble);
    }
    
    if (!batchUpdateCount_) {
        updateLayout();
    }
}

void MessageBubbleContainer::clearMessages(bool animated) {
    // Clear groups
    qDeleteAll(groups_);
    groups_.clear();
    currentGroup_ = nullptr;
    
    // Clear individual bubble tracking
    bubbles_.clear();
    bubbleMap_.clear();
    selectedBubbles_.clear();
    
    // Clear layout
    QLayoutItem* item;
    while ((item = layout()->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    
    if (!batchUpdateCount_) {
        updateLayout();
    }
}

MessageBubble* MessageBubbleContainer::getBubble(const QUuid& id) const {
    return bubbleMap_.value(id);
}

QList<MessageBubble*> MessageBubbleContainer::getAllBubbles() const {
    return bubbles_;
}

QList<MessageBubble*> MessageBubbleContainer::getSelectedBubbles() const {
    return selectedBubbles_.values();
}

void MessageBubbleContainer::selectBubble(const QUuid& id, bool exclusive) {
    MessageBubble* bubble = bubbleMap_.value(id);
    if (!bubble) return;
    
    if (exclusive) {
        clearSelection();
    }
    
    bubble->setSelected(true);
    selectedBubbles_.insert(bubble);
    
    emit selectionChanged();
}

void MessageBubbleContainer::selectAll() {
    for (MessageBubble* bubble : bubbles_) {
        bubble->setSelected(true);
        selectedBubbles_.insert(bubble);
    }
    
    emit selectionChanged();
}

void MessageBubbleContainer::clearSelection() {
    for (MessageBubble* bubble : selectedBubbles_) {
        bubble->setSelected(false);
    }
    selectedBubbles_.clear();
    
    emit selectionChanged();
}

void MessageBubbleContainer::scrollToMessage(const QUuid& id, bool animated) {
    MessageBubble* bubble = bubbleMap_.value(id);
    if (!bubble) return;
    
    // Find parent scroll area
    QScrollArea* scrollArea = nullptr;
    QWidget* parent = parentWidget();
    while (parent && !scrollArea) {
        scrollArea = qobject_cast<QScrollArea*>(parent);
        parent = parent->parentWidget();
    }
    
    if (scrollArea) {
        if (animated) {
            SmoothScroller::smoothScrollToWidget(scrollArea, bubble);
        } else {
            scrollArea->ensureWidgetVisible(bubble);
        }
    }
    
    emit scrollRequested();
}

void MessageBubbleContainer::scrollToBottom(bool animated) {
    QScrollArea* scrollArea = nullptr;
    QWidget* parent = parentWidget();
    while (parent && !scrollArea) {
        scrollArea = qobject_cast<QScrollArea*>(parent);
        parent = parent->parentWidget();
    }
    
    if (scrollArea) {
        int maxScroll = scrollArea->verticalScrollBar()->maximum();
        if (animated) {
            SmoothScroller::smoothScrollTo(scrollArea, QPoint(0, maxScroll));
        } else {
            scrollArea->verticalScrollBar()->setValue(maxScroll);
        }
    }
    
    emit scrollRequested();
}

void MessageBubbleContainer::scrollToTop(bool animated) {
    QScrollArea* scrollArea = nullptr;
    QWidget* parent = parentWidget();
    while (parent && !scrollArea) {
        scrollArea = qobject_cast<QScrollArea*>(parent);
        parent = parent->parentWidget();
    }
    
    if (scrollArea) {
        if (animated) {
            SmoothScroller::smoothScrollTo(scrollArea, QPoint(0, 0));
        } else {
            scrollArea->verticalScrollBar()->setValue(0);
        }
    }
    
    emit scrollRequested();
}

void MessageBubbleContainer::setBubbleStyle(MessageBubble::BubbleStyle style) {
    bubbleStyle_ = style;
    for (MessageBubble* bubble : bubbles_) {
        bubble->setBubbleStyle(style);
    }
}

void MessageBubbleContainer::setAnimationType(MessageBubble::AnimationType type) {
    animationType_ = type;
}


void MessageBubbleContainer::setDensityMode(int mode) {
    densityMode_ = mode;
    
    // Update all groups
    for (MessageGroup* group : groups_) {
        group->setDensityMode(mode);
    }
    
    // Update spacing
    switch (mode) {
        case 0: // Compact
            spacing_ = Design::SPACING_XS;
            break;
        case 1: // Cozy
            spacing_ = Design::SPACING_SM;
            break;
        case 2: // Spacious
            spacing_ = Design::SPACING_MD;
            break;
    }
    
    layout()->setSpacing(spacing_);
    updateLayout();
}

void MessageBubbleContainer::setMaxBubbleWidth(int width) {
    maxBubbleWidth_ = width;
    for (MessageBubble* bubble : bubbles_) {
        bubble->setMaxWidth(width);
    }
    updateLayout();
}

void MessageBubbleContainer::beginBatchUpdate() {
    batchUpdateCount_++;
}

void MessageBubbleContainer::endBatchUpdate() {
    if (batchUpdateCount_ > 0) {
        batchUpdateCount_--;
        if (batchUpdateCount_ == 0) {
            updateLayout();
        }
    }
}

void MessageBubbleContainer::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateVisibleBubbles();
}

void MessageBubbleContainer::paintEvent(QPaintEvent* event) {
    // CRITICAL: Do NOT call QWidget::paintEvent as it uses the application style
    // Paint our own background with theme colors
    QPainter painter(this);
    const auto& colors = ThemeManager::instance().colors();
    painter.fillRect(rect(), colors.background);
    
    // QWidget::paintEvent(event);  // REMOVED - was causing theme inheritance
}

bool MessageBubbleContainer::eventFilter(QObject* watched, QEvent* event) {
    return QWidget::eventFilter(watched, event);
}

void MessageBubbleContainer::onBubbleClicked() {
    if (auto* bubble = qobject_cast<MessageBubble*>(sender())) {
        if (bubble->message()) {
            emit bubbleClicked(bubble->message()->id());
        }
    }
}

void MessageBubbleContainer::onBubbleDoubleClicked() {
    if (auto* bubble = qobject_cast<MessageBubble*>(sender())) {
        if (bubble->message()) {
            emit bubbleDoubleClicked(bubble->message()->id());
        }
    }
}

void MessageBubbleContainer::onBubbleContextMenu(const QPoint& pos) {
    if (auto* bubble = qobject_cast<MessageBubble*>(sender())) {
        if (bubble->message()) {
            emit bubbleContextMenu(bubble->message()->id(), 
                                 bubble->mapToGlobal(pos));
        }
    }
}

void MessageBubbleContainer::onBubbleSelectionChanged(bool selected) {
    if (auto* bubble = qobject_cast<MessageBubble*>(sender())) {
        if (selected) {
            selectedBubbles_.insert(bubble);
        } else {
            selectedBubbles_.remove(bubble);
        }
        emit selectionChanged();
    }
}

void MessageBubbleContainer::updateLayout() {
    if (!layoutPending_) {
        layoutPending_ = true;
        layoutTimer_->start();
    }
}

void MessageBubbleContainer::cleanupBubble(MessageBubble* bubble) {
    bubble->deleteLater();
}

void MessageBubbleContainer::setupBubble(MessageBubble* bubble) {
    bubble->setBubbleStyle(bubbleStyle_);
    bubble->setAnimationType(animationType_);
    bubble->setMaxWidth(maxBubbleWidth_);
    
    connect(bubble, &MessageBubble::clicked,
            this, &MessageBubbleContainer::onBubbleClicked);
    connect(bubble, &MessageBubble::doubleClicked,
            this, &MessageBubbleContainer::onBubbleDoubleClicked);
    connect(bubble, &MessageBubble::selectionChanged,
            this, &MessageBubbleContainer::onBubbleSelectionChanged);
}

void MessageBubbleContainer::animateInsertion(MessageBubble* bubble, int index) {
    qobject_cast<QVBoxLayout*>(layout())->insertWidget(index, bubble);
    bubble->animateIn();
}

void MessageBubbleContainer::animateRemoval(MessageBubble* bubble) {
    connect(bubble, &MessageBubble::animationFinished, [this, bubble]() {
        cleanupBubble(bubble);
    });
    bubble->animateOut();
}

QRect MessageBubbleContainer::calculateBubbleGeometry(MessageBubble* bubble, int y) const {
    int bubbleWidth = qMin(bubble->sizeHint().width(), maxBubbleWidth_);
    int bubbleHeight = bubble->sizeHint().height();
    
    int x = 0;
    if (bubble->message()) {
        // Align based on message role
        if (bubble->message()->role() == Role::User) {
            x = width() - bubbleWidth - Design::SPACING_MD;
        } else {
            x = Design::SPACING_MD;
        }
    }
    
    return QRect(x, y, bubbleWidth, bubbleHeight);
}

void MessageBubbleContainer::updateVisibleBubbles() {
    // Determine which bubbles are visible for optimization
    visibleRect_ = rect();
    visibleBubbles_.clear();
    
    for (MessageBubble* bubble : bubbles_) {
        if (bubble->geometry().intersects(visibleRect_)) {
            visibleBubbles_.insert(bubble);
        }
    }
}

void MessageBubbleContainer::performLayout() {
    layoutPending_ = false;
    
    // Custom layout logic if needed
    // For now, rely on QVBoxLayout
    
    updateVisibleBubbles();
}

} // namespace llm_re::ui_v2