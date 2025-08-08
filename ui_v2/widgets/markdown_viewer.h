#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"

namespace llm_re::ui_v2 {

// Forward declarations
class CodeBlockHighlighter;
class MarkdownProcessor;

// Unified markdown/HTML viewer with syntax highlighting
class MarkdownViewer : public BaseStyledWidget {
    Q_OBJECT
    Q_PROPERTY(bool readOnly READ isReadOnly WRITE setReadOnly)
    Q_PROPERTY(qreal zoomFactor READ zoomFactor WRITE setZoomFactor)

public:
    explicit MarkdownViewer(QWidget* parent = nullptr);
    ~MarkdownViewer() override;

    // Content management
    void setMarkdown(const QString& markdown);
    void setHtml(const QString& html);
    void setPlainText(const QString& text);
    void appendMarkdown(const QString& markdown);
    void appendHtml(const QString& html);
    void clear();
    
    QString toMarkdown() const;
    QString toHtml() const;
    QString toPlainText() const;
    
    // Syntax highlighting
    void setSyntaxHighlightingEnabled(bool enabled);
    bool isSyntaxHighlightingEnabled() const { return syntaxHighlightingEnabled_; }
    void setDefaultCodeLanguage(const QString& language);
    void registerLanguageHighlighter(const QString& language, 
                                   std::function<void(QTextDocument*, const QString&)> highlighter);
    
    // View options
    bool isReadOnly() const;
    void setReadOnly(bool readOnly);
    
    qreal zoomFactor() const;
    void setZoomFactor(qreal factor);
    void zoomIn(int steps = 1);
    void zoomOut(int steps = 1);
    void resetZoom();
    
    // Selection and search
    bool hasSelection() const;
    QString selectedText() const;
    void selectAll();
    void copy();
    
    void findText(const QString& text, bool forward = true, 
                  bool caseSensitive = false, bool wholeWords = false);
    void clearSearch();
    int searchMatchCount() const { return searchMatches_.size(); }
    int currentSearchMatch() const { return currentSearchIndex_; }
    
    // Navigation
    void scrollToAnchor(const QString& anchor);
    void scrollToTop();
    void scrollToBottom();
    void ensureVisible(int position);
    
    
    // Customization
    void setLinkColor(const QColor& color);
    void setCodeBlockStyle(const QString& style);
    void setLineSpacing(qreal spacing);
    void setDocumentMargins(const QMargins& margins);
    
    // Advanced features
    void setOpenExternalLinks(bool open) { openExternalLinks_ = open; }
    void setOpenLinksInternally(bool internal) { openLinksInternally_ = internal; }
    void setImageCaching(bool cache) { imageCachingEnabled_ = cache; }
    void setMaxImageWidth(int width) { maxImageWidth_ = width; }
    

signals:
    void linkClicked(const QUrl& url);
    void linkHovered(const QUrl& url);
    void contentChanged();
    void selectionChanged();
    void searchMatchesChanged(int count);
    void zoomFactorChanged(qreal factor);
    void copyAvailable(bool available);
    void scrollPositionChanged();

public slots:
    void refresh();
    void updateTheme();

protected:
    void paintContent(QPainter* painter) override;
    void resizeEvent(QResizeEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void onThemeChanged() override;

private slots:
    void onLinkClicked(const QUrl& url);
    void onCursorPositionChanged();
    void onTextChanged();
    void onScrollPositionChanged();
    void updateSearchHighlight();

private:
    void setupTextBrowser();
    void applyStyleSheet();
    void processContent();
    void highlightCodeBlocks();
    void processImages();
    void updateDocumentLayout();
    void createContextMenu();
    void navigateToNextMatch();
    void navigateToPreviousMatch();
    void updateSearchMatches();
    
    // HTML to Markdown conversion helpers
    QString htmlToMarkdown(const QString& html) const;
    void processBlockToMarkdown(const QTextBlock& block, QTextStream& stream) const;
    void processBlockFragments(const QTextBlock& block, QTextStream& stream) const;
    void processFragmentToMarkdown(const QTextFragment& fragment, QTextStream& stream) const;
    void processTableToMarkdown(QTextTable* table, QTextStream& stream) const;
    QString escapeMarkdownSpecialChars(const QString& text) const;
    
    // Core components
    QTextBrowser* textBrowser_;
    std::unique_ptr<MarkdownProcessor> markdownProcessor_;
    std::unordered_map<QString, std::function<void(QTextDocument*, const QString&)>> languageHighlighters_;
    
    // State
    QString currentContent_;
    QString currentContentType_; // "markdown", "html", "plain"
    bool syntaxHighlightingEnabled_ = true;
    QString defaultCodeLanguage_ = "cpp";
    qreal currentZoom_ = 1.0;
    
    // Search
    QString currentSearchText_;
    QList<QTextCursor> searchMatches_;
    int currentSearchIndex_ = -1;
    bool searchCaseSensitive_ = false;
    bool searchWholeWords_ = false;
    
    // Options
    bool openExternalLinks_ = true;
    bool openLinksInternally_ = false;
    bool imageCachingEnabled_ = true;
    int maxImageWidth_ = 800;
    QString codeBlockStyle_; // Custom CSS for code blocks
    
    // Context menu
    QMenu* contextMenu_ = nullptr;
    QAction* copyAction_ = nullptr;
    QAction* selectAllAction_ = nullptr;
    QAction* findAction_ = nullptr;
    QAction* zoomInAction_ = nullptr;
    QAction* zoomOutAction_ = nullptr;
    QAction* resetZoomAction_ = nullptr;
};

// Markdown processor with extended features
class MarkdownProcessor : public QObject {
    Q_OBJECT

public:
    explicit MarkdownProcessor(QObject* parent = nullptr);
    
    QString processMarkdown(const QString& markdown);
    void setCodeBlockTemplate(const QString& template_);
    void setTableStyling(bool enable) { enableTableStyling_ = enable; }
    void setTaskListSupport(bool enable) { enableTaskLists_ = enable; }
    void setEmojiSupport(bool enable) { enableEmoji_ = enable; }
    void setFootnoteSupport(bool enable) { enableFootnotes_ = enable; }
    void setMathSupport(bool enable) { enableMath_ = enable; }
    

private:
    QString preprocessMarkdown(const QString& markdown);
    QString postprocessHtml(const QString& html);
    QString processCodeBlocks(const QString& text);
    QString processTaskLists(const QString& text);
    QString processEmoji(const QString& text);
    QString processFootnotes(const QString& text);
    QString processMath(const QString& text);
    QString styleTables(const QString& html);
    
    QString codeBlockTemplate_;
    bool enableTableStyling_ = true;
    bool enableTaskLists_ = true;
    bool enableEmoji_ = true;
    bool enableFootnotes_ = true;
    bool enableMath_ = false;
    
    QRegularExpression codeBlockRegex_;
    QRegularExpression taskListRegex_;
    QRegularExpression emojiRegex_;
    QRegularExpression footnoteRegex_;
    QRegularExpression mathBlockRegex_;
    QRegularExpression mathInlineRegex_;
};

// Syntax highlighter for code blocks
class CodeBlockHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit CodeBlockHighlighter(QTextDocument* parent = nullptr);
    
    void setLanguage(const QString& language);
    void setTheme(const QString& theme);
    
protected:
    void highlightBlock(const QString& text) override;
    
private:
    struct HighlightingRule {
        QRegularExpression pattern;
        QTextCharFormat format;
    };
    
    void setupRules();
    void highlightCpp(const QString& text);
    void highlightPython(const QString& text);
    void highlightJavaScript(const QString& text);
    void highlightJson(const QString& text);
    void highlightXml(const QString& text);
    void highlightMarkdown(const QString& text);
    void highlightBash(const QString& text);
    void highlightAsm(const QString& text);
    
    QString language_;
    QString theme_;
    QVector<HighlightingRule> rules_;
    
    // Format cache
    QTextCharFormat keywordFormat_;
    QTextCharFormat stringFormat_;
    QTextCharFormat commentFormat_;
    QTextCharFormat numberFormat_;
    QTextCharFormat functionFormat_;
    QTextCharFormat variableFormat_;
    QTextCharFormat operatorFormat_;
    QTextCharFormat preprocessorFormat_;
};

} // namespace llm_re::ui_v2