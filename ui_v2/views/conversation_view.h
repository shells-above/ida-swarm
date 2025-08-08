#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"
#include "../models/conversation_model.h"
#include "../widgets/message_bubble.h"

namespace llm_re::ui_v2 {

// Forward declarations
class ConversationInputArea;
class ConversationSearchBar;
class TypingIndicator;
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
    void addMessage(std::unique_ptr<Message> message);
    void addUserMessage(const QString& content);
    void addAssistantMessage(const QString& content);
    void addSystemMessage(const QString& content);
    void addToolMessage(const QString& toolName, const QString& content);
    void clearConversation();
    
    // View control
    void scrollToBottom(bool animated = true);
    void scrollToMessage(const QUuid& id, bool animated = true);
    void focusInput();
    
    // Search
    void showSearchBar();
    void hideSearchBar();
    void findNext();
    void findPrevious();
    
    // Selection
    void selectMessage(const QUuid& id);
    void selectAll();
    void clearSelection();
    QList<Message*> selectedMessages() const;
    
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
    void searchRequested(const QString& text);
    void sessionChanged(const QString& sessionId);
    void unsavedChangesChanged(bool hasChanges);
    void toolExecutionRequested(const QString& toolName, const QJsonObject& params);
    void linkClicked(const QUrl& url);
    void scrolledToBottom();
    
public slots:
    void submitInput();
    void cancelInput();
    void showTypingIndicator(const QString& user = QString());
    void hideTypingIndicator();
    void updateTheme();
    void onAgentStateChanged();  // Called when agent state changes
    
protected:
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void onThemeChanged() override;
    
private slots:
    void onModelDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight);
    void onModelRowsInserted(const QModelIndex& parent, int first, int last);
    void onModelRowsRemoved(const QModelIndex& parent, int first, int last);
    void onBubbleClicked(const QUuid& id);
    void onBubbleContextMenu(const QUuid& id, const QPoint& pos);
    void onBubbleLinkClicked(const QUrl& url);
    void onBubbleEditRequested(const QUuid& id);
    void onBubbleDeleteRequested(const QUuid& id);
    void onInputTextChanged();
    void onSearchTextChanged(const QString& text);
    void onScrollPositionChanged();
    void onAutoSaveTimeout();
    void updateButtonStates();
    void handleFileDropped(const QString& filePath);
    
private:
    void setupUI();
    void createToolBar();
    void createMessageArea();
    void createInputArea();
    void createSearchBar();
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
    ConversationSearchBar* searchBar_ = nullptr;
    QLineEdit* searchInput_ = nullptr;
    QLabel* searchStatusLabel_ = nullptr;
    QWidget* statusBar_ = nullptr;
    QLabel* statusLabel_ = nullptr;
    QLabel* wordCountLabel_ = nullptr;
    TypingIndicator* typingIndicator_ = nullptr;
    
    // Actions
    QAction* newSessionAction_ = nullptr;
    QAction* saveSessionAction_ = nullptr;
    QAction* loadSessionAction_ = nullptr;
    QAction* clearAction_ = nullptr;
    QAction* searchAction_ = nullptr;
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
    
    // Search state
    QString currentSearchText_;
    int currentSearchIndex_ = -1;
    
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

// Search bar widget
class ConversationSearchBar : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit ConversationSearchBar(QWidget* parent = nullptr);
    
    void setSearchText(const QString& text);
    QString searchText() const;
    
    void setMatchCount(int current, int total);
    void focusSearch();
    
    void showMessage(const QString& message, int timeout = 3000);
    
    // Search options getters
    bool isCaseSensitive() const { return caseSensitive_; }
    bool isWholeWords() const { return wholeWord_; }
    bool isRegex() const { return useRegex_; }
    
    // Search options setters
    void setCaseSensitive(bool enabled);
    void setWholeWords(bool enabled);
    void setRegex(bool enabled);
    
signals:
    void searchTextChanged(const QString& text);
    void findNextRequested();
    void findPreviousRequested();
    void closeRequested();
    void caseSensitivityChanged(bool caseSensitive);
    void wholeWordChanged(bool wholeWord);
    void regexChanged(bool regex);
    
protected:
    void keyPressEvent(QKeyEvent* event) override;
    
private:
    void setupUI();
    
    QLineEdit* searchInput_ = nullptr;
    QLabel* matchLabel_ = nullptr;
    QToolButton* prevButton_ = nullptr;
    QToolButton* nextButton_ = nullptr;
    QToolButton* caseSensitiveButton_ = nullptr;
    QToolButton* wholeWordButton_ = nullptr;
    QToolButton* regexButton_ = nullptr;
    QToolButton* closeButton_ = nullptr;
    
    bool caseSensitive_ = false;
    bool wholeWord_ = false;
    bool useRegex_ = false;
};

// Side panel for conversation info and tools
class ConversationSidePanel : public BaseStyledWidget {
    Q_OBJECT
    
public:
    explicit ConversationSidePanel(QWidget* parent = nullptr);
    
    void setModel(ConversationModel* model);
    void updateStatistics();
    
    void showPanel(const QString& panelId);
    void hidePanel();
    bool isPanelVisible() const { return isVisible(); }
    
signals:
    void panelClosed();
    void actionRequested(const QString& action, const QVariant& data);
    
private:
    void setupUI();
    void createInfoPanel();
    void createToolsPanel();
    void createHistoryPanel();
    void createSettingsPanel();
    
    ConversationModel* model_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    
    // Panels
    QWidget* infoPanel_ = nullptr;
    QWidget* toolsPanel_ = nullptr;
    QWidget* historyPanel_ = nullptr;
    QWidget* settingsPanel_ = nullptr;
    
    // Info widgets
    QLabel* messageCountLabel_ = nullptr;
    QLabel* wordCountLabel_ = nullptr;
    QLabel* durationLabel_ = nullptr;
    QLabel* toolCountLabel_ = nullptr;
    QListWidget* participantsList_ = nullptr;
    
    // Tools widgets
    QListWidget* toolsList_ = nullptr;
    QPushButton* runToolButton_ = nullptr;
    
    // History widgets
    QListWidget* historyList_ = nullptr;
    QLineEdit* historySearchInput_ = nullptr;
    
    // Settings widgets
    QComboBox* themeCombo_ = nullptr;
    QComboBox* bubbleStyleCombo_ = nullptr;
    QSlider* fontSizeSlider_ = nullptr;
    QCheckBox* showTimestampsCheck_ = nullptr;
    QCheckBox* autoSaveCheck_ = nullptr;
    QSpinBox* autoSaveIntervalSpin_ = nullptr;
};

} // namespace llm_re::ui_v2