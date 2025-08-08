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
    
    // CRITICAL: Prevent Qt from using the application style for background
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    
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
    model_combo_->addItems({"Opus 4.1", "Sonnet 4", "Sonnet 3.7", "Haiku 3.5"});
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
    connect(theme_combo_, &QComboBox::currentTextChanged, 
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
    
    start_minimized_check_ = new QCheckBox("Start Minimized");
    connect(start_minimized_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    windowLayout->addWidget(start_minimized_check_);
    
    remember_window_state_check_ = new QCheckBox("Remember Window State");
    connect(remember_window_state_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    windowLayout->addWidget(remember_window_state_check_);
    
    layout->addWidget(windowGroup);
    
    // Conversation view
    auto* conversationGroup = new QGroupBox("Conversation View");
    auto* conversationLayout = new QFormLayout(conversationGroup);
    
    auto_save_conversations_check_ = new QCheckBox("Auto-save Conversations");
    auto_save_conversations_check_->setToolTip("Automatically saves conversation to the current session file.\nOnly works after you've manually saved the session at least once.");
    connect(auto_save_conversations_check_, &QCheckBox::toggled, this, &SettingsDialog::onSettingChanged);
    conversationLayout->addRow(auto_save_conversations_check_);
    
    auto_save_interval_spin_ = new QSpinBox();
    auto_save_interval_spin_->setRange(10, 600);
    auto_save_interval_spin_->setSuffix(" seconds");
    connect(auto_save_interval_spin_, QOverload<int>::of(&QSpinBox::valueChanged), 
            this, &SettingsDialog::onSettingChanged);
    conversationLayout->addRow("Auto-save Interval:", auto_save_interval_spin_);
    
    density_mode_combo_ = new QComboBox();
    density_mode_combo_->addItem("Compact");
    density_mode_combo_->addItem("Cozy");
    density_mode_combo_->addItem("Spacious");
    connect(density_mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onSettingChanged);
    conversationLayout->addRow("Density Mode:", density_mode_combo_);
    
    layout->addWidget(conversationGroup);
    layout->addStretch();
    
    scroll->setWidget(content);
    
    auto* tabLayout = new QVBoxLayout(tab);
    tabLayout->setContentsMargins(0, 0, 0, 0);
    tabLayout->addWidget(scroll);
    
    tab_widget_->addTab(tab, "User Interface");
}

void SettingsDialog::createAdvancedTab() {
    auto* tab = new QWidget();
    auto* layout = new QFormLayout(tab);
    
    // Currently no advanced settings
    auto* placeholder_label = new QLabel(
        "<i>No advanced settings available</i>");
    layout->addRow(placeholder_label);
    
    tab_widget_->addTab(tab, "Advanced");
}

void SettingsDialog::loadSettings() {
    const Config& config = SettingsManager::instance().config();
    
    // API settings
    api_key_edit_->setText(QString::fromStdString(config.api.api_key));
    base_url_edit_->setText(QString::fromStdString(config.api.base_url));
    
    // Map model enum to combo index
    switch (config.api.model) {
        case api::Model::Opus41:
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

    // UI settings
    log_buffer_spin_->setValue(config.ui.log_buffer_size);
    auto_scroll_check_->setChecked(config.ui.auto_scroll);
    theme_combo_->setCurrentText(QString::fromStdString(config.ui.theme_name));
    font_size_spin_->setValue(config.ui.font_size);
    show_timestamps_check_->setChecked(config.ui.show_timestamps);
    show_tool_details_check_->setChecked(config.ui.show_tool_details);
    
    // Window management
    start_minimized_check_->setChecked(config.ui.start_minimized);
    remember_window_state_check_->setChecked(config.ui.remember_window_state);
    
    // Conversation view
    auto_save_conversations_check_->setChecked(config.ui.auto_save_conversations);
    auto_save_interval_spin_->setValue(config.ui.auto_save_interval);
    density_mode_combo_->setCurrentIndex(config.ui.density_mode);
}

void SettingsDialog::applySettings() {
    Config& config = SettingsManager::instance().config();
    
    // API settings
    config.api.api_key = api_key_edit_->text().toStdString();
    config.api.base_url = base_url_edit_->text().toStdString();
    
    // Map combo index to model enum
    switch (model_combo_->currentIndex()) {
        case 0:
            config.api.model = api::Model::Opus41;
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

    // UI settings
    config.ui.log_buffer_size = log_buffer_spin_->value();
    config.ui.auto_scroll = auto_scroll_check_->isChecked();
    config.ui.theme_name = theme_combo_->currentText().toStdString();
    config.ui.font_size = font_size_spin_->value();
    config.ui.show_timestamps = show_timestamps_check_->isChecked();
    config.ui.show_tool_details = show_tool_details_check_->isChecked();
    
    // Window management
    config.ui.start_minimized = start_minimized_check_->isChecked();
    config.ui.remember_window_state = remember_window_state_check_->isChecked();
    
    // Conversation view
    config.ui.auto_save_conversations = auto_save_conversations_check_->isChecked();
    config.ui.auto_save_interval = auto_save_interval_spin_->value();
    config.ui.density_mode = density_mode_combo_->currentIndex();
    
    // Apply and save
    SettingsManager::instance().applyUISettings();
    SettingsManager::instance().saveSettings();
    
    has_changes_ = false;
    apply_button_->setEnabled(false);
}

void SettingsDialog::onTestAPI() {
    test_api_button_->setEnabled(false);
    api_status_label_->setText("Testing...");
    
    QString apiKey = api_key_edit_->text().trimmed();
    if (apiKey.isEmpty()) {
        api_status_label_->setText("<font color='red'>✗ API key required</font>");
        test_api_button_->setEnabled(true);
        return;
    }

    // run request in background thread
    QThread* thread = QThread::create([this, apiKey]() {
        bool valid = validateApiKey(apiKey.toStdString());

        // update ui in main thread
        QMetaObject::invokeMethod(this, [this, valid]() {
            test_api_button_->setEnabled(true);
            if (valid) {
                api_status_label_->setText("<font color='green'>✓ Connected - API key is valid</font>");
            } else {
                api_status_label_->setText("<font color='red'>✗ Invalid API key or connection error</font>");
            }
        }, Qt::QueuedConnection);
    });
    
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
}

bool SettingsDialog::validateApiKey(const std::string& apiKey) {
    try {
        api::AnthropicClient client(apiKey);
        
        api::ChatRequest request;
        request.model = api::Model::Haiku35;
        request.max_tokens = 1;
        request.temperature = 0;
        request.enable_thinking = false;
        
        request.messages.push_back(messages::Message::user_text("Hi"));
        
        api::ChatResponse response = client.send_request(request);
        
        if (response.success) {
            return true;
        } else if (response.error) {
            std::string error = response.error.value();
            
            msg("LLM RE: API validation error: %s\n", error.c_str());
            
            if (error.find("401") != std::string::npos ||
                error.find("authentication") != std::string::npos ||
                error.find("Invalid API Key") != std::string::npos) {
                return false;
            }
            
            return false;  // return not valid to be safe
        }
    } catch (const std::exception& e) {
        msg("LLM RE: API validation exception: %s\n", e.what());
        return false;
    } catch (...) {
        msg("LLM RE: API validation unknown exception\n");
        return false;
    }
    
    return false;
}

void SettingsDialog::onResetDefaults() {
    int ret = QMessageBox::question(this, "Reset Settings",
                                   "Are you sure you want to reset all settings to defaults?",
                                   QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        // Reset settings to defaults
        SettingsManager::instance().config().reset();
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