#include "conversation_model.h"
#include "../core/theme_manager.h"
#include "../core/ui_utils.h"

namespace llm_re::ui_v2 {

// ConversationModel implementation

ConversationModel::ConversationModel(QObject* parent)
    : QAbstractItemModel(parent) {
}

ConversationModel::~ConversationModel() = default;

QModelIndex ConversationModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }
    
    if (row >= 0 && row < static_cast<int>(visibleNodes_.size())) {
        return createIndex(row, column, visibleNodes_[row]);
    }
    
    return QModelIndex();
}

QModelIndex ConversationModel::parent(const QModelIndex&) const {
    return QModelIndex(); // Flat list
}

int ConversationModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) {
        return 0; // Flat list
    }
    return static_cast<int>(visibleNodes_.size());
}

int ConversationModel::columnCount(const QModelIndex&) const {
    return ColumnCount;
}

QVariant ConversationModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= static_cast<int>(visibleNodes_.size())) {
        return QVariant();
    }
    
    MessageNode* node = visibleNodes_[index.row()];
    if (!node) {
        return QVariant();
    }
    
    const UIMessage* msg = &node->message;
    
    switch (role) {
        case Qt::DisplayRole:
            if (index.column() == ContentColumn) {
                return msg->getDisplayText();
            }
            break;
            
        case MessageRoleDataRole:
            return static_cast<int>(msg->role());
            
        case MessageIdRole:
            return msg->metadata.id;
            
        case MessageObjectRole:
            return QVariant::fromValue(const_cast<UIMessage*>(msg));
            
        case MetadataRole:
            return QVariant::fromValue(&msg->metadata);
    }
    
    return QVariant();
}

QVariant ConversationModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
        return QVariant();
    }
    
    switch (section) {
        case ContentColumn: return "Message";
        case RoleColumn: return "Role";
        case TimestampColumn: return "Time";
        case StatusColumn: return "Status";
    }
    
    return QVariant();
}

Qt::ItemFlags ConversationModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

bool ConversationModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || index.row() >= static_cast<int>(visibleNodes_.size())) {
        return false;
    }
    
    MessageNode* node = visibleNodes_[index.row()];
    if (!node) {
        return false;
    }
    
    // Handle editable roles if needed
    
    emit dataChanged(index, index, {role});
    return true;
}

void ConversationModel::addMessage(std::shared_ptr<messages::Message> message, const MessageMetadata& metadata) {
    int row = static_cast<int>(nodes_.size());
    
    beginInsertRows(QModelIndex(), row, row);
    
    auto node = std::make_unique<MessageNode>();
    node->message.message = message;
    node->message.metadata = metadata;
    
    if (metadata.id.isNull()) {
        node->message.metadata.id = QUuid::createUuid();
    }
    
    MessageNode* nodePtr = node.get();
    nodes_.push_back(std::move(node));
    nodeMap_[nodePtr->message.metadata.id] = nodePtr;
    
    visibleNodes_.push_back(nodePtr);

    endInsertRows();
    
    emit messageAdded(nodePtr->message.metadata.id);
}

void ConversationModel::insertMessage(int index, std::shared_ptr<messages::Message> message, const MessageMetadata& metadata) {
    if (index < 0 || index > static_cast<int>(nodes_.size())) {
        index = static_cast<int>(nodes_.size());
    }
    
    beginInsertRows(QModelIndex(), index, index);
    
    auto node = std::make_unique<MessageNode>();
    node->message.message = message;
    node->message.metadata = metadata;
    
    if (metadata.id.isNull()) {
        node->message.metadata.id = QUuid::createUuid();
    }
    
    MessageNode* nodePtr = node.get();
    nodes_.insert(nodes_.begin() + index, std::move(node));
    nodeMap_[nodePtr->message.metadata.id] = nodePtr;
    
    endInsertRows();
    
    emit messageAdded(nodePtr->message.metadata.id);
}

void ConversationModel::clearMessages() {
    beginResetModel();
    
    nodes_.clear();
    nodeMap_.clear();
    visibleNodes_.clear();

    endResetModel();
    
    emit conversationCleared();
}

UIMessage* ConversationModel::getMessage(const QUuid& id) {
    auto it = nodeMap_.find(id);
    return it != nodeMap_.end() ? &it->second->message : nullptr;
}

const UIMessage* ConversationModel::getMessage(const QUuid& id) const {
    auto it = nodeMap_.find(id);
    return it != nodeMap_.end() ? &it->second->message : nullptr;
}

UIMessage* ConversationModel::getMessageAt(int index) {
    if (index >= 0 && index < static_cast<int>(visibleNodes_.size())) {
        return &visibleNodes_[index]->message;
    }
    return nullptr;
}

const UIMessage* ConversationModel::getMessageAt(int index) const {
    if (index >= 0 && index < static_cast<int>(visibleNodes_.size())) {
        return &visibleNodes_[index]->message;
    }
    return nullptr;
}

ConversationModel::MessageNode* ConversationModel::findNode(const QUuid& id) const {
    auto it = nodeMap_.find(id);
    return it != nodeMap_.end() ? it->second : nullptr;
}

QModelIndex ConversationModel::indexForMessage(const QUuid& id) const {
    auto it = std::find_if(visibleNodes_.begin(), visibleNodes_.end(),
                           [&id](const MessageNode* node) { 
                               return node->message.metadata.id == id; 
                           });
    
    if (it != visibleNodes_.end()) {
        int row = std::distance(visibleNodes_.begin(), it);
        return index(row, 0);
    }
    
    return QModelIndex();
}

void ConversationModel::beginBatchUpdate() {
    batchUpdateCount_++;
    if (batchUpdateCount_ == 1) {
        beginResetModel();
    }
}

void ConversationModel::endBatchUpdate() {
    if (batchUpdateCount_ > 0) {
        batchUpdateCount_--;
        if (batchUpdateCount_ == 0) {
            endResetModel();
        }
    }
}

// Delegate implementation

ConversationDelegate::ConversationDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {
}

void ConversationDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                const QModelIndex& index) const {
    if (!index.isValid()) {
        return;
    }
    
    // Get message
    UIMessage* msg = qvariant_cast<UIMessage*>(
        index.data(ConversationModel::MessageObjectRole)
    );
    
    if (!msg) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    
    painter->save();
    
    // Draw background
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    }
    
    // Draw message bubble
    drawMessageBubble(painter, option, msg, option.state & QStyle::State_Selected);
    
    painter->restore();
}

QSize ConversationDelegate::sizeHint(const QStyleOptionViewItem& option,
                                    const QModelIndex& index) const {
    // Calculate size based on content
    int width = option.rect.width();
    if (width <= 0) {
        width = maxBubbleWidth_;
    }
    
    int height = 60; // Base height
    
    // Add height for content
    UIMessage* msg = qvariant_cast<UIMessage*>(
        index.data(ConversationModel::MessageObjectRole)
    );
    
    if (msg) {
        QString text = msg->getDisplayText();
        QFontMetrics fm(option.font);
        QRect textRect = fm.boundingRect(QRect(0, 0, width - 40, 0),
                                        Qt::TextWordWrap, text);
        height = textRect.height() + 40;
        
        // Add height for thinking if present
        if (msg->hasThinking()) {
            QString thinking = msg->getThinkingText();
            QRect thinkingRect = fm.boundingRect(QRect(0, 0, width - 40, 0),
                                                Qt::TextWordWrap, thinking);
            height += thinkingRect.height() + 20;
        }
    }
    
    // Add spacing based on density mode
    switch (densityMode_) {
        case 0: height += 5; break;   // Compact
        case 1: height += 15; break;  // Cozy
        case 2: height += 25; break;  // Spacious
    }
    
    return QSize(width, height);
}

void ConversationDelegate::drawMessageBubble(QPainter* painter, 
                                            const QStyleOptionViewItem& option,
                                            const UIMessage* message, 
                                            bool isSelected) const {
    if (!message) return;
    
    const auto& colors = ThemeManager::instance().colors();
    
    // Determine bubble color based on role
    QColor bubbleColor;
    switch (message->role()) {
        case messages::Role::User:
            bubbleColor = colors.userMessage;
            break;
        case messages::Role::Assistant:
            bubbleColor = colors.assistantMessage;
            break;
        case messages::Role::System:
            bubbleColor = colors.systemMessage;
            break;
    }
    
    if (isSelected) {
        bubbleColor = bubbleColor.darker(110);
    }
    
    // Draw bubble background
    QRect bubbleRect = option.rect.adjusted(10, 5, -10, -5);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bubbleColor);
    painter->drawRoundedRect(bubbleRect, 8, 8);
    
    // Draw text
    painter->setPen(colors.textPrimary);
    QRect textRect = bubbleRect.adjusted(15, 10, -15, -10);
    
    QString displayText = message->getDisplayText();
    painter->drawText(textRect, Qt::TextWordWrap, displayText);
    
    // Draw thinking indicator if present
    if (message->hasThinking()) {
        painter->setPen(colors.textSecondary);
        painter->setFont(QFont(option.font.family(), option.font.pointSize() - 1, QFont::Light));
        
        QRect thinkingRect = textRect.adjusted(0, textRect.height() + 5, 0, 0);
        painter->drawText(thinkingRect, Qt::TextWordWrap, 
                         QString("[Thinking] %1").arg(message->getThinkingText()));
    }
    
    // Draw timestamp if enabled
    if (showTimestamps_) {
        painter->setPen(colors.textTertiary);
        painter->setFont(QFont(option.font.family(), option.font.pointSize() - 2));
        
        QString timeStr = message->metadata.timestamp.toString("hh:mm:ss");
        QRect timeRect = bubbleRect.adjusted(0, -20, -10, 0);
        painter->drawText(timeRect, Qt::AlignRight | Qt::AlignTop, timeStr);
    }
}

} // namespace llm_re::ui_v2