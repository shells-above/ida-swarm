#include "settings_dialog.h"
#include "../core/settings_manager.h"
#include "../core/theme_manager.h"
#include "core/oauth_manager.h"
#include "core/oauth_authorizer.h"
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
    createGraderTab();
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
    
    // OAuth settings section
    auto* auth_group = new QGroupBox("Authentication");
    auto* auth_layout = new QFormLayout(auth_group);
    
    use_oauth_check_ = new QCheckBox("Use OAuth (from claude-cpp-sdk)");
    use_oauth_check_->setToolTip("Use OAuth credentials from claude-cpp-sdk instead of API key");
    connect(use_oauth_check_, &QCheckBox::toggled, this, [this](bool checked) {
        oauth_config_dir_edit_->setEnabled(checked);
        authorize_button_->setEnabled(checked);
        api_key_edit_->setEnabled(!checked);
        onSettingChanged();
        if (checked) {
            checkOAuthStatus();
        }
    });
    auth_layout->addRow(use_oauth_check_);
    
    oauth_config_dir_edit_ = new QLineEdit();
    oauth_config_dir_edit_->setPlaceholderText("~/.claude_cpp_sdk");
    connect(oauth_config_dir_edit_, &QLineEdit::textChanged, this, &SettingsDialog::onSettingChanged);
    auth_layout->addRow("OAuth Config Dir:", oauth_config_dir_edit_);
    
    oauth_status_label_ = new QLabel();
    auth_layout->addRow("OAuth Status:", oauth_status_label_);
    
    authorize_button_ = new QPushButton("Authorize Account");
    authorize_button_->setEnabled(false);  // Will be enabled when OAuth is selected
    connect(authorize_button_, &QPushButton::clicked, this, &SettingsDialog::onAuthorize);
    auth_layout->addRow("", authorize_button_);
    
    api_key_edit_ = new QLineEdit();
    api_key_edit_->setEchoMode(QLineEdit::Password);
    connect(api_key_edit_, &QLineEdit::textChanged, this, &SettingsDialog::onSettingChanged);
    auth_layout->addRow("API Key:", api_key_edit_);
    
    layout->addRow(auth_group);
    
    // API settings section
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

void SettingsDialog::createGraderTab() {
    auto* tab = new QWidget();
    auto* layout = new QFormLayout(tab);
    
    // Grader model selection
    grader_model_combo_ = new QComboBox();
    grader_model_combo_->addItems({"Opus 4.1", "Sonnet 4", "Sonnet 3.7", "Haiku 3.5"});
    grader_model_combo_->setToolTip("Model to use for grading agent work");
    connect(grader_model_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &SettingsDialog::onSettingChanged);
    layout->addRow("Grader Model:", grader_model_combo_);
    
    // Max tokens
    grader_max_tokens_spin_ = new QSpinBox();
    grader_max_tokens_spin_->setRange(1, 200000);
    grader_max_tokens_spin_->setSingleStep(1024);
    grader_max_tokens_spin_->setToolTip("Maximum tokens for grader response");
    connect(grader_max_tokens_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::onSettingChanged);
    layout->addRow("Max Response Tokens:", grader_max_tokens_spin_);
    
    // Max thinking tokens
    grader_max_thinking_tokens_spin_ = new QSpinBox();
    grader_max_thinking_tokens_spin_->setRange(1024, 65536);
    grader_max_thinking_tokens_spin_->setSingleStep(1024);
    grader_max_thinking_tokens_spin_->setToolTip("Maximum thinking tokens for grader evaluation");
    connect(grader_max_thinking_tokens_spin_, QOverload<int>::of(&QSpinBox::valueChanged), this, &SettingsDialog::onSettingChanged);
    layout->addRow("Max Thinking Tokens:", grader_max_thinking_tokens_spin_);
    
    // Info label
    auto* info_label = new QLabel(
        "<i>The grader evaluates whether the agent's analysis is perfect and complete. "
        "It demands evidence for all claims and sends questions back if anything is incomplete.</i>"
    );
    info_label->setWordWrap(true);
    layout->addRow(info_label);
    
    tab_widget_->addTab(tab, "Grader");
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
    
    // OAuth/API settings
    use_oauth_check_->setChecked(config.api.use_oauth);
    oauth_config_dir_edit_->setText(QString::fromStdString(config.api.oauth_config_dir));
    oauth_config_dir_edit_->setEnabled(config.api.use_oauth);
    api_key_edit_->setText(QString::fromStdString(config.api.api_key));
    api_key_edit_->setEnabled(!config.api.use_oauth);
    base_url_edit_->setText(QString::fromStdString(config.api.base_url));
    
    if (config.api.use_oauth) {
        checkOAuthStatus();
    }
    
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
    
    // Grader settings
    switch (config.grader.model) {
        case api::Model::Opus41:
            grader_model_combo_->setCurrentIndex(0);
            break;
        case api::Model::Sonnet4:
            grader_model_combo_->setCurrentIndex(1);
            break;
        case api::Model::Sonnet37:
            grader_model_combo_->setCurrentIndex(2);
            break;
        case api::Model::Haiku35:
            grader_model_combo_->setCurrentIndex(3);
            break;
    }
    grader_max_tokens_spin_->setValue(config.grader.max_tokens);
    grader_max_thinking_tokens_spin_->setValue(config.grader.max_thinking_tokens);

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
    
    // OAuth/API settings
    config.api.use_oauth = use_oauth_check_->isChecked();
    config.api.oauth_config_dir = oauth_config_dir_edit_->text().toStdString();
    config.api.api_key = api_key_edit_->text().toStdString();
    config.api.base_url = base_url_edit_->text().toStdString();
    
    // Update auth method based on checkbox
    if (config.api.use_oauth) {
        config.api.auth_method = api::AuthMethod::OAUTH;
    } else {
        config.api.auth_method = api::AuthMethod::API_KEY;
    }
    
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
    
    // Grader settings
    switch (grader_model_combo_->currentIndex()) {
        case 0:
            config.grader.model = api::Model::Opus41;
            break;
        case 1:
            config.grader.model = api::Model::Sonnet4;
            break;
        case 2:
            config.grader.model = api::Model::Sonnet37;
            break;
        case 3:
            config.grader.model = api::Model::Haiku35;
            break;
    }
    config.grader.max_tokens = grader_max_tokens_spin_->value();
    config.grader.max_thinking_tokens = grader_max_thinking_tokens_spin_->value();

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
    
    // Check if OAuth is enabled
    bool useOAuth = use_oauth_check_->isChecked();
    
    if (useOAuth) {
        // Test OAuth connection
        QThread* thread = QThread::create([this]() {
            bool valid = validateOAuth();
            
            // update ui in main thread
            QMetaObject::invokeMethod(this, [this, valid]() {
                test_api_button_->setEnabled(true);
                if (valid) {
                    api_status_label_->setText("<font color='green'>✓ Connected - OAuth authentication is valid</font>");
                } else {
                    api_status_label_->setText("<font color='red'>✗ OAuth authentication failed or connection error</font>");
                }
            }, Qt::QueuedConnection);
        });
        
        connect(thread, &QThread::finished, thread, &QThread::deleteLater);
        thread->start();
    } else {
        // Test API key
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

bool SettingsDialog::validateOAuth() {
    try {
        // Load OAuth credentials
        QString configDir = oauth_config_dir_edit_->text();
        if (configDir.isEmpty()) {
            configDir = "~/.claude_cpp_sdk";
        }
        
        OAuthManager oauth_mgr(configDir.toStdString());
        std::optional<api::OAuthCredentials> oauth_creds = oauth_mgr.get_credentials();
        
        if (!oauth_creds) {
            msg("LLM RE: Failed to load OAuth credentials for validation\n");
            return false;
        }
        
        // Create client with OAuth credentials
        api::AnthropicClient client(*oauth_creds, base_url_edit_->text().toStdString());
        
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
            msg("LLM RE: OAuth validation error: %s\n", error.c_str());
            
            if (error.find("401") != std::string::npos ||
                error.find("unauthorized") != std::string::npos) {
                return false;
            }
        }
    } catch (const std::exception& e) {
        msg("LLM RE: OAuth validation exception: %s\n", e.what());
    }
    
    return false;
}

void SettingsDialog::checkOAuthStatus() {
    QString configDir = oauth_config_dir_edit_->text();
    if (configDir.isEmpty()) {
        configDir = "~/.claude_cpp_sdk";
    }
    
    OAuthManager oauth_mgr(configDir.toStdString());
    
    if (!oauth_mgr.has_credentials()) {
        oauth_status_label_->setText("<font color='red'>✗ No credentials found</font>");
        return;
    }
    
    auto creds = oauth_mgr.get_credentials();
    if (!creds) {
        oauth_status_label_->setText("<font color='red'>✗ Failed to read credentials</font>");
        return;
    }
    
    if (creds->is_expired()) {
        oauth_status_label_->setText("<font color='orange'>⚠ Token expired (may auto-refresh)</font>");
    } else {
        // Calculate time until expiry
        auto now = std::chrono::system_clock::now();
        auto now_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        auto seconds_remaining = creds->expires_at - now_timestamp;
        auto hours_remaining = seconds_remaining / 3600;
        
        if (hours_remaining > 24) {
            auto days_remaining = hours_remaining / 24;
            oauth_status_label_->setText(QString("<font color='green'>✓ Valid (%1 days remaining)</font>").arg(days_remaining));
        } else {
            oauth_status_label_->setText(QString("<font color='green'>✓ Valid (%1 hours remaining)</font>").arg(hours_remaining));
        }
    }
}

void SettingsDialog::onAuthorize() {
    // Disable button while authorizing
    authorize_button_->setEnabled(false);
    authorize_button_->setText("Authorizing...");
    oauth_status_label_->setText("<font color='blue'>⟳ Authorizing...</font>");
    
    // Run authorization in background thread
    QThread* thread = QThread::create([this]() {
        OAuthAuthorizer authorizer;
        bool success = authorizer.authorize();
        std::string error = authorizer.getLastError();
        
        // Update UI in main thread
        QMetaObject::invokeMethod(this, [this, success, error]() {
            authorize_button_->setEnabled(true);
            authorize_button_->setText("Authorize Account");
            
            if (success) {
                QMessageBox::information(this, "Authorization Successful",
                                        "Your account has been authorized successfully!");
                checkOAuthStatus();
                onSettingChanged();
            } else {
                QMessageBox::warning(this, "Authorization Failed",
                                     QString("Failed to authorize: %1").arg(QString::fromStdString(error)));
                oauth_status_label_->setText("<font color='red'>✗ Authorization failed</font>");
            }
        }, Qt::QueuedConnection);
    });
    
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);
    thread->start();
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