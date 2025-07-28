#include "../core/ui_v2_common.h"
#include "floating_inspector.h"
#include "../core/theme_manager.h"

namespace llm_re::ui_v2 {

// FloatingInspector implementation
FloatingInspector::FloatingInspector(QWidget* parent)
    : BaseStyledWidget(parent)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    
    setupUI();
    
    // Create effects
    opacityEffect_ = new QGraphicsOpacityEffect(this);
    opacityEffect_->setOpacity(opacity_);
    setGraphicsEffect(opacityEffect_);
    
    // Create animations
    fadeAnimation_ = new QPropertyAnimation(opacityEffect_, "opacity", this);
    fadeAnimation_->setDuration(animationDuration_);
    fadeAnimation_->setEasingCurve(QEasingCurve::InOutQuad);
    
    moveAnimation_ = new QPropertyAnimation(this, "pos", this);
    moveAnimation_->setDuration(animationDuration_);
    moveAnimation_->setEasingCurve(QEasingCurve::InOutQuad);
    
    sizeAnimation_ = new QPropertyAnimation(this, "size", this);
    sizeAnimation_->setDuration(animationDuration_);
    sizeAnimation_->setEasingCurve(QEasingCurve::InOutQuad);
    
    // Create timer
    autoHideTimer_ = new QTimer(this);
    autoHideTimer_->setSingleShot(true);
    connect(autoHideTimer_, &QTimer::timeout, this, &FloatingInspector::onAutoHideTimeout);
    
    // Connect animation finished
    connect(fadeAnimation_, &QPropertyAnimation::finished, this, &FloatingInspector::onAnimationFinished);
    
    // Initial size
    setMode(CompactMode);
    hide();
}

FloatingInspector::~FloatingInspector()
{
    removeGlobalEventFilter();
}

void FloatingInspector::setupUI()
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    
    createHeader();
    createContent();
    createFooter();
    
    layout->addWidget(headerWidget_);
    layout->addWidget(contentStack_, 1);
    layout->addWidget(footerWidget_);
}

void FloatingInspector::createHeader()
{
    headerWidget_ = new QWidget(this);
    headerWidget_->setObjectName("inspectorHeader");
    
    auto* layout = new QHBoxLayout(headerWidget_);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);
    
    iconLabel_ = new QLabel(this);
    iconLabel_->setFixedSize(16, 16);
    iconLabel_->setScaledContents(true);
    
    titleLabel_ = new QLabel("Inspector", this);
    titleLabel_->setObjectName("inspectorTitle");
    
    layout->addWidget(iconLabel_);
    layout->addWidget(titleLabel_, 1);
    
    // Mode button
    modeButton_ = new QToolButton(this);
    modeButton_->setIcon(QIcon(":/icons/view-mode.png"));
    modeButton_->setToolTip("Change view mode");
    modeButton_->setAutoRaise(true);
    connect(modeButton_, &QToolButton::clicked, this, &FloatingInspector::onModeButtonClicked);
    
    // Pin button
    pinButton_ = new QToolButton(this);
    pinButton_->setIcon(QIcon(":/icons/pin.png"));
    pinButton_->setToolTip("Pin window");
    pinButton_->setCheckable(true);
    pinButton_->setAutoRaise(true);
    connect(pinButton_, &QToolButton::clicked, this, &FloatingInspector::onPinButtonClicked);
    
    // Close button
    closeButton_ = new QToolButton(this);
    closeButton_->setIcon(QIcon(":/icons/close.png"));
    closeButton_->setToolTip("Close");
    closeButton_->setAutoRaise(true);
    connect(closeButton_, &QToolButton::clicked, this, &FloatingInspector::onCloseButtonClicked);
    
    layout->addWidget(modeButton_);
    layout->addWidget(pinButton_);
    layout->addWidget(closeButton_);
}

void FloatingInspector::createContent()
{
    contentStack_ = new QStackedWidget(this);
    
    // Message widget
    messageWidget_ = new QWidget(this);
    auto* messageLayout = new QVBoxLayout(messageWidget_);
    
    roleLabel_ = new QLabel(this);
    roleLabel_->setObjectName("messageRole");
    messageLayout->addWidget(roleLabel_);
    
    messageEdit_ = new QTextBrowser(this);
    messageEdit_->setReadOnly(true);
    messageEdit_->setObjectName("messageContent");
    messageEdit_->setOpenExternalLinks(false); // Handle links internally
    connect(messageEdit_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        onLinkClicked(url.toString());
    });
    messageLayout->addWidget(messageEdit_, 1);
    
    metadataList_ = new QListWidget(this);
    metadataList_->setObjectName("metadataList");
    messageLayout->addWidget(metadataList_);
    
    contentStack_->addWidget(messageWidget_);
    
    // Memory widget
    memoryWidget_ = new QWidget(this);
    auto* memoryLayout = new QVBoxLayout(memoryWidget_);
    
    addressLabel_ = new QLabel(this);
    addressLabel_->setObjectName("memoryAddress");
    memoryLayout->addWidget(addressLabel_);
    
    auto* memoryViewLayout = new QHBoxLayout();
    
    memoryHexEdit_ = new QTextEdit(this);
    memoryHexEdit_->setReadOnly(true);
    memoryHexEdit_->setFont(QFont("Consolas", 10));
    memoryHexEdit_->setObjectName("memoryHex");
    memoryViewLayout->addWidget(memoryHexEdit_);
    
    memoryAsciiEdit_ = new QTextEdit(this);
    memoryAsciiEdit_->setReadOnly(true);
    memoryAsciiEdit_->setFont(QFont("Consolas", 10));
    memoryAsciiEdit_->setObjectName("memoryAscii");
    memoryViewLayout->addWidget(memoryAsciiEdit_);
    
    memoryLayout->addLayout(memoryViewLayout, 1);
    
    memoryInfoList_ = new QListWidget(this);
    memoryInfoList_->setObjectName("memoryInfo");
    memoryLayout->addWidget(memoryInfoList_);
    
    contentStack_->addWidget(memoryWidget_);
    
    // Tool widget
    toolWidget_ = new QWidget(this);
    auto* toolLayout = new QVBoxLayout(toolWidget_);
    
    toolNameLabel_ = new QLabel(this);
    toolNameLabel_->setObjectName("toolName");
    toolLayout->addWidget(toolNameLabel_);
    
    parametersEdit_ = new QTextEdit(this);
    parametersEdit_->setReadOnly(true);
    parametersEdit_->setObjectName("toolParameters");
    toolLayout->addWidget(parametersEdit_);
    
    outputEdit_ = new QTextEdit(this);
    outputEdit_->setReadOnly(true);
    outputEdit_->setObjectName("toolOutput");
    toolLayout->addWidget(outputEdit_, 1);
    
    contentStack_->addWidget(toolWidget_);
    
    // Error widget
    errorWidget_ = new QWidget(this);
    auto* errorLayout = new QVBoxLayout(errorWidget_);
    
    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName("errorLabel");
    errorLabel_->setStyleSheet("color: #ff4444; font-weight: bold;");
    errorLayout->addWidget(errorLabel_);
    
    errorMessageEdit_ = new QTextEdit(this);
    errorMessageEdit_->setReadOnly(true);
    errorMessageEdit_->setObjectName("errorMessage");
    errorLayout->addWidget(errorMessageEdit_);
    
    stackTraceEdit_ = new QTextEdit(this);
    stackTraceEdit_->setReadOnly(true);
    stackTraceEdit_->setFont(QFont("Consolas", 9));
    stackTraceEdit_->setObjectName("stackTrace");
    errorLayout->addWidget(stackTraceEdit_);
    
    contextList_ = new QListWidget(this);
    contextList_->setObjectName("errorContext");
    errorLayout->addWidget(contextList_);
    
    contentStack_->addWidget(errorWidget_);
    
    // Metrics widget
    metricsWidget_ = new QWidget(this);
    auto* metricsLayout = new QVBoxLayout(metricsWidget_);
    
    metricsTable_ = new QTableWidget(this);
    metricsTable_->setObjectName("metricsTable");
    metricsTable_->setAlternatingRowColors(true);
    metricsLayout->addWidget(metricsTable_);
    
    contentStack_->addWidget(metricsWidget_);
    
    // Custom widget holder
    customWidget_ = new QWidget(this);
    auto* customLayout = new QVBoxLayout(customWidget_);
    customLayout->setContentsMargins(0, 0, 0, 0);
    customContentHolder_ = new QWidget(this);
    customLayout->addWidget(customContentHolder_);
    
    contentStack_->addWidget(customWidget_);
    
    // Wrap in scroll area
    scrollArea_ = new QScrollArea(this);
    scrollArea_->setWidget(contentStack_);
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setFrameShape(QFrame::NoFrame);
}

void FloatingInspector::createFooter()
{
    footerWidget_ = new QWidget(this);
    footerWidget_->setObjectName("inspectorFooter");
    
    auto* layout = new QHBoxLayout(footerWidget_);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);
    
    // History navigation
    backButton_ = new QToolButton(this);
    backButton_->setIcon(QIcon(":/icons/back.png"));
    backButton_->setToolTip("Back");
    backButton_->setEnabled(false);
    connect(backButton_, &QToolButton::clicked, this, &FloatingInspector::navigateBack);
    
    forwardButton_ = new QToolButton(this);
    forwardButton_->setIcon(QIcon(":/icons/forward.png"));
    forwardButton_->setToolTip("Forward");
    forwardButton_->setEnabled(false);
    connect(forwardButton_, &QToolButton::clicked, this, &FloatingInspector::navigateForward);
    
    layout->addWidget(backButton_);
    layout->addWidget(forwardButton_);
    
    layout->addStretch();
    
    // Search
    searchEdit_ = new QLineEdit(this);
    searchEdit_->setPlaceholderText("Search...");
    searchEdit_->setMaximumWidth(150);
    connect(searchEdit_, &QLineEdit::textChanged, this, &FloatingInspector::onSearchTextChanged);
    
    searchResultLabel_ = new QLabel(this);
    searchResultLabel_->hide();
    
    layout->addWidget(searchEdit_);
    layout->addWidget(searchResultLabel_);
}

void FloatingInspector::showMessage(const QString& role, const QString& content, const QJsonObject& metadata)
{
    currentType_ = MessageContent;
    
    titleLabel_->setText("Message");
    iconLabel_->setPixmap(QPixmap(":/icons/message.png"));
    
    roleLabel_->setText(role);
    messageEdit_->setHtml(content);
    
    metadataList_->clear();
    if (!metadata.isEmpty()) {
        for (auto it = metadata.begin(); it != metadata.end(); ++it) {
            metadataList_->addItem(QString("%1: %2").arg(it.key(), it.value().toString()));
        }
        metadataList_->setVisible(true);
    } else {
        metadataList_->setVisible(false);
    }
    
    contentStack_->setCurrentWidget(messageWidget_);
    
    // Add to history
    QJsonObject data;
    data["role"] = role;
    data["content"] = content;
    data["metadata"] = metadata;
    addToHistory(MessageContent, data);
    
    show();
}

void FloatingInspector::showMemory(const QString& address, const QJsonObject& data)
{
    currentType_ = MemoryContent;
    
    titleLabel_->setText("Memory");
    iconLabel_->setPixmap(QPixmap(":/icons/memory.png"));
    
    addressLabel_->setText(QString("Address: %1").arg(address));
    
    // Parse hex data
    QString hexData = data["hex"].toString();
    QString asciiData = data["ascii"].toString();
    
    memoryHexEdit_->setPlainText(hexData);
    memoryAsciiEdit_->setPlainText(asciiData);
    
    // Additional info
    memoryInfoList_->clear();
    if (data.contains("info")) {
        QJsonObject info = data["info"].toObject();
        for (auto it = info.begin(); it != info.end(); ++it) {
            memoryInfoList_->addItem(QString("%1: %2").arg(it.key(), it.value().toString()));
        }
    }
    
    contentStack_->setCurrentWidget(memoryWidget_);
    
    // Add to history
    QJsonObject histData = data;
    histData["address"] = address;
    addToHistory(MemoryContent, histData);
    
    show();
}

void FloatingInspector::showTool(const QString& toolName, const QJsonObject& parameters, const QString& output)
{
    currentType_ = ToolContent;
    
    titleLabel_->setText("Tool Execution");
    iconLabel_->setPixmap(QPixmap(":/icons/tool.png"));
    
    toolNameLabel_->setText(QString("Tool: %1").arg(toolName));
    
    // Format parameters
    QJsonDocument doc(parameters);
    parametersEdit_->setPlainText(doc.toJson(QJsonDocument::Indented));
    
    outputEdit_->setPlainText(output);
    
    contentStack_->setCurrentWidget(toolWidget_);
    
    // Add to history
    QJsonObject data;
    data["toolName"] = toolName;
    data["parameters"] = parameters;
    data["output"] = output;
    addToHistory(ToolContent, data);
    
    show();
}

void FloatingInspector::showError(const QString& error, const QString& stackTrace, const QJsonObject& context)
{
    currentType_ = ErrorContent;
    
    titleLabel_->setText("Error");
    iconLabel_->setPixmap(QPixmap(":/icons/error.png"));
    
    errorLabel_->setText("Error Details");
    errorMessageEdit_->setPlainText(error);
    stackTraceEdit_->setPlainText(stackTrace);
    
    contextList_->clear();
    if (!context.isEmpty()) {
        for (auto it = context.begin(); it != context.end(); ++it) {
            contextList_->addItem(QString("%1: %2").arg(it.key(), it.value().toString()));
        }
    }
    
    contentStack_->setCurrentWidget(errorWidget_);
    
    // Add to history
    QJsonObject data;
    data["error"] = error;
    data["stackTrace"] = stackTrace;
    data["context"] = context;
    addToHistory(ErrorContent, data);
    
    show();
}

void FloatingInspector::showMetrics(const QJsonObject& metrics)
{
    currentType_ = MetricsContent;
    
    titleLabel_->setText("Metrics");
    iconLabel_->setPixmap(QPixmap(":/icons/metrics.png"));
    
    // Populate table
    metricsTable_->clear();
    metricsTable_->setColumnCount(2);
    metricsTable_->setHorizontalHeaderLabels({"Metric", "Value"});
    metricsTable_->setRowCount(metrics.size());
    
    int row = 0;
    for (auto it = metrics.begin(); it != metrics.end(); ++it) {
        metricsTable_->setItem(row, 0, new QTableWidgetItem(it.key()));
        metricsTable_->setItem(row, 1, new QTableWidgetItem(it.value().toString()));
        row++;
    }
    
    metricsTable_->resizeColumnsToContents();
    
    contentStack_->setCurrentWidget(metricsWidget_);
    
    // Add to history
    addToHistory(MetricsContent, metrics);
    
    show();
}

void FloatingInspector::showCustom(const QString& title, QWidget* contentWidget)
{
    currentType_ = CustomContent;
    
    titleLabel_->setText(title);
    iconLabel_->setPixmap(QPixmap(":/icons/custom.png"));
    
    // Clear old custom content
    if (customContentHolder_->layout()) {
        QLayoutItem* item;
        while ((item = customContentHolder_->layout()->takeAt(0)) != nullptr) {
            delete item->widget();
            delete item;
        }
        delete customContentHolder_->layout();
    }
    
    // Add new content
    auto* layout = new QVBoxLayout(customContentHolder_);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(contentWidget);
    
    contentStack_->setCurrentWidget(customWidget_);
    
    // Add to history
    QJsonObject data;
    data["title"] = title;
    addToHistory(CustomContent, data);
    
    show();
}

void FloatingInspector::clear()
{
    currentType_ = NoContent;
    titleLabel_->setText("Inspector");
    iconLabel_->clear();
    hide();
}

void FloatingInspector::setMode(InspectorMode mode)
{
    if (mode_ == mode) return;
    
    mode_ = mode;
    applyMode();
    emit modeChanged(mode);
}

void FloatingInspector::applyMode()
{
    QSize newSize = sizeForMode(mode_);
    
    if (isVisible()) {
        sizeAnimation_->setStartValue(size());
        sizeAnimation_->setEndValue(newSize);
        sizeAnimation_->start();
    } else {
        resize(newSize);
    }
    
    // Update UI based on mode
    switch (mode_) {
    case CompactMode:
        metadataList_->setMaximumHeight(50);
        memoryInfoList_->setMaximumHeight(50);
        contextList_->setMaximumHeight(50);
        footerWidget_->hide();
        break;
        
    case DetailedMode:
        metadataList_->setMaximumHeight(100);
        memoryInfoList_->setMaximumHeight(100);
        contextList_->setMaximumHeight(100);
        footerWidget_->show();
        searchEdit_->hide();
        searchResultLabel_->hide();
        break;
        
    case ExpandedMode:
        metadataList_->setMaximumHeight(200);
        memoryInfoList_->setMaximumHeight(200);
        contextList_->setMaximumHeight(200);
        footerWidget_->show();
        searchEdit_->show();
        searchResultLabel_->show();
        break;
    }
}

QSize FloatingInspector::sizeForMode(InspectorMode mode) const
{
    switch (mode) {
    case CompactMode:
        return QSize(COMPACT_WIDTH, COMPACT_HEIGHT);
    case DetailedMode:
        return QSize(DETAILED_WIDTH, DETAILED_HEIGHT);
    case ExpandedMode:
        return QSize(EXPANDED_WIDTH, EXPANDED_HEIGHT);
    default:
        return QSize(COMPACT_WIDTH, COMPACT_HEIGHT);
    }
}

void FloatingInspector::setPosition(Position pos)
{
    if (position_ == pos) return;
    
    position_ = pos;
    updatePosition();
    emit positionChanged(pos);
}

void FloatingInspector::updatePosition()
{
    QPoint newPos = calculatePosition();
    
    if (isVisible() && position_ != Manual) {
        moveAnimation_->setStartValue(pos());
        moveAnimation_->setEndValue(newPos);
        moveAnimation_->start();
    } else {
        move(newPos);
    }
}

QPoint FloatingInspector::calculatePosition() const
{
    QScreen* screen = QApplication::primaryScreen();
    QRect screenRect = screen->availableGeometry();
    QSize winSize = size();
    QPoint pos;
    
    switch (position_) {
    case FollowCursor:
        pos = QCursor::pos() + QPoint(10, 10);
        break;
        
    case TopLeft:
        pos = screenRect.topLeft() + QPoint(20, 20);
        break;
        
    case TopRight:
        pos = screenRect.topRight() - QPoint(winSize.width() + 20, -20);
        break;
        
    case BottomLeft:
        pos = screenRect.bottomLeft() - QPoint(-20, winSize.height() + 20);
        break;
        
    case BottomRight:
        pos = screenRect.bottomRight() - QPoint(winSize.width() + 20, winSize.height() + 20);
        break;
        
    case Center:
        pos = screenRect.center() - QPoint(winSize.width() / 2, winSize.height() / 2);
        break;
        
    case Manual:
        return this->pos();
    }
    
    // Apply offset
    pos += offset_;
    
    // Ensure window stays on screen
    if (pos.x() + winSize.width() > screenRect.right()) {
        pos.setX(screenRect.right() - winSize.width());
    }
    if (pos.y() + winSize.height() > screenRect.bottom()) {
        pos.setY(screenRect.bottom() - winSize.height());
    }
    if (pos.x() < screenRect.left()) {
        pos.setX(screenRect.left());
    }
    if (pos.y() < screenRect.top()) {
        pos.setY(screenRect.top());
    }
    
    return pos;
}

void FloatingInspector::setAutoHide(bool autoHide)
{
    autoHide_ = autoHide;
    if (!autoHide_) {
        stopAutoHideTimer();
    }
}

void FloatingInspector::setPinned(bool pinned)
{
    if (pinned_ == pinned) return;
    
    pinned_ = pinned;
    pinButton_->setChecked(pinned);
    
    if (pinned) {
        stopAutoHideTimer();
        setWindowOpacity(1.0);
    } else {
        setWindowOpacity(opacity_);
        if (autoHide_ && isVisible()) {
            startAutoHideTimer();
        }
    }
    
    emit pinStateChanged(pinned);
}

void FloatingInspector::setOpacity(qreal opacity)
{
    opacity_ = opacity;
    if (!pinned_) {
        opacityEffect_->setOpacity(opacity);
    }
}

void FloatingInspector::animateIn()
{
    if (isVisible()) return;
    
    // Set initial state
    opacityEffect_->setOpacity(0);
    updatePosition();
    
    // Show and animate
    QWidget::show();
    
    fadeAnimation_->setStartValue(0.0);
    fadeAnimation_->setEndValue(pinned_ ? 1.0 : opacity_);
    fadeAnimation_->start();
    
    if (autoHide_ && !pinned_) {
        startAutoHideTimer();
    }
}

void FloatingInspector::animateOut()
{
    if (!isVisible()) return;
    
    fadeAnimation_->setStartValue(opacityEffect_->opacity());
    fadeAnimation_->setEndValue(0.0);
    fadeAnimation_->start();
}

void FloatingInspector::show()
{
    animateIn();
}

void FloatingInspector::hide()
{
    animateOut();
}

void FloatingInspector::toggle()
{
    if (isVisible()) {
        hide();
    } else {
        show();
    }
}

void FloatingInspector::addToHistory(ContentType type, const QJsonObject& data)
{
    // Remove items after current index
    while (history_.size() > historyIndex_ + 1) {
        history_.removeLast();
    }
    
    // Add new item
    HistoryItem item;
    item.type = type;
    item.data = data;
    item.timestamp = QDateTime::currentDateTime();
    
    switch (type) {
    case MessageContent:
        item.title = QString("Message: %1").arg(data["role"].toString());
        break;
    case MemoryContent:
        item.title = QString("Memory: %1").arg(data["address"].toString());
        break;
    case ToolContent:
        item.title = QString("Tool: %1").arg(data["toolName"].toString());
        break;
    case ErrorContent:
        item.title = "Error";
        break;
    case MetricsContent:
        item.title = "Metrics";
        break;
    case CustomContent:
        item.title = data["title"].toString();
        break;
    default:
        item.title = "Unknown";
    }
    
    history_.append(item);
    historyIndex_ = history_.size() - 1;
    
    // Limit history size
    if (history_.size() > maxHistorySize_) {
        history_.removeFirst();
        historyIndex_--;
    }
    
    updateHistoryButtons();
}

void FloatingInspector::showHistory()
{
    auto* dialog = new QDialog(this);
    dialog->setWindowTitle("Inspector History");
    dialog->resize(400, 300);
    
    auto* layout = new QVBoxLayout(dialog);
    
    auto* list = new QListWidget(dialog);
    for (int i = 0; i < history_.size(); ++i) {
        const auto& item = history_[i];
        QString text = QString("[%1] %2")
            .arg(item.timestamp.toString("hh:mm:ss"))
            .arg(item.title);
        list->addItem(text);
        
        if (i == historyIndex_) {
            list->item(i)->setFont(QFont("", -1, QFont::Bold));
        }
    }
    
    connect(list, &QListWidget::itemClicked, [this, dialog](QListWidgetItem* item) {
        int index = item->listWidget()->row(item);
        emit historyNavigated(index);
        dialog->accept();
    });
    
    layout->addWidget(list);
    
    dialog->exec();
}

void FloatingInspector::clearHistory()
{
    history_.clear();
    historyIndex_ = -1;
    updateHistoryButtons();
}

void FloatingInspector::navigateBack()
{
    if (!canNavigateBack()) return;
    
    historyIndex_--;
    const auto& item = history_[historyIndex_];
    
    // Restore content based on type
    switch (item.type) {
    case MessageContent:
        showMessage(item.data["role"].toString(), 
                   item.data["content"].toString(), 
                   item.data["metadata"].toObject());
        break;
    case MemoryContent:
        showMemory(item.data["address"].toString(), item.data);
        break;
    case ToolContent:
        showTool(item.data["toolName"].toString(),
                item.data["parameters"].toObject(),
                item.data["output"].toString());
        break;
    case ErrorContent:
        showError(item.data["error"].toString(),
                 item.data["stackTrace"].toString(),
                 item.data["context"].toObject());
        break;
    case MetricsContent:
        showMetrics(item.data);
        break;
    case CustomContent:
        // Can't restore custom content
        titleLabel_->setText(item.title);
        break;
    default:
        break;
    }
    
    // Don't add to history when navigating
    history_.removeLast();
    historyIndex_ = history_.size() - 1;
    
    updateHistoryButtons();
    emit historyNavigated(historyIndex_);
}

void FloatingInspector::navigateForward()
{
    if (!canNavigateForward()) return;
    
    historyIndex_++;
    const auto& item = history_[historyIndex_];
    
    // Similar to navigateBack
    // ... (implementation similar to navigateBack)
    
    updateHistoryButtons();
    emit historyNavigated(historyIndex_);
}

void FloatingInspector::updateHistoryButtons()
{
    backButton_->setEnabled(canNavigateBack());
    forwardButton_->setEnabled(canNavigateForward());
}

void FloatingInspector::setSearchEnabled(bool enabled)
{
    searchEdit_->setVisible(enabled && mode_ == ExpandedMode);
    searchResultLabel_->setVisible(enabled && mode_ == ExpandedMode);
}

void FloatingInspector::search(const QString& text)
{
    currentSearchText_ = text;
    searchHighlights_.clear();
    currentSearchIndex_ = -1;
    
    if (text.isEmpty()) {
        highlightSearchResults();
        return;
    }
    
    // Search in current content
    QTextEdit* targetEdit = nullptr;
    
    switch (currentType_) {
    case MessageContent:
        targetEdit = messageEdit_;
        break;
    case ToolContent:
        targetEdit = outputEdit_;
        break;
    case ErrorContent:
        targetEdit = errorMessageEdit_;
        break;
    default:
        return;
    }
    
    if (!targetEdit) return;
    
    // Find all occurrences
    QTextDocument* doc = targetEdit->document();
    QTextCursor cursor(doc);
    
    while (!cursor.isNull() && !cursor.atEnd()) {
        cursor = doc->find(text, cursor);
        
        if (!cursor.isNull()) {
            QTextEdit::ExtraSelection selection;
            selection.cursor = cursor;
            selection.format.setBackground(QColor(255, 255, 0, 80));
            searchHighlights_.append(selection);
        }
    }
    
    if (!searchHighlights_.isEmpty()) {
        currentSearchIndex_ = 0;
        searchResultLabel_->setText(QString("%1/%2").arg(1).arg(searchHighlights_.size()));
        searchResultLabel_->show();
        
        // Scroll to first result
        targetEdit->setTextCursor(searchHighlights_[0].cursor);
        targetEdit->ensureCursorVisible();
    } else {
        searchResultLabel_->setText("No results");
        searchResultLabel_->show();
    }
    
    highlightSearchResults();
}

void FloatingInspector::findNext()
{
    if (searchHighlights_.isEmpty()) return;
    
    currentSearchIndex_ = (currentSearchIndex_ + 1) % searchHighlights_.size();
    
    QTextEdit* targetEdit = nullptr;
    switch (currentType_) {
    case MessageContent: targetEdit = messageEdit_; break;
    case ToolContent: targetEdit = outputEdit_; break;
    case ErrorContent: targetEdit = errorMessageEdit_; break;
    default: return;
    }
    
    if (targetEdit) {
        targetEdit->setTextCursor(searchHighlights_[currentSearchIndex_].cursor);
        targetEdit->ensureCursorVisible();
        
        searchResultLabel_->setText(QString("%1/%2")
            .arg(currentSearchIndex_ + 1)
            .arg(searchHighlights_.size()));
    }
}

void FloatingInspector::findPrevious()
{
    if (searchHighlights_.isEmpty()) return;
    
    currentSearchIndex_--;
    if (currentSearchIndex_ < 0) {
        currentSearchIndex_ = searchHighlights_.size() - 1;
    }
    
    // Similar to findNext
    // ... (implementation similar to findNext)
}

void FloatingInspector::highlightSearchResults()
{
    QTextEdit* targetEdit = nullptr;
    
    switch (currentType_) {
    case MessageContent: targetEdit = messageEdit_; break;
    case ToolContent: targetEdit = outputEdit_; break;
    case ErrorContent: targetEdit = errorMessageEdit_; break;
    default: return;
    }
    
    if (targetEdit) {
        targetEdit->setExtraSelections(searchHighlights_);
    }
}


void FloatingInspector::copyToClipboard()
{
    QString text;
    
    switch (currentType_) {
    case MessageContent:
        text = messageEdit_->toPlainText();
        break;
    case ToolContent:
        text = outputEdit_->toPlainText();
        break;
    case ErrorContent:
        text = errorMessageEdit_->toPlainText();
        break;
    default:
        return;
    }
    
    QApplication::clipboard()->setText(text);
}

void FloatingInspector::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw background with rounded corners
    QPainterPath path;
    path.addRoundedRect(rect(), 8, 8);
    
    painter.fillPath(path, QColor(40, 40, 40, 240));
    painter.setPen(QPen(QColor(60, 60, 60), 1));
    painter.drawPath(path);
    
    BaseStyledWidget::paintEvent(event);
}

void FloatingInspector::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        isDragging_ = true;
        dragStartPos_ = event->globalPos() - pos();
        setCursor(Qt::ClosedHandCursor);
    }
    BaseStyledWidget::mousePressEvent(event);
}

void FloatingInspector::mouseMoveEvent(QMouseEvent* event)
{
    if (isDragging_ && (event->buttons() & Qt::LeftButton)) {
        move(event->globalPos() - dragStartPos_);
        position_ = Manual;
        emit positionChanged(Manual);
    }
    BaseStyledWidget::mouseMoveEvent(event);
}

void FloatingInspector::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        isDragging_ = false;
        setCursor(Qt::ArrowCursor);
    }
    BaseStyledWidget::mouseReleaseEvent(event);
}

void FloatingInspector::enterEvent(QEvent* event)
{
    stopAutoHideTimer();
    
    if (!pinned_) {
        // Fade to full opacity
        fadeAnimation_->stop();
        fadeAnimation_->setStartValue(opacityEffect_->opacity());
        fadeAnimation_->setEndValue(1.0);
        fadeAnimation_->setDuration(150);
        fadeAnimation_->start();
    }
    
    BaseStyledWidget::enterEvent(event);
}

void FloatingInspector::leaveEvent(QEvent* event)
{
    if (autoHide_ && !pinned_) {
        startAutoHideTimer();
        
        // Fade back to normal opacity
        fadeAnimation_->stop();
        fadeAnimation_->setStartValue(opacityEffect_->opacity());
        fadeAnimation_->setEndValue(opacity_);
        fadeAnimation_->setDuration(150);
        fadeAnimation_->start();
    }
    
    BaseStyledWidget::leaveEvent(event);
}

void FloatingInspector::closeEvent(QCloseEvent* event)
{
    removeGlobalEventFilter();
    BaseStyledWidget::closeEvent(event);
}

void FloatingInspector::resizeEvent(QResizeEvent* event)
{
    BaseStyledWidget::resizeEvent(event);
    
    if (position_ != Manual) {
        updatePosition();
    }
}

bool FloatingInspector::eventFilter(QObject* obj, QEvent* event)
{
    if (followMouse_ && event->type() == QEvent::MouseMove) {
        if (position_ == FollowCursor) {
            updatePosition();
        }
    }
    
    return BaseStyledWidget::eventFilter(obj, event);
}

void FloatingInspector::startAutoHideTimer()
{
    if (autoHide_ && !pinned_) {
        autoHideTimer_->stop();
        autoHideTimer_->start(autoHideDelay_);
    }
}

void FloatingInspector::stopAutoHideTimer()
{
    autoHideTimer_->stop();
}

void FloatingInspector::onAutoHideTimeout()
{
    if (!pinned_ && !underMouse()) {
        hide();
    }
}

void FloatingInspector::onLinkClicked(const QString& link)
{
    emit linkClicked(link);
}

void FloatingInspector::onModeButtonClicked()
{
    // Cycle through modes
    InspectorMode newMode = CompactMode;
    switch (mode_) {
    case CompactMode:
        newMode = DetailedMode;
        break;
    case DetailedMode:
        newMode = ExpandedMode;
        break;
    case ExpandedMode:
        newMode = CompactMode;
        break;
    }
    
    setMode(newMode);
}

void FloatingInspector::onPinButtonClicked()
{
    setPinned(!pinned_);
}

void FloatingInspector::onCloseButtonClicked()
{
    hide();
}

void FloatingInspector::onHistoryItemClicked(int index)
{
    if (index >= 0 && index < history_.size()) {
        historyIndex_ = index;
        // Navigate to history item
        // ... (similar to navigateBack/Forward)
    }
}

void FloatingInspector::onSearchTextChanged(const QString& text)
{
    search(text);
}

void FloatingInspector::onAnimationFinished()
{
    if (fadeAnimation_->endValue().toReal() == 0.0) {
        QWidget::hide();
    }
}

void FloatingInspector::updateOpacity()
{
    // Called during opacity animation
    update();
}

void FloatingInspector::installGlobalEventFilter()
{
    if (!globalFilterInstalled_ && followMouse_) {
        qApp->installEventFilter(this);
        globalFilterInstalled_ = true;
    }
}

void FloatingInspector::removeGlobalEventFilter()
{
    if (globalFilterInstalled_) {
        qApp->removeEventFilter(this);
        globalFilterInstalled_ = false;
    }
}

void FloatingInspector::updateSizeForMode()
{
    resize(sizeForMode(mode_));
}

// TooltipInspector implementation
TooltipInspector::TooltipInspector(QWidget* parent)
    : FloatingInspector(parent)
{
    setupTooltipStyle();
}

void TooltipInspector::setupTooltipStyle()
{
    setMode(CompactMode);
    setAutoHide(true);
    setAutoHideDelay(2000);
    setPinned(false);
    setWindowFlags(windowFlags() | Qt::ToolTip);
    
    // Hide header buttons except close
    if (modeButton_) modeButton_->hide();
    if (pinButton_) pinButton_->hide();
}

void TooltipInspector::showAtCursor(const QString& text, int duration)
{
    showCustom("Tooltip", new QLabel(text, this));
    setPosition(FollowCursor);
    setAutoHideDelay(duration);
    show();
}

void TooltipInspector::showAtWidget(QWidget* widget, const QString& text, int duration)
{
    if (!widget) return;
    
    showCustom("Tooltip", new QLabel(text, this));
    
    QPoint pos = widget->mapToGlobal(QPoint(widget->width() / 2, widget->height()));
    move(pos - QPoint(width() / 2, 0));
    setPosition(Manual);
    
    setAutoHideDelay(duration);
    show();
}

void TooltipInspector::showAtPoint(const QPoint& pos, const QString& text, int duration)
{
    showCustom("Tooltip", new QLabel(text, this));
    move(pos);
    setPosition(Manual);
    setAutoHideDelay(duration);
    show();
}

void TooltipInspector::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Draw tooltip-style background
    QPainterPath path;
    path.addRoundedRect(rect(), 4, 4);
    
    painter.fillPath(path, QColor(50, 50, 50, 220));
    painter.setPen(QPen(QColor(80, 80, 80), 1));
    painter.drawPath(path);
    
    // Don't call base class to avoid double painting
}

// PropertyInspector implementation
PropertyInspector::PropertyInspector(QWidget* parent)
    : FloatingInspector(parent)
{
    setupPropertyView();
}

void PropertyInspector::setupPropertyView()
{
    propertyTree_ = new QTreeWidget(this);
    propertyTree_->setHeaderLabels({"Property", "Value", "Type"});
    propertyTree_->setAlternatingRowColors(true);
    
    showCustom("Property Inspector", propertyTree_);
    setMode(DetailedMode);
}

void PropertyInspector::inspectObject(QObject* obj)
{
    if (!obj) return;
    
    currentObject_ = obj;
    propertyTree_->clear();
    
    titleLabel_->setText(QString("Properties: %1").arg(obj->objectName()));
    
    // Add meta object info
    auto* metaItem = new QTreeWidgetItem(propertyTree_);
    metaItem->setText(0, "Meta Object");
    metaItem->setText(1, obj->metaObject()->className());
    metaItem->setText(2, "Class");
    
    // Add properties
    const QMetaObject* meta = obj->metaObject();
    for (int i = 0; i < meta->propertyCount(); ++i) {
        QMetaProperty prop = meta->property(i);
        
        if (!showPrivate_ && !prop.isScriptable()) continue;
        
        auto* item = new QTreeWidgetItem(propertyTree_);
        item->setText(0, prop.name());
        item->setText(1, prop.read(obj).toString());
        item->setText(2, prop.typeName());
        
        if (prop.isWritable()) {
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
    }
    
    // Expand to level
    if (expandLevel_ > 0) {
        propertyTree_->expandToDepth(expandLevel_ - 1);
    }
    
    show();
}

void PropertyInspector::inspectJson(const QJsonObject& json)
{
    propertyTree_->clear();
    titleLabel_->setText("JSON Inspector");
    
    // Recursive function to add JSON items
    std::function<void(QTreeWidgetItem*, const QString&, const QJsonValue&)> addJsonValue;
    addJsonValue = [&](QTreeWidgetItem* parent, const QString& key, const QJsonValue& value) {
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(propertyTree_);
        item->setText(0, key);
        
        switch (value.type()) {
        case QJsonValue::Object: {
            item->setText(2, "Object");
            QJsonObject obj = value.toObject();
            for (auto it = obj.begin(); it != obj.end(); ++it) {
                addJsonValue(item, it.key(), it.value());
            }
            break;
        }
        case QJsonValue::Array: {
            item->setText(2, "Array");
            QJsonArray arr = value.toArray();
            for (int i = 0; i < arr.size(); ++i) {
                addJsonValue(item, QString("[%1]").arg(i), arr[i]);
            }
            break;
        }
        default:
            item->setText(1, value.toString());
            // Get type name based on QJsonValue::Type
            QString typeName;
            switch (value.type()) {
                case QJsonValue::Null: typeName = "null"; break;
                case QJsonValue::Bool: typeName = "bool"; break;
                case QJsonValue::Double: typeName = "number"; break;
                case QJsonValue::String: typeName = "string"; break;
                case QJsonValue::Array: typeName = "array"; break;
                case QJsonValue::Object: typeName = "object"; break;
                default: typeName = "undefined"; break;
            }
            item->setText(2, typeName);
        }
    };
    
    for (auto it = json.begin(); it != json.end(); ++it) {
        addJsonValue(nullptr, it.key(), it.value());
    }
    
    propertyTree_->expandToDepth(expandLevel_ - 1);
    show();
}

void PropertyInspector::inspectProperties(const QVariantMap& properties)
{
    propertyTree_->clear();
    titleLabel_->setText("Properties");
    
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        auto* item = new QTreeWidgetItem(propertyTree_);
        item->setText(0, it.key());
        item->setText(1, it.value().toString());
        item->setText(2, it.value().typeName());
    }
    
    show();
}

void PropertyInspector::populateProperties()
{
    // Implementation handled in inspect methods
}

// CodeInspector implementation
CodeInspector::CodeInspector(QWidget* parent)
    : FloatingInspector(parent)
{
    setupCodeView();
    setAcceptDrops(true);
}

void CodeInspector::setupCodeView()
{
    codeEdit_ = new QTextEdit(this);
    codeEdit_->setReadOnly(true);
    codeEdit_->setFont(QFont("Consolas", 10));
    
    connect(codeEdit_, &QTextEdit::cursorPositionChanged, [this]() {
        int line = codeEdit_->textCursor().blockNumber() + 1;
        emit lineClicked(line);
    });
    
    showCustom("Code Inspector", codeEdit_);
    setMode(ExpandedMode);
}

void CodeInspector::showCode(const QString& code, const QString& language)
{
    currentLanguage_ = language;
    codeEdit_->setPlainText(code);
    
    if (showLineNumbers_) {
        applyLineNumbers();
    }
    
    setSyntaxHighlighter(language);
    titleLabel_->setText(QString("Code (%1)").arg(language));
    
    show();
}

void CodeInspector::showFile(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        showError("Failed to open file", filePath);
        return;
    }
    
    QTextStream stream(&file);
    QString code = stream.readAll();
    
    // Detect language from extension
    QString ext = QFileInfo(filePath).suffix().toLower();
    QString language = "text";
    
    if (ext == "cpp" || ext == "cc" || ext == "cxx") language = "cpp";
    else if (ext == "h" || ext == "hpp") language = "cpp";
    else if (ext == "py") language = "python";
    else if (ext == "js") language = "javascript";
    else if (ext == "java") language = "java";
    else if (ext == "cs") language = "csharp";
    
    showCode(code, language);
    titleLabel_->setText(QFileInfo(filePath).fileName());
}

void CodeInspector::showDiff(const QString& before, const QString& after)
{
    // Simple diff display
    QStringList beforeLines = before.split('\n');
    QStringList afterLines = after.split('\n');
    
    QString diffText;
    int maxLines = qMax(beforeLines.size(), afterLines.size());
    
    for (int i = 0; i < maxLines; ++i) {
        QString beforeLine = i < beforeLines.size() ? beforeLines[i] : "";
        QString afterLine = i < afterLines.size() ? afterLines[i] : "";
        
        if (beforeLine != afterLine) {
            if (!beforeLine.isEmpty()) {
                diffText += QString("- %1\n").arg(beforeLine);
            }
            if (!afterLine.isEmpty()) {
                diffText += QString("+ %1\n").arg(afterLine);
            }
        } else {
            diffText += QString("  %1\n").arg(beforeLine);
        }
    }
    
    codeEdit_->setPlainText(diffText);
    titleLabel_->setText("Diff View");
    
    // Apply diff highlighting
    QTextDocument* doc = codeEdit_->document();
    QTextCursor cursor(doc);
    
    while (!cursor.atEnd()) {
        cursor.movePosition(QTextCursor::StartOfLine);
        cursor.movePosition(QTextCursor::EndOfLine, QTextCursor::KeepAnchor);
        QString line = cursor.selectedText();
        
        QTextCharFormat format;
        if (line.startsWith("+ ")) {
            format.setBackground(QColor(0, 255, 0, 30));
        } else if (line.startsWith("- ")) {
            format.setBackground(QColor(255, 0, 0, 30));
        }
        
        cursor.mergeCharFormat(format);
        cursor.movePosition(QTextCursor::NextBlock);
    }
    
    show();
}

void CodeInspector::setHighlightLine(int line)
{
    highlightedLine_ = line;
    highlightLine(line);
}

void CodeInspector::clearHighlight()
{
    highlightedLine_ = -1;
    codeEdit_->setExtraSelections({});
}

void CodeInspector::setSyntaxHighlighter(const QString& language)
{
    // Create simple syntax highlighter based on language
    class SimpleSyntaxHighlighter : public QSyntaxHighlighter {
        QString language_;
        
    public:
        SimpleSyntaxHighlighter(QTextDocument* parent, const QString& lang)
            : QSyntaxHighlighter(parent), language_(lang) {}
            
    protected:
        void highlightBlock(const QString& text) override {
            if (language_ == "cpp") {
                // Keywords
                QTextCharFormat keywordFormat;
                keywordFormat.setForeground(QColor(86, 156, 214));
                keywordFormat.setFontWeight(QFont::Bold);
                
                QStringList keywords = {
                    "\\bclass\\b", "\\bstruct\\b", "\\benum\\b", "\\bnamespace\\b",
                    "\\bpublic\\b", "\\bprivate\\b", "\\bprotected\\b",
                    "\\bif\\b", "\\belse\\b", "\\bfor\\b", "\\bwhile\\b", "\\breturn\\b",
                    "\\bvoid\\b", "\\bint\\b", "\\bbool\\b", "\\bdouble\\b", "\\bfloat\\b"
                };
                
                for (const QString& pattern : keywords) {
                    QRegularExpression expr(pattern);
                    QRegularExpressionMatchIterator it = expr.globalMatch(text);
                    while (it.hasNext()) {
                        QRegularExpressionMatch match = it.next();
                        setFormat(match.capturedStart(), match.capturedLength(), keywordFormat);
                    }
                }
                
                // Comments
                QTextCharFormat commentFormat;
                commentFormat.setForeground(QColor(87, 166, 74));
                commentFormat.setFontItalic(true);
                
                // Single line comments
                QRegularExpression commentExpr("//[^\n]*");
                QRegularExpressionMatch match = commentExpr.match(text);
                if (match.hasMatch()) {
                    setFormat(match.capturedStart(), match.capturedLength(), commentFormat);
                }
                
                // Strings
                QTextCharFormat stringFormat;
                stringFormat.setForeground(QColor(214, 157, 133));
                
                QRegularExpression stringExpr("\".*\"");
                QRegularExpressionMatchIterator it = stringExpr.globalMatch(text);
                while (it.hasNext()) {
                    match = it.next();
                    setFormat(match.capturedStart(), match.capturedLength(), stringFormat);
                }
            }
            // Add more language support as needed
        }
    };
    
    new SimpleSyntaxHighlighter(codeEdit_->document(), language);
}

void CodeInspector::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void CodeInspector::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();
    
    if (mimeData->hasUrls()) {
        QList<QUrl> urls = mimeData->urls();
        if (!urls.isEmpty()) {
            QString filePath = urls.first().toLocalFile();
            showFile(filePath);
            emit fileDropped(filePath);
        }
    }
}

void CodeInspector::applyLineNumbers()
{
    QStringList lines = codeEdit_->toPlainText().split('\n');
    QString numberedText;
    
    int width = QString::number(lines.size()).length();
    
    for (int i = 0; i < lines.size(); ++i) {
        numberedText += QString("%1 | %2\n")
            .arg(i + 1, width)
            .arg(lines[i]);
    }
    
    codeEdit_->setPlainText(numberedText);
}

void CodeInspector::highlightLine(int line)
{
    QList<QTextEdit::ExtraSelection> selections;
    
    QTextDocument* doc = codeEdit_->document();
    QTextBlock block = doc->findBlockByLineNumber(line - 1);
    
    if (block.isValid()) {
        QTextEdit::ExtraSelection selection;
        selection.format.setBackground(QColor(255, 255, 0, 80));
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = QTextCursor(block);
        selection.cursor.clearSelection();
        selections.append(selection);
        
        codeEdit_->setExtraSelections(selections);
        
        // Scroll to line
        QTextCursor cursor(block);
        codeEdit_->setTextCursor(cursor);
        codeEdit_->ensureCursorVisible();
    }
}

// ImageInspector implementation
ImageInspector::ImageInspector(QWidget* parent)
    : FloatingInspector(parent)
{
    setupImageView();
}

void ImageInspector::setupImageView()
{
    auto* widget = new QWidget(this);
    auto* layout = new QVBoxLayout(widget);
    
    // Image selection combo
    imageCombo_ = new QComboBox(this);
    imageCombo_->hide();
    connect(imageCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this](int index) {
        if (index >= 0 && index < imagePaths_.size()) {
            currentImageIndex_ = index;
            showImage(imagePaths_[index]);
        }
    });
    
    layout->addWidget(imageCombo_);
    
    // Image display
    imageScroll_ = new QScrollArea(this);
    imageLabel_ = new QLabel(this);
    imageLabel_->setScaledContents(false);
    imageLabel_->setAlignment(Qt::AlignCenter);
    
    imageScroll_->setWidget(imageLabel_);
    imageScroll_->setWidgetResizable(false);
    layout->addWidget(imageScroll_, 1);
    
    // Zoom controls
    auto* zoomLayout = new QHBoxLayout();
    
    auto* zoomOutBtn = new QToolButton(this);
    zoomOutBtn->setText("-");
    connect(zoomOutBtn, &QToolButton::clicked, [this]() {
        setZoomLevel(zoomLevel_ * 0.9);
    });
    
    zoomSlider_ = new QSlider(Qt::Horizontal, this);
    zoomSlider_->setRange(10, 500);
    zoomSlider_->setValue(100);
    connect(zoomSlider_, &QSlider::valueChanged, [this](int value) {
        setZoomLevel(value / 100.0);
    });
    
    auto* zoomInBtn = new QToolButton(this);
    zoomInBtn->setText("+");
    connect(zoomInBtn, &QToolButton::clicked, [this]() {
        setZoomLevel(zoomLevel_ * 1.1);
    });
    
    auto* zoomLabel = new QLabel("100%", this);
    connect(this, &ImageInspector::zoomChanged, [zoomLabel](qreal zoom) {
        zoomLabel->setText(QString("%1%").arg(qRound(zoom * 100)));
    });
    
    zoomLayout->addWidget(zoomOutBtn);
    zoomLayout->addWidget(zoomSlider_, 1);
    zoomLayout->addWidget(zoomInBtn);
    zoomLayout->addWidget(zoomLabel);
    
    layout->addLayout(zoomLayout);
    
    showCustom("Image Inspector", widget);
    setMode(ExpandedMode);
}

void ImageInspector::showImage(const QPixmap& pixmap)
{
    currentPixmap_ = pixmap;
    updateImage();
    
    titleLabel_->setText(QString("Image (%1x%2)")
        .arg(pixmap.width())
        .arg(pixmap.height()));
    
    show();
}

void ImageInspector::showImage(const QString& path)
{
    QPixmap pixmap(path);
    if (!pixmap.isNull()) {
        showImage(pixmap);
        titleLabel_->setText(QFileInfo(path).fileName());
    } else {
        showError("Failed to load image", path);
    }
}

void ImageInspector::showImages(const QStringList& paths)
{
    imagePaths_ = paths;
    currentImageIndex_ = 0;
    
    imageCombo_->clear();
    for (const QString& path : paths) {
        imageCombo_->addItem(QFileInfo(path).fileName());
    }
    imageCombo_->show();
    
    if (!paths.isEmpty()) {
        showImage(paths.first());
    }
}

void ImageInspector::setZoomLevel(qreal zoom)
{
    zoomLevel_ = qBound(0.1, zoom, 5.0);
    zoomSlider_->setValue(qRound(zoomLevel_ * 100));
    updateImage();
    emit zoomChanged(zoomLevel_);
}

void ImageInspector::setCompareImage(const QPixmap& pixmap)
{
    comparePixmap_ = pixmap;
    if (compareMode_) {
        updateImage();
    }
}

void ImageInspector::updateImage()
{
    if (currentPixmap_.isNull()) return;
    
    QPixmap displayPixmap = currentPixmap_;
    
    if (compareMode_ && !comparePixmap_.isNull()) {
        // Create side-by-side comparison
        int width = currentPixmap_.width() + comparePixmap_.width() + 10;
        int height = qMax(currentPixmap_.height(), comparePixmap_.height());
        
        displayPixmap = QPixmap(width, height);
        displayPixmap.fill(Qt::transparent);
        
        QPainter painter(&displayPixmap);
        painter.drawPixmap(0, 0, currentPixmap_);
        painter.drawPixmap(currentPixmap_.width() + 10, 0, comparePixmap_);
    }
    
    // Apply zoom
    if (zoomMode_ == "fit") {
        imageLabel_->setPixmap(displayPixmap.scaled(
            imageScroll_->viewport()->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
    } else if (zoomMode_ == "actual") {
        imageLabel_->setPixmap(displayPixmap);
    } else { // custom
        QSize newSize = displayPixmap.size() * zoomLevel_;
        imageLabel_->setPixmap(displayPixmap.scaled(
            newSize,
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
    }
    
    imageLabel_->adjustSize();
}

void ImageInspector::wheelEvent(QWheelEvent* event)
{
    if (event->modifiers() & Qt::ControlModifier) {
        qreal delta = event->angleDelta().y() / 120.0;
        setZoomLevel(zoomLevel_ * (1.0 + delta * 0.1));
        event->accept();
    } else {
        FloatingInspector::wheelEvent(event);
    }
}

// InspectorFactory implementation
QHash<QString, std::function<FloatingInspector*(QWidget*)>> InspectorFactory::creators_;

FloatingInspector* InspectorFactory::createInspector(const QString& type, QWidget* parent)
{
    if (creators_.contains(type)) {
        return creators_[type](parent);
    }
    
    // Default types
    if (type == "tooltip") {
        return new TooltipInspector(parent);
    } else if (type == "property") {
        return new PropertyInspector(parent);
    } else if (type == "code") {
        return new CodeInspector(parent);
    } else if (type == "image") {
        return new ImageInspector(parent);
    }
    
    // Default to base inspector
    return new FloatingInspector(parent);
}

void InspectorFactory::registerInspectorType(const QString& type, 
                                           std::function<FloatingInspector*(QWidget*)> creator)
{
    creators_[type] = creator;
}

} // namespace llm_re::ui_v2