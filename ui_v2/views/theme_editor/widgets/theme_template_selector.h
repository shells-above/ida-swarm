#pragma once

#include "../../../core/ui_v2_common.h"
#include "../../../core/theme_templates.h"

namespace llm_re::ui_v2 {

// Forward declaration
class ThemeTemplateSelector;

// Template card widget for displaying theme templates
class TemplateCard : public QWidget {
    Q_OBJECT
    
public:
    explicit TemplateCard(const ThemeTemplates::TemplateInfo& info, 
                        int index, QWidget* parent = nullptr);
    
    int templateIndex() const { return templateIndex_; }
    QString category() const { return category_; }
    
signals:
    void clicked();
    
protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    
private:
    ThemeTemplates::TemplateInfo info_;
    int templateIndex_;
    QString category_;
    bool hovered_ = false;
    bool selected_ = false;
    
    friend class ThemeTemplateSelector;
};

class ThemeTemplateSelector : public QWidget {
    Q_OBJECT

public:
    explicit ThemeTemplateSelector(QWidget* parent = nullptr);
    ~ThemeTemplateSelector() = default;

signals:
    void templateSelected(ThemeTemplates::Template tmpl);
    void createFromTemplate(ThemeTemplates::Template tmpl, const QString& name);

private slots:
    void onTemplateClicked();
    void onFilterChanged(const QString& filter);
    void onCreateClicked();

private:
    void setupUI();
    void createTemplateCard(const ThemeTemplates::TemplateInfo& info, int index);
    void updateFilter();
    
    // UI elements
    QLineEdit* searchEdit_ = nullptr;
    QComboBox* categoryCombo_ = nullptr;
    QScrollArea* scrollArea_ = nullptr;
    QWidget* cardsContainer_ = nullptr;
    QGridLayout* cardsLayout_ = nullptr;
    QPushButton* createButton_ = nullptr;
    QPushButton* applyButton_ = nullptr;
    QTextEdit* descriptionText_ = nullptr;
    
    // Template cards
    std::vector<TemplateCard*> templateCards_;
    TemplateCard* selectedCard_ = nullptr;
    
    // Current filter
    QString currentFilter_;
    QString currentCategory_ = "All";
};

} // namespace llm_re::ui_v2