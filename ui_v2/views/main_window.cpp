#include "../core/ui_v2_common.h"
#include "main_window.h"
#include "../core/theme_manager.h"
#include "../core/ui_constants.h"
#include "../core/ui_utils.h"
#include "../core/settings_manager.h"
#include "../widgets/command_palette.h"
#include "memory_dock.h"
#include "tool_execution_dock.h"
#include "statistics_dock.h"
#include "floating_inspector.h"
#include "settings_dialog.h"

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
            typeColor = QColor("#4CAF50");
            iconName = "check-circle";
            break;
        case NotificationManager::Warning:
            typeColor = QColor("#FF9800");
            iconName = "warning";
            break;
        case NotificationManager::Error:
            typeColor = QColor("#F44336");
            iconName = "error";
            break;
        default:
            typeColor = ThemeManager::instance().colors().primary;
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
    setupUI();
    loadSettings();
    
    // Set global instance
    setInstance(this);
    
    // Apply initial theme
    connect(&ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &MainWindow::onThemeChanged);
    onThemeChanged();
    
    // Start with a new session
    QTimer::singleShot(0, this, &MainWindow::newSession);
}

MainWindow::~MainWindow() {
    saveSettings();
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
    createTrayIcon();
    
    // Connect signals
    connectSignals();
    
    // Register command providers
    registerCommandProviders();
    
    // Setup managers
    layoutManager_ = new LayoutManager(this);
    shortcutManager_ = new ShortcutManager(this);
    notificationManager_ = new NotificationManager(this);
    floatingInspector_ = new FloatingInspector(this);
    
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
    undoAction_ = new QAction(ThemeManager::instance().themedIcon("edit-undo"), tr("&Undo"), this);
    undoAction_->setShortcut(QKeySequence::Undo);
    connect(undoAction_, &QAction::triggered, this, &MainWindow::onEditUndo);
    
    redoAction_ = new QAction(ThemeManager::instance().themedIcon("edit-redo"), tr("&Redo"), this);
    redoAction_->setShortcut(QKeySequence::Redo);
    connect(redoAction_, &QAction::triggered, this, &MainWindow::onEditRedo);
    
    cutAction_ = new QAction(ThemeManager::instance().themedIcon("edit-cut"), tr("Cu&t"), this);
    cutAction_->setShortcut(QKeySequence::Cut);
    connect(cutAction_, &QAction::triggered, this, &MainWindow::onEditCut);
    
    copyAction_ = new QAction(ThemeManager::instance().themedIcon("edit-copy"), tr("&Copy"), this);
    copyAction_->setShortcut(QKeySequence::Copy);
    connect(copyAction_, &QAction::triggered, this, &MainWindow::onEditCopy);
    
    pasteAction_ = new QAction(ThemeManager::instance().themedIcon("edit-paste"), tr("&Paste"), this);
    pasteAction_->setShortcut(QKeySequence::Paste);
    connect(pasteAction_, &QAction::triggered, this, &MainWindow::onEditPaste);
    
    selectAllAction_ = new QAction(ThemeManager::instance().themedIcon("edit-select-all"), tr("Select &All"), this);
    selectAllAction_->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction_, &QAction::triggered, this, &MainWindow::onEditSelectAll);
    
    findAction_ = new QAction(ThemeManager::instance().themedIcon("edit-find"), tr("&Find..."), this);
    findAction_->setShortcut(QKeySequence::Find);
    connect(findAction_, &QAction::triggered, this, &MainWindow::onEditFind);
    
    replaceAction_ = new QAction(ThemeManager::instance().themedIcon("edit-find-replace"), tr("&Replace..."), this);
    replaceAction_->setShortcut(QKeySequence(tr("Ctrl+H")));
    connect(replaceAction_, &QAction::triggered, this, &MainWindow::onEditReplace);
    
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
    commandPaletteAction_ = new QAction(ThemeManager::instance().themedIcon("command-palette"), tr("&Command Palette"), this);
    commandPaletteAction_->setShortcut(QKeySequence(tr("Ctrl+K")));
    connect(commandPaletteAction_, &QAction::triggered, this, &MainWindow::onToolsCommandPalette);
    
    floatingInspectorAction_ = new QAction(tr("&Floating Inspector"), this);
    floatingInspectorAction_->setCheckable(true);
    floatingInspectorAction_->setShortcut(QKeySequence(tr("Ctrl+I")));
    connect(floatingInspectorAction_, &QAction::triggered, this, &MainWindow::onToolsFloatingInspector);
    
    statisticsAction_ = new QAction(ThemeManager::instance().themedIcon("view-statistics"), tr("&Statistics"), this);
    statisticsAction_->setCheckable(true);
    connect(statisticsAction_, &QAction::triggered, this, &MainWindow::onToolsStatistics);
    
    memoryAnalysisAction_ = new QAction(ThemeManager::instance().themedIcon("memory-analysis"), tr("&Memory Analysis"), this);
    memoryAnalysisAction_->setCheckable(true);
    connect(memoryAnalysisAction_, &QAction::triggered, this, &MainWindow::onToolsMemoryAnalysis);
    
    executionHistoryAction_ = new QAction(ThemeManager::instance().themedIcon("execution-history"), tr("&Execution History"), this);
    executionHistoryAction_->setCheckable(true);
    connect(executionHistoryAction_, &QAction::triggered, this, &MainWindow::onToolsExecutionHistory);
    
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
    editMenu_->addAction(undoAction_);
    editMenu_->addAction(redoAction_);
    editMenu_->addSeparator();
    editMenu_->addAction(cutAction_);
    editMenu_->addAction(copyAction_);
    editMenu_->addAction(pasteAction_);
    editMenu_->addSeparator();
    editMenu_->addAction(selectAllAction_);
    editMenu_->addSeparator();
    editMenu_->addAction(findAction_);
    editMenu_->addAction(replaceAction_);
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
        ThemeManager::instance().setTheme(ThemeManager::Theme::Dark);
    });
    connect(lightThemeAction, &QAction::triggered, [this]() {
        ThemeManager::instance().setTheme(ThemeManager::Theme::Light);
    });
    
    // Set initial theme check
    if (ThemeManager::instance().currentTheme() == ThemeManager::Theme::Dark) {
        darkThemeAction->setChecked(true);
    } else {
        lightThemeAction->setChecked(true);
    }
    
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
    toolsMenu_->addAction(commandPaletteAction_);
    toolsMenu_->addAction(floatingInspectorAction_);
    toolsMenu_->addSeparator();
    toolsMenu_->addAction(memoryAnalysisAction_);
    toolsMenu_->addAction(executionHistoryAction_);
    toolsMenu_->addAction(statisticsAction_);
    
    // Window menu
    windowMenu_ = menuBar()->addMenu(tr("&Window"));
    connect(windowMenu_, &QMenu::aboutToShow, [this]() {
        windowMenu_->clear();
        
        // Split actions
        windowMenu_->addAction(tr("Split &Horizontally"), this, &MainWindow::splitHorizontally);
        windowMenu_->addAction(tr("Split &Vertically"), this, &MainWindow::splitVertically);
        windowMenu_->addAction(tr("&Remove Split"), this, &MainWindow::removeSplit);
        windowMenu_->addSeparator();
        windowMenu_->addAction(tr("Focus &Next Split"), this, &MainWindow::focusNextSplit);
        windowMenu_->addAction(tr("Focus &Previous Split"), this, &MainWindow::focusPreviousSplit);
        windowMenu_->addSeparator();
        
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
    mainToolBar_->addAction(commandPaletteAction_);
    
    // Edit toolbar
    editToolBar_ = addToolBar(tr("Edit"));
    editToolBar_->setObjectName("EditToolBar");
    editToolBar_->setMovable(true);
    editToolBar_->addAction(undoAction_);
    editToolBar_->addAction(redoAction_);
    editToolBar_->addSeparator();
    editToolBar_->addAction(cutAction_);
    editToolBar_->addAction(copyAction_);
    editToolBar_->addAction(pasteAction_);
    
    // View toolbar
    viewToolBar_ = addToolBar(tr("View"));
    viewToolBar_->setObjectName("ViewToolBar");
    viewToolBar_->setMovable(true);
    viewToolBar_->addAction(toggleFullScreenAction_);
    viewToolBar_->addSeparator();
    viewToolBar_->addAction(memoryAnalysisAction_);
    viewToolBar_->addAction(executionHistoryAction_);
    viewToolBar_->addAction(statisticsAction_);
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
    
    // Create command palette
    commandPalette_ = new CommandPalette(this);
    commandPalette_->setObjectName("CommandPalette");
    commandPalette_->registerBuiltinCommands();
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
    
    // Statistics dock
    statsDock_ = new StatisticsDock(this);
    statsDockWidget_ = new QDockWidget(tr("Statistics"), this);
    statsDockWidget_->setObjectName("StatsDock");
    statsDockWidget_->setWidget(statsDock_);
    statsDockWidget_->setAllowedAreas(Qt::AllDockWidgetAreas);
    addDockWidget(Qt::RightDockWidgetArea, statsDockWidget_);
    statsDockWidget_->hide();
    
    connect(statsDockWidget_, &QDockWidget::visibilityChanged,
            statisticsAction_, &QAction::setChecked);
    
    // Tab docks on the right
    tabifyDockWidget(memoryDockWidget_, statsDockWidget_);
}

void MainWindow::createTrayIcon() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        return;
    }
    
    trayIconMenu_ = new QMenu(this);
    trayIconMenu_->addAction(tr("&Show"), this, &MainWindow::showWindow);
    trayIconMenu_->addAction(tr("&Hide"), this, &MainWindow::hideWindow);
    trayIconMenu_->addSeparator();
    trayIconMenu_->addAction(exitAction_);
    
    trayIcon_ = new QSystemTrayIcon(this);
    trayIcon_->setContextMenu(trayIconMenu_);
    trayIcon_->setIcon(ThemeManager::instance().themedIcon("application-icon"));
    trayIcon_->setToolTip(tr("LLM RE Agent"));
    
    connect(trayIcon_, &QSystemTrayIcon::activated,
            this, &MainWindow::onTrayIconActivated);
    
    if (showTrayIcon_) {
        trayIcon_->show();
    }
}

void MainWindow::connectSignals() {
    // Conversation view signals
    connect(conversationView_, &ConversationView::messageSubmitted,
            controller_.get(), &UiController::routeUserMessage);
    connect(conversationView_, &ConversationView::toolExecutionRequested,
            [this](const QString& toolName, const QJsonObject& params) {
                controller_->routeToolExecution(toolName, params);
                if (toolDockWidget_ && !toolDockWidget_->isVisible()) {
                    toolDockWidget_->show();
                }
            });
    connect(conversationView_, &ConversationView::unsavedChangesChanged,
            this, &MainWindow::onSessionModified);
    
    // Command palette signals
    connect(commandPalette_, &CommandPalette::commandExecuted,
            [this](const QString& commandId) {
                showStatusMessage(tr("Executed: %1").arg(commandId));
            });
    
    // Floating inspector
    connect(conversationView_, &ConversationView::linkClicked,
            [this](const QUrl& url) {
                if (floatingInspector_ && floatingInspectorAction_->isChecked()) {
                    floatingInspector_->setPosition(FloatingInspector::FollowCursor);
                    floatingInspector_->showMessage("Link", url.toString());
                }
            });
}

void MainWindow::registerCommandProviders() {
    // File commands
    commandPalette_->registerCommand({
        "file.new", tr("New Session"), tr("Start a new conversation session"),
        "File", ThemeManager::instance().themedIcon("document-new"), QKeySequence::New,
        [this]() { newSession(); }
    });
    
    commandPalette_->registerCommand({
        "file.open", tr("Open Session"), tr("Open an existing session"),
        "File", ThemeManager::instance().themedIcon("document-open"), QKeySequence::Open,
        [this]() { openSession(); }
    });
    
    commandPalette_->registerCommand({
        "file.save", tr("Save Session"), tr("Save the current session"),
        "File", ThemeManager::instance().themedIcon("document-save"), QKeySequence::Save,
        [this]() { saveSession(); }
    });
    
    // View commands
    commandPalette_->registerCommand({
        "view.theme.dark", tr("Dark Theme"), tr("Switch to dark theme"),
        "View", ThemeManager::instance().themedIcon("theme-dark"), QKeySequence(),
        [this]() { ThemeManager::instance().setTheme(ThemeManager::Theme::Dark); }
    });
    
    commandPalette_->registerCommand({
        "view.theme.light", tr("Light Theme"), tr("Switch to light theme"),
        "View", ThemeManager::instance().themedIcon("theme-light"), QKeySequence(),
        [this]() { ThemeManager::instance().setTheme(ThemeManager::Theme::Light); }
    });
    
    // Window commands
    commandPalette_->registerCommand({
        "window.split.horizontal", tr("Split Horizontally"), tr("Split view horizontally"),
        "Window", ThemeManager::instance().themedIcon("view-split-horizontal"), QKeySequence(),
        [this]() { splitHorizontally(); }
    });
    
    commandPalette_->registerCommand({
        "window.split.vertical", tr("Split Vertically"), tr("Split view vertically"),
        "Window", ThemeManager::instance().themedIcon("view-split-vertical"), QKeySequence(),
        [this]() { splitVertically(); }
    });
    
    // Tools commands
    commandPalette_->registerCommand({
        "tools.memory", tr("Memory Analysis"), tr("Show memory analysis panel"),
        "Tools", ThemeManager::instance().themedIcon("memory-analysis"), QKeySequence(),
        [this]() { memoryDockWidget_->setVisible(!memoryDockWidget_->isVisible()); }
    });
    
    commandPalette_->registerCommand({
        "tools.execution", tr("Tool Execution"), tr("Show tool execution history"),
        "Tools", ThemeManager::instance().themedIcon("execution-history"), QKeySequence(),
        [this]() { toolDockWidget_->setVisible(!toolDockWidget_->isVisible()); }
    });
    
    commandPalette_->registerCommand({
        "tools.statistics", tr("Statistics"), tr("Show statistics dashboard"),
        "Tools", ThemeManager::instance().themedIcon("view-statistics"), QKeySequence(),
        [this]() { statsDockWidget_->setVisible(!statsDockWidget_->isVisible()); }
    });
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
    
    shortcutManager_->registerShortcut(
        "focus.stats", QKeySequence(tr("Alt+4")),
        tr("Focus statistics panel"),
        [this]() { controller_->focusStats(); }
    );
    
    // Quick split shortcuts
    shortcutManager_->registerShortcut(
        "split.horizontal", QKeySequence(tr("Ctrl+Shift+H")),
        tr("Split horizontally"),
        [this]() { splitHorizontally(); }
    );
    
    shortcutManager_->registerShortcut(
        "split.vertical", QKeySequence(tr("Ctrl+Shift+V")),
        tr("Split vertically"),
        [this]() { splitVertically(); }
    );
    
    shortcutManager_->registerShortcut(
        "split.close", QKeySequence(tr("Ctrl+Shift+W")),
        tr("Close current split"),
        [this]() { removeSplit(); }
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
    statsDockWidget_->setFloating(false);
    
    addDockWidget(Qt::RightDockWidgetArea, memoryDockWidget_);
    addDockWidget(Qt::BottomDockWidgetArea, toolDockWidget_);
    addDockWidget(Qt::RightDockWidgetArea, statsDockWidget_);
    
    tabifyDockWidget(memoryDockWidget_, statsDockWidget_);
    
    // Hide all docks by default
    memoryDockWidget_->hide();
    toolDockWidget_->hide();
    statsDockWidget_->hide();
    
    showStatusMessage(tr("Layout reset to default"));
}

QStringList MainWindow::availableLayouts() const {
    return layoutManager_->availableLayouts();
}

void MainWindow::deleteLayout(const QString& name) {
    layoutManager_->deleteLayout(name);
}

void MainWindow::applyTheme(const QString& theme) {
    if (theme == "dark") {
        ThemeManager::instance().setTheme(ThemeManager::Theme::Dark);
    } else if (theme == "light") {
        ThemeManager::instance().setTheme(ThemeManager::Theme::Light);
    }
    emit themeChanged(theme);
}

void MainWindow::splitHorizontally() {
    createSplitView(Qt::Horizontal);
}

void MainWindow::splitVertically() {
    createSplitView(Qt::Vertical);
}

void MainWindow::createSplitView(Qt::Orientation orientation) {
    if (conversationViews_.size() >= 4) {
        showStatusMessage(tr("Maximum number of splits reached"));
        return;
    }
    
    // Create new conversation view
    auto* newView = createConversationView();
    conversationViews_.append(newView);
    controller_->registerConversationView(newView);
    
    // Get current widget and its parent splitter
    QWidget* current = conversationView_;
    QSplitter* parentSplitter = qobject_cast<QSplitter*>(current->parent());
    
    if (!parentSplitter) {
        // This shouldn't happen, but handle it gracefully
        delete newView;
        conversationViews_.removeLast();
        return;
    }
    
    // Create new splitter
    auto* newSplitter = new QSplitter(orientation, this);
    
    // Get index of current widget in parent
    int index = parentSplitter->indexOf(current);
    
    // Remove current widget from parent
    current->setParent(nullptr);
    
    // Add both widgets to new splitter
    newSplitter->addWidget(current);
    newSplitter->addWidget(newView);
    
    // Insert new splitter at the same position
    parentSplitter->insertWidget(index, newSplitter);
    
    // Set equal sizes
    QList<int> sizes;
    int totalSize = (orientation == Qt::Horizontal) 
        ? newSplitter->width() : newSplitter->height();
    sizes << totalSize / 2 << totalSize / 2;
    newSplitter->setSizes(sizes);
    
    // Focus new view
    newView->setFocus();
    newView->focusInput();
    
    emit splitViewCreated();
    showStatusMessage(tr("Split view created"));
}

void MainWindow::removeSplit() {
    if (conversationViews_.size() <= 1) {
        showStatusMessage(tr("Cannot remove the last view"));
        return;
    }
    
    // Find current focused view
    ConversationView* viewToRemove = nullptr;
    for (auto* view : conversationViews_) {
        if (view->hasFocus() || view->isAncestorOf(QApplication::focusWidget())) {
            viewToRemove = view;
            break;
        }
    }
    
    if (!viewToRemove || viewToRemove == conversationView_) {
        // Don't remove the main view
        viewToRemove = conversationViews_.last();
    }
    
    // Check for unsaved changes
    if (viewToRemove->hasUnsavedChanges()) {
        auto reply = QMessageBox::question(
            this, tr("Unsaved Changes"),
            tr("This view has unsaved changes. Close anyway?"),
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    
    // Remove from controller
    controller_->unregisterConversationView(viewToRemove);
    conversationViews_.removeOne(viewToRemove);
    
    // Handle splitter cleanup
    QSplitter* parentSplitter = qobject_cast<QSplitter*>(viewToRemove->parent());
    if (parentSplitter && parentSplitter->count() == 2) {
        // Get the other widget
        QWidget* otherWidget = nullptr;
        for (int i = 0; i < parentSplitter->count(); ++i) {
            if (parentSplitter->widget(i) != viewToRemove) {
                otherWidget = parentSplitter->widget(i);
                break;
            }
        }
        
        if (otherWidget) {
            // Get grandparent splitter
            QSplitter* grandParent = qobject_cast<QSplitter*>(parentSplitter->parent());
            if (grandParent) {
                int index = grandParent->indexOf(parentSplitter);
                otherWidget->setParent(nullptr);
                grandParent->insertWidget(index, otherWidget);
                delete parentSplitter;
            }
        }
    }
    
    // Delete the view
    viewToRemove->deleteLater();
    
    // Focus another view
    if (!conversationViews_.isEmpty()) {
        conversationViews_.first()->setFocus();
    }
    
    emit splitViewRemoved();
    showStatusMessage(tr("Split view removed"));
}

void MainWindow::focusNextSplit() {
    if (conversationViews_.size() <= 1) return;
    
    // Find current focused view
    int currentIndex = -1;
    for (int i = 0; i < conversationViews_.size(); ++i) {
        if (conversationViews_[i]->hasFocus() || 
            conversationViews_[i]->isAncestorOf(QApplication::focusWidget())) {
            currentIndex = i;
            break;
        }
    }
    
    // Focus next view
    int nextIndex = (currentIndex + 1) % conversationViews_.size();
    conversationViews_[nextIndex]->setFocus();
    conversationViews_[nextIndex]->focusInput();
}

void MainWindow::focusPreviousSplit() {
    if (conversationViews_.size() <= 1) return;
    
    // Find current focused view
    int currentIndex = -1;
    for (int i = 0; i < conversationViews_.size(); ++i) {
        if (conversationViews_[i]->hasFocus() || 
            conversationViews_[i]->isAncestorOf(QApplication::focusWidget())) {
            currentIndex = i;
            break;
        }
    }
    
    // Focus previous view
    int prevIndex = (currentIndex - 1 + conversationViews_.size()) % conversationViews_.size();
    conversationViews_[prevIndex]->setFocus();
    conversationViews_[prevIndex]->focusInput();
}

void MainWindow::newSession() {
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
        saveSessionAs();
        return;
    }
    
    conversationView_->saveSession(fileName);
    setCurrentFile(fileName);
    hasUnsavedChanges_ = false;
    updateWindowTitle();
    showStatusMessage(tr("Session saved: %1").arg(QFileInfo(fileName).fileName()));
}

void MainWindow::saveSessionAs() {
    QString fileName = QFileDialog::getSaveFileName(
        this, tr("Save Session As"),
        QDir::homePath(),
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
        {tr("Undo"), QKeySequence(QKeySequence::Undo).toString()},
        {tr("Redo"), QKeySequence(QKeySequence::Redo).toString()},
        {tr("Cut"), QKeySequence(QKeySequence::Cut).toString()},
        {tr("Copy"), QKeySequence(QKeySequence::Copy).toString()},
        {tr("Paste"), QKeySequence(QKeySequence::Paste).toString()},
        {tr("Select All"), QKeySequence(QKeySequence::SelectAll).toString()},
        {tr("Find"), QKeySequence(QKeySequence::Find).toString()},
        {tr("Replace"), tr("Ctrl+H")}
    };
    
    // View shortcuts
    categorizedShortcuts["View"] = {
        {tr("Toggle Sidebar"), tr("Ctrl+B")},
        {tr("Full Screen"), QKeySequence(QKeySequence::FullScreen).toString()},
        {tr("Command Palette"), tr("Ctrl+K")},
        {tr("Floating Inspector"), tr("Ctrl+I")}
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
        {tr("Focus Tools"), tr("Alt+3")},
        {tr("Focus Statistics"), tr("Alt+4")}
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
    QString title = tr("LLM RE Agent");
    
    if (!currentFile_.isEmpty()) {
        title = tr("%1[*] - %2").arg(strippedName(currentFile_)).arg(title);
    } else {
        title = tr("Untitled[*] - %1").arg(title);
    }
    
    setWindowTitle(title);
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
    if (closeToTray_ && trayIcon_ && trayIcon_->isVisible()) {
        hide();
        event->ignore();
        return;
    }
    
    if (maybeSave()) {
        isClosing_ = true;
        saveSettings();
        event->accept();
    } else {
        event->ignore();
    }
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange) {
        if (isMinimized() && minimizeToTray_ && trayIcon_ && trayIcon_->isVisible()) {
            hide();
            event->ignore();
            return;
        }
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    // Global key handling
    if (event->modifiers() == Qt::ControlModifier) {
        switch (event->key()) {
        case Qt::Key_Tab:
            focusNextSplit();
            event->accept();
            return;
        case Qt::Key_Backtab:
            focusPreviousSplit();
            event->accept();
            return;
        }
    }
    
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

void MainWindow::onTrayIconActivated(QSystemTrayIcon::ActivationReason reason) {
    switch (reason) {
    case QSystemTrayIcon::Trigger:
    case QSystemTrayIcon::DoubleClick:
        toggleWindow();
        break;
    case QSystemTrayIcon::MiddleClick:
        showNotification(
            tr("LLM RE Agent"),
            tr("Running in background"),
            "info"
        );
        break;
    default:
        break;
    }
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

void MainWindow::onEditUndo() {
    if (auto* focusWidget = QApplication::focusWidget()) {
        if (auto* textEdit = qobject_cast<QTextEdit*>(focusWidget)) {
            textEdit->undo();
        } else if (auto* lineEdit = qobject_cast<QLineEdit*>(focusWidget)) {
            lineEdit->undo();
        }
    }
}

void MainWindow::onEditRedo() {
    if (auto* focusWidget = QApplication::focusWidget()) {
        if (auto* textEdit = qobject_cast<QTextEdit*>(focusWidget)) {
            textEdit->redo();
        } else if (auto* lineEdit = qobject_cast<QLineEdit*>(focusWidget)) {
            lineEdit->redo();
        }
    }
}

void MainWindow::onEditCut() {
    if (auto* focusWidget = QApplication::focusWidget()) {
        if (auto* textEdit = qobject_cast<QTextEdit*>(focusWidget)) {
            textEdit->cut();
        } else if (auto* lineEdit = qobject_cast<QLineEdit*>(focusWidget)) {
            lineEdit->cut();
        }
    }
}

void MainWindow::onEditCopy() {
    if (auto* focusWidget = QApplication::focusWidget()) {
        if (auto* textEdit = qobject_cast<QTextEdit*>(focusWidget)) {
            textEdit->copy();
        } else if (auto* lineEdit = qobject_cast<QLineEdit*>(focusWidget)) {
            lineEdit->copy();
        } else if (conversationView_) {
            conversationView_->copySelectedMessages();
        }
    }
}

void MainWindow::onEditPaste() {
    if (auto* focusWidget = QApplication::focusWidget()) {
        if (auto* textEdit = qobject_cast<QTextEdit*>(focusWidget)) {
            textEdit->paste();
        } else if (auto* lineEdit = qobject_cast<QLineEdit*>(focusWidget)) {
            lineEdit->paste();
        }
    }
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

void MainWindow::onEditFind() {
    if (conversationView_) {
        conversationView_->showSearchBar();
    }
}

void MainWindow::onEditReplace() {
    // Show replace dialog if implemented
    showStatusMessage(tr("Replace functionality coming soon"));
}

void MainWindow::onEditPreferences() {
    showSettings();
}

void MainWindow::onViewToggleSidebar() {
    // Toggle all dock widgets
    bool visible = toggleSidebarAction_->isChecked();
    memoryDockWidget_->setVisible(visible && memoryAnalysisAction_->isChecked());
    toolDockWidget_->setVisible(visible && executionHistoryAction_->isChecked());
    statsDockWidget_->setVisible(visible && statisticsAction_->isChecked());
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

void MainWindow::onToolsCommandPalette() {
    if (commandPalette_) {
        commandPalette_->popup();
    }
}

void MainWindow::onToolsFloatingInspector() {
    if (floatingInspector_) {
        if (floatingInspectorAction_->isChecked()) {
            floatingInspector_->show();
        } else {
            floatingInspector_->hide();
        }
    }
}

void MainWindow::onToolsStatistics() {
    if (statsDockWidget_) {
        statsDockWidget_->setVisible(statisticsAction_->isChecked());
        if (statisticsAction_->isChecked()) {
            statsDockWidget_->raise();
        }
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
    
    undoAction_->setEnabled(isTextWidget);
    redoAction_->setEnabled(isTextWidget);
    cutAction_->setEnabled(isTextWidget);
    copyAction_->setEnabled(isTextWidget || (conversationView_ && 
                                           !conversationView_->selectedMessages().isEmpty()));
    pasteAction_->setEnabled(isTextWidget);
    selectAllAction_->setEnabled(isTextWidget || hasSession);
    findAction_->setEnabled(hasSession);
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

void MainWindow::onFloatingInspectorRequested(const QPoint& pos, const QString& context) {
    if (floatingInspector_ && floatingInspectorAction_->isChecked()) {
        floatingInspector_->move(pos);
        floatingInspector_->showMessage("Context", context);
    }
}

void MainWindow::loadSettings() {
    // Load configuration from SettingsManager
    SettingsManager::instance().loadSettings();
    const Config& config = SettingsManager::instance().config();
    
    // Apply window management settings
    showTrayIcon_ = config.ui.show_tray_icon;
    minimizeToTray_ = config.ui.minimize_to_tray;
    closeToTray_ = config.ui.close_to_tray;
    startMinimized_ = config.ui.start_minimized;
    rememberWindowState_ = config.ui.remember_window_state;
    autoSaveLayout_ = config.ui.auto_save_layout;
    
    // Apply conversation view settings
    if (conversationView_) {
        conversationView_->setAutoSaveEnabled(config.ui.auto_save_conversations);
        conversationView_->setAutoSaveInterval(config.ui.auto_save_interval);
        conversationView_->setCompactMode(config.ui.compact_mode);
        conversationView_->setShowTimestamps(config.ui.show_timestamps);
    }
    
    QSettings settings;
    
    settings.beginGroup("MainWindow");
    restoreGeometry(settings.value("geometry").toByteArray());
    restoreState(settings.value("windowState").toByteArray());
    
    showTrayIcon_ = settings.value("showTrayIcon", true).toBool();
    minimizeToTray_ = settings.value("minimizeToTray", true).toBool();
    closeToTray_ = settings.value("closeToTray", false).toBool();
    startMinimized_ = settings.value("startMinimized", false).toBool();
    rememberWindowState_ = settings.value("rememberWindowState", true).toBool();
    autoSaveLayout_ = settings.value("autoSaveLayout", true).toBool();
    
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
    QSettings settings;
    
    settings.beginGroup("MainWindow");
    settings.setValue("geometry", saveGeometry());
    settings.setValue("windowState", saveState());
    settings.setValue("showTrayIcon", showTrayIcon_);
    settings.setValue("minimizeToTray", minimizeToTray_);
    settings.setValue("closeToTray", closeToTray_);
    settings.setValue("startMinimized", startMinimized_);
    settings.setValue("rememberWindowState", rememberWindowState_);
    settings.setValue("autoSaveLayout", autoSaveLayout_);
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

UiController::~UiController() = default;

void UiController::registerConversationView(ConversationView* view) {
    if (!conversationViews_.contains(view)) {
        conversationViews_.append(view);
        
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
    } else if (mainWindow_->statsDock() && 
               mainWindow_->statsDock()->isAncestorOf(lastFocusedWidget_)) {
        lastFocusedComponent_ = "stats";
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
        } else if (lastFocusedComponent_ == "stats") {
            focusStats();
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

void UiController::focusStats() {
    if (mainWindow_->statsDock()) {
        // Show stats dock through its parent dock widget
        if (auto dockWidget = qobject_cast<QDockWidget*>(mainWindow_->statsDock()->parent())) {
            dockWidget->show();
            dockWidget->raise();
        }
        mainWindow_->statsDock()->setFocus();
        emit focusChanged("stats");
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