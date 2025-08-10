#pragma once

#include "../core/ui_v2_common.h"
#include "conversation_view.h"

namespace llm_re::ui_v2 {

class MemoryDock;
class ToolExecutionDock;
class ConsoleDock;
class NotificationManager;
class LayoutManager;
class ShortcutManager;
class UiController;

// Main application window
class MainWindow : public QMainWindow {
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;
    
    // Component access
    ConversationView* conversationView() { return conversationView_; }
    MemoryDock* memoryDock() { return memoryDock_; }
    ToolExecutionDock* toolDock() { return toolDock_; }
    ConsoleDock* consoleDock() { return consoleDock_; }
    NotificationManager* notificationManager() { return notificationManager_; }
    UiController* controller() { return controller_.get(); }
    
    // Window management
    void showWindow();
    void hideWindow();
    void toggleWindow();
    void bringToFront();
    
    // Layout management
    void saveLayout(const QString& name = QString());
    void loadLayout(const QString& name);
    void resetLayout();
    QStringList availableLayouts() const;
    void deleteLayout(const QString& name);
    
    // Theme
    void applyTheme(const QString& theme);
    
    // Session management
    void newSession();
    void openSession(const QString& path = QString());
    void saveSession(const QString& path = QString());
    void saveSessionAs();
    bool hasUnsavedChanges() const;
    
    // Settings
    void showSettings();
    void showAbout();
    void showKeyboardShortcuts();
    
    // Shutdown handling
    void setShuttingDown(bool shutting) { isShuttingDown_ = shutting; }
    
    // Controller access
    UiController* uiController() const { return controller_.get(); }
    
    // Global instance
    static MainWindow* instance();
    static void setInstance(MainWindow* window);
    
signals:
    void windowShown();
    void windowHidden();
    void layoutChanged(const QString& layout);
    void themeChanged(const QString& theme);
    void sessionChanged(const QString& sessionPath);
    void fullScreenChanged(bool fullScreen);
    
public slots:
    void showNotification(const QString& title, const QString& message, const QString& type = "info", int duration = 5000);
    void showStatusMessage(const QString& message, int timeout = 2000);
    void updateWindowTitle();
    void toggleFullScreen();
    
protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    
private slots:
    void onFileNew();
    void onFileOpen();
    void onFileSave();
    void onFileSaveAs();
    void onFileExit();
    void onEditSelectAll();
    void onEditPreferences();
    void onViewToggleSidebar();
    void onViewToggleToolBar();
    void onViewToggleStatusBar();
    void onViewToggleFullScreen();
    void onViewResetLayout();
    void onViewSaveLayout();
    void onViewLoadLayout();
    void onToolsMemoryAnalysis();
    void onToolsExecutionHistory();
    void onToolsConsole();
    void onHelpDocumentation();
    void onHelpKeyboardShortcuts();
    void onHelpAbout();
    void onThemeChanged();
    void onDockLocationChanged(Qt::DockWidgetArea area);
    void onSplitterMoved(int pos, int index);
    void updateActions();
    void saveWindowState();
    void restoreWindowState();
    void checkUnsavedChanges();
    void onSessionModified();
    
private:
    void setupUI();
    void createActions();
    void createMenus();
    void createToolBars();
    void createStatusBar();
    void createDockWindows();
    void createCentralWidget();
    void connectSignals();
    void loadSettings();
    void saveSettings();
    void setupShortcuts();
    bool maybeSave();
    void updateRecentFiles();
    void setCurrentFile(const QString& fileName);
    QString strippedName(const QString& fullFileName);
    ConversationView* createConversationView();
    
    // UI Controller
    std::unique_ptr<UiController> controller_;
    
    // Main components
    ConversationView* conversationView_ = nullptr;
    QStackedWidget* centralStack_ = nullptr;
    QSplitter* mainSplitter_ = nullptr;
    QList<ConversationView*> conversationViews_;
    
    // Dock windows
    MemoryDock* memoryDock_ = nullptr;
    ToolExecutionDock* toolDock_ = nullptr;
    ConsoleDock* consoleDock_ = nullptr;
    QDockWidget* memoryDockWidget_ = nullptr;
    QDockWidget* toolDockWidget_ = nullptr;
    QDockWidget* consoleDockWidget_ = nullptr;

    // Managers
    NotificationManager* notificationManager_ = nullptr;
    LayoutManager* layoutManager_ = nullptr;
    ShortcutManager* shortcutManager_ = nullptr;
    
    // Menus
    QMenu* fileMenu_ = nullptr;
    QMenu* editMenu_ = nullptr;
    QMenu* viewMenu_ = nullptr;
    QMenu* toolsMenu_ = nullptr;
    QMenu* windowMenu_ = nullptr;
    QMenu* helpMenu_ = nullptr;
    QMenu* recentFilesMenu_ = nullptr;
    QMenu* layoutMenu_ = nullptr;
    QMenu* themeMenu_ = nullptr;
    
    // Toolbars
    QToolBar* mainToolBar_ = nullptr;
    QToolBar* editToolBar_ = nullptr;
    QToolBar* viewToolBar_ = nullptr;
    
    // Actions
    QAction* newAction_ = nullptr;
    QAction* openAction_ = nullptr;
    QAction* saveAction_ = nullptr;
    QAction* saveAsAction_ = nullptr;
    QAction* exitAction_ = nullptr;
    QAction* selectAllAction_ = nullptr;
    QAction* preferencesAction_ = nullptr;
    QAction* toggleSidebarAction_ = nullptr;
    QAction* toggleToolBarAction_ = nullptr;
    QAction* toggleStatusBarAction_ = nullptr;
    QAction* toggleFullScreenAction_ = nullptr;
    QAction* resetLayoutAction_ = nullptr;
    QAction* saveLayoutAction_ = nullptr;
    QAction* memoryAnalysisAction_ = nullptr;
    QAction* executionHistoryAction_ = nullptr;
    QAction* consoleAction_ = nullptr;
    QAction* documentationAction_ = nullptr;
    QAction* keyboardShortcutsAction_ = nullptr;
    QAction* aboutAction_ = nullptr;
    QAction* aboutQtAction_ = nullptr;
    
    // Recent files
    QList<QAction*> recentFileActions_;
    static constexpr int MaxRecentFiles = 10;
    
    // State
    QString currentFile_;
    QString currentLayout_;
    bool hasUnsavedChanges_ = false;
    bool isClosing_ = false;
    bool isShuttingDown_ = false;
    bool shouldSaveSettings_ = true;
    
    // Settings
    bool startMinimized_ = false;
    bool rememberWindowState_ = true;
    
    // Global instance
    static MainWindow* instance_;
};

// Forward declaration
class AgentController;

// UI Controller - manages UI state and coordination
class UiController : public QObject {
    Q_OBJECT
    
public:
    explicit UiController(MainWindow* mainWindow);
    ~UiController();
    
    // Component registration
    void registerConversationView(ConversationView* view);
    void unregisterConversationView(ConversationView* view);
    
    // Message routing
    void routeUserMessage(const QString& content);
    void routeAssistantMessage(const QString& content);
    void routeToolExecution(const QString& toolName, const QJsonObject& params);
    
    // Focus management
    void saveFocusState();
    void restoreFocusState();
    void focusConversation();
    void focusMemory();
    void focusTools();
    
    // Coordination
    void synchronizeViews();
    void broadcastThemeChange();
    void broadcastLayoutChange();
    
    // State queries
    ConversationView* activeConversationView() const;
    bool hasActiveConversations() const;
    
    // Cleanup
    void cleanup();
    
    // Agent controller
    void setAgentController(AgentController* controller) { agentController_ = controller; }
    AgentController* agentController() const { return agentController_; }
    
signals:
    void conversationViewActivated(ConversationView* view);
    void messageRouted(const QString& content, const QString& target);
    void focusChanged(const QString& component);
    void stateChanged();
    
private:
    MainWindow* mainWindow_;
    QList<ConversationView*> conversationViews_;
    ConversationView* activeView_ = nullptr;
    AgentController* agentController_ = nullptr;
    
    // Focus state
    QWidget* lastFocusedWidget_ = nullptr;
    QString lastFocusedComponent_;
};

// Notification manager
class NotificationManager : public QObject {
    Q_OBJECT
    
public:
    enum NotificationType {
        Info,
        Success,
        Warning,
        Error
    };
    
    explicit NotificationManager(QWidget* parent = nullptr);
    ~NotificationManager();
    
    void showNotification(const QString& title, const QString& message,
                         NotificationType type = Info, int duration = 5000);
    void showToast(const QString& message, int duration = 3000);
    void clearAll();
    
    // Settings
    void setPosition(Qt::Corner corner) { corner_ = corner; }
    Qt::Corner position() const { return corner_; }
    
    void setMaxVisible(int max) { maxVisible_ = max; }
    int maxVisible() const { return maxVisible_; }
    
    void setSoundEnabled(bool enabled) { soundEnabled_ = enabled; }
    bool isSoundEnabled() const { return soundEnabled_; }
    
    // History
    QList<QPair<QString, QString>> notificationHistory() const { return history_; }
    void clearHistory() { history_.clear(); }
    
signals:
    void notificationClicked(const QString& title);
    void notificationClosed(const QString& title);
    void allCleared();
    
private:
    QWidget* parentWidget_;
    Qt::Corner corner_ = Qt::TopRightCorner;
    int maxVisible_ = 3;
    bool soundEnabled_ = true;
    QList<QWidget*> activeNotifications_;
    QList<QPair<QString, QString>> history_;
    
    void positionNotifications();
    void playSound(NotificationType type);
};

// Layout manager
class LayoutManager : public QObject {
    Q_OBJECT
    
public:
    explicit LayoutManager(MainWindow* mainWindow);
    
    void saveLayout(const QString& name);
    void loadLayout(const QString& name);
    void deleteLayout(const QString& name);
    QStringList availableLayouts() const;
    bool hasLayout(const QString& name) const;
    
    void saveWindowState();
    void restoreWindowState();
    
    QString currentLayout() const { return currentLayout_; }
    
signals:
    void layoutSaved(const QString& name);
    void layoutLoaded(const QString& name);
    void layoutDeleted(const QString& name);
    
private:
    MainWindow* mainWindow_;
    QString currentLayout_;
    QString layoutsPath_;
    
    QString layoutFilePath(const QString& name) const;
};

// Shortcut manager
class ShortcutManager : public QObject {
    Q_OBJECT
    
public:
    explicit ShortcutManager(MainWindow* mainWindow);
    
    void registerShortcut(const QString& id, const QKeySequence& sequence,
                         const QString& description, std::function<void()> action);
    void unregisterShortcut(const QString& id);
    void updateShortcut(const QString& id, const QKeySequence& sequence);
    
    QKeySequence shortcutFor(const QString& id) const;
    QString descriptionFor(const QString& id) const;
    QList<QPair<QString, QString>> allShortcuts() const;
    
    void setEnabled(bool enabled);
    bool isEnabled() const { return enabled_; }
    
    void loadCustomShortcuts();
    void saveCustomShortcuts();
    void resetToDefaults();
    
signals:
    void shortcutTriggered(const QString& id);
    void shortcutsChanged();
    
private:
    struct ShortcutInfo {
        QKeySequence sequence;
        QString description;
        std::function<void()> action;
        QShortcut* shortcut = nullptr;
    };
    
    MainWindow* mainWindow_;
    QHash<QString, ShortcutInfo> shortcuts_;
    bool enabled_ = true;
};

} // namespace llm_re::ui_v2