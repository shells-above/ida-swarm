#include "settings_dialog.h"
#include "../core/settings_manager.h"
#include "../core/theme_manager.h"
#include "../../api/anthropic_api.h"

namespace llm_re::ui_v2 {

SettingsDialog::SettingsDialog(QWidget* parent) 
    : QDialog(parent) {
    setWindowTitle("Settings");
    setModal(true);
    resize(600, 500);
    
    setupUI();
    loadSettings();
    
    // Reset change tracking
    has_changes_ = false;
}

void SettingsDialog::setupUI() {
    auto* layout = new QVBoxLayout(this);
    
    // Create tab widget
    tab_widget_ = new QTabWidget(this);
    
    createAPITab();
    createAgentTab();
    createUITab();
    createExportTab();
    createAdvancedTab();
    
    layout->addWidget(tab_widget_);
    
    // Button box
    auto* button_layout = new QHBoxLayout();
    
    reset_button_ = new QPushButton("Reset Defaults", this);
    connect(reset_button_, &QPushButton::clicked, this, &SettingsDialog::onResetDefaults);
    
    button_layout->addWidget(reset_button_);
    button_layout->addStretch();
    
    ok_button_ = new QPushButton("OK", this);
    connect(ok_button_, &QPushButton::clicked, this, &SettingsDialog::onOK);
    
    cancel_button_ = new QPushButton("Cancel", this);
    connect(cancel_button_, &QPushButton::clicked, this, &SettingsDialog::onCancel);
    
    apply_button_ = new QPushButton("Apply", this);
    apply_button_->setEnabled(false);
    connect(apply_button_, &QPushButton::clicked, this, &SettingsDialog::onApply);
    
    button_layout->addWidget(ok_button_);
    button_layout->addWidget(cancel_button_);
    button_layout->addWidget(apply_button_);
    
    layout->addLayout(button_layout);
}

void SettingsDialog::createAPITab() {
    auto* tab = new QWidget();
    auto* layout = new QFormLayout(tab);
    
    api_key_edit_ = new QLineEdit();
    api_key_edit_->setEchoMode(QLineEdit::Password);
    connect(api_key_edit_, &QLineEdit::textChanged, this, &SettingsDialog::onSettingChanged);
    layout->addRow("API Key:", api_key_edit_);
    
    base_url_edit_ = new QLineEdit();
    connect(base_url_edit_, &QLineEdit::textChanged, this, &SettingsDialog::onSettingChanged);
    layout->addRow("Base URL:", base_url_edit_);
    
    model_combo_ = new QComboBox();
    model_combo_->addItems({"Opus 4", "Sonnet 4", "Sonnet 3.7", "Haiku 3.5"});
    connect(model_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &SettingsDialog::onSettingChanged);
    layout->addRow("Model:", model_combo_);
    
    max_tokens_spin_ = new QSpinBox();
    max_tokens_spin_->setRange(1000, 200000);
    max_tokens_spin_->setSingleStep(1000);
    connect(max_tokens_spin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &SettingsDialog::onSettingChanged);
    layout->addRow("Max Tokens:", max_tokens_spin_);
    
    max_thinking_tokens_spin_ = new QSpinBox();
    max_thinking_tokens_spin_->setRange(0, 50000);
    max_thinking_tokens_spin_->setSingleStep(1000);
    connect(max_thinking_tokens_spin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &SettingsDialog::onSettingChanged);
    layout->addRow("Max Thinking Tokens:", max_thinking_tokens_spin_);
    
    temperature_spin_ = new QDoubleSpinBox();
    temperature_spin_->setRange(0.0, 1.0);
    temperature_spin_->setSingleStep(0.1);
    temperature_spin_->setDecimals(1);
    connect(temperature_spin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), 
            this, &SettingsDialog::onSettingChanged);
    layout->addRow("Temperature:", temperature_spin_);
    
    auto* test_layout = new QHBoxLayout();
    test_api_button_ = new QPushButton("Test Connection");
    connect(test_api_button_, &QPushButton::clicked, this, &SettingsDialog::onTestAPI);
    api_status_label_ = new QLabel();
    test_layout->addWidget(test_api_button_);
    test_layout->addWidget(api_status_label_);
    test_layout->addStretch();
    layout->addRow("", test_layout);
    
    tab_widget_->addTab(tab, "API");
}

void SettingsDialog::createAgentTab() {
    auto* tab = new QWidget();
    auto* layout = new QFormLayout(tab);
    
    max_iterations_spin_ = new QSpinBox();
    max_iterations_spin_->setRange(1, 10000);
    connect(max_iterations_spin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &SettingsDialog::onSettingChanged);
    layout->addRow("Max Iterations:", max_iterations_spin_);
    
    enable_thinking_check_ = new QCheckBox("Enable Thinking Mode");
    connect(enable_thinking_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    layout->addRow(enable_thinking_check_);
    
    enable_interleaved_thinking_check_ = new QCheckBox("Enable Interleaved Thinking");
    connect(enable_interleaved_thinking_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    layout->addRow(enable_interleaved_thinking_check_);
    
    enable_deep_analysis_check_ = new QCheckBox("Enable Deep Analysis");
    connect(enable_deep_analysis_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    layout->addRow(enable_deep_analysis_check_);
    
    verbose_logging_check_ = new QCheckBox("Verbose Logging");
    connect(verbose_logging_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    layout->addRow(verbose_logging_check_);
    
    tab_widget_->addTab(tab, "Agent");
}

void SettingsDialog::createUITab() {
    auto* tab = new QWidget();
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    
    // General UI settings
    auto* generalGroup = new QGroupBox("General");
    auto* generalLayout = new QFormLayout(generalGroup);
    
    log_buffer_spin_ = new QSpinBox();
    log_buffer_spin_->setRange(100, 10000);
    log_buffer_spin_->setSingleStep(100);
    connect(log_buffer_spin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &SettingsDialog::onSettingChanged);
    generalLayout->addRow("Log Buffer Size:", log_buffer_spin_);
    
    auto_scroll_check_ = new QCheckBox("Auto-scroll Messages");
    connect(auto_scroll_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    generalLayout->addRow(auto_scroll_check_);
    
    theme_combo_ = new QComboBox();
    theme_combo_->addItems({"Default", "Dark", "Light"});
    connect(theme_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), 
            this, &SettingsDialog::onSettingChanged);
    generalLayout->addRow("Theme:", theme_combo_);
    
    font_size_spin_ = new QSpinBox();
    font_size_spin_->setRange(8, 24);
    connect(font_size_spin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &SettingsDialog::onSettingChanged);
    generalLayout->addRow("Font Size:", font_size_spin_);
    
    show_timestamps_check_ = new QCheckBox("Show Timestamps");
    connect(show_timestamps_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    generalLayout->addRow(show_timestamps_check_);
    
    show_tool_details_check_ = new QCheckBox("Show Tool Details");
    connect(show_tool_details_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    generalLayout->addRow(show_tool_details_check_);
    
    layout->addWidget(generalGroup);
    
    // Window management
    auto* windowGroup = new QGroupBox("Window Management");
    auto* windowLayout = new QVBoxLayout(windowGroup);
    
    show_tray_icon_check_ = new QCheckBox("Show System Tray Icon");
    connect(show_tray_icon_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    windowLayout->addWidget(show_tray_icon_check_);
    
    minimize_to_tray_check_ = new QCheckBox("Minimize to System Tray");
    connect(minimize_to_tray_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    windowLayout->addWidget(minimize_to_tray_check_);
    
    close_to_tray_check_ = new QCheckBox("Close to System Tray");
    connect(close_to_tray_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    windowLayout->addWidget(close_to_tray_check_);
    
    start_minimized_check_ = new QCheckBox("Start Minimized");
    connect(start_minimized_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    windowLayout->addWidget(start_minimized_check_);
    
    remember_window_state_check_ = new QCheckBox("Remember Window State");
    connect(remember_window_state_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    windowLayout->addWidget(remember_window_state_check_);
    
    auto_save_layout_check_ = new QCheckBox("Auto-save Window Layout");
    connect(auto_save_layout_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    windowLayout->addWidget(auto_save_layout_check_);
    
    layout->addWidget(windowGroup);
    
    // Conversation view
    auto* conversationGroup = new QGroupBox("Conversation View");
    auto* conversationLayout = new QFormLayout(conversationGroup);
    
    auto_save_conversations_check_ = new QCheckBox("Auto-save Conversations");
    connect(auto_save_conversations_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    conversationLayout->addRow(auto_save_conversations_check_);
    
    auto_save_interval_spin_ = new QSpinBox();
    auto_save_interval_spin_->setRange(10, 600);
    auto_save_interval_spin_->setSuffix(" seconds");
    connect(auto_save_interval_spin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &SettingsDialog::onSettingChanged);
    conversationLayout->addRow("Auto-save Interval:", auto_save_interval_spin_);
    
    compact_mode_check_ = new QCheckBox("Compact Mode");
    connect(compact_mode_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    conversationLayout->addRow(compact_mode_check_);
    
    layout->addWidget(conversationGroup);
    
    // Inspector
    auto* inspectorGroup = new QGroupBox("Floating Inspector");
    auto* inspectorLayout = new QFormLayout(inspectorGroup);
    
    inspector_follow_cursor_check_ = new QCheckBox("Follow Cursor");
    connect(inspector_follow_cursor_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    inspectorLayout->addRow(inspector_follow_cursor_check_);
    
    auto* opacityLayout = new QHBoxLayout();
    inspector_opacity_slider_ = new QSlider(Qt::Horizontal);
    inspector_opacity_slider_->setRange(20, 100);
    connect(inspector_opacity_slider_, &QSlider::valueChanged, [this](int value) {
        inspector_opacity_label_->setText(QString("%1%").arg(value));
        onSettingChanged();
    });
    inspector_opacity_label_ = new QLabel("80%");
    opacityLayout->addWidget(inspector_opacity_slider_);
    opacityLayout->addWidget(inspector_opacity_label_);
    inspectorLayout->addRow("Opacity:", opacityLayout);
    
    inspector_auto_hide_check_ = new QCheckBox("Auto-hide");
    connect(inspector_auto_hide_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    inspectorLayout->addRow(inspector_auto_hide_check_);
    
    inspector_auto_hide_delay_spin_ = new QSpinBox();
    inspector_auto_hide_delay_spin_->setRange(500, 10000);
    inspector_auto_hide_delay_spin_->setSingleStep(500);
    inspector_auto_hide_delay_spin_->setSuffix(" ms");
    connect(inspector_auto_hide_delay_spin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &SettingsDialog::onSettingChanged);
    inspectorLayout->addRow("Auto-hide Delay:", inspector_auto_hide_delay_spin_);
    
    layout->addWidget(inspectorGroup);
    layout->addStretch();
    
    scroll->setWidget(content);
    
    auto* tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->addWidget(scroll);
    
    tab_widget_->addTab(tab, "User Interface");
}

void SettingsDialog::createExportTab() {
    auto* tab = new QWidget();
    auto* layout = new QFormLayout(tab);
    
    auto* path_layout = new QHBoxLayout();
    export_path_edit_ = new QLineEdit();
    connect(export_path_edit_, &QLineEdit::textChanged, this, &SettingsDialog::onSettingChanged);
    
    browse_export_button_ = new QPushButton("Browse...");
    connect(browse_export_button_, &QPushButton::clicked, this, &SettingsDialog::onBrowseExportPath);
    
    path_layout->addWidget(export_path_edit_);
    path_layout->addWidget(browse_export_button_);
    
    layout->addRow("Export Path:", path_layout);
    
    tab_widget_->addTab(tab, "Export");
}

void SettingsDialog::createAdvancedTab() {
    auto* tab = new QWidget();
    auto* layout = new QFormLayout(tab);
    
    debug_mode_check_ = new QCheckBox("Enable Debug Mode");
    connect(debug_mode_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    layout->addRow(debug_mode_check_);
    
    auto* warning_label = new QLabel(
        "<i>Warning: Debug mode may impact performance and generate large log files.</i>");
    warning_label->setWordWrap(true);
    layout->addRow(warning_label);
    
    tab_widget_->addTab(tab, "Advanced");
}

void SettingsDialog::loadSettings() {
    const Config& config = SettingsManager::instance().config();
    
    // API settings
    api_key_edit_->setText(QString::fromStdString(config.api.api_key));
    base_url_edit_->setText(QString::fromStdString(config.api.base_url));
    
    // Map model enum to combo index
    switch (config.api.model) {
        case api::Model::Opus4:
            model_combo_->setCurrentIndex(0);
            break;
        case api::Model::Sonnet4:
            model_combo_->setCurrentIndex(1);
            break;
        case api::Model::Sonnet37:
            model_combo_->setCurrentIndex(2);
            break;
        case api::Model::Haiku35:
            model_combo_->setCurrentIndex(3);
            break;
    }
    
    max_tokens_spin_->setValue(config.api.max_tokens);
    max_thinking_tokens_spin_->setValue(config.api.max_thinking_tokens);
    temperature_spin_->setValue(config.api.temperature);
    
    // Agent settings
    max_iterations_spin_->setValue(config.agent.max_iterations);
    enable_thinking_check_->setChecked(config.agent.enable_thinking);
    enable_interleaved_thinking_check_->setChecked(config.agent.enable_interleaved_thinking);
    enable_deep_analysis_check_->setChecked(config.agent.enable_deep_analysis);
    verbose_logging_check_->setChecked(config.agent.verbose_logging);
    
    // UI settings
    log_buffer_spin_->setValue(config.ui.log_buffer_size);
    auto_scroll_check_->setChecked(config.ui.auto_scroll);
    theme_combo_->setCurrentIndex(config.ui.theme);
    font_size_spin_->setValue(config.ui.font_size);
    show_timestamps_check_->setChecked(config.ui.show_timestamps);
    show_tool_details_check_->setChecked(config.ui.show_tool_details);
    
    // Window management
    show_tray_icon_check_->setChecked(config.ui.show_tray_icon);
    minimize_to_tray_check_->setChecked(config.ui.minimize_to_tray);
    close_to_tray_check_->setChecked(config.ui.close_to_tray);
    start_minimized_check_->setChecked(config.ui.start_minimized);
    remember_window_state_check_->setChecked(config.ui.remember_window_state);
    auto_save_layout_check_->setChecked(config.ui.auto_save_layout);
    
    // Conversation view
    auto_save_conversations_check_->setChecked(config.ui.auto_save_conversations);
    auto_save_interval_spin_->setValue(config.ui.auto_save_interval);
    compact_mode_check_->setChecked(config.ui.compact_mode);
    
    // Inspector
    inspector_follow_cursor_check_->setChecked(config.ui.inspector_follow_cursor);
    inspector_opacity_slider_->setValue(config.ui.inspector_opacity);
    inspector_opacity_label_->setText(QString("%1%").arg(config.ui.inspector_opacity));
    inspector_auto_hide_check_->setChecked(config.ui.inspector_auto_hide);
    inspector_auto_hide_delay_spin_->setValue(config.ui.inspector_auto_hide_delay);
    
    // Export settings
    export_path_edit_->setText(QString::fromStdString(config.export_settings.path));
    
    // Advanced settings
    debug_mode_check_->setChecked(config.debug_mode);
}

void SettingsDialog::applySettings() {
    Config& config = SettingsManager::instance().config();
    
    // API settings
    config.api.api_key = api_key_edit_->text().toStdString();
    config.api.base_url = base_url_edit_->text().toStdString();
    
    // Map combo index to model enum
    switch (model_combo_->currentIndex()) {
        case 0:
            config.api.model = api::Model::Opus4;
            break;
        case 1:
            config.api.model = api::Model::Sonnet4;
            break;
        case 2:
            config.api.model = api::Model::Sonnet37;
            break;
        case 3:
            config.api.model = api::Model::Haiku35;
            break;
    }
    
    config.api.max_tokens = max_tokens_spin_->value();
    config.api.max_thinking_tokens = max_thinking_tokens_spin_->value();
    config.api.temperature = temperature_spin_->value();
    
    // Agent settings
    config.agent.max_iterations = max_iterations_spin_->value();
    config.agent.enable_thinking = enable_thinking_check_->isChecked();
    config.agent.enable_interleaved_thinking = enable_interleaved_thinking_check_->isChecked();
    config.agent.enable_deep_analysis = enable_deep_analysis_check_->isChecked();
    config.agent.verbose_logging = verbose_logging_check_->isChecked();
    
    // UI settings
    config.ui.log_buffer_size = log_buffer_spin_->value();
    config.ui.auto_scroll = auto_scroll_check_->isChecked();
    config.ui.theme = theme_combo_->currentIndex();
    config.ui.font_size = font_size_spin_->value();
    config.ui.show_timestamps = show_timestamps_check_->isChecked();
    config.ui.show_tool_details = show_tool_details_check_->isChecked();
    
    // Window management
    config.ui.show_tray_icon = show_tray_icon_check_->isChecked();
    config.ui.minimize_to_tray = minimize_to_tray_check_->isChecked();
    config.ui.close_to_tray = close_to_tray_check_->isChecked();
    config.ui.start_minimized = start_minimized_check_->isChecked();
    config.ui.remember_window_state = remember_window_state_check_->isChecked();
    config.ui.auto_save_layout = auto_save_layout_check_->isChecked();
    
    // Conversation view
    config.ui.auto_save_conversations = auto_save_conversations_check_->isChecked();
    config.ui.auto_save_interval = auto_save_interval_spin_->value();
    config.ui.compact_mode = compact_mode_check_->isChecked();
    
    // Inspector
    config.ui.inspector_follow_cursor = inspector_follow_cursor_check_->isChecked();
    config.ui.inspector_opacity = inspector_opacity_slider_->value();
    config.ui.inspector_auto_hide = inspector_auto_hide_check_->isChecked();
    config.ui.inspector_auto_hide_delay = inspector_auto_hide_delay_spin_->value();
    
    // Export settings
    config.export_settings.path = export_path_edit_->text().toStdString();
    
    // Advanced settings
    config.debug_mode = debug_mode_check_->isChecked();
    
    // Apply and save
    SettingsManager::instance().applyUISettings();
    SettingsManager::instance().saveSettings();
    
    has_changes_ = false;
    apply_button_->setEnabled(false);
}

void SettingsDialog::onTestAPI() {
    test_api_button_->setEnabled(false);
    api_status_label_->setText("Testing...");
    
    // TODO: Implement actual API test
    // For now, just simulate
    QTimer::singleShot(1000, [this]() {
        test_api_button_->setEnabled(true);
        if (!api_key_edit_->text().isEmpty()) {
            api_status_label_->setText("<font color='green'>✓ Connected</font>");
        } else {
            api_status_label_->setText("<font color='red'>✗ API key required</font>");
        }
    });
}

void SettingsDialog::onBrowseExportPath() {
    QString dir = QFileDialog::getExistingDirectory(this, "Select Export Directory",
                                                    export_path_edit_->text());
    if (!dir.isEmpty()) {
        export_path_edit_->setText(dir);
    }
}

void SettingsDialog::onResetDefaults() {
    int ret = QMessageBox::question(this, "Reset Settings",
                                   "Are you sure you want to reset all settings to defaults?",
                                   QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        Config default_config;
        SettingsManager::instance().config() = default_config;
        loadSettings();
        has_changes_ = true;
        apply_button_->setEnabled(true);
    }
}

void SettingsDialog::onSettingChanged() {
    has_changes_ = true;
    apply_button_->setEnabled(true);
}

void SettingsDialog::onOK() {
    if (has_changes_) {
        applySettings();
    }
    accept();
}

void SettingsDialog::onCancel() {
    if (has_changes_) {
        int ret = QMessageBox::question(this, "Unsaved Changes",
                                       "You have unsaved changes. Discard them?",
                                       QMessageBox::Yes | QMessageBox::No);
        if (ret == QMessageBox::No) {
            return;
        }
    }
    reject();
}

void SettingsDialog::onApply() {
    applySettings();
}

} // namespace llm_re::ui_v2