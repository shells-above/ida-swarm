#include "../core/ui_v2_common.h"
#include "main_window.h"
#include "../core/theme_manager.h"
#include "../core/ui_constants.h"
#include "../core/ui_utils.h"
#include "../core/settings_manager.h"
#include "../core/agent_controller.h"
#include "memory_dock.h"
#include "tool_execution_dock.h"
#include "console_dock.h"
#include "settings_dialog.h"
#include "theme_editor/theme_editor_dialog.h"

namespace llm_re::ui_v2 {

// NotificationWidget - toast notification implementation
class NotificationWidget : public BaseStyledWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)
    Q_PROPERTY(int slideOffset READ slideOffset WRITE setSlideOffset)
    
public:
    NotificationWidget(const QString& title, const QString& message,
                      NotificationManager::NotificationType type, QWidget* parent = nullptr)
        : BaseStyledWidget(parent)
        , title_(title)
        , message_(message)
        , type_(type)
    {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFocusPolicy(Qt::NoFocus);
        
        setupUI();
        
        // Set initial opacity
        setOpacity(0.0);
    }
    
    qreal opacity() const { return opacity_; }
    void setOpacity(qreal opacity) {
        opacity_ = opacity;
        update();
    }
    
    int slideOffset() const { return slideOffset_; }
    void setSlideOffset(int offset) {
        slideOffset_ = offset;
        move(x(), baseY_ + offset);
    }
    
    void animateIn() {
        show();
        raise();
        
        // Store base position
        baseY_ = y();
        
        // Fade in
        auto* fadeAnim = new QPropertyAnimation(this, "opacity");
        fadeAnim->setDuration(200);
        fadeAnim->setStartValue(0.0);
        fadeAnim->setEndValue(1.0);
        fadeAnim->setEasingCurve(QEasingCurve::OutCubic);
        
        // Slide in
        auto* slideAnim = new QPropertyAnimation(this, "slideOffset");
        slideAnim->setDuration(200);
        slideAnim->setStartValue(-20);
        slideAnim->setEndValue(0);
        slideAnim->setEasingCurve(QEasingCurve::OutCubic);
        
        fadeAnim->start(QAbstractAnimation::DeleteWhenStopped);
        slideAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    
    void animateOut() {
        // Fade out
        auto* fadeAnim = new QPropertyAnimation(this, "opacity");
        fadeAnim->setDuration(150);
        fadeAnim->setStartValue(1.0);
        fadeAnim->setEndValue(0.0);
        fadeAnim->setEasingCurve(QEasingCurve::InCubic);
        
        // Slide out
        auto* slideAnim = new QPropertyAnimation(this, "slideOffset");
        slideAnim->setDuration(150);
        slideAnim->setStartValue(0);
        slideAnim->setEndValue(-10);
        slideAnim->setEasingCurve(QEasingCurve::InCubic);
        
        connect(fadeAnim, &QPropertyAnimation::finished, this, &NotificationWidget::close);
        
        fadeAnim->start(QAbstractAnimation::DeleteWhenStopped);
        slideAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }
    
    QSize sizeHint() const override {
        return QSize(320, 80);
    }
    
signals:
    void clicked();
    void closed();
    
protected:
    void paintEvent(QPaintEvent* event) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setOpacity(opacity_);
        
        // Background
        QColor bgColor = ThemeManager::instance().colors().surface;
        bgColor.setAlphaF(0.95);
        
        painter.setPen(Qt::NoPen);
        painter.setBrush(bgColor);
        painter.drawRoundedRect(rect(), 8, 8);
        
        // Type indicator
        QColor typeColor;
        QString iconName;
        switch (type_) {
        case NotificationManager::Success:
            typeColor = ThemeColor("notificationSuccess");
            iconName = "check-circle";
            break;
        case NotificationManager::Warning:
            typeColor = ThemeColor("notificationWarning");
            iconName = "warning";
            break;
        case NotificationManager::Error:
            typeColor = ThemeColor("notificationError");
            iconName = "error";
            break;
        default:
            typeColor = ThemeColor("notificationInfo");
            iconName = "info";
            break;
        }
        
        // Type bar
        painter.setBrush(typeColor);
        painter.drawRoundedRect(0, 0, 4, height(), 2, 2);
        
        // Icon
        QRect iconRect(12, (height() - 24) / 2, 24, 24);
        QIcon icon = ThemeManager::instance().themedIcon(iconName);
        icon.paint(&painter, iconRect);
        
        // Close button
        if (closeButton_ && closeButton_->underMouse()) {
            painter.setOpacity(opacity_ * 0.8);
        }
        
        BaseStyledWidget::paintEvent(event);
    }
    
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            emit clicked();
        }
        BaseStyledWidget::mousePressEvent(event);
    }
    
    void enterEvent(QEvent* event) override {
        if (closeButton_) {
            closeButton_->show();
        }
        BaseStyledWidget::enterEvent(event);
    }
    
    void leaveEvent(QEvent* event) override {
        if (closeButton_) {
            closeButton_->hide();
        }
        BaseStyledWidget::leaveEvent(event);
    }
    
private:
    void setupUI() {
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(44, 12, 12, 12);
        layout->setSpacing(12);
        
        // Text layout
        auto* textLayout = new QVBoxLayout();
        textLayout->setSpacing(4);
        
        if (!title_.isEmpty()) {
            auto* titleLabel = new QLabel(title_, this);
            QFont font = titleLabel->font();
            font.setWeight(QFont::DemiBold);
            titleLabel->setFont(font);
            titleLabel->setStyleSheet(QString("color: %1;").arg(
                ThemeManager::instance().colors().textPrimary.name()
            ));
            textLayout->addWidget(titleLabel);
        }
        
        auto* messageLabel = new QLabel(message_, this);
        messageLabel->setWordWrap(true);
        messageLabel->setStyleSheet(QString("color: %1;").arg(
            ThemeManager::instance().colors().textSecondary.name()
        ));
        textLayout->addWidget(messageLabel);
        textLayout->addStretch();
        
        layout->addLayout(textLayout);
        
        // Close button
        closeButton_ = new QToolButton(this);
        closeButton_->setIcon(ThemeManager::instance().themedIcon("close"));
        closeButton_->setIconSize(QSize(16, 16));
        closeButton_->setAutoRaise(true);
        closeButton_->hide();
        closeButton_->setStyleSheet(
            "QToolButton { border: none; background: transparent; }"
            "QToolButton:hover { background: rgba(0,0,0,0.1); border-radius: 2px; }"
        );
        
        connect(closeButton_, &QToolButton::clicked, this, &NotificationWidget::animateOut);
        
        layout->addWidget(closeButton_, 0, Qt::AlignTop);
    }
    
    QString title_;
    QString message_;
    NotificationManager::NotificationType type_;
    QToolButton* closeButton_ = nullptr;
    qreal opacity_ = 1.0;
    int slideOffset_ = 0;
    int baseY_ = 0;
};

#include "main_window.moc"

MainWindow* MainWindow::instance_ = nullptr;

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , controller_(std::make_unique<UiController>(this))
{
    setObjectName("MainWindow");
    
    // Mark this as a plugin widget to prevent theme bleeding into IDA
    setProperty("llm_re_widget", true);
    
    // CRITICAL: Prevent Qt from using the application style for background
    setAttribute(Qt::WA_StyledBackground, false);
    // This ensures our widget doesn't inherit IDA's theme
    setAutoFillBackground(false);
    
    setupUI();
    loadSettings();
    
    // Set global instance
    setInstance(this);
    
    // Apply initial theme
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MainWindow::onThemeChanged);
    onThemeChanged();
    
    // Start with a new session (without prompting for save)
    QTimer::singleShot(0, this, [this]() {
        // Finish initialization now that settings are loaded
        conversationView_->finishInitialization();
        
        // Initialize a new session without checking for unsaved changes
        conversationView_->newSession();
        currentFile_.clear();
        setCurrentFile(QString());
        hasUnsavedChanges_ = false;
        updateWindowTitle();
        showStatusMessage(tr("Session ready"));
    });
}

MainWindow::~MainWindow() {
    // Clean up controller connections first
    if (controller_) {
        controller_->cleanup();
    }
    
    // Disconnect all conversation view signals
    for (auto* view : conversationViews_) {
        if (view) {
            view->disconnect();
        }
    }
    
    // Only save settings if preference is enabled and we should save
    if (rememberWindowState_ && shouldSaveSettings_) {
        saveSettings();
    }
    if (instance_ == this) {
        instance_ = nullptr;
    }
}

void MainWindow::setupUI() {
    // Window properties
    setWindowTitle(tr("LLM RE Agent"));
    resize(1200, 800);
    setMinimumSize(800, 600);
    
    // Enable drag & drop
    setAcceptDrops(true);
    
    // Create UI components
    createActions();
    createMenus();
    createToolBars();
    createStatusBar();
    createCentralWidget();
    createDockWindows();
    
    // Connect signals
    connectSignals();
    
    // Setup managers
    layoutManager_ = new LayoutManager(this);
    shortcutManager_ = new ShortcutManager(this);
    notificationManager_ = new NotificationManager(this);
    
    // Setup shortcuts
    setupShortcuts();
    
    // Update initial state
    updateActions();
    updateWindowTitle();
}

void MainWindow::createActions() {
    // File actions
    newAction_ = new QAction(ThemeManager::instance().themedIcon("document-new"), tr("&New Session"), this);
    newAction_->setShortcut(QKeySequence::New);
    newAction_->setStatusTip(tr("Start a new conversation session"));
    connect(newAction_, &QAction::triggered, this, &MainWindow::onFileNew);
    
    openAction_ = new QAction(ThemeManager::instance().themedIcon("document-open"), tr("&Open Session..."), this);
    openAction_->setShortcut(QKeySequence::Open);
    openAction_->setStatusTip(tr("Open an existing session"));
    connect(openAction_, &QAction::triggered, this, &MainWindow::onFileOpen);
    
    saveAction_ = new QAction(ThemeManager::instance().themedIcon("document-save"), tr("&Save Session"), this);
    saveAction_->setShortcut(QKeySequence::Save);
    saveAction_->setStatusTip(tr("Save the current session"));
    connect(saveAction_, &QAction::triggered, this, &MainWindow::onFileSave);
    
    saveAsAction_ = new QAction(ThemeManager::instance().themedIcon("document-save-as"), tr("Save Session &As..."), this);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    saveAsAction_->setStatusTip(tr("Save the session with a new name"));
    connect(saveAsAction_, &QAction::triggered, this, &MainWindow::onFileSaveAs);
    
    exitAction_ = new QAction(ThemeManager::instance().themedIcon("application-exit"), tr("E&xit"), this);
    exitAction_->setShortcut(QKeySequence::Quit);
    exitAction_->setStatusTip(tr("Exit the application"));
    connect(exitAction_, &QAction::triggered, this, &MainWindow::onFileExit);
    
    // Edit actions
    
    selectAllAction_ = new QAction(ThemeManager::instance().themedIcon("edit-select-all"), tr("Select &All"), this);
    selectAllAction_->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction_, &QAction::triggered, this, &MainWindow::onEditSelectAll);
    
    preferencesAction_ = new QAction(ThemeManager::instance().themedIcon("preferences-system"), tr("&Preferences..."), this);
    preferencesAction_->setShortcut(QKeySequence::Preferences);
    connect(preferencesAction_, &QAction::triggered, this, &MainWindow::onEditPreferences);
    
    // View actions
    toggleSidebarAction_ = new QAction(tr("Toggle &Sidebar"), this);
    toggleSidebarAction_->setCheckable(true);
    toggleSidebarAction_->setChecked(true);
    toggleSidebarAction_->setShortcut(QKeySequence(tr("Ctrl+B")));
    connect(toggleSidebarAction_, &QAction::triggered, this, &MainWindow::onViewToggleSidebar);
    
    toggleToolBarAction_ = new QAction(tr("Toggle &Toolbar"), this);
    toggleToolBarAction_->setCheckable(true);
    toggleToolBarAction_->setChecked(true);
    connect(toggleToolBarAction_, &QAction::triggered, this, &MainWindow::onViewToggleToolBar);
    
    toggleStatusBarAction_ = new QAction(tr("Toggle &Status Bar"), this);
    toggleStatusBarAction_->setCheckable(true);
    toggleStatusBarAction_->setChecked(true);
    connect(toggleStatusBarAction_, &QAction::triggered, this, &MainWindow::onViewToggleStatusBar);
    
    toggleFullScreenAction_ = new QAction(ThemeManager::instance().themedIcon("view-fullscreen"), tr("&Full Screen"), this);
    toggleFullScreenAction_->setCheckable(true);
    toggleFullScreenAction_->setShortcut(QKeySequence::FullScreen);
    connect(toggleFullScreenAction_, &QAction::triggered, this, &MainWindow::onViewToggleFullScreen);
    
    resetLayoutAction_ = new QAction(tr("&Reset Layout"), this);
    connect(resetLayoutAction_, &QAction::triggered, this, &MainWindow::onViewResetLayout);
    
    saveLayoutAction_ = new QAction(tr("&Save Layout..."), this);
    connect(saveLayoutAction_, &QAction::triggered, this, &MainWindow::onViewSaveLayout);
    
    // Tools actions
    
    memoryAnalysisAction_ = new QAction(ThemeManager::instance().themedIcon("memory-analysis"), tr("&Memory Analysis"), this);
    memoryAnalysisAction_->setCheckable(true);
    connect(memoryAnalysisAction_, &QAction::triggered, this, &MainWindow::onToolsMemoryAnalysis);
    
    executionHistoryAction_ = new QAction(ThemeManager::instance().themedIcon("execution-history"), tr("&Execution History"), this);
    executionHistoryAction_->setCheckable(true);
    connect(executionHistoryAction_, &QAction::triggered, this, &MainWindow::onToolsExecutionHistory);
    
    consoleAction_ = new QAction(ThemeManager::instance().themedIcon("utilities-terminal"), tr("&Console"), this);
    consoleAction_->setCheckable(true);
    connect(consoleAction_, &QAction::triggered, this, &MainWindow::onToolsConsole);
    
    // Help actions
    documentationAction_ = new QAction(ThemeManager::instance().themedIcon("help-contents"), tr("&Documentation"), this);
    documentationAction_->setShortcut(QKeySequence::HelpContents);
    connect(documentationAction_, &QAction::triggered, this, &MainWindow::onHelpDocumentation);
    
    keyboardShortcutsAction_ = new QAction(tr("&Keyboard Shortcuts"), this);
    keyboardShortcutsAction_->setShortcut(QKeySequence(tr("Ctrl+?")));
    connect(keyboardShortcutsAction_, &QAction::triggered, this, &MainWindow::onHelpKeyboardShortcuts);
    
    
    aboutAction_ = new QAction(tr("&About LLM RE Agent"), this);
    connect(aboutAction_, &QAction::triggered, this, &MainWindow::onHelpAbout);
    
    aboutQtAction_ = new QAction(tr("About &Qt"), this);
    connect(aboutQtAction_, &QAction::triggered, qApp, &QApplication::aboutQt);
    
    // Recent file actions
    for (int i = 0; i < MaxRecentFiles; ++i) {
        auto* action = new QAction(this);
        action->setVisible(false);
        connect(action, &QAction::triggered, this, [this, i]() {
            if (auto* action = recentFileActions_[i]) {
                openSession(action->data().toString());
            }
        });
        recentFileActions_.append(action);
    }
}

void MainWindow::createMenus() {
    // File menu
    fileMenu_ = menuBar()->addMenu(tr("&File"));
    fileMenu_->addAction(newAction_);
    fileMenu_->addAction(openAction_);
    fileMenu_->addAction(saveAction_);
    fileMenu_->addAction(saveAsAction_);
    fileMenu_->addSeparator();
    
    recentFilesMenu_ = fileMenu_->addMenu(tr("Recent Sessions"));
    for (auto* action : recentFileActions_) {
        recentFilesMenu_->addAction(action);
    }
    updateRecentFiles();
    
    fileMenu_->addSeparator();
    fileMenu_->addAction(exitAction_);
    
    // Edit menu
    editMenu_ = menuBar()->addMenu(tr("&Edit"));
    editMenu_->addAction(selectAllAction_);
    editMenu_->addSeparator();
    editMenu_->addAction(preferencesAction_);
    
    // View menu
    viewMenu_ = menuBar()->addMenu(tr("&View"));
    
    themeMenu_ = viewMenu_->addMenu(tr("&Theme"));
    auto* darkThemeAction = themeMenu_->addAction(tr("Dark"));
    auto* lightThemeAction = themeMenu_->addAction(tr("Light"));
    darkThemeAction->setCheckable(true);
    lightThemeAction->setCheckable(true);
    
    auto* themeGroup = new QActionGroup(this);
    themeGroup->addAction(darkThemeAction);
    themeGroup->addAction(lightThemeAction);
    
    connect(darkThemeAction, &QAction::triggered, [this]() {
        ThemeManager::instance().loadTheme("dark");
    });
    connect(lightThemeAction, &QAction::triggered, [this]() {
        ThemeManager::instance().loadTheme("light");
    });
    
    // Set initial theme check
    auto currentInfo = ThemeManager::instance().getCurrentThemeInfo();
    if (currentInfo.name == "dark") {
        darkThemeAction->setChecked(true);
    } else if (currentInfo.name == "light") {
        lightThemeAction->setChecked(true);
    }
    
    themeMenu_->addSeparator();
    auto* themeEditorAction = themeMenu_->addAction(tr("Theme Editor..."));
    connect(themeEditorAction, &QAction::triggered, [this]() {
        auto* dialog = new ThemeEditorDialog(this);
        dialog->loadCurrentTheme();
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
    
    viewMenu_->addSeparator();
    viewMenu_->addAction(toggleSidebarAction_);
    viewMenu_->addAction(toggleToolBarAction_);
    viewMenu_->addAction(toggleStatusBarAction_);
    viewMenu_->addSeparator();
    viewMenu_->addAction(toggleFullScreenAction_);
    viewMenu_->addSeparator();
    
    layoutMenu_ = viewMenu_->addMenu(tr("&Layout"));
    layoutMenu_->addAction(resetLayoutAction_);
    layoutMenu_->addAction(saveLayoutAction_);
    layoutMenu_->addSeparator();
    // Layout list will be populated dynamically
    
    // Tools menu
    toolsMenu_ = menuBar()->addMenu(tr("&Tools"));
    toolsMenu_->addAction(memoryAnalysisAction_);
    toolsMenu_->addAction(executionHistoryAction_);
    toolsMenu_->addAction(consoleAction_);
    
    // Window menu
    windowMenu_ = menuBar()->addMenu(tr("&Window"));
    connect(windowMenu_, &QMenu::aboutToShow, [this]() {
        windowMenu_->clear();

        // List open conversation views
        int index = 1;
        for (auto* view : conversationViews_) {
            auto* action = windowMenu_->addAction(
                tr("Conversation %1").arg(index++),
                [this, view]() { view->setFocus(); }
            );
            if (view == conversationView_) {
                action->setChecked(true);
            }
        }
    });
    
    // Help menu
    helpMenu_ = menuBar()->addMenu(tr("&Help"));
    helpMenu_->addAction(documentationAction_);
    helpMenu_->addAction(keyboardShortcutsAction_);
    helpMenu_->addSeparator();
    helpMenu_->addSeparator();
    helpMenu_->addAction(aboutAction_);
    helpMenu_->addAction(aboutQtAction_);
}

void MainWindow::createToolBars() {
    // Main toolbar
    mainToolBar_ = addToolBar(tr("Main"));
    mainToolBar_->setObjectName("MainToolBar");
    mainToolBar_->setMovable(true);
    
    // Edit toolbar
    editToolBar_ = addToolBar(tr("Edit"));
    editToolBar_->setObjectName("EditToolBar");
    editToolBar_->setMovable(true);
    
    // View toolbar
    viewToolBar_ = addToolBar(tr("View"));
    viewToolBar_->setObjectName("ViewToolBar");
    viewToolBar_->setMovable(true);
    viewToolBar_->addAction(toggleFullScreenAction_);
    viewToolBar_->addSeparator();
    viewToolBar_->addAction(memoryAnalysisAction_);
    viewToolBar_->addAction(executionHistoryAction_);
    viewToolBar_->addAction(consoleAction_);
}

void MainWindow::createStatusBar() {
    statusBar()->showMessage(tr("Ready"));
    
    // Add permanent widgets
    auto* sessionLabel = new QLabel(this);
    sessionLabel->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    statusBar()->addPermanentWidget(sessionLabel);
    
    auto* messageCountLabel = new QLabel(this);
    messageCountLabel->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
    statusBar()->addPermanentWidget(messageCountLabel);
    
    // Update status periodically
    auto* statusTimer = new QTimer(this);
    connect(statusTimer, &QTimer::timeout, [this, sessionLabel, messageCountLabel]() {
        if (conversationView_ && conversationView_->model()) {
            sessionLabel->setText(tr("Session: %1").arg(
                conversationView_->currentSessionId().left(8)
            ));
            messageCountLabel->setText(tr("Messages: %1").arg(
                conversationView_->model()->rowCount()
            ));
        }
    });
    statusTimer->start(1000);
}

void MainWindow::createCentralWidget() {
    // Create main splitter for split views
    mainSplitter_ = new QSplitter(Qt::Horizontal, this);
    mainSplitter_->setObjectName("MainSplitter");
    
    // Create conversation view
    conversationView_ = createConversationView();
    conversationViews_.append(conversationView_);
    controller_->registerConversationView(conversationView_);
    
    // Add to splitter
    mainSplitter_->addWidget(conversationView_);
    
    // Set as central widget
    setCentralWidget(mainSplitter_);
    
}

void MainWindow::createDockWindows() {
    // Memory dock
    memoryDock_ = new MemoryDock(this);
    memoryDockWidget_ = new QDockWidget(tr("Memory Analysis"), this);
    memoryDockWidget_->setObjectName("MemoryDock");
    memoryDockWidget_->setWidget(memoryDock_);
    memoryDockWidget_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::RightDockWidgetArea, memoryDockWidget_);
    memoryDockWidget_->hide();
    
    connect(memoryDockWidget_, &QDockWidget::visibilityChanged,
            memoryAnalysisAction_, &QAction::setChecked);
    
    // Tool execution dock
    toolDock_ = new ToolExecutionDock(this);
    toolDockWidget_ = new QDockWidget(tr("Tool Execution"), this);
    toolDockWidget_->setObjectName("ToolDock");
    toolDockWidget_->setWidget(toolDock_);
    toolDockWidget_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::BottomDockWidgetArea, toolDockWidget_);
    toolDockWidget_->hide();
    
    connect(toolDockWidget_, &QDockWidget::visibilityChanged,
            executionHistoryAction_, &QAction::setChecked);
    
    // Console dock
    consoleDock_ = new ConsoleDock(this);
    consoleDockWidget_ = new QDockWidget(tr("Console"), this);
    consoleDockWidget_->setObjectName("ConsoleDock");
    consoleDockWidget_->setWidget(consoleDock_);
    consoleDockWidget_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::BottomDockWidgetArea, consoleDockWidget_);
    consoleDockWidget_->hide();
    
    // Connect visibility changes to action state
    connect(consoleDockWidget_, &QDockWidget::visibilityChanged,
            consoleAction_, &QAction::setChecked);
    
    // Tab docks on the right
}

void MainWindow::connectSignals() {
    // Conversation view signals
    connect(conversationView_, &ConversationView::unsavedChangesChanged,
            this, &MainWindow::onSessionModified);
}


void MainWindow::setupShortcuts() {
    // Additional global shortcuts
    shortcutManager_->registerShortcut(
        "focus.conversation", QKeySequence(tr("Alt+1")),
        tr("Focus conversation view"),
        [this]() { controller_->focusConversation(); }
    );
    
    shortcutManager_->registerShortcut(
        "focus.memory", QKeySequence(tr("Alt+2")),
        tr("Focus memory panel"),
        [this]() { controller_->focusMemory(); }
    );
    
    shortcutManager_->registerShortcut(
        "focus.tools", QKeySequence(tr("Alt+3")),
        tr("Focus tools panel"),
        [this]() { controller_->focusTools(); }
    );
}

ConversationView* MainWindow::createConversationView() {
    auto* view = new ConversationView(this);
    
    // Connect view signals
    connect(view, &ConversationView::messageSubmitted,
            controller_.get(), &UiController::routeUserMessage);
    connect(view, &ConversationView::unsavedChangesChanged,
            this, &MainWindow::onSessionModified);
    
    return view;
}

void MainWindow::showWindow() {
    show();
    raise();
    activateWindow();
    emit windowShown();
}

void MainWindow::hideWindow() {
    hide();
    emit windowHidden();
}

void MainWindow::toggleWindow() {
    if (isVisible() && !isMinimized()) {
        hideWindow();
    } else {
        showWindow();
    }
}

void MainWindow::bringToFront() {
    showWindow();
    
    // Platform-specific bringing to front
#ifdef Q_OS_MAC
    raise();
    activateWindow();
#elif defined(Q_OS_WIN)
    // Windows-specific code to bring window to front
    setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
    raise();
    activateWindow();
#else
    // Linux/X11
    raise();
    activateWindow();
#endif
}

void MainWindow::saveLayout(const QString& name) {
    QString layoutName = name;
    if (layoutName.isEmpty()) {
        bool ok;
        layoutName = QInputDialog::getText(
            this, tr("Save Layout"),
            tr("Enter layout name:"),
            QLineEdit::Normal, "", &ok
        );
        if (!ok || layoutName.isEmpty()) {
            return;
        }
    }
    
    layoutManager_->saveLayout(layoutName);
    showStatusMessage(tr("Layout saved: %1").arg(layoutName));
}

void MainWindow::loadLayout(const QString& name) {
    layoutManager_->loadLayout(name);
    currentLayout_ = name;
    emit layoutChanged(name);
}

void MainWindow::resetLayout() {
    // Reset to default layout
    restoreState(QByteArray());
    restoreGeometry(QByteArray());
    
    // Reset dock widgets
    memoryDockWidget_->setFloating(false);
    toolDockWidget_->setFloating(false);
    
    addDockWidget(Qt::RightDockWidgetArea, memoryDockWidget_);
    addDockWidget(Qt::BottomDockWidgetArea, toolDockWidget_);
    
    // Hide all docks by default
    memoryDockWidget_->hide();
    toolDockWidget_->hide();
    
    showStatusMessage(tr("Layout reset to default"));
}

QStringList MainWindow::availableLayouts() const {
    return layoutManager_->availableLayouts();
}

void MainWindow::deleteLayout(const QString& name) {
    layoutManager_->deleteLayout(name);
}

void MainWindow::applyTheme(const QString& theme) {
    if (!ThemeManager::instance().loadTheme(theme)) {
        QMessageBox::warning(this, "Theme Load Failed",
            QString("Failed to load theme '%1'. Using default dark theme.").arg(theme));
        ThemeManager::instance().loadTheme("dark");
    }
    emit themeChanged(theme);
}

void MainWindow::newSession() {
    // This is called when user explicitly requests a new session
    // (not during startup)
    if (!maybeSave()) {
        return;
    }
    
    conversationView_->newSession();
    currentFile_.clear();
    setCurrentFile(QString());
    hasUnsavedChanges_ = false;
    updateWindowTitle();
    showStatusMessage(tr("New session started"));
}

void MainWindow::openSession(const QString& path) {
    if (!maybeSave()) {
        return;
    }
    
    QString fileName = path;
    if (fileName.isEmpty()) {
        fileName = QFileDialog::getOpenFileName(
            this, tr("Open Session"),
            QDir::homePath(),
            tr("Session Files (*.llmre);;All Files (*)")
        );
    }
    
    if (!fileName.isEmpty()) {
        conversationView_->loadSession(fileName);
        setCurrentFile(fileName);
        hasUnsavedChanges_ = false;
        updateWindowTitle();
        showStatusMessage(tr("Session loaded: %1").arg(QFileInfo(fileName).fileName()));
    }
}

void MainWindow::saveSession(const QString& path) {
    QString fileName = path;
    if (fileName.isEmpty()) {
        fileName = currentFile_;
    }
    
    if (fileName.isEmpty()) {
        // Auto-generate a default session file in the IDB directory
        QString idbPath = QString::fromStdString(get_path(PATH_TYPE_IDB));
        QFileInfo idbInfo(idbPath);
        QString idbDir = idbInfo.absolutePath();
        
        // Get session ID from conversation view
        QString sessionId;
        if (conversationView_) {
            sessionId = conversationView_->currentSessionId();
        }
        if (sessionId.isEmpty()) {
            sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        }
        
        // Create default filename
        fileName = QDir(idbDir).absoluteFilePath(QString("session_%1.llmre").arg(sessionId));
    }
    
    conversationView_->saveSession(fileName);
    setCurrentFile(fileName);
    hasUnsavedChanges_ = false;
    updateWindowTitle();
    showStatusMessage(tr("Session saved: %1").arg(QFileInfo(fileName).fileName()));
}

void MainWindow::saveSessionAs() {
    // Get IDB directory
    QString idbPath = QString::fromStdString(get_path(PATH_TYPE_IDB));
    QFileInfo idbInfo(idbPath);
    QString idbDir = idbInfo.absolutePath();
    
    // Generate suggested filename with session ID
    QString sessionId;
    if (conversationView_) {
        sessionId = conversationView_->currentSessionId();
    }
    if (sessionId.isEmpty()) {
        sessionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }
    QString suggestedName = QString("session_%1.llmre").arg(sessionId);
    QString suggestedPath = QDir(idbDir).absoluteFilePath(suggestedName);
    
    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Save Session As"),
        suggestedPath,
        tr("Session Files (*.llmre);;All Files (*)")
    );
    
    if (!fileName.isEmpty()) {
        saveSession(fileName);
    }
}

bool MainWindow::hasUnsavedChanges() const {
    return hasUnsavedChanges_ || conversationView_->hasUnsavedChanges();
}


void MainWindow::showSettings() {
    auto* dialog = new SettingsDialog(this);
    dialog->exec();
    dialog->deleteLater();
}

void MainWindow::showAbout() {
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(tr("About LLM RE Agent"));
    dialog->setModal(true);
    
    auto* layout = new QVBoxLayout(dialog);
    
    auto* iconLabel = new QLabel(dialog);
    iconLabel->setPixmap(ThemeManager::instance().themedIcon("application-icon").pixmap(64, 64));
    iconLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(iconLabel);
    
    auto* titleLabel = new QLabel(tr("<h2>LLM RE Agent</h2>"), dialog);
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);
    
    auto* versionLabel = new QLabel(tr("Version 2.0.0"), dialog);
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);
    
    auto* descLabel = new QLabel(
        tr("An advanced reverse engineering assistant powered by large language models."),
        dialog
    );
    descLabel->setWordWrap(true);
    descLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(descLabel);
    
    layout->addSpacing(20);
    
    auto* copyrightLabel = new QLabel(
        tr("Copyright © 2024 LLM RE Team<br>All rights reserved."),
        dialog
    );
    copyrightLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(copyrightLabel);
    
    auto* licenseLabel = new QLabel(
        tr("This software is licensed under the MIT License."),
        dialog
    );
    licenseLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(licenseLabel);
    
    layout->addSpacing(20);
    
    auto* closeButton = new QPushButton(tr("Close"), dialog);
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
    layout->addWidget(closeButton, 0, Qt::AlignCenter);
    
    dialog->exec();
    dialog->deleteLater();
}

void MainWindow::showKeyboardShortcuts() {
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle(tr("Keyboard Shortcuts"));
    dialog->setModal(true);
    dialog->resize(500, 600);
    
    auto* layout = new QVBoxLayout(dialog);
    
    auto* searchEdit = new QLineEdit(dialog);
    searchEdit->setPlaceholderText(tr("Search shortcuts..."));
    layout->addWidget(searchEdit);
    
    auto* shortcutsTree = new QTreeWidget(dialog);
    shortcutsTree->setHeaderLabels({tr("Action"), tr("Shortcut")});
    shortcutsTree->setRootIsDecorated(false);
    shortcutsTree->setAlternatingRowColors(true);
    
    // Populate shortcuts
    QMap<QString, QList<QPair<QString, QString>>> categorizedShortcuts;
    
    // File shortcuts
    categorizedShortcuts["File"] = {
        {tr("New Session"), QKeySequence(QKeySequence::New).toString()},
        {tr("Open Session"), QKeySequence(QKeySequence::Open).toString()},
        {tr("Save Session"), QKeySequence(QKeySequence::Save).toString()},
        {tr("Save As"), QKeySequence(QKeySequence::SaveAs).toString()},
        {tr("Quit"), QKeySequence(QKeySequence::Quit).toString()}
    };
    
    // Edit shortcuts
    categorizedShortcuts["Edit"] = {
        {tr("Select All"), QKeySequence(QKeySequence::SelectAll).toString()},
        {tr("Find"), QKeySequence(QKeySequence::Find).toString()},
        {tr("Replace"), tr("Ctrl+H")}
    };
    
    // View shortcuts
    categorizedShortcuts["View"] = {
        {tr("Toggle Sidebar"), tr("Ctrl+B")},
        {tr("Full Screen"), QKeySequence(QKeySequence::FullScreen).toString()}
    };
    
    // Window shortcuts
    categorizedShortcuts["Window"] = {
        {tr("Split Horizontally"), tr("Ctrl+Shift+H")},
        {tr("Split Vertically"), tr("Ctrl+Shift+V")},
        {tr("Close Split"), tr("Ctrl+Shift+W")},
        {tr("Focus Next Split"), tr("Ctrl+Tab")},
        {tr("Focus Previous Split"), tr("Ctrl+Shift+Tab")}
    };
    
    // Focus shortcuts
    categorizedShortcuts["Focus"] = {
        {tr("Focus Conversation"), tr("Alt+1")},
        {tr("Focus Memory"), tr("Alt+2")},
        {tr("Focus Tools"), tr("Alt+3")}
    };
    
    // Add custom shortcuts from shortcut manager
    for (const auto& shortcut : shortcutManager_->allShortcuts()) {
        bool found = false;
        for (auto& category : categorizedShortcuts) {
            for (const auto& item : category) {
                if (item.second == shortcut.first) {
                    found = true;
                    break;
                }
            }
            if (found) break;
        }
        if (!found) {
            categorizedShortcuts["Custom"].append({shortcut.second, shortcut.first});
        }
    }
    
    // Populate tree
    for (auto it = categorizedShortcuts.begin(); it != categorizedShortcuts.end(); ++it) {
        auto* categoryItem = new QTreeWidgetItem(shortcutsTree);
        categoryItem->setText(0, it.key());
        categoryItem->setFirstColumnSpanned(true);
        categoryItem->setFlags(categoryItem->flags() & ~Qt::ItemIsSelectable);
        
        QFont font = categoryItem->font(0);
        font.setBold(true);
        categoryItem->setFont(0, font);
        
        for (const auto& shortcut : it.value()) {
            auto* item = new QTreeWidgetItem(categoryItem);
            item->setText(0, shortcut.first);
            item->setText(1, shortcut.second);
        }
        
        categoryItem->setExpanded(true);
    }
    
    // Search functionality
    connect(searchEdit, &QLineEdit::textChanged, [shortcutsTree](const QString& text) {
        for (int i = 0; i < shortcutsTree->topLevelItemCount(); ++i) {
            auto* categoryItem = shortcutsTree->topLevelItem(i);
            bool categoryVisible = false;
            
            for (int j = 0; j < categoryItem->childCount(); ++j) {
                auto* item = categoryItem->child(j);
                bool matches = text.isEmpty() ||
                              item->text(0).contains(text, Qt::CaseInsensitive) ||
                              item->text(1).contains(text, Qt::CaseInsensitive);
                item->setHidden(!matches);
                if (matches) categoryVisible = true;
            }
            
            categoryItem->setHidden(!categoryVisible);
        }
    });
    
    layout->addWidget(shortcutsTree);
    
    auto* closeButton = new QPushButton(tr("Close"), dialog);
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);
    layout->addWidget(closeButton, 0, Qt::AlignCenter);
    
    dialog->exec();
    dialog->deleteLater();
}

void MainWindow::showNotification(const QString& title, const QString& message,
                                 const QString& type, int duration) {
    if (notificationManager_) {
        NotificationManager::NotificationType notifType = NotificationManager::Info;
        if (type == "success") notifType = NotificationManager::Success;
        else if (type == "warning") notifType = NotificationManager::Warning;
        else if (type == "error") notifType = NotificationManager::Error;
        
        notificationManager_->showNotification(title, message, notifType, duration);
    }
    
    // Also show in status bar
    showStatusMessage(message, duration);
}

void MainWindow::showStatusMessage(const QString& message, int timeout) {
    statusBar()->showMessage(message, timeout);
}

void MainWindow::updateWindowTitle() {
    setWindowTitle(tr("LLM RE Agent"));
    setWindowModified(hasUnsavedChanges());
}

void MainWindow::toggleFullScreen() {
    if (isFullScreen()) {
        showNormal();
        toggleFullScreenAction_->setChecked(false);
    } else {
        showFullScreen();
        toggleFullScreenAction_->setChecked(true);
    }
    emit fullScreenChanged(isFullScreen());
}


void MainWindow::closeEvent(QCloseEvent* event) {
    // Prevent multiple close dialogs
    if (isClosing_) {
        event->accept();
        return;
    }
    
    // If IDA is shutting down, don't show save dialog
    if (isShuttingDown_) {
        isClosing_ = true;
        if (rememberWindowState_ && shouldSaveSettings_) {
            saveSettings();
        }
        event->accept();
        return;
    }
    
    
    // Set flag before cleanup
    isClosing_ = true;
    
    // Save window state if preference is enabled
    if (rememberWindowState_ && shouldSaveSettings_) {
        saveSettings();
    }
    
    event->accept();
}

void MainWindow::changeEvent(QEvent* event) {
    QMainWindow::changeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    QMainWindow::keyPressEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mimeData = event->mimeData();
    
    if (mimeData->hasUrls()) {
        QList<QUrl> urls = mimeData->urls();
        for (const QUrl& url : urls) {
            if (url.isLocalFile()) {
                QString filePath = url.toLocalFile();
                if (filePath.endsWith(".llmre")) {
                    openSession(filePath);
                } else {
                    // Let conversation view handle other file types through its drop event
                    // by not accepting the event here
                    event->ignore();
                    return;
                }
            }
        }
        event->acceptProposedAction();
    }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::paintEvent(QPaintEvent* event) {
    // CRITICAL: Override painting to prevent IDA's theme from being used
    // We paint our own background with our theme colors
    QPainter painter(this);
    const auto& colors = ThemeManager::instance().colors();
    painter.fillRect(rect(), colors.background);
    
    // Don't call QMainWindow::paintEvent as it would use the application style
    // QMainWindow::paintEvent(event);  // INTENTIONALLY NOT CALLED
}


void MainWindow::onFileNew() {
    newSession();
}

void MainWindow::onFileOpen() {
    openSession();
}

void MainWindow::onFileSave() {
    saveSession();
}

void MainWindow::onFileSaveAs() {
    saveSessionAs();
}


void MainWindow::onFileExit() {
    close();
}

void MainWindow::onEditSelectAll() {
    if (auto* focusWidget = QApplication::focusWidget()) {
        if (auto* textEdit = qobject_cast<QTextEdit*>(focusWidget)) {
            textEdit->selectAll();
        } else if (auto* lineEdit = qobject_cast<QLineEdit*>(focusWidget)) {
            lineEdit->selectAll();
        } else if (conversationView_) {
            conversationView_->selectAll();
        }
    }
}

void MainWindow::onEditPreferences() {
    showSettings();
}

void MainWindow::onViewToggleSidebar() {
    // Toggle all dock widgets
    bool visible = toggleSidebarAction_->isChecked();
    memoryDockWidget_->setVisible(visible && memoryAnalysisAction_->isChecked());
    toolDockWidget_->setVisible(visible && executionHistoryAction_->isChecked());
}

void MainWindow::onViewToggleToolBar() {
    bool visible = toggleToolBarAction_->isChecked();
    mainToolBar_->setVisible(visible);
    editToolBar_->setVisible(visible);
    viewToolBar_->setVisible(visible);
}

void MainWindow::onViewToggleStatusBar() {
    statusBar()->setVisible(toggleStatusBarAction_->isChecked());
}

void MainWindow::onViewToggleFullScreen() {
    toggleFullScreen();
}

void MainWindow::onViewResetLayout() {
    resetLayout();
}

void MainWindow::onViewSaveLayout() {
    saveLayout();
}

void MainWindow::onViewLoadLayout() {
    QStringList layouts = availableLayouts();
    if (layouts.isEmpty()) {
        showStatusMessage(tr("No saved layouts found"));
        return;
    }
    
    bool ok;
    QString layout = QInputDialog::getItem(
        this, tr("Load Layout"),
        tr("Select layout:"),
        layouts, 0, false, &ok
    );
    
    if (ok && !layout.isEmpty()) {
        loadLayout(layout);
    }
}



void MainWindow::onToolsMemoryAnalysis() {
    if (memoryDockWidget_) {
        memoryDockWidget_->setVisible(memoryAnalysisAction_->isChecked());
        if (memoryAnalysisAction_->isChecked()) {
            memoryDockWidget_->raise();
        }
    }
}

void MainWindow::onToolsExecutionHistory() {
    if (toolDockWidget_) {
        toolDockWidget_->setVisible(executionHistoryAction_->isChecked());
        if (executionHistoryAction_->isChecked()) {
            toolDockWidget_->raise();
        }
    }
}

void MainWindow::onToolsConsole() {
    if (consoleDockWidget_) {
        consoleDockWidget_->setVisible(consoleAction_->isChecked());
        if (consoleAction_->isChecked()) {
            consoleDockWidget_->raise();
        }
    }
}

void MainWindow::onHelpDocumentation() {
    QDesktopServices::openUrl(QUrl("https://llmre.github.io/docs"));
}

void MainWindow::onHelpKeyboardShortcuts() {
    showKeyboardShortcuts();
}


void MainWindow::onHelpAbout() {
    showAbout();
}

void MainWindow::onThemeChanged() {
    // Apply theme ONLY to this window and its children
    ThemeManager::instance().applyThemeToWidget(this);
    
    // Update action states
    for (auto* action : findChildren<QAction*>()) {
        if (action->property("themeIcon").isValid()) {
            action->setIcon(ThemeManager::instance().themedIcon(action->property("themeIcon").toString()));
        }
    }
}

void MainWindow::onDockLocationChanged(Qt::DockWidgetArea area) {
    Q_UNUSED(area);
    // Handle dock location changes if needed
}

void MainWindow::onSplitterMoved(int pos, int index) {
    Q_UNUSED(pos);
    Q_UNUSED(index);
    // Handle splitter movements if needed
}

void MainWindow::updateActions() {
    bool hasSession = conversationView_ && conversationView_->model() && 
                     conversationView_->model()->rowCount() > 0;
    
    saveAction_->setEnabled(hasSession);
    saveAsAction_->setEnabled(hasSession);
    
    // Update edit actions based on focus
    QWidget* focusWidget = QApplication::focusWidget();
    bool isTextWidget = qobject_cast<QTextEdit*>(focusWidget) || 
                       qobject_cast<QLineEdit*>(focusWidget);

    selectAllAction_->setEnabled(isTextWidget || hasSession);
}

void MainWindow::saveWindowState() {
    if (layoutManager_) {
        layoutManager_->saveWindowState();
    }
}

void MainWindow::restoreWindowState() {
    if (layoutManager_) {
        layoutManager_->restoreWindowState();
    }
}

void MainWindow::checkUnsavedChanges() {
    if (hasUnsavedChanges() && conversationView_->isAutoSaveEnabled()) {
        saveSession();
    }
}

void MainWindow::onSessionModified() {
    hasUnsavedChanges_ = true;
    updateWindowTitle();
    updateActions();
}


void MainWindow::loadSettings() {
    // Load configuration from SettingsManager
    SettingsManager::instance().loadSettings();
    const Config& config = SettingsManager::instance().config();
    
    // Apply window management settings
    startMinimized_ = config.ui.start_minimized;
    rememberWindowState_ = config.ui.remember_window_state;
    
    // Apply conversation view settings
    if (conversationView_) {
        conversationView_->setAutoSaveEnabled(config.ui.auto_save_conversations);
        conversationView_->setAutoSaveInterval(config.ui.auto_save_interval);
        conversationView_->setDensityMode(config.ui.density_mode);
        conversationView_->setShowTimestamps(config.ui.show_timestamps);
    }
    
    QSettings settings;
    
    settings.beginGroup("MainWindow");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    
    startMinimized_ = settings.value("startMinimized", false).toBool();
    rememberWindowState_ = settings.value("rememberWindowState", true).toBool();
    
    settings.endGroup();
    
    // Load recent files
    settings.beginGroup("RecentFiles");
    QStringList recentFiles = settings.value("files").toStringList();
    settings.endGroup();
    
    for (const QString& file : recentFiles) {
        if (QFile::exists(file)) {
            updateRecentFiles();
        }
    }
    
    // Apply startup options
    if (startMinimized_) {
        QTimer::singleShot(0, this, &QMainWindow::showMinimized);
    }
}

void MainWindow::saveSettings() {
    // Only save if preference is enabled
    if (!rememberWindowState_) {
        return;
    }
    
    QSettings settings;
    
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.setValue("startMinimized", startMinimized_);
    settings.setValue("rememberWindowState", rememberWindowState_);
    settings.endGroup();
    
    // Save recent files
    settings.beginGroup("RecentFiles");
    QStringList recentFiles;
    for (auto* action : recentFileActions_) {
        if (!action->data().toString().isEmpty()) {
            recentFiles.append(action->data().toString());
        }
    }
    settings.setValue("files", recentFiles);
    settings.endGroup();
    
    // Save custom shortcuts
    if (shortcutManager_) {
        shortcutManager_->saveCustomShortcuts();
    }
}

bool MainWindow::maybeSave() {
    if (!hasUnsavedChanges()) {
        return true;
    }
    
    const QMessageBox::StandardButton ret = QMessageBox::warning(
        this, tr("LLM RE Agent"),
        tr("The session has been modified.\n"
           "Do you want to save your changes?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
    );
    
    switch (ret) {
    case QMessageBox::Save:
        saveSession();
        return !hasUnsavedChanges();
    case QMessageBox::Cancel:
        return false;
    case QMessageBox::Discard:
        // Clear unsaved changes to prevent auto-save on destruction
        for (auto* view : conversationViews_) {
            if (view) {
                view->discardChanges();
            }
        }
        hasUnsavedChanges_ = false;
        // Don't save settings when user explicitly discards changes
        shouldSaveSettings_ = false;
        return true;
    default:
        return true;
    }
}

void MainWindow::updateRecentFiles() {
    QSettings settings;
    settings.beginGroup("RecentFiles");
    QStringList files = settings.value("files").toStringList();
    
    // Add current file if not empty
    if (!currentFile_.isEmpty()) {
        files.removeAll(currentFile_);
        files.prepend(currentFile_);
        while (files.size() > MaxRecentFiles) {
            files.removeLast();
        }
    }
    
    settings.setValue("files", files);
    settings.endGroup();
    
    // Update menu actions
    int numRecentFiles = qMin(files.size(), MaxRecentFiles);
    
    for (int i = 0; i < numRecentFiles; ++i) {
        QString text = tr("&%1 %2").arg(i + 1).arg(strippedName(files[i]));
        recentFileActions_[i]->setText(text);
        recentFileActions_[i]->setData(files[i]);
        recentFileActions_[i]->setVisible(true);
    }
    
    for (int j = numRecentFiles; j < MaxRecentFiles; ++j) {
        recentFileActions_[j]->setVisible(false);
    }
    
    recentFilesMenu_->setEnabled(numRecentFiles > 0);
}

void MainWindow::setCurrentFile(const QString& fileName) {
    currentFile_ = fileName;
    updateRecentFiles();
    updateWindowTitle();
}

QString MainWindow::strippedName(const QString& fullFileName) {
    return QFileInfo(fullFileName).fileName();
}

MainWindow* MainWindow::instance() {
    return instance_;
}

void MainWindow::setInstance(MainWindow* window) {
    instance_ = window;
}

// UiController implementation

UiController::UiController(MainWindow* mainWindow)
    : QObject(mainWindow)
    , mainWindow_(mainWindow)
{
}

UiController::~UiController() {
    cleanup();
}

void UiController::registerConversationView(ConversationView* view) {
    if (!conversationViews_.contains(view)) {
        conversationViews_.append(view);
        
        // Set as active view if we don't have one
        if (!activeView_) {
            activeView_ = view;
        }
        
        // Connect view signals
        connect(view, &QObject::destroyed, [this, view]() {
            conversationViews_.removeOne(view);
            if (activeView_ == view) {
                activeView_ = conversationViews_.isEmpty() ? nullptr : conversationViews_.first();
            }
        });
    }
}

void UiController::unregisterConversationView(ConversationView* view) {
    conversationViews_.removeOne(view);
    if (activeView_ == view) {
        activeView_ = conversationViews_.isEmpty() ? nullptr : conversationViews_.first();
    }
}

void UiController::routeUserMessage(const QString& content) {
    // Route to active conversation view
    if (activeView_) {
        emit messageRouted(content, "user");
        
        // Send to agent controller if available
        if (agentController_) {
            // Check agent state to determine routing
            bool isRunning = agentController_->isRunning();
            bool isPaused = agentController_->isPaused();
            bool isCompleted = agentController_->isCompleted();
            
            if (!isRunning && !isPaused && !isCompleted) {
                // No active task, start new one
                agentController_->executeTask(content.toStdString());
            } else if (isRunning) {
                // Agent is running, inject as guidance
                agentController_->injectUserMessage(content.toStdString());
            } else if (agentController_->canContinue()) {
                // Agent is completed/idle, continue with new instructions
                agentController_->continueWithTask(content.toStdString());
            }
        } else {
            msg("LLM RE: ERROR - agentController_ is null!\n");
        }
    } else {
        msg("LLM RE: ERROR - activeView_ is null!\n");
    }
}

void UiController::routeAssistantMessage(const QString& content) {
    // Route to active conversation view
    if (activeView_) {
        activeView_->addAssistantMessage(content);
        emit messageRouted(content, "assistant");
    }
}

void UiController::routeToolExecution(const QString& toolName, const QJsonObject& params) {
    // Route to tool execution dock
    if (mainWindow_->toolDock()) {
        // Tool dock will handle execution
        emit messageRouted(toolName, "tool");
    }
}

void UiController::saveFocusState() {
    lastFocusedWidget_ = QApplication::focusWidget();
    
    // Determine which component has focus
    if (activeView_ && activeView_->isAncestorOf(lastFocusedWidget_)) {
        lastFocusedComponent_ = "conversation";
    } else if (mainWindow_->memoryDock() && 
               mainWindow_->memoryDock()->isAncestorOf(lastFocusedWidget_)) {
        lastFocusedComponent_ = "memory";
    } else if (mainWindow_->toolDock() && 
               mainWindow_->toolDock()->isAncestorOf(lastFocusedWidget_)) {
        lastFocusedComponent_ = "tools";
    }
}

void UiController::restoreFocusState() {
    if (lastFocusedWidget_ && lastFocusedWidget_->isVisible()) {
        lastFocusedWidget_->setFocus();
    } else {
        // Restore focus by component
        if (lastFocusedComponent_ == "conversation") {
            focusConversation();
        } else if (lastFocusedComponent_ == "memory") {
            focusMemory();
        } else if (lastFocusedComponent_ == "tools") {
            focusTools();
        }
    }
}

void UiController::focusConversation() {
    if (activeView_) {
        activeView_->setFocus();
        activeView_->focusInput();
        emit focusChanged("conversation");
    }
}

void UiController::focusMemory() {
    if (mainWindow_->memoryDock()) {
        // Show memory dock through its parent dock widget
        if (auto dockWidget = qobject_cast<QDockWidget*>(mainWindow_->memoryDock()->parent())) {
            dockWidget->show();
            dockWidget->raise();
        }
        mainWindow_->memoryDock()->setFocus();
        emit focusChanged("memory");
    }
}

void UiController::focusTools() {
    if (mainWindow_->toolDock()) {
        // Show tool dock through its parent dock widget
        if (auto dockWidget = qobject_cast<QDockWidget*>(mainWindow_->toolDock()->parent())) {
            dockWidget->show();
            dockWidget->raise();
        }
        mainWindow_->toolDock()->setFocus();
        emit focusChanged("tools");
    }
}

void UiController::synchronizeViews() {
    // Synchronize state across all conversation views
    for (auto* view : conversationViews_) {
        view->updateTheme();
    }
    emit stateChanged();
}

void UiController::broadcastThemeChange() {
    synchronizeViews();
}

void UiController::broadcastLayoutChange() {
    emit stateChanged();
}

ConversationView* UiController::activeConversationView() const {
    return activeView_;
}

bool UiController::hasActiveConversations() const {
    return !conversationViews_.isEmpty();
}

void UiController::cleanup() {
    // Disconnect all signals from conversation views
    for (auto* view : conversationViews_) {
        if (view) {
            view->disconnect(this);
        }
    }
    
    // Clear the list
    conversationViews_.clear();
    activeView_ = nullptr;
}

// NotificationManager implementation

NotificationManager::NotificationManager(QWidget* parent)
    : QObject(parent)
    , parentWidget_(parent)
{
}

NotificationManager::~NotificationManager() {
    clearAll();
}

void NotificationManager::showNotification(const QString& title, const QString& message,
                                         NotificationType type, int duration) {
    // Create notification widget
    auto* notification = new NotificationWidget(title, message, type, parentWidget_);
    
    connect(notification, &NotificationWidget::clicked, [this, title]() {
        emit notificationClicked(title);
    });
    
    connect(notification, &NotificationWidget::closed, [this, notification, title]() {
        activeNotifications_.removeOne(notification);
        notification->deleteLater();
        positionNotifications();
        emit notificationClosed(title);
    });
    
    // Add to active list
    activeNotifications_.append(notification);
    
    // Limit visible notifications
    while (activeNotifications_.size() > maxVisible_) {
        auto* oldest = activeNotifications_.takeFirst();
        oldest->close();
    }
    
    // Position and show
    positionNotifications();
    notification->show();
    notification->animateIn();
    
    // Auto-hide after duration
    if (duration > 0) {
        QTimer::singleShot(duration, notification, &NotificationWidget::animateOut);
    }
    
    // Add to history
    history_.append({title, message});
    while (history_.size() > 100) {
        history_.removeFirst();
    }
    
    // Play sound
    if (soundEnabled_) {
        playSound(type);
    }
}

void NotificationManager::showToast(const QString& message, int duration) {
    showNotification("", message, Info, duration);
}

void NotificationManager::clearAll() {
    for (auto* notification : activeNotifications_) {
        notification->close();
        notification->deleteLater();
    }
    activeNotifications_.clear();
    emit allCleared();
}

void NotificationManager::positionNotifications() {
    if (!parentWidget_) return;
    
    const int margin = 10;
    const int spacing = 5;
    
    QRect screenRect = parentWidget_->window()->geometry();
    int currentY = margin;
    
    for (auto* notification : activeNotifications_) {
        QSize size = notification->sizeHint();
        int x = 0;
        int y = 0;
        
        switch (corner_) {
        case Qt::TopLeftCorner:
            x = margin;
            y = currentY;
            break;
        case Qt::TopRightCorner:
            x = screenRect.width() - size.width() - margin;
            y = currentY;
            break;
        case Qt::BottomLeftCorner:
            x = margin;
            y = screenRect.height() - currentY - size.height();
            break;
        case Qt::BottomRightCorner:
            x = screenRect.width() - size.width() - margin;
            y = screenRect.height() - currentY - size.height();
            break;
        }
        
        notification->move(x, y);
        currentY += size.height() + spacing;
    }
}

void NotificationManager::playSound(NotificationType type) {
    // Platform-specific sound playback
    QString soundFile;
    switch (type) {
    case Success:
        soundFile = "success";
        break;
    case Warning:
        soundFile = "warning";
        break;
    case Error:
        soundFile = "error";
        break;
    default:
        soundFile = "info";
        break;
    }
    
    // QSound requires QtMultimedia module
    // QSound::play(":sounds/" + soundFile + ".wav");
    Q_UNUSED(soundFile);
}

// LayoutManager implementation

LayoutManager::LayoutManager(MainWindow* mainWindow)
    : QObject(mainWindow)
    , mainWindow_(mainWindow)
{
    // Set up layouts directory
    QString configPath = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    layoutsPath_ = QDir(configPath).filePath("layouts");
    QDir().mkpath(layoutsPath_);
}

void LayoutManager::saveLayout(const QString& name) {
    QString filePath = layoutFilePath(name);
    
    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup("Layout");
    settings.setValue("windowState", mainWindow_->saveState());
    settings.setValue("geometry", mainWindow_->saveGeometry());
    settings.setValue("timestamp", QDateTime::currentDateTime());
    settings.endGroup();
    
    currentLayout_ = name;
    emit layoutSaved(name);
}

void LayoutManager::loadLayout(const QString& name) {
    QString filePath = layoutFilePath(name);
    
    if (!QFile::exists(filePath)) {
        return;
    }
    
    QSettings settings(filePath, QSettings::IniFormat);
    settings.beginGroup("Layout");
    mainWindow_->restoreState(settings.value("windowState").toByteArray());
    mainWindow_->restoreGeometry(settings.value("geometry").toByteArray());
    settings.endGroup();
    
    currentLayout_ = name;
    emit layoutLoaded(name);
}

void LayoutManager::deleteLayout(const QString& name) {
    QString filePath = layoutFilePath(name);
    
    if (QFile::remove(filePath)) {
        if (currentLayout_ == name) {
            currentLayout_.clear();
        }
        emit layoutDeleted(name);
    }
}

QStringList LayoutManager::availableLayouts() const {
    QDir dir(layoutsPath_);
    QStringList filters;
    filters << "*.ini";
    
    QStringList layouts;
    for (const QString& file : dir.entryList(filters, QDir::Files)) {
        layouts.append(QFileInfo(file).baseName());
    }
    
    return layouts;
}

bool LayoutManager::hasLayout(const QString& name) const {
    return QFile::exists(layoutFilePath(name));
}

void LayoutManager::saveWindowState() {
    QSettings settings;
    settings.beginGroup("WindowState");
    settings.setValue("geometry", mainWindow_->saveGeometry());
    settings.setValue("state", mainWindow_->saveState());
    settings.setValue("maximized", mainWindow_->isMaximized());
    settings.setValue("fullscreen", mainWindow_->isFullScreen());
    settings.endGroup();
}

void LayoutManager::restoreWindowState() {
    QSettings settings;
    settings.beginGroup("WindowState");
    
    mainWindow_->restoreGeometry(settings.value("geometry").toByteArray());
    mainWindow_->restoreState(settings.value("state").toByteArray());
    
    if (settings.value("maximized", false).toBool()) {
        mainWindow_->showMaximized();
    } else if (settings.value("fullscreen", false).toBool()) {
        mainWindow_->showFullScreen();
    }
    
    settings.endGroup();
}

QString LayoutManager::layoutFilePath(const QString& name) const {
    return QDir(layoutsPath_).filePath(name + ".ini");
}

// ShortcutManager implementation

ShortcutManager::ShortcutManager(MainWindow* mainWindow)
    : QObject(mainWindow)
    , mainWindow_(mainWindow)
{
    loadCustomShortcuts();
}

void ShortcutManager::registerShortcut(const QString& id, const QKeySequence& sequence,
                                      const QString& description, std::function<void()> action) {
    // Remove existing shortcut if present
    if (shortcuts_.contains(id)) {
        delete shortcuts_[id].shortcut;
    }
    
    // Create new shortcut
    ShortcutInfo info;
    info.sequence = sequence;
    info.description = description;
    info.action = action;
    
    if (!sequence.isEmpty()) {
        info.shortcut = new QShortcut(sequence, mainWindow_);
        info.shortcut->setContext(Qt::ApplicationShortcut);
        
        connect(info.shortcut, &QShortcut::activated, [this, id, action]() {
            if (enabled_) {
                action();
                emit shortcutTriggered(id);
            }
        });
    }
    
    shortcuts_[id] = info;
    emit shortcutsChanged();
}

void ShortcutManager::unregisterShortcut(const QString& id) {
    if (shortcuts_.contains(id)) {
        delete shortcuts_[id].shortcut;
        shortcuts_.remove(id);
        emit shortcutsChanged();
    }
}

void ShortcutManager::updateShortcut(const QString& id, const QKeySequence& sequence) {
    if (shortcuts_.contains(id)) {
        auto& info = shortcuts_[id];
        
        // Delete old shortcut
        delete info.shortcut;
        info.shortcut = nullptr;
        
        // Update sequence
        info.sequence = sequence;
        
        // Create new shortcut if sequence is valid
        if (!sequence.isEmpty()) {
            info.shortcut = new QShortcut(sequence, mainWindow_);
            info.shortcut->setContext(Qt::ApplicationShortcut);
            
            connect(info.shortcut, &QShortcut::activated, [this, id]() {
                if (enabled_ && shortcuts_[id].action) {
                    shortcuts_[id].action();
                    emit shortcutTriggered(id);
                }
            });
        }
        
        emit shortcutsChanged();
    }
}

QKeySequence ShortcutManager::shortcutFor(const QString& id) const {
    return shortcuts_.value(id).sequence;
}

QString ShortcutManager::descriptionFor(const QString& id) const {
    return shortcuts_.value(id).description;
}

QList<QPair<QString, QString>> ShortcutManager::allShortcuts() const {
    QList<QPair<QString, QString>> result;
    
    for (auto it = shortcuts_.begin(); it != shortcuts_.end(); ++it) {
        if (!it.value().sequence.isEmpty()) {
            result.append({it.value().sequence.toString(), it.value().description});
        }
    }
    
    return result;
}

void ShortcutManager::setEnabled(bool enabled) {
    enabled_ = enabled;
    
    // Enable/disable all shortcuts
    for (auto& info : shortcuts_) {
        if (info.shortcut) {
            info.shortcut->setEnabled(enabled);
        }
    }
}

void ShortcutManager::loadCustomShortcuts() {
    QSettings settings;
    settings.beginGroup("CustomShortcuts");
    
    for (const QString& id : settings.childKeys()) {
        QString sequenceStr = settings.value(id).toString();
        if (!sequenceStr.isEmpty() && shortcuts_.contains(id)) {
            updateShortcut(id, QKeySequence(sequenceStr));
        }
    }
    
    settings.endGroup();
}

void ShortcutManager::saveCustomShortcuts() {
    QSettings settings;
    settings.beginGroup("CustomShortcuts");
    
    // Clear existing
    settings.remove("");
    
    // Save current
    for (auto it = shortcuts_.begin(); it != shortcuts_.end(); ++it) {
        if (!it.value().sequence.isEmpty()) {
            settings.setValue(it.key(), it.value().sequence.toString());
        }
    }
    
    settings.endGroup();
}

void ShortcutManager::resetToDefaults() {
    // This would reload default shortcuts from a configuration
    // For now, just clear custom shortcuts
    QSettings settings;
    settings.beginGroup("CustomShortcuts");
    settings.remove("");
    settings.endGroup();
    
    // Re-register all shortcuts with defaults
    emit shortcutsChanged();
}

} // namespace llm_re::ui_v2