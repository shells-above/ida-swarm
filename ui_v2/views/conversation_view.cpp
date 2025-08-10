#include "../core/ui_v2_common.h"
#include "conversation_view.h"

#include "main_window.h"
#include "memory_dock.h"
#include "tool_execution_dock.h"
#include "../core/theme_manager.h"
#include "../core/ui_utils.h"
#include "ui_v2/core/agent_controller.h"

namespace llm_re::ui_v2 {

// ConversationView implementation

ConversationView::ConversationView(QWidget* parent) : BaseStyledWidget(parent) {
    
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
    
    // Keep initialization flag true until MainWindow finishes setup
    // isInitializing_ = false;  // Moved to finishInitialization()
}

ConversationView::~ConversationView() {
    // Only auto-save if we have a session path (user has saved at least once)
    if (hasUnsavedChanges_ && autoSaveEnabled_ && !sessionPath_.isEmpty()) {
        saveSession(sessionPath_);
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
                this, tr("Load Session"), QString(), tr("Session Files (*.llmre)"));
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

    // View options
    auto* viewButton = new QToolButton(this);
    viewButton->setIcon(ThemeManager::instance().themedIcon("view"));
    viewButton->setPopupMode(QToolButton::InstantPopup);
    viewButton->setToolTip(tr("View Options"));
    
    auto* viewMenu = new QMenu(viewButton);
    
    // Density mode submenu
    auto* densityMenu = viewMenu->addMenu(tr("Message Density"));
    auto* densityGroup = new QActionGroup(this);
    
    auto* compactDensityAction = densityMenu->addAction(tr("Compact"));
    compactDensityAction->setCheckable(true);
    compactDensityAction->setActionGroup(densityGroup);
    compactDensityAction->setChecked(densityMode_ == 0);
    connect(compactDensityAction, &QAction::triggered, [this]() {
        setDensityMode(0);
    });
    
    auto* cozyDensityAction = densityMenu->addAction(tr("Cozy"));
    cozyDensityAction->setCheckable(true);
    cozyDensityAction->setActionGroup(densityGroup);
    cozyDensityAction->setChecked(densityMode_ == 1);
    connect(cozyDensityAction, &QAction::triggered, [this]() {
        setDensityMode(1);
    });
    
    auto* spaciousDensityAction = densityMenu->addAction(tr("Spacious"));
    spaciousDensityAction->setCheckable(true);
    spaciousDensityAction->setActionGroup(densityGroup);
    spaciousDensityAction->setChecked(densityMode_ == 2);
    connect(spaciousDensityAction, &QAction::triggered, [this]() {
        setDensityMode(2);
    });
    
    showTimestampsAction_ = viewMenu->addAction(tr("Show Timestamps"));
    showTimestampsAction_->setCheckable(true);
    showTimestampsAction_->setChecked(showTimestamps_);
    connect(showTimestampsAction_, &QAction::toggled, [this](bool checked) {
        setShowTimestamps(checked);
    });
    
    viewMenu->addSeparator();
    
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
}

void ConversationView::createMessageArea() {
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    bubbleContainer_ = new MessageBubbleContainer(scrollArea_);
    bubbleContainer_->setBubbleStyle(bubbleStyle_);
    bubbleContainer_->setDensityMode(densityMode_);
    bubbleContainer_->setMaxBubbleWidth(maxBubbleWidth_);
    
    scrollArea_->setWidget(bubbleContainer_);
    
    // Connect bubble signals
    connect(bubbleContainer_, &MessageBubbleContainer::bubbleClicked,
            this, &ConversationView::onBubbleClicked);
    connect(bubbleContainer_, &MessageBubbleContainer::bubbleContextMenu,
            this, &ConversationView::onBubbleContextMenu);
    connect(bubbleContainer_, &MessageBubbleContainer::selectionChanged,
            this, &ConversationView::selectionChanged);
    
    // Monitor scroll position
    connect(scrollArea_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &ConversationView::onScrollPositionChanged);
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
    
    // Resume button (hidden by default)
    resumeButton_ = new QPushButton(tr("Resume"), this);
    resumeButton_->setIcon(ThemeManager::instance().themedIcon("media-playback-start"));
    resumeButton_->hide();
    resumeButton_->setToolTip(tr("Resume paused analysis"));
    connect(resumeButton_, &QPushButton::clicked, [this]() {
        // Get agent controller and resume
        if (auto mainWindow = qobject_cast<MainWindow*>(window())) {
            if (UiController* uiController = mainWindow->uiController()) {
                if (AgentController* agentController = uiController->agentController()) {
                    agentController->resumeExecution();
                }
            }
        }
    });
    
    // Stop button (hidden by default)
    stopButton_ = new QPushButton(tr("Stop"), this);
    stopButton_->setIcon(ThemeManager::instance().themedIcon("media-playback-stop"));
    stopButton_->hide();
    stopButton_->setToolTip(tr("Stop running analysis"));
    connect(stopButton_, &QPushButton::clicked, [this]() {
        // Get agent controller and stop
        if (auto mainWindow = qobject_cast<MainWindow*>(window())) {
            if (UiController* uiController = mainWindow->uiController()) {
                if (AgentController* agentController = uiController->agentController()) {
                    agentController->stopExecution();
                }
            }
        }
    });
    
    buttonLayout->addWidget(sendButton_);
    buttonLayout->addWidget(cancelButton_);
    buttonLayout->addWidget(resumeButton_);
    buttonLayout->addWidget(stopButton_);
    buttonLayout->addStretch();
    
    inputLayout->addWidget(buttonContainer);
    
    layout->addLayout(inputLayout);
    
    // Keyboard shortcuts
    // Shortcuts are now handled by ConversationInputArea internally
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


void ConversationView::addUserMessage(const QString& content) {
    if (!model_) return;
    
    auto msg = std::make_shared<Message>(Role::User);
    msg->add_content(std::make_unique<TextContent>(content.toStdString()));
    
    MessageMetadata metadata;
    metadata.id = QUuid::createUuid();
    metadata.timestamp = QDateTime::currentDateTime();
    metadata.author = tr("User");
    
    model_->addMessage(msg, metadata);
    markUnsavedChanges();
}

void ConversationView::addAssistantMessage(const QString& content) {
    if (!model_) return;
    
    auto msg = std::make_shared<Message>(Role::Assistant);
    msg->add_content(std::make_unique<TextContent>(content.toStdString()));
    
    MessageMetadata metadata;
    metadata.id = QUuid::createUuid();
    metadata.timestamp = QDateTime::currentDateTime();
    metadata.author = tr("Assistant");
    
    model_->addMessage(msg, metadata);
    markUnsavedChanges();
}

void ConversationView::addSystemMessage(const QString& content) {
    if (!model_) return;
    
    auto msg = std::make_shared<Message>(Role::System);
    msg->add_content(std::make_unique<TextContent>(content.toStdString()));
    
    MessageMetadata metadata;
    metadata.id = QUuid::createUuid();
    metadata.timestamp = QDateTime::currentDateTime();
    metadata.author = tr("System");
    
    model_->addMessage(msg, metadata);
    markUnsavedChanges();
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

void ConversationView::selectMessage(const QUuid& id) {
    bubbleContainer_->selectBubble(id);
}

void ConversationView::selectAll() {
    bubbleContainer_->selectAll();
}

void ConversationView::clearSelection() {
    bubbleContainer_->clearSelection();
}

QList<UIMessage*> ConversationView::selectedMessages() const {
    QList<UIMessage*> messages;
    for (MessageBubble* bubble : bubbleContainer_->getSelectedBubbles()) {
        if (bubble->message()) {
            messages.append(bubble->message());
        }
    }
    return messages;
}


void ConversationView::copySelectedMessages() {
    QStringList texts;
    for (UIMessage* msg : selectedMessages()) {
        texts.append(QString("%1: %2").arg(msg->roleString()).arg(msg->getDisplayText()));
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


void ConversationView::setDensityMode(int mode) {
    if (densityMode_ != mode) {
        densityMode_ = mode;
        bubbleContainer_->setDensityMode(mode);
        markUnsavedChanges();
    }
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
    if (maxBubbleWidth_ != width) {
        maxBubbleWidth_ = width;
        bubbleContainer_->setMaxBubbleWidth(width);
        // Don't mark as changed - this is just a UI preference
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
            // Get IDB directory
            QString idbPath = QString::fromStdString(get_path(PATH_TYPE_IDB));
            QFileInfo idbInfo(idbPath);
            QString idbDir = idbInfo.absolutePath();
            
            // Suggest filename in IDB directory
            QString suggestedName = QString("session_%1.llmre").arg(sessionId_);
            QString suggestedPath = QDir(idbDir).absoluteFilePath(suggestedName);
            
            savePath = QFileDialog::getSaveFileName(
                this, tr("Save Session"),
                suggestedPath,
                tr("Session Files (*.llmre)"));
            
            if (savePath.isEmpty()) return;
            sessionPath_ = savePath;
        } else {
            savePath = sessionPath_;
        }
    }
    
    // Create session data
    QJsonObject session;
    session["id"] = sessionId_;
    session["version"] = 2;  // Version 2 with complete state
    session["created"] = sessionCreatedTime_.toString(Qt::ISODate);
    session["modified"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    // Metadata
    QJsonObject metadata;
    metadata["idbPath"] = QString::fromStdString(get_path(PATH_TYPE_IDB));
    session["metadata"] = metadata;
    
    // Agent state (if agent controller is connected)
    if (auto mainWindow = qobject_cast<MainWindow*>(window())) {
        if (UiController* uiController = mainWindow->uiController()) {
            if (AgentController* controller = uiController->agentController()) {
                QJsonObject agentState;
                agentState["active"] = controller->isRunning() || controller->isPaused();
                agentState["paused"] = controller->isPaused();
                agentState["completed"] = controller->isCompleted();
                
                if (controller->isRunning() || controller->isPaused()) {
                    // Get full agent state
                    json state = controller->getAgentState();
                    agentState["state"] = QJsonDocument::fromJson(
                        QString::fromStdString(state.dump()).toUtf8()).object();
                    
                    // Store last error if any
                    std::string lastError = controller->getLastError();
                    if (!lastError.empty()) {
                        agentState["lastError"] = QString::fromStdString(lastError);
                    }
                }
                
                session["agent"] = agentState;
            }
        }
    }
    
    // UI state
    QJsonObject uiState;
    
    // Main window state
    if (auto mainWindow = qobject_cast<MainWindow*>(window())) {
        QJsonObject mainWindowState;
        mainWindowState["geometry"] = QString::fromUtf8(mainWindow->saveGeometry().toBase64());
        mainWindowState["state"] = QString::fromUtf8(mainWindow->saveState().toBase64());
        mainWindowState["maximized"] = mainWindow->isMaximized();
        mainWindowState["fullscreen"] = mainWindow->isFullScreen();
        uiState["mainWindow"] = mainWindowState;
        
        // Dock states
        QJsonObject dockStates;
        
        if (auto memoryDock = mainWindow->memoryDock()) {
            dockStates["memory"] = memoryDock->exportState();
        }
        
        if (auto toolDock = mainWindow->toolDock()) {
            dockStates["toolExecution"] = toolDock->exportState();
        }
        
        
        uiState["docks"] = dockStates;
    }
    
    // View states
    QJsonObject viewStates;
    
    // Scroll position
    if (scrollArea_) {
        QJsonObject scrollPos;
        scrollPos["vertical"] = scrollArea_->verticalScrollBar()->value();
        scrollPos["horizontal"] = scrollArea_->horizontalScrollBar()->value();
        viewStates["scrollPosition"] = scrollPos;
    }

    uiState["viewStates"] = viewStates;
    session["ui"] = uiState;
    
    // Write file
    QFile file(savePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QJsonDocument doc(session);
        file.write(doc.toJson(QJsonDocument::Indented));
        
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
    
    // Check version and handle migration
    int version = session["version"].toInt();
    if (version < 1 || version > 2) {
        QMessageBox::warning(this, tr("Load Failed"),
                           tr("Unsupported session file version."));
        return;
    }
    
    // Clear current conversation
    clearConversation();
    
    // Load session data
    sessionId_ = session["id"].toString();
    sessionPath_ = path;
    

    // Version 2 format - complete state restoration
    // Load creation time
    if (session.contains("created")) {
        sessionCreatedTime_ = QDateTime::fromString(session["created"].toString(), Qt::ISODate);
    }

    // Restore UI state
    if (session.contains("ui")) {
        QJsonObject uiState = session["ui"].toObject();

        // Restore main window state
        if (auto mainWindow = qobject_cast<MainWindow*>(window())) {
            if (uiState.contains("mainWindow")) {
                QJsonObject mainWindowState = uiState["mainWindow"].toObject();

                if (mainWindowState.contains("geometry")) {
                    mainWindow->restoreGeometry(QByteArray::fromBase64(
                        mainWindowState["geometry"].toString().toUtf8()));
                }
                if (mainWindowState.contains("state")) {
                    mainWindow->restoreState(QByteArray::fromBase64(
                        mainWindowState["state"].toString().toUtf8()));
                }

                // Restore window state after geometry
                if (mainWindowState["fullscreen"].toBool()) {
                    mainWindow->showFullScreen();
                } else if (mainWindowState["maximized"].toBool()) {
                    mainWindow->showMaximized();
                }
            }

            // Restore dock states
            if (uiState.contains("docks")) {
                QJsonObject dockStates = uiState["docks"].toObject();

                if (dockStates.contains("memory") && mainWindow->memoryDock()) {
                    mainWindow->memoryDock()->importState(dockStates["memory"].toObject());
                }

                if (dockStates.contains("toolExecution") && mainWindow->toolDock()) {
                    mainWindow->toolDock()->importState(dockStates["toolExecution"].toObject());
                }
            }
        }

        // Restore view states
        if (uiState.contains("viewStates")) {
            QJsonObject viewStates = uiState["viewStates"].toObject();

            // Restore scroll position (deferred)
            if (viewStates.contains("scrollPosition") && scrollArea_) {
                QJsonObject scrollPos = viewStates["scrollPosition"].toObject();
                int vPos = scrollPos["vertical"].toInt();
                int hPos = scrollPos["horizontal"].toInt();

                // Defer scroll restoration until layout is complete
                QTimer::singleShot(100, [this, vPos, hPos]() {
                    if (scrollArea_) {
                        scrollArea_->verticalScrollBar()->setValue(vPos);
                        scrollArea_->horizontalScrollBar()->setValue(hPos);
                    }
                });
            }
        }
    }

    // Note: Agent state restoration would require coordination with AgentController
    // This is left as a future enhancement to avoid starting agent tasks automatically
    if (session.contains("agent")) {
        QJsonObject agentState = session["agent"].toObject();
        if (agentState["active"].toBool()) {
            statusLabel_->setText(tr("Session loaded (agent state not restored)"));
        }
    }

    clearUnsavedChanges();
    if (statusLabel_->text().isEmpty()) {
        statusLabel_->setText(tr("Session loaded"));
    }
    emit sessionChanged(sessionId_);
}

void ConversationView::finishInitialization() {
    // Called by MainWindow after all initial setup is complete
    isInitializing_ = false;
    
    // Connect to agent controller signals if available
    if (auto mainWindow = qobject_cast<MainWindow*>(window())) {
        if (UiController* uiController = mainWindow->uiController()) {
            if (AgentController* agentController = uiController->agentController()) {
                // Connect agent state change signals
                connect(agentController, &AgentController::agentStarted, 
                        this, &ConversationView::onAgentStateChanged);
                connect(agentController, &AgentController::agentPaused, 
                        this, &ConversationView::onAgentStateChanged);
                connect(agentController, &AgentController::agentCompleted, 
                        this, &ConversationView::onAgentStateChanged);
                
                // Initial button state update
                updateButtonStates();
            }
        }
    }
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
    
    // Set initializing flag to prevent marking changes
    isInitializing_ = true;
    
    clearConversation();
    generateSessionId();
    sessionPath_.clear();
    clearUnsavedChanges();
    
    // Done initializing
    isInitializing_ = false;
    
    emit sessionChanged(sessionId_);
}

void ConversationView::submitInput() {
    if (!inputArea_) return;
    
    QString text = inputArea_->text().trimmed();
    if (text.isEmpty()) return;
    
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

void ConversationView::updateTheme() {
    // Handled by onThemeChanged
}

void ConversationView::onAgentStateChanged() {
    // Update button states when agent state changes
    updateButtonStates();
}

void ConversationView::resizeEvent(QResizeEvent* event) {
    BaseStyledWidget::resizeEvent(event);
    
    // Update max bubble width based on view width
    // Only update if change is significant (>50 pixels) to prevent constant resizing
    int viewWidth = scrollArea_->viewport()->width();
    int maxWidth = qMin(800, viewWidth - 100);
    if (abs(maxWidth - maxBubbleWidth_) > 50) {
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

void ConversationView::onModelRowsInserted(const QModelIndex& parent, int first, int last) {
    Q_UNUSED(parent)
    
    // Add new bubbles
    for (int row = first; row <= last; ++row) {
        UIMessage* msg = model_->getMessageAt(row);
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
    UIMessage* msg = model_->getMessage(id);
    if (!msg) return;
    
    QMenu menu(this);
    
    menu.addAction(ThemeManager::instance().themedIcon("copy"), tr("Copy"), [this, msg]() {
        QApplication::clipboard()->setText(msg->getDisplayText());
    });

    menu.exec(pos);
}

void ConversationView::onInputTextChanged() {
    updateButtonStates();
    
    // Update word count
    if (inputArea_) {
        int words = inputArea_->wordCount();
        int chars = inputArea_->charCount();
        wordCountLabel_->setText(tr("%1 words, %2 chars").arg(words).arg(chars));
    }
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

void ConversationView::updateButtonStates() {
    bool hasText = inputArea_ && inputArea_->hasText();
    
    // Get agent controller state if available
    AgentController* agentController = nullptr;
    if (auto mainWindow = qobject_cast<MainWindow*>(window())) {
        if (UiController* uiController = mainWindow->uiController()) {
            agentController = uiController->agentController();
        }
    }
    
    // Update button visibility based on agent state
    if (agentController) {
        bool isRunning = agentController->isRunning();
        bool isPaused = agentController->isPaused();
        
        // Resume button: shown when agent is paused
        resumeButton_->setVisible(isPaused);
        resumeButton_->setEnabled(isPaused);
        
        // Stop button: shown when agent is running
        stopButton_->setVisible(isRunning);
        stopButton_->setEnabled(isRunning);
        
        // Send button: enabled when has text and agent not running
        sendButton_->setEnabled(hasText && !isRunning);
        sendButton_->setVisible(!isPaused); // Hide when paused to show resume instead
        
        // Cancel button stays hidden unless typing
        // (existing behavior)
    } else {
        // No agent controller, fall back to simple behavior
        sendButton_->setEnabled(hasText);
        resumeButton_->hide();
        stopButton_->hide();
    }
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
        UIMessage* msg = model_->getMessageAt(i);
        if (msg) {
            bubbleContainer_->addMessage(msg, false);
        }
    }
}

void ConversationView::markUnsavedChanges() {
    // Don't mark changes during initialization
    if (isInitializing_) {
        return;
    }
    
    if (!hasUnsavedChanges_) {
        hasUnsavedChanges_ = true;
        emit unsavedChangesChanged(true);
        
        // Update window title or status
        if (saveSessionAction_) {
            saveSessionAction_->setText(tr("Save Session*"));
        }
    }
}

void ConversationView::discardChanges() {
    clearUnsavedChanges();
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
    sessionCreatedTime_ = QDateTime::currentDateTime();
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
    
    setupUI();
}

void ConversationInputArea::setupUI() {
    // Create container for input area
    auto* container = new QWidget(this);
    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setSpacing(0);
    containerLayout->setContentsMargins(0, 0, 0, 0);
    
    // Create text edit
    textEdit_ = new QTextEdit(this);
    textEdit_->setPlaceholderText(tr("Type a message..."));
    textEdit_->setAcceptRichText(false);
    textEdit_->setFont(ThemeManager::instance().typography().body);
    
    // Remove extra margins
    textEdit_->setContentsMargins(0, 0, 0, 0);
    textEdit_->document()->setDocumentMargin(4);
    
    // Set initial height to properly show single line text
    textEdit_->setFixedHeight(baseHeight_);
    textEdit_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    
    // Connect signals
    connect(textEdit_, &QTextEdit::textChanged, [this]() {
        emit textChanged();
        adjustHeight();
    });
    
    // Install event filter for key handling
    textEdit_->installEventFilter(this);
    
    containerLayout->addWidget(textEdit_);
    
    // Add container to main layout
    layout()->addWidget(container);
    container->show();
}

void ConversationInputArea::adjustHeight() {
    if (!textEdit_) return;
    
    // Calculate content height
    QTextDocument* doc = textEdit_->document();
    QSizeF docSizeF = doc->documentLayout()->documentSize();
    
    int contentHeight = qCeil(docSizeF.height()) + 2 * doc->documentMargin();
    int totalHeight = contentHeight + 8; // padding for top/bottom
    
    // Ensure minimum height is baseHeight_ and limit to ~10 lines max
    int maxHeight = baseHeight_ * 10;
    int newHeight = qBound(baseHeight_, totalHeight, maxHeight);
    
    // Update height if changed
    if (newHeight != textEdit_->height()) {
        textEdit_->setFixedHeight(newHeight);
    }
}

QString ConversationInputArea::text() const {
    return textEdit_ ? textEdit_->toPlainText() : QString();
}

void ConversationInputArea::setText(const QString& text) {
    if (textEdit_) {
        textEdit_->setPlainText(text);
    }
}

void ConversationInputArea::clear() {
    if (textEdit_) {
        textEdit_->clear();
    }
}

void ConversationInputArea::focus() {
    if (textEdit_) {
        textEdit_->setFocus();
    }
}

void ConversationInputArea::selectAll() {
    if (textEdit_) {
        textEdit_->selectAll();
    }
}

void ConversationInputArea::setPlaceholder(const QString& text) {
    if (textEdit_) {
        textEdit_->setPlaceholderText(text);
    }
}

void ConversationInputArea::setMaxLength(int length) {
    // Store max length for later use
    maxLength_ = length;
    
    // Connect to textChanged if not already connected
    if (length > 0 && !maxLengthConnected_ && textEdit_) {
        maxLengthConnected_ = true;
        
        // Disconnect existing textChanged connections first
        disconnect(textEdit_, &QTextEdit::textChanged, nullptr, nullptr);
        
        // Reconnect with max length enforcement
        connect(textEdit_, &QTextEdit::textChanged, [this]() {
            // Check length and truncate if needed
            QString text = textEdit_->toPlainText();
            if (maxLength_ > 0 && text.length() > maxLength_) {
                // Block signals to prevent recursion
                textEdit_->blockSignals(true);
                
                // Preserve cursor position
                QTextCursor cursor = textEdit_->textCursor();
                int cursorPos = cursor.position();
                
                // Truncate text
                text = text.left(maxLength_);
                textEdit_->setPlainText(text);
                
                // Restore cursor position (at end if it was beyond max)
                cursor.setPosition(qMin(cursorPos, text.length()));
                textEdit_->setTextCursor(cursor);
                
                // Show warning
                QToolTip::showText(textEdit_->mapToGlobal(QPoint(0, 0)), 
                                  tr("Maximum length of %1 characters reached").arg(maxLength_),
                                  textEdit_, QRect(), 3000);
                
                textEdit_->blockSignals(false);
            }
            
            // Emit signals
            emit textChanged();
            adjustHeight();
        });
    }
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
    if (event->type() == QEvent::KeyPress && watched == textEdit_) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        
        // Handle Ctrl+Left and Ctrl+Right for word navigation
        // This prevents IDA from intercepting these shortcuts
        if (keyEvent->modifiers() & Qt::ControlModifier) {
            QTextCursor::MoveMode moveMode = (keyEvent->modifiers() & Qt::ShiftModifier) 
                ? QTextCursor::KeepAnchor : QTextCursor::MoveAnchor;
            
            if (keyEvent->key() == Qt::Key_Left) {
                // Move cursor to beginning of previous word
                QTextCursor cursor = textEdit_->textCursor();
                cursor.movePosition(QTextCursor::PreviousWord, moveMode);
                textEdit_->setTextCursor(cursor);
                return true; // Consume the event
            }
            else if (keyEvent->key() == Qt::Key_Right) {
                // Move cursor to beginning of next word
                QTextCursor cursor = textEdit_->textCursor();
                cursor.movePosition(QTextCursor::NextWord, moveMode);
                textEdit_->setTextCursor(cursor);
                return true; // Consume the event
            }
            else if (keyEvent->key() == Qt::Key_Home) {
                // Move cursor to beginning of document
                QTextCursor cursor = textEdit_->textCursor();
                cursor.movePosition(QTextCursor::Start, moveMode);
                textEdit_->setTextCursor(cursor);
                return true; // Consume the event
            }
            else if (keyEvent->key() == Qt::Key_End) {
                // Move cursor to end of document
                QTextCursor cursor = textEdit_->textCursor();
                cursor.movePosition(QTextCursor::End, moveMode);
                textEdit_->setTextCursor(cursor);
                return true; // Consume the event
            }
            // Ctrl+Enter (or Cmd+Enter on Mac) to submit
            else if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                emit submitRequested();
                return true;
            }
        }
        // Plain Enter creates new line
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
    } else if (mimeData->hasText() && textEdit_) {
        textEdit_->insertPlainText(mimeData->text());
    }
    
    event->acceptProposedAction();
}

} // namespace llm_re::ui_v2