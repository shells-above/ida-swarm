#pragma once

#include "../core/base_styled_widget.h"
#include "../models/memory_model.h"
#include <QDockWidget>
#include <QAbstractItemModel>
#include <QSortFilterProxyModel>
#include <memory>

class QTreeView;
class QTableView;
class QListView;
class QGraphicsView;
class QGraphicsScene;
class QToolBar;
class QLineEdit;
class QComboBox;
class QSlider;
class QSplitter;
class QStackedWidget;
class QMenu;
class QAction;

namespace llm_re::ui_v2 {

// Memory entry for analysis
struct MemoryEntry {
    QUuid id;
    QString address;
    QString function;
    QString module;
    QString analysis;
    QStringList tags;
    QDateTime timestamp;
    int confidence = 0;
    bool isBookmarked = false;
    QJsonObject metadata;
    
    // Relationships
    QList<QUuid> references;
    QList<QUuid> referencedBy;
};

// Graph node for visualization
class MemoryGraphNode : public QGraphicsItem {
public:
    explicit MemoryGraphNode(const MemoryEntry& entry);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    void setHighlighted(bool highlighted) { highlighted_ = highlighted; update(); }
    bool isHighlighted() const { return highlighted_; }
    
    const MemoryEntry& entry() const { return entry_; }
    QPointF centerPos() const;
    
    void addEdge(MemoryGraphNode* target);
    void removeEdge(MemoryGraphNode* target);
    const QList<MemoryGraphNode*>& edges() const { return edges_; }
    
protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* event) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    
private:
    MemoryEntry entry_;
    QList<MemoryGraphNode*> edges_;
    bool highlighted_ = false;
    bool hovered_ = false;
    qreal nodeRadius_ = 30.0;
};

// Graph edge connection
class MemoryGraphEdge : public QGraphicsItem {
public:
    MemoryGraphEdge(MemoryGraphNode* source, MemoryGraphNode* target);
    
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    
    void updatePosition();
    
private:
    MemoryGraphNode* source_;
    MemoryGraphNode* target_;
};

// Graph view for memory relationships
class MemoryGraphView : public QGraphicsView {
    Q_OBJECT
    
public:
    explicit MemoryGraphView(QWidget* parent = nullptr);
    
    void setEntries(const QList<MemoryEntry>& entries);
    void highlightEntry(const QUuid& id);
    void centerOnEntry(const QUuid& id);
    
    void setLayoutAlgorithm(const QString& algorithm);
    void setEdgeStyle(const QString& style);
    void setShowLabels(bool show);
    void setAnimated(bool animated);
    
    void exportGraph(const QString& format);
    
signals:
    void entryClicked(const QUuid& id);
    void entryDoubleClicked(const QUuid& id);
    void selectionChanged(const QList<QUuid>& ids);
    
public slots:
    void zoomIn();
    void zoomOut();
    void fitInView();
    void resetZoom();
    
protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    
private:
    void performLayout();
    void performForceDirectedLayout();
    void performHierarchicalLayout();
    void performCircularLayout();
    void animateToLayout();
    
    QGraphicsScene* scene_;
    QHash<QUuid, MemoryGraphNode*> nodes_;
    QList<MemoryGraphEdge*> edges_;
    
    QString layoutAlgorithm_ = "force-directed";
    QString edgeStyle_ = "curved";
    bool showLabels_ = true;
    bool animated_ = true;
    
    // Interaction
    bool isPanning_ = false;
    QPoint lastMousePos_;
    qreal currentScale_ = 1.0;
};

// Heatmap view for function coverage
class MemoryHeatmapView : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit MemoryHeatmapView(QWidget* parent = nullptr);
    
    void setEntries(const QList<MemoryEntry>& entries);
    void setColorScheme(const QString& scheme);
    void setGroupBy(const QString& field);
    void setMetric(const QString& metric);
    
    void exportHeatmap(const QString& format);
    
signals:
    void cellClicked(const QString& group, const QString& subgroup);
    void selectionChanged(const QStringList& groups);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    
private:
    void calculateLayout();
    QColor valueToColor(double value);
    QString formatValue(double value);
    
    struct HeatmapCell {
        QString group;
        QString subgroup;
        double value;
        QRectF rect;
        int count;
    };
    
    QList<MemoryEntry> entries_;
    QVector<HeatmapCell> cells_;
    QString colorScheme_ = "viridis";
    QString groupBy_ = "function";
    QString metric_ = "count";
    
    // Interaction
    int hoveredCell_ = -1;
    QSet<int> selectedCells_;
    
    // Layout
    int cellSize_ = 20;
    int margin_ = 60;
    int spacing_ = 2;
};

// Main memory dock widget
class MemoryDock : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit MemoryDock(QWidget* parent = nullptr);
    ~MemoryDock() override;
    
    // Data management
    void addEntry(const MemoryEntry& entry);
    void updateEntry(const QUuid& id, const MemoryEntry& entry);
    void removeEntry(const QUuid& id);
    void clearEntries();
    
    QList<MemoryEntry> entries() const;
    MemoryEntry entry(const QUuid& id) const;
    
    // View control
    void setViewMode(const QString& mode);
    QString viewMode() const { return currentViewMode_; }
    
    void showEntry(const QUuid& id);
    void selectEntry(const QUuid& id);
    void selectEntries(const QList<QUuid>& ids);
    
    // Filtering
    void setFilter(const QString& text);
    void setTagFilter(const QStringList& tags);
    void setDateRangeFilter(const QDateTime& start, const QDateTime& end);
    void clearFilters();
    
    // Queries
    void saveQuery(const QString& name);
    void loadQuery(const QString& name);
    QStringList savedQueries() const;
    void deleteQuery(const QString& name);
    
    // Export
    void exportData(const QString& format = "json");
    void exportSelection(const QString& format = "json");
    
    // Bulk operations
    void tagSelection(const QStringList& tags);
    void untagSelection(const QStringList& tags);
    void deleteSelection();
    void bookmarkSelection(bool bookmark);
    
signals:
    void entryClicked(const QUuid& id);
    void entryDoubleClicked(const QUuid& id);
    void entryContextMenu(const QUuid& id, const QPoint& pos);
    void selectionChanged(const QList<QUuid>& ids);
    void viewModeChanged(const QString& mode);
    void filterChanged();
    void dataExported(const QString& path);
    void navigateToAddress(const QString& address);
    void analyzeRequested(const QUuid& id);
    
public slots:
    void refreshView();
    void importData(const QString& path);
    
protected:
    void onThemeChanged() override;
    
private slots:
    void onViewModeChanged(int index);
    void onSearchTextChanged(const QString& text);
    void onAdvancedFilterClicked();
    void onExportClicked();
    void onImportClicked();
    void onEntryActivated(const QModelIndex& index);
    void onSelectionChanged();
    void onCustomContextMenu(const QPoint& pos);
    void onGraphEntryClicked(const QUuid& id);
    void onHeatmapCellClicked(const QString& group, const QString& subgroup);
    void updateStatusBar();
    
private:
    void setupUI();
    void createToolBar();
    void createViews();
    void createStatusBar();
    void connectSignals();
    void createContextMenu();
    void applyFilters();
    void saveSettings();
    void loadSettings();
    
    // Models
    MemoryModel* model_ = nullptr;
    QSortFilterProxyModel* proxyModel_ = nullptr;
    
    // Views
    QStackedWidget* viewStack_ = nullptr;
    QTreeView* treeView_ = nullptr;
    QTableView* tableView_ = nullptr;
    MemoryGraphView* graphView_ = nullptr;
    MemoryHeatmapView* heatmapView_ = nullptr;
    
    // UI elements
    QToolBar* toolBar_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QComboBox* viewModeCombo_ = nullptr;
    QComboBox* groupByCombo_ = nullptr;
    QAction* refreshAction_ = nullptr;
    QAction* importAction_ = nullptr;
    QAction* exportAction_ = nullptr;
    QAction* filterAction_ = nullptr;
    QAction* bookmarkAction_ = nullptr;
    QAction* deleteAction_ = nullptr;
    QMenu* contextMenu_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    
    // State
    QString currentViewMode_ = "tree";
    QList<QUuid> selectedEntries_;
    
    // Filters
    QString searchText_;
    QStringList tagFilters_;
    QDateTime startDateFilter_;
    QDateTime endDateFilter_;
    
    // Settings
    QStringList recentImports_;
    QHash<QString, QJsonObject> savedQueries_;
};

// Advanced filter dialog
class MemoryFilterDialog : public QDialog {
    Q_OBJECT
    
public:
    explicit MemoryFilterDialog(QWidget* parent = nullptr);
    
    void setFilters(const QString& text, const QStringList& tags,
                   const QDateTime& startDate, const QDateTime& endDate);
    
    QString searchText() const;
    QStringList selectedTags() const;
    QDateTime startDate() const;
    QDateTime endDate() const;
    
    void setAvailableTags(const QStringList& tags);
    
private:
    void setupUI();
    
    QLineEdit* searchEdit_ = nullptr;
    QListWidget* tagsList_ = nullptr;
    QDateTimeEdit* startDateEdit_ = nullptr;
    QDateTimeEdit* endDateEdit_ = nullptr;
    QComboBox* confidenceCombo_ = nullptr;
    QCheckBox* bookmarkedOnlyCheck_ = nullptr;
};

// Memory model implementation
class MemoryModel : public QAbstractItemModel {
    Q_OBJECT
    
public:
    enum Column {
        AddressColumn,
        FunctionColumn,
        ModuleColumn,
        TagsColumn,
        TimestampColumn,
        ConfidenceColumn,
        ColumnCount
    };
    
    enum Role {
        EntryRole = Qt::UserRole + 1,
        IdRole,
        BookmarkedRole,
        ConfidenceRole
    };
    
    explicit MemoryModel(QObject* parent = nullptr);
    ~MemoryModel() override;
    
    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    
    // Data management
    void addEntry(const MemoryEntry& entry);
    void updateEntry(const QUuid& id, const MemoryEntry& entry);
    void removeEntry(const QUuid& id);
    void clearEntries();
    
    MemoryEntry entry(const QUuid& id) const;
    QList<MemoryEntry> entries() const { return entries_; }
    
    // Grouping
    void setGroupBy(const QString& field);
    QString groupBy() const { return groupBy_; }
    
    // Statistics
    int totalEntries() const { return entries_.size(); }
    int bookmarkedCount() const;
    QStringList allTags() const;
    QStringList allModules() const;
    QStringList allFunctions() const;
    
signals:
    void entryAdded(const QUuid& id);
    void entryUpdated(const QUuid& id);
    void entryRemoved(const QUuid& id);
    void modelReset();
    
private:
    struct TreeNode {
        QString name;
        TreeNode* parent = nullptr;
        QList<TreeNode*> children;
        MemoryEntry* entry = nullptr;
        bool isGroup = false;
    };
    
    void rebuildTree();
    void clearTree();
    TreeNode* nodeForIndex(const QModelIndex& index) const;
    QModelIndex indexForNode(TreeNode* node) const;
    
    QList<MemoryEntry> entries_;
    QHash<QUuid, MemoryEntry*> entryMap_;
    TreeNode* rootNode_ = nullptr;
    QString groupBy_ = "module";
};

} // namespace llm_re::ui_v2