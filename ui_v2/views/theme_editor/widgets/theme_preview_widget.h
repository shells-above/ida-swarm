#pragma once

#include "../../../core/ui_v2_common.h"
#include "../../../core/ui_constants.h"

namespace llm_re::ui_v2 {

class ThemePreviewWidget : public QWidget {
    Q_OBJECT

public:
    explicit ThemePreviewWidget(QWidget* parent = nullptr);
    ~ThemePreviewWidget() = default;

    // Update preview with new theme data
    void updateTheme(const ColorPalette& colors, 
                    const Typography& typography,
                    const ComponentStyles& components);

    // Set preview mode
    void setPreviewMode(const QString& mode);

    // Show specific component preview
    void highlightComponent(const QString& componentName);

private:
    void setupUI();
    void createFullUIPreview();
    void createColorsOnlyPreview();
    void createComponentsPreview();
    void createChartsPreview();
    
    // Preview components
    QWidget* createButtonPreview();
    QWidget* createInputPreview();
    QWidget* createCardPreview();
    QWidget* createMessagePreview();
    QWidget* createSyntaxPreview();
    QWidget* createStatusPreview();
    
    // Mini chart previews
    QWidget* createMiniLineChart();
    QWidget* createMiniBarChart();
    QWidget* createMiniPieChart();
    
    void applyThemeToWidget(QWidget* widget);
    
    // Main container
    QStackedWidget* stackedWidget_ = nullptr;
    
    // Different preview modes
    QWidget* fullUIWidget_ = nullptr;
    QWidget* fullUIPreview_ = nullptr;
    QScrollArea* colorsOnlyWidget_ = nullptr;
    QScrollArea* componentsWidget_ = nullptr;
    QWidget* chartsWidget_ = nullptr;
    
    // Current theme data
    ColorPalette currentColors_;
    Typography currentTypography_;
    ComponentStyles currentComponents_;
    
    // Highlight animation
    QPropertyAnimation* highlightAnimation_ = nullptr;
};

// Mini component previews
class MiniButton : public QPushButton {
public:
    MiniButton(const QString& text, const ColorPalette& colors, 
               const ComponentStyles& styles, QWidget* parent = nullptr);
};

class MiniInput : public QLineEdit {
public:
    MiniInput(const QString& placeholder, const ColorPalette& colors,
              const ComponentStyles& styles, QWidget* parent = nullptr);
};

class MiniCard : public QFrame {
public:
    MiniCard(const QString& title, const QString& content,
             const ColorPalette& colors, const ComponentStyles& styles,
             QWidget* parent = nullptr);
};

class MiniMessage : public QWidget {
public:
    enum MessageRole { User, Assistant, System };
    
    MiniMessage(MessageRole role, const QString& text,
                const ColorPalette& colors, const ComponentStyles& styles,
                QWidget* parent = nullptr);
                
protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    MessageRole role_;
    QString text_;
    ColorPalette colors_;
    ComponentStyles styles_;
};

class MiniSyntaxHighlight : public QWidget {
public:
    MiniSyntaxHighlight(const ColorPalette& colors, QWidget* parent = nullptr);
    
protected:
    void paintEvent(QPaintEvent* event) override;
    
private:
    ColorPalette colors_;
    
    struct Token {
        QString text;
        QColor color;
        QFont font;
    };
    
    QList<Token> tokens_;
    void generateSampleCode();
};

} // namespace llm_re::ui_v2