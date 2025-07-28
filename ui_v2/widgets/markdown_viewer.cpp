#include "markdown_viewer.h"
#include "../core/theme_manager.h"
#include "../core/ui_utils.h"
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QScrollBar>
#include <QMenu>
#include <QAction>
#include <QContextMenuEvent>
#include <QPainter>
#include <QTextDocument>
#include <QTextCursor>
#include <QTextBlock>
#include <QTextList>
#include <QTextFragment>
#include <QTextStream>
#include <QTextImageFormat>
#include <QTextTable>
#include <QTextTableCell>
#include <QRegularExpression>
#include <QSet>
#include <QApplication>
#include <QClipboard>
#include <QPrinter>
#include <QPrintDialog>
#include <QFileDialog>
#include <QTextDocumentWriter>
#include <QDesktopServices>
#include <QMimeData>
#include <QInputDialog>
#include <QPixmap>
#include <QPixmapCache>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <cmath>

namespace llm_re::ui_v2 {

MarkdownViewer::MarkdownViewer(QWidget* parent)
    : BaseStyledWidget(parent) {
    
    markdownProcessor_ = std::make_unique<MarkdownProcessor>(this);
    setupTextBrowser();
    createContextMenu();
    applyStyleSheet();
    
    // Set base properties
    setBackgroundColor(ThemeManager::instance().colors().surface);
    setBorderRadius(Design::RADIUS_MD);
    setShadowEnabled(false);
}

MarkdownViewer::~MarkdownViewer() = default;

void MarkdownViewer::setupTextBrowser() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    textBrowser_ = new QTextBrowser(this);
    textBrowser_->setFrameShape(QFrame::NoFrame);
    textBrowser_->setOpenExternalLinks(false);
    textBrowser_->setOpenLinks(false);
    
    // Connect signals
    connect(textBrowser_, &QTextBrowser::anchorClicked, 
            this, &MarkdownViewer::onLinkClicked);
    connect(textBrowser_, &QTextBrowser::cursorPositionChanged,
            this, &MarkdownViewer::onCursorPositionChanged);
    connect(textBrowser_->document(), &QTextDocument::contentsChanged,
            this, &MarkdownViewer::onTextChanged);
    connect(textBrowser_->verticalScrollBar(), &QScrollBar::valueChanged,
            this, &MarkdownViewer::onScrollPositionChanged);
    
    layout->addWidget(textBrowser_);
    
    // Register default language highlighters
    registerLanguageHighlighter("cpp", [this](QTextDocument* doc, const QString& code) {
        auto* highlighter = new CodeBlockHighlighter(doc);
        highlighter->setLanguage("cpp");
    });
    
    registerLanguageHighlighter("python", [this](QTextDocument* doc, const QString& code) {
        auto* highlighter = new CodeBlockHighlighter(doc);
        highlighter->setLanguage("python");
    });
    
    registerLanguageHighlighter("javascript", [this](QTextDocument* doc, const QString& code) {
        auto* highlighter = new CodeBlockHighlighter(doc);
        highlighter->setLanguage("javascript");
    });
}

void MarkdownViewer::createContextMenu() {
    contextMenu_ = new QMenu(this);
    
    copyAction_ = contextMenu_->addAction(tr("Copy"), this, &MarkdownViewer::copy);
    copyAction_->setShortcut(QKeySequence::Copy);
    copyAction_->setEnabled(false);
    
    selectAllAction_ = contextMenu_->addAction(tr("Select All"), this, &MarkdownViewer::selectAll);
    selectAllAction_->setShortcut(QKeySequence::SelectAll);
    
    contextMenu_->addSeparator();
    
    findAction_ = contextMenu_->addAction(tr("Find..."), [this]() {
        // Emit signal for parent to show find dialog
        // For now, we'll use a simple implementation
        bool ok;
        QString text = QInputDialog::getText(this, tr("Find"), tr("Search for:"), 
                                           QLineEdit::Normal, currentSearchText_, &ok);
        if (ok && !text.isEmpty()) {
            findText(text);
        }
    });
    findAction_->setShortcut(QKeySequence::Find);
    
    contextMenu_->addSeparator();
    
    zoomInAction_ = contextMenu_->addAction(tr("Zoom In"), [this]() { zoomIn(); });
    zoomInAction_->setShortcut(QKeySequence::ZoomIn);
    
    zoomOutAction_ = contextMenu_->addAction(tr("Zoom Out"), [this]() { zoomOut(); });
    zoomOutAction_->setShortcut(QKeySequence::ZoomOut);
    
    resetZoomAction_ = contextMenu_->addAction(tr("Reset Zoom"), [this]() { resetZoom(); });
    resetZoomAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
}

void MarkdownViewer::applyStyleSheet() {
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    
    QString css = QString(R"(
        QTextBrowser {
            background-color: transparent;
            color: %1;
            font-family: %2;
            font-size: %3px;
            line-height: 1.6;
            padding: %4px;
        }
        
        a {
            color: %5;
            text-decoration: none;
        }
        
        a:hover {
            text-decoration: underline;
        }
        
        pre {
            background-color: %6;
            border: 1px solid %7;
            border-radius: %8px;
            padding: %9px;
            margin: %10px 0;
            overflow-x: auto;
            %25
        }
        
        code {
            background-color: %6;
            padding: 2px 4px;
            border-radius: 3px;
            font-family: %11;
            font-size: %12px;
        }
        
        blockquote {
            border-left: 4px solid %13;
            margin: 0;
            padding-left: %14px;
            color: %15;
        }
        
        table {
            border-collapse: collapse;
            width: 100%;
            margin: %10px 0;
        }
        
        th, td {
            border: 1px solid %7;
            padding: %16px;
            text-align: left;
        }
        
        th {
            background-color: %17;
            font-weight: bold;
        }
        
        h1, h2, h3, h4, h5, h6 {
            margin-top: %18px;
            margin-bottom: %19px;
            font-weight: 600;
        }
        
        h1 { font-size: %20px; }
        h2 { font-size: %21px; }
        h3 { font-size: %22px; }
        
        hr {
            border: none;
            border-top: 1px solid %7;
            margin: %23px 0;
        }
        
        ::selection {
            background-color: %24;
            color: %1;
        }
    )").arg(colors.textPrimary.name())
       .arg(theme.typography().body.family())
       .arg(theme.typography().body.pointSize())
       .arg(Design::SPACING_MD)
       .arg(colors.textLink.name())
       .arg(colors.codeBackground.name())
       .arg(colors.border.name())
       .arg(Design::RADIUS_SM)
       .arg(Design::SPACING_SM)
       .arg(Design::SPACING_MD)
       .arg(theme.typography().code.family())
       .arg(theme.typography().code.pointSize())
       .arg(colors.primary.name())
       .arg(Design::SPACING_MD)
       .arg(colors.textSecondary.name())
       .arg(Design::SPACING_SM)
       .arg(colors.surfaceHover.name())
       .arg(Design::SPACING_LG)
       .arg(Design::SPACING_SM)
       .arg(theme.typography().heading1.pointSize())
       .arg(theme.typography().heading2.pointSize())
       .arg(theme.typography().heading3.pointSize())
       .arg(Design::SPACING_LG)
       .arg(colors.selection.name())
       .arg(codeBlockStyle_);
    
    textBrowser_->document()->setDefaultStyleSheet(css);
}

void MarkdownViewer::setMarkdown(const QString& markdown) {
    currentContent_ = markdown;
    currentContentType_ = "markdown";
    
    QString html = markdownProcessor_->processMarkdown(markdown);
    textBrowser_->setHtml(html);
    
    processContent();
    emit contentChanged();
}

void MarkdownViewer::setHtml(const QString& html) {
    currentContent_ = html;
    currentContentType_ = "html";
    
    textBrowser_->setHtml(html);
    
    processContent();
    emit contentChanged();
}

void MarkdownViewer::setPlainText(const QString& text) {
    currentContent_ = text;
    currentContentType_ = "plain";
    
    textBrowser_->setPlainText(text);
    
    emit contentChanged();
}

void MarkdownViewer::appendMarkdown(const QString& markdown) {
    if (currentContentType_ != "markdown") {
        setMarkdown(markdown);
        return;
    }
    
    currentContent_ += "\n\n" + markdown;
    QString html = markdownProcessor_->processMarkdown(currentContent_);
    
    // Save scroll position
    int scrollPos = textBrowser_->verticalScrollBar()->value();
    bool wasAtBottom = textBrowser_->verticalScrollBar()->value() == 
                      textBrowser_->verticalScrollBar()->maximum();
    
    textBrowser_->setHtml(html);
    processContent();
    
    // Restore scroll or scroll to bottom if was at bottom
    if (wasAtBottom) {
        textBrowser_->verticalScrollBar()->setValue(
            textBrowser_->verticalScrollBar()->maximum());
    } else {
        textBrowser_->verticalScrollBar()->setValue(scrollPos);
    }
    
    emit contentChanged();
}

void MarkdownViewer::appendHtml(const QString& html) {
    if (currentContentType_ != "html") {
        setHtml(html);
        return;
    }
    
    currentContent_ += html;
    
    // Save scroll position
    int scrollPos = textBrowser_->verticalScrollBar()->value();
    bool wasAtBottom = textBrowser_->verticalScrollBar()->value() == 
                      textBrowser_->verticalScrollBar()->maximum();
    
    textBrowser_->setHtml(currentContent_);
    processContent();
    
    // Restore scroll or scroll to bottom if was at bottom
    if (wasAtBottom) {
        textBrowser_->verticalScrollBar()->setValue(
            textBrowser_->verticalScrollBar()->maximum());
    } else {
        textBrowser_->verticalScrollBar()->setValue(scrollPos);
    }
    
    emit contentChanged();
}

void MarkdownViewer::clear() {
    currentContent_.clear();
    currentContentType_.clear();
    textBrowser_->clear();
    searchMatches_.clear();
    currentSearchIndex_ = -1;
    emit contentChanged();
    emit searchMatchesChanged(0);
}

QString MarkdownViewer::toMarkdown() const {
    if (currentContentType_ == "markdown") {
        return currentContent_;
    }
    
    // Convert HTML to Markdown
    QString html = toHtml();
    return htmlToMarkdown(html);
}

// Private helper method for HTML to Markdown conversion
QString MarkdownViewer::htmlToMarkdown(const QString& html) const {
    // Parse the HTML using QTextDocument
    QTextDocument doc;
    doc.setHtml(html);
    
    QString markdown;
    QTextStream stream(&markdown);
    
    // Keep track of processed tables to avoid duplicates
    QSet<QTextTable*> processedTables;
    
    // Process each block in the document
    QTextBlock block = doc.begin();
    while (block.isValid()) {
        // Check if this block is in a table
        QTextCursor cursor(block);
        QTextTable* table = cursor.currentTable();
        
        if (table && !processedTables.contains(table)) {
            // Process the entire table
            processTableToMarkdown(table, stream);
            processedTables.insert(table);
            
            // Skip to the block after the table
            QTextTableCell lastCell = table->cellAt(table->rows() - 1, table->columns() - 1);
            if (lastCell.isValid()) {
                block = doc.findBlock(lastCell.lastCursorPosition().position());
            }
        } else if (!table) {
            // Only process blocks that are not in tables
            processBlockToMarkdown(block, stream);
        }
        
        block = block.next();
    }
    
    // Clean up extra newlines
    markdown = markdown.trimmed();
    markdown.replace(QRegularExpression("\n{3,}"), "\n\n");
    
    return markdown;
}

void MarkdownViewer::processBlockToMarkdown(const QTextBlock& block, QTextStream& stream) const {
    if (!block.isValid()) return;
    
    QTextBlockFormat blockFormat = block.blockFormat();
    int headingLevel = blockFormat.headingLevel();
    
    // Handle different block types
    if (headingLevel > 0) {
        // Heading
        stream << QString(headingLevel, '#') << " ";
        processBlockFragments(block, stream);
        stream << "\n\n";
    } else if (block.textList()) {
        // List item
        QTextList* list = block.textList();
        QTextListFormat listFormat = list->format();
        int indent = listFormat.indent();
        QString prefix(indent * 2, ' ');
        
        if (listFormat.style() == QTextListFormat::ListDecimal) {
            // Ordered list
            int itemNumber = list->itemNumber(block) + 1;
            stream << prefix << itemNumber << ". ";
        } else {
            // Unordered list
            stream << prefix << "- ";
        }
        
        processBlockFragments(block, stream);
        stream << "\n";
        
        // Add extra newline after last list item
        if (block.next().isValid() && !block.next().textList()) {
            stream << "\n";
        }
    } else if (blockFormat.indent() > 0) {
        // Check if it's a code block by examining the format
        bool isCodeBlock = false;
        if (block.begin() != block.end()) {
            QTextFragment fragment = block.begin().fragment();
            if (fragment.isValid()) {
                QTextCharFormat charFormat = fragment.charFormat();
                if (charFormat.fontFamily() == "Consolas" || charFormat.fontFamily() == "Courier" || 
                    charFormat.fontFamily() == "monospace" || charFormat.fontFixedPitch()) {
                    isCodeBlock = true;
                }
            }
        }
        
        if (isCodeBlock) {
            // Code block
            stream << "```\n";
            stream << block.text() << "\n";
            stream << "```\n\n";
        } else {
            // Blockquote
            stream << "> ";
            processBlockFragments(block, stream);
            stream << "\n\n";
        }
    } else if (block.text().trimmed().isEmpty()) {
        // Empty line
        stream << "\n";
    } else {
        // Check if entire block is code formatted
        bool isCodeBlock = true;
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            QTextFragment fragment = it.fragment();
            if (fragment.isValid()) {
                QTextCharFormat format = fragment.charFormat();
                if (!(format.fontFamily() == "Consolas" || format.fontFamily() == "Courier" || 
                      format.fontFamily() == "monospace" || format.fontFixedPitch())) {
                    isCodeBlock = false;
                    break;
                }
            }
        }
        
        if (isCodeBlock && block.text().contains('\n')) {
            // Multi-line code block
            stream << "```\n" << block.text() << "\n```\n\n";
        } else {
            // Regular paragraph
            processBlockFragments(block, stream);
            stream << "\n\n";
        }
    }
}

void MarkdownViewer::processBlockFragments(const QTextBlock& block, QTextStream& stream) const {
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
        QTextFragment fragment = it.fragment();
        if (fragment.isValid()) {
            processFragmentToMarkdown(fragment, stream);
        }
    }
}

void MarkdownViewer::processFragmentToMarkdown(const QTextFragment& fragment, QTextStream& stream) const {
    QTextCharFormat format = fragment.charFormat();
    QString text = fragment.text();
    
    // Handle special cases
    if (format.isImageFormat()) {
        // Image
        QTextImageFormat imageFormat = format.toImageFormat();
        QString imageName = imageFormat.name();
        stream << "![Image](" << imageName << ")";
        return;
    }
    
    // Handle code blocks
    if (format.fontFamily() == "Consolas" || format.fontFamily() == "Courier" || 
        format.fontFamily() == "monospace" || format.fontFixedPitch()) {
        // Check if it's a multi-line code block
        if (text.contains('\n')) {
            stream << "```\n" << text << "\n```\n";
        } else {
            stream << "`" << text << "`";
        }
        return;
    }
    
    // Build markdown formatting
    QString prefix, suffix;
    
    // Bold
    if (format.fontWeight() == QFont::Bold || format.font().bold()) {
        prefix += "**";
        suffix = "**" + suffix;
    }
    
    // Italic
    if (format.fontItalic() || format.font().italic()) {
        prefix += "*";
        suffix = "*" + suffix;
    }
    
    // Underline (convert to bold in markdown as markdown doesn't have underline)
    if (format.fontUnderline() || format.font().underline()) {
        if (prefix.isEmpty()) {
            prefix += "**";
            suffix = "**" + suffix;
        }
    }
    
    // Strikethrough
    if (format.fontStrikeOut() || format.font().strikeOut()) {
        prefix += "~~";
        suffix = "~~" + suffix;
    }
    
    // Links
    if (format.isAnchor()) {
        QString href = format.anchorHref();
        stream << "[" << text << "](" << href << ")";
        return;
    }
    
    // Escape special markdown characters
    text = escapeMarkdownSpecialChars(text);
    
    stream << prefix << text << suffix;
}

QString MarkdownViewer::escapeMarkdownSpecialChars(const QString& text) const {
    QString escaped = text;
    
    // Escape characters that have special meaning in markdown
    escaped.replace("\\", "\\\\");  // Backslash must be first
    escaped.replace("*", "\\*");
    escaped.replace("_", "\\_");
    escaped.replace("[", "\\[");
    escaped.replace("]", "\\]");
    escaped.replace("(", "\\(");
    escaped.replace(")", "\\)");
    escaped.replace("#", "\\#");
    escaped.replace("+", "\\+");
    escaped.replace("-", "\\-");
    escaped.replace(".", "\\.");
    escaped.replace("!", "\\!");
    escaped.replace("`", "\\`");
    escaped.replace(">", "\\>");
    escaped.replace("|", "\\|");
    
    return escaped;
}

void MarkdownViewer::processTableToMarkdown(QTextTable* table, QTextStream& stream) const {
    if (!table) return;
    
    int rows = table->rows();
    int cols = table->columns();
    
    if (rows == 0 || cols == 0) return;
    
    // Process table rows
    for (int row = 0; row < rows; ++row) {
        stream << "|";
        
        for (int col = 0; col < cols; ++col) {
            QTextTableCell cell = table->cellAt(row, col);
            if (cell.isValid()) {
                QString cellText;
                QTextBlock block = cell.firstCursorPosition().block();
                while (block.isValid() && block.position() <= cell.lastCursorPosition().position()) {
                    cellText += block.text();
                    block = block.next();
                    if (block.isValid() && block.position() <= cell.lastCursorPosition().position()) {
                        cellText += " ";  // Replace newlines with spaces in table cells
                    }
                }
                stream << " " << cellText.trimmed() << " |";
            } else {
                stream << " |";
            }
        }
        
        stream << "\n";
        
        // Add separator row after header (first row)
        if (row == 0) {
            stream << "|";
            for (int col = 0; col < cols; ++col) {
                stream << " --- |";
            }
            stream << "\n";
        }
    }
    
    stream << "\n";
}

QString MarkdownViewer::toHtml() const {
    return textBrowser_->toHtml();
}

QString MarkdownViewer::toPlainText() const {
    return textBrowser_->toPlainText();
}

void MarkdownViewer::setSyntaxHighlightingEnabled(bool enabled) {
    if (syntaxHighlightingEnabled_ != enabled) {
        syntaxHighlightingEnabled_ = enabled;
        refresh();
    }
}

void MarkdownViewer::setDefaultCodeLanguage(const QString& language) {
    defaultCodeLanguage_ = language;
}

void MarkdownViewer::registerLanguageHighlighter(const QString& language,
                                                std::function<void(QTextDocument*, const QString&)> highlighter) {
    languageHighlighters_[language.toLower()] = highlighter;
}

bool MarkdownViewer::isReadOnly() const {
    return textBrowser_->isReadOnly();
}

void MarkdownViewer::setReadOnly(bool readOnly) {
    textBrowser_->setReadOnly(readOnly);
}

qreal MarkdownViewer::zoomFactor() const {
    return currentZoom_;
}

void MarkdownViewer::setZoomFactor(qreal factor) {
    if (factor < 0.25) factor = 0.25;
    if (factor > 5.0) factor = 5.0;
    
    currentZoom_ = factor;
    textBrowser_->setZoomFactor(factor);
    emit zoomFactorChanged(factor);
}

void MarkdownViewer::zoomIn(int steps) {
    setZoomFactor(currentZoom_ * std::pow(1.1, steps));
}

void MarkdownViewer::zoomOut(int steps) {
    setZoomFactor(currentZoom_ / std::pow(1.1, steps));
}

void MarkdownViewer::resetZoom() {
    setZoomFactor(1.0);
}

bool MarkdownViewer::hasSelection() const {
    return textBrowser_->textCursor().hasSelection();
}

QString MarkdownViewer::selectedText() const {
    return textBrowser_->textCursor().selectedText();
}

void MarkdownViewer::selectAll() {
    textBrowser_->selectAll();
}

void MarkdownViewer::copy() {
    textBrowser_->copy();
}

void MarkdownViewer::findText(const QString& text, bool forward, 
                            bool caseSensitive, bool wholeWords) {
    currentSearchText_ = text;
    updateSearchMatches();
    
    if (!searchMatches_.isEmpty()) {
        if (forward) {
            navigateToNextMatch();
        } else {
            navigateToPreviousMatch();
        }
    }
}

void MarkdownViewer::clearSearch() {
    currentSearchText_.clear();
    searchMatches_.clear();
    currentSearchIndex_ = -1;
    
    // Clear highlighting
    QTextCursor cursor(textBrowser_->document());
    cursor.select(QTextCursor::Document);
    QTextCharFormat format;
    cursor.mergeCharFormat(format);
    
    emit searchMatchesChanged(0);
}

void MarkdownViewer::updateSearchMatches() {
    searchMatches_.clear();
    currentSearchIndex_ = -1;
    
    if (currentSearchText_.isEmpty()) {
        emit searchMatchesChanged(0);
        return;
    }
    
    QTextDocument* doc = textBrowser_->document();
    QTextCursor cursor(doc);
    
    // Find all matches
    while (!cursor.isNull() && !cursor.atEnd()) {
        cursor = doc->find(currentSearchText_, cursor);
        if (!cursor.isNull()) {
            searchMatches_.append(cursor);
        }
    }
    
    updateSearchHighlight();
    emit searchMatchesChanged(searchMatches_.size());
}

void MarkdownViewer::navigateToNextMatch() {
    if (searchMatches_.isEmpty()) return;
    
    currentSearchIndex_ = (currentSearchIndex_ + 1) % searchMatches_.size();
    QTextCursor cursor = searchMatches_[currentSearchIndex_];
    textBrowser_->setTextCursor(cursor);
    textBrowser_->ensureCursorVisible();
    updateSearchHighlight();
}

void MarkdownViewer::navigateToPreviousMatch() {
    if (searchMatches_.isEmpty()) return;
    
    currentSearchIndex_--;
    if (currentSearchIndex_ < 0) {
        currentSearchIndex_ = searchMatches_.size() - 1;
    }
    
    QTextCursor cursor = searchMatches_[currentSearchIndex_];
    textBrowser_->setTextCursor(cursor);
    textBrowser_->ensureCursorVisible();
    updateSearchHighlight();
}

void MarkdownViewer::updateSearchHighlight() {
    const auto& theme = ThemeManager::instance();
    
    // Clear previous highlighting
    QTextCursor cursor(textBrowser_->document());
    cursor.select(QTextCursor::Document);
    QTextCharFormat clearFormat;
    cursor.mergeCharFormat(clearFormat);
    
    // Highlight all matches
    QTextCharFormat matchFormat;
    matchFormat.setBackground(theme.colors().selection);
    
    for (int i = 0; i < searchMatches_.size(); ++i) {
        QTextCursor match = searchMatches_[i];
        if (i == currentSearchIndex_) {
            // Current match gets different color
            QTextCharFormat currentFormat = matchFormat;
            currentFormat.setBackground(theme.colors().primary);
            currentFormat.setForeground(theme.colors().textInverse);
            match.mergeCharFormat(currentFormat);
        } else {
            match.mergeCharFormat(matchFormat);
        }
    }
}

void MarkdownViewer::scrollToAnchor(const QString& anchor) {
    textBrowser_->scrollToAnchor(anchor);
}

void MarkdownViewer::scrollToTop() {
    textBrowser_->verticalScrollBar()->setValue(0);
}

void MarkdownViewer::scrollToBottom() {
    textBrowser_->verticalScrollBar()->setValue(
        textBrowser_->verticalScrollBar()->maximum());
}

void MarkdownViewer::ensureVisible(int position) {
    QTextCursor cursor(textBrowser_->document());
    cursor.setPosition(position);
    textBrowser_->setTextCursor(cursor);
    textBrowser_->ensureCursorVisible();
}

void MarkdownViewer::exportToPdf(const QString& filePath) {
    QPrinter printer(QPrinter::HighResolution);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOutputFileName(filePath);
    textBrowser_->document()->print(&printer);
}

void MarkdownViewer::exportToHtml(const QString& filePath) {
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&file);
        stream << toHtml();
    }
}

void MarkdownViewer::print() {
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, this);
    if (dialog.exec() == QDialog::Accepted) {
        textBrowser_->document()->print(&printer);
    }
}

void MarkdownViewer::setLinkColor(const QColor& color) {
    // Update stylesheet with new link color
    applyStyleSheet();
}

void MarkdownViewer::setCodeBlockStyle(const QString& style) {
    codeBlockStyle_ = style;
    applyStyleSheet();
}

void MarkdownViewer::setLineSpacing(qreal spacing) {
    QTextBlockFormat format;
    format.setLineHeight(spacing * 100, QTextBlockFormat::ProportionalHeight);
    
    QTextCursor cursor(textBrowser_->document());
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(format);
}

void MarkdownViewer::setDocumentMargins(const QMargins& margins) {
    textBrowser_->document()->setDocumentMargin(margins.top());
}

QStringList MarkdownViewer::tableOfContents() const {
    if (currentContentType_ == "markdown") {
        return markdownProcessor_->extractTableOfContents(currentContent_);
    }
    
    // Extract from HTML
    QStringList toc;
    QTextDocument* doc = textBrowser_->document();
    QTextCursor cursor(doc);
    
    while (!cursor.atEnd()) {
        QTextBlock block = cursor.block();
        if (block.blockFormat().headingLevel() > 0) {
            toc.append(QString("%1 %2")
                      .arg(QString(block.blockFormat().headingLevel(), '#'))
                      .arg(block.text()));
        }
        cursor.movePosition(QTextCursor::NextBlock);
    }
    
    return toc;
}

void MarkdownViewer::scrollToHeading(int level, const QString& text) {
    QTextDocument* doc = textBrowser_->document();
    QTextCursor cursor(doc);
    
    while (!cursor.atEnd()) {
        QTextBlock block = cursor.block();
        if (block.blockFormat().headingLevel() == level && 
            block.text().contains(text, Qt::CaseInsensitive)) {
            textBrowser_->setTextCursor(cursor);
            textBrowser_->ensureCursorVisible();
            break;
        }
        cursor.movePosition(QTextCursor::NextBlock);
    }
}

void MarkdownViewer::refresh() {
    if (currentContentType_ == "markdown") {
        setMarkdown(currentContent_);
    } else if (currentContentType_ == "html") {
        setHtml(currentContent_);
    }
}

void MarkdownViewer::updateTheme() {
    applyStyleSheet();
    refresh();
}

void MarkdownViewer::paintContent(QPainter* painter) {
    // Base class handles background
    // We don't need to paint anything extra
}

void MarkdownViewer::resizeEvent(QResizeEvent* event) {
    BaseStyledWidget::resizeEvent(event);
    
    // Update image max width based on widget width
    if (imageCachingEnabled_) {
        maxImageWidth_ = width() - 2 * Design::SPACING_MD;
        processImages();
    }
}

void MarkdownViewer::contextMenuEvent(QContextMenuEvent* event) {
    copyAction_->setEnabled(hasSelection());
    contextMenu_->exec(event->globalPos());
}

void MarkdownViewer::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        // Zoom with Ctrl+Wheel
        int delta = event->angleDelta().y();
        if (delta > 0) {
            zoomIn();
        } else if (delta < 0) {
            zoomOut();
        }
        event->accept();
    } else {
        BaseStyledWidget::wheelEvent(event);
    }
}

void MarkdownViewer::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::Find)) {
        findAction_->trigger();
    } else if (event->key() == Qt::Key_F3) {
        if (event->modifiers() & Qt::ShiftModifier) {
            navigateToPreviousMatch();
        } else {
            navigateToNextMatch();
        }
    } else if (event->key() == Qt::Key_Escape) {
        clearSearch();
    } else {
        BaseStyledWidget::keyPressEvent(event);
    }
}

void MarkdownViewer::onThemeChanged() {
    BaseStyledWidget::onThemeChanged();
    updateTheme();
}

void MarkdownViewer::onLinkClicked(const QUrl& url) {
    if (url.scheme() == "http" || url.scheme() == "https") {
        if (openExternalLinks_) {
            QDesktopServices::openUrl(url);
        }
    } else if (url.scheme() == "file") {
        // Handle file links
        if (openLinksInternally_) {
            emit linkClicked(url);
        } else {
            QDesktopServices::openUrl(url);
        }
    } else {
        // Internal anchor or custom scheme
        emit linkClicked(url);
    }
}

void MarkdownViewer::onCursorPositionChanged() {
    bool hasSelection = textBrowser_->textCursor().hasSelection();
    emit selectionChanged();
    emit copyAvailable(hasSelection);
}

void MarkdownViewer::onTextChanged() {
    // Process content after text changes
    processContent();
}

void MarkdownViewer::onScrollPositionChanged() {
    emit scrollPositionChanged();
}

void MarkdownViewer::processContent() {
    if (syntaxHighlightingEnabled_) {
        highlightCodeBlocks();
    }
    processImages();
    updateDocumentLayout();
}

void MarkdownViewer::highlightCodeBlocks() {
    // Find all code blocks and apply syntax highlighting
    QTextDocument* doc = textBrowser_->document();
    QTextCursor cursor(doc);
    
    // Find <pre> blocks
    while (!cursor.isNull() && !cursor.atEnd()) {
        cursor = doc->find("<pre", cursor);
        if (!cursor.isNull()) {
            // Extract language from class attribute if present
            QTextBlock block = cursor.block();
            QString blockText = block.text();
            
            QRegularExpression langRegex("class=\"language-(\\w+)\"");
            QRegularExpressionMatch match = langRegex.match(blockText);
            
            QString language = defaultCodeLanguage_;
            if (match.hasMatch()) {
                language = match.captured(1).toLower();
            }
            
            // Apply highlighter if available
            auto it = languageHighlighters_.find(language);
            if (it != languageHighlighters_.end()) {
                it->second(doc, blockText);
            }
        }
    }
}

void MarkdownViewer::processImages() {
    if (!imageCachingEnabled_) return;
    
    // Find and resize images
    QTextDocument* doc = textBrowser_->document();
    QTextCursor cursor(doc);
    
    while (!cursor.isNull() && !cursor.atEnd()) {
        cursor = doc->find("<img", cursor);
        if (!cursor.isNull()) {
            // Get the image format
            QTextImageFormat imageFormat = cursor.charFormat().toImageFormat();
            if (imageFormat.isValid()) {
                QString imageName = imageFormat.name();
                
                // Check if image is already cached
                QPixmap pixmap;
                if (!QPixmapCache::find(imageName, &pixmap)) {
                    // Load image
                    if (imageName.startsWith("http://") || imageName.startsWith("https://")) {
                        // For network images, we would need async loading
                        // For now, skip network images
                        continue;
                    } else {
                        pixmap.load(imageName);
                        if (!pixmap.isNull()) {
                            // Resize if too large
                            if (pixmap.width() > maxImageWidth_) {
                                pixmap = pixmap.scaledToWidth(maxImageWidth_, Qt::SmoothTransformation);
                            }
                            
                            // Cache the processed image
                            QPixmapCache::insert(imageName, pixmap);
                        }
                    }
                }
                
                // Update the image in the document if we have a cached version
                if (!pixmap.isNull()) {
                    imageFormat.setWidth(pixmap.width());
                    imageFormat.setHeight(pixmap.height());
                    cursor.setCharFormat(imageFormat);
                }
            }
        }
    }
}

void MarkdownViewer::updateDocumentLayout() {
    // Force layout update
    textBrowser_->document()->adjustSize();
}

// MarkdownProcessor implementation

MarkdownProcessor::MarkdownProcessor(QObject* parent)
    : QObject(parent) {
    
    // Initialize regex patterns
    codeBlockRegex_ = QRegularExpression(
        "```(\\w*)\\n([\\s\\S]*?)\\n```",
        QRegularExpression::MultilineOption);
    
    taskListRegex_ = QRegularExpression(
        "^\\s*[-*+]\\s+\\[([ xX])\\]\\s+(.*)$",
        QRegularExpression::MultilineOption);
    
    emojiRegex_ = QRegularExpression(":([a-zA-Z0-9_+-]+):");
    
    footnoteRegex_ = QRegularExpression("\\[\\^(\\d+)\\]");
    
    mathBlockRegex_ = QRegularExpression(
        "\\$\\$([\\s\\S]*?)\\$\\$",
        QRegularExpression::MultilineOption);
    
    mathInlineRegex_ = QRegularExpression("\\$([^\\$]+)\\$");
    
    // Default code block template
    codeBlockTemplate_ = R"(
<div class="code-block">
    <div class="code-header">%1</div>
    <pre class="language-%1"><code>%2</code></pre>
</div>
)";
}

QString MarkdownProcessor::processMarkdown(const QString& markdown) {
    QString processed = preprocessMarkdown(markdown);
    
    // Convert to HTML using Qt's built-in converter
    QTextDocument doc;
    doc.setMarkdown(processed);
    QString html = doc.toHtml();
    
    return postprocessHtml(html);
}

QString MarkdownProcessor::preprocessMarkdown(const QString& markdown) {
    QString result = markdown;
    
    if (enableTaskLists_) {
        result = processTaskLists(result);
    }
    
    if (enableEmoji_) {
        result = processEmoji(result);
    }
    
    if (enableFootnotes_) {
        result = processFootnotes(result);
    }
    
    if (enableMath_) {
        result = processMath(result);
    }
    
    result = processCodeBlocks(result);
    
    return result;
}

QString MarkdownProcessor::postprocessHtml(const QString& html) {
    QString result = html;
    
    if (enableHeadingAnchors_) {
        result = addHeadingAnchors(result);
    }
    
    if (enableTableStyling_) {
        result = styleTables(result);
    }
    
    return result;
}

QString MarkdownProcessor::processCodeBlocks(const QString& text) {
    QString result = text;
    
    QRegularExpressionMatchIterator it = codeBlockRegex_.globalMatch(text);
    QList<QPair<int, int>> replacements;
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        QString language = match.captured(1);
        QString code = match.captured(2);
        
        if (language.isEmpty()) {
            language = "text";
        }
        
        QString replacement = codeBlockTemplate_
            .arg(language)
            .arg(UIUtils::escapeHtml(code));
        
        replacements.prepend({match.capturedStart(), match.capturedLength()});
    }
    
    // Apply replacements in reverse order
    for (const auto& rep : replacements) {
        result.replace(rep.first, rep.second, 
                      codeBlockTemplate_.arg("text").arg("CODE"));
    }
    
    return result;
}

QString MarkdownProcessor::processTaskLists(const QString& text) {
    QStringList lines = text.split('\n');
    
    for (int i = 0; i < lines.size(); ++i) {
        QRegularExpressionMatch match = taskListRegex_.match(lines[i]);
        if (match.hasMatch()) {
            bool checked = match.captured(1).trimmed().toLower() == "x";
            QString task = match.captured(2);
            
            lines[i] = QString("<label><input type='checkbox' disabled %1> %2</label>")
                      .arg(checked ? "checked" : "")
                      .arg(task);
        }
    }
    
    return lines.join('\n');
}

QString MarkdownProcessor::processEmoji(const QString& text) {
    // Simple emoji replacement - could be extended with full emoji database
    QString result = text;
    
    // Common emojis
    result.replace(":smile:", "😊");
    result.replace(":thumbsup:", "👍");
    result.replace(":warning:", "⚠️");
    result.replace(":info:", "ℹ️");
    result.replace(":check:", "✓");
    result.replace(":x:", "✗");
    
    return result;
}

QString MarkdownProcessor::processFootnotes(const QString& text) {
    QString result = text;
    QMap<QString, QString> footnotes;
    
    // First pass: find footnote definitions [^1]: text
    QRegularExpression defRegex("\\[\\^(\\d+)\\]:\\s*(.+)$", QRegularExpression::MultilineOption);
    QRegularExpressionMatchIterator defIt = defRegex.globalMatch(result);
    
    while (defIt.hasNext()) {
        QRegularExpressionMatch match = defIt.next();
        QString id = match.captured(1);
        QString content = match.captured(2);
        footnotes[id] = content;
    }
    
    // Remove footnote definitions from text
    result.remove(defRegex);
    
    // Second pass: replace footnote references with superscript links
    QRegularExpressionMatchIterator refIt = footnoteRegex_.globalMatch(result);
    QList<QPair<int, int>> replacements;
    
    while (refIt.hasNext()) {
        QRegularExpressionMatch match = refIt.next();
        QString id = match.captured(1);
        
        if (footnotes.contains(id)) {
            QString replacement = QString("<sup><a href=\"#fn%1\" id=\"fnref%1\">[%1]</a></sup>").arg(id);
            replacements.prepend({match.capturedStart(), match.capturedLength()});
        }
    }
    
    // Apply replacements in reverse order
    for (const auto& rep : replacements) {
        QString id = result.mid(rep.first + 2, rep.second - 3); // Extract ID from [^ID]
        if (footnotes.contains(id)) {
            QString replacement = QString("<sup><a href=\"#fn%1\" id=\"fnref%1\">[%1]</a></sup>").arg(id);
            result.replace(rep.first, rep.second, replacement);
        }
    }
    
    // Add footnotes section at the end if any footnotes exist
    if (!footnotes.isEmpty()) {
        result += "\n\n<hr>\n<ol class=\"footnotes\">\n";
        for (auto it = footnotes.begin(); it != footnotes.end(); ++it) {
            result += QString("<li id=\"fn%1\">%2 <a href=\"#fnref%1\">↩</a></li>\n")
                     .arg(it.key())
                     .arg(it.value());
        }
        result += "</ol>\n";
    }
    
    return result;
}

QString MarkdownProcessor::processMath(const QString& text) {
    QString result = text;
    
    // Process display math blocks $$...$$
    QRegularExpressionMatchIterator blockIt = mathBlockRegex_.globalMatch(result);
    QList<QPair<int, int>> blockReplacements;
    
    while (blockIt.hasNext()) {
        QRegularExpressionMatch match = blockIt.next();
        QString math = match.captured(1).trimmed();
        
        // Wrap in a div with class for styling
        QString replacement = QString("<div class=\"math-block\" data-math=\"%1\">$$%1$$</div>")
                             .arg(UIUtils::escapeHtml(math));
        
        blockReplacements.prepend({match.capturedStart(), match.capturedLength()});
    }
    
    // Apply block replacements in reverse order
    for (const auto& rep : blockReplacements) {
        QRegularExpressionMatch match = mathBlockRegex_.match(result, rep.first);
        if (match.hasMatch()) {
            QString math = match.captured(1).trimmed();
            QString replacement = QString("<div class=\"math-block\" data-math=\"%1\">$$%1$$</div>")
                                 .arg(UIUtils::escapeHtml(math));
            result.replace(rep.first, rep.second, replacement);
        }
    }
    
    // Process inline math $...$
    QRegularExpression inlineRegex("\\$([^$\\n]+)\\$");
    QRegularExpressionMatchIterator inlineIt = inlineRegex.globalMatch(result);
    QList<QPair<int, int>> inlineReplacements;
    
    while (inlineIt.hasNext()) {
        QRegularExpressionMatch match = inlineIt.next();
        QString math = match.captured(1);
        
        // Skip if it looks like currency (e.g., $5.99)
        if (QRegularExpression("^\\d+\\.?\\d*$").match(math).hasMatch()) {
            continue;
        }
        
        inlineReplacements.prepend({match.capturedStart(), match.capturedLength()});
    }
    
    // Apply inline replacements in reverse order
    for (const auto& rep : inlineReplacements) {
        QRegularExpressionMatch match = inlineRegex.match(result, rep.first);
        if (match.hasMatch()) {
            QString math = match.captured(1);
            QString replacement = QString("<span class=\"math-inline\" data-math=\"%1\">$%1$</span>")
                                 .arg(UIUtils::escapeHtml(math));
            result.replace(rep.first, rep.second, replacement);
        }
    }
    
    return result;
}

QString MarkdownProcessor::addHeadingAnchors(const QString& html) {
    QString result = html;
    
    QRegularExpression headingRegex("<h([1-6])>(.*?)</h\\1>");
    QRegularExpressionMatchIterator it = headingRegex.globalMatch(html);
    
    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        int level = match.captured(1).toInt();
        QString text = match.captured(2);
        
        // Create anchor from heading text
        QString anchor = text.toLower()
            .replace(QRegularExpression("[^a-z0-9\\s-]"), "")
            .replace(QRegularExpression("\\s+"), "-");
        
        QString replacement = QString("<h%1 id='%2'>%3</h%1>")
            .arg(level).arg(anchor).arg(text);
        
        result.replace(match.captured(), replacement);
    }
    
    return result;
}

QString MarkdownProcessor::styleTables(const QString& html) {
    QString result = html;
    
    // Add Bootstrap-like table classes
    result.replace("<table>", "<table class='table table-striped'>");
    
    return result;
}

QStringList MarkdownProcessor::extractTableOfContents(const QString& markdown) {
    QStringList toc;
    QStringList lines = markdown.split('\n');
    
    QRegularExpression headingRegex("^(#{1,6})\\s+(.+)$");
    
    for (const QString& line : lines) {
        QRegularExpressionMatch match = headingRegex.match(line);
        if (match.hasMatch()) {
            QString prefix = match.captured(1);
            QString text = match.captured(2);
            toc.append(QString("%1 %2").arg(prefix).arg(text));
        }
    }
    
    return toc;
}

// CodeBlockHighlighter implementation

CodeBlockHighlighter::CodeBlockHighlighter(QTextDocument* parent)
    : QSyntaxHighlighter(parent) {
    setupRules();
}

void CodeBlockHighlighter::setLanguage(const QString& language) {
    language_ = language.toLower();
    setupRules();
    rehighlight();
}

void CodeBlockHighlighter::setTheme(const QString& theme) {
    theme_ = theme;
    setupRules();
    rehighlight();
}

void CodeBlockHighlighter::setupRules() {
    const auto& colors = ThemeManager::instance().colors();
    
    // Setup format styles
    keywordFormat_.setForeground(colors.syntaxKeyword);
    keywordFormat_.setFontWeight(QFont::Bold);
    
    stringFormat_.setForeground(colors.syntaxString);
    
    commentFormat_.setForeground(colors.syntaxComment);
    commentFormat_.setFontItalic(true);
    
    numberFormat_.setForeground(colors.syntaxNumber);
    
    functionFormat_.setForeground(colors.syntaxFunction);
    
    variableFormat_.setForeground(colors.syntaxVariable);
    
    operatorFormat_.setForeground(colors.syntaxOperator);
    
    preprocessorFormat_.setForeground(colors.syntaxKeyword);
    preprocessorFormat_.setFontWeight(QFont::Bold);
}

void CodeBlockHighlighter::highlightBlock(const QString& text) {
    if (language_ == "cpp" || language_ == "c++") {
        highlightCpp(text);
    } else if (language_ == "python" || language_ == "py") {
        highlightPython(text);
    } else if (language_ == "javascript" || language_ == "js") {
        highlightJavaScript(text);
    } else if (language_ == "json") {
        highlightJson(text);
    } else if (language_ == "xml" || language_ == "html") {
        highlightXml(text);
    } else if (language_ == "markdown" || language_ == "md") {
        highlightMarkdown(text);
    } else if (language_ == "bash" || language_ == "sh") {
        highlightBash(text);
    } else if (language_ == "asm" || language_ == "assembly") {
        highlightAsm(text);
    }
    // Default: no highlighting for unknown languages
}

void CodeBlockHighlighter::highlightCpp(const QString& text) {
    // C++ keywords
    QStringList keywords = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand",
        "bitor", "bool", "break", "case", "catch", "char", "char16_t",
        "char32_t", "class", "compl", "const", "constexpr", "const_cast",
        "continue", "decltype", "default", "delete", "do", "double",
        "dynamic_cast", "else", "enum", "explicit", "export", "extern",
        "false", "float", "for", "friend", "goto", "if", "inline", "int",
        "long", "mutable", "namespace", "new", "noexcept", "not", "not_eq",
        "nullptr", "operator", "or", "or_eq", "private", "protected",
        "public", "register", "reinterpret_cast", "return", "short",
        "signed", "sizeof", "static", "static_assert", "static_cast",
        "struct", "switch", "template", "this", "thread_local", "throw",
        "true", "try", "typedef", "typeid", "typename", "union", "unsigned",
        "using", "virtual", "void", "volatile", "wchar_t", "while", "xor",
        "xor_eq"
    };
    
    // Apply keyword highlighting
    for (const QString& keyword : keywords) {
        QRegularExpression regex("\\b" + keyword + "\\b");
        QRegularExpressionMatchIterator it = regex.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), keywordFormat_);
        }
    }
    
    // Strings
    QRegularExpression stringRegex("\"([^\"\\\\]|\\\\.)*\"");
    QRegularExpressionMatchIterator stringIt = stringRegex.globalMatch(text);
    while (stringIt.hasNext()) {
        QRegularExpressionMatch match = stringIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), stringFormat_);
    }
    
    // Comments
    QRegularExpression singleLineComment("//[^\n]*");
    QRegularExpressionMatchIterator commentIt = singleLineComment.globalMatch(text);
    while (commentIt.hasNext()) {
        QRegularExpressionMatch match = commentIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), commentFormat_);
    }
    
    // Numbers
    QRegularExpression numberRegex("\\b[0-9]+\\.?[0-9]*\\b");
    QRegularExpressionMatchIterator numberIt = numberRegex.globalMatch(text);
    while (numberIt.hasNext()) {
        QRegularExpressionMatch match = numberIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), numberFormat_);
    }
    
    // Preprocessor
    if (text.trimmed().startsWith('#')) {
        setFormat(0, text.length(), preprocessorFormat_);
    }
}

void CodeBlockHighlighter::highlightPython(const QString& text) {
    // Python keywords
    QStringList keywords = {
        "False", "None", "True", "and", "as", "assert", "async", "await",
        "break", "class", "continue", "def", "del", "elif", "else", "except",
        "finally", "for", "from", "global", "if", "import", "in", "is",
        "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
        "while", "with", "yield"
    };
    
    // Apply keyword highlighting
    for (const QString& keyword : keywords) {
        QRegularExpression regex("\\b" + keyword + "\\b");
        QRegularExpressionMatchIterator it = regex.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), keywordFormat_);
        }
    }
    
    // Strings (single and double quotes)
    QRegularExpression stringRegex("(['\"])([^\\1\\\\]|\\\\.)*\\1");
    QRegularExpressionMatchIterator stringIt = stringRegex.globalMatch(text);
    while (stringIt.hasNext()) {
        QRegularExpressionMatch match = stringIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), stringFormat_);
    }
    
    // Comments
    QRegularExpression commentRegex("#[^\n]*");
    QRegularExpressionMatchIterator commentIt = commentRegex.globalMatch(text);
    while (commentIt.hasNext()) {
        QRegularExpressionMatch match = commentIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), commentFormat_);
    }
    
    // Numbers
    QRegularExpression numberRegex("\\b[0-9]+\\.?[0-9]*\\b");
    QRegularExpressionMatchIterator numberIt = numberRegex.globalMatch(text);
    while (numberIt.hasNext()) {
        QRegularExpressionMatch match = numberIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), numberFormat_);
    }
    
    // Function definitions
    QRegularExpression funcRegex("def\\s+([a-zA-Z_][a-zA-Z0-9_]*)\\s*\\(");
    QRegularExpressionMatchIterator funcIt = funcRegex.globalMatch(text);
    while (funcIt.hasNext()) {
        QRegularExpressionMatch match = funcIt.next();
        setFormat(match.capturedStart(1), match.capturedLength(1), functionFormat_);
    }
}

void CodeBlockHighlighter::highlightJavaScript(const QString& text) {
    // JavaScript keywords
    QStringList keywords = {
        "async", "await", "break", "case", "catch", "class", "const",
        "continue", "debugger", "default", "delete", "do", "else", "export",
        "extends", "finally", "for", "function", "if", "import", "in",
        "instanceof", "let", "new", "return", "super", "switch", "this",
        "throw", "try", "typeof", "var", "void", "while", "with", "yield"
    };
    
    // Apply highlighting similar to C++
    for (const QString& keyword : keywords) {
        QRegularExpression regex("\\b" + keyword + "\\b");
        QRegularExpressionMatchIterator it = regex.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), keywordFormat_);
        }
    }
    
    // Rest of highlighting similar to other languages...
}

void CodeBlockHighlighter::highlightJson(const QString& text) {
    // JSON is simple - just strings, numbers, and keywords
    QStringList keywords = {"true", "false", "null"};
    
    for (const QString& keyword : keywords) {
        QRegularExpression regex("\\b" + keyword + "\\b");
        QRegularExpressionMatchIterator it = regex.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), keywordFormat_);
        }
    }
    
    // Strings
    QRegularExpression stringRegex("\"([^\"\\\\]|\\\\.)*\"");
    QRegularExpressionMatchIterator stringIt = stringRegex.globalMatch(text);
    while (stringIt.hasNext()) {
        QRegularExpressionMatch match = stringIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), stringFormat_);
    }
    
    // Numbers
    QRegularExpression numberRegex("-?\\b[0-9]+\\.?[0-9]*([eE][+-]?[0-9]+)?\\b");
    QRegularExpressionMatchIterator numberIt = numberRegex.globalMatch(text);
    while (numberIt.hasNext()) {
        QRegularExpressionMatch match = numberIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), numberFormat_);
    }
}

void CodeBlockHighlighter::highlightXml(const QString& text) {
    // XML/HTML tags
    QRegularExpression tagRegex("<[^>]+>");
    QRegularExpressionMatchIterator tagIt = tagRegex.globalMatch(text);
    while (tagIt.hasNext()) {
        QRegularExpressionMatch match = tagIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), keywordFormat_);
    }
    
    // Attributes
    QRegularExpression attrRegex("\\w+=");
    QRegularExpressionMatchIterator attrIt = attrRegex.globalMatch(text);
    while (attrIt.hasNext()) {
        QRegularExpressionMatch match = attrIt.next();
        setFormat(match.capturedStart(), match.capturedLength() - 1, variableFormat_);
    }
    
    // Strings in attributes
    QRegularExpression stringRegex("=\"([^\"]*)\"");
    QRegularExpressionMatchIterator stringIt = stringRegex.globalMatch(text);
    while (stringIt.hasNext()) {
        QRegularExpressionMatch match = stringIt.next();
        setFormat(match.capturedStart() + 1, match.capturedLength() - 1, stringFormat_);
    }
    
    // Comments
    QRegularExpression commentRegex("<!--[\\s\\S]*?-->");
    QRegularExpressionMatchIterator commentIt = commentRegex.globalMatch(text);
    while (commentIt.hasNext()) {
        QRegularExpressionMatch match = commentIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), commentFormat_);
    }
}

void CodeBlockHighlighter::highlightMarkdown(const QString& text) {
    // Headers
    if (text.startsWith('#')) {
        setFormat(0, text.length(), keywordFormat_);
        return;
    }
    
    // Bold
    QRegularExpression boldRegex("\\*\\*([^*]+)\\*\\*|__([^_]+)__");
    QRegularExpressionMatchIterator boldIt = boldRegex.globalMatch(text);
    while (boldIt.hasNext()) {
        QRegularExpressionMatch match = boldIt.next();
        QTextCharFormat format = keywordFormat_;
        format.setFontWeight(QFont::Bold);
        setFormat(match.capturedStart(), match.capturedLength(), format);
    }
    
    // Italic
    QRegularExpression italicRegex("\\*([^*]+)\\*|_([^_]+)_");
    QRegularExpressionMatchIterator italicIt = italicRegex.globalMatch(text);
    while (italicIt.hasNext()) {
        QRegularExpressionMatch match = italicIt.next();
        QTextCharFormat format = keywordFormat_;
        format.setFontItalic(true);
        setFormat(match.capturedStart(), match.capturedLength(), format);
    }
    
    // Code
    QRegularExpression codeRegex("`([^`]+)`");
    QRegularExpressionMatchIterator codeIt = codeRegex.globalMatch(text);
    while (codeIt.hasNext()) {
        QRegularExpressionMatch match = codeIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), stringFormat_);
    }
    
    // Links
    QRegularExpression linkRegex("\\[([^\\]]+)\\]\\(([^)]+)\\)");
    QRegularExpressionMatchIterator linkIt = linkRegex.globalMatch(text);
    while (linkIt.hasNext()) {
        QRegularExpressionMatch match = linkIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), functionFormat_);
    }
}

void CodeBlockHighlighter::highlightBash(const QString& text) {
    // Bash keywords
    QStringList keywords = {
        "if", "then", "else", "elif", "fi", "for", "while", "do", "done",
        "case", "esac", "function", "return", "in", "local", "export",
        "echo", "cd", "ls", "rm", "mv", "cp", "mkdir", "touch", "grep",
        "sed", "awk", "cat", "less", "more", "head", "tail", "sort", "uniq"
    };
    
    // Apply keyword highlighting
    for (const QString& keyword : keywords) {
        QRegularExpression regex("\\b" + keyword + "\\b");
        QRegularExpressionMatchIterator it = regex.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), keywordFormat_);
        }
    }
    
    // Variables
    QRegularExpression varRegex("\\$[a-zA-Z_][a-zA-Z0-9_]*|\\${[^}]+}");
    QRegularExpressionMatchIterator varIt = varRegex.globalMatch(text);
    while (varIt.hasNext()) {
        QRegularExpressionMatch match = varIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), variableFormat_);
    }
    
    // Strings
    QRegularExpression stringRegex("\"([^\"\\\\]|\\\\.)*\"|'([^'\\\\]|\\\\.)*'");
    QRegularExpressionMatchIterator stringIt = stringRegex.globalMatch(text);
    while (stringIt.hasNext()) {
        QRegularExpressionMatch match = stringIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), stringFormat_);
    }
    
    // Comments
    if (text.trimmed().startsWith('#')) {
        setFormat(0, text.length(), commentFormat_);
    }
}

void CodeBlockHighlighter::highlightAsm(const QString& text) {
    // Assembly instructions (common x86/x64)
    QStringList instructions = {
        "mov", "push", "pop", "lea", "add", "sub", "inc", "dec", "mul",
        "div", "and", "or", "xor", "not", "shl", "shr", "rol", "ror",
        "cmp", "test", "jmp", "je", "jne", "jz", "jnz", "ja", "jb", "jg",
        "jl", "jge", "jle", "call", "ret", "nop", "int", "syscall"
    };
    
    // Apply instruction highlighting
    for (const QString& inst : instructions) {
        QRegularExpression regex("\\b" + inst + "\\b", QRegularExpression::CaseInsensitiveOption);
        QRegularExpressionMatchIterator it = regex.globalMatch(text);
        while (it.hasNext()) {
            QRegularExpressionMatch match = it.next();
            setFormat(match.capturedStart(), match.capturedLength(), keywordFormat_);
        }
    }
    
    // Registers
    QRegularExpression regRegex("\\b(rax|rbx|rcx|rdx|rsi|rdi|rbp|rsp|r[0-9]+|eax|ebx|ecx|edx|esi|edi|ebp|esp|ax|bx|cx|dx|al|ah|bl|bh|cl|ch|dl|dh)\\b",
                               QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator regIt = regRegex.globalMatch(text);
    while (regIt.hasNext()) {
        QRegularExpressionMatch match = regIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), variableFormat_);
    }
    
    // Numbers (hex and decimal)
    QRegularExpression numRegex("\\b(0x[0-9a-fA-F]+|[0-9]+)\\b");
    QRegularExpressionMatchIterator numIt = numRegex.globalMatch(text);
    while (numIt.hasNext()) {
        QRegularExpressionMatch match = numIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), numberFormat_);
    }
    
    // Comments
    QRegularExpression commentRegex(";[^\n]*");
    QRegularExpressionMatchIterator commentIt = commentRegex.globalMatch(text);
    while (commentIt.hasNext()) {
        QRegularExpressionMatch match = commentIt.next();
        setFormat(match.capturedStart(), match.capturedLength(), commentFormat_);
    }
    
    // Labels
    if (text.trimmed().endsWith(':')) {
        setFormat(0, text.indexOf(':') + 1, functionFormat_);
    }
}

} // namespace llm_re::ui_v2