#include "../core/ui_v2_common.h"
#include "theme_selector_widget.h"
#include "../core/ui_utils.h"
#include "../views/theme_editor/theme_editor_dialog.h"
#include "../../core/config.h"
#include "../core/theme_manager.h"
#include "../core/theme_templates.h"

namespace llm_re::ui_v2 {

ThemeSelectorWidget::ThemeSelectorWidget(QWidget* parent)
    : QWidget(parent) {
    // CRITICAL: Prevent Qt from using the application style for background
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    
    setupUI();
    loadThemes();
}

void ThemeSelectorWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    
    // Header
    auto* headerLayout = new QHBoxLayout();
    
    currentThemeLabel_ = new QLabel("Current Theme: Dark");
    currentThemeLabel_->setStyleSheet("font-weight: bold; font-size: 14px;");
    headerLayout->addWidget(currentThemeLabel_);
    
    headerLayout->addStretch();
    
    // Action buttons
    editButton_ = new QPushButton("Edit Theme");
    editButton_->setIcon(QIcon(":/icons/edit.svg"));
    connect(editButton_, &QPushButton::clicked, this, &ThemeSelectorWidget::onEditClicked);
    headerLayout->addWidget(editButton_);
    
    createButton_ = new QPushButton("Create New");
    createButton_->setIcon(QIcon(":/icons/plus.svg"));
    connect(createButton_, &QPushButton::clicked, this, &ThemeSelectorWidget::onCreateClicked);
    headerLayout->addWidget(createButton_);
    
    importButton_ = new QPushButton("Import");
    importButton_->setIcon(QIcon(":/icons/download.svg"));
    connect(importButton_, &QPushButton::clicked, this, &ThemeSelectorWidget::onImportClicked);
    headerLayout->addWidget(importButton_);
    
    exportButton_ = new QPushButton("Export");
    exportButton_->setIcon(QIcon(":/icons/upload.svg"));
    exportButton_->setEnabled(false);
    connect(exportButton_, &QPushButton::clicked, this, &ThemeSelectorWidget::onExportClicked);
    headerLayout->addWidget(exportButton_);
    
    mainLayout->addLayout(headerLayout);
    
    // Main content with splitter
    auto* splitter = new QSplitter(Qt::Horizontal);
    
    // Left: Theme grid
    scrollArea_ = new QScrollArea();
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    cardsContainer_ = new QWidget();
    cardsLayout_ = new QGridLayout(cardsContainer_);
    cardsLayout_->setSpacing(12);
    
    scrollArea_->setWidget(cardsContainer_);
    splitter->addWidget(scrollArea_);
    
    // Right: Theme details
    auto* detailsWidget = new QWidget();
    auto* detailsLayout = new QVBoxLayout(detailsWidget);
    
    auto* detailsLabel = new QLabel("Theme Details");
    detailsLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    detailsLayout->addWidget(detailsLabel);
    
    descriptionText_ = new QTextEdit();
    descriptionText_->setReadOnly(true);
    descriptionText_->setMaximumHeight(200);
    detailsLayout->addWidget(descriptionText_);
    
    // Preview area
    auto* previewLabel = new QLabel("Preview");
    previewLabel->setStyleSheet("font-weight: bold; font-size: 14px; margin-top: 10px;");
    detailsLayout->addWidget(previewLabel);
    
    auto* previewWidget = new QWidget();
    previewWidget->setMinimumHeight(300);
    // Use explicit colors from theme instead of palette to avoid inheriting IDA's theme
    const auto& colors = ThemeManager::instance().colors();
    previewWidget->setStyleSheet(QString("background-color: %1; border: 1px solid %2;")
                                .arg(colors.surface.name())
                                .arg(colors.border.name()));
    detailsLayout->addWidget(previewWidget);
    
    detailsLayout->addStretch();
    
    splitter->addWidget(detailsWidget);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    
    mainLayout->addWidget(splitter);
}

void ThemeSelectorWidget::loadThemes() {
    // Clear existing cards
    for (auto* card : themeCards_) {
        delete card;
    }
    themeCards_.clear();
    
    // Get current theme info
    auto currentInfo = ThemeManager::instance().getCurrentThemeInfo();
    currentThemeName_ = currentInfo.name;
    
    QString displayText = QString("Current Theme: %1").arg(currentInfo.displayName);
    if (currentInfo.isModified) {
        displayText += " (modified)";
    }
    currentThemeLabel_->setText(displayText);
    
    // Get all themes from ThemeManager
    auto themes = ThemeManager::instance().getAllThemes();
    
    for (const auto& info : themes) {
        createThemeCard(info.name, info.isBuiltIn);
    }
    
    updateSelection();
}

void ThemeSelectorWidget::createThemeCard(const QString& themeName, bool isBuiltIn) {
    auto* card = new ThemeCard(themeName, isBuiltIn);
    
    connect(card, &ThemeCard::clicked, this, &ThemeSelectorWidget::onThemeSelected);
    connect(card, &ThemeCard::deleteRequested, [this, themeName]() {
        onDeleteClicked();
    });
    
    int row = themeCards_.size() / 3;
    int col = themeCards_.size() % 3;
    cardsLayout_->addWidget(card, row, col);
    
    themeCards_.push_back(card);
}

void ThemeSelectorWidget::updateSelection() {
    for (auto* card : themeCards_) {
        bool isSelected = card->themeName() == currentThemeName_;
        card->setSelected(isSelected);
        if (isSelected) {
            selectedCard_ = card;
            showThemePreview(currentThemeName_);
            
            // Update button states
            editButton_->setEnabled(!card->isBuiltIn());
            exportButton_->setEnabled(!card->isBuiltIn());
        }
    }
}

void ThemeSelectorWidget::showThemePreview(const QString& themeName) {
    // Load theme metadata
    ThemeMetadata metadata;
    if (themeName == "dark" || themeName == "light" || themeName == "default") {
        metadata.name = themeName;
        metadata.description = QString("Built-in %1 theme").arg(themeName);
        metadata.author = "LLM RE Team";
        metadata.version = "1.0";
    } else {
        metadata = ThemeManager::instance().getThemeMetadata(themeName);
    }
    
    // Update description
    descriptionText_->setHtml(QString(
        "<h3>%1</h3>"
        "<p>%2</p>"
        "<p><b>Author:</b> %3<br>"
        "<b>Version:</b> %4<br>"
        "<b>Base Theme:</b> %5<br>"
        "<b>Created:</b> %6<br>"
        "<b>Modified:</b> %7</p>"
    ).arg(metadata.name)
     .arg(metadata.description)
     .arg(metadata.author)
     .arg(metadata.version)
     .arg(metadata.baseTheme)
     .arg(metadata.createdDate.toString("yyyy-MM-dd"))
     .arg(metadata.modifiedDate.toString("yyyy-MM-dd")));
}

void ThemeSelectorWidget::onThemeSelected() {
    auto* card = qobject_cast<ThemeCard*>(sender());
    if (!card) return;
    
    if (card->themeName() != currentThemeName_) {
        currentThemeName_ = card->themeName();
        
        // Apply theme
        Config::instance().ui.theme_name = currentThemeName_.toStdString();
        Config::instance().save();
        
        // Update UI
        ThemeManager::instance().loadTheme(currentThemeName_);
        
        updateSelection();
        emit themeChanged(currentThemeName_);
    }
}

void ThemeSelectorWidget::onEditClicked() {
    if (selectedCard_ && !selectedCard_->isBuiltIn()) {
        emit editThemeRequested(selectedCard_->themeName());
    }
}

void ThemeSelectorWidget::onCreateClicked() {
    auto* dialog = new ThemeEditorDialog(this);
    dialog->setWindowTitle("Create New Theme");
    dialog->loadCurrentTheme();
    
    connect(dialog, &ThemeEditorDialog::themeSaved, [this](const QString& themeName) {
        refresh();
        currentThemeName_ = themeName;
        updateSelection();
    });
    
    dialog->exec();
}

void ThemeSelectorWidget::onDeleteClicked() {
    if (!selectedCard_ || selectedCard_->isBuiltIn()) return;
    
    int ret = QMessageBox::question(this, "Delete Theme",
        QString("Are you sure you want to delete the theme '%1'?").arg(selectedCard_->themeName()),
        QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        ThemeManager::instance().deleteTheme(selectedCard_->themeName());
        
        // Switch to default theme
        currentThemeName_ = "dark";
        Config::instance().ui.theme_name = currentThemeName_.toStdString();
        Config::instance().save();
        ThemeManager::instance().loadTheme(currentThemeName_);
        
        refresh();
        emit deleteThemeRequested(selectedCard_->themeName());
    }
}

void ThemeSelectorWidget::onImportClicked() {
    QString fileName = QFileDialog::getOpenFileName(this,
        "Import Theme", "",
        QString("Theme Files (*%1)").arg(ThemeConstants::THEME_FILE_EXTENSION));
    
    if (!fileName.isEmpty()) {
        ThemeError error = ThemeManager::instance().importTheme(fileName);
        
        if (error == ThemeError::None) {
            refresh();
            QMessageBox::information(this, "Import Successful", 
                "Theme imported successfully!");
        } else {
            QMessageBox::critical(this, "Import Failed", 
                "Failed to import theme.");
        }
    }
}

void ThemeSelectorWidget::onExportClicked() {
    if (!selectedCard_ || selectedCard_->isBuiltIn()) return;
    
    QString fileName = QFileDialog::getSaveFileName(this,
        "Export Theme", selectedCard_->themeName(),
        QString("Theme Files (*%1)").arg(ThemeConstants::THEME_FILE_EXTENSION));
    
    if (!fileName.isEmpty()) {
        ThemeMetadata metadata = ThemeManager::instance().getThemeMetadata(selectedCard_->themeName());
        
        if (ThemeManager::instance().exportTheme(fileName, metadata)) {
            QMessageBox::information(this, "Export Successful", 
                "Theme exported successfully!");
        } else {
            QMessageBox::critical(this, "Export Failed", 
                "Failed to export theme.");
        }
    }
}

void ThemeSelectorWidget::refresh() {
    loadThemes();
}

// ThemeCard implementation

ThemeSelectorWidget::ThemeCard::ThemeCard(const QString& themeName, bool isBuiltIn, QWidget* parent)
    : QWidget(parent), themeName_(themeName), isBuiltIn_(isBuiltIn) {
    setFixedSize(180, 140);
    setCursor(Qt::PointingHandCursor);
    
    // CRITICAL: Prevent Qt from using the application style for background
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    
    generatePreview();
}

void ThemeSelectorWidget::ThemeCard::setSelected(bool selected) {
    selected_ = selected;
    update();
}

void ThemeSelectorWidget::ThemeCard::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Background
    const auto& colors = ThemeManager::instance().colors();
    QColor bgColor = colors.surface;
    if (selected_) {
        bgColor = colors.primary;
    } else if (hovered_) {
        bgColor = colors.surfaceHover;
    }
    
    painter.fillRect(rect(), bgColor);
    
    // Border
    QPen borderPen;
    if (selected_) {
        borderPen = QPen(colors.primary, 2);
    } else {
        borderPen = QPen(colors.border, 1);
    }
    painter.setPen(borderPen);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    
    // Preview
    if (!preview_.isNull()) {
        painter.drawPixmap(10, 10, preview_);
    }
    
    // Theme name
    QRect nameRect(10, height() - 35, width() - 20, 20);
    QFont nameFont = font();
    nameFont.setPointSize(12);
    nameFont.setBold(true);
    painter.setFont(nameFont);
    painter.setPen(colors.textPrimary);
    painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, themeName_);
    
    // Built-in badge
    if (isBuiltIn_) {
        QRect badgeRect(width() - 60, 5, 50, 20);
        painter.fillRect(badgeRect, colors.primary);
        painter.setPen(UIUtils::contrastColor(colors.primary));
        painter.setFont(QFont(font().family(), 9));
        painter.drawText(badgeRect, Qt::AlignCenter, "Built-in");
    }
}

void ThemeSelectorWidget::ThemeCard::enterEvent(QEnterEvent* event) {
    hovered_ = true;
    update();
}

void ThemeSelectorWidget::ThemeCard::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
}

void ThemeSelectorWidget::ThemeCard::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
}

void ThemeSelectorWidget::ThemeCard::contextMenuEvent(QContextMenuEvent* event) {
    if (!isBuiltIn_) {
        QMenu menu(this);
        menu.addAction("Delete Theme", [this]() {
            emit deleteRequested();
        });
        menu.exec(event->globalPos());
    }
}

void ThemeSelectorWidget::ThemeCard::generatePreview() {
    preview_ = QPixmap(160, 90);
    preview_.fill(Qt::transparent);
    
    QPainter painter(&preview_);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Simple preview based on theme name
    QColor bgColor, fgColor, accentColor;
    
    // Get theme preview colors from theme templates
    ThemeTemplates templates;
    auto preview = templates.getPreviewInfo(themeName_);
    
    if (preview.name.isEmpty()) {
        // Fallback if theme not found
        const auto& colors = ThemeManager::instance().colors();
        bgColor = colors.background;
        fgColor = colors.textPrimary;
        accentColor = colors.primary;
    } else {
        bgColor = preview.backgroundColor;
        fgColor = preview.foregroundColor;
        accentColor = preview.accentColor;
    }
    
    // Background
    painter.fillRect(preview_.rect(), bgColor);
    
    // Sample UI elements
    painter.setPen(fgColor);
    
    // Title bar
    QRect titleBar(0, 0, preview_.width(), 20);
    painter.fillRect(titleBar, accentColor);
    
    // Sample text lines
    painter.setPen(fgColor);
    for (int i = 0; i < 4; ++i) {
        int y = 30 + i * 15;
        int width = preview_.width() - 20 - (i * 20);
        painter.drawLine(10, y, 10 + width, y);
    }
}

} // namespace llm_re::ui_v2