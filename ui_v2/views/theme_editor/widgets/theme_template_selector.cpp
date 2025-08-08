#include "../../../core/ui_v2_common.h"
#include "theme_template_selector.h"
#include "../../../core/theme_manager.h"

namespace llm_re::ui_v2 {

ThemeTemplateSelector::ThemeTemplateSelector(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    
    // Load templates
    auto templates = ThemeTemplates::getAvailableTemplates();
    for (int i = 0; i < templates.size(); ++i) {
        createTemplateCard(templates[i], i);
    }
    
    updateFilter();
}

void ThemeTemplateSelector::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    
    // Header with search and filter
    auto* headerLayout = new QHBoxLayout();
    
    headerLayout->addWidget(new QLabel("Search:"));
    searchEdit_ = new QLineEdit();
    searchEdit_->setPlaceholderText("Search templates...");
    connect(searchEdit_, &QLineEdit::textChanged,
            this, &ThemeTemplateSelector::onFilterChanged);
    headerLayout->addWidget(searchEdit_);
    
    headerLayout->addSpacing(20);
    
    headerLayout->addWidget(new QLabel("Category:"));
    categoryCombo_ = new QComboBox();
    categoryCombo_->addItems({"All", "Modern", "Corporate", "Classic", 
                             "Accessibility", "Creative"});
    connect(categoryCombo_, &QComboBox::currentTextChanged,
            this, &ThemeTemplateSelector::onFilterChanged);
    headerLayout->addWidget(categoryCombo_);
    
    headerLayout->addStretch();
    mainLayout->addLayout(headerLayout);
    
    // Main content area with splitter
    auto* splitter = new QSplitter(Qt::Horizontal);
    
    // Left: Template cards
    scrollArea_ = new QScrollArea();
    scrollArea_->setWidgetResizable(true);
    scrollArea_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    
    cardsContainer_ = new QWidget();
    cardsLayout_ = new QGridLayout(cardsContainer_);
    cardsLayout_->setSpacing(16);
    
    scrollArea_->setWidget(cardsContainer_);
    splitter->addWidget(scrollArea_);
    
    // Right: Details panel
    auto* detailsWidget = new QWidget();
    auto* detailsLayout = new QVBoxLayout(detailsWidget);
    
    auto* detailsLabel = new QLabel("Template Details");
    detailsLabel->setStyleSheet("font-size: 16px; font-weight: bold;");
    detailsLayout->addWidget(detailsLabel);
    
    descriptionText_ = new QTextEdit();
    descriptionText_->setReadOnly(true);
    descriptionText_->setMaximumHeight(150);
    detailsLayout->addWidget(descriptionText_);
    
    // Preview area could go here
    auto* previewLabel = new QLabel("Preview");
    previewLabel->setStyleSheet("font-size: 14px; font-weight: bold; margin-top: 10px;");
    detailsLayout->addWidget(previewLabel);
    
    auto* previewWidget = new QWidget();
    previewWidget->setMinimumHeight(200);
    // Use explicit colors from theme instead of palette to avoid inheriting IDA's theme
    const auto& colors = ThemeManager::instance().colors();
    previewWidget->setStyleSheet(QString("background-color: %1; border: 1px solid %2;")
                                .arg(colors.surface.name())
                                .arg(colors.border.name()));
    detailsLayout->addWidget(previewWidget);
    
    detailsLayout->addStretch();
    
    // Action buttons
    auto* buttonLayout = new QHBoxLayout();
    
    createButton_ = new QPushButton("Create New Theme");
    createButton_->setEnabled(false);
    connect(createButton_, &QPushButton::clicked,
            this, &ThemeTemplateSelector::onCreateClicked);
    buttonLayout->addWidget(createButton_);
    
    applyButton_ = new QPushButton("Apply Template");
    applyButton_->setEnabled(false);
    connect(applyButton_, &QPushButton::clicked, [this]() {
        if (selectedCard_) {
            emit templateSelected(static_cast<ThemeTemplates::Template>(
                selectedCard_->templateIndex()));
        }
    });
    buttonLayout->addWidget(applyButton_);
    
    buttonLayout->addStretch();
    detailsLayout->addLayout(buttonLayout);
    
    splitter->addWidget(detailsWidget);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);
    
    mainLayout->addWidget(splitter);
}

void ThemeTemplateSelector::createTemplateCard(const ThemeTemplates::TemplateInfo& info, int index) {
    auto* card = new TemplateCard(info, index, this);
    connect(card, &TemplateCard::clicked, this, &ThemeTemplateSelector::onTemplateClicked);
    templateCards_.push_back(card);
}

void ThemeTemplateSelector::updateFilter() {
    // Clear layout
    while (cardsLayout_->count() > 0) {
        cardsLayout_->takeAt(0);
    }
    
    // Filter and arrange cards
    int row = 0, col = 0;
    const int cols = 3;
    
    for (auto* card : templateCards_) {
        bool visible = true;
        
        // Category filter
        if (currentCategory_ != "All" && card->category() != currentCategory_) {
            visible = false;
        }
        
        // Search filter
        if (!currentFilter_.isEmpty()) {
            QString searchText = card->info_.name + " " + card->info_.description;
            if (!searchText.contains(currentFilter_, Qt::CaseInsensitive)) {
                visible = false;
            }
        }
        
        card->setVisible(visible);
        
        if (visible) {
            cardsLayout_->addWidget(card, row, col);
            col++;
            if (col >= cols) {
                col = 0;
                row++;
            }
        }
    }
}

void ThemeTemplateSelector::onTemplateClicked() {
    auto* clickedCard = qobject_cast<TemplateCard*>(sender());
    if (!clickedCard) return;
    
    // Deselect previous
    if (selectedCard_) {
        selectedCard_->selected_ = false;
        selectedCard_->update();
    }
    
    // Select new
    selectedCard_ = clickedCard;
    selectedCard_->selected_ = true;
    selectedCard_->update();
    
    // Update details
    const auto& info = selectedCard_->info_;
    descriptionText_->setHtml(QString(
        "<h3>%1</h3>"
        "<p>%2</p>"
        "<p><b>Category:</b> %3<br>"
        "<b>Base Theme:</b> %4<br>"
        "<b>Author:</b> %5</p>"
    ).arg(info.name)
     .arg(info.description)
     .arg(info.category)
     .arg(info.metadata.baseTheme)
     .arg(info.metadata.author));
    
    // Enable buttons
    createButton_->setEnabled(true);
    applyButton_->setEnabled(true);
}

void ThemeTemplateSelector::onFilterChanged(const QString& filter) {
    currentFilter_ = searchEdit_->text();
    currentCategory_ = categoryCombo_->currentText();
    updateFilter();
}

void ThemeTemplateSelector::onCreateClicked() {
    if (!selectedCard_) return;
    
    bool ok;
    QString name = QInputDialog::getText(this, "Create Theme from Template",
                                        "Theme name:", QLineEdit::Normal,
                                        selectedCard_->info_.name + " Custom", &ok);
    
    if (ok && !name.isEmpty()) {
        emit createFromTemplate(static_cast<ThemeTemplates::Template>(
            selectedCard_->templateIndex()), name);
    }
}

// TemplateCard implementation

TemplateCard::TemplateCard(const ThemeTemplates::TemplateInfo& info,
                                                  int index, QWidget* parent)
    : QWidget(parent), info_(info), templateIndex_(index), category_(info.category) {
    setFixedSize(220, 260);
    setCursor(Qt::PointingHandCursor);
    
    // Add shadow effect
    auto* shadow = new QGraphicsDropShadowEffect();
    shadow->setBlurRadius(10);
    shadow->setOffset(0, 2);
    shadow->setColor(ThemeManager::instance().colors().shadow);
    setGraphicsEffect(shadow);
}

void TemplateCard::paintEvent(QPaintEvent* event) {
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
    if (selected_) {
        painter.setPen(QPen(colors.primary, 2));
    } else {
        painter.setPen(QPen(colors.border, 1));
    }
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
    
    // Preview image
    QRect previewRect(10, 10, width() - 20, 120);
    if (!info_.preview.isNull()) {
        painter.drawPixmap(previewRect, info_.preview);
    } else {
        painter.fillRect(previewRect, colors.surfaceHover);
        painter.setPen(colors.textPrimary);
        painter.drawText(previewRect, Qt::AlignCenter, "Preview");
    }
    
    // Template name
    QRect nameRect(10, 140, width() - 20, 30);
    QFont nameFont = font();
    nameFont.setPointSize(14);
    nameFont.setBold(true);
    painter.setFont(nameFont);
    painter.setPen(colors.textPrimary);
    painter.drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, info_.name);
    
    // Category badge
    QRect categoryRect(10, 175, width() - 20, 20);
    QFont categoryFont = font();
    categoryFont.setPointSize(10);
    painter.setFont(categoryFont);
    painter.setPen(ThemeManager::adjustAlpha(colors.textPrimary, 200));
    painter.drawText(categoryRect, Qt::AlignLeft | Qt::AlignVCenter, info_.category);
    
    // Description
    QRect descRect(10, 200, width() - 20, 50);
    QFont descFont = font();
    descFont.setPointSize(11);
    painter.setFont(descFont);
    painter.setPen(ThemeManager::adjustAlpha(colors.textPrimary, 180));
    painter.drawText(descRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, 
                    info_.description);
}

void TemplateCard::enterEvent(QEvent* event) {
    hovered_ = true;
    update();
    
    // Animate shadow on hover
    auto* shadow = qobject_cast<QGraphicsDropShadowEffect*>(graphicsEffect());
    if (shadow) {
        auto* anim = new QPropertyAnimation(shadow, "blurRadius");
        anim->setDuration(200);
        anim->setStartValue(10);
        anim->setEndValue(20);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void TemplateCard::leaveEvent(QEvent* event) {
    hovered_ = false;
    update();
    
    // Animate shadow back
    auto* shadow = qobject_cast<QGraphicsDropShadowEffect*>(graphicsEffect());
    if (shadow) {
        auto* anim = new QPropertyAnimation(shadow, "blurRadius");
        anim->setDuration(200);
        anim->setStartValue(20);
        anim->setEndValue(10);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

void TemplateCard::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
}

} // namespace llm_re::ui_v2