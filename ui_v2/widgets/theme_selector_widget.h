#pragma once

#include "../core/ui_v2_common.h"
#include "../core/theme_manager.h"

namespace llm_re::ui_v2 {

class ThemeSelectorWidget : public QWidget {
    Q_OBJECT

public:
    explicit ThemeSelectorWidget(QWidget* parent = nullptr);
    ~ThemeSelectorWidget() = default;

    void refresh();

signals:
    void themeChanged(const QString& themeName);
    void editThemeRequested(const QString& themeName);
    void createThemeRequested();
    void deleteThemeRequested(const QString& themeName);

private slots:
    void onThemeSelected();
    void onEditClicked();
    void onCreateClicked();
    void onDeleteClicked();
    void onImportClicked();
    void onExportClicked();

private:
    void setupUI();
    void loadThemes();
    void createThemeCard(const QString& themeName, bool isBuiltIn);
    void updateSelection();
    void showThemePreview(const QString& themeName);
    
    // Theme card widget
    class ThemeCard : public QWidget {
        Q_OBJECT
        
    public:
        explicit ThemeCard(const QString& themeName, bool isBuiltIn, QWidget* parent = nullptr);
        
        QString themeName() const { return themeName_; }
        bool isBuiltIn() const { return isBuiltIn_; }
        void setSelected(bool selected);
        
    signals:
        void clicked();
        void deleteRequested();
        
    protected:
        void paintEvent(QPaintEvent* event) override;
        void enterEvent(QEnterEvent* event) override;
        void leaveEvent(QEvent* event) override;
        void mousePressEvent(QMouseEvent* event) override;
        void contextMenuEvent(QContextMenuEvent* event) override;
        
    private:
        QString themeName_;
        bool isBuiltIn_;
        bool selected_ = false;
        bool hovered_ = false;
        QPixmap preview_;
        
        void generatePreview();
    };
    
    // UI elements
    QScrollArea* scrollArea_ = nullptr;
    QWidget* cardsContainer_ = nullptr;
    QGridLayout* cardsLayout_ = nullptr;
    QLabel* currentThemeLabel_ = nullptr;
    QPushButton* editButton_ = nullptr;
    QPushButton* createButton_ = nullptr;
    QPushButton* importButton_ = nullptr;
    QPushButton* exportButton_ = nullptr;
    QTextEdit* descriptionText_ = nullptr;
    
    // Theme cards
    std::vector<ThemeCard*> themeCards_;
    ThemeCard* selectedCard_ = nullptr;
    
    // Current state
    QString currentThemeName_;
};

} // namespace llm_re::ui_v2