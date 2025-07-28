#pragma once

#include <QAbstractItemModel>
#include <QDateTime>
#include <QUuid>
#include <memory>
#include <vector>
#include <unordered_map>

namespace llm_re::ui_v2 {

// Message types
enum class MessageRole {
    User,
    Assistant,
    System,
    Tool,
    Error
};

enum class MessageType {
    Text,
    Code,
    Analysis,
    ToolExecution,
    Error,
    Info,
    Warning
};

// Tool execution state
enum class ToolExecutionState {
    Pending,
    Running,
    Completed,
    Failed,
    Cancelled
};

// Message metadata
struct MessageMetadata {
    QDateTime timestamp;
    QString author;
    QStringList tags;
    QUuid parentId; // For threaded conversations
    bool isEdited = false;
    QDateTime editedAt;
    bool isPinned = false;
    bool isBookmarked = false;
    int viewCount = 0;
    QStringList reactions;
    QString language; // For code blocks
    QString fileName; // Associated file
    int lineNumber = -1; // Associated line
};

// Tool execution info
struct ToolExecution {
    QString toolName;
    QString toolId;
    QJsonObject parameters;
    ToolExecutionState state = ToolExecutionState::Pending;
    QString output;
    QString error;
    QDateTime startTime;
    QDateTime endTime;
    int exitCode = 0;
    qint64 duration = 0; // milliseconds
    QStringList affectedFiles;
    
    // Progress tracking
    int progressMin = 0;
    int progressMax = 100;
    int progressValue = 0;
    QString progressText;
    
    // Sub-tasks
    struct SubTask {
        QString id;
        QString description;
        bool completed = false;
        QDateTime completedAt;
    };
    std::vector<SubTask> subTasks;
};

// Analysis entry
struct AnalysisEntry {
    QString type; // "note", "finding", "hypothesis", "question", "analysis", "deep_analysis"
    QString content;
    QString functionName;
    quint64 address = 0;
    int confidence = 0; // 0-100
    QStringList relatedFunctions;
    QStringList references;
    QJsonObject customData;
};

// Message attachment
struct MessageAttachment {
    QString id;
    QString name;
    QString mimeType;
    qint64 size = 0;
    QByteArray data; // For small attachments
    QString filePath; // For large attachments
    QString thumbnailPath;
    QJsonObject metadata;
};

// Core message structure
class Message {
public:
    Message();
    explicit Message(const QString& content, MessageRole role = MessageRole::User);
    
    // Identity
    QUuid id() const { return id_; }
    void setId(const QUuid& id) { id_ = id; }
    
    // Content
    QString content() const { return content_; }
    void setContent(const QString& content);
    QString htmlContent() const { return htmlContent_; }
    void setHtmlContent(const QString& html) { htmlContent_ = html; }
    
    // Properties
    MessageRole role() const { return role_; }
    void setRole(MessageRole role) { role_ = role; }
    
    MessageType type() const { return type_; }
    void setType(MessageType type) { type_ = type; }
    
    // Metadata
    MessageMetadata& metadata() { return metadata_; }
    const MessageMetadata& metadata() const { return metadata_; }
    
    // Tool execution
    bool hasToolExecution() const { return toolExecution_ != nullptr; }
    ToolExecution* toolExecution() { return toolExecution_.get(); }
    const ToolExecution* toolExecution() const { return toolExecution_.get(); }
    void setToolExecution(std::unique_ptr<ToolExecution> execution);
    
    // Analysis
    bool hasAnalysis() const { return !analysisEntries_.empty(); }
    const std::vector<AnalysisEntry>& analysisEntries() const { return analysisEntries_; }
    void addAnalysisEntry(const AnalysisEntry& entry);
    void clearAnalysisEntries() { analysisEntries_.clear(); }
    
    // Attachments
    bool hasAttachments() const { return !attachments_.empty(); }
    const std::vector<MessageAttachment>& attachments() const { return attachments_; }
    void addAttachment(const MessageAttachment& attachment);
    void removeAttachment(const QString& id);
    
    // Thread management
    bool isThreadRoot() const { return metadata_.parentId.isNull(); }
    bool hasReplies() const { return !replies_.empty(); }
    const std::vector<QUuid>& replies() const { return replies_; }
    void addReply(const QUuid& replyId);
    
    // Search and filtering
    bool matchesSearch(const QString& searchText, bool includeContent = true,
                      bool includeTags = true, bool includeAttachments = false) const;
    
    // Serialization
    QJsonObject toJson() const;
    static std::unique_ptr<Message> fromJson(const QJsonObject& json);
    
    // Utility
    QString summary(int maxLength = 100) const;
    QString roleString() const;
    QString typeString() const;
    QIcon roleIcon() const;
    QColor roleColor() const;
    
private:
    QUuid id_;
    QString content_;
    QString htmlContent_;
    MessageRole role_ = MessageRole::User;
    MessageType type_ = MessageType::Text;
    MessageMetadata metadata_;
    std::unique_ptr<ToolExecution> toolExecution_;
    std::vector<AnalysisEntry> analysisEntries_;
    std::vector<MessageAttachment> attachments_;
    std::vector<QUuid> replies_;
};

// Conversation model
class ConversationModel : public QAbstractItemModel {
    Q_OBJECT
    
public:
    enum Column {
        ContentColumn = 0,
        RoleColumn,
        TimestampColumn,
        StatusColumn,
        ColumnCount
    };
    
    enum DataRole {
        MessageRole = Qt::UserRole + 1,
        MessageTypeRole,
        MessageIdRole,
        MessageObjectRole,
        ToolExecutionRole,
        AnalysisRole,
        AttachmentsRole,
        MetadataRole,
        SearchMatchRole,
        ThreadDepthRole,
        IsEditedRole,
        IsPinnedRole,
        IsBookmarkedRole,
        HasRepliesRole,
        ReactionCountRole,
        ProgressRole
    };
    
    explicit ConversationModel(QObject* parent = nullptr);
    ~ConversationModel() override;
    
    // QAbstractItemModel interface
    QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex& child) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    
    // Message management
    void addMessage(std::unique_ptr<Message> message);
    void insertMessage(int index, std::unique_ptr<Message> message);
    void removeMessage(const QUuid& id);
    void updateMessage(const QUuid& id, const QString& newContent);
    void clearMessages();
    
    Message* getMessage(const QUuid& id);
    const Message* getMessage(const QUuid& id) const;
    Message* getMessageAt(int index);
    const Message* getMessageAt(int index) const;
    int getMessageIndex(const QUuid& id) const;
    
    // Bulk operations
    void addMessages(std::vector<std::unique_ptr<Message>> messages);
    void removeMessages(const QSet<QUuid>& ids);
    
    // Tool execution updates
    void updateToolExecution(const QUuid& messageId, 
                           const std::function<void(ToolExecution*)>& updater);
    void setToolExecutionState(const QUuid& messageId, ToolExecutionState state);
    void setToolExecutionProgress(const QUuid& messageId, int value, const QString& text = QString());
    void addToolExecutionOutput(const QUuid& messageId, const QString& output);
    
    // Thread management
    void addReply(const QUuid& parentId, std::unique_ptr<Message> reply);
    std::vector<const Message*> getThread(const QUuid& rootId) const;
    void collapseThread(const QUuid& rootId);
    void expandThread(const QUuid& rootId);
    bool isThreadCollapsed(const QUuid& rootId) const;
    
    // Filtering and search
    void setSearchFilter(const QString& searchText);
    void setRoleFilter(const QSet<MessageRole>& roles);
    void setTypeFilter(const QSet<MessageType>& types);
    void setDateRangeFilter(const QDateTime& start, const QDateTime& end);
    void clearFilters();
    bool isFiltered() const { return !searchFilter_.isEmpty() || 
                                   !roleFilter_.isEmpty() || 
                                   !typeFilter_.isEmpty() || 
                                   dateRangeStart_.isValid(); }
    int getSearchMatchCount() const { return searchMatches_.size(); }
    
    // Pinning and bookmarking
    void setPinned(const QUuid& id, bool pinned);
    void setBookmarked(const QUuid& id, bool bookmarked);
    std::vector<const Message*> getPinnedMessages() const;
    std::vector<const Message*> getBookmarkedMessages() const;
    
    // Reactions
    void addReaction(const QUuid& id, const QString& reaction);
    void removeReaction(const QUuid& id, const QString& reaction);
    
    // Export
    QString exportToMarkdown(bool includeMetadata = true) const;
    QString exportToHtml(bool includeStyles = true) const;
    QJsonDocument exportToJson() const;
    void importFromJson(const QJsonDocument& doc, bool append = false);
    
    // Statistics
    struct ConversationStats {
        int totalMessages = 0;
        int userMessages = 0;
        int assistantMessages = 0;
        int toolExecutions = 0;
        int successfulTools = 0;
        int failedTools = 0;
        int totalAnalyses = 0;
        QMap<QString, int> analysisByType;
        QMap<QString, int> toolUsageCount;
        qint64 totalToolDuration = 0;
        QDateTime firstMessage;
        QDateTime lastMessage;
        int totalTokens = 0; // If available
    };
    ConversationStats getStatistics() const;
    
    // Batch updates
    void beginBatchUpdate();
    void endBatchUpdate();
    bool isBatchUpdating() const { return batchUpdateCount_ > 0; }
    
    // Undo/Redo support
    void setUndoStack(QUndoStack* stack) { undoStack_ = stack; }
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    
signals:
    void messageAdded(const QUuid& id);
    void messageRemoved(const QUuid& id);
    void messageUpdated(const QUuid& id);
    void toolExecutionStarted(const QUuid& messageId);
    void toolExecutionCompleted(const QUuid& messageId, bool success);
    void toolExecutionProgress(const QUuid& messageId, int value);
    void searchMatchesChanged(int count);
    void statisticsChanged();
    void conversationCleared();
    void filtersChanged();
    void threadCollapsed(const QUuid& rootId);
    void threadExpanded(const QUuid& rootId);
    
private:
    struct MessageNode {
        std::unique_ptr<Message> message;
        std::vector<MessageNode*> children;
        MessageNode* parent = nullptr;
        bool collapsed = false;
        bool matchesFilter = true;
        int threadDepth = 0;
    };
    
    void buildThreadTree();
    void updateThreadDepths(MessageNode* node, int depth = 0);
    void applyFilters();
    bool messageMatchesFilters(const Message* msg) const;
    MessageNode* findNode(const QUuid& id) const;
    void collectVisibleNodes(MessageNode* node, std::vector<MessageNode*>& visible) const;
    void emitDataChangedForMessage(const QUuid& id);
    QModelIndex indexForMessage(const QUuid& id) const;
    
    // Storage
    std::vector<std::unique_ptr<MessageNode>> nodes_;
    std::unordered_map<QUuid, MessageNode*, QUuidHash> nodeMap_;
    std::vector<MessageNode*> visibleNodes_; // Flattened view for display
    std::vector<MessageNode*> roots_; // Thread roots
    
    // Filters
    QString searchFilter_;
    QSet<MessageRole> roleFilter_;
    QSet<MessageType> typeFilter_;
    QDateTime dateRangeStart_;
    QDateTime dateRangeEnd_;
    
    // State
    int batchUpdateCount_ = 0;
    QSet<QUuid> searchMatches_;
    QUndoStack* undoStack_ = nullptr;
    
    // Performance
    mutable QHash<QUuid, ConversationStats> statsCache_;
    mutable bool statsCacheDirty_ = true;
};

// Custom item delegate for rich message rendering
class ConversationDelegate : public QStyledItemDelegate {
    Q_OBJECT
    
public:
    explicit ConversationDelegate(QObject* parent = nullptr);
    
    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override;
    
    QSize sizeHint(const QStyleOptionViewItem& option,
                   const QModelIndex& index) const override;
    
    bool editorEvent(QEvent* event, QAbstractItemModel* model,
                     const QStyleOptionViewItem& option,
                     const QModelIndex& index) override;
    
    // Customization
    void setCompactMode(bool compact) { compactMode_ = compact; }
    void setShowAvatars(bool show) { showAvatars_ = show; }
    void setShowTimestamps(bool show) { showTimestamps_ = show; }
    void setMaxBubbleWidth(int width) { maxBubbleWidth_ = width; }
    void setAnimateMessages(bool animate) { animateMessages_ = animate; }
    
signals:
    void linkClicked(const QUrl& url);
    void replyRequested(const QUuid& messageId);
    void reactionClicked(const QUuid& messageId, const QString& reaction);
    void attachmentClicked(const QUuid& messageId, const QString& attachmentId);
    void toolOutputToggled(const QUuid& messageId);
    
private:
    void drawMessageBubble(QPainter* painter, const QStyleOptionViewItem& option,
                          const Message* message, bool isSelected) const;
    void drawToolExecution(QPainter* painter, const QRect& rect,
                          const ToolExecution* execution) const;
    void drawAnalysisEntries(QPainter* painter, const QRect& rect,
                           const std::vector<AnalysisEntry>& entries) const;
    void drawAttachments(QPainter* painter, const QRect& rect,
                        const std::vector<MessageAttachment>& attachments) const;
    void drawReactions(QPainter* painter, const QRect& rect,
                      const QStringList& reactions) const;
    void drawThreadIndicator(QPainter* painter, const QRect& rect,
                           int depth, bool hasReplies) const;
    
    QRect hitTest(const QPoint& pos, const QStyleOptionViewItem& option,
                  const QModelIndex& index) const;
    
    bool compactMode_ = false;
    bool showAvatars_ = true;
    bool showTimestamps_ = true;
    int maxBubbleWidth_ = 600;
    bool animateMessages_ = true;
    
    mutable QHash<QUuid, QPropertyAnimation*> animations_;
    mutable QHash<QUuid, QRect> bubbleRects_;
    mutable QHash<QUuid, QMap<QString, QRect>> hitAreas_;
};

} // namespace llm_re::ui_v2