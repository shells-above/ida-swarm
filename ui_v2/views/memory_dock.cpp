#include "memory_dock.h"
#include "../core/theme_manager.h"
#include "../core/ui_constants.h"
#include "../core/ui_utils.h"
#include <QGraphicsDropShadowEffect>
#include <QPainter>
#include <QRegularExpression>

namespace llm_re::ui_v2 {

// ========== MemoryEntryViewer Implementation ==========

MemoryEntryViewer::MemoryEntryViewer(const llm_re::AnalysisEntry& entry, QWidget* parent)
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
    
    QString title = QString::fromStdString(entry_.key);
    titleLabel_ = new QLabel(title.isEmpty() ? tr("(no title)") : title, this);
    QFont titleFont = titleLabel_->font();
    titleFont.setWeight(QFont::DemiBold);
    titleLabel_->setFont(titleFont);
    headerLayout->addWidget(titleLabel_);
    
    if (entry_.address) {
        QString addressStr = QString("0x%1").arg(*entry_.address, 0, 16);
        headerLayout->addWidget(new QLabel(addressStr, this));
    }
    
    QDateTime timestamp = QDateTime::fromSecsSinceEpoch(entry_.timestamp);
    headerLayout->addWidget(new QLabel(timestamp.toString("yyyy-MM-dd hh:mm"), this));
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
    markdownView_->setPlainText(QString::fromStdString(entry_.content));
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
    jsonView_->setPlainText(formatJson(entryToJson()));
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

QJsonObject MemoryEntryViewer::entryToJson() const {
    QJsonObject obj;
    obj["key"] = QString::fromStdString(entry_.key);
    obj["content"] = QString::fromStdString(entry_.content);
    obj["type"] = QString::fromStdString(entry_.type);
    if (entry_.address) {
        obj["address"] = QString("0x%1").arg(*entry_.address, 0, 16);
    }
    obj["timestamp"] = QDateTime::fromSecsSinceEpoch(entry_.timestamp).toString(Qt::ISODate);
    return obj;
}

void MemoryEntryViewer::onCopyMarkdown() {
    QApplication::clipboard()->setText(QString::fromStdString(entry_.content));
    
    // Visual feedback
    copyMarkdownBtn_->setText(tr("Copied!"));
    QTimer::singleShot(1000, [this]() {
        copyMarkdownBtn_->setText(tr("Copy"));
    });
}

void MemoryEntryViewer::onCopyJson() {
    QJsonDocument doc(entryToJson());
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

void MemoryTableModel::setMemory(std::shared_ptr<llm_re::BinaryMemory> memory) {
    beginResetModel();
    memory_ = memory;
    refresh();
    endResetModel();
}

int MemoryTableModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return isFiltered_ ? filteredKeys_.size() : keys_.size();
}

int MemoryTableModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return ColumnCount;
}

QVariant MemoryTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= rowCount() || !memory_)
        return QVariant();
    
    const std::string& key = isFiltered_ ? 
        filteredKeys_.at(index.row()) : keys_.at(index.row());
    
    // Get the entry from memory
    auto analyses = memory_->get_analysis(key);
    if (analyses.empty())
        return QVariant();
    
    const llm_re::AnalysisEntry& entry = analyses[0];
    
    if (role == Qt::DisplayRole || role == Qt::EditRole) {
        switch (index.column()) {
        case AddressColumn:
            return entry.address ? QString("0x%1").arg(*entry.address, 0, 16) : QString("-");
        case FunctionColumn:
            return QString::fromStdString(entry.key);
        case TimestampColumn:
            return QDateTime::fromSecsSinceEpoch(entry.timestamp).toString("yyyy-MM-dd hh:mm");
        }
    } else if (role == Qt::ToolTipRole) {
        // Show first few lines of analysis as tooltip
        QString content = QString::fromStdString(entry.content);
        QString preview = content.left(200);
        if (content.length() > 200) {
            preview += "...";
        }
        return preview;
    } else if (role == Qt::UserRole) {
        // Store the key for easy retrieval
        return QString::fromStdString(key);
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
        return tr("Key/Function");
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

void MemoryTableModel::refresh() {
    if (!memory_) return;
    
    // Get all analyses from memory
    auto allAnalyses = memory_->get_analysis();
    
    // Extract unique keys
    keys_.clear();
    for (const auto& entry : allAnalyses) {
        keys_.push_back(entry.key);
    }
    
    // Apply filters if any
    applyFilters();
    
    emit dataRefreshed();
}

void MemoryTableModel::clearEntries() {
    beginResetModel();
    keys_.clear();
    filteredKeys_.clear();
    endResetModel();
}

llm_re::AnalysisEntry MemoryTableModel::entry(int row) const {
    if (row < 0 || row >= rowCount() || !memory_)
        return llm_re::AnalysisEntry{};
    
    const std::string& key = isFiltered_ ? filteredKeys_.at(row) : keys_.at(row);
    auto analyses = memory_->get_analysis(key);
    return analyses.empty() ? llm_re::AnalysisEntry{} : analyses[0];
}

llm_re::AnalysisEntry MemoryTableModel::entry(const QString& key) const {
    if (!memory_)
        return llm_re::AnalysisEntry{};
    
    auto analyses = memory_->get_analysis(key.toStdString());
    return analyses.empty() ? llm_re::AnalysisEntry{} : analyses[0];
}

std::vector<llm_re::AnalysisEntry> MemoryTableModel::allEntries() const {
    if (!memory_)
        return {};
    
    return memory_->get_analysis();
}

int MemoryTableModel::findEntry(const QString& key) const {
    const std::vector<std::string>& list = isFiltered_ ? filteredKeys_ : keys_;
    std::string keyStr = key.toStdString();
    
    for (size_t i = 0; i < list.size(); ++i) {
        if (list[i] == keyStr)
            return i;
    }
    return -1;
}

QString MemoryTableModel::keyAt(int row) const {
    if (row < 0 || row >= rowCount())
        return QString();
    
    const std::string& key = isFiltered_ ? filteredKeys_.at(row) : keys_.at(row);
    return QString::fromStdString(key);
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
        filteredKeys_.clear();
    } else {
        isFiltered_ = true;
        filteredKeys_.clear();
        
        if (memory_) {
            for (const std::string& key : keys_) {
                auto analyses = memory_->get_analysis(key);
                if (!analyses.empty() && matchesFilter(analyses[0])) {
                    filteredKeys_.push_back(key);
                }
            }
        }
    }
    
    endResetModel();
}

bool MemoryTableModel::matchesFilter(const llm_re::AnalysisEntry& entry) const {
    if (!searchText_.isEmpty()) {
        QString searchLower = searchText_.toLower();
        QString keyStr = QString::fromStdString(entry.key).toLower();
        QString contentStr = QString::fromStdString(entry.content).toLower();
        QString addressStr = entry.address ? 
            QString("0x%1").arg(*entry.address, 0, 16).toLower() : QString();
        
        return keyStr.contains(searchLower) ||
               contentStr.contains(searchLower) ||
               (!addressStr.isEmpty() && addressStr.contains(searchLower));
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
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(Design::SPACING_SM, Design::SPACING_SM, 
                                    Design::SPACING_SM, Design::SPACING_SM);
    mainLayout->setSpacing(Design::SPACING_SM);
    
    // Search bar
    auto* searchLayout = new QHBoxLayout();
    searchLayout->setSpacing(Design::SPACING_SM);
    
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText(tr("Search memory entries..."));
    searchEdit_->setClearButtonEnabled(true);
    searchLayout->addWidget(searchEdit_);
    
    mainLayout->addLayout(searchLayout);
    
    // Table view
    tableView_ = new QTableView(this);
    model_ = new MemoryTableModel(this);
    tableView_->setModel(model_);
    
    // Table appearance
    tableView_->setAlternatingRowColors(true);
    tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableView_->setSelectionMode(QAbstractItemView::SingleSelection);
    tableView_->setSortingEnabled(false);
    tableView_->setWordWrap(false);
    tableView_->horizontalHeader()->setStretchLastSection(true);
    tableView_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    tableView_->verticalHeader()->setVisible(false);
    tableView_->setContextMenuPolicy(Qt::CustomContextMenu);
    
    // Set column widths
    tableView_->setColumnWidth(MemoryTableModel::AddressColumn, 120);
    tableView_->setColumnWidth(MemoryTableModel::FunctionColumn, 300);
    tableView_->setColumnWidth(MemoryTableModel::TimestampColumn, 150);
    
    mainLayout->addWidget(tableView_, 1);
    
    // Status bar
    statusLabel_ = new QLabel(tr("No entries"), this);
    statusLabel_->setProperty("class", "status-text");
    mainLayout->addWidget(statusLabel_);
    
    updateStatusText();
}

void MemoryDock::createActions() {
    importAction_ = new QAction(tr("Import Session..."), this);
    importAction_->setShortcut(QKeySequence("Ctrl+I"));
    
    clearAction_ = new QAction(tr("Clear All"), this);
    clearAction_->setShortcut(QKeySequence("Ctrl+Shift+X"));
    
    copyAddressAction_ = new QAction(tr("Copy Address"), this);
    copyAddressAction_->setShortcut(QKeySequence::Copy);
    
    viewAction_ = new QAction(tr("View Entry"), this);
    viewAction_->setShortcut(Qt::Key_Return);
    
    // Context menu
    contextMenu_ = new QMenu(this);
    contextMenu_->addAction(viewAction_);
    contextMenu_->addSeparator();
    contextMenu_->addAction(copyAddressAction_);
}

void MemoryDock::connectSignals() {
    connect(searchEdit_, &QLineEdit::textChanged,
            this, &MemoryDock::onSearchTextChanged);
    
    connect(tableView_, &QTableView::doubleClicked,
            this, &MemoryDock::onTableDoubleClicked);
    
    connect(tableView_, &QTableView::customContextMenuRequested,
            this, &MemoryDock::onContextMenuRequested);
    
    connect(importAction_, &QAction::triggered,
            this, &MemoryDock::onImportClicked);
    
    connect(clearAction_, &QAction::triggered,
            this, &MemoryDock::onClearClicked);
    
    connect(copyAddressAction_, &QAction::triggered,
            this, &MemoryDock::onCopyAddress);
    
    connect(viewAction_, &QAction::triggered,
            this, &MemoryDock::onViewEntry);
    
    connect(model_, &MemoryTableModel::dataRefreshed,
            this, &MemoryDock::updateStatusText);
}

void MemoryDock::setMemory(std::shared_ptr<llm_re::BinaryMemory> memory) {
    memory_ = memory;
    model_->setMemory(memory);
    updateStatusText();
}

void MemoryDock::refresh() {
    if (model_) {
        model_->refresh();
        updateStatusText();
    }
}

void MemoryDock::clearEntries(bool showConfirmation) {
    // doesn't actually clear BinaryMemory, just the UI view
    if (showConfirmation) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, tr("Clear Memory"),
                                     tr("Clear all memory entries? This cannot be undone."),
                                     QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) {
            return;
        }
    }
    
    model_->clearEntries();
    updateStatusText();
}

std::vector<llm_re::AnalysisEntry> MemoryDock::entries() const {
    return model_->allEntries();
}

llm_re::AnalysisEntry MemoryDock::entry(const QString& key) const {
    return model_->entry(key);
}

void MemoryDock::importFromLLMRESession(const QString& path) {
    // handles actually importing entries into BinaryMemory
    QString filePath = path;
    
    if (filePath.isEmpty()) {
        filePath = QFileDialog::getOpenFileName(this,
            tr("Import LLM RE Session"),
            QString(),
            tr("LLM RE Sessions (*.llmre);;All Files (*)"));
    }
    
    if (!filePath.isEmpty()) {
        QFile file(filePath);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            QJsonDocument doc = QJsonDocument::fromJson(data);
            
            if (doc.isObject()) {
                QJsonObject root = doc.object();
                
                // Import memory snapshot if present
                if (root.contains("memory") && memory_) {
                    QJsonObject memObj = root["memory"].toObject();
                    
                    // Convert QJsonObject to nlohmann::json for import
                    // This is a bit hacky but works for now
                    std::string jsonStr = QJsonDocument(memObj).toJson(QJsonDocument::Compact).toStdString();
                    json snapshot = json::parse(jsonStr);
                    memory_->import_memory_snapshot(snapshot);
                    
                    // Refresh display
                    refresh();
                }
            }
        }
    }
}

QJsonObject MemoryDock::exportState() const {
    QJsonObject state;
    
    // Export all entries using entryToJson format
    QJsonArray entriesArray;
    for (const AnalysisEntry& entry: entries()) {
        QJsonObject obj;
        obj["key"] = QString::fromStdString(entry.key);
        obj["content"] = QString::fromStdString(entry.content);
        obj["type"] = QString::fromStdString(entry.type);
        if (entry.address) {
            obj["address"] = QString("0x%1").arg(*entry.address, 0, 16);
        }
        obj["timestamp"] = QDateTime::fromSecsSinceEpoch(entry.timestamp).toString(Qt::ISODate);
        entriesArray.append(obj);
    }
    state["entries"] = entriesArray;
    
    return state;
}

void MemoryDock::importState(const QJsonObject& state) {
    // Note: This doesn't directly import into BinaryMemory
    // That should be done through the agent/memory interface
    // This is just for UI state restoration if needed
    
    if (state.contains("entries")) {
        refresh();
    }
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
    if (!index.isValid())
        return;
    
    QString key = index.data(Qt::UserRole).toString();
    if (!key.isEmpty()) {
        showEntryViewer(key);
        emit entryDoubleClicked(key);
    }
}

void MemoryDock::onContextMenuRequested(const QPoint& pos) {
    QModelIndex index = tableView_->indexAt(pos);
    if (!index.isValid())
        return;
    
    selectedEntryKey_ = index.data(Qt::UserRole).toString();
    
    if (!selectedEntryKey_.isEmpty()) {
        // Enable/disable actions based on entry
        llm_re::AnalysisEntry entry = model_->entry(selectedEntryKey_);
        copyAddressAction_->setEnabled(entry.address.has_value());
        
        contextMenu_->exec(tableView_->mapToGlobal(pos));
    }
}

void MemoryDock::onCopyAddress() {
    if (!selectedEntryKey_.isEmpty()) {
        llm_re::AnalysisEntry entry = model_->entry(selectedEntryKey_);
        if (entry.address) {
            QString addressStr = QString("0x%1").arg(*entry.address, 0, 16);
            QApplication::clipboard()->setText(addressStr);
        }
    }
}

void MemoryDock::onViewEntry() {
    if (!selectedEntryKey_.isEmpty()) {
        showEntryViewer(selectedEntryKey_);
    }
}

void MemoryDock::showEntryViewer(const QString& key) {
    llm_re::AnalysisEntry entry = model_->entry(key);
    if (!entry.key.empty()) {
        MemoryEntryViewer viewer(entry, this);
        viewer.exec();
    }
}

void MemoryDock::updateStatusText() {
    int count = model_->rowCount();
    if (count == 0) {
        statusLabel_->setText(tr("No entries"));
    } else if (count == 1) {
        statusLabel_->setText(tr("1 entry"));
    } else {
        statusLabel_->setText(tr("%1 entries").arg(count));
    }
}

} // namespace llm_re::ui_v2