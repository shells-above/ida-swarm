#include "conversation_view.h"
#include "../core/theme_manager.h"
#include "../core/ui_utils.h"
#include "../widgets/command_palette.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QToolButton>
#include <QPushButton>
#include <QTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QSplitter>
#include <QScrollBar>
#include <QMenu>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMimeData>
#include <QPdfWriter>
#include <QPainter>
#include <QTextDocument>
#include <QDragEnterEvent>
#include <QClipboard>
#include <QShortcut>
#include <QUuid>
#include <QDateTime>
#include <QStackedWidget>
#include <QListWidget>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QTime>
#include <QRegularExpression>

namespace llm_re::ui_v2 {

// ConversationView implementation

ConversationView::ConversationView(QWidget* parent)
    : BaseStyledWidget(parent) {
    
    // Create default model
    model_ = new ConversationModel(this);
    ownModel_ = true;
    
    setupUI();
    connectModelSignals();
    generateSessionId();
    
    // Setup auto-save
    autoSaveTimer_ = new QTimer(this);
    connect(autoSaveTimer_, &QTimer::timeout, this, &ConversationView::onAutoSaveTimeout);
    if (autoSaveEnabled_) {
        autoSaveTimer_->start(autoSaveInterval_ * 1000);
    }
    
    // Set initial focus
    QTimer::singleShot(0, this, &ConversationView::focusInput);
}

ConversationView::~ConversationView() {
    if (hasUnsavedChanges_ && autoSaveEnabled_) {
        saveSession();
    }
}

void ConversationView::setupUI() {
    setShadowEnabled(false);
    setBorderWidth(0);
    
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    createToolBar();
    mainLayout->addWidget(toolBar_);
    
    // Main content area
    auto* contentWidget = new QWidget(this);
    auto* contentLayout = new QVBoxLayout(contentWidget);
    contentLayout->setSpacing(0);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    
    createSearchBar();
    contentLayout->addWidget(searchBar_);
    searchBar_->hide();
    
    createMessageArea();
    contentLayout->addWidget(scrollArea_, 1);
    
    createInputArea();
    contentLayout->addWidget(inputContainer_);
    
    createStatusBar();
    contentLayout->addWidget(statusBar_);
    
    mainLayout->addWidget(contentWidget);
    
    // Set drop acceptance
    setAcceptDrops(true);
}

void ConversationView::createToolBar() {
    toolBar_ = new QToolBar(this);
    toolBar_->setMovable(false);
    toolBar_->setIconSize(QSize(16, 16));
    
    // New session
    newSessionAction_ = toolBar_->addAction(
        ThemeManager::instance().themedIcon("new"),
        tr("New Session"), this, &ConversationView::newSession);
    newSessionAction_->setShortcut(QKeySequence::New);
    
    // Save session
    saveSessionAction_ = toolBar_->addAction(
        ThemeManager::instance().themedIcon("save"),
        tr("Save Session"), [this]() { saveSession(); });
    saveSessionAction_->setShortcut(QKeySequence::Save);
    
    // Load session
    loadSessionAction_ = toolBar_->addAction(
        ThemeManager::instance().themedIcon("open"),
        tr("Load Session"), [this]() {
            QString path = QFileDialog::getOpenFileName(
                this, tr("Load Session"), QString(), tr("Session Files (*.json)"));
            if (!path.isEmpty()) {
                loadSession(path);
            }
        });
    loadSessionAction_->setShortcut(QKeySequence::Open);
    
    toolBar_->addSeparator();
    
    
    // Clear
    clearAction_ = toolBar_->addAction(
        ThemeManager::instance().themedIcon("clear"),
        tr("Clear"), [this]() {
            if (model_->rowCount() > 0) {
                int ret = QMessageBox::question(
                    this, tr("Clear Conversation"),
                    tr("Are you sure you want to clear the conversation?"),
                    QMessageBox::Yes | QMessageBox::No);
                if (ret == QMessageBox::Yes) {
                    clearConversation();
                }
            }
        });
    
    toolBar_->addSeparator();
    
    // Search
    searchAction_ = toolBar_->addAction(
        ThemeManager::instance().themedIcon("search"),
        tr("Search"), this, &ConversationView::showSearchBar);
    searchAction_->setShortcut(QKeySequence::Find);
    
    toolBar_->addSeparator();
    
    // View options
    auto* viewButton = new QToolButton(this);
    viewButton->setIcon(ThemeManager::instance().themedIcon("view"));
    viewButton->setPopupMode(QToolButton::InstantPopup);
    viewButton->setToolTip(tr("View Options"));
    
    auto* viewMenu = new QMenu(viewButton);
    
    compactModeAction_ = viewMenu->addAction(tr("Compact Mode"));
    compactModeAction_->setCheckable(true);
    compactModeAction_->setChecked(compactMode_);
    connect(compactModeAction_, &QAction::toggled, [this](bool checked) {
        setCompactMode(checked);
    });
    
    showTimestampsAction_ = viewMenu->addAction(tr("Show Timestamps"));
    showTimestampsAction_->setCheckable(true);
    showTimestampsAction_->setChecked(showTimestamps_);
    connect(showTimestampsAction_, &QAction::toggled, [this](bool checked) {
        setShowTimestamps(checked);
    });
    
    viewMenu->addSeparator();
    
    // Input modes
    auto* inputModeMenu = viewMenu->addMenu(tr("Input Mode"));
    auto* inputModeGroup = new QActionGroup(this);
    
    auto addInputModeAction = [&](const QString& name, const QString& mode) {
        auto* action = inputModeMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(inputMode_ == mode);
        action->setActionGroup(inputModeGroup);
        connect(action, &QAction::triggered, [this, mode]() {
            setInputMode(mode);
        });
    };
    
    addInputModeAction(tr("Single Line"), "single");
    addInputModeAction(tr("Multi Line"), "multi");
    
    viewMenu->addSeparator();
    
    // Bubble styles
    auto* styleMenu = viewMenu->addMenu(tr("Bubble Style"));
    auto* styleGroup = new QActionGroup(this);
    
    auto addStyleAction = [&](const QString& name, MessageBubble::BubbleStyle style) {
        auto* action = styleMenu->addAction(name);
        action->setCheckable(true);
        action->setChecked(bubbleStyle_ == style);
        action->setActionGroup(styleGroup);
        connect(action, &QAction::triggered, [this, style]() {
            setBubbleStyle(style);
        });
    };
    
    addStyleAction(tr("Classic"), MessageBubble::BubbleStyle::Classic);
    addStyleAction(tr("Modern"), MessageBubble::BubbleStyle::Modern);
    addStyleAction(tr("Minimal"), MessageBubble::BubbleStyle::Minimal);
    addStyleAction(tr("Terminal"), MessageBubble::BubbleStyle::Terminal);
    addStyleAction(tr("Paper"), MessageBubble::BubbleStyle::Paper);
    
    viewButton->setMenu(viewMenu);
    toolBar_->addWidget(viewButton);
    
    // Spacer
    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar_->addWidget(spacer);
    
    // Session info
    auto* sessionLabel = new QLabel(this);
    sessionLabel->setText(tr("Session: %1").arg(sessionId_.left(8)));
    sessionLabel->setStyleSheet(QString("color: %1; margin: 0 10px;")
                              .arg(ThemeManager::instance().colors().textSecondary.name()));
    toolBar_->addWidget(sessionLabel);
}

void ConversationView::createMessageArea() {
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    bubbleContainer_ = new MessageBubbleContainer(scrollArea_);
    bubbleContainer_->setBubbleStyle(bubbleStyle_);
    bubbleContainer_->setCompactMode(compactMode_);
    bubbleContainer_->setMaxBubbleWidth(maxBubbleWidth_);
    
    scrollArea_->setWidget(bubbleContainer_);
    
    // Connect bubble signals
    connect(bubbleContainer_, &MessageBubbleContainer::bubbleClicked,
            this, &ConversationView::onBubbleClicked);
    connect(bubbleContainer_, &MessageBubbleContainer::bubbleContextMenu,
            this, &ConversationView::onBubbleContextMenu);
    connect(bubbleContainer_, &MessageBubbleContainer::linkClicked,
            this, &ConversationView::onBubbleLinkClicked);
    connect(bubbleContainer_, &MessageBubbleContainer::selectionChanged,
            this, &ConversationView::selectionChanged);
    
    // Monitor scroll position
    connect(scrollArea_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &ConversationView::onScrollPositionChanged);
    
    // Typing indicator
    typingIndicator_ = new TypingIndicator(this);
    typingIndicator_->hide();
}

void ConversationView::createInputArea() {
    inputContainer_ = new QWidget(this);
    auto* layout = new QVBoxLayout(inputContainer_);
    layout->setSpacing(Design::SPACING_SM);
    layout->setContentsMargins(Design::SPACING_MD, Design::SPACING_SM, 
                              Design::SPACING_MD, Design::SPACING_MD);
    
    // Input area with buttons
    auto* inputLayout = new QHBoxLayout();
    inputLayout->setSpacing(Design::SPACING_SM);
    
    // Use ConversationInputArea instead of QTextEdit
    inputArea_ = new ConversationInputArea(this);
    inputArea_->setPlaceholder(tr("Type a message..."));
    
    // Connect signals
    connect(inputArea_, &ConversationInputArea::textChanged,
            this, &ConversationView::onInputTextChanged);
    connect(inputArea_, &ConversationInputArea::submitRequested,
            this, &ConversationView::submitInput);
    connect(inputArea_, &ConversationInputArea::cancelRequested,
            this, &ConversationView::cancelInput);
    connect(inputArea_, &ConversationInputArea::fileDropped,
            this, &ConversationView::handleFileDropped);
    
    inputLayout->addWidget(inputArea_, 1);
    
    // Button container
    auto* buttonContainer = new QWidget(this);
    auto* buttonLayout = new QVBoxLayout(buttonContainer);
    buttonLayout->setSpacing(Design::SPACING_XS);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    
    // Send button
    sendButton_ = new QPushButton(tr("Send"), this);
    sendButton_->setIcon(ThemeManager::instance().themedIcon("send"));
    sendButton_->setEnabled(false);
    sendButton_->setDefault(true);
    connect(sendButton_, &QPushButton::clicked, this, &ConversationView::submitInput);
    
    // Cancel button (hidden by default)
    cancelButton_ = new QPushButton(tr("Cancel"), this);
    cancelButton_->setIcon(ThemeManager::instance().themedIcon("cancel"));
    cancelButton_->hide();
    connect(cancelButton_, &QPushButton::clicked, this, &ConversationView::cancelInput);
    
    buttonLayout->addWidget(sendButton_);
    buttonLayout->addWidget(cancelButton_);
    buttonLayout->addStretch();
    
    inputLayout->addWidget(buttonContainer);
    
    layout->addLayout(inputLayout);
    
    // Keyboard shortcuts
    // Shortcuts are now handled by ConversationInputArea internally
}

void ConversationView::createSearchBar() {
    searchBar_ = new ConversationSearchBar(this);
    
    connect(searchBar_, &ConversationSearchBar::searchTextChanged,
            this, &ConversationView::onSearchTextChanged);
    connect(searchBar_, &ConversationSearchBar::findNextRequested,
            this, &ConversationView::findNext);
    connect(searchBar_, &ConversationSearchBar::findPreviousRequested,
            this, &ConversationView::findPrevious);
    connect(searchBar_, &ConversationSearchBar::closeRequested,
            this, &ConversationView::hideSearchBar);
}

void ConversationView::createStatusBar() {
    statusBar_ = new QWidget(this);
    statusBar_->setFixedHeight(24);
    
    auto* layout = new QHBoxLayout(statusBar_);
    layout->setSpacing(Design::SPACING_MD);
    layout->setContentsMargins(Design::SPACING_MD, 0, Design::SPACING_MD, 0);
    
    statusLabel_ = new QLabel(tr("Ready"), this);
    statusLabel_->setFont(ThemeManager::instance().typography().caption);
    layout->addWidget(statusLabel_);
    
    layout->addStretch();
    
    wordCountLabel_ = new QLabel(this);
    wordCountLabel_->setFont(ThemeManager::instance().typography().caption);
    layout->addWidget(wordCountLabel_);
    
    // Style
    const auto& colors = ThemeManager::instance().colors();
    statusBar_->setStyleSheet(QString(
        "QWidget { background-color: %1; border-top: 1px solid %2; }"
        "QLabel { color: %3; }"
    ).arg(colors.surface.name())
     .arg(colors.border.name())
     .arg(colors.textSecondary.name()));
}

void ConversationView::setModel(ConversationModel* model) {
    if (model_ == model) return;
    
    // Disconnect old model
    if (model_) {
        disconnectModelSignals();
        if (ownModel_) {
            delete model_;
        }
    }
    
    // Set new model
    model_ = model;
    ownModel_ = false;
    
    if (model_) {
        connectModelSignals();
        updateMessageBubbles();
    }
}

void ConversationView::connectModelSignals() {
    if (!model_) return;
    
    connect(model_, &ConversationModel::dataChanged,
            this, &ConversationView::onModelDataChanged);
    connect(model_, &ConversationModel::rowsInserted,
            this, &ConversationView::onModelRowsInserted);
    connect(model_, &ConversationModel::rowsRemoved,
            this, &ConversationView::onModelRowsRemoved);
    connect(model_, &ConversationModel::modelReset,
            this, &ConversationView::updateMessageBubbles);
    
    connect(model_, &ConversationModel::messageAdded,
            this, &ConversationView::messageAdded);
    connect(model_, &ConversationModel::conversationCleared,
            this, &ConversationView::conversationCleared);
}

void ConversationView::disconnectModelSignals() {
    if (!model_) return;
    
    disconnect(model_, nullptr, this, nullptr);
}

void ConversationView::addMessage(std::unique_ptr<Message> message) {
    if (!model_) return;
    
    model_->addMessage(std::move(message));
    markUnsavedChanges();
}

void ConversationView::addUserMessage(const QString& content) {
    auto msg = std::make_unique<Message>(content, MessageRole::User);
    msg->metadata().author = tr("User");
    addMessage(std::move(msg));
}

void ConversationView::addAssistantMessage(const QString& content) {
    auto msg = std::make_unique<Message>(content, MessageRole::Assistant);
    msg->metadata().author = tr("Assistant");
    addMessage(std::move(msg));
}

void ConversationView::addSystemMessage(const QString& content) {
    auto msg = std::make_unique<Message>(content, MessageRole::System);
    msg->metadata().author = tr("System");
    addMessage(std::move(msg));
}

void ConversationView::addToolMessage(const QString& toolName, const QString& content) {
    auto msg = std::make_unique<Message>(content, MessageRole::Tool);
    msg->metadata().author = toolName;
    
    auto exec = std::make_unique<ToolExecution>();
    exec->toolName = toolName;
    exec->state = ToolExecutionState::Pending;
    msg->setToolExecution(std::move(exec));
    
    addMessage(std::move(msg));
}

void ConversationView::clearConversation() {
    if (!model_) return;
    
    model_->clearMessages();
    bubbleContainer_->clearMessages();
    clearUnsavedChanges();
}

void ConversationView::scrollToBottom(bool animated) {
    programmaticScroll_ = true;
    bubbleContainer_->scrollToBottom(animated);
    programmaticScroll_ = false;
}

void ConversationView::scrollToMessage(const QUuid& id, bool animated) {
    programmaticScroll_ = true;
    bubbleContainer_->scrollToMessage(id, animated);
    programmaticScroll_ = false;
}

void ConversationView::focusInput() {
    if (inputArea_) {
        inputArea_->focus();
    }
}

void ConversationView::showSearchBar() {
    searchBar_->show();
    searchBar_->focusSearch();
    
    // Apply current search to bubbles
    if (!searchBar_->searchText().isEmpty()) {
        bubbleContainer_->setSearchFilter(searchBar_->searchText());
    }
}

void ConversationView::hideSearchBar() {
    searchBar_->hide();
    bubbleContainer_->clearSearchFilter();
    focusInput();
}

void ConversationView::findNext() {
    bubbleContainer_->highlightNextMatch();
}

void ConversationView::findPrevious() {
    bubbleContainer_->highlightPreviousMatch();
}

void ConversationView::selectMessage(const QUuid& id) {
    bubbleContainer_->selectBubble(id);
}

void ConversationView::selectAll() {
    bubbleContainer_->selectAll();
}

void ConversationView::clearSelection() {
    bubbleContainer_->clearSelection();
}

QList<Message*> ConversationView::selectedMessages() const {
    QList<Message*> messages;
    for (MessageBubble* bubble : bubbleContainer_->getSelectedBubbles()) {
        if (bubble->message()) {
            messages.append(bubble->message());
        }
    }
    return messages;
}


void ConversationView::copySelectedMessages() {
    QStringList texts;
    for (Message* msg : selectedMessages()) {
        texts.append(QString("%1: %2").arg(msg->roleString()).arg(msg->content()));
    }
    
    if (!texts.isEmpty()) {
        QApplication::clipboard()->setText(texts.join("\n\n"));
        statusLabel_->setText(tr("Copied %1 messages").arg(texts.size()));
    }
}

void ConversationView::setBubbleStyle(MessageBubble::BubbleStyle style) {
    bubbleStyle_ = style;
    bubbleContainer_->setBubbleStyle(style);
    markUnsavedChanges();
}

void ConversationView::setCompactMode(bool compact) {
    compactMode_ = compact;
    bubbleContainer_->setCompactMode(compact);
    compactModeAction_->setChecked(compact);
    markUnsavedChanges();
}


void ConversationView::setShowTimestamps(bool show) {
    showTimestamps_ = show;
    
    // Update all bubbles
    for (MessageBubble* bubble : bubbleContainer_->getAllBubbles()) {
        bubble->setShowTimestamp(show);
    }
    
    showTimestampsAction_->setChecked(show);
    markUnsavedChanges();
}

void ConversationView::setMaxBubbleWidth(int width) {
    maxBubbleWidth_ = width;
    bubbleContainer_->setMaxBubbleWidth(width);
    markUnsavedChanges();
}

void ConversationView::setInputMode(const QString& mode) {
    if (inputMode_ != mode) {
        inputMode_ = mode;
        
        // Update the input area mode
        if (inputArea_) {
            if (mode == "single") {
                inputArea_->setMode(ConversationInputArea::SingleLine);
            } else if (mode == "multi") {
                inputArea_->setMode(ConversationInputArea::MultiLine);
            }
        }
        
        emit inputModeChanged(mode);
        markUnsavedChanges();
    }
}

void ConversationView::startToolExecution(const QUuid& messageId, const QString& toolName) {
    if (!model_) return;
    
    model_->setToolExecutionState(messageId, ToolExecutionState::Running);
    
    // Update bubble
    if (MessageBubble* bubble = bubbleContainer_->getBubble(messageId)) {
        bubble->updateToolExecution();
    }
}

void ConversationView::updateToolProgress(const QUuid& messageId, int progress, const QString& status) {
    if (!model_) return;
    
    model_->setToolExecutionProgress(messageId, progress, status);
    
    // Update bubble
    if (MessageBubble* bubble = bubbleContainer_->getBubble(messageId)) {
        bubble->updateToolExecution();
    }
}

void ConversationView::completeToolExecution(const QUuid& messageId, bool success, const QString& output) {
    if (!model_) return;
    
    model_->setToolExecutionState(messageId, 
                                 success ? ToolExecutionState::Completed : ToolExecutionState::Failed);
    
    if (!output.isEmpty()) {
        model_->addToolExecutionOutput(messageId, output);
    }
    
    // Update bubble
    if (MessageBubble* bubble = bubbleContainer_->getBubble(messageId)) {
        bubble->updateToolExecution();
    }
}

void ConversationView::setAutoSaveEnabled(bool enabled) {
    autoSaveEnabled_ = enabled;
    
    if (enabled && autoSaveTimer_) {
        autoSaveTimer_->start(autoSaveInterval_ * 1000);
    } else if (autoSaveTimer_) {
        autoSaveTimer_->stop();
    }
}

void ConversationView::setAutoSaveInterval(int seconds) {
    autoSaveInterval_ = seconds;
    
    if (autoSaveEnabled_ && autoSaveTimer_) {
        autoSaveTimer_->setInterval(seconds * 1000);
    }
}

void ConversationView::saveSession(const QString& path) {
    if (!model_) return;
    
    QString savePath = path;
    if (savePath.isEmpty()) {
        if (sessionPath_.isEmpty()) {
            savePath = QFileDialog::getSaveFileName(
                this, tr("Save Session"),
                QString("session_%1.json").arg(sessionId_),
                tr("Session Files (*.json)"));
            
            if (savePath.isEmpty()) return;
            sessionPath_ = savePath;
        } else {
            savePath = sessionPath_;
        }
    }
    
    // Create session data
    QJsonObject session;
    session["id"] = sessionId_;
    session["version"] = 1;
    session["created"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    // Note: export functionality has been removed
    // Session saving now only stores settings, not messages
    
    // Settings
    QJsonObject settings;
    settings["bubbleStyle"] = static_cast<int>(bubbleStyle_);
    settings["compactMode"] = compactMode_;
    settings["showTimestamps"] = showTimestamps_;
    settings["maxBubbleWidth"] = maxBubbleWidth_;
    settings["inputMode"] = inputMode_;
    session["settings"] = settings;
    
    // Write file
    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(session);
        file.write(doc.toJson());
        
        clearUnsavedChanges();
        statusLabel_->setText(tr("Session saved"));
    } else {
        QMessageBox::warning(this, tr("Save Failed"),
                           tr("Failed to save session."));
    }
}

void ConversationView::loadSession(const QString& path) {
    if (!model_) return;
    
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Load Failed"),
                           tr("Failed to open session file."));
        return;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        QMessageBox::warning(this, tr("Load Failed"),
                           tr("Invalid session file format."));
        return;
    }
    
    QJsonObject session = doc.object();
    
    // Check version
    if (session["version"].toInt() != 1) {
        QMessageBox::warning(this, tr("Load Failed"),
                           tr("Unsupported session file version."));
        return;
    }
    
    // Clear current conversation
    clearConversation();
    
    // Load session data
    sessionId_ = session["id"].toString();
    sessionPath_ = path;
    
    // Load messages
    QJsonDocument messagesDoc;
    QJsonObject messagesObj;
    messagesObj["messages"] = session["messages"];
    messagesDoc.setObject(messagesObj);
    model_->importFromJson(messagesDoc);
    
    // Load settings
    if (session.contains("settings")) {
        QJsonObject settings = session["settings"].toObject();
        setBubbleStyle(static_cast<MessageBubble::BubbleStyle>(
            settings["bubbleStyle"].toInt()));
        setCompactMode(settings["compactMode"].toBool());
        setShowTimestamps(settings["showTimestamps"].toBool());
        setMaxBubbleWidth(settings["maxBubbleWidth"].toInt());
        setInputMode(settings["inputMode"].toString());
    }
    
    clearUnsavedChanges();
    statusLabel_->setText(tr("Session loaded"));
    emit sessionChanged(sessionId_);
}

void ConversationView::newSession() {
    if (hasUnsavedChanges_) {
        int ret = QMessageBox::question(
            this, tr("New Session"),
            tr("Current session has unsaved changes. Save before creating new session?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        
        if (ret == QMessageBox::Save) {
            saveSession();
        } else if (ret == QMessageBox::Cancel) {
            return;
        }
    }
    
    clearConversation();
    generateSessionId();
    sessionPath_.clear();
    clearUnsavedChanges();
    
    emit sessionChanged(sessionId_);
}

void ConversationView::submitInput() {
    if (!inputArea_) return;
    
    QString text = inputArea_->text().trimmed();
    if (text.isEmpty()) return;
    
    // Add user message
    addUserMessage(text);
    
    // Clear input
    inputArea_->clear();
    inputArea_->focus();
    
    // Scroll to bottom
    scrollToBottom(true);
    
    // Emit signal for processing
    emit messageSubmitted(text);
}

void ConversationView::cancelInput() {
    if (inputArea_) {
        inputArea_->clear();
    }
    sendButton_->setText(tr("Send"));
    cancelButton_->hide();
}

void ConversationView::showTypingIndicator(const QString& user) {
    if (!typingIndicator_) return;
    
    typingIndicator_->setTypingUser(user.isEmpty() ? tr("Assistant") : user);
    typingIndicator_->startAnimation();
    
    // Add to bubble container
    bubbleContainer_->showTypingIndicator(user);
    
    scrollToBottom(true);
}

void ConversationView::hideTypingIndicator() {
    if (!typingIndicator_) return;
    
    typingIndicator_->stopAnimation();
    typingIndicator_->hide();
    
    // Hide in bubble container
    bubbleContainer_->hideTypingIndicator();
}

void ConversationView::updateTheme() {
    // Handled by onThemeChanged
}

void ConversationView::resizeEvent(QResizeEvent* event) {
    BaseStyledWidget::resizeEvent(event);
    
    // Update max bubble width based on view width
    int viewWidth = scrollArea_->viewport()->width();
    int maxWidth = qMin(800, viewWidth - 100);
    if (maxWidth != maxBubbleWidth_) {
        setMaxBubbleWidth(maxWidth);
    }
}

void ConversationView::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
}

void ConversationView::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void ConversationView::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    
    if (mimeData->hasUrls()) {
        for (const QUrl& url : mimeData->urls()) {
            if (url.isLocalFile()) {
                handleFileDropped(url.toLocalFile());
            }
        }
    } else if (mimeData->hasText()) {
        if (inputArea_) {
            inputArea_->setText(inputArea_->text() + mimeData->text());
        }
    }
    
    event->acceptProposedAction();
}

void ConversationView::keyPressEvent(QKeyEvent* event) {
    // Global shortcuts
    if (event->matches(QKeySequence::Find)) {
        showSearchBar();
        event->accept();
        return;
    }
    
    BaseStyledWidget::keyPressEvent(event);
}

void ConversationView::onThemeChanged() {
    BaseStyledWidget::onThemeChanged();
    
    // Update status bar styling
    if (statusBar_) {
        const auto& colors = ThemeManager::instance().colors();
        statusBar_->setStyleSheet(QString(
            "QWidget { background-color: %1; border-top: 1px solid %2; }"
            "QLabel { color: %3; }"
        ).arg(colors.surface.name())
         .arg(colors.border.name())
         .arg(colors.textSecondary.name()));
    }
}

bool ConversationView::eventFilter(QObject* watched, QEvent* event) {
    // Input handling is now done by ConversationInputArea
    
    return BaseStyledWidget::eventFilter(watched, event);
}

void ConversationView::onModelDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight) {
    // Update affected bubbles
    for (int row = topLeft.row(); row <= bottomRight.row(); ++row) {
        Message* msg = model_->getMessageAt(row);
        if (msg) {
            MessageBubble* bubble = bubbleContainer_->getBubble(msg->id());
            if (bubble) {
                bubble->updateMessage();
            }
        }
    }
}

void ConversationView::onModelRowsInserted(const QModelIndex& parent, int first, int last) {
    Q_UNUSED(parent)
    
    // Add new bubbles
    for (int row = first; row <= last; ++row) {
        Message* msg = model_->getMessageAt(row);
        if (msg) {
            bubbleContainer_->addMessage(msg, true);
        }
    }
    
    // Scroll to bottom if was at bottom
    if (isAtBottom_) {
        scrollToBottom(true);
    }
}

void ConversationView::onModelRowsRemoved(const QModelIndex& parent, int first, int last) {
    Q_UNUSED(parent)
    Q_UNUSED(first)
    Q_UNUSED(last)
    
    // Bubbles handle their own removal
}

void ConversationView::onBubbleClicked(const QUuid& id) {
    emit messageSelected(id);
}

void ConversationView::onBubbleContextMenu(const QUuid& id, const QPoint& pos) {
    Message* msg = model_->getMessage(id);
    if (!msg) return;
    
    QMenu menu(this);
    
    menu.addAction(ThemeManager::instance().themedIcon("copy"), tr("Copy"), [this, msg]() {
        QApplication::clipboard()->setText(msg->content());
    });
    
    if (msg->role() == MessageRole::User) {
        menu.addAction(ThemeManager::instance().themedIcon("edit"), tr("Edit"), [this, id]() {
            onBubbleEditRequested(id);
        });
    }
    
    menu.addSeparator();
    
    
    auto* pinAction = menu.addAction(ThemeManager::instance().themedIcon("pin"), 
                                    msg->metadata().isPinned ? tr("Unpin") : tr("Pin"));
    connect(pinAction, &QAction::triggered, [this, id, msg]() {
        model_->setPinned(id, !msg->metadata().isPinned);
    });
    
    auto* bookmarkAction = menu.addAction(ThemeManager::instance().themedIcon("bookmark"),
                                         msg->metadata().isBookmarked ? tr("Remove Bookmark") : tr("Bookmark"));
    connect(bookmarkAction, &QAction::triggered, [this, id, msg]() {
        model_->setBookmarked(id, !msg->metadata().isBookmarked);
    });
    
    menu.addSeparator();
    
    menu.addAction(ThemeManager::instance().themedIcon("delete"), tr("Delete"), [this, id]() {
        onBubbleDeleteRequested(id);
    });
    
    menu.exec(pos);
}

void ConversationView::onBubbleLinkClicked(const QUrl& url) {
    emit linkClicked(url);
}


void ConversationView::onBubbleEditRequested(const QUuid& id) {
    Message* msg = model_->getMessage(id);
    if (!msg || msg->role() != MessageRole::User) return;
    
    // Put message content in input for editing
    if (inputArea_) {
        inputArea_->setText(msg->content());
        inputArea_->focus();
        inputArea_->selectAll();
    }
    
    // Delete original message
    model_->removeMessage(id);
}

void ConversationView::onBubbleDeleteRequested(const QUuid& id) {
    int ret = QMessageBox::question(
        this, tr("Delete Message"),
        tr("Are you sure you want to delete this message?"),
        QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        model_->removeMessage(id);
        markUnsavedChanges();
    }
}

void ConversationView::onInputTextChanged() {
    updateSendButtonState();
    
    // Update word count
    if (inputArea_) {
        int words = inputArea_->wordCount();
        int chars = inputArea_->charCount();
        wordCountLabel_->setText(tr("%1 words, %2 chars").arg(words).arg(chars));
    }
}

void ConversationView::onSearchTextChanged(const QString& text) {
    currentSearchText_ = text;
    
    if (text.isEmpty()) {
        bubbleContainer_->clearSearchFilter();
        model_->clearFilters();
    } else {
        bubbleContainer_->setSearchFilter(text);
        model_->setSearchFilter(text);
        
        // Update search status
        int matches = model_->getSearchMatchCount();
        searchBar_->setMatchCount(currentSearchIndex_ + 1, matches);
    }
    
    emit searchRequested(text);
}

void ConversationView::onScrollPositionChanged() {
    if (programmaticScroll_) return;
    
    // Check if at bottom
    QScrollBar* vbar = scrollArea_->verticalScrollBar();
    bool wasAtBottom = isAtBottom_;
    isAtBottom_ = vbar->value() >= vbar->maximum() - 10;
    
    if (!wasAtBottom && isAtBottom_) {
        emit scrolledToBottom();
    }
}

void ConversationView::onAutoSaveTimeout() {
    if (hasUnsavedChanges_ && !sessionPath_.isEmpty()) {
        saveSession(sessionPath_);
    }
}

void ConversationView::updateSendButtonState() {
    bool hasText = inputArea_ && inputArea_->hasText();
    sendButton_->setEnabled(hasText);
}

void ConversationView::handleFileDropped(const QString& filePath) {
    // Add as attachment or process based on file type
    QFileInfo info(filePath);
    
    if (info.suffix().toLower() == "json") {
        // Try to load as session
        int ret = QMessageBox::question(
            this, tr("Load Session"),
            tr("Load dropped file as session?"),
            QMessageBox::Yes | QMessageBox::No);
        
        if (ret == QMessageBox::Yes) {
            loadSession(filePath);
            return;
        }
    }
    
    // Otherwise add reference to input
    if (inputArea_) {
        QString currentText = inputArea_->text();
        inputArea_->setText(currentText + QString("[File: %1]").arg(info.fileName()));
    }
}

void ConversationView::updateMessageBubbles() {
    if (!model_) return;
    
    // Clear and recreate all bubbles
    bubbleContainer_->clearMessages(false);
    
    for (int i = 0; i < model_->rowCount(); ++i) {
        Message* msg = model_->getMessageAt(i);
        if (msg) {
            bubbleContainer_->addMessage(msg, false);
        }
    }
}

void ConversationView::markUnsavedChanges() {
    if (!hasUnsavedChanges_) {
        hasUnsavedChanges_ = true;
        emit unsavedChangesChanged(true);
        
        // Update window title or status
        if (saveSessionAction_) {
            saveSessionAction_->setText(tr("Save Session*"));
        }
    }
}

void ConversationView::clearUnsavedChanges() {
    if (hasUnsavedChanges_) {
        hasUnsavedChanges_ = false;
        emit unsavedChangesChanged(false);
        
        if (saveSessionAction_) {
            saveSessionAction_->setText(tr("Save Session"));
        }
    }
}

void ConversationView::generateSessionId() {
    sessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
}

// ConversationSearchBar implementation

ConversationSearchBar::ConversationSearchBar(QWidget* parent)
    : BaseStyledWidget(parent) {
    
    setupUI();
    
    setShadowEnabled(false);
    setBorderWidth(0);
    setBackgroundColor(ThemeManager::instance().colors().surface);
}

void ConversationSearchBar::setupUI() {
    auto* layout = new QHBoxLayout(this);
    layout->setSpacing(Design::SPACING_SM);
    layout->setContentsMargins(Design::SPACING_MD, Design::SPACING_SM, 
                              Design::SPACING_MD, Design::SPACING_SM);
    
    // Search input
    searchInput_ = new QLineEdit(this);
    searchInput_->setPlaceholderText(tr("Find in conversation..."));
    searchInput_->setClearButtonEnabled(true);
    connect(searchInput_, &QLineEdit::textChanged,
            this, &ConversationSearchBar::searchTextChanged);
    
    layout->addWidget(searchInput_, 1);
    
    // Match count label
    matchLabel_ = new QLabel(this);
    matchLabel_->setFont(ThemeManager::instance().typography().caption);
    layout->addWidget(matchLabel_);
    
    // Navigation buttons
    prevButton_ = new QToolButton(this);
    prevButton_->setIcon(ThemeManager::instance().themedIcon("arrow-up"));
    prevButton_->setToolTip(tr("Previous match (Shift+F3)"));
    prevButton_->setAutoRaise(true);
    connect(prevButton_, &QToolButton::clicked,
            this, &ConversationSearchBar::findPreviousRequested);
    layout->addWidget(prevButton_);
    
    nextButton_ = new QToolButton(this);
    nextButton_->setIcon(ThemeManager::instance().themedIcon("arrow-down"));
    nextButton_->setToolTip(tr("Next match (F3)"));
    nextButton_->setAutoRaise(true);
    connect(nextButton_, &QToolButton::clicked,
            this, &ConversationSearchBar::findNextRequested);
    layout->addWidget(nextButton_);
    
    layout->addSpacing(Design::SPACING_SM);
    
    // Options
    caseSensitiveButton_ = new QToolButton(this);
    caseSensitiveButton_->setText("Aa");
    caseSensitiveButton_->setToolTip(tr("Case sensitive"));
    caseSensitiveButton_->setCheckable(true);
    caseSensitiveButton_->setAutoRaise(true);
    connect(caseSensitiveButton_, &QToolButton::toggled,
            this, &ConversationSearchBar::caseSensitivityChanged);
    layout->addWidget(caseSensitiveButton_);
    
    wholeWordButton_ = new QToolButton(this);
    wholeWordButton_->setText("W");
    wholeWordButton_->setToolTip(tr("Whole word"));
    wholeWordButton_->setCheckable(true);
    wholeWordButton_->setAutoRaise(true);
    connect(wholeWordButton_, &QToolButton::toggled,
            this, &ConversationSearchBar::wholeWordChanged);
    layout->addWidget(wholeWordButton_);
    
    regexButton_ = new QToolButton(this);
    regexButton_->setText(".*");
    regexButton_->setToolTip(tr("Regular expression"));
    regexButton_->setCheckable(true);
    regexButton_->setAutoRaise(true);
    connect(regexButton_, &QToolButton::toggled,
            this, &ConversationSearchBar::regexChanged);
    layout->addWidget(regexButton_);
    
    layout->addSpacing(Design::SPACING_SM);
    
    // Close button
    closeButton_ = new QToolButton(this);
    closeButton_->setIcon(ThemeManager::instance().themedIcon("close"));
    closeButton_->setToolTip(tr("Close (Escape)"));
    closeButton_->setAutoRaise(true);
    connect(closeButton_, &QToolButton::clicked,
            this, &ConversationSearchBar::closeRequested);
    layout->addWidget(closeButton_);
    
    // Keyboard shortcuts
    auto* findNextShortcut = new QShortcut(QKeySequence("F3"), this);
    connect(findNextShortcut, &QShortcut::activated,
            this, &ConversationSearchBar::findNextRequested);
    
    auto* findPrevShortcut = new QShortcut(QKeySequence("Shift+F3"), this);
    connect(findPrevShortcut, &QShortcut::activated,
            this, &ConversationSearchBar::findPreviousRequested);
}

void ConversationSearchBar::setSearchText(const QString& text) {
    searchInput_->setText(text);
}

QString ConversationSearchBar::searchText() const {
    return searchInput_->text();
}

void ConversationSearchBar::setMatchCount(int current, int total) {
    if (total == 0) {
        matchLabel_->setText(tr("No matches"));
        prevButton_->setEnabled(false);
        nextButton_->setEnabled(false);
    } else {
        matchLabel_->setText(tr("%1 of %2").arg(current).arg(total));
        prevButton_->setEnabled(true);
        nextButton_->setEnabled(true);
    }
}

void ConversationSearchBar::focusSearch() {
    searchInput_->setFocus();
    searchInput_->selectAll();
}

void ConversationSearchBar::showMessage(const QString& message, int timeout) {
    matchLabel_->setText(message);
    if (timeout > 0) {
        QTimer::singleShot(timeout, [this]() {
            matchLabel_->clear();
        });
    }
}

void ConversationSearchBar::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        emit closeRequested();
        event->accept();
        return;
    }
    
    BaseStyledWidget::keyPressEvent(event);
}

// ConversationInputArea implementation

ConversationInputArea::ConversationInputArea(QWidget* parent)
    : BaseStyledWidget(parent) {
    
    setShadowEnabled(false);
    setBorderWidth(1);
    
    // Create main layout
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    
    // Initialize with multi-line mode by default
    setMode(MultiLine);
}

void ConversationInputArea::setMode(InputMode mode) {
    if (mode_ == mode) return;
    
    mode_ = mode;
    updateLayout();
    emit modeChanged(mode);
}

QString ConversationInputArea::text() const {
    if (singleLineEdit_ && currentWidget_ == singleLineEdit_) {
        return singleLineEdit_->text();
    } else if (multiLineEdit_ && currentWidget_ == multiLineEdit_) {
        return multiLineEdit_->toPlainText();
    }
    return QString();
}

void ConversationInputArea::setText(const QString& text) {
    if (singleLineEdit_ && currentWidget_ == singleLineEdit_) {
        singleLineEdit_->setText(text);
    } else if (multiLineEdit_ && currentWidget_ == multiLineEdit_) {
        multiLineEdit_->setPlainText(text);
    }
}

void ConversationInputArea::clear() {
    if (singleLineEdit_) singleLineEdit_->clear();
    if (multiLineEdit_) multiLineEdit_->clear();
}

void ConversationInputArea::focus() {
    if (currentWidget_) {
        currentWidget_->setFocus();
    }
}

void ConversationInputArea::selectAll() {
    if (singleLineEdit_ && currentWidget_ == singleLineEdit_) {
        singleLineEdit_->selectAll();
    } else if (multiLineEdit_ && currentWidget_ == multiLineEdit_) {
        multiLineEdit_->selectAll();
    }
}

void ConversationInputArea::setPlaceholder(const QString& text) {
    if (singleLineEdit_) singleLineEdit_->setPlaceholderText(text);
    if (multiLineEdit_) multiLineEdit_->setPlaceholderText(text);
}

void ConversationInputArea::setMaxLength(int length) {
    if (singleLineEdit_) singleLineEdit_->setMaxLength(length);
    // For QTextEdit, we'll need to handle this in textChanged signal
}

int ConversationInputArea::wordCount() const {
    QString content = text();
    if (content.isEmpty()) return 0;
    return content.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts).count();
}

int ConversationInputArea::charCount() const {
    return text().length();
}


void ConversationInputArea::keyPressEvent(QKeyEvent* event) {
    // Mode-specific key handling will be implemented in the respective setup methods
    BaseStyledWidget::keyPressEvent(event);
}

bool ConversationInputArea::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        
        // Multi-line mode key handling
        if (mode_ == MultiLine && watched == multiLineEdit_) {
            // Ctrl+Enter (or Cmd+Enter on Mac) to submit
            if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
                (keyEvent->modifiers() & Qt::ControlModifier)) {
                emit submitRequested();
                return true;
            }
            // Shift+Enter for new line (default behavior)
            else if ((keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) &&
                     (keyEvent->modifiers() & Qt::ShiftModifier)) {
                // Let the default behavior handle this
                return false;
            }
            // Plain Enter also creates new line in multi-line mode
            else if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                // Let the default behavior handle this
                return false;
            }
            // Escape to cancel
            else if (keyEvent->key() == Qt::Key_Escape) {
                emit cancelRequested();
                return true;
            }
        }
        
    }
    
    return BaseStyledWidget::eventFilter(watched, event);
}

void ConversationInputArea::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls() || event->mimeData()->hasText()) {
        event->acceptProposedAction();
    }
}

void ConversationInputArea::dragMoveEvent(QDragMoveEvent* event) {
    event->acceptProposedAction();
}

void ConversationInputArea::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    
    if (mimeData->hasUrls()) {
        for (const QUrl& url : mimeData->urls()) {
            if (url.isLocalFile()) {
                emit fileDropped(url.toLocalFile());
            }
        }
    } else if (mimeData->hasText()) {
        if (multiLineEdit_ && currentWidget_ == multiLineEdit_) {
            multiLineEdit_->insertPlainText(mimeData->text());
        } else if (singleLineEdit_ && currentWidget_ == singleLineEdit_) {
            singleLineEdit_->insert(mimeData->text());
        }
    }
    
    event->acceptProposedAction();
}

void ConversationInputArea::setupSingleLineMode() {
    // Remove current widget
    if (currentWidget_) {
        layout()->removeWidget(currentWidget_);
        currentWidget_->hide();
    }
    
    // Create single line edit if needed
    if (!singleLineEdit_) {
        singleLineEdit_ = new QLineEdit(this);
        singleLineEdit_->setPlaceholderText(tr("Type a message..."));
        
        connect(singleLineEdit_, &QLineEdit::textChanged,
                this, &ConversationInputArea::textChanged);
        connect(singleLineEdit_, &QLineEdit::returnPressed,
                this, &ConversationInputArea::submitRequested);
    }
    
    // Add to layout
    layout()->addWidget(singleLineEdit_);
    singleLineEdit_->show();
    currentWidget_ = singleLineEdit_;
    
    // Set focus
    singleLineEdit_->setFocus();
}

void ConversationInputArea::setupMultiLineMode() {
    // Remove current widget
    if (currentWidget_) {
        layout()->removeWidget(currentWidget_);
        currentWidget_->hide();
    }
    
    
    // Create container for multi-line mode
    auto* container = new QWidget(this);
    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setSpacing(Design::SPACING_XS);
    containerLayout->setContentsMargins(Design::SPACING_SM, Design::SPACING_SM, 
                                      Design::SPACING_SM, Design::SPACING_SM);
    
    // Create multi-line text edit if needed
    if (!multiLineEdit_) {
        multiLineEdit_ = new QTextEdit(this);
        multiLineEdit_->setPlaceholderText(tr("Type a message... (Ctrl+Enter to send, Shift+Enter for new line)"));
        multiLineEdit_->setAcceptRichText(false);
        multiLineEdit_->setMinimumHeight(60);
        multiLineEdit_->setMaximumHeight(200);
        
        connect(multiLineEdit_, &QTextEdit::textChanged, [this]() {
            emit textChanged();
            
            // Update status with character/word count
            if (statusLabel_ && statusLabel_->isVisible()) {
                int words = wordCount();
                int chars = charCount();
                statusLabel_->setText(tr("%1 words, %2 chars").arg(words).arg(chars));
            }
        });
        
        // Install event filter for key handling
        multiLineEdit_->installEventFilter(this);
    }
    
    containerLayout->addWidget(multiLineEdit_);
    
    // Create status bar for character/word count
    if (!statusLabel_) {
        statusLabel_ = new QLabel(this);
        statusLabel_->setFont(ThemeManager::instance().typography().caption);
        statusLabel_->setAlignment(Qt::AlignRight);
        
        const auto& colors = ThemeManager::instance().colors();
        statusLabel_->setStyleSheet(QString("QLabel { color: %1; padding: 2px 5px; }")
                                     .arg(colors.textSecondary.name()));
    }
    
    statusLabel_->setText(tr("0 words, 0 chars"));
    containerLayout->addWidget(statusLabel_);
    
    // Add container to main layout
    layout()->addWidget(container);
    container->show();
    currentWidget_ = container;
    
    // Set focus
    multiLineEdit_->setFocus();
}


void ConversationInputArea::updateLayout() {
    switch (mode_) {
        case SingleLine:
            setupSingleLineMode();
            break;
        case MultiLine:
            setupMultiLineMode();
            break;
    }
}


} // namespace llm_re::ui_v2