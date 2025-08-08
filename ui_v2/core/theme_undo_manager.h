#pragma once

#include "ui_v2_common.h"
#include "theme_manager.h"

namespace llm_re::ui_v2 {

// Base class for undoable commands
class ThemeCommand {
public:
    virtual ~ThemeCommand() = default;
    
    virtual void execute() = 0;
    virtual void undo() = 0;
    virtual QString description() const = 0;
    
    QDateTime timestamp() const { return timestamp_; }
    
protected:
    ThemeCommand() : timestamp_(QDateTime::currentDateTime()) {}
    
private:
    QDateTime timestamp_;
};

// Specific command types
class ColorChangeCommand : public ThemeCommand {
public:
    ColorChangeCommand(const QString& colorName, const QColor& oldColor, const QColor& newColor)
        : colorName_(colorName), oldColor_(oldColor), newColor_(newColor) {}
    
    void execute() override;
    void undo() override;
    QString description() const override;
    
private:
    QString colorName_;
    QColor oldColor_;
    QColor newColor_;
};

class FontChangeCommand : public ThemeCommand {
public:
    FontChangeCommand(const QString& fontType, const QFont& oldFont, const QFont& newFont)
        : fontType_(fontType), oldFont_(oldFont), newFont_(newFont) {}
    
    void execute() override;
    void undo() override;
    QString description() const override;
    
private:
    QString fontType_;
    QFont oldFont_;
    QFont newFont_;
};

class BatchCommand : public ThemeCommand {
public:
    BatchCommand(const QString& description) : description_(description) {}
    
    void addCommand(std::unique_ptr<ThemeCommand> cmd);
    void execute() override;
    void undo() override;
    QString description() const override { return description_; }
    bool isEmpty() const { return commands_.empty(); }
    
private:
    QString description_;
    std::vector<std::unique_ptr<ThemeCommand>> commands_;
};

// Main undo/redo manager
class ThemeUndoManager : public QObject {
    Q_OBJECT

public:
    static ThemeUndoManager& instance();
    
    // Command management
    void executeCommand(std::unique_ptr<ThemeCommand> command);
    void beginBatch(const QString& description);
    void endBatch();
    
    // Undo/redo operations
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    void clear();
    
    // History access
    QString undoDescription() const;
    QString redoDescription() const;
    std::vector<QString> undoHistory() const;
    std::vector<QString> redoHistory() const;
    
    // Settings
    void setMaxUndoLevels(int levels) { maxUndoLevels_ = levels; }
    int maxUndoLevels() const { return maxUndoLevels_; }
    
signals:
    void canUndoChanged(bool canUndo);
    void canRedoChanged(bool canRedo);
    void undoDescriptionChanged(const QString& description);
    void redoDescriptionChanged(const QString& description);
    void historyChanged();

private:
    ThemeUndoManager();
    ~ThemeUndoManager() = default;
    ThemeUndoManager(const ThemeUndoManager&) = delete;
    ThemeUndoManager& operator=(const ThemeUndoManager&) = delete;
    
    void updateState();
    void trimHistory();
    
    std::deque<std::unique_ptr<ThemeCommand>> undoStack_;
    std::deque<std::unique_ptr<ThemeCommand>> redoStack_;
    std::unique_ptr<BatchCommand> currentBatch_;
    int maxUndoLevels_ = 50;
};

// Convenience functions for creating commands
std::unique_ptr<ThemeCommand> makeColorChangeCommand(const QString& colorName, 
                                                     const QColor& oldColor, 
                                                     const QColor& newColor);

std::unique_ptr<ThemeCommand> makeFontChangeCommand(const QString& fontType,
                                                    const QFont& oldFont,
                                                    const QFont& newFont);

} // namespace llm_re::ui_v2