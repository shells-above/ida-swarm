#include "command_palette.h"
#include "../core/theme_manager.h"
#include "../core/ui_utils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QKeyEvent>
#include <QApplication>
#include <QDesktopWidget>
#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QShortcut>
#include <QTimer>
#include <QSettings>
#include <QDebug>
#include <algorithm>

namespace llm_re::ui_v2 {

// Static instance
CommandPalette* CommandPalette::instance_ = nullptr;

// CommandPaletteInput implementation

CommandPaletteInput::CommandPaletteInput(QWidget* parent)
    : QLineEdit(parent) {
    
    setFrame(false);
    setAttribute(Qt::WA_MacShowFocusRect, false);
    
    // Apply custom styling
    const auto& theme = ThemeManager::instance();
    setFont(theme.typography().subtitle);
    
    setStyleSheet(QString(
        "QLineEdit {"
        "  background-color: transparent;"
        "  color: %1;"
        "  padding: %2px;"
        "  font-size: %3px;"
        "}"
    ).arg(theme.colors().textPrimary.name())
     .arg(Design::SPACING_MD)
     .arg(theme.typography().subtitle.pointSize()));
}

void CommandPaletteInput::setPlaceholderTextWithShortcut(const QString& text, const QKeySequence& shortcut) {
    setPlaceholderText(text);
    placeholderShortcut_ = shortcut.toString(QKeySequence::NativeText);
}

void CommandPaletteInput::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
        case Qt::Key_Escape:
            emit escapePressed();
            event->accept();
            return;
            
        case Qt::Key_Up:
            emit upPressed();
            event->accept();
            return;
            
        case Qt::Key_Down:
            emit downPressed();
            event->accept();
            return;
            
        case Qt::Key_Return:
        case Qt::Key_Enter:
            emit enterPressed();
            event->accept();
            return;
            
        case Qt::Key_Tab:
            emit tabPressed();
            event->accept();
            return;
    }
    
    QLineEdit::keyPressEvent(event);
}

void CommandPaletteInput::paintEvent(QPaintEvent* event) {
    QLineEdit::paintEvent(event);
    
    // Draw shortcut hint if empty
    if (text().isEmpty() && !placeholderShortcut_.isEmpty()) {
        QPainter painter(this);
        const auto& colors = ThemeManager::instance().colors();
        
        QRect shortcutRect = rect().adjusted(0, 0, -Design::SPACING_MD, 0);
        painter.setPen(colors.textTertiary);
        painter.setFont(ThemeManager::instance().typography().caption);
        painter.drawText(shortcutRect, Qt::AlignRight | Qt::AlignVCenter, placeholderShortcut_);
    }
}

// CommandItemDelegate implementation

CommandItemDelegate::CommandItemDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {
}

void CommandItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
    painter->save();
    
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    
    // Get command data
    Command cmd = index.data(Qt::UserRole).value<Command>();
    
    // Draw background
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, colors.selection);
    } else if (option.state & QStyle::State_MouseOver) {
        painter->fillRect(option.rect, colors.surfaceHover);
    }
    
    // Layout
    QRect iconRect = option.rect.adjusted(Design::SPACING_MD, 0, 0, 0);
    iconRect.setWidth(32);
    
    QRect textRect = option.rect.adjusted(iconRect.right() + Design::SPACING_SM, 0, 
                                         -Design::SPACING_MD, 0);
    
    // Draw icon
    if (!cmd.icon.isNull()) {
        cmd.icon.paint(painter, iconRect.adjusted(4, 8, -4, -8));
    }
    
    // Draw category
    if (!cmd.category.isEmpty()) {
        painter->setPen(colors.textTertiary);
        painter->setFont(theme.typography().caption);
        
        QRect categoryRect = textRect.adjusted(0, Design::SPACING_SM, 0, 0);
        categoryRect.setHeight(20);
        painter->drawText(categoryRect, Qt::AlignLeft | Qt::AlignTop, cmd.category);
        
        textRect.adjust(0, 20, 0, 0);
    }
    
    // Draw name with highlighting
    painter->setPen(colors.textPrimary);
    painter->setFont(theme.typography().subtitle);
    
    if (!highlightPositions_.isEmpty()) {
        // Draw with fuzzy match highlighting
        QString name = cmd.name;
        int x = textRect.x();
        int y = textRect.y() + 20;
        
        for (int i = 0; i < name.length(); ++i) {
            if (highlightPositions_.contains(i)) {
                painter->setPen(colors.primary);
                painter->setFont(QFont(theme.typography().subtitle.family(),
                                     theme.typography().subtitle.pointSize(),
                                     QFont::Bold));
            } else {
                painter->setPen(colors.textPrimary);
                painter->setFont(theme.typography().subtitle);
            }
            
            QString ch = name.mid(i, 1);
            painter->drawText(x, y, ch);
            x += painter->fontMetrics().horizontalAdvance(ch);
        }
    } else {
        painter->drawText(textRect.adjusted(0, 0, 0, -25), 
                         Qt::AlignLeft | Qt::AlignVCenter, cmd.name);
    }
    
    // Draw description
    if (!cmd.description.isEmpty()) {
        painter->setPen(colors.textSecondary);
        painter->setFont(theme.typography().caption);
        painter->drawText(textRect.adjusted(0, 20, 0, -Design::SPACING_SM),
                         Qt::AlignLeft | Qt::AlignVCenter, cmd.description);
    }
    
    // Draw shortcut
    if (!cmd.shortcut.isEmpty()) {
        QString shortcutText = cmd.shortcut.toString(QKeySequence::NativeText);
        QRect shortcutRect = option.rect.adjusted(0, 0, -Design::SPACING_MD, 0);
        
        painter->setPen(colors.textTertiary);
        painter->setFont(theme.typography().caption);
        painter->drawText(shortcutRect, Qt::AlignRight | Qt::AlignVCenter, shortcutText);
    }
    
    painter->restore();
}

QSize CommandItemDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
    Q_UNUSED(option)
    
    Command cmd = index.data(Qt::UserRole).value<Command>();
    int height = Design::SPACING_MD * 2;
    
    if (!cmd.category.isEmpty()) {
        height += 20;
    }
    
    if (!cmd.description.isEmpty()) {
        height += 20;
    } else {
        height += 10;
    }
    
    return QSize(500, height);
}

// CommandPalette implementation

CommandPalette::CommandPalette(QWidget* parent)
    : BaseStyledWidget(parent) {
    
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFocusPolicy(Qt::StrongFocus);
    
    // Styling
    setShadowEnabled(true);
    setShadowBlur(20);
    setBorderRadius(Design::RADIUS_LG);
    
    setupUI();
    registerBuiltinCommands();
    loadRecentCommands();
    
    // Install event filter for global key handling
    qApp->installEventFilter(this);
}

CommandPalette::~CommandPalette() {
    saveRecentCommands();
    qApp->removeEventFilter(this);
}

void CommandPalette::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(0);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    // Search input
    searchInput_ = new CommandPaletteInput(this);
    searchInput_->setPlaceholderTextWithShortcut(tr("Type to search commands..."), 
                                                 QKeySequence("Ctrl+K"));
    
    connect(searchInput_, &CommandPaletteInput::textChanged,
            this, &CommandPalette::onSearchTextChanged);
    connect(searchInput_, &CommandPaletteInput::escapePressed,
            this, &CommandPalette::onEscapePressed);
    connect(searchInput_, &CommandPaletteInput::upPressed,
            this, &CommandPalette::onUpPressed);
    connect(searchInput_, &CommandPaletteInput::downPressed,
            this, &CommandPalette::onDownPressed);
    connect(searchInput_, &CommandPaletteInput::enterPressed,
            this, &CommandPalette::onEnterPressed);
    connect(searchInput_, &CommandPaletteInput::tabPressed,
            this, &CommandPalette::onTabPressed);
    
    // Separator
    auto* separator = new QWidget(this);
    separator->setFixedHeight(1);
    separator->setStyleSheet(QString("background-color: %1;")
                           .arg(ThemeManager::instance().colors().border.name()));
    
    // Results list
    resultsList_ = new QListView(this);
    resultsList_->setFrameShape(QFrame::NoFrame);
    resultsList_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    resultsList_->setSelectionMode(QAbstractItemView::SingleSelection);
    resultsList_->setSelectionBehavior(QAbstractItemView::SelectRows);
    
    // Models
    model_ = new QStandardItemModel(this);
    proxyModel_ = new QSortFilterProxyModel(this);
    proxyModel_->setSourceModel(model_);
    proxyModel_->setFilterCaseSensitivity(Qt::CaseInsensitive);
    
    resultsList_->setModel(proxyModel_);
    
    // Delegate
    delegate_ = new CommandItemDelegate(this);
    resultsList_->setItemDelegate(delegate_);
    
    connect(resultsList_, &QListView::activated,
            this, &CommandPalette::onItemActivated);
    
    // Status bar
    auto* statusBar = new QWidget(this);
    statusBar->setFixedHeight(30);
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(Design::SPACING_MD, 0, Design::SPACING_MD, 0);
    
    statusLabel_ = new QLabel(this);
    statusLabel_->setFont(ThemeManager::instance().typography().caption);
    statusLayout->addWidget(statusLabel_);
    
    statusLayout->addStretch();
    
    shortcutHintLabel_ = new QLabel(this);
    shortcutHintLabel_->setFont(ThemeManager::instance().typography().caption);
    shortcutHintLabel_->setText(tr("↑↓ Navigate  ↵ Select  ESC Close"));
    statusLayout->addWidget(shortcutHintLabel_);
    
    // Add to layout
    mainLayout->addWidget(searchInput_);
    mainLayout->addWidget(separator);
    mainLayout->addWidget(resultsList_, 1);
    mainLayout->addWidget(statusBar);
    
    // Set size
    resize(600, 400);
}

void CommandPalette::registerProvider(std::shared_ptr<ICommandProvider> provider) {
    if (!provider) return;
    
    // Remove existing provider with same ID
    unregisterProvider(provider->providerId());
    
    providers_.append(provider);
    refreshCommands();
}

void CommandPalette::unregisterProvider(const QString& providerId) {
    providers_.erase(
        std::remove_if(providers_.begin(), providers_.end(),
                      [&providerId](const std::shared_ptr<ICommandProvider>& p) {
                          return p->providerId() == providerId;
                      }),
        providers_.end()
    );
    refreshCommands();
}

void CommandPalette::clearProviders() {
    providers_.clear();
    refreshCommands();
}

void CommandPalette::registerCommand(const Command& command) {
    staticCommands_.append(command);
    refreshCommands();
}

void CommandPalette::registerCommands(const QList<Command>& commands) {
    staticCommands_.append(commands);
    refreshCommands();
}

void CommandPalette::unregisterCommand(const QString& commandId) {
    staticCommands_.erase(
        std::remove_if(staticCommands_.begin(), staticCommands_.end(),
                      [&commandId](const Command& cmd) {
                          return cmd.id == commandId;
                      }),
        staticCommands_.end()
    );
    refreshCommands();
}

void CommandPalette::clearCommands() {
    staticCommands_.clear();
    refreshCommands();
}

void CommandPalette::registerBuiltinCommands() {
    // Register built-in providers
    registerProvider(std::make_shared<FileCommandProvider>());
    registerProvider(std::make_shared<EditCommandProvider>());
    registerProvider(std::make_shared<ViewCommandProvider>());
    registerProvider(std::make_shared<ToolsCommandProvider>());
    registerProvider(std::make_shared<HelpCommandProvider>());
    
    // Register palette-specific commands
    Command clearRecent;
    clearRecent.id = "palette.clearRecent";
    clearRecent.name = "Clear Recent Commands";
    clearRecent.description = "Clear the list of recently used commands";
    clearRecent.category = "Command Palette";
    clearRecent.icon = ThemeManager::instance().themedIcon("clear");
    clearRecent.action = [this]() { clearRecentCommands(); };
    registerCommand(clearRecent);
    
    Command toggleFuzzy;
    toggleFuzzy.id = "palette.toggleFuzzy";
    toggleFuzzy.name = "Toggle Fuzzy Search";
    toggleFuzzy.description = "Enable or disable fuzzy matching in search";
    toggleFuzzy.category = "Command Palette";
    toggleFuzzy.icon = ThemeManager::instance().themedIcon("search");
    toggleFuzzy.action = [this]() {
        setFuzzySearchEnabled(!isFuzzySearchEnabled());
        updateFilter();
    };
    registerCommand(toggleFuzzy);
}

void CommandPalette::popup(const QPoint& pos) {
    // Refresh commands from providers
    refreshCommands();
    
    // Position window
    if (pos.isNull()) {
        centerOnScreen();
    } else {
        move(pos);
    }
    
    // Reset state
    searchInput_->clear();
    searchInput_->setFocus();
    
    if (rememberLastCommand_ && !lastQuery_.isEmpty()) {
        searchInput_->setText(lastQuery_);
        searchInput_->selectAll();
    }
    
    // Show with animation
    animateShow();
    
    emit paletteShown();
}

void CommandPalette::hide() {
    if (rememberLastCommand_) {
        lastQuery_ = searchInput_->text();
    }
    
    animateHide();
    emit paletteHidden();
}

void CommandPalette::setShowProgress(qreal progress) {
    showProgress_ = progress;
    setWindowOpacity(progress);
    
    // Scale effect
    qreal scale = 0.8 + 0.2 * progress;
    setFixedSize(600 * scale, 400 * scale);
    
    update();
}

CommandPalette* CommandPalette::instance() {
    return instance_;
}

void CommandPalette::setInstance(CommandPalette* palette) {
    instance_ = palette;
}

void CommandPalette::refreshCommands() {
    collectAllCommands();
    updateFilter();
}

void CommandPalette::focusSearch() {
    searchInput_->setFocus();
    searchInput_->selectAll();
}

void CommandPalette::paintEvent(QPaintEvent* event) {
    BaseStyledWidget::paintEvent(event);
    
    // Additional custom painting if needed
}

void CommandPalette::resizeEvent(QResizeEvent* event) {
    BaseStyledWidget::resizeEvent(event);
    
    // Keep centered if was centered
    if (geometry().center() == QApplication::desktop()->availableGeometry().center()) {
        centerOnScreen();
    }
}

void CommandPalette::showEvent(QShowEvent* event) {
    BaseStyledWidget::showEvent(event);
    searchInput_->setFocus();
}

void CommandPalette::hideEvent(QHideEvent* event) {
    BaseStyledWidget::hideEvent(event);
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event) {
    // Global shortcut handling could go here
    return BaseStyledWidget::eventFilter(watched, event);
}

void CommandPalette::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }
    
    BaseStyledWidget::keyPressEvent(event);
}

void CommandPalette::onThemeChanged() {
    BaseStyledWidget::onThemeChanged();
    
    // Update colors
    const auto& colors = ThemeManager::instance().colors();
    
    if (statusLabel_) {
        statusLabel_->setStyleSheet(QString("color: %1;")
                                  .arg(colors.textSecondary.name()));
    }
    
    if (shortcutHintLabel_) {
        shortcutHintLabel_->setStyleSheet(QString("color: %1;")
                                        .arg(colors.textTertiary.name()));
    }
}

void CommandPalette::onSearchTextChanged(const QString& text) {
    updateFilter();
    emit searchTextChanged(text);
}

void CommandPalette::onItemActivated(const QModelIndex& index) {
    if (!index.isValid()) return;
    
    QModelIndex sourceIndex = proxyModel_->mapToSource(index);
    Command cmd = model_->item(sourceIndex.row())->data(Qt::UserRole).value<Command>();
    
    executeCommand(cmd);
}

void CommandPalette::onEscapePressed() {
    hide();
}

void CommandPalette::onUpPressed() {
    QModelIndex current = resultsList_->currentIndex();
    int row = current.row() - 1;
    
    if (row < 0) {
        row = proxyModel_->rowCount() - 1;
    }
    
    resultsList_->setCurrentIndex(proxyModel_->index(row, 0));
}

void CommandPalette::onDownPressed() {
    QModelIndex current = resultsList_->currentIndex();
    int row = current.row() + 1;
    
    if (row >= proxyModel_->rowCount()) {
        row = 0;
    }
    
    resultsList_->setCurrentIndex(proxyModel_->index(row, 0));
}

void CommandPalette::onEnterPressed() {
    QModelIndex current = resultsList_->currentIndex();
    if (current.isValid()) {
        onItemActivated(current);
    } else if (proxyModel_->rowCount() > 0) {
        // Execute first result
        onItemActivated(proxyModel_->index(0, 0));
    }
}

void CommandPalette::onTabPressed() {
    // Tab could be used for autocomplete
    onDownPressed();
}

void CommandPalette::updateFilter() {
    QString query = searchInput_->text().trimmed();
    
    if (fuzzySearchEnabled_ && !query.isEmpty()) {
        performFuzzySearch(query);
    } else {
        performSimpleSearch(query);
    }
    
    // Update status
    int totalCommands = allCommands_.size();
    int visibleCommands = proxyModel_->rowCount();
    
    if (query.isEmpty()) {
        statusLabel_->setText(tr("%1 commands available").arg(totalCommands));
    } else {
        statusLabel_->setText(tr("%1 of %2 commands").arg(visibleCommands).arg(totalCommands));
    }
    
    // Select first item
    if (proxyModel_->rowCount() > 0) {
        resultsList_->setCurrentIndex(proxyModel_->index(0, 0));
    }
}

void CommandPalette::executeCommand(const Command& command) {
    if (!command.isEnabled()) {
        return;
    }
    
    // Add to recent
    addToRecentCommands(command.id);
    
    // Close palette if requested
    if (command.closeOnExecute) {
        hide();
    }
    
    // Execute action
    if (command.action) {
        command.action();
    }
    
    emit commandExecuted(command.id);
}

void CommandPalette::createBuiltinCommands() {
    // This is handled by providers now
}

void CommandPalette::loadRecentCommands() {
    QSettings settings;
    settings.beginGroup("CommandPalette");
    recentCommands_ = settings.value("recentCommands").toStringList();
    
    // Limit to last 20
    while (recentCommands_.size() > 20) {
        recentCommands_.removeLast();
    }
    
    settings.endGroup();
}

void CommandPalette::saveRecentCommands() {
    QSettings settings;
    settings.beginGroup("CommandPalette");
    settings.setValue("recentCommands", recentCommands_);
    settings.endGroup();
}

void CommandPalette::addToRecentCommands(const QString& commandId) {
    recentCommands_.removeAll(commandId);
    recentCommands_.prepend(commandId);
    
    while (recentCommands_.size() > 20) {
        recentCommands_.removeLast();
    }
    
    saveRecentCommands();
}

void CommandPalette::collectAllCommands() {
    allCommands_.clear();
    commandMap_.clear();
    
    // Add static commands
    allCommands_.append(staticCommands_);
    
    // Add provider commands
    for (const auto& provider : providers_) {
        provider->refresh();
        allCommands_.append(provider->commands());
    }
    
    // Filter visible and enabled commands
    allCommands_.erase(
        std::remove_if(allCommands_.begin(), allCommands_.end(),
                      [](const Command& cmd) {
                          return !cmd.isVisible();
                      }),
        allCommands_.end()
    );
    
    // Build map for quick lookup
    for (const Command& cmd : allCommands_) {
        commandMap_[cmd.id] = cmd;
    }
    
    // Sort by priority and recent usage
    std::sort(allCommands_.begin(), allCommands_.end(),
             [this](const Command& a, const Command& b) {
                 // Recent commands first
                 int aRecent = recentCommands_.indexOf(a.id);
                 int bRecent = recentCommands_.indexOf(b.id);
                 
                 if (aRecent >= 0 && bRecent >= 0) {
                     return aRecent < bRecent;
                 } else if (aRecent >= 0) {
                     return true;
                 } else if (bRecent >= 0) {
                     return false;
                 }
                 
                 // Then by priority
                 if (a.priority != b.priority) {
                     return a.priority > b.priority;
                 }
                 
                 // Then alphabetically
                 return a.name < b.name;
             });
}

void CommandPalette::performFuzzySearch(const QString& query) {
    model_->clear();
    
    QList<FuzzyMatch> matches;
    
    for (const Command& cmd : allCommands_) {
        QVector<int> positions;
        int score = calculateFuzzyScore(cmd.name, query, positions);
        
        // Also check description and keywords
        if (score == 0) {
            score = calculateFuzzyScore(cmd.description, query, positions) / 2;
        }
        
        if (score == 0) {
            for (const QString& keyword : cmd.keywords) {
                score = calculateFuzzyScore(keyword, query, positions) / 3;
                if (score > 0) break;
            }
        }
        
        if (score > 0) {
            FuzzyMatch match;
            match.score = score;
            match.matchPositions = positions;
            match.command = cmd;
            matches.append(match);
        }
    }
    
    // Sort by score
    std::sort(matches.begin(), matches.end(),
             [](const FuzzyMatch& a, const FuzzyMatch& b) {
                 return a.score > b.score;
             });
    
    // Add to model
    int count = 0;
    for (const FuzzyMatch& match : matches) {
        if (count >= maxResults_) break;
        
        auto* item = new QStandardItem();
        item->setData(QVariant::fromValue(match.command), Qt::UserRole);
        item->setData(match.command.name, Qt::DisplayRole);
        
        model_->appendRow(item);
        
        // Update delegate with match positions
        if (count == 0) {
            delegate_->setHighlightPositions(match.matchPositions);
        }
        
        count++;
    }
}

void CommandPalette::performSimpleSearch(const QString& query) {
    model_->clear();
    
    int count = 0;
    for (const Command& cmd : allCommands_) {
        if (count >= maxResults_) break;
        
        bool matches = query.isEmpty() ||
                      cmd.name.contains(query, Qt::CaseInsensitive) ||
                      cmd.description.contains(query, Qt::CaseInsensitive) ||
                      cmd.category.contains(query, Qt::CaseInsensitive);
        
        if (!matches) {
            for (const QString& keyword : cmd.keywords) {
                if (keyword.contains(query, Qt::CaseInsensitive)) {
                    matches = true;
                    break;
                }
            }
        }
        
        if (matches) {
            auto* item = new QStandardItem();
            item->setData(QVariant::fromValue(cmd), Qt::UserRole);
            item->setData(cmd.name, Qt::DisplayRole);
            
            model_->appendRow(item);
            count++;
        }
    }
    
    // Clear highlight positions for simple search
    delegate_->setHighlightPositions({});
}

int CommandPalette::calculateFuzzyScore(const QString& text, const QString& query, 
                                       QVector<int>& matchPositions) {
    matchPositions.clear();
    
    if (query.isEmpty() || text.isEmpty()) {
        return 0;
    }
    
    QString lowerText = text.toLower();
    QString lowerQuery = query.toLower();
    
    int score = 0;
    int textIndex = 0;
    int consecutiveMatches = 0;
    
    for (int queryIndex = 0; queryIndex < lowerQuery.length(); ++queryIndex) {
        QChar queryChar = lowerQuery[queryIndex];
        bool found = false;
        
        for (int i = textIndex; i < lowerText.length(); ++i) {
            if (lowerText[i] == queryChar) {
                found = true;
                matchPositions.append(i);
                
                // Score calculation
                score += 10; // Base score for match
                
                // Bonus for consecutive matches
                if (i == textIndex) {
                    consecutiveMatches++;
                    score += consecutiveMatches * 5;
                } else {
                    consecutiveMatches = 1;
                }
                
                // Bonus for matching at word boundaries
                if (i == 0 || !lowerText[i-1].isLetterOrNumber()) {
                    score += 15;
                }
                
                // Bonus for matching capital letters
                if (text[i].isUpper()) {
                    score += 10;
                }
                
                textIndex = i + 1;
                break;
            }
        }
        
        if (!found) {
            return 0; // Query character not found
        }
    }
    
    // Penalty for length difference
    score -= (lowerText.length() - lowerQuery.length());
    
    return qMax(score, 1);
}

void CommandPalette::updateListSelection() {
    // Handled by key press events
}

void CommandPalette::animateShow() {
    if (showAnimation_) {
        showAnimation_->stop();
        delete showAnimation_;
    }
    
    show();
    raise();
    activateWindow();
    
    setShowProgress(0.0);
    
    showAnimation_ = new QPropertyAnimation(this, "showProgress", this);
    showAnimation_->setDuration(Design::ANIM_FAST);
    showAnimation_->setStartValue(0.0);
    showAnimation_->setEndValue(1.0);
    showAnimation_->setEasingCurve(QEasingCurve::OutCubic);
    showAnimation_->start(QAbstractAnimation::DeleteWhenStopped);
}

void CommandPalette::animateHide() {
    if (showAnimation_) {
        showAnimation_->stop();
        delete showAnimation_;
    }
    
    showAnimation_ = new QPropertyAnimation(this, "showProgress", this);
    showAnimation_->setDuration(Design::ANIM_FAST);
    showAnimation_->setStartValue(showProgress_);
    showAnimation_->setEndValue(0.0);
    showAnimation_->setEasingCurve(QEasingCurve::InCubic);
    
    connect(showAnimation_, &QPropertyAnimation::finished, [this]() {
        QWidget::hide();
        showAnimation_ = nullptr;
    });
    
    showAnimation_->start(QAbstractAnimation::DeleteWhenStopped);
}

void CommandPalette::centerOnScreen() {
    QRect screen = QApplication::desktop()->availableGeometry(this);
    move(screen.center() - rect().center());
}

// Built-in command providers

QList<Command> FileCommandProvider::commands() const {
    QList<Command> cmds;
    
    Command newFile;
    newFile.id = "file.new";
    newFile.name = "New File";
    newFile.description = "Create a new file";
    newFile.category = "File";
    newFile.icon = ThemeManager::instance().themedIcon("file-new");
    newFile.shortcut = QKeySequence::New;
    newFile.keywords = {"create", "add"};
    cmds.append(newFile);
    
    Command openFile;
    openFile.id = "file.open";
    openFile.name = "Open File";
    openFile.description = "Open an existing file";
    openFile.category = "File";
    openFile.icon = ThemeManager::instance().themedIcon("file-open");
    openFile.shortcut = QKeySequence::Open;
    cmds.append(openFile);
    
    Command saveFile;
    saveFile.id = "file.save";
    saveFile.name = "Save File";
    saveFile.description = "Save the current file";
    saveFile.category = "File";
    saveFile.icon = ThemeManager::instance().themedIcon("file-save");
    saveFile.shortcut = QKeySequence::Save;
    cmds.append(saveFile);
    
    Command saveAs;
    saveAs.id = "file.saveAs";
    saveAs.name = "Save As...";
    saveAs.description = "Save the current file with a new name";
    saveAs.category = "File";
    saveAs.icon = ThemeManager::instance().themedIcon("file-save-as");
    saveAs.shortcut = QKeySequence::SaveAs;
    cmds.append(saveAs);
    
    return cmds;
}

QList<Command> EditCommandProvider::commands() const {
    QList<Command> cmds;
    
    Command undo;
    undo.id = "edit.undo";
    undo.name = "Undo";
    undo.description = "Undo the last action";
    undo.category = "Edit";
    undo.icon = ThemeManager::instance().themedIcon("undo");
    undo.shortcut = QKeySequence::Undo;
    cmds.append(undo);
    
    Command redo;
    redo.id = "edit.redo";
    redo.name = "Redo";
    redo.description = "Redo the last undone action";
    redo.category = "Edit";
    redo.icon = ThemeManager::instance().themedIcon("redo");
    redo.shortcut = QKeySequence::Redo;
    cmds.append(redo);
    
    Command cut;
    cut.id = "edit.cut";
    cut.name = "Cut";
    cut.description = "Cut the selected text";
    cut.category = "Edit";
    cut.icon = ThemeManager::instance().themedIcon("cut");
    cut.shortcut = QKeySequence::Cut;
    cmds.append(cut);
    
    Command copy;
    copy.id = "edit.copy";
    copy.name = "Copy";
    copy.description = "Copy the selected text";
    copy.category = "Edit";
    copy.icon = ThemeManager::instance().themedIcon("copy");
    copy.shortcut = QKeySequence::Copy;
    cmds.append(copy);
    
    Command paste;
    paste.id = "edit.paste";
    paste.name = "Paste";
    paste.description = "Paste from clipboard";
    paste.category = "Edit";
    paste.icon = ThemeManager::instance().themedIcon("paste");
    paste.shortcut = QKeySequence::Paste;
    cmds.append(paste);
    
    Command find;
    find.id = "edit.find";
    find.name = "Find";
    find.description = "Find text in the current document";
    find.category = "Edit";
    find.icon = ThemeManager::instance().themedIcon("search");
    find.shortcut = QKeySequence::Find;
    find.keywords = {"search", "locate"};
    cmds.append(find);
    
    Command replace;
    replace.id = "edit.replace";
    replace.name = "Replace";
    replace.description = "Find and replace text";
    replace.category = "Edit";
    replace.icon = ThemeManager::instance().themedIcon("replace");
    replace.shortcut = QKeySequence::Replace;
    cmds.append(replace);
    
    return cmds;
}

QList<Command> ViewCommandProvider::commands() const {
    QList<Command> cmds;
    
    Command toggleTheme;
    toggleTheme.id = "view.toggleTheme";
    toggleTheme.name = "Toggle Theme";
    toggleTheme.description = "Switch between dark and light theme";
    toggleTheme.category = "View";
    toggleTheme.icon = ThemeManager::instance().themedIcon("theme");
    toggleTheme.keywords = {"dark", "light", "mode"};
    toggleTheme.action = []() {
        auto& theme = ThemeManager::instance();
        theme.setTheme(theme.currentTheme() == ThemeManager::Theme::Dark ?
                      ThemeManager::Theme::Light : ThemeManager::Theme::Dark);
    };
    cmds.append(toggleTheme);
    
    Command zoomIn;
    zoomIn.id = "view.zoomIn";
    zoomIn.name = "Zoom In";
    zoomIn.description = "Increase the zoom level";
    zoomIn.category = "View";
    zoomIn.icon = ThemeManager::instance().themedIcon("zoom-in");
    zoomIn.shortcut = QKeySequence::ZoomIn;
    cmds.append(zoomIn);
    
    Command zoomOut;
    zoomOut.id = "view.zoomOut";
    zoomOut.name = "Zoom Out";
    zoomOut.description = "Decrease the zoom level";
    zoomOut.category = "View";
    zoomOut.icon = ThemeManager::instance().themedIcon("zoom-out");
    zoomOut.shortcut = QKeySequence::ZoomOut;
    cmds.append(zoomOut);
    
    Command resetZoom;
    resetZoom.id = "view.resetZoom";
    resetZoom.name = "Reset Zoom";
    resetZoom.description = "Reset zoom to 100%";
    resetZoom.category = "View";
    resetZoom.icon = ThemeManager::instance().themedIcon("zoom-reset");
    resetZoom.shortcut = QKeySequence("Ctrl+0");
    cmds.append(resetZoom);
    
    Command fullScreen;
    fullScreen.id = "view.fullScreen";
    fullScreen.name = "Toggle Full Screen";
    fullScreen.description = "Enter or exit full screen mode";
    fullScreen.category = "View";
    fullScreen.icon = ThemeManager::instance().themedIcon("fullscreen");
    fullScreen.shortcut = QKeySequence::FullScreen;
    cmds.append(fullScreen);
    
    return cmds;
}

QList<Command> ToolsCommandProvider::commands() const {
    QList<Command> cmds;
    
    Command settings;
    settings.id = "tools.settings";
    settings.name = "Settings";
    settings.description = "Open application settings";
    settings.category = "Tools";
    settings.icon = ThemeManager::instance().themedIcon("settings");
    settings.shortcut = QKeySequence::Preferences;
    settings.keywords = {"preferences", "options", "configure"};
    cmds.append(settings);
    
    Command showCommandPalette;
    showCommandPalette.id = "tools.commandPalette";
    showCommandPalette.name = "Command Palette";
    showCommandPalette.description = "Show the command palette";
    showCommandPalette.category = "Tools";
    showCommandPalette.icon = ThemeManager::instance().themedIcon("command");
    showCommandPalette.shortcut = QKeySequence("Ctrl+Shift+P");
    showCommandPalette.closeOnExecute = false;
    cmds.append(showCommandPalette);
    
    return cmds;
}

QList<Command> HelpCommandProvider::commands() const {
    QList<Command> cmds;
    
    Command documentation;
    documentation.id = "help.documentation";
    documentation.name = "Documentation";
    documentation.description = "Open the documentation";
    documentation.category = "Help";
    documentation.icon = ThemeManager::instance().themedIcon("help");
    documentation.shortcut = QKeySequence::HelpContents;
    cmds.append(documentation);
    
    Command about;
    about.id = "help.about";
    about.name = "About";
    about.description = "Show information about this application";
    about.category = "Help";
    about.icon = ThemeManager::instance().themedIcon("info");
    cmds.append(about);
    
    Command shortcuts;
    shortcuts.id = "help.shortcuts";
    shortcuts.name = "Keyboard Shortcuts";
    shortcuts.description = "Show all keyboard shortcuts";
    shortcuts.category = "Help";
    shortcuts.icon = ThemeManager::instance().themedIcon("keyboard");
    shortcuts.keywords = {"keys", "hotkeys", "keybindings"};
    cmds.append(shortcuts);
    
    return cmds;
}

// QuickCommandButton implementation

QuickCommandButton::QuickCommandButton(QWidget* parent)
    : BaseStyledWidget(parent) {
    
    setFixedSize(48, 48);
    setShadowEnabled(true);
    setBorderRadius(24);
    setHoverEnabled(true);
    setCursor(Qt::PointingHandCursor);
    
    // Setup auto-hide timer
    autoHideTimer_ = new QTimer(this);
    autoHideTimer_->setSingleShot(true);
    autoHideTimer_->setInterval(3000);
    connect(autoHideTimer_, &QTimer::timeout, [this]() {
        if (!isHovered_) {
            hide();
        }
    });
    
    updatePosition();
}

void QuickCommandButton::setShortcut(const QKeySequence& shortcut) {
    shortcut_ = shortcut;
    
    if (shortcutObj_) {
        delete shortcutObj_;
    }
    
    if (!shortcut.isEmpty()) {
        shortcutObj_ = new QShortcut(shortcut, parentWidget());
        connect(shortcutObj_, &QShortcut::activated, this, &QuickCommandButton::triggered);
    }
}

void QuickCommandButton::paintContent(QPainter* painter) {
    const auto& colors = ThemeManager::instance().colors();
    
    // Draw icon
    QIcon icon = ThemeManager::instance().themedIcon("command");
    QRect iconRect = rect().adjusted(12, 12, -12, -12);
    icon.paint(painter, iconRect);
    
    // Draw shortcut hint when hovered
    if (isHovered_ && !shortcut_.isEmpty()) {
        painter->setPen(colors.textSecondary);
        painter->setFont(ThemeManager::instance().typography().caption);
        QString hint = shortcut_.toString(QKeySequence::NativeText);
        painter->drawText(rect(), Qt::AlignCenter | Qt::AlignBottom, hint);
    }
}

void QuickCommandButton::enterEvent(QEvent* event) {
    BaseStyledWidget::enterEvent(event);
    isHovered_ = true;
    
    if (autoHide_) {
        autoHideTimer_->stop();
    }
    
    update();
}

void QuickCommandButton::leaveEvent(QEvent* event) {
    BaseStyledWidget::leaveEvent(event);
    isHovered_ = false;
    
    if (autoHide_) {
        startAutoHideTimer();
    }
    
    update();
}

void QuickCommandButton::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit triggered();
    }
    BaseStyledWidget::mousePressEvent(event);
}

void QuickCommandButton::resizeEvent(QResizeEvent* event) {
    BaseStyledWidget::resizeEvent(event);
    updatePosition();
}

void QuickCommandButton::updatePosition() {
    if (!parentWidget()) return;
    
    QRect parentRect = parentWidget()->rect();
    QPoint pos;
    
    switch (position_) {
        case TopLeft:
            pos = QPoint(20, 20);
            break;
        case TopRight:
            pos = QPoint(parentRect.width() - width() - 20, 20);
            break;
        case BottomLeft:
            pos = QPoint(20, parentRect.height() - height() - 20);
            break;
        case BottomRight:
            pos = QPoint(parentRect.width() - width() - 20,
                        parentRect.height() - height() - 20);
            break;
    }
    
    move(pos);
}

void QuickCommandButton::startAutoHideTimer() {
    if (autoHide_ && !isHovered_) {
        autoHideTimer_->start();
    }
}

} // namespace llm_re::ui_v2

// Register Command as Qt metatype
Q_DECLARE_METATYPE(llm_re::ui_v2::Command)