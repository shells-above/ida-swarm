#include "conversation_model.h"
#include "../core/theme_manager.h"
#include "../core/ui_utils.h"
#include <QPainter>
#include <QApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QTextDocument>
#include <QUndoStack>
#include <QMouseEvent>
#include <QToolTip>
#include <algorithm>
#include <QDebug>

namespace llm_re::ui_v2 {

// Message implementation

Message::Message() 
    : id_(QUuid::createUuid()) {
    metadata_.timestamp = QDateTime::currentDateTime();
}

Message::Message(const QString& content, MessageRole role)
    : id_(QUuid::createUuid()), content_(content), role_(role) {
    metadata_.timestamp = QDateTime::currentDateTime();
}

void Message::setContent(const QString& content) {
    content_ = content;
    metadata_.isEdited = true;
    metadata_.editedAt = QDateTime::currentDateTime();
}

void Message::setToolExecution(std::unique_ptr<ToolExecution> execution) {
    toolExecution_ = std::move(execution);
    type_ = MessageType::ToolExecution;
}

void Message::addAnalysisEntry(const AnalysisEntry& entry) {
    analysisEntries_.push_back(entry);
    if (type_ == MessageType::Text) {
        type_ = MessageType::Analysis;
    }
}

void Message::addAttachment(const MessageAttachment& attachment) {
    attachments_.push_back(attachment);
}

void Message::removeAttachment(const QString& id) {
    attachments_.erase(
        std::remove_if(attachments_.begin(), attachments_.end(),
                      [&id](const MessageAttachment& att) { return att.id == id; }),
        attachments_.end()
    );
}

void Message::addReply(const QUuid& replyId) {
    replies_.push_back(replyId);
}

bool Message::matchesSearch(const QString& searchText, bool includeContent,
                          bool includeTags, bool includeAttachments) const {
    if (searchText.isEmpty()) return true;
    
    QString search = searchText.toLower();
    
    if (includeContent && content_.toLower().contains(search)) {
        return true;
    }
    
    if (includeTags) {
        for (const QString& tag : metadata_.tags) {
            if (tag.toLower().contains(search)) {
                return true;
            }
        }
    }
    
    if (includeAttachments) {
        for (const auto& attachment : attachments_) {
            if (attachment.name.toLower().contains(search)) {
                return true;
            }
        }
    }
    
    return false;
}

QJsonObject Message::toJson() const {
    QJsonObject obj;
    obj["id"] = id_.toString();
    obj["content"] = content_;
    obj["htmlContent"] = htmlContent_;
    obj["role"] = static_cast<int>(role_);
    obj["type"] = static_cast<int>(type_);
    
    // Metadata
    QJsonObject meta;
    meta["timestamp"] = metadata_.timestamp.toString(Qt::ISODate);
    meta["author"] = metadata_.author;
    meta["tags"] = QJsonArray::fromStringList(metadata_.tags);
    meta["parentId"] = metadata_.parentId.toString();
    meta["isEdited"] = metadata_.isEdited;
    meta["editedAt"] = metadata_.editedAt.toString(Qt::ISODate);
    meta["isPinned"] = metadata_.isPinned;
    meta["isBookmarked"] = metadata_.isBookmarked;
    meta["viewCount"] = metadata_.viewCount;
    meta["reactions"] = QJsonArray::fromStringList(metadata_.reactions);
    meta["language"] = metadata_.language;
    meta["fileName"] = metadata_.fileName;
    meta["lineNumber"] = metadata_.lineNumber;
    obj["metadata"] = meta;
    
    // Tool execution
    if (toolExecution_) {
        QJsonObject tool;
        tool["toolName"] = toolExecution_->toolName;
        tool["toolId"] = toolExecution_->toolId;
        tool["parameters"] = toolExecution_->parameters;
        tool["state"] = static_cast<int>(toolExecution_->state);
        tool["output"] = toolExecution_->output;
        tool["error"] = toolExecution_->error;
        tool["startTime"] = toolExecution_->startTime.toString(Qt::ISODate);
        tool["endTime"] = toolExecution_->endTime.toString(Qt::ISODate);
        tool["exitCode"] = toolExecution_->exitCode;
        tool["duration"] = toolExecution_->duration;
        tool["affectedFiles"] = QJsonArray::fromStringList(toolExecution_->affectedFiles);
        obj["toolExecution"] = tool;
    }
    
    // Analysis entries
    if (!analysisEntries_.empty()) {
        QJsonArray analyses;
        for (const auto& entry : analysisEntries_) {
            QJsonObject analysis;
            analysis["type"] = entry.type;
            analysis["content"] = entry.content;
            analysis["functionName"] = entry.functionName;
            analysis["address"] = QString::number(entry.address, 16);
            analysis["confidence"] = entry.confidence;
            analysis["relatedFunctions"] = QJsonArray::fromStringList(entry.relatedFunctions);
            analysis["references"] = QJsonArray::fromStringList(entry.references);
            analysis["customData"] = entry.customData;
            analyses.append(analysis);
        }
        obj["analysisEntries"] = analyses;
    }
    
    // Attachments
    if (!attachments_.empty()) {
        QJsonArray atts;
        for (const auto& attachment : attachments_) {
            QJsonObject att;
            att["id"] = attachment.id;
            att["name"] = attachment.name;
            att["mimeType"] = attachment.mimeType;
            att["size"] = attachment.size;
            att["filePath"] = attachment.filePath;
            att["thumbnailPath"] = attachment.thumbnailPath;
            att["metadata"] = attachment.metadata;
            atts.append(att);
        }
        obj["attachments"] = atts;
    }
    
    // Replies
    if (!replies_.empty()) {
        QJsonArray reps;
        for (const auto& reply : replies_) {
            reps.append(reply.toString());
        }
        obj["replies"] = reps;
    }
    
    return obj;
}

std::unique_ptr<Message> Message::fromJson(const QJsonObject& json) {
    auto msg = std::make_unique<Message>();
    
    msg->id_ = QUuid::fromString(json["id"].toString());
    msg->content_ = json["content"].toString();
    msg->htmlContent_ = json["htmlContent"].toString();
    msg->role_ = static_cast<MessageRole>(json["role"].toInt());
    msg->type_ = static_cast<MessageType>(json["type"].toInt());
    
    // Metadata
    if (json.contains("metadata")) {
        QJsonObject meta = json["metadata"].toObject();
        msg->metadata_.timestamp = QDateTime::fromString(meta["timestamp"].toString(), Qt::ISODate);
        msg->metadata_.author = meta["author"].toString();
        msg->metadata_.tags = meta["tags"].toArray().toVariantList().toStringList();
        msg->metadata_.parentId = QUuid::fromString(meta["parentId"].toString());
        msg->metadata_.isEdited = meta["isEdited"].toBool();
        msg->metadata_.editedAt = QDateTime::fromString(meta["editedAt"].toString(), Qt::ISODate);
        msg->metadata_.isPinned = meta["isPinned"].toBool();
        msg->metadata_.isBookmarked = meta["isBookmarked"].toBool();
        msg->metadata_.viewCount = meta["viewCount"].toInt();
        msg->metadata_.reactions = meta["reactions"].toArray().toVariantList().toStringList();
        msg->metadata_.language = meta["language"].toString();
        msg->metadata_.fileName = meta["fileName"].toString();
        msg->metadata_.lineNumber = meta["lineNumber"].toInt();
    }
    
    // Tool execution
    if (json.contains("toolExecution")) {
        QJsonObject tool = json["toolExecution"].toObject();
        auto exec = std::make_unique<ToolExecution>();
        exec->toolName = tool["toolName"].toString();
        exec->toolId = tool["toolId"].toString();
        exec->parameters = tool["parameters"].toObject();
        exec->state = static_cast<ToolExecutionState>(tool["state"].toInt());
        exec->output = tool["output"].toString();
        exec->error = tool["error"].toString();
        exec->startTime = QDateTime::fromString(tool["startTime"].toString(), Qt::ISODate);
        exec->endTime = QDateTime::fromString(tool["endTime"].toString(), Qt::ISODate);
        exec->exitCode = tool["exitCode"].toInt();
        exec->duration = tool["duration"].toVariant().toLongLong();
        exec->affectedFiles = tool["affectedFiles"].toArray().toVariantList().toStringList();
        msg->toolExecution_ = std::move(exec);
    }
    
    // Analysis entries
    if (json.contains("analysisEntries")) {
        QJsonArray analyses = json["analysisEntries"].toArray();
        for (const QJsonValue& val : analyses) {
            QJsonObject analysis = val.toObject();
            AnalysisEntry entry;
            entry.type = analysis["type"].toString();
            entry.content = analysis["content"].toString();
            entry.functionName = analysis["functionName"].toString();
            entry.address = analysis["address"].toString().toULongLong(nullptr, 16);
            entry.confidence = analysis["confidence"].toInt();
            entry.relatedFunctions = analysis["relatedFunctions"].toArray().toVariantList().toStringList();
            entry.references = analysis["references"].toArray().toVariantList().toStringList();
            entry.customData = analysis["customData"].toObject();
            msg->analysisEntries_.push_back(entry);
        }
    }
    
    // Attachments
    if (json.contains("attachments")) {
        QJsonArray atts = json["attachments"].toArray();
        for (const QJsonValue& val : atts) {
            QJsonObject att = val.toObject();
            MessageAttachment attachment;
            attachment.id = att["id"].toString();
            attachment.name = att["name"].toString();
            attachment.mimeType = att["mimeType"].toString();
            attachment.size = att["size"].toVariant().toLongLong();
            attachment.filePath = att["filePath"].toString();
            attachment.thumbnailPath = att["thumbnailPath"].toString();
            attachment.metadata = att["metadata"].toObject();
            msg->attachments_.push_back(attachment);
        }
    }
    
    // Replies
    if (json.contains("replies")) {
        QJsonArray reps = json["replies"].toArray();
        for (const QJsonValue& val : reps) {
            msg->replies_.push_back(QUuid::fromString(val.toString()));
        }
    }
    
    return msg;
}

QString Message::summary(int maxLength) const {
    QString text = content_;
    if (text.length() > maxLength) {
        text = text.left(maxLength - 3) + "...";
    }
    return text;
}

QString Message::roleString() const {
    switch (role_) {
        case MessageRole::User: return "User";
        case MessageRole::Assistant: return "Assistant";
        case MessageRole::System: return "System";
        case MessageRole::Tool: return "Tool";
        case MessageRole::Error: return "Error";
    }
    return "Unknown";
}

QString Message::typeString() const {
    switch (type_) {
        case MessageType::Text: return "Text";
        case MessageType::Code: return "Code";
        case MessageType::Analysis: return "Analysis";
        case MessageType::ToolExecution: return "Tool Execution";
        case MessageType::Error: return "Error";
        case MessageType::Info: return "Info";
        case MessageType::Warning: return "Warning";
    }
    return "Unknown";
}

QIcon Message::roleIcon() const {
    switch (role_) {
        case MessageRole::User:
            return ThemeManager::instance().themedIcon("user");
        case MessageRole::Assistant:
            return ThemeManager::instance().themedIcon("assistant");
        case MessageRole::System:
            return ThemeManager::instance().themedIcon("system");
        case MessageRole::Tool:
            return ThemeManager::instance().themedIcon("tool");
        case MessageRole::Error:
            return ThemeManager::instance().themedIcon("error");
    }
    return QIcon();
}

QColor Message::roleColor() const {
    const auto& colors = ThemeManager::instance().colors();
    switch (role_) {
        case MessageRole::User: return colors.userMessage;
        case MessageRole::Assistant: return colors.assistantMessage;
        case MessageRole::System: return colors.systemMessage;
        case MessageRole::Tool: return colors.info;
        case MessageRole::Error: return colors.error;
    }
    return colors.textPrimary;
}

// ConversationModel implementation

ConversationModel::ConversationModel(QObject* parent)
    : QAbstractItemModel(parent) {
}

ConversationModel::~ConversationModel() = default;

QModelIndex ConversationModel::index(int row, int column, const QModelIndex& parent) const {
    if (!hasIndex(row, column, parent)) {
        return QModelIndex();
    }
    
    if (!parent.isValid()) {
        // Root level
        if (row < visibleNodes_.size()) {
            return createIndex(row, column, visibleNodes_[row]);
        }
    } else {
        // Child of a thread - we flatten the view so this shouldn't happen
        // unless we're in tree mode
    }
    
    return QModelIndex();
}

QModelIndex ConversationModel::parent(const QModelIndex& child) const {
    // We use a flat list view, so no parents
    return QModelIndex();
}

int ConversationModel::rowCount(const QModelIndex& parent) const {
    if (!parent.isValid()) {
        return visibleNodes_.size();
    }
    return 0;
}

int ConversationModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent)
    return ColumnCount;
}

QVariant ConversationModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= visibleNodes_.size()) {
        return QVariant();
    }
    
    MessageNode* node = static_cast<MessageNode*>(index.internalPointer());
    if (!node || !node->message) {
        return QVariant();
    }
    
    const Message* msg = node->message.get();
    
    switch (role) {
        case Qt::DisplayRole:
            if (index.column() == ContentColumn) {
                return msg->summary();
            } else if (index.column() == RoleColumn) {
                return msg->roleString();
            } else if (index.column() == TimestampColumn) {
                return msg->metadata().timestamp.toString("hh:mm:ss");
            } else if (index.column() == StatusColumn) {
                if (msg->hasToolExecution()) {
                    switch (msg->toolExecution()->state) {
                        case ToolExecutionState::Running: return "Running...";
                        case ToolExecutionState::Completed: return "Completed";
                        case ToolExecutionState::Failed: return "Failed";
                        default: return "";
                    }
                }
            }
            break;
            
        case Qt::ToolTipRole:
            if (index.column() == ContentColumn) {
                return msg->content();
            }
            break;
            
        case MessageRole:
            return QVariant::fromValue(static_cast<int>(msg->role()));
            
        case MessageTypeRole:
            return QVariant::fromValue(static_cast<int>(msg->type()));
            
        case MessageIdRole:
            return msg->id();
            
        case MessageObjectRole:
            return QVariant::fromValue(const_cast<Message*>(msg));
            
        case ToolExecutionRole:
            return QVariant::fromValue(msg->toolExecution());
            
        case AnalysisRole:
            return QVariant::fromValue(&msg->analysisEntries());
            
        case AttachmentsRole:
            return QVariant::fromValue(&msg->attachments());
            
        case MetadataRole:
            return QVariant::fromValue(&msg->metadata());
            
        case SearchMatchRole:
            return searchMatches_.contains(msg->id());
            
        case ThreadDepthRole:
            return node->threadDepth;
            
        case IsEditedRole:
            return msg->metadata().isEdited;
            
        case IsPinnedRole:
            return msg->metadata().isPinned;
            
        case IsBookmarkedRole:
            return msg->metadata().isBookmarked;
            
        case HasRepliesRole:
            return msg->hasReplies();
            
        case ReactionCountRole:
            return msg->metadata().reactions.count();
            
        case ProgressRole:
            if (msg->hasToolExecution()) {
                return msg->toolExecution()->progressValue;
            }
            break;
    }
    
    return QVariant();
}

QVariant ConversationModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
            case ContentColumn: return "Message";
            case RoleColumn: return "Role";
            case TimestampColumn: return "Time";
            case StatusColumn: return "Status";
        }
    }
    return QVariant();
}

Qt::ItemFlags ConversationModel::flags(const QModelIndex& index) const {
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }
    
    Qt::ItemFlags flags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    
    // Allow editing of user messages
    MessageNode* node = static_cast<MessageNode*>(index.internalPointer());
    if (node && node->message && node->message->role() == MessageRole::User) {
        flags |= Qt::ItemIsEditable;
    }
    
    return flags;
}

bool ConversationModel::setData(const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid() || role != Qt::EditRole) {
        return false;
    }
    
    MessageNode* node = static_cast<MessageNode*>(index.internalPointer());
    if (!node || !node->message) {
        return false;
    }
    
    if (index.column() == ContentColumn) {
        node->message->setContent(value.toString());
        emit dataChanged(index, index);
        emit messageUpdated(node->message->id());
        return true;
    }
    
    return false;
}

void ConversationModel::addMessage(std::unique_ptr<Message> message) {
    if (batchUpdateCount_ > 0) {
        // Queue for batch update
        auto node = std::make_unique<MessageNode>();
        node->message = std::move(message);
        nodes_.push_back(std::move(node));
        nodeMap_[nodes_.back()->message->id()] = nodes_.back().get();
        return;
    }
    
    beginInsertRows(QModelIndex(), visibleNodes_.size(), visibleNodes_.size());
    
    auto node = std::make_unique<MessageNode>();
    QUuid id = message->id();
    node->message = std::move(message);
    
    // Handle threading
    if (!node->message->metadata().parentId.isNull()) {
        MessageNode* parent = findNode(node->message->metadata().parentId);
        if (parent) {
            node->parent = parent;
            parent->children.push_back(node.get());
            parent->message->addReply(id);
        }
    } else {
        roots_.push_back(node.get());
    }
    
    nodeMap_[id] = node.get();
    nodes_.push_back(std::move(node));
    
    buildThreadTree();
    applyFilters();
    
    endInsertRows();
    
    emit messageAdded(id);
    statsCacheDirty_ = true;
}

void ConversationModel::insertMessage(int index, std::unique_ptr<Message> message) {
    if (index < 0 || index > visibleNodes_.size()) {
        addMessage(std::move(message));
        return;
    }
    
    beginInsertRows(QModelIndex(), index, index);
    
    auto node = std::make_unique<MessageNode>();
    QUuid id = message->id();
    node->message = std::move(message);
    
    nodeMap_[id] = node.get();
    nodes_.insert(nodes_.begin() + index, std::move(node));
    
    buildThreadTree();
    applyFilters();
    
    endInsertRows();
    
    emit messageAdded(id);
    statsCacheDirty_ = true;
}

void ConversationModel::removeMessage(const QUuid& id) {
    MessageNode* node = findNode(id);
    if (!node) return;
    
    int row = -1;
    for (int i = 0; i < visibleNodes_.size(); ++i) {
        if (visibleNodes_[i] == node) {
            row = i;
            break;
        }
    }
    
    if (row >= 0) {
        beginRemoveRows(QModelIndex(), row, row);
    }
    
    // Remove from parent's children
    if (node->parent) {
        auto& siblings = node->parent->children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), node), siblings.end());
    } else {
        roots_.erase(std::remove(roots_.begin(), roots_.end(), node), roots_.end());
    }
    
    // Remove from maps
    nodeMap_.erase(id);
    
    // Remove from storage
    nodes_.erase(
        std::remove_if(nodes_.begin(), nodes_.end(),
                      [id](const std::unique_ptr<MessageNode>& n) {
                          return n->message && n->message->id() == id;
                      }),
        nodes_.end()
    );
    
    buildThreadTree();
    applyFilters();
    
    if (row >= 0) {
        endRemoveRows();
    }
    
    emit messageRemoved(id);
    statsCacheDirty_ = true;
}

void ConversationModel::updateMessage(const QUuid& id, const QString& newContent) {
    MessageNode* node = findNode(id);
    if (!node || !node->message) return;
    
    node->message->setContent(newContent);
    emitDataChangedForMessage(id);
    emit messageUpdated(id);
    statsCacheDirty_ = true;
}

void ConversationModel::clearMessages() {
    beginResetModel();
    
    nodes_.clear();
    nodeMap_.clear();
    visibleNodes_.clear();
    roots_.clear();
    searchMatches_.clear();
    
    endResetModel();
    
    emit conversationCleared();
    statsCacheDirty_ = true;
}

Message* ConversationModel::getMessage(const QUuid& id) {
    MessageNode* node = findNode(id);
    return node ? node->message.get() : nullptr;
}

const Message* ConversationModel::getMessage(const QUuid& id) const {
    MessageNode* node = findNode(id);
    return node ? node->message.get() : nullptr;
}

Message* ConversationModel::getMessageAt(int index) {
    if (index >= 0 && index < visibleNodes_.size()) {
        return visibleNodes_[index]->message.get();
    }
    return nullptr;
}

const Message* ConversationModel::getMessageAt(int index) const {
    if (index >= 0 && index < visibleNodes_.size()) {
        return visibleNodes_[index]->message.get();
    }
    return nullptr;
}

int ConversationModel::getMessageIndex(const QUuid& id) const {
    for (int i = 0; i < visibleNodes_.size(); ++i) {
        if (visibleNodes_[i]->message && visibleNodes_[i]->message->id() == id) {
            return i;
        }
    }
    return -1;
}

void ConversationModel::addMessages(std::vector<std::unique_ptr<Message>> messages) {
    beginBatchUpdate();
    
    for (auto& msg : messages) {
        addMessage(std::move(msg));
    }
    
    endBatchUpdate();
}

void ConversationModel::removeMessages(const QSet<QUuid>& ids) {
    beginBatchUpdate();
    
    for (const QUuid& id : ids) {
        removeMessage(id);
    }
    
    endBatchUpdate();
}

void ConversationModel::updateToolExecution(const QUuid& messageId,
                                          const std::function<void(ToolExecution*)>& updater) {
    Message* msg = getMessage(messageId);
    if (!msg || !msg->toolExecution()) return;
    
    updater(msg->toolExecution());
    emitDataChangedForMessage(messageId);
    
    if (msg->toolExecution()->state == ToolExecutionState::Running) {
        emit toolExecutionStarted(messageId);
    } else if (msg->toolExecution()->state == ToolExecutionState::Completed ||
               msg->toolExecution()->state == ToolExecutionState::Failed) {
        emit toolExecutionCompleted(messageId, 
                                  msg->toolExecution()->state == ToolExecutionState::Completed);
    }
    
    statsCacheDirty_ = true;
}

void ConversationModel::setToolExecutionState(const QUuid& messageId, ToolExecutionState state) {
    updateToolExecution(messageId, [state](ToolExecution* exec) {
        exec->state = state;
        if (state == ToolExecutionState::Running) {
            exec->startTime = QDateTime::currentDateTime();
        } else if (state == ToolExecutionState::Completed || state == ToolExecutionState::Failed) {
            exec->endTime = QDateTime::currentDateTime();
            exec->duration = exec->startTime.msecsTo(exec->endTime);
        }
    });
}

void ConversationModel::setToolExecutionProgress(const QUuid& messageId, int value, const QString& text) {
    updateToolExecution(messageId, [value, text](ToolExecution* exec) {
        exec->progressValue = value;
        if (!text.isEmpty()) {
            exec->progressText = text;
        }
    });
    emit toolExecutionProgress(messageId, value);
}

void ConversationModel::addToolExecutionOutput(const QUuid& messageId, const QString& output) {
    updateToolExecution(messageId, [output](ToolExecution* exec) {
        exec->output += output;
    });
}

void ConversationModel::addReply(const QUuid& parentId, std::unique_ptr<Message> reply) {
    reply->metadata().parentId = parentId;
    addMessage(std::move(reply));
}

std::vector<const Message*> ConversationModel::getThread(const QUuid& rootId) const {
    std::vector<const Message*> thread;
    MessageNode* root = findNode(rootId);
    if (!root) return thread;
    
    std::function<void(MessageNode*)> collectThread = [&](MessageNode* node) {
        thread.push_back(node->message.get());
        for (MessageNode* child : node->children) {
            collectThread(child);
        }
    };
    
    collectThread(root);
    return thread;
}

void ConversationModel::collapseThread(const QUuid& rootId) {
    MessageNode* node = findNode(rootId);
    if (!node || node->children.empty()) return;
    
    node->collapsed = true;
    buildThreadTree();
    applyFilters();
    
    emit threadCollapsed(rootId);
}

void ConversationModel::expandThread(const QUuid& rootId) {
    MessageNode* node = findNode(rootId);
    if (!node) return;
    
    node->collapsed = false;
    buildThreadTree();
    applyFilters();
    
    emit threadExpanded(rootId);
}

bool ConversationModel::isThreadCollapsed(const QUuid& rootId) const {
    MessageNode* node = findNode(rootId);
    return node ? node->collapsed : false;
}

void ConversationModel::setSearchFilter(const QString& searchText) {
    if (searchFilter_ == searchText) return;
    
    searchFilter_ = searchText;
    applyFilters();
    emit filtersChanged();
}

void ConversationModel::setRoleFilter(const QSet<MessageRole>& roles) {
    roleFilter_ = roles;
    applyFilters();
    emit filtersChanged();
}

void ConversationModel::setTypeFilter(const QSet<MessageType>& types) {
    typeFilter_ = types;
    applyFilters();
    emit filtersChanged();
}

void ConversationModel::setDateRangeFilter(const QDateTime& start, const QDateTime& end) {
    dateRangeStart_ = start;
    dateRangeEnd_ = end;
    applyFilters();
    emit filtersChanged();
}

void ConversationModel::clearFilters() {
    searchFilter_.clear();
    roleFilter_.clear();
    typeFilter_.clear();
    dateRangeStart_ = QDateTime();
    dateRangeEnd_ = QDateTime();
    applyFilters();
    emit filtersChanged();
}

void ConversationModel::setPinned(const QUuid& id, bool pinned) {
    Message* msg = getMessage(id);
    if (!msg) return;
    
    msg->metadata().isPinned = pinned;
    emitDataChangedForMessage(id);
}

void ConversationModel::setBookmarked(const QUuid& id, bool bookmarked) {
    Message* msg = getMessage(id);
    if (!msg) return;
    
    msg->metadata().isBookmarked = bookmarked;
    emitDataChangedForMessage(id);
}

std::vector<const Message*> ConversationModel::getPinnedMessages() const {
    std::vector<const Message*> pinned;
    for (const auto& node : nodes_) {
        if (node->message && node->message->metadata().isPinned) {
            pinned.push_back(node->message.get());
        }
    }
    return pinned;
}

std::vector<const Message*> ConversationModel::getBookmarkedMessages() const {
    std::vector<const Message*> bookmarked;
    for (const auto& node : nodes_) {
        if (node->message && node->message->metadata().isBookmarked) {
            bookmarked.push_back(node->message.get());
        }
    }
    return bookmarked;
}

void ConversationModel::addReaction(const QUuid& id, const QString& reaction) {
    Message* msg = getMessage(id);
    if (!msg) return;
    
    if (!msg->metadata().reactions.contains(reaction)) {
        msg->metadata().reactions.append(reaction);
        emitDataChangedForMessage(id);
    }
}

void ConversationModel::removeReaction(const QUuid& id, const QString& reaction) {
    Message* msg = getMessage(id);
    if (!msg) return;
    
    msg->metadata().reactions.removeAll(reaction);
    emitDataChangedForMessage(id);
}

QString ConversationModel::exportToMarkdown(bool includeMetadata) const {
    QString markdown;
    
    for (const auto& node : visibleNodes_) {
        const Message* msg = node->message.get();
        if (!msg) continue;
        
        // Add thread indentation
        for (int i = 0; i < node->threadDepth; ++i) {
            markdown += "  ";
        }
        
        // Role prefix
        markdown += QString("**%1**: ").arg(msg->roleString());
        
        // Content
        markdown += msg->content() + "\n";
        
        // Metadata
        if (includeMetadata) {
            markdown += QString("*%1*\n").arg(
                msg->metadata().timestamp.toString("yyyy-MM-dd hh:mm:ss"));
            
            if (msg->metadata().isEdited) {
                markdown += QString("*(edited %1)*\n").arg(
                    msg->metadata().editedAt.toString("yyyy-MM-dd hh:mm:ss"));
            }
            
            if (!msg->metadata().tags.isEmpty()) {
                markdown += "Tags: " + msg->metadata().tags.join(", ") + "\n";
            }
        }
        
        // Tool execution
        if (msg->hasToolExecution()) {
            const ToolExecution* exec = msg->toolExecution();
            markdown += QString("```\nTool: %1\nStatus: %2\n")
                .arg(exec->toolName)
                .arg(exec->state == ToolExecutionState::Completed ? "Success" : "Failed");
            
            if (!exec->output.isEmpty()) {
                markdown += "Output:\n" + exec->output + "\n";
            }
            if (!exec->error.isEmpty()) {
                markdown += "Error:\n" + exec->error + "\n";
            }
            markdown += "```\n";
        }
        
        // Analysis entries
        if (msg->hasAnalysis()) {
            for (const auto& entry : msg->analysisEntries()) {
                markdown += QString("\n**%1**: %2\n")
                    .arg(entry.type)
                    .arg(entry.content);
                
                if (!entry.functionName.isEmpty()) {
                    markdown += QString("Function: %1 @ 0x%2\n")
                        .arg(entry.functionName)
                        .arg(entry.address, 0, 16);
                }
            }
        }
        
        markdown += "\n---\n\n";
    }
    
    return markdown;
}

QString ConversationModel::exportToHtml(bool includeStyles) const {
    QString html;
    
    if (includeStyles) {
        html = R"(
<!DOCTYPE html>
<html>
<head>
<style>
    body { font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }
    .message { margin: 10px 0; padding: 10px; border-radius: 8px; }
    .user { background: #e3f2fd; margin-left: 20%; }
    .assistant { background: #f5f5f5; margin-right: 20%; }
    .system { background: #fff3e0; font-style: italic; }
    .tool { background: #e8f5e9; font-family: monospace; }
    .error { background: #ffebee; color: #c62828; }
    .metadata { font-size: 0.8em; color: #666; margin-top: 5px; }
    .analysis { background: #f3e5f5; padding: 5px; margin: 5px 0; }
    pre { background: #263238; color: #aed581; padding: 10px; overflow-x: auto; }
</style>
</head>
<body>
)";
    }
    
    for (const auto& node : visibleNodes_) {
        const Message* msg = node->message.get();
        if (!msg) continue;
        
        QString roleClass = msg->roleString().toLower();
        html += QString("<div class='message %1'>").arg(roleClass);
        
        // Content
        html += QString("<strong>%1:</strong> %2")
            .arg(msg->roleString())
            .arg(msg->htmlContent().isEmpty() ? 
                 UIUtils::escapeHtml(msg->content()) : msg->htmlContent());
        
        // Metadata
        html += QString("<div class='metadata'>%1</div>")
            .arg(msg->metadata().timestamp.toString("yyyy-MM-dd hh:mm:ss"));
        
        // Tool execution
        if (msg->hasToolExecution()) {
            const ToolExecution* exec = msg->toolExecution();
            html += "<pre>";
            html += UIUtils::escapeHtml(QString("Tool: %1\nStatus: %2\n")
                .arg(exec->toolName)
                .arg(exec->state == ToolExecutionState::Completed ? "Success" : "Failed"));
            
            if (!exec->output.isEmpty()) {
                html += UIUtils::escapeHtml("Output:\n" + exec->output);
            }
            html += "</pre>";
        }
        
        // Analysis entries
        if (msg->hasAnalysis()) {
            for (const auto& entry : msg->analysisEntries()) {
                html += QString("<div class='analysis'><strong>%1:</strong> %2</div>")
                    .arg(entry.type)
                    .arg(UIUtils::escapeHtml(entry.content));
            }
        }
        
        html += "</div>";
    }
    
    if (includeStyles) {
        html += "</body></html>";
    }
    
    return html;
}

QJsonDocument ConversationModel::exportToJson() const {
    QJsonArray messages;
    
    for (const auto& node : nodes_) {
        if (node->message) {
            messages.append(node->message->toJson());
        }
    }
    
    QJsonObject root;
    root["version"] = 1;
    root["exportDate"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["messages"] = messages;
    root["messageCount"] = messages.size();
    
    return QJsonDocument(root);
}

void ConversationModel::importFromJson(const QJsonDocument& doc, bool append) {
    if (!doc.isObject()) return;
    
    QJsonObject root = doc.object();
    if (!root.contains("messages")) return;
    
    if (!append) {
        clearMessages();
    }
    
    beginBatchUpdate();
    
    QJsonArray messages = root["messages"].toArray();
    for (const QJsonValue& val : messages) {
        if (val.isObject()) {
            auto msg = Message::fromJson(val.toObject());
            if (msg) {
                addMessage(std::move(msg));
            }
        }
    }
    
    endBatchUpdate();
}

ConversationModel::ConversationStats ConversationModel::getStatistics() const {
    if (!statsCacheDirty_ && statsCache_.contains(QUuid())) {
        return statsCache_[QUuid()];
    }
    
    ConversationStats stats;
    
    for (const auto& node : nodes_) {
        const Message* msg = node->message.get();
        if (!msg) continue;
        
        stats.totalMessages++;
        
        // Count by role
        switch (msg->role()) {
            case MessageRole::User:
                stats.userMessages++;
                break;
            case MessageRole::Assistant:
                stats.assistantMessages++;
                break;
            default:
                break;
        }
        
        // Tool executions
        if (msg->hasToolExecution()) {
            stats.toolExecutions++;
            const ToolExecution* exec = msg->toolExecution();
            
            if (exec->state == ToolExecutionState::Completed) {
                stats.successfulTools++;
            } else if (exec->state == ToolExecutionState::Failed) {
                stats.failedTools++;
            }
            
            stats.toolUsageCount[exec->toolName]++;
            stats.totalToolDuration += exec->duration;
        }
        
        // Analysis entries
        if (msg->hasAnalysis()) {
            stats.totalAnalyses += msg->analysisEntries().size();
            for (const auto& entry : msg->analysisEntries()) {
                stats.analysisByType[entry.type]++;
            }
        }
        
        // Timestamps
        if (!stats.firstMessage.isValid() || msg->metadata().timestamp < stats.firstMessage) {
            stats.firstMessage = msg->metadata().timestamp;
        }
        if (!stats.lastMessage.isValid() || msg->metadata().timestamp > stats.lastMessage) {
            stats.lastMessage = msg->metadata().timestamp;
        }
    }
    
    const_cast<ConversationModel*>(this)->statsCache_[QUuid()] = stats;
    const_cast<ConversationModel*>(this)->statsCacheDirty_ = false;
    
    return stats;
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
            buildThreadTree();
            applyFilters();
            endResetModel();
            emit statisticsChanged();
        }
    }
}

bool ConversationModel::canUndo() const {
    return undoStack_ && undoStack_->canUndo();
}

bool ConversationModel::canRedo() const {
    return undoStack_ && undoStack_->canRedo();
}

void ConversationModel::undo() {
    if (undoStack_) {
        undoStack_->undo();
    }
}

void ConversationModel::redo() {
    if (undoStack_) {
        undoStack_->redo();
    }
}

void ConversationModel::buildThreadTree() {
    // Clear existing tree structure
    roots_.clear();
    for (auto& node : nodes_) {
        node->children.clear();
        node->parent = nullptr;
    }
    
    // Build parent-child relationships
    for (auto& node : nodes_) {
        if (!node->message) continue;
        
        if (node->message->metadata().parentId.isNull()) {
            roots_.push_back(node.get());
        } else {
            MessageNode* parent = findNode(node->message->metadata().parentId);
            if (parent) {
                node->parent = parent;
                parent->children.push_back(node.get());
            } else {
                // Orphaned message, treat as root
                roots_.push_back(node.get());
            }
        }
    }
    
    // Sort roots by timestamp
    std::sort(roots_.begin(), roots_.end(), [](MessageNode* a, MessageNode* b) {
        return a->message->metadata().timestamp < b->message->metadata().timestamp;
    });
    
    // Update thread depths
    for (MessageNode* root : roots_) {
        updateThreadDepths(root, 0);
    }
}

void ConversationModel::updateThreadDepths(MessageNode* node, int depth) {
    if (!node) return;
    
    node->threadDepth = depth;
    
    for (MessageNode* child : node->children) {
        updateThreadDepths(child, depth + 1);
    }
}

void ConversationModel::applyFilters() {
    visibleNodes_.clear();
    searchMatches_.clear();
    
    // Collect all nodes that match filters
    for (MessageNode* root : roots_) {
        collectVisibleNodes(root, visibleNodes_);
    }
    
    // Update search matches
    if (!searchFilter_.isEmpty()) {
        int matchCount = 0;
        for (MessageNode* node : visibleNodes_) {
            if (node->message && node->message->matchesSearch(searchFilter_)) {
                searchMatches_.insert(node->message->id());
                matchCount++;
            }
        }
        emit searchMatchesChanged(matchCount);
    } else {
        emit searchMatchesChanged(0);
    }
}

bool ConversationModel::messageMatchesFilters(const Message* msg) const {
    if (!msg) return false;
    
    // Role filter
    if (!roleFilter_.isEmpty() && !roleFilter_.contains(msg->role())) {
        return false;
    }
    
    // Type filter
    if (!typeFilter_.isEmpty() && !typeFilter_.contains(msg->type())) {
        return false;
    }
    
    // Date range filter
    if (dateRangeStart_.isValid() && msg->metadata().timestamp < dateRangeStart_) {
        return false;
    }
    if (dateRangeEnd_.isValid() && msg->metadata().timestamp > dateRangeEnd_) {
        return false;
    }
    
    // Search filter
    if (!searchFilter_.isEmpty() && !msg->matchesSearch(searchFilter_)) {
        return false;
    }
    
    return true;
}

ConversationModel::MessageNode* ConversationModel::findNode(const QUuid& id) const {
    auto it = nodeMap_.find(id);
    return it != nodeMap_.end() ? it->second : nullptr;
}

void ConversationModel::collectVisibleNodes(MessageNode* node, std::vector<MessageNode*>& visible) const {
    if (!node || !node->message) return;
    
    node->matchesFilter = messageMatchesFilters(node->message.get());
    
    if (node->matchesFilter) {
        visible.push_back(node);
    }
    
    // Include children if not collapsed
    if (!node->collapsed) {
        for (MessageNode* child : node->children) {
            collectVisibleNodes(child, visible);
        }
    }
}

void ConversationModel::emitDataChangedForMessage(const QUuid& id) {
    QModelIndex idx = indexForMessage(id);
    if (idx.isValid()) {
        emit dataChanged(idx, index(idx.row(), ColumnCount - 1));
    }
}

QModelIndex ConversationModel::indexForMessage(const QUuid& id) const {
    int row = getMessageIndex(id);
    if (row >= 0) {
        return index(row, 0);
    }
    return QModelIndex();
}

// ConversationDelegate implementation

ConversationDelegate::ConversationDelegate(QObject* parent)
    : QStyledItemDelegate(parent) {
}

void ConversationDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
    if (!index.isValid()) return;
    
    const Message* msg = index.data(ConversationModel::MessageObjectRole).value<Message*>();
    if (!msg) return;
    
    painter->save();
    
    // Draw background
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    }
    
    // Draw thread indicator
    int threadDepth = index.data(ConversationModel::ThreadDepthRole).toInt();
    bool hasReplies = index.data(ConversationModel::HasRepliesRole).toBool();
    if (threadDepth > 0 || hasReplies) {
        drawThreadIndicator(painter, option.rect, threadDepth, hasReplies);
    }
    
    // Calculate bubble rect
    QRect bubbleRect = option.rect.adjusted(
        Design::SPACING_MD + threadDepth * Design::SPACING_LG,
        Design::SPACING_SM,
        -Design::SPACING_MD,
        -Design::SPACING_SM
    );
    
    // Limit bubble width
    if (bubbleRect.width() > maxBubbleWidth_) {
        if (msg->role() == MessageRole::User) {
            bubbleRect.setLeft(bubbleRect.right() - maxBubbleWidth_);
        } else {
            bubbleRect.setRight(bubbleRect.left() + maxBubbleWidth_);
        }
    }
    
    // Store bubble rect for hit testing
    bubbleRects_[msg->id()] = bubbleRect;
    
    // Draw message bubble
    drawMessageBubble(painter, option, msg, option.state & QStyle::State_Selected);
    
    // Draw tool execution if present
    if (msg->hasToolExecution()) {
        QRect toolRect = bubbleRect.adjusted(0, bubbleRect.height() + Design::SPACING_SM, 0, 0);
        drawToolExecution(painter, toolRect, msg->toolExecution());
    }
    
    // Draw analysis entries if present
    if (msg->hasAnalysis()) {
        QRect analysisRect = bubbleRect.adjusted(0, bubbleRect.height() + Design::SPACING_SM, 0, 0);
        drawAnalysisEntries(painter, analysisRect, msg->analysisEntries());
    }
    
    // Draw attachments if present
    if (msg->hasAttachments()) {
        QRect attachRect = bubbleRect.adjusted(0, bubbleRect.height() + Design::SPACING_SM, 0, 0);
        drawAttachments(painter, attachRect, msg->attachments());
    }
    
    // Draw reactions
    if (!msg->metadata().reactions.isEmpty()) {
        QRect reactRect = bubbleRect.adjusted(0, bubbleRect.height() + Design::SPACING_XS, 0, 0);
        drawReactions(painter, reactRect, msg->metadata().reactions);
    }
    
    painter->restore();
}

QSize ConversationDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
    if (!index.isValid()) return QSize();
    
    const Message* msg = index.data(ConversationModel::MessageObjectRole).value<Message*>();
    if (!msg) return QSize();
    
    // Calculate text size
    QTextDocument doc;
    doc.setDefaultFont(option.font);
    doc.setHtml(msg->htmlContent().isEmpty() ? 
                UIUtils::escapeHtml(msg->content()) : msg->htmlContent());
    doc.setTextWidth(maxBubbleWidth_ - 2 * Design::SPACING_MD);
    
    int height = doc.size().height() + 2 * Design::SPACING_MD;
    
    // Add space for metadata
    if (showTimestamps_) {
        height += option.fontMetrics.height() + Design::SPACING_XS;
    }
    
    // Add space for tool execution
    if (msg->hasToolExecution()) {
        height += 100; // Approximate height for tool execution display
    }
    
    // Add space for analysis entries
    if (msg->hasAnalysis()) {
        height += msg->analysisEntries().size() * 60; // Approximate height per entry
    }
    
    // Add space for attachments
    if (msg->hasAttachments()) {
        height += 80; // Approximate height for attachment preview
    }
    
    // Add space for reactions
    if (!msg->metadata().reactions.isEmpty()) {
        height += 30;
    }
    
    // Add vertical spacing
    height += 2 * Design::SPACING_SM;
    
    if (compactMode_) {
        height = height * 0.8; // Reduce height in compact mode
    }
    
    return QSize(option.rect.width(), height);
}

bool ConversationDelegate::editorEvent(QEvent* event, QAbstractItemModel* model,
                                     const QStyleOptionViewItem& option,
                                     const QModelIndex& index) {
    if (event->type() == QEvent::MouseButtonPress) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        
        const Message* msg = index.data(ConversationModel::MessageObjectRole).value<Message*>();
        if (!msg) return false;
        
        QRect hitRect = hitTest(mouseEvent->pos(), option, index);
        QString hitArea = "";
        
        // Check what was clicked
        auto hitAreas = hitAreas_[msg->id()];
        for (auto it = hitAreas.begin(); it != hitAreas.end(); ++it) {
            if (it.value().contains(mouseEvent->pos())) {
                hitArea = it.key();
                break;
            }
        }
        
        if (hitArea.startsWith("reaction:")) {
            QString reaction = hitArea.mid(9);
            emit reactionClicked(msg->id(), reaction);
            return true;
        } else if (hitArea == "reply") {
            emit replyRequested(msg->id());
            return true;
        } else if (hitArea.startsWith("attachment:")) {
            QString attachmentId = hitArea.mid(11);
            emit attachmentClicked(msg->id(), attachmentId);
            return true;
        } else if (hitArea == "toolOutput") {
            emit toolOutputToggled(msg->id());
            return true;
        }
    } else if (event->type() == QEvent::MouseMove) {
        QMouseEvent* mouseEvent = static_cast<QMouseEvent*>(event);
        
        // Check for links
        const Message* msg = index.data(ConversationModel::MessageObjectRole).value<Message*>();
        if (msg) {
            QTextDocument doc;
            doc.setHtml(msg->htmlContent().isEmpty() ? 
                       UIUtils::escapeHtml(msg->content()) : msg->htmlContent());
            
            QString anchor = doc.documentLayout()->anchorAt(mouseEvent->pos());
            if (!anchor.isEmpty()) {
                QToolTip::showText(mouseEvent->globalPosition().toPoint(), anchor);
            }
        }
    }
    
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

void ConversationDelegate::drawMessageBubble(QPainter* painter, const QStyleOptionViewItem& option,
                                           const Message* message, bool isSelected) const {
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    
    QRect bubbleRect = bubbleRects_[message->id()];
    
    // Draw bubble background
    QColor bubbleColor = message->roleColor();
    if (isSelected) {
        bubbleColor = ThemeManager::mix(bubbleColor, colors.selection, 0.3);
    }
    
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(bubbleColor);
    painter->drawRoundedRect(bubbleRect, Design::RADIUS_MD, Design::RADIUS_MD);
    
    // Draw avatar if enabled
    if (showAvatars_) {
        QRect avatarRect(bubbleRect.left() - 40, bubbleRect.top(), 32, 32);
        if (message->role() == MessageRole::User) {
            avatarRect.moveLeft(bubbleRect.right() + 8);
        }
        
        painter->setBrush(ThemeManager::darken(bubbleColor, 20));
        painter->drawEllipse(avatarRect);
        
        // Draw initial
        painter->setPen(colors.textInverse);
        painter->setFont(theme.typography().body);
        painter->drawText(avatarRect, Qt::AlignCenter, 
                         message->roleString().left(1).toUpper());
    }
    
    // Draw content
    QRect contentRect = bubbleRect.adjusted(
        Design::SPACING_MD, Design::SPACING_MD,
        -Design::SPACING_MD, -Design::SPACING_MD
    );
    
    painter->setPen(colors.textPrimary);
    painter->setFont(theme.typography().body);
    
    QTextDocument doc;
    doc.setDefaultFont(theme.typography().body);
    doc.setHtml(message->htmlContent().isEmpty() ? 
                UIUtils::escapeHtml(message->content()) : message->htmlContent());
    doc.setTextWidth(contentRect.width());
    
    painter->translate(contentRect.topLeft());
    doc.drawContents(painter);
    painter->translate(-contentRect.topLeft());
    
    // Draw metadata
    if (showTimestamps_) {
        QString timeStr = UIUtils::formatRelativeTime(message->metadata().timestamp);
        QRect timeRect = bubbleRect.adjusted(
            Design::SPACING_MD, -Design::SPACING_MD - option.fontMetrics.height(),
            -Design::SPACING_MD, -Design::SPACING_MD
        );
        timeRect.moveTop(bubbleRect.bottom() - timeRect.height());
        
        painter->setPen(colors.textTertiary);
        painter->setFont(theme.typography().caption);
        painter->drawText(timeRect, Qt::AlignRight | Qt::AlignBottom, timeStr);
    }
    
    // Draw status indicators
    if (message->metadata().isPinned) {
        // Draw pin icon
        QRect pinRect(bubbleRect.right() - 20, bubbleRect.top() + 4, 16, 16);
        painter->setPen(colors.primary);
        painter->drawText(pinRect, Qt::AlignCenter, "📌");
    }
    
    if (message->metadata().isEdited) {
        // Draw edited indicator
        QString editedStr = "(edited)";
        QRect editRect = bubbleRect.adjusted(
            Design::SPACING_MD, -Design::SPACING_MD - option.fontMetrics.height(),
            -Design::SPACING_MD, -Design::SPACING_MD
        );
        editRect.moveTop(bubbleRect.bottom() - editRect.height());
        
        painter->setPen(colors.textTertiary);
        painter->setFont(theme.typography().caption);
        painter->drawText(editRect, Qt::AlignLeft | Qt::AlignBottom, editedStr);
    }
}

void ConversationDelegate::drawToolExecution(QPainter* painter, const QRect& rect,
                                           const ToolExecution* execution) const {
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    
    // Draw tool execution card
    QRect cardRect = rect.adjusted(0, 0, 0, 80);
    painter->setPen(Qt::NoPen);
    painter->setBrush(colors.surface);
    painter->drawRoundedRect(cardRect, Design::RADIUS_SM, Design::RADIUS_SM);
    
    // Draw border
    painter->setPen(QPen(colors.border, 1));
    painter->setBrush(Qt::NoBrush);
    painter->drawRoundedRect(cardRect, Design::RADIUS_SM, Design::RADIUS_SM);
    
    // Draw tool name and status
    QRect headerRect = cardRect.adjusted(Design::SPACING_SM, Design::SPACING_SM,
                                       -Design::SPACING_SM, 0);
    headerRect.setHeight(20);
    
    painter->setPen(colors.textPrimary);
    painter->setFont(theme.typography().subtitle);
    painter->drawText(headerRect, Qt::AlignLeft, execution->toolName);
    
    // Draw status
    QColor statusColor;
    QString statusText;
    switch (execution->state) {
        case ToolExecutionState::Pending:
            statusColor = colors.textTertiary;
            statusText = "Pending";
            break;
        case ToolExecutionState::Running:
            statusColor = colors.info;
            statusText = "Running...";
            break;
        case ToolExecutionState::Completed:
            statusColor = colors.success;
            statusText = "Completed";
            break;
        case ToolExecutionState::Failed:
            statusColor = colors.error;
            statusText = "Failed";
            break;
        case ToolExecutionState::Cancelled:
            statusColor = colors.warning;
            statusText = "Cancelled";
            break;
    }
    
    painter->setPen(statusColor);
    painter->drawText(headerRect, Qt::AlignRight, statusText);
    
    // Draw progress bar if running
    if (execution->state == ToolExecutionState::Running) {
        QRect progressRect = cardRect.adjusted(
            Design::SPACING_SM, headerRect.bottom() + Design::SPACING_XS,
            -Design::SPACING_SM, 0
        );
        progressRect.setHeight(4);
        
        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.border);
        painter->drawRoundedRect(progressRect, 2, 2);
        
        int progress = execution->progressValue;
        if (progress > 0) {
            QRect fillRect = progressRect;
            fillRect.setWidth(progressRect.width() * progress / 100);
            painter->setBrush(colors.primary);
            painter->drawRoundedRect(fillRect, 2, 2);
        }
        
        // Draw progress text
        if (!execution->progressText.isEmpty()) {
            QRect textRect = progressRect.adjusted(0, 6, 0, 20);
            painter->setPen(colors.textSecondary);
            painter->setFont(theme.typography().caption);
            painter->drawText(textRect, Qt::AlignLeft, execution->progressText);
        }
    }
    
    // Draw duration
    if (execution->duration > 0) {
        QString durationStr = UIUtils::formatDuration(
            std::chrono::milliseconds(execution->duration));
        QRect durationRect = cardRect.adjusted(
            Design::SPACING_SM, 0,
            -Design::SPACING_SM, -Design::SPACING_SM
        );
        
        painter->setPen(colors.textTertiary);
        painter->setFont(theme.typography().caption);
        painter->drawText(durationRect, Qt::AlignLeft | Qt::AlignBottom, durationStr);
    }
    
    // Store hit area for output toggle
    hitAreas_[execution->toolId]["toolOutput"] = cardRect;
}

void ConversationDelegate::drawAnalysisEntries(QPainter* painter, const QRect& rect,
                                             const std::vector<AnalysisEntry>& entries) const {
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    
    int y = rect.top();
    
    for (const auto& entry : entries) {
        QRect entryRect(rect.left(), y, rect.width(), 50);
        
        // Get color based on analysis type
        QColor typeColor;
        if (entry.type == "note") {
            typeColor = colors.analysisNote;
        } else if (entry.type == "finding") {
            typeColor = colors.analysisFinding;
        } else if (entry.type == "hypothesis") {
            typeColor = colors.analysisHypothesis;
        } else if (entry.type == "question") {
            typeColor = colors.analysisQuestion;
        } else if (entry.type == "analysis") {
            typeColor = colors.analysisAnalysis;
        } else if (entry.type == "deep_analysis") {
            typeColor = colors.analysisDeepAnalysis;
        } else {
            typeColor = colors.textSecondary;
        }
        
        // Draw type indicator
        QRect typeRect(entryRect.left(), entryRect.top(), 4, entryRect.height());
        painter->fillRect(typeRect, typeColor);
        
        // Draw content
        QRect contentRect = entryRect.adjusted(8, 4, -4, -4);
        painter->setPen(colors.textPrimary);
        painter->setFont(theme.typography().body);
        
        QString text = entry.content;
        if (text.length() > 100) {
            text = text.left(97) + "...";
        }
        painter->drawText(contentRect, Qt::AlignLeft | Qt::TextWordWrap, text);
        
        // Draw function info if present
        if (!entry.functionName.isEmpty()) {
            QString funcInfo = QString("%1 @ 0x%2")
                .arg(entry.functionName)
                .arg(entry.address, 0, 16);
            
            QRect funcRect = contentRect.adjusted(0, 30, 0, 0);
            painter->setPen(colors.textSecondary);
            painter->setFont(theme.typography().caption);
            painter->drawText(funcRect, Qt::AlignLeft, funcInfo);
        }
        
        y += entryRect.height() + Design::SPACING_XS;
    }
}

void ConversationDelegate::drawAttachments(QPainter* painter, const QRect& rect,
                                         const std::vector<MessageAttachment>& attachments) const {
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    
    int x = rect.left();
    
    for (const auto& attachment : attachments) {
        QRect attachRect(x, rect.top(), 100, 80);
        
        // Draw attachment card
        painter->setPen(QPen(colors.border, 1));
        painter->setBrush(colors.surface);
        painter->drawRoundedRect(attachRect, Design::RADIUS_SM, Design::RADIUS_SM);
        
        // Draw icon based on mime type
        QRect iconRect = attachRect.adjusted(0, 10, 0, -30);
        QString icon;
        if (attachment.mimeType.startsWith("image/")) {
            icon = "🏷";
        } else if (attachment.mimeType.startsWith("text/")) {
            icon = "📄";
        } else if (attachment.mimeType.startsWith("application/pdf")) {
            icon = "📕";
        } else {
            icon = "📎";
        }
        
        painter->setPen(colors.textPrimary);
        painter->setFont(QFont(theme.typography().body.family(), 24));
        painter->drawText(iconRect, Qt::AlignCenter, icon);
        
        // Draw name
        QRect nameRect = attachRect.adjusted(4, -25, -4, -4);
        painter->setPen(colors.textSecondary);
        painter->setFont(theme.typography().caption);
        
        QString name = attachment.name;
        if (name.length() > 12) {
            name = name.left(9) + "...";
        }
        painter->drawText(nameRect, Qt::AlignCenter | Qt::AlignBottom, name);
        
        // Store hit area
        hitAreas_[attachment.id][QString("attachment:%1").arg(attachment.id)] = attachRect;
        
        x += attachRect.width() + Design::SPACING_SM;
    }
}

void ConversationDelegate::drawReactions(QPainter* painter, const QRect& rect,
                                       const QStringList& reactions) const {
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    
    int x = rect.left();
    
    for (const QString& reaction : reactions) {
        QRect reactionRect(x, rect.top(), 40, 24);
        
        // Draw reaction bubble
        painter->setPen(QPen(colors.border, 1));
        painter->setBrush(colors.surfaceHover);
        painter->drawRoundedRect(reactionRect, 12, 12);
        
        // Draw reaction emoji
        painter->setPen(colors.textPrimary);
        painter->setFont(theme.typography().body);
        painter->drawText(reactionRect, Qt::AlignCenter, reaction);
        
        // Store hit area
        hitAreas_[reaction][QString("reaction:%1").arg(reaction)] = reactionRect;
        
        x += reactionRect.width() + Design::SPACING_XS;
    }
}

void ConversationDelegate::drawThreadIndicator(QPainter* painter, const QRect& rect,
                                             int depth, bool hasReplies) const {
    const auto& colors = ThemeManager::instance().colors();
    
    // Draw vertical lines for thread depth
    painter->setPen(QPen(colors.border, 1, Qt::DotLine));
    
    for (int i = 0; i < depth; ++i) {
        int x = rect.left() + Design::SPACING_SM + i * Design::SPACING_LG;
        painter->drawLine(x, rect.top(), x, rect.bottom());
    }
    
    // Draw reply indicator
    if (hasReplies) {
        int x = rect.left() + Design::SPACING_SM + depth * Design::SPACING_LG;
        int y = rect.center().y();
        
        painter->setPen(QPen(colors.primary, 2));
        painter->drawLine(x, y, x + 10, y);
        painter->drawLine(x + 7, y - 3, x + 10, y);
        painter->drawLine(x + 7, y + 3, x + 10, y);
    }
}

QRect ConversationDelegate::hitTest(const QPoint& pos, const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
    // This would implement precise hit testing for interactive elements
    // For now, return the full rect
    return option.rect;
}

} // namespace llm_re::ui_v2