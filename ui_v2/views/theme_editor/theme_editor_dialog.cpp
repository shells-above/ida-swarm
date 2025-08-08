#include "theme_editor_dialog.h"
#include "../../core/theme_manager.h"
#include "../../core/animation_manager.h"
#include "../../core/effects_manager.h"
#include "../../core/theme_templates.h"
#include "../../core/theme_undo_manager.h"
#include "../../../core/config.h"
#include "widgets/color_picker_widget.h"
#include "widgets/theme_preview_widget.h"
#include "widgets/animation_config_widget.h"
#include "widgets/effects_config_widget.h"
#include "widgets/chart_theme_widget.h"
#include "widgets/theme_template_selector.h"
#include "widgets/accessibility_tools.h"
#include "widgets/theme_save_as_dialog.h"

namespace llm_re::ui_v2 {

ThemeEditorDialog::ThemeEditorDialog(QWidget* parent)
    : QDialog(parent) {
    setWindowTitle("Theme Editor");
    setModal(false);  // Non-modal so users can see changes in real-time
    
    // Set size constraints
    resize(1200, 800);
    setMinimumSize(800, 600);
    setMaximumSize(1600, 1200);
    
    // Enable resize grip
    setSizeGripEnabled(true);
    
    // Setup preview timer BEFORE loading theme to avoid crashes
    previewTimer_ = new QTimer(this);
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(100);  // 100ms delay for preview updates
    connect(previewTimer_, &QTimer::timeout, this, &ThemeEditorDialog::updatePreview);
    
    setupUI();
    loadCurrentTheme();
}

void ThemeEditorDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    // Create menu bar
    createMenuBar();
    
    // Create toolbar
    createToolBar();
    
    // Create status bar
    statusBar_ = new QStatusBar(this);
    statusBar_->setSizeGripEnabled(false);
    
    // Create main content area with splitter
    mainSplitter_ = new QSplitter(Qt::Horizontal);
    
    // Left side: Tabs for editing
    auto* leftWidget = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftWidget);
    
    // Metadata section
    leftLayout->addWidget(createMetadataWidget());
    
    // Tab widget for different aspects
    tabWidget_ = new QTabWidget();
    createTabs();
    leftLayout->addWidget(tabWidget_);
    
    // Right side: Live preview
    previewWidget_ = new ThemePreviewWidget();
    previewWidget_->setMinimumWidth(400);
    
    mainSplitter_->addWidget(leftWidget);
    mainSplitter_->addWidget(previewWidget_);
    mainSplitter_->setStretchFactor(0, 3);
    mainSplitter_->setStretchFactor(1, 2);
    
    mainLayout->addWidget(mainSplitter_);
    
    // Bottom button bar
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    auto* applyButton = new QPushButton("Apply");
    connect(applyButton, &QPushButton::clicked, this, &ThemeEditorDialog::onApplyTheme);
    
    auto* saveButton = new QPushButton("Save");
    connect(saveButton, &QPushButton::clicked, this, &ThemeEditorDialog::onSaveTheme);
    
    auto* cancelButton = new QPushButton("Cancel");
    connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    
    buttonLayout->addWidget(applyButton);
    buttonLayout->addWidget(saveButton);
    buttonLayout->addWidget(cancelButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Add status bar at the bottom
    mainLayout->addWidget(statusBar_);
}

void ThemeEditorDialog::createMenuBar() {
    auto* menuBar = new QMenuBar(this);
    
    // File menu
    auto* fileMenu = menuBar->addMenu("File");
    
    saveAction_ = fileMenu->addAction("Save", this, &ThemeEditorDialog::onSaveTheme);
    saveAction_->setShortcut(QKeySequence::Save);
    
    saveAsAction_ = fileMenu->addAction("Save As...", this, &ThemeEditorDialog::onSaveThemeAs);
    saveAsAction_->setShortcut(QKeySequence::SaveAs);
    
    fileMenu->addSeparator();
    
    loadAction_ = fileMenu->addAction("Load Theme...", this, &ThemeEditorDialog::onLoadTheme);
    loadAction_->setShortcut(QKeySequence::Open);
    
    fileMenu->addSeparator();
    
    exportAction_ = fileMenu->addAction("Export Theme...", this, &ThemeEditorDialog::onExportTheme);
    importAction_ = fileMenu->addAction("Import Theme...", this, &ThemeEditorDialog::onImportTheme);
    
    // Edit menu
    auto* editMenu = menuBar->addMenu("Edit");
    
    resetAction_ = editMenu->addAction("Reset to Base Theme", this, &ThemeEditorDialog::onResetTheme);
    
    editMenu->addSeparator();
    
    applyAction_ = editMenu->addAction("Apply Changes", this, &ThemeEditorDialog::onApplyTheme);
    applyAction_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    
    layout()->setMenuBar(menuBar);
}

void ThemeEditorDialog::createToolBar() {
    auto* toolBar = new QToolBar();
    toolBar->setMovable(false);
    
    // Theme selector
    auto* themeLabel = new QLabel("Base Theme: ");
    toolBar->addWidget(themeLabel);
    
    baseThemeCombo_ = new QComboBox();
    baseThemeCombo_->addItems({"Dark", "Light", "Default"});
    connect(baseThemeCombo_, &QComboBox::currentTextChanged,
            [this]() { setHasChanges(true); });
    toolBar->addWidget(baseThemeCombo_);
    
    toolBar->addSeparator();
    
    // Quick actions
    toolBar->addAction(QIcon(":/icons/save.svg"), "Save", this, &ThemeEditorDialog::onSaveTheme);
    toolBar->addAction(QIcon(":/icons/folder-open.svg"), "Load", this, &ThemeEditorDialog::onLoadTheme);
    toolBar->addAction(QIcon(":/icons/refresh.svg"), "Reset", this, &ThemeEditorDialog::onResetTheme);
    
    toolBar->addSeparator();
    
    // Undo/Redo actions
    auto* undoAction = toolBar->addAction(QIcon(":/icons/undo.svg"), "Undo");
    undoAction->setShortcut(QKeySequence::Undo);
    connect(undoAction, &QAction::triggered, []() {
        ThemeUndoManager::instance().undo();
    });
    
    auto* redoAction = toolBar->addAction(QIcon(":/icons/redo.svg"), "Redo");
    redoAction->setShortcut(QKeySequence::Redo);
    connect(redoAction, &QAction::triggered, []() {
        ThemeUndoManager::instance().redo();
    });
    
    // Connect undo manager signals
    connect(&ThemeUndoManager::instance(), &ThemeUndoManager::canUndoChanged,
            undoAction, &QAction::setEnabled);
    connect(&ThemeUndoManager::instance(), &ThemeUndoManager::canRedoChanged,
            redoAction, &QAction::setEnabled);
    
    // Initial state
    undoAction->setEnabled(ThemeUndoManager::instance().canUndo());
    redoAction->setEnabled(ThemeUndoManager::instance().canRedo());
    
    toolBar->addSeparator();
    
    // Hot reload toggle
    hotReloadCheck_ = new QCheckBox("Hot Reload");
    hotReloadCheck_->setChecked(hotReloadEnabled_);
    hotReloadCheck_->setToolTip("Apply changes immediately to the UI");
    connect(hotReloadCheck_, &QCheckBox::toggled, [this](bool checked) {
        hotReloadEnabled_ = checked;
    });
    toolBar->addWidget(hotReloadCheck_);
    
    toolBar->addSeparator();
    
    // Preview mode selector
    auto* previewLabel = new QLabel("Preview: ");
    toolBar->addWidget(previewLabel);
    
    auto* previewModeCombo = new QComboBox();
    previewModeCombo->addItems({"Full UI", "Colors Only", "Components", "Charts"});
    connect(previewModeCombo, &QComboBox::currentTextChanged,
            [this](const QString& mode) {
                previewWidget_->setPreviewMode(mode);
            });
    toolBar->addWidget(previewModeCombo);
    
    static_cast<QVBoxLayout*>(layout())->insertWidget(0, toolBar);
}

void ThemeEditorDialog::createTabs() {
    createTemplatesTab();
    createColorsTab();
    createTypographyTab();
    createComponentsTab();
    createAnimationsTab();
    createEffectsTab();
    createChartsTab();
    createAccessibilityTab();
}

void ThemeEditorDialog::createTemplatesTab() {
    templateSelector_ = new ThemeTemplateSelector();
    
    connect(templateSelector_, &ThemeTemplateSelector::templateSelected,
            this, &ThemeEditorDialog::onTemplateSelected);
    
    connect(templateSelector_, &ThemeTemplateSelector::createFromTemplate,
            this, &ThemeEditorDialog::onCreateFromTemplate);
    
    tabWidget_->addTab(templateSelector_, "Templates");
}

void ThemeEditorDialog::createColorsTab() {
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    
    // Group colors by category
    struct ColorCategory {
        QString name;
        QStringList colors;
    };
    
    std::vector<ColorCategory> categories = {
        {"Brand Colors", {"primary", "primaryHover", "primaryActive"}},
        {"Semantic Colors", {"success", "warning", "error", "info"}},
        {"UI Colors", {"background", "surface", "surfaceHover", "surfaceActive", 
                      "border", "borderStrong", "overlay", "shadow"}},
        {"Text Colors", {"textPrimary", "textSecondary", "textTertiary", 
                        "textInverse", "textLink"}},
        {"Message Colors", {"userMessage", "assistantMessage", "systemMessage"}},
        {"Analysis Colors", {"analysisNote", "analysisFinding", "analysisHypothesis",
                           "analysisQuestion", "analysisAnalysis", "analysisDeepAnalysis"}},
        {"Syntax Highlighting", {"syntaxKeyword", "syntaxString", "syntaxNumber",
                               "syntaxComment", "syntaxFunction", "syntaxVariable", 
                               "syntaxOperator"}},
        {"Status Colors", {"statusPending", "statusRunning", "statusCompleted",
                          "statusFailed", "statusInterrupted", "statusUnknown"}},
        {"Special Purpose", {"codeBackground", "codeText", "selection", "bookmark",
                           "searchHighlight", "diffAdd", "diffRemove", "currentLineHighlight"}}
    };
    
    for (const auto& category : categories) {
        auto* group = new QGroupBox(category.name);
        auto* groupLayout = new QGridLayout(group);
        
        int row = 0;
        int col = 0;
        
        for (const QString& colorName : category.colors) {
            auto* label = new QLabel(colorName);
            auto* picker = new ColorPickerWidget();
            
            connect(picker, &ColorPickerWidget::colorChanged,
                    [this, colorName](const QColor& color) {
                        onColorChanged(colorName, color);
                    });
            
            groupLayout->addWidget(label, row, col * 2);
            groupLayout->addWidget(picker, row, col * 2 + 1);
            
            colorPickers_[colorName] = picker;
            
            col++;
            if (col >= 2) {
                col = 0;
                row++;
            }
        }
        
        layout->addWidget(group);
    }
    
    layout->addStretch();
    scrollArea->setWidget(widget);
    tabWidget_->addTab(scrollArea, "Colors");
}

void ThemeEditorDialog::createTypographyTab() {
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    
    auto* widget = new QWidget();
    auto* layout = new QFormLayout(widget);
    
    // Font families
    baseFontCombo_ = new QFontComboBox();
    connect(baseFontCombo_, &QFontComboBox::currentFontChanged,
            [this]() { onTypographyChanged(); });
    layout->addRow("Base Font:", baseFontCombo_);
    
    codeFontCombo_ = new QFontComboBox();
    codeFontCombo_->setFontFilters(QFontComboBox::MonospacedFonts);
    connect(codeFontCombo_, &QFontComboBox::currentFontChanged,
            [this]() { onTypographyChanged(); });
    layout->addRow("Code Font:", codeFontCombo_);
    
    // Font scale
    auto* scaleLayout = new QHBoxLayout();
    fontScaleSlider_ = new QSlider(Qt::Horizontal);
    fontScaleSlider_->setRange(50, 200);
    fontScaleSlider_->setValue(100);
    fontScaleSlider_->setTickPosition(QSlider::TicksBelow);
    fontScaleSlider_->setTickInterval(25);
    
    fontScaleLabel_ = new QLabel("100%");
    fontScaleLabel_->setMinimumWidth(50);
    
    connect(fontScaleSlider_, &QSlider::valueChanged, [this](int value) {
        fontScaleLabel_->setText(QString("%1%").arg(value));
        onTypographyChanged();
    });
    
    scaleLayout->addWidget(fontScaleSlider_);
    scaleLayout->addWidget(fontScaleLabel_);
    layout->addRow("Font Scale:", scaleLayout);
    
    // Individual font sizes
    auto* sizesGroup = new QGroupBox("Font Sizes");
    auto* sizesLayout = new QFormLayout(sizesGroup);
    
    QStringList fontTypes = {"heading1", "heading2", "heading3", "body", 
                            "bodySmall", "code", "caption"};
    
    for (const QString& fontType : fontTypes) {
        auto* spin = new QSpinBox();
        spin->setRange(8, 48);
        spin->setSuffix(" px");
        
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                [this]() { onTypographyChanged(); });
        
        sizesLayout->addRow(fontType + ":", spin);
        fontSizeSpins_[fontType] = spin;
    }
    
    layout->addRow(sizesGroup);
    
    scrollArea->setWidget(widget);
    tabWidget_->addTab(scrollArea, "Typography");
}

void ThemeEditorDialog::createComponentsTab() {
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    
    // Density mode
    auto* densityLayout = new QHBoxLayout();
    densityLayout->addWidget(new QLabel("Density:"));
    
    densityCombo_ = new QComboBox();
    densityCombo_->addItems({"Compact", "Cozy", "Spacious"});
    connect(densityCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [this]() { onComponentStyleChanged(); });
    
    densityLayout->addWidget(densityCombo_);
    densityLayout->addStretch();
    layout->addLayout(densityLayout);
    
    // Component-specific settings
    // Button settings
    auto* buttonGroup = new QGroupBox("Buttons");
    auto* buttonLayout = new QFormLayout(buttonGroup);
    
    auto* btnPaddingH = new QSpinBox();
    btnPaddingH->setRange(0, 50);
    btnPaddingH->setSuffix(" px");
    connect(btnPaddingH, QOverload<int>::of(&QSpinBox::valueChanged),
            [this]() { onComponentStyleChanged(); });
    buttonLayout->addRow("Horizontal Padding:", btnPaddingH);
    componentEditors_["button.paddingHorizontal"] = btnPaddingH;
    
    auto* btnPaddingV = new QSpinBox();
    btnPaddingV->setRange(0, 50);
    btnPaddingV->setSuffix(" px");
    connect(btnPaddingV, QOverload<int>::of(&QSpinBox::valueChanged),
            [this]() { onComponentStyleChanged(); });
    buttonLayout->addRow("Vertical Padding:", btnPaddingV);
    componentEditors_["button.paddingVertical"] = btnPaddingV;
    
    auto* btnRadius = new QSpinBox();
    btnRadius->setRange(0, 50);
    btnRadius->setSuffix(" px");
    connect(btnRadius, QOverload<int>::of(&QSpinBox::valueChanged),
            [this]() { onComponentStyleChanged(); });
    buttonLayout->addRow("Border Radius:", btnRadius);
    componentEditors_["button.borderRadius"] = btnRadius;
    
    layout->addWidget(buttonGroup);
    
    // Similar groups for other components...
    
    layout->addStretch();
    scrollArea->setWidget(widget);
    tabWidget_->addTab(scrollArea, "Components");
}

void ThemeEditorDialog::createAnimationsTab() {
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    
    animationConfig_ = new AnimationConfigWidget();
    connect(animationConfig_, &AnimationConfigWidget::settingChanged,
            this, &ThemeEditorDialog::onAnimationSettingChanged);
    
    scrollArea->setWidget(animationConfig_);
    tabWidget_->addTab(scrollArea, "Animations");
}

void ThemeEditorDialog::createEffectsTab() {
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    
    effectsConfig_ = new EffectsConfigWidget();
    connect(effectsConfig_, &EffectsConfigWidget::settingChanged,
            this, &ThemeEditorDialog::onEffectSettingChanged);
    
    scrollArea->setWidget(effectsConfig_);
    tabWidget_->addTab(scrollArea, "Effects");
}

void ThemeEditorDialog::createChartsTab() {
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    
    chartConfig_ = new ChartThemeWidget();
    connect(chartConfig_, &ChartThemeWidget::settingChanged,
            this, &ThemeEditorDialog::onChartSettingChanged);
    
    scrollArea->setWidget(chartConfig_);
    tabWidget_->addTab(scrollArea, "Charts");
}

QWidget* ThemeEditorDialog::createMetadataWidget() {
    auto* group = new QGroupBox("Theme Information");
    auto* layout = new QFormLayout(group);
    
    themeNameEdit_ = new QLineEdit();
    connect(themeNameEdit_, &QLineEdit::textChanged,
            this, &ThemeEditorDialog::onThemeNameChanged);
    layout->addRow("Theme Name:", themeNameEdit_);
    
    authorEdit_ = new QLineEdit();
    connect(authorEdit_, &QLineEdit::textChanged,
            [this]() { setHasChanges(true); });
    layout->addRow("Author:", authorEdit_);
    
    versionEdit_ = new QLineEdit();
    versionEdit_->setText("1.0");
    connect(versionEdit_, &QLineEdit::textChanged,
            [this]() { setHasChanges(true); });
    layout->addRow("Version:", versionEdit_);
    
    descriptionEdit_ = new QTextEdit();
    descriptionEdit_->setMaximumHeight(60);
    connect(descriptionEdit_, &QTextEdit::textChanged,
            [this]() { setHasChanges(true); });
    layout->addRow("Description:", descriptionEdit_);
    
    return group;
}

void ThemeEditorDialog::loadCurrentTheme() {
    auto& tm = ThemeManager::instance();
    auto info = tm.getCurrentThemeInfo();
    
    // Update metadata fields
    themeNameEdit_->setText(info.metadata.name);
    authorEdit_->setText(info.metadata.author);
    versionEdit_->setText(info.metadata.version);
    descriptionEdit_->setText(info.metadata.description);
    baseThemeCombo_->setCurrentText(info.metadata.baseTheme);
    
    // Update color pickers
    for (auto& [name, picker] : colorPickers_) {
        QColor color = tm.color(name);
        if (color.isValid()) {
            picker->setColor(color);
        }
    }
    
    // Load typography
    const auto& typography = tm.typography();
    
    baseFontCombo_->setCurrentFont(typography.body);
    codeFontCombo_->setCurrentFont(typography.code);
    fontScaleSlider_->setValue(tm.fontScale() * 100);
    
    // Update font size spinners if they exist
    if (fontSizeSpins_.count("heading1"))
        fontSizeSpins_["heading1"]->setValue(typography.heading1.pointSize());
    if (fontSizeSpins_.count("heading2"))
        fontSizeSpins_["heading2"]->setValue(typography.heading2.pointSize());
    if (fontSizeSpins_.count("heading3"))
        fontSizeSpins_["heading3"]->setValue(typography.heading3.pointSize());
    if (fontSizeSpins_.count("body"))
        fontSizeSpins_["body"]->setValue(typography.body.pointSize());
    if (fontSizeSpins_.count("bodySmall"))
        fontSizeSpins_["bodySmall"]->setValue(typography.bodySmall.pointSize());
    if (fontSizeSpins_.count("code"))
        fontSizeSpins_["code"]->setValue(typography.code.pointSize());
    if (fontSizeSpins_.count("caption"))
        fontSizeSpins_["caption"]->setValue(typography.caption.pointSize());
    
    densityCombo_->setCurrentIndex(tm.densityMode());
    
    // Load animation settings
    animationConfig_->loadSettings();
    
    // Load effects settings
    effectsConfig_->loadSettings();
    
    // Load chart settings
    chartConfig_->loadSettings();
    
    // Update preview
    updatePreview();
    
    // Reset change tracking
    setHasChanges(false);
    updateStatusBar();
}

void ThemeEditorDialog::onColorChanged(const QString& colorName, const QColor& color) {
    // Store old color for undo
    QColor oldColor = ThemeManager::instance().color(colorName);
    
    // Update the color immediately for hot reload
    if (hotReloadEnabled_) {
        ThemeManager::instance().setColor(colorName, color);
    }
    
    // Add to undo stack
    ThemeUndoManager::instance().executeCommand(
        makeColorChangeCommand(colorName, oldColor, color)
    );
    
    setHasChanges(true);
    previewTimer_->start();  // Debounced preview update
    
    // Show popup preview
    showColorPreview(colorName);
}

void ThemeEditorDialog::onTypographyChanged() {
    setHasChanges(true);
    previewTimer_->start();
}

void ThemeEditorDialog::onComponentStyleChanged() {
    setHasChanges(true);
    previewTimer_->start();
}

void ThemeEditorDialog::onAnimationSettingChanged() {
    setHasChanges(true);
    previewTimer_->start();
}

void ThemeEditorDialog::onEffectSettingChanged() {
    setHasChanges(true);
    previewTimer_->start();
}

void ThemeEditorDialog::onChartSettingChanged() {
    setHasChanges(true);
    previewTimer_->start();
}

void ThemeEditorDialog::updatePreview() {
    // Update the preview widget with current theme settings from ThemeManager
    auto& tm = ThemeManager::instance();
    previewWidget_->updateTheme(tm.colors(), tm.typography(), tm.componentStyles());
}

void ThemeEditorDialog::onSaveTheme() {
    auto& tm = ThemeManager::instance();
    auto info = tm.getCurrentThemeInfo();
    
    // If current theme has no name or is built-in, use Save As
    if (info.name.isEmpty() || info.isBuiltIn) {
        onSaveThemeAs();
        return;
    }
    
    // Update metadata from UI fields
    info.metadata.name = themeNameEdit_->text();
    info.metadata.author = authorEdit_->text();
    info.metadata.version = versionEdit_->text();
    info.metadata.description = descriptionEdit_->toPlainText();
    info.metadata.baseTheme = baseThemeCombo_->currentText();
    
    if (tm.saveTheme()) {
        setHasChanges(false);
        updateStatusBar();
        updateWindowTitle();
        emit themeSaved(info.name);
        
        QMessageBox::information(this, "Theme Saved", 
            QString("Theme '%1' saved successfully.").arg(info.displayName));
    } else {
        QMessageBox::critical(this, "Save Failed", "Failed to save theme.");
    }
}

void ThemeEditorDialog::onSaveThemeAs() {
    // Create and show the new Save As dialog
    ThemeSaveAsDialog dialog(this);
    
    auto currentInfo = ThemeManager::instance().getCurrentThemeInfo();
    if (!currentInfo.name.isEmpty() && !currentInfo.isBuiltIn) {
        dialog.setCurrentName(currentInfo.displayName);
    } else {
        dialog.setCurrentName(generateThemeName());
    }
    
    if (dialog.exec() == QDialog::Accepted) {
        QString newName = dialog.getThemeName();
        
        // Update metadata from UI fields
        auto& tm = ThemeManager::instance();
        auto info = tm.getCurrentThemeInfo();
        info.metadata.name = newName;
        info.metadata.author = authorEdit_->text();
        info.metadata.version = versionEdit_->text();
        info.metadata.description = descriptionEdit_->toPlainText();
        info.metadata.baseTheme = baseThemeCombo_->currentText();
        
        if (tm.saveThemeAs(newName)) {
            themeNameEdit_->setText(newName);
            setHasChanges(false);
            updateStatusBar();
            updateWindowTitle();
            emit themeSaved(newName);
            
            auto savedInfo = tm.getCurrentThemeInfo();
            QMessageBox::information(this, "Theme Saved", 
                QString("Theme '%1' saved successfully to:\n%2")
                .arg(savedInfo.displayName)
                .arg(savedInfo.filePath));
        } else {
            QMessageBox::critical(this, "Save Failed", "Failed to save theme.");
        }
    }
}

void ThemeEditorDialog::onLoadTheme() {
    auto themes = ThemeManager::instance().getAllThemes();
    
    QStringList themeNames;
    for (const auto& info : themes) {
        themeNames << info.displayName;
    }
    
    bool ok;
    QString selectedName = QInputDialog::getItem(this, "Load Theme", 
                                         "Select theme:", themeNames, 
                                         0, false, &ok);
    if (ok && !selectedName.isEmpty()) {
        // Find the actual theme name from display name
        QString themeName;
        for (const auto& info : themes) {
            if (info.displayName == selectedName) {
                themeName = info.name;
                break;
            }
        }
        
        if (hasUnsavedChanges()) {
            int ret = QMessageBox::question(this, "Unsaved Changes",
                "You have unsaved changes. Do you want to save them first?",
                QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
            
            if (ret == QMessageBox::Save) {
                onSaveTheme();
            } else if (ret == QMessageBox::Cancel) {
                return;
            }
        }
        
        loadTheme(themeName);
    }
}

void ThemeEditorDialog::onExportTheme() {
    QString fileName = QFileDialog::getSaveFileName(this, 
        "Export Theme", "", 
        QString("Theme Files (*%1)").arg(ThemeConstants::THEME_FILE_EXTENSION));
    
    if (fileName.isEmpty()) return;
    
    // Update metadata before export
    auto& tm = ThemeManager::instance();
    auto info = tm.getCurrentThemeInfo();
    info.metadata.name = themeNameEdit_->text();
    info.metadata.author = authorEdit_->text();
    info.metadata.version = versionEdit_->text();
    info.metadata.description = descriptionEdit_->toPlainText();
    info.metadata.baseTheme = baseThemeCombo_->currentText();
    tm.setCurrentThemeMetadata(info.metadata);
    
    if (tm.exportThemeFile(themeNameEdit_->text(), fileName)) {
        QMessageBox::information(this, "Export Successful", 
            "Theme exported successfully!");
    } else {
        QMessageBox::critical(this, "Export Failed", 
            "Failed to export theme.");
    }
}

void ThemeEditorDialog::onImportTheme() {
    QString fileName = QFileDialog::getOpenFileName(this, 
        "Import Theme", "", 
        QString("Theme Files (*%1)").arg(ThemeConstants::THEME_FILE_EXTENSION));
    
    if (fileName.isEmpty()) return;
    
    QString themeName = ThemeManager::instance().importThemeFile(fileName);
    
    if (!themeName.isEmpty()) {
        // Load the imported theme
        if (ThemeManager::instance().loadTheme(themeName)) {
            loadCurrentTheme();
            QMessageBox::information(this, "Import Successful", 
                QString("Theme '%1' imported successfully!").arg(themeName));
        }
    } else {
        QMessageBox::critical(this, "Import Failed", 
            "Failed to import theme. The file may be invalid or corrupted.");
    }
}

void ThemeEditorDialog::onResetTheme() {
    if (QMessageBox::question(this, "Reset Theme",
        "Reset all changes to the base theme?",
        QMessageBox::Yes | QMessageBox::No) == QMessageBox::Yes) {
        
        QString baseTheme = baseThemeCombo_->currentText().toLower();
        ThemeManager::instance().loadTheme(baseTheme);
        
        loadCurrentTheme();
    }
}

void ThemeEditorDialog::onApplyTheme() {
    applyThemeChanges();
    emit themeApplied();
}

void ThemeEditorDialog::onThemeNameChanged(const QString& name) {
    setHasChanges(true);
    updateWindowTitle();
}

void ThemeEditorDialog::setHasChanges(bool hasChanges) {
    hasChanges_ = hasChanges;
    updateWindowTitle();
    
    // Update action states
    saveAction_->setEnabled(hasChanges);
    applyAction_->setEnabled(hasChanges);
}

void ThemeEditorDialog::updateWindowTitle() {
    QString title = "Theme Editor";
    auto info = ThemeManager::instance().getCurrentThemeInfo();
    if (!info.name.isEmpty()) {
        title += " - " + info.displayName;
    }
    if (hasChanges_ || info.isModified) {
        title += " *";
    }
    setWindowTitle(title);
}

void ThemeEditorDialog::updateStatusBar() {
    if (!statusBar_) return;
    
    auto info = ThemeManager::instance().getCurrentThemeInfo();
    QString status = QString("Theme: %1").arg(info.displayName);
    
    if (info.isBuiltIn) {
        status += " [Built-in - Save As to create custom]";
    } else if (!info.filePath.isEmpty()) {
        // Show abbreviated path to keep status bar clean
        QFileInfo fileInfo(info.filePath);
        status += QString(" [%1]").arg(fileInfo.fileName());
    } else {
        status += " [Unsaved]";
    }
    
    if (info.isModified || hasChanges_) {
        status += " *Modified*";
    }
    
    statusBar_->showMessage(status);
}

QString ThemeEditorDialog::generateThemeName() const {
    return QString("Custom Theme %1").arg(
        QDateTime::currentDateTime().toString("yyyy-MM-dd"));
}


void ThemeEditorDialog::applyThemeChanges() {
    auto& tm = ThemeManager::instance();
    
    // Apply colors
    for (const auto& [name, picker] : colorPickers_) {
        if (picker) {
            tm.setColor(name, picker->color());
        }
    }
    
    // Apply typography
    Typography typography = tm.typography();
    typography.body = baseFontCombo_->currentFont();
    typography.code = codeFontCombo_->currentFont();
    
    // Apply font sizes
    if (fontSizeSpins_.count("heading1")) {
        typography.heading1.setPointSize(fontSizeSpins_["heading1"]->value());
    }
    if (fontSizeSpins_.count("heading2")) {
        typography.heading2.setPointSize(fontSizeSpins_["heading2"]->value());
    }
    if (fontSizeSpins_.count("heading3")) {
        typography.heading3.setPointSize(fontSizeSpins_["heading3"]->value());
    }
    if (fontSizeSpins_.count("body")) {
        typography.body.setPointSize(fontSizeSpins_["body"]->value());
    }
    if (fontSizeSpins_.count("bodySmall")) {
        typography.bodySmall.setPointSize(fontSizeSpins_["bodySmall"]->value());
    }
    if (fontSizeSpins_.count("code")) {
        typography.code.setPointSize(fontSizeSpins_["code"]->value());
    }
    if (fontSizeSpins_.count("caption")) {
        typography.caption.setPointSize(fontSizeSpins_["caption"]->value());
    }
    
    tm.setTypography(typography);
    tm.setFontScale(fontScaleSlider_->value() / 100.0);
    
    // Apply component styles
    tm.setDensityMode(densityCombo_->currentIndex());
    
    // Apply animation settings from animationConfig_
    if (animationConfig_) {
        // Animation config will handle its own apply
    }
    
    // Apply effects settings from effectsConfig_
    if (effectsConfig_) {
        // Effects config will handle its own apply
    }
    
    // Apply chart settings from chartConfig_
    if (chartConfig_) {
        tm.setChartStyle(chartConfig_->selectedStyle());
    }
    
    // The theme changes will be applied automatically through signals
    // emitted by ThemeManager when colors/fonts/etc are changed
    
    setHasChanges(false);
}

void ThemeEditorDialog::loadTheme(const QString& themeName) {
    auto& tm = ThemeManager::instance();
    
    // Load the theme
    tm.loadTheme(themeName);
    
    // Reload all values in the editor
    loadCurrentTheme();
    
    updateWindowTitle();
}

void ThemeEditorDialog::showColorPreview(const QString& colorName) {
    if (!previewPopup_) {
        previewPopup_ = new QWidget(this, Qt::Popup | Qt::FramelessWindowHint);
        const auto& colors = ThemeManager::instance().colors();
        previewPopup_->setStyleSheet(QString("background-color: %1; border: 1px solid %2;")
                                    .arg(colors.surface.name())
                                    .arg(colors.border.name()));
        auto* layout = new QVBoxLayout(previewPopup_);
        layout->setContentsMargins(10, 10, 10, 10);
    }
    
    // Clear existing content
    QLayoutItem* item;
    while ((item = previewPopup_->layout()->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    
    auto* layout = static_cast<QVBoxLayout*>(previewPopup_->layout());
    
    // Add title
    auto* titleLabel = new QLabel(QString("Color: %1").arg(colorName));
    titleLabel->setStyleSheet("font-weight: bold;");
    layout->addWidget(titleLabel);
    
    // Add color swatch
    auto* colorWidget = new QWidget();
    colorWidget->setFixedSize(200, 100);
    QColor color = ThemeManager::instance().color(colorName);
    colorWidget->setStyleSheet(QString("background-color: %1; border-radius: 8px;").arg(color.name()));
    layout->addWidget(colorWidget);
    
    // Add color info
    auto* infoLabel = new QLabel(QString("Hex: %1\nRGB: %2, %3, %4")
                                .arg(color.name())
                                .arg(color.red())
                                .arg(color.green())
                                .arg(color.blue()));
    layout->addWidget(infoLabel);
    
    // Add usage examples
    auto* usageLabel = new QLabel("Usage Examples:");
    usageLabel->setStyleSheet("font-weight: bold; margin-top: 10px;");
    layout->addWidget(usageLabel);
    
    // Show different usage contexts
    if (colorName.contains("primary")) {
        auto* button = new QPushButton("Primary Button");
        button->setStyleSheet(QString("background-color: %1; color: white; padding: 8px 16px; border-radius: 4px;").arg(color.name()));
        layout->addWidget(button);
    } else if (colorName.contains("text")) {
        auto* textLabel = new QLabel("Sample text in this color");
        textLabel->setStyleSheet(QString("color: %1; padding: 8px;").arg(color.name()));
        layout->addWidget(textLabel);
    } else if (colorName.contains("error") || colorName.contains("warning") || colorName.contains("success")) {
        auto* alertWidget = new QWidget();
        alertWidget->setStyleSheet(QString("background-color: %1; padding: 8px; border-radius: 4px;").arg(color.name()));
        auto* alertLayout = new QHBoxLayout(alertWidget);
        alertLayout->addWidget(new QLabel(QString("This is a %1 message").arg(colorName)));
        layout->addWidget(alertWidget);
    }
    
    // Position and show popup
    QPoint globalPos = QCursor::pos();
    previewPopup_->adjustSize();
    previewPopup_->move(globalPos + QPoint(10, 10));
    previewPopup_->show();
    
    // Auto-hide after 3 seconds
    QTimer::singleShot(3000, previewPopup_, &QWidget::hide);
}

void ThemeEditorDialog::showComponentPreview(const QString& componentName) {
    if (!previewPopup_) {
        previewPopup_ = new QWidget(this, Qt::Popup | Qt::FramelessWindowHint);
        const auto& colors = ThemeManager::instance().colors();
        previewPopup_->setStyleSheet(QString("background-color: %1; border: 1px solid %2;")
                                    .arg(colors.surface.name())
                                    .arg(colors.border.name()));
        auto* layout = new QVBoxLayout(previewPopup_);
        layout->setContentsMargins(10, 10, 10, 10);
    }
    
    // Clear existing content
    QLayoutItem* item;
    while ((item = previewPopup_->layout()->takeAt(0)) != nullptr) {
        delete item->widget();
        delete item;
    }
    
    auto* layout = static_cast<QVBoxLayout*>(previewPopup_->layout());
    
    // Add title
    auto* titleLabel = new QLabel(QString("Component: %1").arg(componentName));
    titleLabel->setStyleSheet("font-weight: bold;");
    layout->addWidget(titleLabel);
    
    // Add component preview based on type
    if (componentName == "button") {
        layout->addWidget(new QLabel("Button Variations:"));
        
        auto* primaryBtn = new QPushButton("Primary Button");
        primaryBtn->setProperty("primary", true);
        layout->addWidget(primaryBtn);
        
        auto* secondaryBtn = new QPushButton("Secondary Button");
        layout->addWidget(secondaryBtn);
        
        auto* disabledBtn = new QPushButton("Disabled Button");
        disabledBtn->setEnabled(false);
        layout->addWidget(disabledBtn);
    } else if (componentName == "input") {
        layout->addWidget(new QLabel("Input Variations:"));
        
        auto* textInput = new QLineEdit("Text input");
        layout->addWidget(textInput);
        
        auto* disabledInput = new QLineEdit("Disabled input");
        disabledInput->setEnabled(false);
        layout->addWidget(disabledInput);
        
        auto* combo = new QComboBox();
        combo->addItems({"Option 1", "Option 2", "Option 3"});
        layout->addWidget(combo);
    } else if (componentName == "card") {
        auto* card = new QFrame();
        card->setFrameStyle(QFrame::StyledPanel);
        const auto& colors = ThemeManager::instance().colors();
        card->setStyleSheet(QString("QFrame { background-color: %1; border: 1px solid %2; border-radius: 8px; padding: 16px; }")
                           .arg(colors.surface.name())
                           .arg(colors.border.name()));
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->addWidget(new QLabel("Card Title"));
        cardLayout->addWidget(new QLabel("Card content goes here..."));
        layout->addWidget(card);
    }
    
    // Position and show popup
    QPoint globalPos = QCursor::pos();
    previewPopup_->adjustSize();
    previewPopup_->move(globalPos + QPoint(10, 10));
    previewPopup_->show();
    
    // Auto-hide after 3 seconds
    QTimer::singleShot(3000, previewPopup_, &QWidget::hide);
}

void ThemeEditorDialog::onTemplateSelected(ThemeTemplates::Template tmpl) {
    if (hasUnsavedChanges()) {
        int ret = QMessageBox::question(this, "Unsaved Changes",
            "You have unsaved changes. Do you want to save them first?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        
        if (ret == QMessageBox::Save) {
            onSaveTheme();
        } else if (ret == QMessageBox::Cancel) {
            return;
        }
    }
    
    // Apply the template
    ThemeTemplates::applyTemplate(tmpl);
    
    // Update metadata from template
    auto info = ThemeTemplates::getTemplateInfo(tmpl);
    auto& tm = ThemeManager::instance();
    auto currentInfo = tm.getCurrentThemeInfo();
    currentInfo.metadata = info.metadata;
    currentInfo.metadata.modifiedDate = QDateTime::currentDateTime();
    tm.setCurrentThemeMetadata(currentInfo.metadata);
    
    // Reload the theme in the editor
    loadCurrentTheme();
    
    // Mark as changed
    setHasChanges(true);
    
    QMessageBox::information(this, "Template Applied",
        QString("The '%1' template has been applied. Remember to save your theme.")
        .arg(info.name));
}

void ThemeEditorDialog::onCreateFromTemplate(ThemeTemplates::Template tmpl, const QString& name) {
    if (hasUnsavedChanges()) {
        int ret = QMessageBox::question(this, "Unsaved Changes",
            "You have unsaved changes. Do you want to save them first?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        
        if (ret == QMessageBox::Save) {
            onSaveTheme();
        } else if (ret == QMessageBox::Cancel) {
            return;
        }
    }
    
    // Create theme from template
    ThemeTemplates::createThemeFromTemplate(tmpl, name);
    
    // Load the new theme
    loadTheme(name);
    
    QMessageBox::information(this, "Theme Created",
        QString("Theme '%1' has been created from the template.").arg(name));
}

void ThemeEditorDialog::createAccessibilityTab() {
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    
    accessibilityPanel_ = new AccessibilityPanel();
    
    // Update colors when they change
    connect(this, &ThemeEditorDialog::themeApplied, [this]() {
        std::map<QString, QColor> colors;
        for (const auto& [name, picker] : colorPickers_) {
            colors[name] = picker->color();
        }
        accessibilityPanel_->updateColors(colors);
    });
    
    // Handle accessibility suggestions
    connect(accessibilityPanel_, &AccessibilityPanel::suggestionMade,
            [this](const QString& colorName, const QColor& suggestedColor) {
        if (colorPickers_.count(colorName)) {
            int ret = QMessageBox::question(this, "Apply Suggestion",
                QString("Change %1 to %2 for better accessibility?")
                    .arg(colorName)
                    .arg(suggestedColor.name()),
                QMessageBox::Yes | QMessageBox::No);
            
            if (ret == QMessageBox::Yes) {
                colorPickers_[colorName]->setColor(suggestedColor);
            }
        }
    });
    
    scrollArea->setWidget(accessibilityPanel_);
    tabWidget_->addTab(scrollArea, "Accessibility");
}

} // namespace llm_re::ui_v2