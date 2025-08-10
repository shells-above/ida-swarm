#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"
#include "../models/conversation_model.h"
#include "../widgets/message_bubble.h"

namespace llm_re::ui_v2 {

// Forward declarations
class ConversationInputArea;
class ConversationSearchBar;
class MessageBubbleContainer;

// Main conversation view that combines model, bubbles, and input
class ConversationView : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit ConversationView(QWidget* parent = nullptr);
    ~ConversationView() override;
    
    // Model access
    ConversationModel* model() { return model_; }
    const ConversationModel* model() const { return model_; }
    void setModel(ConversationModel* model);
    
    // Message management
    void addUserMessage(const QString& content);
    void addAssistantMessage(const QString& content);
    void addSystemMessage(const QString& content);
    void clearConversation();
    
    // View control
    void scrollToBottom(bool animated = true);
    void scrollToMessage(const QUuid& id, bool animated = true);
    void focusInput();
    
    // Selection
    void selectMessage(const QUuid& id);
    void selectAll();
    void clearSelection();
    QList<UIMessage*> selectedMessages() const;
    
    void copySelectedMessages();
    
    // Settings
    void setBubbleStyle(MessageBubble::BubbleStyle style);
    void setShowTimestamps(bool show);
    void setMaxBubbleWidth(int width);
    void setDensityMode(int mode); // 0=Compact, 1=Cozy, 2=Spacious
    int densityMode() const { return densityMode_; }
    
    // State
    bool hasUnsavedChanges() const { return hasUnsavedChanges_; }
    void discardChanges();  // Public method to discard unsaved changes
    QString currentSessionId() const { return sessionId_; }
    
    // Auto-save
    void setAutoSaveEnabled(bool enabled);
    bool isAutoSaveEnabled() const { return autoSaveEnabled_; }
    void setAutoSaveInterval(int seconds);
    
    // Session management  
    void saveSession(const QString& path = QString());
    void loadSession(const QString& path);
    void newSession();
    
    // Initialization
    void finishInitialization();
    
signals:
    void messageSubmitted(const QString& content);
    void messageAdded(const QUuid& id);
    void messageSelected(const QUuid& id);
    void selectionChanged();
    void conversationCleared();
    void sessionChanged(const QString& sessionId);
    void unsavedChangesChanged(bool hasChanges);
    void scrolledToBottom();
    
public slots:
    void submitInput();
    void cancelInput();
    void updateTheme();
    void onAgentStateChanged();
    
protected:
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void onThemeChanged() override;
    
private slots:
    void onModelRowsInserted(const QModelIndex& parent, int first, int last);
    void onModelRowsRemoved(const QModelIndex& parent, int first, int last);
    void onBubbleClicked(const QUuid& id);
    void onBubbleContextMenu(const QUuid& id, const QPoint& pos);
    void onInputTextChanged();
    void onScrollPositionChanged();
    void onAutoSaveTimeout();
    void updateButtonStates();
    void handleFileDropped(const QString& filePath);
    
private:
    void setupUI();
    void createToolBar();
    void createMessageArea();
    void createInputArea();
    void createStatusBar();
    void connectModelSignals();
    void disconnectModelSignals();
    void updateMessageBubbles();
    void markUnsavedChanges();
    void clearUnsavedChanges();
    void generateSessionId();
    bool eventFilter(QObject* watched, QEvent* event) override;
    
    // Model
    ConversationModel* model_ = nullptr;
    bool ownModel_ = false;
    
    // UI Components
    QToolBar* toolBar_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    MessageBubbleContainer* bubbleContainer_ = nullptr;
    QWidget* inputContainer_ = nullptr;
    ConversationInputArea* inputArea_ = nullptr;
    QPushButton* sendButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    QPushButton* resumeButton_ = nullptr;
    QPushButton* stopButton_ = nullptr;
    QWidget* statusBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* wordCountLabel_ = nullptr;

    // Actions
    QAction* newSessionAction_ = nullptr;
    QAction* saveSessionAction_ = nullptr;
    QAction* loadSessionAction_ = nullptr;
    QAction* clearAction_ = nullptr;
    QAction* showTimestampsAction_ = nullptr;
    
    // State
    QString sessionId_;
    QString sessionPath_;
    QDateTime sessionCreatedTime_;
    bool hasUnsavedChanges_ = false;
    bool isInitializing_ = true;  // Prevent marking changes during setup
    bool autoSaveEnabled_ = true;
    QTimer* autoSaveTimer_ = nullptr;
    int autoSaveInterval_ = 60; // seconds
    MessageBubble::BubbleStyle bubbleStyle_ = MessageBubble::BubbleStyle::Modern;
    int densityMode_ = 1; // 0=Compact, 1=Cozy, 2=Spacious
    bool showTimestamps_ = true;
    int maxBubbleWidth_ = 600;
    
    // Scroll state
    bool isAtBottom_ = true;
    bool programmaticScroll_ = false;
};

// Input area - single widget that can expand
class ConversationInputArea : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit ConversationInputArea(QWidget* parent = nullptr);
    
    QString text() const;
    void setText(const QString& text);
    void clear();
    void focus();
    void selectAll();
    
    void setPlaceholder(const QString& text);
    void setMaxLength(int length);
    
    bool hasText() const { return !text().trimmed().isEmpty(); }
    int wordCount() const;
    int charCount() const;
    
signals:
    void submitRequested();
    void cancelRequested();
    void textChanged();
    void fileDropped(const QString& path);
    void pasteRequested();
    
protected:
    void keyPressEvent(QKeyEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    
private:
    void setupUI();
    void adjustHeight();
    
    QTextEdit* textEdit_ = nullptr;
    int baseHeight_ = 45;
    int maxLength_ = 0;
    bool maxLengthConnected_ = false;
};

} // namespace llm_re::ui_v2