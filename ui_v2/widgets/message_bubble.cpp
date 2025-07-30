#include "../core/ui_v2_common.h"
#include "message_bubble.h"
#include "message_group.h"
#include "../core/theme_manager.h"
#include "../core/ui_utils.h"

namespace llm_re::ui_v2 {

// MessageBubble implementation

MessageBubble::MessageBubble(Message* message, QWidget* parent)
    : CardWidget(parent), message_(message) {
    
    setupUI();
    updateMessage();
    applyBubbleStyle();
    
    // Set initial properties
    setFocusPolicy(Qt::NoFocus);
    setContextMenuPolicy(Qt::DefaultContextMenu);
    
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
    createFooter();
    
    // Add to layout
    if (headerWidget_) mainLayout->addWidget(headerWidget_);
    if (contentWidget_) mainLayout->addWidget(contentWidget_);
    if (toolWidget_) mainLayout->addWidget(toolWidget_);
    if (analysisWidget_) mainLayout->addWidget(analysisWidget_);
    if (attachmentsWidget_) mainLayout->addWidget(attachmentsWidget_);
    if (footerWidget_) mainLayout->addWidget(footerWidget_);
    
    createContextMenu();
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
    
    // Menu button
    menuButton_ = new QToolButton(this);
    menuButton_->setIcon(ThemeManager::instance().themedIcon("more"));
    menuButton_->setIconSize(QSize(16, 16));
    menuButton_->setAutoRaise(true);
    menuButton_->setPopupMode(QToolButton::InstantPopup);
    connect(menuButton_, &QToolButton::clicked, [this]() {
        if (contextMenu_) {
            contextMenu_->popup(menuButton_->mapToGlobal(
                QPoint(0, menuButton_->height())));
        }
    });
    layout->addWidget(menuButton_);
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
    
    connect(contentViewer_, &MarkdownViewer::linkClicked,
            this, &MessageBubble::onContentLinkClicked);
    
    layout->addWidget(contentViewer_);
    
    // Alternative plain text label for simple messages
    plainTextLabel_ = new QLabel(this);
    plainTextLabel_->setWordWrap(true);
    plainTextLabel_->setTextInteractionFlags(
        Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse);
    plainTextLabel_->setVisible(false);
    layout->addWidget(plainTextLabel_);
}

void MessageBubble::createFooter() {
    footerWidget_ = new QWidget(this);
    auto* layout = new QHBoxLayout(footerWidget_);
    layout->setSpacing(Design::SPACING_SM);
    layout->setContentsMargins(Design::SPACING_MD, 0, Design::SPACING_MD, Design::SPACING_SM);
    
    
    
    // Share button
    shareButton_ = new QToolButton(this);
    shareButton_->setIcon(ThemeManager::instance().themedIcon("share"));
    shareButton_->setToolTip(tr("Share"));
    shareButton_->setAutoRaise(true);
    layout->addWidget(shareButton_);
    
    layout->addStretch();
    
    // Initially hidden
    footerWidget_->setVisible(false);
}

void MessageBubble::createToolExecutionWidget() {
    if (!toolWidget_) {
        toolWidget_ = new QWidget(this);
        auto* layout = new QVBoxLayout(toolWidget_);
        layout->setSpacing(Design::SPACING_SM);
        layout->setContentsMargins(Design::SPACING_MD, 0, Design::SPACING_MD, 0);
        
        // Tool header
        auto* headerLayout = new QHBoxLayout();
        
        toolNameLabel_ = new QLabel(this);
        toolNameLabel_->setFont(ThemeManager::instance().typography().body);
        headerLayout->addWidget(toolNameLabel_);
        
        headerLayout->addStretch();
        
        toolStatusLabel_ = new QLabel(this);
        toolStatusLabel_->setFont(ThemeManager::instance().typography().caption);
        headerLayout->addWidget(toolStatusLabel_);
        
        toolOutputToggle_ = new QToolButton(this);
        toolOutputToggle_->setIcon(ThemeManager::instance().themedIcon("expand"));
        toolOutputToggle_->setCheckable(true);
        toolOutputToggle_->setAutoRaise(true);
        connect(toolOutputToggle_, &QToolButton::toggled, [this](bool checked) {
            setToolOutputVisible(checked);
        });
        headerLayout->addWidget(toolOutputToggle_);
        
        layout->addLayout(headerLayout);
        
        // Progress bar
        toolProgress_ = new QProgressBar(this);
        toolProgress_->setTextVisible(false);
        toolProgress_->setMaximumHeight(4);
        layout->addWidget(toolProgress_);
        
        // Output area
        toolOutputEdit_ = new QTextEdit(this);
        toolOutputEdit_->setReadOnly(true);
        toolOutputEdit_->setFont(ThemeManager::instance().typography().code);
        toolOutputEdit_->setMaximumHeight(200);
        toolOutputEdit_->setVisible(false);
        layout->addWidget(toolOutputEdit_);
        
        // Insert after content
        if (auto* mainLayout = qobject_cast<QVBoxLayout*>(this->layout())) {
            int index = mainLayout->indexOf(contentWidget_) + 1;
            mainLayout->insertWidget(index, toolWidget_);
        }
    }
    
    toolWidget_->setVisible(message_ && message_->hasToolExecution());
}

void MessageBubble::createAnalysisWidget() {
    if (!analysisWidget_) {
        analysisWidget_ = new QWidget(this);
        auto* layout = new QVBoxLayout(analysisWidget_);
        layout->setSpacing(Design::SPACING_XS);
        layout->setContentsMargins(Design::SPACING_MD, 0, Design::SPACING_MD, 0);
        
        // Analysis entries will be created dynamically
        
        // Insert after tool widget or content
        if (auto* mainLayout = qobject_cast<QVBoxLayout*>(this->layout())) {
            int index = mainLayout->indexOf(toolWidget_ ? toolWidget_ : contentWidget_) + 1;
            mainLayout->insertWidget(index, analysisWidget_);
        }
    }
    
    analysisWidget_->setVisible(message_ && message_->hasAnalysis());
}

void MessageBubble::createAttachmentsWidget() {
    if (!attachmentsWidget_) {
        attachmentsWidget_ = new QWidget(this);
        auto* layout = new QHBoxLayout(attachmentsWidget_);
        layout->setSpacing(Design::SPACING_SM);
        layout->setContentsMargins(Design::SPACING_MD, 0, Design::SPACING_MD, 0);
        
        // Attachment previews will be created dynamically
        
        // Insert before footer
        if (auto* mainLayout = qobject_cast<QVBoxLayout*>(this->layout())) {
            int index = footerWidget_ ? mainLayout->indexOf(footerWidget_) : mainLayout->count();
            mainLayout->insertWidget(index, attachmentsWidget_);
        }
    }
    
    attachmentsWidget_->setVisible(message_ && message_->hasAttachments());
}


void MessageBubble::createContextMenu() {
    contextMenu_ = new QMenu(this);
    
    copyAction_ = contextMenu_->addAction(ThemeManager::instance().themedIcon("copy"), 
                                          tr("Copy"), this, &MessageBubble::onCopyAction);
    copyAction_->setShortcut(QKeySequence::Copy);
    
    if (message_ && message_->role() == MessageRole::User) {
        editAction_ = contextMenu_->addAction(ThemeManager::instance().themedIcon("edit"),
                                             tr("Edit"), this, &MessageBubble::onEditAction);
    }
    
    contextMenu_->addSeparator();
    
    
    contextMenu_->addSeparator();
    
    pinAction_ = contextMenu_->addAction(ThemeManager::instance().themedIcon("pin"),
                                        tr("Pin"), this, &MessageBubble::onPinAction);
    pinAction_->setCheckable(true);
    
    bookmarkAction_ = contextMenu_->addAction(ThemeManager::instance().themedIcon("bookmark"),
                                             tr("Bookmark"), this, &MessageBubble::onBookmarkAction);
    bookmarkAction_->setCheckable(true);
    
    contextMenu_->addSeparator();
    
    deleteAction_ = contextMenu_->addAction(ThemeManager::instance().themedIcon("delete"),
                                           tr("Delete"), this, &MessageBubble::onDeleteAction);
    deleteAction_->setShortcut(QKeySequence::Delete);
}

void MessageBubble::updateMessage() {
    if (!message_) return;
    
    sizeCacheDirty_ = true;
    
    // Update header
    if (nameLabel_) {
        nameLabel_->setText(message_->metadata().author.isEmpty() ? 
                           message_->roleString() : message_->metadata().author);
    }
    
    if (timestampLabel_) {
        // Convert QDateTime to std::chrono::system_clock::time_point
        auto timestamp = std::chrono::system_clock::from_time_t(
            message_->metadata().timestamp.toSecsSinceEpoch());
        timestampLabel_->setText(UIUtils::formatRelativeTime(timestamp));
        timestampLabel_->setVisible(showTimestamp_);
    }
    
    // Apply role-specific styling
    const auto& colors = ThemeManager::instance().colors();
    switch (message_->role()) {
        case MessageRole::User:
            setBackgroundColor(colors.primary.lighter(180));
            break;
        case MessageRole::Assistant:
            setBackgroundColor(colors.surface);
            break;
        case MessageRole::System:
            setBackgroundColor(colors.surfaceHover);
            break;
        case MessageRole::Tool:
            setBackgroundColor(colors.warning.lighter(180));
            break;
    }
    
    // Update content
    updateContentDisplay();
    
    // Update tool execution
    if (message_->hasToolExecution()) {
        createToolExecutionWidget();
        updateToolExecutionDisplay();
    }
    
    // Update analysis
    if (message_->hasAnalysis()) {
        createAnalysisWidget();
        updateAnalysisDisplay();
    }
    
    // Update attachments
    if (message_->hasAttachments()) {
        createAttachmentsWidget();
        updateAttachmentsDisplay();
    }
    
    
    // Update context menu state
    if (pinAction_) {
        pinAction_->setChecked(message_->metadata().isPinned);
    }
    if (bookmarkAction_) {
        bookmarkAction_->setChecked(message_->metadata().isBookmarked);
    }
    
    updateLayout();
}

void MessageBubble::updateContentDisplay() {
    if (!message_) return;
    
    QString content;
    
    // Add thinking content if present (for assistant messages)
    if (message_->hasThinking() && message_->role() == MessageRole::Assistant) {
        QString thinking = message_->thinkingContent();
        
        // Apply search highlighting to thinking if needed
        if (!searchHighlight_.isEmpty()) {
            thinking = UIUtils::highlightText(thinking, searchHighlight_, "search-highlight");
        }
        
        // Format thinking content in italics with darker color
        content = QString("*<span style='color: #888888;'>Thinking:</span>*\n\n");
        content += QString("*<span style='color: #888888;'>%1</span>*\n\n").arg(thinking.replace("\n", "\n> "));
    }
    
    // Add regular content
    QString mainContent = message_->content();
    
    // Apply search highlighting if needed
    if (!searchHighlight_.isEmpty()) {
        mainContent = UIUtils::highlightText(mainContent, searchHighlight_, "search-highlight");
    }
    
    content += mainContent;
    
    // Use markdown viewer for rich content
    if (message_->type() == MessageType::Code || 
        content.contains("```") || 
        content.contains("**") ||
        content.contains("[]()") ||
        message_->hasThinking()) {  // Always use markdown if has thinking
        
        contentViewer_->setMarkdown(content);
        contentViewer_->setVisible(true);
        plainTextLabel_->setVisible(false);
        
    } else {
        // Use plain text for simple messages
        plainTextLabel_->setText(content);
        plainTextLabel_->setVisible(true);
        contentViewer_->setVisible(false);
    }
}

void MessageBubble::updateToolExecutionDisplay() {
    if (!message_ || !message_->hasToolExecution() || !toolWidget_) return;
    
    const ToolExecution* exec = message_->toolExecution();
    
    toolNameLabel_->setText(exec->toolName);
    
    // Update status
    QString statusText;
    QColor statusColor;
    const auto& colors = ThemeManager::instance().colors();
    
    switch (exec->state) {
        case ToolExecutionState::Pending:
            statusText = tr("Pending");
            statusColor = colors.textTertiary;
            break;
        case ToolExecutionState::Running:
            statusText = tr("Running...");
            statusColor = colors.info;
            toolProgress_->setVisible(true);
            break;
        case ToolExecutionState::Completed:
            statusText = tr("Completed");
            statusColor = colors.success;
            toolProgress_->setVisible(false);
            break;
        case ToolExecutionState::Failed:
            statusText = tr("Failed");
            statusColor = colors.error;
            toolProgress_->setVisible(false);
            break;
        case ToolExecutionState::Cancelled:
            statusText = tr("Cancelled");
            statusColor = colors.warning;
            toolProgress_->setVisible(false);
            break;
    }
    
    toolStatusLabel_->setText(statusText);
    toolStatusLabel_->setStyleSheet(QString("color: %1;").arg(statusColor.name()));
    
    // Update progress
    if (exec->state == ToolExecutionState::Running) {
        toolProgress_->setValue(exec->progress);
        if (!exec->progressMessage.isEmpty()) {
            toolProgress_->setToolTip(exec->progressMessage);
        }
    }
    
    // Update output
    QString output = exec->output;
    if (!exec->errorMessage.isEmpty()) {
        output += "\n\nError:\n" + exec->errorMessage;
    }
    toolOutputEdit_->setPlainText(output);
    
    // Show output toggle only if there's output
    toolOutputToggle_->setVisible(!output.isEmpty());
}

void MessageBubble::updateAnalysisDisplay() {
    if (!message_ || !message_->hasAnalysis() || !analysisWidget_) return;
    
    // Clear existing entries
    QLayoutItem* item;
    while ((item = analysisWidget_->layout()->takeAt(0))) {
        delete item->widget();
        delete item;
    }
    
    const auto& colors = ThemeManager::instance().colors();
    
    for (const auto& entry : message_->analysisEntries()) {
        auto* entryWidget = new QWidget(this);
        auto* layout = new QHBoxLayout(entryWidget);
        layout->setSpacing(Design::SPACING_SM);
        layout->setContentsMargins(0, 0, 0, 0);
        
        // Type indicator
        auto* indicator = new QWidget(this);
        indicator->setFixedSize(4, 40);
        
        QColor typeColor;
        if (entry.type == "note") typeColor = colors.analysisNote;
        else if (entry.type == "finding") typeColor = colors.analysisFinding;
        else if (entry.type == "hypothesis") typeColor = colors.analysisHypothesis;
        else if (entry.type == "question") typeColor = colors.analysisQuestion;
        else if (entry.type == "analysis") typeColor = colors.analysisAnalysis;
        else if (entry.type == "deep_analysis") typeColor = colors.analysisDeepAnalysis;
        else typeColor = colors.textSecondary;
        
        indicator->setStyleSheet(QString("background-color: %1; border-radius: 2px;")
                               .arg(typeColor.name()));
        layout->addWidget(indicator);
        
        // Content
        auto* contentLabel = new QLabel(this);
        contentLabel->setText(entry.content);
        contentLabel->setWordWrap(true);
        contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(contentLabel, 1);
        
        analysisWidget_->layout()->addWidget(entryWidget);
    }
}

void MessageBubble::updateAttachmentsDisplay() {
    if (!message_ || !message_->hasAttachments() || !attachmentsWidget_) return;
    
    // Clear existing attachments
    QLayoutItem* item;
    while ((item = attachmentsWidget_->layout()->takeAt(0))) {
        delete item->widget();
        delete item;
    }
    
    for (const auto& attachment : message_->attachments()) {
        auto* attachButton = new QToolButton(this);
        attachButton->setFixedSize(80, 80);
        attachButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        attachButton->setText(attachment.name);
        attachButton->setToolTip(QString("%1\n%2")
                               .arg(attachment.name)
                               .arg(UIUtils::humanizeBytes(attachment.size)));
        
        // Set icon based on mime type
        if (attachment.mimeType.startsWith("image/")) {
            attachButton->setIcon(ThemeManager::instance().themedIcon("image"));
        } else if (attachment.mimeType.startsWith("text/")) {
            attachButton->setIcon(ThemeManager::instance().themedIcon("document"));
        } else {
            attachButton->setIcon(ThemeManager::instance().themedIcon("attachment"));
        }
        
        connect(attachButton, &QToolButton::clicked, [this, attachment]() {
            emit attachmentClicked(attachment);
        });
        
        attachmentsWidget_->layout()->addWidget(attachButton);
    }
    
    qobject_cast<QHBoxLayout*>(attachmentsWidget_->layout())->addStretch();
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
    
    // Set background color based on role
    if (message_) {
        setBackgroundColor(message_->roleColor());
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
            int startX = message_ && message_->role() == MessageRole::User ?
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
                anim->setDuration(message_->content().length() * 20); // 20ms per char
                anim->setStartValue(0);
                anim->setEndValue(message_->content().length());
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

void MessageBubble::updateToolExecution() {
    if (message_ && message_->hasToolExecution()) {
        createToolExecutionWidget();
        updateToolExecutionDisplay();
        
        // Connect to progress updates if running
        if (message_->toolExecution()->state == ToolExecutionState::Running) {
            // Start a timer to simulate progress updates
            // In real implementation, this would be connected to actual tool execution
            auto* timer = new QTimer(this);
            timer->setInterval(100);
            connect(timer, &QTimer::timeout, this, &MessageBubble::onToolProgressChanged);
            timer->start();
        }
    }
}

void MessageBubble::setToolOutputVisible(bool visible) {
    if (toolOutputVisible_ != visible) {
        toolOutputVisible_ = visible;
        if (toolOutputEdit_) {
            toolOutputEdit_->setVisible(visible);
        }
        if (toolOutputToggle_) {
            toolOutputToggle_->setIcon(ThemeManager::instance().themedIcon(
                visible ? "collapse" : "expand"));
        }
        updateLayout();
        emit toolOutputToggled();
    }
}

void MessageBubble::setSearchHighlight(const QString& text) {
    if (searchHighlight_ != text) {
        searchHighlight_ = text;
        updateContentDisplay();
    }
}

void MessageBubble::clearSearchHighlight() {
    setSearchHighlight(QString());
}

QString MessageBubble::toPlainText() const {
    if (!message_) return QString();
    return message_->content();
}


void MessageBubble::setExpandProgress(qreal progress) {
    expandProgress_ = progress;
    
    // Update visibility of collapsible elements
    bool showFull = progress > 0.5;
    if (toolWidget_) toolWidget_->setVisible(showFull && message_->hasToolExecution());
    if (analysisWidget_) analysisWidget_->setVisible(showFull && message_->hasAnalysis());
    if (attachmentsWidget_) attachmentsWidget_->setVisible(showFull && message_->hasAttachments());
    
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
        QString visibleText = message_->content().left(position);
        if (contentViewer_) {
            contentViewer_->setMarkdown(visibleText);
        } else if (plainTextLabel_) {
            plainTextLabel_->setText(visibleText);
        }
    }
}

QSize MessageBubble::sizeHint() const {
    if (!sizeCacheDirty_ && cachedSize_.isValid()) {
        return cachedSize_;
    }
    
    QSize size = CardWidget::sizeHint();
    
    // Limit width
    if (size.width() > maxWidth_) {
        size.setWidth(maxWidth_);
    }
    
    // Cache the size
    cachedSize_ = size;
    sizeCacheDirty_ = false;
    
    return size;
}

QSize MessageBubble::minimumSizeHint() const {
    return QSize(200, 50);
}

void MessageBubble::updateTheme() {
    applyBubbleStyle();
    updateMessage();
}

void MessageBubble::refresh() {
    updateMessage();
}

void MessageBubble::paintContent(QPainter* painter) {
    CardWidget::paintContent(painter);
    
    // Paint selection overlay
    if (isSelected_) {
        paintSelectionOverlay(painter);
    }
    
    // Paint status indicators
    paintStatusIndicator(painter, rect());
}

void MessageBubble::resizeEvent(QResizeEvent* event) {
    CardWidget::resizeEvent(event);
    sizeCacheDirty_ = true;
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

void MessageBubble::contextMenuEvent(QContextMenuEvent* event) {
    if (contextMenu_ && interactive_) {
        contextMenu_->popup(event->globalPos());
        emit contextMenuRequested(event->pos());
    }
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

void MessageBubble::onContentLinkClicked(const QUrl& url) {
    emit linkClicked(url);
}

void MessageBubble::onCopyAction() {
    QApplication::clipboard()->setText(toPlainText());
    emit copyRequested();
}


void MessageBubble::onEditAction() {
    emit editRequested();
}

void MessageBubble::onDeleteAction() {
    emit deleteRequested();
}

void MessageBubble::onPinAction() {
    if (message_) {
        message_->metadata().isPinned = !message_->metadata().isPinned;
        pinAction_->setChecked(message_->metadata().isPinned);
    }
}

void MessageBubble::onBookmarkAction() {
    if (message_) {
        message_->metadata().isBookmarked = !message_->metadata().isBookmarked;
        bookmarkAction_->setChecked(message_->metadata().isBookmarked);
    }
}


void MessageBubble::onAnimationFinished() {
    currentAnimation_ = nullptr;
    emit animationFinished();
}

void MessageBubble::onToolProgressChanged() {
    // Simulate progress update
    // In real implementation, this would be connected to actual tool execution
    if (message_ && message_->hasToolExecution() && toolProgress_) {
        int value = toolProgress_->value() + 5;
        if (value >= 100) {
            value = 100;
            sender()->deleteLater(); // Stop the timer
        }
        toolProgress_->setValue(value);
    }
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


void MessageBubble::paintStatusIndicator(QPainter* painter, const QRect& rect) {
    if (!message_) return;
    
    const auto& colors = ThemeManager::instance().colors();
    int indicatorSize = 8;
    int margin = 4;
    
    // Draw pin indicator
    if (message_->metadata().isPinned) {
        QRect pinRect(rect.right() - indicatorSize - margin, 
                     rect.top() + margin, indicatorSize, indicatorSize);
        painter->setBrush(colors.primary);
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(pinRect);
    }
    
    // Draw edit indicator
    if (message_->metadata().isEdited) {
        painter->setPen(colors.textTertiary);
        painter->setFont(ThemeManager::instance().typography().caption);
        painter->drawText(rect.adjusted(margin, 0, -margin, -margin),
                         Qt::AlignBottom | Qt::AlignLeft, tr("(edited)"));
    }
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
    vboxLayout->setAlignment(Qt::AlignTop);  // Prevent messages from spreading out
    setLayout(vboxLayout);
    
    // Setup layout timer for batched updates
    layoutTimer_ = new QTimer(this);
    layoutTimer_->setSingleShot(true);
    layoutTimer_->setInterval(50);
    connect(layoutTimer_, &QTimer::timeout, this, &MessageBubbleContainer::performLayout);
}

void MessageBubbleContainer::addMessage(Message* message, bool animated) {
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
        connect(group, &MessageGroup::linkClicked, this, &MessageBubbleContainer::linkClicked);
        
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

void MessageBubbleContainer::insertMessage(int index, Message* message, bool animated) {
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

void MessageBubbleContainer::removeMessage(const QUuid& id, bool animated) {
    MessageBubble* bubble = bubbleMap_.value(id);
    if (!bubble) return;
    
    bubbles_.removeOne(bubble);
    bubbleMap_.remove(id);
    selectedBubbles_.remove(bubble);
    
    if (animated && !batchUpdateCount_) {
        animateRemoval(bubble);
    } else {
        cleanupBubble(bubble);
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

void MessageBubbleContainer::showTypingIndicator(const QString& user) {
    if (!typingIndicator_) {
        typingIndicator_ = new TypingIndicator(this);
        layout()->addWidget(typingIndicator_);
    }
    
    typingIndicator_->setTypingUser(user.isEmpty() ? tr("Assistant") : user);
    typingIndicator_->startAnimation();
    typingIndicator_->show();
    
    // Scroll to bottom to show typing indicator
    if (auto* scrollArea = qobject_cast<QScrollArea*>(parentWidget())) {
        QTimer::singleShot(100, [scrollArea]() {
            scrollArea->verticalScrollBar()->setValue(
                scrollArea->verticalScrollBar()->maximum());
        });
    }
}

void MessageBubbleContainer::hideTypingIndicator() {
    if (typingIndicator_) {
        typingIndicator_->stopAnimation();
        typingIndicator_->hide();
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

void MessageBubbleContainer::setSearchFilter(const QString& text) {
    searchFilter_ = text;
    
    for (MessageBubble* bubble : bubbles_) {
        bubble->setSearchHighlight(text);
    }
}

void MessageBubbleContainer::clearSearchFilter() {
    searchFilter_.clear();
    currentSearchMatch_ = -1;
    
    for (MessageBubble* bubble : bubbles_) {
        bubble->clearSearchHighlight();
    }
}

void MessageBubbleContainer::highlightNextMatch() {
    if (searchFilter_.isEmpty() || bubbles_.isEmpty()) return;
    
    currentSearchMatch_++;
    if (currentSearchMatch_ >= bubbles_.size()) {
        currentSearchMatch_ = 0;
    }
    
    // Find next matching bubble
    for (int i = currentSearchMatch_; i < bubbles_.size(); ++i) {
        if (bubbles_[i]->message() && 
            bubbles_[i]->message()->matchesSearch(searchFilter_)) {
            currentSearchMatch_ = i;
            bubbles_[i]->setHighlighted(true);
            scrollToMessage(bubbles_[i]->message()->id());
            break;
        }
    }
}

void MessageBubbleContainer::highlightPreviousMatch() {
    if (searchFilter_.isEmpty() || bubbles_.isEmpty()) return;
    
    currentSearchMatch_--;
    if (currentSearchMatch_ < 0) {
        currentSearchMatch_ = bubbles_.size() - 1;
    }
    
    // Find previous matching bubble
    for (int i = currentSearchMatch_; i >= 0; --i) {
        if (bubbles_[i]->message() && 
            bubbles_[i]->message()->matchesSearch(searchFilter_)) {
            currentSearchMatch_ = i;
            bubbles_[i]->setHighlighted(true);
            scrollToMessage(bubbles_[i]->message()->id());
            break;
        }
    }
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
    // Could implement custom background here
    QWidget::paintEvent(event);
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
    connect(bubble, &MessageBubble::contextMenuRequested,
            this, &MessageBubbleContainer::onBubbleContextMenu);
    connect(bubble, &MessageBubble::selectionChanged,
            this, &MessageBubbleContainer::onBubbleSelectionChanged);
    connect(bubble, &MessageBubble::linkClicked,
            this, &MessageBubbleContainer::linkClicked);
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
        if (bubble->message()->role() == MessageRole::User) {
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

// TypingIndicator implementation

TypingIndicator::TypingIndicator(QWidget* parent)
    : BaseStyledWidget(parent) {
    
    setShadowEnabled(false);
    setBorderWidth(0);
    setBackgroundColor(ThemeManager::instance().colors().surface);
    setFixedHeight(40);
}

void TypingIndicator::setTypingUser(const QString& user) {
    typingUser_ = user;
    update();
}

void TypingIndicator::startAnimation() {
    if (!animationTimer_) {
        animationTimer_ = new QTimer(this);
        animationTimer_->setInterval(50);
        connect(animationTimer_, &QTimer::timeout, [this]() {
            animationPhase_ += 0.1;
            if (animationPhase_ > 2 * M_PI) {
                animationPhase_ -= 2 * M_PI;
            }
            update();
        });
    }
    animationTimer_->start();
}

void TypingIndicator::stopAnimation() {
    if (animationTimer_) {
        animationTimer_->stop();
    }
}

void TypingIndicator::setAnimationPhase(qreal phase) {
    animationPhase_ = phase;
    update();
}

QSize TypingIndicator::sizeHint() const {
    return QSize(200, 40);
}

void TypingIndicator::paintContent(QPainter* painter) {
    const auto& colors = ThemeManager::instance().colors();
    
    painter->setRenderHint(QPainter::Antialiasing);
    
    // Draw user name
    if (!typingUser_.isEmpty()) {
        painter->setPen(colors.textSecondary);
        painter->setFont(ThemeManager::instance().typography().caption);
        painter->drawText(rect().adjusted(Design::SPACING_MD, 0, 0, 0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         typingUser_ + " is typing");
    }
    
    // Draw animated dots
    int dotSize = 8;
    int dotSpacing = 12;
    int startX = rect().center().x() - dotSpacing;
    int y = rect().center().y();
    
    for (int i = 0; i < 3; ++i) {
        qreal phase = animationPhase_ + i * M_PI / 3;
        qreal scale = 0.5 + 0.5 * sin(phase);
        
        painter->setBrush(colors.textTertiary);
        painter->setPen(Qt::NoPen);
        
        int size = dotSize * scale;
        painter->drawEllipse(QPoint(startX + i * dotSpacing, y), size/2, size/2);
    }
}


} // namespace llm_re::ui_v2