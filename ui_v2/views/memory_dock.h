#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"
#include "analysis/memory.h"
#include <memory>

namespace llm_re::ui_v2 {

// Beautiful dialog for viewing memory entries
class MemoryEntryViewer : public QDialog {
    Q_OBJECT
    
public:
    explicit MemoryEntryViewer(const llm_re::AnalysisEntry& entry, QWidget* parent = nullptr);
    
private:
    void setupUI();
    QString renderMarkdown(const QString& text);
    QString formatJson(const QJsonObject& obj);
    QJsonObject entryToJson() const;
    
    llm_re::AnalysisEntry entry_;
    
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
    
    // Set the memory source
    void setMemory(std::shared_ptr<llm_re::BinaryMemory> memory);
    
    // QAbstractTableModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    
    // Data management
    void refresh();  // Refresh from memory
    void clearEntries();
    
    // Get entry by row or key
    llm_re::AnalysisEntry entry(int row) const;
    llm_re::AnalysisEntry entry(const QString& key) const;
    std::vector<llm_re::AnalysisEntry> allEntries() const;
    int findEntry(const QString& key) const;
    QString keyAt(int row) const;
    
    // Search/Filter
    void setSearchFilter(const QString& text);
    void clearFilters();
    
signals:
    void dataRefreshed();
    
private:
    std::shared_ptr<llm_re::BinaryMemory> memory_;
    std::vector<std::string> keys_;  // Ordered list of keys for display
    std::vector<std::string> filteredKeys_;  // Filtered keys
    QString searchText_;
    bool isFiltered_ = false;
    
    void applyFilters();
    bool matchesFilter(const llm_re::AnalysisEntry& entry) const;
};

// Main memory dock widget - simplified
class MemoryDock : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit MemoryDock(QWidget* parent = nullptr);
    ~MemoryDock() override;
    
    // Set the memory source
    void setMemory(std::shared_ptr<llm_re::BinaryMemory> memory);
    
    // Data management
    void refresh();  // Refresh display from memory
    void clearEntries(bool showConfirmation = true);
    
    std::vector<llm_re::AnalysisEntry> entries() const;
    llm_re::AnalysisEntry entry(const QString& key) const;
    
    // Import
    void importFromLLMRESession(const QString& path = QString());
    
    // State management
    QJsonObject exportState() const;
    void importState(const QJsonObject& state);
    
signals:
    void entryDoubleClicked(const QString& key);
    void navigateToAddress(const QString& address);
    
private slots:
    void onSearchTextChanged(const QString& text);
    void onImportClicked();
    void onClearClicked();
    void onTableDoubleClicked(const QModelIndex& index);
    void onContextMenuRequested(const QPoint& pos);
    void onCopyAddress();
    void onViewEntry();
    
private:
    void setupUI();
    void createActions();
    void connectSignals();
    void updateStatusText();
    void showEntryViewer(const QString& key);
    
    // UI elements
    QTableView* tableView_ = nullptr;
    MemoryTableModel* model_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    
    // Actions
    QAction* importAction_ = nullptr;
    QAction* clearAction_ = nullptr;
    QAction* copyAddressAction_ = nullptr;
    QAction* viewAction_ = nullptr;
    
    // Context menu
    QMenu* contextMenu_ = nullptr;
    
    // Current selection
    QString selectedEntryKey_;
    
    // Memory source
    std::shared_ptr<llm_re::BinaryMemory> memory_;
};

} // namespace llm_re::ui_v2