#pragma once

#include "../core/base_styled_widget.h"
#include <QPropertyAnimation>
#include <QGraphicsOpacityEffect>
#include <memory>

class QTimer;
class QTextEdit;
class QLabel;
class QToolButton;
class QListWidget;
class QStackedWidget;
class QScrollArea;
class QJsonObject;
class QJsonArray;

namespace llm_re::ui_v2 {

// Floating inspector window for detailed information
class FloatingInspector : public BaseStyledWidget {
    Q_OBJECT
    
public:
    enum InspectorMode {
        CompactMode,
        DetailedMode,
        ExpandedMode
    };
    
    enum ContentType {
        NoContent,
        MessageContent,
        MemoryContent,
        ToolContent,
        ErrorContent,
        MetricsContent,
        CustomContent
    };
    
    enum Position {
        FollowCursor,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
        Center,
        Manual
    };
    
    explicit FloatingInspector(QWidget* parent = nullptr);
    ~FloatingInspector() override;
    
    // Content management
    void showMessage(const QString& role, const QString& content, const QJsonObject& metadata = QJsonObject());
    void showMemory(const QString& address, const QJsonObject& data);
    void showTool(const QString& toolName, const QJsonObject& parameters, const QString& output = QString());
    void showError(const QString& error, const QString& stackTrace = QString(), const QJsonObject& context = QJsonObject());
    void showMetrics(const QJsonObject& metrics);
    void showCustom(const QString& title, QWidget* contentWidget);
    void clear();
    
    // Display control
    void setMode(InspectorMode mode);
    InspectorMode mode() const { return mode_; }
    
    void setPosition(Position pos);
    Position position() const { return position_; }
    
    void setOffset(const QPoint& offset) { offset_ = offset; updatePosition(); }
    QPoint offset() const { return offset_; }
    
    void setAutoHide(bool autoHide);
    bool autoHide() const { return autoHide_; }
    
    void setAutoHideDelay(int ms) { autoHideDelay_ = ms; }
    int autoHideDelay() const { return autoHideDelay_; }
    
    void setPinned(bool pinned);
    bool isPinned() const { return pinned_; }
    
    void setOpacity(qreal opacity);
    qreal opacity() const { return opacity_; }
    
    void setFollowMouse(bool follow) { followMouse_ = follow; }
    bool followMouse() const { return followMouse_; }
    
    // Animation
    void animateIn();
    void animateOut();
    void setAnimationDuration(int ms) { animationDuration_ = ms; }
    
    // History
    void addToHistory(ContentType type, const QJsonObject& data);
    void showHistory();
    void clearHistory();
    void navigateBack();
    void navigateForward();
    bool canNavigateBack() const { return historyIndex_ > 0; }
    bool canNavigateForward() const { return historyIndex_ < history_.size() - 1; }
    
    // Search
    void setSearchEnabled(bool enabled);
    void search(const QString& text);
    void findNext();
    void findPrevious();
    
    // Export
    void exportContent(const QString& format = "txt");
    void copyToClipboard();
    
signals:
    void linkClicked(const QString& link);
    void actionRequested(const QString& action, const QJsonObject& data);
    void modeChanged(InspectorMode mode);
    void positionChanged(Position position);
    void pinStateChanged(bool pinned);
    void historyNavigated(int index);
    void searchResultFound(int index, int total);
    
public slots:
    void show();
    void hide();
    void toggle();
    void updatePosition();
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    
private slots:
    void onAutoHideTimeout();
    void onLinkClicked(const QString& link);
    void onModeButtonClicked();
    void onPinButtonClicked();
    void onCloseButtonClicked();
    void onHistoryItemClicked(int index);
    void onSearchTextChanged(const QString& text);
    void onAnimationFinished();
    void updateOpacity();
    
private:
    void setupUI();
    void createHeader();
    void createContent();
    void createFooter();
    void applyMode();
    void startAutoHideTimer();
    void stopAutoHideTimer();
    void updateHistoryButtons();
    void highlightSearchResults();
    void updateSizeForMode();
    QSize sizeForMode(InspectorMode mode) const;
    QPoint calculatePosition() const;
    void installGlobalEventFilter();
    void removeGlobalEventFilter();
    
    // UI Components
    QWidget* headerWidget_ = nullptr;
    QLabel* titleLabel_ = nullptr;
    QLabel* iconLabel_ = nullptr;
    QToolButton* modeButton_ = nullptr;
    QToolButton* pinButton_ = nullptr;
    QToolButton* closeButton_ = nullptr;
    
    QStackedWidget* contentStack_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    
    // Content widgets
    QWidget* messageWidget_ = nullptr;
    QLabel* roleLabel_ = nullptr;
    QTextEdit* messageEdit_ = nullptr;
    QListWidget* metadataList_ = nullptr;
    
    QWidget* memoryWidget_ = nullptr;
    QLabel* addressLabel_ = nullptr;
    QTextEdit* memoryHexEdit_ = nullptr;
    QTextEdit* memoryAsciiEdit_ = nullptr;
    QListWidget* memoryInfoList_ = nullptr;
    
    QWidget* toolWidget_ = nullptr;
    QLabel* toolNameLabel_ = nullptr;
    QTextEdit* parametersEdit_ = nullptr;
    QTextEdit* outputEdit_ = nullptr;
    
    QWidget* errorWidget_ = nullptr;
    QLabel* errorLabel_ = nullptr;
    QTextEdit* errorMessageEdit_ = nullptr;
    QTextEdit* stackTraceEdit_ = nullptr;
    QListWidget* contextList_ = nullptr;
    
    QWidget* metricsWidget_ = nullptr;
    QTableWidget* metricsTable_ = nullptr;
    
    QWidget* customWidget_ = nullptr;
    QWidget* customContentHolder_ = nullptr;
    
    // Footer
    QWidget* footerWidget_ = nullptr;
    QToolButton* backButton_ = nullptr;
    QToolButton* forwardButton_ = nullptr;
    QLineEdit* searchEdit_ = nullptr;
    QLabel* searchResultLabel_ = nullptr;
    QToolButton* exportButton_ = nullptr;
    
    // State
    InspectorMode mode_ = CompactMode;
    ContentType currentType_ = NoContent;
    Position position_ = FollowCursor;
    QPoint offset_;
    bool pinned_ = false;
    bool autoHide_ = true;
    int autoHideDelay_ = 3000;
    bool followMouse_ = false;
    qreal opacity_ = 0.95;
    int animationDuration_ = 200;
    
    // Animation
    QPropertyAnimation* fadeAnimation_ = nullptr;
    QPropertyAnimation* moveAnimation_ = nullptr;
    QPropertyAnimation* sizeAnimation_ = nullptr;
    QGraphicsOpacityEffect* opacityEffect_ = nullptr;
    
    // Timer
    QTimer* autoHideTimer_ = nullptr;
    
    // Mouse tracking
    bool isDragging_ = false;
    QPoint dragStartPos_;
    
    // History
    struct HistoryItem {
        ContentType type;
        QJsonObject data;
        QString title;
        QDateTime timestamp;
    };
    QList<HistoryItem> history_;
    int historyIndex_ = -1;
    int maxHistorySize_ = 50;
    
    // Search
    QString currentSearchText_;
    QList<QTextEdit::ExtraSelection> searchHighlights_;
    int currentSearchIndex_ = -1;
    
    // Global event filter
    bool globalFilterInstalled_ = false;
    
    // Sizes for different modes
    static constexpr int COMPACT_WIDTH = 300;
    static constexpr int COMPACT_HEIGHT = 200;
    static constexpr int DETAILED_WIDTH = 400;
    static constexpr int DETAILED_HEIGHT = 300;
    static constexpr int EXPANDED_WIDTH = 600;
    static constexpr int EXPANDED_HEIGHT = 400;
};

// Custom tooltip-style inspector
class TooltipInspector : public FloatingInspector {
    Q_OBJECT
    
public:
    explicit TooltipInspector(QWidget* parent = nullptr);
    
    void showAtCursor(const QString& text, int duration = 2000);
    void showAtWidget(QWidget* widget, const QString& text, int duration = 2000);
    void showAtPoint(const QPoint& pos, const QString& text, int duration = 2000);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    void setupTooltipStyle();
};

// Property inspector for detailed object information
class PropertyInspector : public FloatingInspector {
    Q_OBJECT
    
public:
    explicit PropertyInspector(QWidget* parent = nullptr);
    
    void inspectObject(QObject* obj);
    void inspectJson(const QJsonObject& json);
    void inspectProperties(const QVariantMap& properties);
    
    void setExpandLevel(int level) { expandLevel_ = level; }
    int expandLevel() const { return expandLevel_; }
    
    void setShowPrivate(bool show) { showPrivate_ = show; }
    bool showPrivate() const { return showPrivate_; }
    
signals:
    void propertyChanged(const QString& name, const QVariant& value);
    
private:
    void setupPropertyView();
    void populateProperties();
    
    QTreeWidget* propertyTree_ = nullptr;
    QObject* currentObject_ = nullptr;
    int expandLevel_ = 2;
    bool showPrivate_ = false;
};

// Code inspector for syntax highlighting
class CodeInspector : public FloatingInspector {
    Q_OBJECT
    
public:
    explicit CodeInspector(QWidget* parent = nullptr);
    
    void showCode(const QString& code, const QString& language = "cpp");
    void showFile(const QString& filePath);
    void showDiff(const QString& before, const QString& after);
    
    void setLineNumbers(bool show) { showLineNumbers_ = show; }
    bool lineNumbers() const { return showLineNumbers_; }
    
    void setHighlightLine(int line);
    void clearHighlight();
    
    void setSyntaxHighlighter(const QString& language);
    
signals:
    void lineClicked(int line);
    void fileDropped(const QString& path);
    
protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    
private:
    void setupCodeView();
    void applyLineNumbers();
    void highlightLine(int line);
    
    QTextEdit* codeEdit_ = nullptr;
    QString currentLanguage_ = "cpp";
    bool showLineNumbers_ = true;
    int highlightedLine_ = -1;
};

// Image inspector for visual content
class ImageInspector : public FloatingInspector {
    Q_OBJECT
    
public:
    explicit ImageInspector(QWidget* parent = nullptr);
    
    void showImage(const QPixmap& pixmap);
    void showImage(const QString& path);
    void showImages(const QStringList& paths);
    
    void setZoomMode(const QString& mode) { zoomMode_ = mode; } // fit, actual, custom
    void setZoomLevel(qreal zoom);
    qreal zoomLevel() const { return zoomLevel_; }
    
    void enableCompare(bool enable) { compareMode_ = enable; }
    void setCompareImage(const QPixmap& pixmap);
    
signals:
    void pixelClicked(const QPoint& pos, const QColor& color);
    void zoomChanged(qreal zoom);
    
protected:
    void wheelEvent(QWheelEvent* event) override;
    
private:
    void setupImageView();
    void updateImage();
    
    QLabel* imageLabel_ = nullptr;
    QScrollArea* imageScroll_ = nullptr;
    QSlider* zoomSlider_ = nullptr;
    QComboBox* imageCombo_ = nullptr;
    
    QPixmap currentPixmap_;
    QPixmap comparePixmap_;
    QStringList imagePaths_;
    int currentImageIndex_ = 0;
    
    QString zoomMode_ = "fit";
    qreal zoomLevel_ = 1.0;
    bool compareMode_ = false;
};

// Inspector factory
class InspectorFactory {
public:
    static FloatingInspector* createInspector(const QString& type, QWidget* parent = nullptr);
    static void registerInspectorType(const QString& type, std::function<FloatingInspector*(QWidget*)> creator);
    
private:
    static QHash<QString, std::function<FloatingInspector*(QWidget*)>> creators_;
};

} // namespace llm_re::ui_v2