#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"

namespace llm_re::ui_v2 {

// Command definition
struct Command {
    QString id;
    QString name;
    QString description;
    QString category;
    QIcon icon;
    QKeySequence shortcut;
    std::function<void()> action;
    std::function<bool()> isEnabled = []() { return true; };
    std::function<bool()> isVisible = []() { return true; };
    QStringList keywords;
    int priority = 0;
    bool closeOnExecute = true;
};

// Command provider interface
class ICommandProvider {
public:
    virtual ~ICommandProvider() = default;
    virtual QString providerId() const = 0;
    virtual QString providerName() const = 0;
    virtual QList<Command> commands() const = 0;
    virtual void refresh() {} // Called when palette opens
};

// Fuzzy matching result
struct FuzzyMatch {
    int score = 0;
    QVector<int> matchPositions;
    Command command;
};

// Command palette search input
class CommandPaletteInput : public QLineEdit {
    Q_OBJECT
    
public:
    explicit CommandPaletteInput(QWidget* parent = nullptr);
    
    void setPlaceholderTextWithShortcut(const QString& text, const QKeySequence& shortcut);
    
signals:
    void escapePressed();
    void upPressed();
    void downPressed();
    void enterPressed();
    void tabPressed();
    
protected:
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    
private:
    QString placeholderShortcut_;
};

// Command item delegate
class CommandItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
    
public:
    explicit CommandItemDelegate(QObject* parent = nullptr);
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    
    void setHighlightPositions(const QVector<int>& positions) { highlightPositions_ = positions; }
    
private:
    mutable QVector<int> highlightPositions_;
};

// Main command palette widget
class CommandPalette : public BaseStyledWidget {
    Q_OBJECT
    Q_PROPERTY(qreal showProgress READ showProgress WRITE setShowProgress)
    
public:
    explicit CommandPalette(QWidget* parent = nullptr);
    ~CommandPalette() override;
    
    // Provider management
    void registerProvider(std::shared_ptr<ICommandProvider> provider);
    void unregisterProvider(const QString& providerId);
    void clearProviders();
    
    // Command management
    void registerCommand(const Command& command);
    void registerCommands(const QList<Command>& commands);
    void unregisterCommand(const QString& commandId);
    void clearCommands();
    
    // Built-in commands
    void registerBuiltinCommands();
    
    // Show/hide
    void popup(const QPoint& pos = QPoint());
    void hide();
    bool isVisible() const { return QWidget::isVisible(); }
    
    // Settings
    void setMaxResults(int max) { maxResults_ = max; }
    int maxResults() const { return maxResults_; }
    
    void setFuzzySearchEnabled(bool enabled) { fuzzySearchEnabled_ = enabled; }
    bool isFuzzySearchEnabled() const { return fuzzySearchEnabled_; }
    
    void setShowShortcuts(bool show) { showShortcuts_ = show; }
    bool showShortcuts() const { return showShortcuts_; }
    
    void setShowCategories(bool show) { showCategories_ = show; }
    bool showCategories() const { return showCategories_; }
    
    void setRememberLastCommand(bool remember) { rememberLastCommand_ = remember; }
    bool rememberLastCommand() const { return rememberLastCommand_; }
    
    // Recent commands
    QStringList recentCommands() const { return recentCommands_; }
    void clearRecentCommands() { recentCommands_.clear(); saveRecentCommands(); }
    
    // Animation
    qreal showProgress() const { return showProgress_; }
    void setShowProgress(qreal progress);
    
    // Global instance
    static CommandPalette* instance();
    static void setInstance(CommandPalette* palette);
    
signals:
    void commandExecuted(const QString& commandId);
    void paletteShown();
    void paletteHidden();
    void searchTextChanged(const QString& text);
    
public slots:
    void refreshCommands();
    void focusSearch();
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void onThemeChanged() override;
    
private slots:
    void onSearchTextChanged(const QString& text);
    void onItemActivated(const QModelIndex& index);
    void onEscapePressed();
    void onUpPressed();
    void onDownPressed();
    void onEnterPressed();
    void onTabPressed();
    void updateFilter();
    void executeCommand(const Command& command);
    
private:
    void setupUI();
    void createBuiltinCommands();
    void loadRecentCommands();
    void saveRecentCommands();
    void addToRecentCommands(const QString& commandId);
    void collectAllCommands();
    void performFuzzySearch(const QString& query);
    void performSimpleSearch(const QString& query);
    int calculateFuzzyScore(const QString& text, const QString& query, QVector<int>& matchPositions);
    void updateListSelection();
    void animateShow();
    void animateHide();
    void centerOnScreen();
    
    // UI components
    CommandPaletteInput* searchInput_ = nullptr;
    QListView* resultsList_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* shortcutHintLabel_ = nullptr;
    
    // Models
    QStandardItemModel* model_ = nullptr;
    QSortFilterProxyModel* proxyModel_ = nullptr;
    CommandItemDelegate* delegate_ = nullptr;
    
    // Providers and commands
    QList<std::shared_ptr<ICommandProvider>> providers_;
    QList<Command> staticCommands_;
    QList<Command> allCommands_;
    QHash<QString, Command> commandMap_;
    
    // State
    QStringList recentCommands_;
    QString lastQuery_;
    int maxResults_ = 10;
    bool fuzzySearchEnabled_ = true;
    bool showShortcuts_ = true;
    bool showCategories_ = true;
    bool rememberLastCommand_ = true;
    qreal showProgress_ = 0.0;
    
    // Animation
    QPropertyAnimation* showAnimation_ = nullptr;
    
    // Global instance
    static CommandPalette* instance_;
};

// Built-in command providers

class FileCommandProvider : public ICommandProvider {
public:
    QString providerId() const override { return "file"; }
    QString providerName() const override { return "File"; }
    QList<Command> commands() const override;
};

class EditCommandProvider : public ICommandProvider {
public:
    QString providerId() const override { return "edit"; }
    QString providerName() const override { return "Edit"; }
    QList<Command> commands() const override;
};

class ViewCommandProvider : public ICommandProvider {
public:
    QString providerId() const override { return "view"; }
    QString providerName() const override { return "View"; }
    QList<Command> commands() const override;
};

class ToolsCommandProvider : public ICommandProvider {
public:
    QString providerId() const override { return "tools"; }
    QString providerName() const override { return "Tools"; }
    QList<Command> commands() const override;
};

class HelpCommandProvider : public ICommandProvider {
public:
    QString providerId() const override { return "help"; }
    QString providerName() const override { return "Help"; }
    QList<Command> commands() const override;
};

// Quick access widget that shows in a corner
class QuickCommandButton : public BaseStyledWidget {
    Q_OBJECT
    
public:
    enum Position {
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight
    };
    
    explicit QuickCommandButton(QWidget* parent = nullptr);
    
    void setPosition(Position pos) { position_ = pos; updatePosition(); }
    Position position() const { return position_; }
    
    void setShortcut(const QKeySequence& shortcut);
    QKeySequence shortcut() const { return shortcut_; }
    
    void setAutoHide(bool autoHide) { autoHide_ = autoHide; }
    bool autoHide() const { return autoHide_; }
    
signals:
    void triggered();
    
protected:
    void paintContent(QPainter* painter) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    
private:
    void updatePosition();
    void startAutoHideTimer();
    
    Position position_ = BottomRight;
    QKeySequence shortcut_;
    bool autoHide_ = true;
    QTimer* autoHideTimer_ = nullptr;
    QShortcut* shortcutObj_ = nullptr;
    bool isHovered_ = false;
};

} // namespace llm_re::ui_v2