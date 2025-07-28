#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"
#include "conversation_view.h"
#include "../widgets/command_palette.h"

namespace llm_re::ui_v2 {

class MemoryDock;
class ToolExecutionDock;
class StatisticsDock;
class FloatingInspector;
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
    CommandPalette* commandPalette() { return commandPalette_; }
    MemoryDock* memoryDock() { return memoryDock_; }
    ToolExecutionDock* toolDock() { return toolDock_; }
    StatisticsDock* statsDock() { return statsDock_; }
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
    
    // Split views
    void splitHorizontally();
    void splitVertically();
    void removeSplit();
    void focusNextSplit();
    void focusPreviousSplit();
    
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
    
    // Global instance
    static MainWindow* instance();
    static void setInstance(MainWindow* window);
    
signals:
    void windowShown();
    void windowHidden();
    void layoutChanged(const QString& layout);
    void themeChanged(const QString& theme);
    void sessionChanged(const QString& sessionPath);
    void splitViewCreated();
    void splitViewRemoved();
    void fullScreenChanged(bool fullScreen);
    
public slots:
    void showNotification(const QString& title, const QString& message, 
                         const QString& type = "info", int duration = 5000);
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
    
private slots:
    void onTrayIconActivated(QSystemTrayIcon::ActivationReason reason);
    void onFileNew();
    void onFileOpen();
    void onFileSave();
    void onFileSaveAs();
    void onFileExit();
    void onEditUndo();
    void onEditRedo();
    void onEditCut();
    void onEditCopy();
    void onEditPaste();
    void onEditSelectAll();
    void onEditFind();
    void onEditReplace();
    void onEditPreferences();
    void onViewToggleSidebar();
    void onViewToggleToolBar();
    void onViewToggleStatusBar();
    void onViewToggleFullScreen();
    void onViewResetLayout();
    void onViewSaveLayout();
    void onViewLoadLayout();
    void onToolsCommandPalette();
    void onToolsFloatingInspector();
    void onToolsStatistics();
    void onToolsMemoryAnalysis();
    void onToolsExecutionHistory();
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
    void onFloatingInspectorRequested(const QPoint& pos, const QString& context);
    
private:
    void setupUI();
    void createActions();
    void createMenus();
    void createToolBars();
    void createStatusBar();
    void createDockWindows();
    void createTrayIcon();
    void createCentralWidget();
    void connectSignals();
    void loadSettings();
    void saveSettings();
    void registerCommandProviders();
    void setupShortcuts();
    void setupSplitView();
    bool maybeSave();
    void updateRecentFiles();
    void setCurrentFile(const QString& fileName);
    QString strippedName(const QString& fullFileName);
    void createSplitView(Qt::Orientation orientation);
    ConversationView* createConversationView();
    
    // UI Controller
    std::unique_ptr<UiController> controller_;
    
    // Main components
    ConversationView* conversationView_ = nullptr;
    CommandPalette* commandPalette_ = nullptr;
    QStackedWidget* centralStack_ = nullptr;
    QSplitter* mainSplitter_ = nullptr;
    QList<ConversationView*> conversationViews_;
    
    // Dock windows
    MemoryDock* memoryDock_ = nullptr;
    ToolExecutionDock* toolDock_ = nullptr;
    StatisticsDock* statsDock_ = nullptr;
    QDockWidget* memoryDockWidget_ = nullptr;
    QDockWidget* toolDockWidget_ = nullptr;
    QDockWidget* statsDockWidget_ = nullptr;
    
    // Floating windows
    FloatingInspector* floatingInspector_ = nullptr;
    
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
    QAction* undoAction_ = nullptr;
    QAction* redoAction_ = nullptr;
    QAction* cutAction_ = nullptr;
    QAction* copyAction_ = nullptr;
    QAction* pasteAction_ = nullptr;
    QAction* selectAllAction_ = nullptr;
    QAction* findAction_ = nullptr;
    QAction* replaceAction_ = nullptr;
    QAction* preferencesAction_ = nullptr;
    QAction* toggleSidebarAction_ = nullptr;
    QAction* toggleToolBarAction_ = nullptr;
    QAction* toggleStatusBarAction_ = nullptr;
    QAction* toggleFullScreenAction_ = nullptr;
    QAction* resetLayoutAction_ = nullptr;
    QAction* saveLayoutAction_ = nullptr;
    QAction* commandPaletteAction_ = nullptr;
    QAction* floatingInspectorAction_ = nullptr;
    QAction* statisticsAction_ = nullptr;
    QAction* memoryAnalysisAction_ = nullptr;
    QAction* executionHistoryAction_ = nullptr;
    QAction* documentationAction_ = nullptr;
    QAction* keyboardShortcutsAction_ = nullptr;
    QAction* aboutAction_ = nullptr;
    QAction* aboutQtAction_ = nullptr;
    
    // Recent files
    QList<QAction*> recentFileActions_;
    static constexpr int MaxRecentFiles = 10;
    
    // System tray
    QSystemTrayIcon* trayIcon_ = nullptr;
    QMenu* trayIconMenu_ = nullptr;
    
    // State
    QString currentFile_;
    QString currentLayout_;
    bool hasUnsavedChanges_ = false;
    bool isClosing_ = false;
    
    // Settings
    bool showTrayIcon_ = true;
    bool minimizeToTray_ = true;
    bool closeToTray_ = false;
    bool startMinimized_ = false;
    bool rememberWindowState_ = true;
    bool autoSaveLayout_ = true;
    
    // Global instance
    static MainWindow* instance_;
};

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
    void focusStats();
    
    // Coordination
    void synchronizeViews();
    void broadcastThemeChange();
    void broadcastLayoutChange();
    
    // State queries
    ConversationView* activeConversationView() const;
    bool hasActiveConversations() const;
    
signals:
    void conversationViewActivated(ConversationView* view);
    void messageRouted(const QString& content, const QString& target);
    void focusChanged(const QString& component);
    void stateChanged();
    
private:
    MainWindow* mainWindow_;
    QList<ConversationView*> conversationViews_;
    ConversationView* activeView_ = nullptr;
    
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