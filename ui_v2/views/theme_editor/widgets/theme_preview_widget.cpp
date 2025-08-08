#include "../../../core/ui_v2_common.h"
#include "theme_preview_widget.h"
#include "../../../core/theme_manager.h"
#include "../../../core/animation_manager.h"
#include "../../../core/effects_manager.h"

namespace llm_re::ui_v2 {

ThemePreviewWidget::ThemePreviewWidget(QWidget* parent)
    : QWidget(parent) {
    setupUI();
    
    // Initialize with current theme
    auto& tm = ThemeManager::instance();
    updateTheme(tm.colors(), tm.typography(), tm.componentStyles());
}

void ThemePreviewWidget::setupUI() {
    auto* layout = new QVBoxLayout(this);
    
    // Mode selector is in the parent dialog
    
    // Stacked widget for different preview modes
    stackedWidget_ = new QStackedWidget();
    
    // Create different preview modes
    createFullUIPreview();
    createColorsOnlyPreview();
    createComponentsPreview();
    createChartsPreview();
    
    stackedWidget_->addWidget(fullUIWidget_);
    stackedWidget_->addWidget(colorsOnlyWidget_);
    stackedWidget_->addWidget(componentsWidget_);
    stackedWidget_->addWidget(chartsWidget_);
    
    layout->addWidget(stackedWidget_);
}

void ThemePreviewWidget::createFullUIPreview() {
    fullUIWidget_ = new QWidget();
    auto* layout = new QVBoxLayout(fullUIWidget_);
    
    // Mini application preview
    auto* appFrame = new QFrame();
    appFrame->setFrameStyle(QFrame::StyledPanel);
    auto* appLayout = new QVBoxLayout(appFrame);
    
    // Title bar
    auto* titleBar = new QWidget();
    titleBar->setFixedHeight(30);
    // Use explicit colors from theme instead of palette to avoid inheriting IDA's theme
    const auto& colors = ThemeManager::instance().colors();
    titleBar->setStyleSheet(QString("background-color: %1;").arg(colors.surface.name()));
    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(8, 0, 8, 0);
    titleLayout->addWidget(new QLabel("LLM RE Agent"));
    titleLayout->addStretch();
    appLayout->addWidget(titleBar);
    
    // Main content area
    auto* contentSplitter = new QSplitter(Qt::Horizontal);
    
    // Left panel (conversation)
    auto* leftPanel = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftPanel);
    
    leftLayout->addWidget(new QLabel("Conversation"));
    
    // Sample messages
    leftLayout->addWidget(new MiniMessage(MiniMessage::User, 
        "Analyze this function", currentColors_, currentComponents_));
    leftLayout->addWidget(new MiniMessage(MiniMessage::Assistant, 
        "I'll analyze the function...", currentColors_, currentComponents_));
    leftLayout->addWidget(new MiniMessage(MiniMessage::System, 
        "Analysis complete", currentColors_, currentComponents_));
    
    leftLayout->addStretch();
    
    // Right panel (code/details)
    auto* rightPanel = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightPanel);
    
    rightLayout->addWidget(new QLabel("Code View"));
    rightLayout->addWidget(new MiniSyntaxHighlight(currentColors_));
    rightLayout->addStretch();
    
    contentSplitter->addWidget(leftPanel);
    contentSplitter->addWidget(rightPanel);
    contentSplitter->setStretchFactor(0, 3);
    contentSplitter->setStretchFactor(1, 2);
    
    appLayout->addWidget(contentSplitter);
    
    // Status bar
    auto* statusBar = new QWidget();
    statusBar->setFixedHeight(25);
    auto* statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(8, 0, 8, 0);
    statusLayout->addWidget(new QLabel("Ready"));
    statusLayout->addStretch();
    appLayout->addWidget(statusBar);
    
    layout->addWidget(appFrame);
}

void ThemePreviewWidget::createColorsOnlyPreview() {
    colorsOnlyWidget_ = new QScrollArea();
    colorsOnlyWidget_->setWidgetResizable(true);
    
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    
    // Color swatches organized by category
    struct ColorGroup {
        QString name;
        QList<QPair<QString, std::function<QColor()>>> colors;
    };
    
    std::vector<ColorGroup> groups = {
        {"Primary Colors", {
            {"Primary", [this]() { return currentColors_.primary; }},
            {"Primary Hover", [this]() { return currentColors_.primaryHover; }},
            {"Primary Active", [this]() { return currentColors_.primaryActive; }}
        }},
        {"Semantic Colors", {
            {"Success", [this]() { return currentColors_.success; }},
            {"Warning", [this]() { return currentColors_.warning; }},
            {"Error", [this]() { return currentColors_.error; }},
            {"Info", [this]() { return currentColors_.info; }}
        }},
        {"UI Colors", {
            {"Background", [this]() { return currentColors_.background; }},
            {"Surface", [this]() { return currentColors_.surface; }},
            {"Border", [this]() { return currentColors_.border; }},
            {"Shadow", [this]() { return currentColors_.shadow; }}
        }},
        {"Text Colors", {
            {"Primary", [this]() { return currentColors_.textPrimary; }},
            {"Secondary", [this]() { return currentColors_.textSecondary; }},
            {"Tertiary", [this]() { return currentColors_.textTertiary; }},
            {"Link", [this]() { return currentColors_.textLink; }}
        }}
    };
    
    for (const auto& group : groups) {
        auto* groupBox = new QGroupBox(group.name);
        auto* groupLayout = new QGridLayout(groupBox);
        
        int row = 0;
        for (const auto& [name, colorFunc] : group.colors) {
            auto* swatch = new QWidget();
            swatch->setFixedSize(60, 40);
            swatch->setStyleSheet(QString("background-color: %1; border: 1px solid %2;")
                                 .arg(colorFunc().name())
                                 .arg(ThemeManager::instance().colors().border.name()));
            
            auto* label = new QLabel(name);
            label->setAlignment(Qt::AlignCenter);
            
            groupLayout->addWidget(swatch, row, 0);
            groupLayout->addWidget(label, row, 1);
            row++;
        }
        
        layout->addWidget(groupBox);
    }
    
    layout->addStretch();
    colorsOnlyWidget_->setWidget(widget);
}

void ThemePreviewWidget::createComponentsPreview() {
    componentsWidget_ = new QScrollArea();
    componentsWidget_->setWidgetResizable(true);
    
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    
    // Buttons
    auto* buttonGroup = new QGroupBox("Buttons");
    auto* buttonLayout = new QHBoxLayout(buttonGroup);
    
    buttonLayout->addWidget(new MiniButton("Primary", currentColors_, currentComponents_));
    buttonLayout->addWidget(new MiniButton("Secondary", currentColors_, currentComponents_));
    buttonLayout->addWidget(new MiniButton("Disabled", currentColors_, currentComponents_));
    buttonLayout->addStretch();
    
    layout->addWidget(buttonGroup);
    
    // Inputs
    auto* inputGroup = new QGroupBox("Inputs");
    auto* inputLayout = new QVBoxLayout(inputGroup);
    
    inputLayout->addWidget(new MiniInput("Text input", currentColors_, currentComponents_));
    
    auto* combo = new QComboBox();
    combo->addItems({"Option 1", "Option 2", "Option 3"});
    inputLayout->addWidget(combo);
    
    layout->addWidget(inputGroup);
    
    // Cards
    auto* cardGroup = new QGroupBox("Cards");
    auto* cardLayout = new QHBoxLayout(cardGroup);
    
    cardLayout->addWidget(new MiniCard("Card Title", "Card content goes here",
                                       currentColors_, currentComponents_));
    cardLayout->addStretch();
    
    layout->addWidget(cardGroup);
    
    // Messages
    auto* messageGroup = new QGroupBox("Messages");
    auto* messageLayout = new QVBoxLayout(messageGroup);
    
    messageLayout->addWidget(new MiniMessage(MiniMessage::User, "User message",
                                            currentColors_, currentComponents_));
    messageLayout->addWidget(new MiniMessage(MiniMessage::Assistant, "Assistant message",
                                            currentColors_, currentComponents_));
    
    layout->addWidget(messageGroup);
    
    layout->addStretch();
    componentsWidget_->setWidget(widget);
}

void ThemePreviewWidget::createChartsPreview() {
    chartsWidget_ = new QWidget();
    auto* layout = new QGridLayout(chartsWidget_);
    
    layout->addWidget(createMiniLineChart(), 0, 0);
    layout->addWidget(createMiniBarChart(), 0, 1);
    layout->addWidget(createMiniPieChart(), 1, 0);
    
    // Empty space
    layout->addWidget(new QWidget(), 1, 1);
    
    layout->setRowStretch(2, 1);
    layout->setColumnStretch(2, 1);
}

QWidget* ThemePreviewWidget::createMiniLineChart() {
    auto* widget = new QWidget();
    widget->setFixedSize(200, 150);
    
    // Custom paint for simple line chart
    auto* paintWidget = new QWidget(widget);
    paintWidget->setGeometry(widget->rect());
    
    // We'll implement actual chart drawing later
    auto* label = new QLabel("Line Chart Preview", widget);
    label->setAlignment(Qt::AlignCenter);
    label->setGeometry(widget->rect());
    
    return widget;
}

QWidget* ThemePreviewWidget::createMiniBarChart() {
    auto* widget = new QWidget();
    widget->setFixedSize(200, 150);
    
    auto* label = new QLabel("Bar Chart Preview", widget);
    label->setAlignment(Qt::AlignCenter);
    label->setGeometry(widget->rect());
    
    return widget;
}

QWidget* ThemePreviewWidget::createMiniPieChart() {
    auto* widget = new QWidget();
    widget->setFixedSize(200, 150);
    
    auto* label = new QLabel("Pie Chart Preview", widget);
    label->setAlignment(Qt::AlignCenter);
    label->setGeometry(widget->rect());
    
    return widget;
}

void ThemePreviewWidget::updateTheme(const ColorPalette& colors,
                                    const Typography& typography,
                                    const ComponentStyles& components) {
    currentColors_ = colors;
    currentTypography_ = typography;
    currentComponents_ = components;
    
    // Recreate all preview widgets with new theme
    createFullUIPreview();
    createColorsOnlyPreview();
    createComponentsPreview();
    createChartsPreview();
    
    // Update the stacked widget
    while (stackedWidget_->count() > 0) {
        stackedWidget_->removeWidget(stackedWidget_->widget(0));
    }
    
    stackedWidget_->addWidget(fullUIWidget_);
    stackedWidget_->addWidget(colorsOnlyWidget_);
    stackedWidget_->addWidget(componentsWidget_);
    stackedWidget_->addWidget(chartsWidget_);
}

void ThemePreviewWidget::setPreviewMode(const QString& mode) {
    if (mode == "Full UI") {
        stackedWidget_->setCurrentIndex(0);
    } else if (mode == "Colors Only") {
        stackedWidget_->setCurrentIndex(1);
    } else if (mode == "Components") {
        stackedWidget_->setCurrentIndex(2);
    } else if (mode == "Charts") {
        stackedWidget_->setCurrentIndex(3);
    }
}

void ThemePreviewWidget::highlightComponent(const QString& componentName) {
    // Find the component in the current view
    QWidget* targetWidget = nullptr;
    
    if (componentName == "button" && fullUIPreview_) {
        targetWidget = fullUIPreview_->findChild<QPushButton*>();
    } else if (componentName == "input" && fullUIPreview_) {
        targetWidget = fullUIPreview_->findChild<QLineEdit*>();
    } else if (componentName == "card" && fullUIPreview_) {
        targetWidget = fullUIPreview_->findChild<QFrame*>();
    }
    
    if (targetWidget) {
        // Create highlight animation
        auto* effect = new QGraphicsColorizeEffect();
        effect->setColor(ThemeManager::instance().colors().primary);  // Use theme primary color
        targetWidget->setGraphicsEffect(effect);
        
        // Animate the highlight
        auto* animation = new QPropertyAnimation(effect, "strength");
        animation->setDuration(1000);
        animation->setKeyValueAt(0, 0.0);
        animation->setKeyValueAt(0.5, 1.0);
        animation->setKeyValueAt(1.0, 0.0);
        animation->setEasingCurve(QEasingCurve::InOutQuad);
        
        // Remove effect after animation
        connect(animation, &QPropertyAnimation::finished, [targetWidget, effect]() {
            targetWidget->setGraphicsEffect(nullptr);
            effect->deleteLater();
        });
        
        animation->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

// Mini component implementations

MiniButton::MiniButton(const QString& text, const ColorPalette& colors,
                      const ComponentStyles& styles, QWidget* parent)
    : QPushButton(text, parent) {
    
    // Apply theme styles
    QString style = QString(R"(
        QPushButton {
            background-color: %1;
            color: %2;
            border: %3px solid %4;
            border-radius: %5px;
            padding: %6px %7px;
            font-weight: 500;
        }
        QPushButton:hover {
            background-color: %8;
        }
        QPushButton:pressed {
            background-color: %9;
        }
    )").arg(colors.primary.name())
       .arg(colors.textInverse.name())
       .arg(styles.button.borderWidth)
       .arg(colors.border.name())
       .arg(styles.button.borderRadius)
       .arg(styles.button.paddingVertical)
       .arg(styles.button.paddingHorizontal)
       .arg(colors.primaryHover.name())
       .arg(colors.primaryActive.name());
    
    setStyleSheet(style);
}

MiniInput::MiniInput(const QString& placeholder, const ColorPalette& colors,
                    const ComponentStyles& styles, QWidget* parent)
    : QLineEdit(parent) {
    
    setPlaceholderText(placeholder);
    
    QString style = QString(R"(
        QLineEdit {
            background-color: %1;
            color: %2;
            border: %3px solid %4;
            border-radius: %5px;
            padding: %6px %7px;
        }
        QLineEdit:focus {
            border-color: %8;
        }
    )").arg(colors.surface.name())
       .arg(colors.textPrimary.name())
       .arg(styles.input.borderWidth)
       .arg(colors.border.name())
       .arg(styles.input.borderRadius)
       .arg(styles.input.paddingVertical)
       .arg(styles.input.paddingHorizontal)
       .arg(colors.primary.name());
    
    setStyleSheet(style);
}

MiniCard::MiniCard(const QString& title, const QString& content,
                  const ColorPalette& colors, const ComponentStyles& styles,
                  QWidget* parent)
    : QFrame(parent) {
    
    setFrameStyle(QFrame::StyledPanel);
    
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(styles.card.padding, styles.card.padding,
                              styles.card.padding, styles.card.padding);
    
    auto* titleLabel = new QLabel(title);
    titleLabel->setStyleSheet(QString("font-weight: bold; color: %1;")
                             .arg(colors.textPrimary.name()));
    layout->addWidget(titleLabel);
    
    auto* contentLabel = new QLabel(content);
    contentLabel->setStyleSheet(QString("color: %1;")
                               .arg(colors.textSecondary.name()));
    layout->addWidget(contentLabel);
    
    QString style = QString(R"(
        QFrame {
            background-color: %1;
            border: %2px solid %3;
            border-radius: %4px;
        }
    )").arg(colors.surface.name())
       .arg(styles.card.borderWidth)
       .arg(colors.border.name())
       .arg(styles.card.borderRadius);
    
    setStyleSheet(style);
}

MiniMessage::MiniMessage(MessageRole role, const QString& text,
                        const ColorPalette& colors, const ComponentStyles& styles,
                        QWidget* parent)
    : QWidget(parent), role_(role), text_(text), colors_(colors), styles_(styles) {
    
    setFixedHeight(50);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void MiniMessage::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // Determine colors based on role
    QColor bgColor;
    switch (role_) {
        case User:
            bgColor = colors_.userMessage;
            break;
        case Assistant:
            bgColor = colors_.assistantMessage;
            break;
        case System:
            bgColor = colors_.systemMessage;
            break;
    }
    
    // Draw rounded rectangle
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(5, 5, -5, -5), 
                       styles_.message.borderRadius,
                       styles_.message.borderRadius);
    
    painter.fillPath(path, bgColor);
    
    // Draw text
    painter.setPen(colors_.textPrimary);
    painter.drawText(rect().adjusted(15, 0, -15, 0), 
                    Qt::AlignVCenter | Qt::AlignLeft, text_);
}

MiniSyntaxHighlight::MiniSyntaxHighlight(const ColorPalette& colors, QWidget* parent)
    : QWidget(parent), colors_(colors) {
    
    setFixedHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    generateSampleCode();
}

void MiniSyntaxHighlight::generateSampleCode() {
    // Sample Python code
    tokens_ = {
        {"def ", colors_.syntaxKeyword, QFont()},
        {"analyze_function", colors_.syntaxFunction, QFont()},
        {"(", colors_.syntaxOperator, QFont()},
        {"func_ea", colors_.syntaxVariable, QFont()},
        {"):\n", colors_.syntaxOperator, QFont()},
        {"    ", colors_.codeText, QFont()},
        {"# Analyze the function\n", colors_.syntaxComment, QFont()},
        {"    ", colors_.codeText, QFont()},
        {"return ", colors_.syntaxKeyword, QFont()},
        {"\"Analysis complete\"", colors_.syntaxString, QFont()}
    };
}

void MiniSyntaxHighlight::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(rect(), colors_.codeBackground);
    
    // Draw code tokens
    int x = 10;
    int y = 20;
    int lineHeight = 20;
    
    for (const auto& token : tokens_) {
        painter.setPen(token.color);
        painter.setFont(token.font);
        
        if (token.text.contains('\n')) {
            // New line
            painter.drawText(x, y, token.text.left(token.text.indexOf('\n')));
            x = 10;
            y += lineHeight;
        } else {
            painter.drawText(x, y, token.text);
            x += painter.fontMetrics().horizontalAdvance(token.text);
        }
    }
}

} // namespace llm_re::ui_v2