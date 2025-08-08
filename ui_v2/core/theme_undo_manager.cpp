#include "ui_v2_common.h"
#include "theme_undo_manager.h"

namespace llm_re::ui_v2 {

// ColorChangeCommand implementation

void ColorChangeCommand::execute() {
    ThemeManager::instance().setColor(colorName_, newColor_);
}

void ColorChangeCommand::undo() {
    ThemeManager::instance().setColor(colorName_, oldColor_);
}

QString ColorChangeCommand::description() const {
    return QString("Change %1 color").arg(colorName_);
}

// FontChangeCommand implementation

void FontChangeCommand::execute() {
    auto& tm = ThemeManager::instance();
    auto typography = tm.typography();
    
    if (fontType_ == "base") {
        typography.body = newFont_;
    } else if (fontType_ == "heading1") {
        typography.heading1 = newFont_;
    } else if (fontType_ == "heading2") {
        typography.heading2 = newFont_;
    } else if (fontType_ == "heading3") {
        typography.heading3 = newFont_;
    } else if (fontType_ == "code") {
        typography.code = newFont_;
    } else if (fontType_ == "caption") {
        typography.caption = newFont_;
    } else if (fontType_ == "bodySmall") {
        typography.bodySmall = newFont_;
    }
    
    tm.setTypography(typography);
}

void FontChangeCommand::undo() {
    auto& tm = ThemeManager::instance();
    auto typography = tm.typography();
    
    if (fontType_ == "base") {
        typography.body = oldFont_;
    } else if (fontType_ == "heading1") {
        typography.heading1 = oldFont_;
    } else if (fontType_ == "heading2") {
        typography.heading2 = oldFont_;
    } else if (fontType_ == "heading3") {
        typography.heading3 = oldFont_;
    } else if (fontType_ == "code") {
        typography.code = oldFont_;
    } else if (fontType_ == "caption") {
        typography.caption = oldFont_;
    } else if (fontType_ == "bodySmall") {
        typography.bodySmall = oldFont_;
    }
    
    tm.setTypography(typography);
}

QString FontChangeCommand::description() const {
    return QString("Change %1 font").arg(fontType_);
}

// BatchCommand implementation

void BatchCommand::addCommand(std::unique_ptr<ThemeCommand> cmd) {
    commands_.push_back(std::move(cmd));
}

void BatchCommand::execute() {
    for (auto& cmd : commands_) {
        cmd->execute();
    }
}

void BatchCommand::undo() {
    // Undo in reverse order
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
        (*it)->undo();
    }
}

// ThemeUndoManager implementation

ThemeUndoManager& ThemeUndoManager::instance() {
    static ThemeUndoManager instance;
    return instance;
}

ThemeUndoManager::ThemeUndoManager() : QObject(nullptr) {
    // Constructor
}

void ThemeUndoManager::executeCommand(std::unique_ptr<ThemeCommand> command) {
    if (currentBatch_) {
        // Add to current batch
        currentBatch_->addCommand(std::move(command));
        return;
    }
    
    // Execute the command
    command->execute();
    
    // Add to undo stack
    undoStack_.push_back(std::move(command));
    
    // Clear redo stack when new command is executed
    redoStack_.clear();
    
    // Trim history if needed
    trimHistory();
    
    updateState();
}

void ThemeUndoManager::beginBatch(const QString& description) {
    if (currentBatch_) {
        qWarning() << "Already in batch mode";
        return;
    }
    
    currentBatch_ = std::make_unique<BatchCommand>(description);
}

void ThemeUndoManager::endBatch() {
    if (!currentBatch_) {
        qWarning() << "Not in batch mode";
        return;
    }
    
    if (!currentBatch_->isEmpty()) {
        executeCommand(std::move(currentBatch_));
    }
    
    currentBatch_.reset();
}

bool ThemeUndoManager::canUndo() const {
    return !undoStack_.empty();
}

bool ThemeUndoManager::canRedo() const {
    return !redoStack_.empty();
}

void ThemeUndoManager::undo() {
    if (!canUndo()) return;
    
    // Get command from undo stack
    auto command = std::move(undoStack_.back());
    undoStack_.pop_back();
    
    // Undo it
    command->undo();
    
    // Add to redo stack
    redoStack_.push_back(std::move(command));
    
    updateState();
}

void ThemeUndoManager::redo() {
    if (!canRedo()) return;
    
    // Get command from redo stack
    auto command = std::move(redoStack_.back());
    redoStack_.pop_back();
    
    // Execute it
    command->execute();
    
    // Add to undo stack
    undoStack_.push_back(std::move(command));
    
    updateState();
}

void ThemeUndoManager::clear() {
    undoStack_.clear();
    redoStack_.clear();
    currentBatch_.reset();
    updateState();
}

QString ThemeUndoManager::undoDescription() const {
    if (undoStack_.empty()) return QString();
    return undoStack_.back()->description();
}

QString ThemeUndoManager::redoDescription() const {
    if (redoStack_.empty()) return QString();
    return redoStack_.back()->description();
}

std::vector<QString> ThemeUndoManager::undoHistory() const {
    std::vector<QString> history;
    for (const auto& cmd : undoStack_) {
        history.push_back(cmd->description());
    }
    return history;
}

std::vector<QString> ThemeUndoManager::redoHistory() const {
    std::vector<QString> history;
    for (const auto& cmd : redoStack_) {
        history.push_back(cmd->description());
    }
    return history;
}

void ThemeUndoManager::updateState() {
    emit canUndoChanged(canUndo());
    emit canRedoChanged(canRedo());
    emit undoDescriptionChanged(undoDescription());
    emit redoDescriptionChanged(redoDescription());
    emit historyChanged();
}

void ThemeUndoManager::trimHistory() {
    while (undoStack_.size() > maxUndoLevels_) {
        undoStack_.pop_front();
    }
}

// Convenience functions

std::unique_ptr<ThemeCommand> makeColorChangeCommand(const QString& colorName, 
                                                     const QColor& oldColor, 
                                                     const QColor& newColor) {
    return std::make_unique<ColorChangeCommand>(colorName, oldColor, newColor);
}

std::unique_ptr<ThemeCommand> makeFontChangeCommand(const QString& fontType,
                                                    const QFont& oldFont,
                                                    const QFont& newFont) {
    return std::make_unique<FontChangeCommand>(fontType, oldFont, newFont);
}

} // namespace llm_re::ui_v2