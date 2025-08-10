#pragma once

#include "../core/ui_v2_common.h"
#include "sdk/messages/types.h"

namespace llm_re::ui_v2 {

// Use the API message types directly!
using claude::messages::Message;
using claude::messages::Role;
using claude::messages::Content;
using claude::messages::TextContent;
using claude::messages::ThinkingContent;
using claude::messages::ToolUseContent;
using claude::messages::ToolResultContent;

// UI-specific metadata that extends the base message
struct MessageMetadata {
    QUuid id;
    QDateTime timestamp;
    QString author;
    QString language; // For code blocks
    QString fileName; // Associated file
    int lineNumber = -1; // Associated line
};

// Wrapper that holds an API message + UI metadata
struct UIMessage {
    std::shared_ptr<Message> message;  // The actual API message
    MessageMetadata metadata;                     // UI-specific metadata
    
    // Helper functions to access message content
    QUuid id() const { return metadata.id; }
    Role role() const { return message->role(); }
    
    QString roleString() const {
        switch (message->role()) {
            case Role::User: return "User";
            case Role::Assistant: return "Assistant";
            case Role::System: return "System";
            default: return "Unknown";
        }
    }

    QString getDisplayText() const {
        QString result;
        for (const auto& content : message->contents()) {
            if (auto* text = dynamic_cast<const TextContent*>(content.get())) {
                if (!result.isEmpty()) result += "\n";
                result += QString::fromStdString(text->text);
            }
        }
        return result;
    }
    
    QString getThinkingText() const {
        QString result;
        for (const auto& content : message->contents()) {
            if (auto* thinking = dynamic_cast<const ThinkingContent*>(content.get())) {
                if (!result.isEmpty()) result += "\n";
                result += QString::fromStdString(thinking->thinking);
            }
        }
        return result;
    }
    
    bool hasThinking() const {
        for (const auto& content : message->contents()) {
            if (dynamic_cast<const ThinkingContent*>(content.get())) {
                return true;
            }
        }
        return false;
    }
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
        MessageRoleDataRole = Qt::UserRole + 1,
        MessageTypeRole,
        MessageIdRole,
        MessageObjectRole,
        MetadataRole,
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
    void addMessage(std::shared_ptr<Message> message, const MessageMetadata& metadata = {});
    void insertMessage(int index, std::shared_ptr<Message> message, const MessageMetadata& metadata = {});
    void clearMessages();
    
    UIMessage* getMessage(const QUuid& id);
    const UIMessage* getMessage(const QUuid& id) const;
    UIMessage* getMessageAt(int index);
    const UIMessage* getMessageAt(int index) const;

    // Batch updates
    void beginBatchUpdate();
    void endBatchUpdate();
    bool isBatchUpdating() const { return batchUpdateCount_ > 0; }
    
    
signals:
    void messageAdded(const QUuid& id);
    void searchMatchesChanged(int count);
    void conversationCleared();
    void filtersChanged();
    
private:
    struct MessageNode {
        UIMessage message;
        bool matchesFilter = true;
    };
    
    MessageNode* findNode(const QUuid& id) const;
    QModelIndex indexForMessage(const QUuid& id) const;
    
    // Storage
    std::vector<std::unique_ptr<MessageNode>> nodes_;
    std::unordered_map<QUuid, MessageNode*, QUuidHash> nodeMap_;
    std::vector<MessageNode*> visibleNodes_; // Visible messages for display

    // State
    int batchUpdateCount_ = 0;
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
    
    // Customization
    void setDensityMode(int mode) { densityMode_ = mode; }  // 0=Compact, 1=Cozy, 2=Spacious
    void setShowTimestamps(bool show) { showTimestamps_ = show; }
    void setMaxBubbleWidth(int width) { maxBubbleWidth_ = width; }
    void setAnimateMessages(bool animate) { animateMessages_ = animate; }
    
private:
    void drawMessageBubble(QPainter* painter, const QStyleOptionViewItem& option,
                          const UIMessage* message, bool isSelected) const;
    
    int densityMode_ = 1;  // 0=Compact, 1=Cozy, 2=Spacious
    bool showTimestamps_ = true;
    int maxBubbleWidth_ = 600;
    bool animateMessages_ = true;
    
    mutable QHash<QUuid, QPropertyAnimation*> animations_;
    mutable QHash<QUuid, QRect> bubbleRects_;
    mutable QHash<QUuid, QMap<QString, QRect>> hitAreas_;
};

} // namespace llm_re::ui_v2

// Register types with Qt's meta-type system
Q_DECLARE_METATYPE(llm_re::ui_v2::UIMessage*)
Q_DECLARE_METATYPE(const llm_re::ui_v2::MessageMetadata*)