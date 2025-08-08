#include "memory_dock.h"
#include "../core/theme_manager.h"
#include "../core/ui_constants.h"
#include "../core/ui_utils.h"
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QRegularExpression>

namespace llm_re::ui_v2 {

// ========== MemoryEntryViewer Implementation ==========

MemoryEntryViewer::MemoryEntryViewer(const MemoryEntry& entry, QWidget* parent)
    : QDialog(parent), entry_(entry) {
    setupUI();
    
    // Set window properties
    setWindowTitle(tr("Memory Analysis Entry"));
    setModal(true);
    resize(900, 600);
    
    // Center on parent
    if (parent) {
        move(parent->window()->frameGeometry().center() - rect().center());
    }
}

void MemoryEntryViewer::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);
    mainLayout->setAlignment(Qt::AlignTop);
    
    // Simple header - title, address, timestamp on one line
    auto* headerLayout = new QHBoxLayout();
    
    titleLabel_ = new QLabel(entry_.title.isEmpty() ? tr("(no title)") : entry_.title, this);
    QFont titleFont = titleLabel_->font();
    titleFont.setWeight(QFont::DemiBold);
    titleLabel_->setFont(titleFont);
    headerLayout->addWidget(titleLabel_);
    
    if (!entry_.address.isEmpty()) {
        headerLayout->addWidget(new QLabel(entry_.address, this));
    }
    
    headerLayout->addWidget(new QLabel(entry_.timestamp.toString("yyyy-MM-dd hh:mm"), this));
    headerLayout->addStretch();
    
    mainLayout->addLayout(headerLayout);
    
    // Splitter for two panels
    splitter_ = new QSplitter(Qt::Horizontal, this);
    splitter_->setChildrenCollapsible(false);
    
    // Left panel - Analysis view
    auto* leftPanel = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 4, 0);
    leftLayout->setSpacing(4);
    
    auto* mdHeaderLayout = new QHBoxLayout();
    auto* mdLabel = new QLabel(tr("Analysis"), this);
    QFont sectionFont = mdLabel->font();
    sectionFont.setWeight(QFont::DemiBold);
    mdLabel->setFont(sectionFont);
    mdHeaderLayout->addWidget(mdLabel);
    mdHeaderLayout->addStretch();
    
    copyMarkdownBtn_ = new QPushButton(tr("Copy"), this);
    copyMarkdownBtn_->setMaximumHeight(24);
    mdHeaderLayout->addWidget(copyMarkdownBtn_);
    
    leftLayout->addLayout(mdHeaderLayout);
    
    markdownView_ = new QTextBrowser(this);
    markdownView_->setOpenExternalLinks(false);
    markdownView_->setPlainText(entry_.analysis);
    leftLayout->addWidget(markdownView_);
    
    // Right panel - JSON view
    auto* rightPanel = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(4, 0, 0, 0);
    rightLayout->setSpacing(4);
    
    auto* jsonHeaderLayout = new QHBoxLayout();
    auto* jsonLabel = new QLabel(tr("JSON"), this);
    jsonLabel->setFont(sectionFont);
    jsonHeaderLayout->addWidget(jsonLabel);
    jsonHeaderLayout->addStretch();
    
    copyJsonBtn_ = new QPushButton(tr("Copy"), this);
    copyJsonBtn_->setMaximumHeight(24);
    jsonHeaderLayout->addWidget(copyJsonBtn_);
    
    rightLayout->addLayout(jsonHeaderLayout);
    
    jsonView_ = new QTextEdit(this);
    jsonView_->setReadOnly(true);
    jsonView_->setPlainText(formatJson(entry_.toJson()));
    jsonView_->setFont(QFont("Menlo, Consolas, Monaco, monospace", 11));
    rightLayout->addWidget(jsonView_);
    
    splitter_->addWidget(leftPanel);
    splitter_->addWidget(rightPanel);
    splitter_->setSizes({600, 600});
    
    mainLayout->addWidget(splitter_, 1);  // Stretch factor 1 to fill available space
    
    // Button bar at bottom
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeBtn);
    
    mainLayout->addLayout(buttonLayout);
    
    // Connect copy buttons
    connect(copyMarkdownBtn_, &QPushButton::clicked, this, &MemoryEntryViewer::onCopyMarkdown);
    connect(copyJsonBtn_, &QPushButton::clicked, this, &MemoryEntryViewer::onCopyJson);
}

QString MemoryEntryViewer::renderMarkdown(const QString& text) {
    // For now, just display as plain text with basic formatting
    // QTextBrowser will handle basic markdown
    return text;
}

QString MemoryEntryViewer::formatJson(const QJsonObject& obj) {
    QJsonDocument doc(obj);
    return doc.toJson(QJsonDocument::Indented);
}

void MemoryEntryViewer::onCopyMarkdown() {
    QApplication::clipboard()->setText(entry_.analysis);
    
    // Visual feedback
    copyMarkdownBtn_->setText(tr("Copied!"));
    QTimer::singleShot(1000, [this]() {
        copyMarkdownBtn_->setText(tr("Copy"));
    });
}

void MemoryEntryViewer::onCopyJson() {
    QJsonDocument doc(entry_.toJson());
    QApplication::clipboard()->setText(doc.toJson(QJsonDocument::Indented));
    
    // Visual feedback
    copyJsonBtn_->setText(tr("Copied!"));
    QTimer::singleShot(1000, [this]() {
        copyJsonBtn_->setText(tr("Copy"));
    });
}

// ========== MemoryTableModel Implementation ==========

MemoryTableModel::MemoryTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int MemoryTableModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return isFiltered_ ? filteredEntries_.size() : entries_.size();
}

int MemoryTableModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return ColumnCount;
}

QVariant MemoryTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount())
        return QVariant();
    
    const MemoryEntry& entry = isFiltered_ ? 
        filteredEntries_.at(index.row()) : entries_.at(index.row());
    
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case AddressColumn:
            return entry.address.isEmpty() ? "-" : entry.address;
        case FunctionColumn:
            return entry.title.isEmpty() ? "Analysis Entry" : entry.title;
        case TimestampColumn:
            return entry.timestamp.toString("yyyy-MM-dd hh:mm");
        }
    } else if (role == Qt::ToolTipRole) {
        // Show first few lines of analysis as tooltip
        QString preview = entry.analysis.left(200);
        if (entry.analysis.length() > 200) {
            preview += "...";
        }
        return preview;
    } else if (role == Qt::UserRole) {
        // Store the entry ID for easy retrieval
        return entry.id;
    } else if (role == Qt::TextAlignmentRole) {
        if (index.column() == TimestampColumn) {
            return Qt::AlignCenter;
        }
    }
    
    return QVariant();
}

QVariant MemoryTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QVariant();
    
    switch (section) {
    case AddressColumn:
        return tr("Address");
    case FunctionColumn:
        return tr("Title");
    case TimestampColumn:
        return tr("Timestamp");
    }
    
    return QVariant();
}

Qt::ItemFlags MemoryTableModel::flags(const QModelIndex& index) const {
    if (!index.isValid())
        return Qt::NoItemFlags;
    
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void MemoryTableModel::addEntry(const MemoryEntry& entry) {
    if (isFiltered_) {
        // Add to main list
        entries_.append(entry);
        
        // Check if it matches filter
        if (matchesFilter(entry)) {
            beginInsertRows(QModelIndex(), filteredEntries_.size(), filteredEntries_.size());
            filteredEntries_.append(entry);
            endInsertRows();
        }
    } else {
        beginInsertRows(QModelIndex(), entries_.size(), entries_.size());
        entries_.append(entry);
        endInsertRows();
    }
    
    emit entryAdded(entry.id);
}

void MemoryTableModel::updateEntry(int row, const MemoryEntry& entry) {
    if (row < 0 || row >= rowCount())
        return;
    
    if (isFiltered_) {
        filteredEntries_[row] = entry;
        // Update in main list
        for (MemoryEntry& e : entries_) {
            if (e.id == entry.id) {
                e = entry;
                break;
            }
        }
    } else {
        entries_[row] = entry;
    }
    
    emit dataChanged(index(row, 0), index(row, ColumnCount - 1));
    emit entryUpdated(entry.id);
}

void MemoryTableModel::removeEntry(int row) {
    if (row < 0 || row >= rowCount())
        return;
    
    QUuid id;
    
    if (isFiltered_) {
        id = filteredEntries_.at(row).id;
        
        beginRemoveRows(QModelIndex(), row, row);
        filteredEntries_.removeAt(row);
        endRemoveRows();
        
        // Remove from main list
        for (int i = 0; i < entries_.size(); ++i) {
            if (entries_[i].id == id) {
                entries_.removeAt(i);
                break;
            }
        }
    } else {
        id = entries_.at(row).id;
        
        beginRemoveRows(QModelIndex(), row, row);
        entries_.removeAt(row);
        endRemoveRows();
    }
    
    emit entryRemoved(id);
}

void MemoryTableModel::removeEntry(const QUuid& id) {
    int row = findEntry(id);
    if (row >= 0) {
        removeEntry(row);
    }
}

void MemoryTableModel::clearEntries() {
    beginResetModel();
    entries_.clear();
    filteredEntries_.clear();
    endResetModel();
}

MemoryEntry MemoryTableModel::entry(int row) const {
    if (row < 0 || row >= rowCount())
        return MemoryEntry();
    
    return isFiltered_ ? filteredEntries_.at(row) : entries_.at(row);
}

MemoryEntry MemoryTableModel::entry(const QUuid& id) const {
    for (const MemoryEntry& e : entries_) {
        if (e.id == id)
            return e;
    }
    return MemoryEntry();
}

int MemoryTableModel::findEntry(const QUuid& id) const {
    const QList<MemoryEntry>& list = isFiltered_ ? filteredEntries_ : entries_;
    
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).id == id)
            return i;
    }
    return -1;
}

void MemoryTableModel::setSearchFilter(const QString& text) {
    searchText_ = text;
    applyFilters();
}

void MemoryTableModel::clearFilters() {
    searchText_.clear();
    applyFilters();
}

void MemoryTableModel::applyFilters() {
    beginResetModel();
    
    if (searchText_.isEmpty()) {
        isFiltered_ = false;
        filteredEntries_.clear();
    } else {
        isFiltered_ = true;
        filteredEntries_.clear();
        
        for (const MemoryEntry& entry : entries_) {
            if (matchesFilter(entry)) {
                filteredEntries_.append(entry);
            }
        }
    }
    
    endResetModel();
}

bool MemoryTableModel::matchesFilter(const MemoryEntry& entry) const {
    if (!searchText_.isEmpty()) {
        QString searchLower = searchText_.toLower();
        return entry.address.toLower().contains(searchLower) ||
               entry.title.toLower().contains(searchLower) ||
               entry.analysis.toLower().contains(searchLower);
    }
    return true;
}

// ========== MemoryDock Implementation ==========

MemoryDock::MemoryDock(QWidget* parent)
    : BaseStyledWidget(parent) {
    setupUI();
    createActions();
    connectSignals();
}

MemoryDock::~MemoryDock() {
    // Cleanup handled by Qt parent-child relationships
}

void MemoryDock::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    // Toolbar
    auto* toolbar = new QWidget(this);
    toolbar->setObjectName("MemoryToolbar");
    auto* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);
    
    // Search box
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(tr("Search memory entries..."));
    searchEdit_->setClearButtonEnabled(true);
    toolbarLayout->addWidget(searchEdit_);
    
    // Import button
    auto* importBtn = new QPushButton(tr("Import"), this);
    importBtn->setIcon(ThemeManager::instance().themedIcon("document-import"));
    toolbarLayout->addWidget(importBtn);
    
    // Clear button
    auto* clearBtn = new QPushButton(tr("Clear"), this);
    clearBtn->setIcon(ThemeManager::instance().themedIcon("edit-clear"));
    toolbarLayout->addWidget(clearBtn);
    
    toolbarLayout->addStretch();
    
    layout->addWidget(toolbar);
    
    // Table view
    model_ = new MemoryTableModel(this);
    
    tableView_ = new QTableView(this);
    tableView_->setModel(model_);
    tableView_->setAlternatingRowColors(true);
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    tableView_->setContextMenuPolicy(Qt::CustomContextMenu);
    tableView_->setSortingEnabled(false);
    tableView_->horizontalHeader()->setStretchLastSection(true);
    
    // Set column widths
    tableView_->setColumnWidth(MemoryTableModel::AddressColumn, 150);
    tableView_->setColumnWidth(MemoryTableModel::FunctionColumn, 350);
    tableView_->setColumnWidth(MemoryTableModel::TimestampColumn, 150);
    
    // Make function column stretch
    tableView_->horizontalHeader()->setSectionResizeMode(
        MemoryTableModel::FunctionColumn, QHeaderView::Stretch);
    
    
    layout->addWidget(tableView_);
    
    // Status bar
    statusLabel_ = new QLabel(this);
    statusLabel_->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(statusLabel_);
    
    updateStatusText();
    
    // Connect button signals
    connect(importBtn, &QPushButton::clicked, this, &MemoryDock::onImportClicked);
    connect(clearBtn, &QPushButton::clicked, this, &MemoryDock::onClearClicked);
}

void MemoryDock::createActions() {
    // Context menu actions
    viewAction_ = new QAction(tr("View Entry"), this);
    viewAction_->setIcon(ThemeManager::instance().themedIcon("document-open"));
    
    copyAddressAction_ = new QAction(tr("Copy Address"), this);
    copyAddressAction_->setIcon(ThemeManager::instance().themedIcon("edit-copy"));
    
    deleteAction_ = new QAction(tr("Delete Entry"), this);
    deleteAction_->setIcon(ThemeManager::instance().themedIcon("edit-delete"));
    deleteAction_->setShortcut(QKeySequence::Delete);
    
    // Create context menu
    contextMenu_ = new QMenu(this);
    contextMenu_->addAction(viewAction_);
    contextMenu_->addSeparator();
    contextMenu_->addAction(copyAddressAction_);
    contextMenu_->addSeparator();
    contextMenu_->addAction(deleteAction_);
}

void MemoryDock::connectSignals() {
    // Search
    connect(searchEdit_, &QLineEdit::textChanged,
            this, &MemoryDock::onSearchTextChanged);
    
    // Table
    connect(tableView_, &QTableView::doubleClicked,
            this, &MemoryDock::onTableDoubleClicked);
    
    connect(tableView_, &QWidget::customContextMenuRequested,
            this, &MemoryDock::onContextMenuRequested);
    
    // Model
    connect(model_, &MemoryTableModel::entryAdded,
            [this]() { updateStatusText(); });
    
    connect(model_, &MemoryTableModel::entryRemoved,
            [this]() { updateStatusText(); });
    
    // Context menu actions
    connect(viewAction_, &QAction::triggered,
            this, &MemoryDock::onViewEntry);
    
    connect(copyAddressAction_, &QAction::triggered,
            this, &MemoryDock::onCopyAddress);
    
    connect(deleteAction_, &QAction::triggered,
            this, &MemoryDock::onDeleteEntry);
}

void MemoryDock::addEntry(const MemoryEntry& entry) {
    model_->addEntry(entry);
}

void MemoryDock::updateEntry(const QUuid& id, const MemoryEntry& entry) {
    int row = model_->findEntry(id);
    if (row >= 0) {
        model_->updateEntry(row, entry);
    }
}

void MemoryDock::removeEntry(const QUuid& id) {
    model_->removeEntry(id);
}

void MemoryDock::clearEntries() {
    if (model_->rowCount() > 0) {
        auto reply = QMessageBox::question(
            this, tr("Clear Entries"),
            tr("Clear all %1 memory entries?").arg(model_->rowCount()),
            QMessageBox::Yes | QMessageBox::No);
        
        if (reply == QMessageBox::Yes) {
            model_->clearEntries();
        }
    }
}

QList<MemoryEntry> MemoryDock::entries() const {
    return model_->entries();
}

MemoryEntry MemoryDock::entry(const QUuid& id) const {
    return model_->entry(id);
}

void MemoryDock::importFromLLMRESession(const QString& path) {
    QString fileName = path;
    if (fileName.isEmpty()) {
        fileName = QFileDialog::getOpenFileName(
            this, tr("Import Session"),
            "",
            tr("LLMRE Session Files (*.llmre);;All Files (*)"));
    }
    
    if (fileName.isEmpty())
        return;
    
    QFile file(fileName);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Import Failed"),
                           tr("Could not open file for reading."));
        return;
    }
    
    QTextStream stream(&file);
    QString content = stream.readAll();
    file.close();
    
    QJsonDocument doc = QJsonDocument::fromJson(content.toUtf8());
    if (!doc.isObject()) {
        QMessageBox::warning(this, tr("Import Failed"),
                           tr("Invalid session file format."));
        return;
    }
    
    QJsonObject session = doc.object();
    
    // Check IDB path
    QString sessionIdbPath;
    if (session.contains("metadata")) {
        sessionIdbPath = session["metadata"].toObject()["idbPath"].toString();
    }
    
    QString currentIdbPath = QString::fromStdString(get_path(PATH_TYPE_IDB));
    
    if (!sessionIdbPath.isEmpty() && sessionIdbPath != currentIdbPath) {
        int ret = QMessageBox::warning(this, tr("IDB Mismatch"),
            tr("This session is from a different IDA database:\n%1\n\nImport anyway?")
            .arg(sessionIdbPath),
            QMessageBox::Yes | QMessageBox::No);
        
        if (ret != QMessageBox::Yes) {
            return;
        }
    }
    
    // Extract memory entries from UI state
    int importedCount = 0;
    
    if (session.contains("ui")) {
        QJsonObject ui = session["ui"].toObject();
        if (ui.contains("docks")) {
            QJsonObject docks = ui["docks"].toObject();
            if (docks.contains("memory")) {
                QJsonObject memoryState = docks["memory"].toObject();
                if (memoryState.contains("entries")) {
                    QJsonArray entriesArray = memoryState["entries"].toArray();
                    
                    for (const QJsonValue& val : entriesArray) {
                        QJsonObject obj = val.toObject();
                        
                        MemoryEntry entry;
                        
                        // Try to use existing ID or generate new one
                        QString idStr = obj["id"].toString();
                        entry.id = idStr.isEmpty() ? QUuid::createUuid() : QUuid(idStr);
                        
                        // Extract address
                        entry.address = obj["address"].toString();
                        
                        // Extract title field (which is the key/identifier)
                        entry.title = obj["title"].toString();
                        
                        // Extract content field
                        entry.analysis = obj["content"].toString();
                        
                        // Extract timestamp
                        QString timestampStr = obj["timestamp"].toString();
                        entry.timestamp = QDateTime::fromString(timestampStr, Qt::ISODate);
                        if (!entry.timestamp.isValid()) {
                            entry.timestamp = QDateTime::currentDateTime();
                        }
                        
                        // Add entry if it has some content
                        if (!entry.address.isEmpty() || !entry.title.isEmpty() || !entry.analysis.isEmpty()) {
                            addEntry(entry);
                            importedCount++;
                        }
                    }
                }
            }
        }
    }
    
    if (importedCount == 0) {
        QMessageBox::warning(this, tr("Import Failed"),
                           tr("No memory entries found in session file."));
    }
}

QJsonObject MemoryDock::exportState() const {
    QJsonObject state;
    
    // Export all entries using the same toJson() format
    QJsonArray entriesArray;
    for (const MemoryEntry& entry : entries()) {
        entriesArray.append(entry.toJson());
    }
    state["entries"] = entriesArray;
    
    // Export current search
    state["searchText"] = searchEdit_->text();
    
    return state;
}

void MemoryDock::importState(const QJsonObject& state) {
    // Clear existing entries
    model_->clearEntries();
    
    // Import entries
    if (state.contains("entries")) {
        QJsonArray entriesArray = state["entries"].toArray();
        for (const QJsonValue& val : entriesArray) {
            QJsonObject obj = val.toObject();
            
            MemoryEntry entry;
            
            QString idStr = obj["id"].toString();
            entry.id = idStr.isEmpty() ? QUuid::createUuid() : QUuid(idStr);
            
            entry.address = obj["address"].toString();
            
            // Extract title field
            entry.title = obj["title"].toString();
            
            // Try content field first (actual format), fallback to analysis
            entry.analysis = obj["content"].toString();
            if (entry.analysis.isEmpty()) {
                entry.analysis = obj["analysis"].toString();
            }
            
            QString timestampStr = obj["timestamp"].toString();
            entry.timestamp = QDateTime::fromString(timestampStr, Qt::ISODate);
            if (!entry.timestamp.isValid()) {
                entry.timestamp = QDateTime::currentDateTime();
            }
            
            addEntry(entry);
        }
    }
    
    // Restore search text
    if (state.contains("searchText")) {
        searchEdit_->setText(state["searchText"].toString());
    }
}

void MemoryDock::onThemeChanged() {
    // Update icons if needed
    updateStatusText();
}

void MemoryDock::onSearchTextChanged(const QString& text) {
    model_->setSearchFilter(text);
    updateStatusText();
}

void MemoryDock::onImportClicked() {
    importFromLLMRESession();
}

void MemoryDock::onClearClicked() {
    clearEntries();
}

void MemoryDock::onTableDoubleClicked(const QModelIndex& index) {
    if (index.isValid()) {
        QUuid id = index.data(Qt::UserRole).toUuid();
        showEntryViewer(id);
    }
}

void MemoryDock::onContextMenuRequested(const QPoint& pos) {
    QModelIndex index = tableView_->indexAt(pos);
    if (index.isValid()) {
        selectedEntryId_ = index.data(Qt::UserRole).toUuid();
        
        // Enable/disable actions based on entry
        MemoryEntry entry = model_->entry(selectedEntryId_);
        copyAddressAction_->setEnabled(!entry.address.isEmpty());
        
        contextMenu_->exec(tableView_->mapToGlobal(pos));
    }
}

void MemoryDock::onCopyAddress() {
    if (!selectedEntryId_.isNull()) {
        MemoryEntry entry = model_->entry(selectedEntryId_);
        if (!entry.address.isEmpty()) {
            QApplication::clipboard()->setText(entry.address);
        }
    }
}

void MemoryDock::onDeleteEntry() {
    if (!selectedEntryId_.isNull()) {
        model_->removeEntry(selectedEntryId_);
        selectedEntryId_ = QUuid();
    }
}

void MemoryDock::onViewEntry() {
    if (!selectedEntryId_.isNull()) {
        showEntryViewer(selectedEntryId_);
    }
}

void MemoryDock::showEntryViewer(const QUuid& id) {
    MemoryEntry entry = model_->entry(id);
    if (!entry.id.isNull()) {
        auto* viewer = new MemoryEntryViewer(entry, this);
        viewer->setAttribute(Qt::WA_DeleteOnClose);
        viewer->show();
        
        // Also emit signal for navigation if address exists
        if (!entry.address.isEmpty()) {
            emit navigateToAddress(entry.address);
        }
        emit entryDoubleClicked(id);
    }
}

void MemoryDock::updateStatusText() {
    int total = model_->entries().size();
    int shown = model_->rowCount();
    
    if (total == shown) {
        statusLabel_->setText(tr("%1 entries").arg(total));
    } else {
        statusLabel_->setText(tr("Showing %1 of %2 entries").arg(shown).arg(total));
    }
}

} // namespace llm_re::ui_v2