#include "theme_save_as_dialog.h"

namespace llm_re::ui_v2 {

ThemeSaveAsDialog::ThemeSaveAsDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Save Theme As");
    setModal(true);
    resize(600, 250);
    
    setupUI();
}

void ThemeSaveAsDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(15);
    
    // Instructions
    auto* instructionLabel = new QLabel("Enter a name for your theme:");
    instructionLabel->setStyleSheet("font-weight: bold;");
    layout->addWidget(instructionLabel);
    
    // Theme name input
    auto* nameLayout = new QHBoxLayout();
    auto* nameLabel = new QLabel("Theme Name:");
    nameLabel->setFixedWidth(100);
    
    nameEdit_ = new QLineEdit();
    nameEdit_->setPlaceholderText("Enter theme name...");
    connect(nameEdit_, &QLineEdit::textChanged, this, &ThemeSaveAsDialog::validateName);
    
    nameLayout->addWidget(nameLabel);
    nameLayout->addWidget(nameEdit_);
    layout->addLayout(nameLayout);
    
    // File path preview
    auto* pathLayout = new QHBoxLayout();
    auto* pathLabel = new QLabel("Will save to:");
    pathLabel->setFixedWidth(100);
    
    filePathLabel_ = new QLabel();
    filePathLabel_->setStyleSheet("color: #888; font-family: monospace; font-size: 11px;");
    filePathLabel_->setWordWrap(true);
    filePathLabel_->setMinimumHeight(40);
    
    pathLayout->addWidget(pathLabel);
    pathLayout->addWidget(filePathLabel_, 1);
    layout->addLayout(pathLayout);
    
    // Validation feedback
    validationLabel_ = new QLabel();
    validationLabel_->setMinimumHeight(30);
    layout->addWidget(validationLabel_);
    
    // Separator
    auto* separator = new QFrame();
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    layout->addWidget(separator);
    
    // Help text
    auto* helpLabel = new QLabel(
        "Note: Theme names can contain letters, numbers, spaces, and underscores.\n"
        "Invalid characters will be automatically removed.");
    helpLabel->setStyleSheet("color: #666; font-size: 10px;");
    helpLabel->setWordWrap(true);
    layout->addWidget(helpLabel);
    
    layout->addStretch();
    
    // Button box
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    saveButton_ = new QPushButton("Save");
    saveButton_->setEnabled(false);
    saveButton_->setDefault(true);
    connect(saveButton_, &QPushButton::clicked, this, &ThemeSaveAsDialog::onSave);
    
    cancelButton_ = new QPushButton("Cancel");
    connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
    
    buttonLayout->addWidget(saveButton_);
    buttonLayout->addWidget(cancelButton_);
    layout->addLayout(buttonLayout);
}

void ThemeSaveAsDialog::setCurrentName(const QString& name) {
    nameEdit_->setText(name);
    nameEdit_->selectAll();
    nameEdit_->setFocus();
}

QString ThemeSaveAsDialog::getThemeName() const {
    return themeName_;
}

void ThemeSaveAsDialog::validateName(const QString& name) {
    auto& tm = ThemeManager::instance();
    
    // Sanitize the name
    QString sanitized = tm.sanitizeThemeName(name);
    
    // If sanitization changed the name, update the field
    if (sanitized != name && !name.isEmpty()) {
        // Block signals to avoid recursion
        nameEdit_->blockSignals(true);
        
        // Save cursor position
        int cursorPos = nameEdit_->cursorPosition();
        
        // Update text
        nameEdit_->setText(sanitized);
        
        // Restore cursor position (adjusted for length change)
        int newPos = qMin(cursorPos, sanitized.length());
        nameEdit_->setCursorPosition(newPos);
        
        nameEdit_->blockSignals(false);
        return;  // validateName will be called again with sanitized name
    }
    
    // Update file path preview
    updateFilePathPreview();
    
    // Validate the name
    if (sanitized.isEmpty()) {
        validationLabel_->setText("❌ Please enter a theme name");
        validationLabel_->setStyleSheet("color: red;");
        saveButton_->setEnabled(false);
        isValid_ = false;
        return;
    }
    
    if (!tm.isValidThemeName(sanitized)) {
        validationLabel_->setText("❌ Invalid theme name");
        validationLabel_->setStyleSheet("color: red;");
        saveButton_->setEnabled(false);
        isValid_ = false;
        return;
    }
    
    // Check if it's a built-in theme name
    if (tm.isBuiltInTheme(sanitized)) {
        validationLabel_->setText("❌ Cannot use built-in theme name");
        validationLabel_->setStyleSheet("color: red;");
        saveButton_->setEnabled(false);
        isValid_ = false;
        return;
    }
    
    // Check for existing theme
    if (tm.themeExists(sanitized)) {
        validationLabel_->setText("⚠️ Theme already exists - will overwrite!");
        validationLabel_->setStyleSheet("color: orange; font-weight: bold;");
        saveButton_->setText("Overwrite");
        saveButton_->setEnabled(true);
        isValid_ = true;
    } else {
        validationLabel_->setText("✓ Valid theme name");
        validationLabel_->setStyleSheet("color: green;");
        saveButton_->setText("Save");
        saveButton_->setEnabled(true);
        isValid_ = true;
    }
    
    themeName_ = sanitized;
}

void ThemeSaveAsDialog::updateFilePathPreview() {
    auto& tm = ThemeManager::instance();
    QString name = nameEdit_->text();
    
    if (name.isEmpty()) {
        filePathLabel_->setText("(enter a name to see file path)");
        return;
    }
    
    QString sanitized = tm.sanitizeThemeName(name);
    QString path = tm.getThemeFilePath(sanitized);
    
    if (path.isEmpty()) {
        filePathLabel_->setText("(built-in theme - cannot save with this name)");
    } else {
        filePathLabel_->setText(path);
        
        // Make the path more readable by adding line breaks if too long
        if (path.length() > 60) {
            int lastSlash = path.lastIndexOf('/');
            if (lastSlash > 0) {
                QString displayPath = path.left(lastSlash + 1) + "\n" + path.mid(lastSlash + 1);
                filePathLabel_->setText(displayPath);
            }
        }
    }
}

void ThemeSaveAsDialog::onSave() {
    if (!isValid_ || themeName_.isEmpty()) {
        return;
    }
    
    // Check for overwrite confirmation
    if (ThemeManager::instance().themeExists(themeName_)) {
        int ret = QMessageBox::warning(this, "Overwrite Theme",
            QString("Theme '%1' already exists. Do you want to overwrite it?").arg(themeName_),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        
        if (ret != QMessageBox::Yes) {
            return;
        }
    }
    
    accept();
}

} // namespace llm_re::ui_v2