#pragma once

#include "../../../core/ui_v2_common.h"
#include "../../../core/theme_manager.h"
#include "../../../core/theme_constants.h"

namespace llm_re::ui_v2 {

class ThemeSaveAsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ThemeSaveAsDialog(QWidget* parent = nullptr);
    
    // Set initial name
    void setCurrentName(const QString& name);
    
    // Get the chosen name
    QString getThemeName() const;
    
private slots:
    void validateName(const QString& name);
    void onSave();
    
private:
    void setupUI();
    void updateFilePathPreview();
    
    // UI elements
    QLineEdit* nameEdit_ = nullptr;
    QLabel* filePathLabel_ = nullptr;
    QLabel* validationLabel_ = nullptr;
    QPushButton* saveButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    
    // State
    QString themeName_;
    bool isValid_ = false;
};

} // namespace llm_re::ui_v2