#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"

namespace llm_re::ui_v2 {

// Simplified memory entry for analysis
struct MemoryEntry {
    QUuid id;
    QString address;
    QString title;  // The key/identifier for this analysis
    QString analysis;
    QDateTime timestamp;
    
    // Constructor
    MemoryEntry() : id(QUuid::createUuid()), timestamp(QDateTime::currentDateTime()) {}
    
    // Equality operator for QList operations
    bool operator==(const MemoryEntry& other) const {
        return id == other.id;
    }
    
    // Convert to JSON for display (matches session file format)
    QJsonObject toJson() const {
        QJsonObject obj;
        obj["address"] = address;
        obj["content"] = analysis;
        obj["timestamp"] = timestamp.toString(Qt::ISODate);
        obj["title"] = title;
        return obj;
    }
};

// Beautiful dialog for viewing memory entries
class MemoryEntryViewer : public QDialog {
    Q_OBJECT
    
public:
    explicit MemoryEntryViewer(const MemoryEntry& entry, QWidget* parent = nullptr);
    
private:
    void setupUI();
    QString renderMarkdown(const QString& text);
    QString formatJson(const QJsonObject& obj);
    
    MemoryEntry entry_;
    
    // UI elements
    QTextBrowser* markdownView_ = nullptr;
    QTextEdit* jsonView_ = nullptr;
    QPushButton* copyMarkdownBtn_ = nullptr;
    QPushButton* copyJsonBtn_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* addressLabel_ = nullptr;
    QLabel* timestampLabel_ = nullptr;
    QSplitter* splitter_ = nullptr;
    
private slots:
    void onCopyMarkdown();
    void onCopyJson();
};

// Simple table model for memory entries
class MemoryTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        AddressColumn,
        FunctionColumn,
        TimestampColumn,
        ColumnCount
    };

    explicit MemoryTableModel(QObject* parent = nullptr);
    
    // QAbstractTableModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    
    // Data management
    void addEntry(const MemoryEntry& entry);
    void updateEntry(int row, const MemoryEntry& entry);
    void removeEntry(int row);
    void removeEntry(const QUuid& id);
    void clearEntries();
    
    MemoryEntry entry(int row) const;
    MemoryEntry entry(const QUuid& id) const;
    QList<MemoryEntry> entries() const { return entries_; }
    int findEntry(const QUuid& id) const;
    
    // Search/Filter
    void setSearchFilter(const QString& text);
    void clearFilters();
    
signals:
    void entryAdded(const QUuid& id);
    void entryUpdated(const QUuid& id);
    void entryRemoved(const QUuid& id);
    
private:
    QList<MemoryEntry> entries_;
    QList<MemoryEntry> filteredEntries_;
    QString searchText_;
    bool isFiltered_ = false;
    
    void applyFilters();
    bool matchesFilter(const MemoryEntry& entry) const;
};

// Main memory dock widget - simplified
class MemoryDock : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit MemoryDock(QWidget* parent = nullptr);
    ~MemoryDock() override;
    
    // Data management
    void addEntry(const MemoryEntry& entry);
    void updateEntry(const QUuid& id, const MemoryEntry& entry);
    void removeEntry(const QUuid& id);
    void clearEntries(bool showConfirmation = true);
    
    QList<MemoryEntry> entries() const;
    MemoryEntry entry(const QUuid& id) const;
    
    // Import
    void importFromLLMRESession(const QString& path = QString());
    
    // State management
    QJsonObject exportState() const;
    void importState(const QJsonObject& state);
    
signals:
    void entryDoubleClicked(const QUuid& id);
    void navigateToAddress(const QString& address);
    
protected:
    void onThemeChanged() override;
    
private slots:
    void onSearchTextChanged(const QString& text);
    void onImportClicked();
    void onClearClicked();
    void onTableDoubleClicked(const QModelIndex& index);
    void onContextMenuRequested(const QPoint& pos);
    void onCopyAddress();
    void onDeleteEntry();
    void onViewEntry();
    
private:
    void setupUI();
    void createActions();
    void connectSignals();
    void updateStatusText();
    void showEntryViewer(const QUuid& id);
    
    // UI elements
    QTableView* tableView_ = nullptr;
    MemoryTableModel* model_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    
    // Actions
    QAction* importAction_ = nullptr;
    QAction* clearAction_ = nullptr;
    QAction* copyAddressAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QAction* viewAction_ = nullptr;
    
    // Context menu
    QMenu* contextMenu_ = nullptr;
    
    // Current selection
    QUuid selectedEntryId_;
};

} // namespace llm_re::ui_v2

// Register MemoryEntry with Qt's meta-type system
Q_DECLARE_METATYPE(llm_re::ui_v2::MemoryEntry)