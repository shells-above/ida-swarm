// Include order is critical! ui_common.h handles the proper ordering
#include "ui_common.h"
#include "preferences_dialog.h"

// Qt implementation headers
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QStackedWidget>
#include <QRadioButton>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QGroupBox>
#include <QTextEdit>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QFileDialog>
#include <QTimer>
#include <QProgressDialog>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTcpSocket>
#include <QDir>
#include <QPointer>

#include "../sdk/client/client.h"
#include "../sdk/auth/oauth_manager.h"
#include "../sdk/messages/types.h"
#include "../core/common_base.h"

#include <fstream>
#include <filesystem>

namespace fs = std::filesystem;

namespace llm_re::ui {

PreferencesDialog::PreferencesDialog(QWidget* parent) 
    : QDialog(parent),
      originalConfig_(Config::instance()),
      currentConfig_(Config::instance()) {
    setupUi();
    loadConfiguration();
    connectSignals();
    
    // Set initial state
    onAuthMethodChanged();
    
    // Setup token status timer (update every 60 seconds)
    tokenStatusTimer_ = new QTimer(this);
    connect(tokenStatusTimer_, &QTimer::timeout, this, &PreferencesDialog::updateTokenStatus);
    tokenStatusTimer_->start(60000); // 60 seconds
    
    // Initial token status update
    updateTokenStatus();
    
    setWindowTitle("Preferences");
    resize(800, 600);
}

void PreferencesDialog::setupUi() {
    auto* layout = new QVBoxLayout(this);
    
    // Create tab widget
    tabWidget_ = new QTabWidget(this);
    
    // Create all tabs
    createApiTab();
    createModelsTab();
    createAgentTab();
    createIrcTab();
    
    layout->addWidget(tabWidget_);
    
    // Create button box
    buttonBox_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply,
        Qt::Horizontal, this);
    
    // Add extra buttons
    resetButton_ = new QPushButton("Reset to Defaults", this);
    exportButton_ = new QPushButton("Export...", this);
    importButton_ = new QPushButton("Import...", this);
    
    auto* extraButtonLayout = new QHBoxLayout();
    extraButtonLayout->addWidget(resetButton_);
    extraButtonLayout->addStretch();
    extraButtonLayout->addWidget(exportButton_);
    extraButtonLayout->addWidget(importButton_);
    
    layout->addLayout(extraButtonLayout);
    layout->addWidget(buttonBox_);
}

void PreferencesDialog::createApiTab() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    
    // Authentication method selector - just radio buttons, no group box
    auto* authSelectorWidget = new QWidget(widget);
    auto* authSelectorLayout = new QHBoxLayout(authSelectorWidget);
    authSelectorLayout->setContentsMargins(0, 0, 0, 10);
    
    auto* authLabel = new QLabel("Authentication Method:", authSelectorWidget);
    apiKeyRadio_ = new QRadioButton("API Key", authSelectorWidget);
    oauthRadio_ = new QRadioButton("OAuth", authSelectorWidget);
    apiKeyRadio_->setChecked(true);
    
    authSelectorLayout->addWidget(authLabel);
    authSelectorLayout->addSpacing(20);
    authSelectorLayout->addWidget(apiKeyRadio_);
    authSelectorLayout->addSpacing(20);
    authSelectorLayout->addWidget(oauthRadio_);
    authSelectorLayout->addStretch();
    
    // Stacked widget to show only the active authentication method
    auto* authStack = new QStackedWidget(widget);
    
    // === API Key Page ===
    auto* apiKeyPage = new QWidget();
    auto* apiKeyPageLayout = new QVBoxLayout(apiKeyPage);
    
    auto* apiKeyGroup = new QGroupBox("API Key Configuration", apiKeyPage);
    auto* apiKeyLayout = new QFormLayout(apiKeyGroup);
    
    apiKeyEdit_ = new QLineEdit(apiKeyGroup);
    apiKeyEdit_->setEchoMode(QLineEdit::Password);
    apiKeyEdit_->setPlaceholderText("sk-ant-api03-...");
    apiKeyLayout->addRow("API Key:", apiKeyEdit_);
    
    // Add helpful text for API key
    auto* apiKeyHelp = new QLabel("Enter your Anthropic API key. You can obtain one from console.anthropic.com", apiKeyGroup);
    apiKeyHelp->setWordWrap(true);
    apiKeyHelp->setStyleSheet("QLabel { color: #666666; font-size: 11px; }");
    apiKeyLayout->addRow("", apiKeyHelp);
    
    apiKeyPageLayout->addWidget(apiKeyGroup);
    apiKeyPageLayout->addStretch();
    
    // === OAuth Page ===
    auto* oauthPage = new QWidget();
    auto* oauthPageLayout = new QVBoxLayout(oauthPage);
    
    auto* oauthGroup = new QGroupBox("OAuth Configuration", oauthPage);
    auto* oauthLayout = new QFormLayout(oauthGroup);
    
    auto* oauthDirLayout = new QHBoxLayout();
    oauthDirEdit_ = new QLineEdit(oauthGroup);
    oauthDirEdit_->setPlaceholderText("~/.claude_cpp_sdk");
    oauthDirBrowse_ = new QPushButton("Browse...", oauthGroup);
    oauthDirLayout->addWidget(oauthDirEdit_);
    oauthDirLayout->addWidget(oauthDirBrowse_);
    oauthLayout->addRow("Config Directory:", oauthDirLayout);
    
    // Token status - simpler layout
    auto* tokenStatusLayout = new QHBoxLayout();
    tokenExpirationLabel_ = new QLabel("Token Status: Checking...", oauthGroup);
    refreshTokenButton_ = new QPushButton("Refresh Token", oauthGroup);
    refreshTokenButton_->setMaximumWidth(120);
    tokenStatusLayout->addWidget(tokenExpirationLabel_);
    tokenStatusLayout->addWidget(refreshTokenButton_);
    tokenStatusLayout->addStretch();
    oauthLayout->addRow("Status:", tokenStatusLayout);
    
    // Add helpful text for OAuth
    auto* oauthHelp = new QLabel("OAuth tokens are automatically refreshed when needed. Use the button above for manual refresh.", oauthGroup);
    oauthHelp->setWordWrap(true);
    oauthHelp->setStyleSheet("QLabel { color: #666666; font-size: 11px; }");
    oauthLayout->addRow("", oauthHelp);
    
    oauthPageLayout->addWidget(oauthGroup);
    oauthPageLayout->addStretch();
    
    // Add pages to stack
    authStack->addWidget(apiKeyPage);
    authStack->addWidget(oauthPage);
    
    // Connection settings (common to both)
    auto* connectionGroup = new QGroupBox("Connection Settings", widget);
    auto* connectionLayout = new QFormLayout(connectionGroup);
    
    baseUrlEdit_ = new QLineEdit(connectionGroup);
    baseUrlEdit_->setPlaceholderText("https://api.anthropic.com/v1/messages");
    connectionLayout->addRow("Base URL:", baseUrlEdit_);
    
    // Test connection with better layout
    auto* testWidget = new QWidget(widget);
    auto* testLayout = new QHBoxLayout(testWidget);
    testLayout->setContentsMargins(0, 10, 0, 0);
    
    testApiButton_ = new QPushButton("Test Connection", testWidget);
    testApiButton_->setMaximumWidth(150);
    apiStatusLabel_ = new QLabel("", testWidget);
    
    testLayout->addWidget(testApiButton_);
    testLayout->addWidget(apiStatusLabel_);
    testLayout->addStretch();
    
    // Connect radio buttons to switch stacked widget pages
    connect(apiKeyRadio_, &QRadioButton::toggled, [authStack](bool checked) {
        if (checked) authStack->setCurrentIndex(0);
    });
    connect(oauthRadio_, &QRadioButton::toggled, [authStack](bool checked) {
        if (checked) authStack->setCurrentIndex(1);
    });
    
    // Final layout assembly
    layout->addWidget(authSelectorWidget);
    layout->addWidget(authStack);
    layout->addWidget(connectionGroup);
    layout->addWidget(testWidget);
    layout->addStretch();
    
    tabWidget_->addTab(widget, "API");
}

void PreferencesDialog::createModelsTab() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    
    // Agent Model Group
    agentModelGroup_ = new QGroupBox("Agent Model", widget);
    auto* agentLayout = new QFormLayout(agentModelGroup_);
    
    agentModelCombo_ = new QComboBox(agentModelGroup_);
    agentModelCombo_->addItem("Claude Opus 4.1", QVariant::fromValue(static_cast<int>(claude::Model::Opus41)));
    agentModelCombo_->addItem("Claude Sonnet 4", QVariant::fromValue(static_cast<int>(claude::Model::Sonnet4)));
    agentModelCombo_->addItem("Claude Haiku 3.5", QVariant::fromValue(static_cast<int>(claude::Model::Haiku35)));
    agentLayout->addRow("Model:", agentModelCombo_);
    
    agentMaxTokensSpin_ = new QSpinBox(agentModelGroup_);
    agentMaxTokensSpin_->setRange(1, 32000);
    agentMaxTokensSpin_->setSuffix(" tokens");
    agentLayout->addRow("Max Tokens:", agentMaxTokensSpin_);
    
    agentMaxThinkingTokensSpin_ = new QSpinBox(agentModelGroup_);
    agentMaxThinkingTokensSpin_->setRange(0, 30000);
    agentMaxThinkingTokensSpin_->setSuffix(" tokens");
    agentLayout->addRow("Max Thinking Tokens:", agentMaxThinkingTokensSpin_);
    
    // Context limit for agent
    contextLimitSpin_ = new QSpinBox(agentModelGroup_);
    contextLimitSpin_->setRange(1000, 200000);
    contextLimitSpin_->setSuffix(" tokens");
    contextLimitSpin_->setToolTip("Token limit before context consolidation");
    agentLayout->addRow("Context Limit:", contextLimitSpin_);
    
    auto* tempLayout = new QHBoxLayout();
    agentTemperatureSpin_ = new QDoubleSpinBox(agentModelGroup_);
    agentTemperatureSpin_->setRange(0.0, 2.0);
    agentTemperatureSpin_->setSingleStep(0.1);
    agentTemperatureSpin_->setDecimals(1);
    
    agentTemperatureSlider_ = new QSlider(Qt::Horizontal, agentModelGroup_);
    agentTemperatureSlider_->setRange(0, 20);
    agentTemperatureSlider_->setTickPosition(QSlider::TicksBelow);
    agentTemperatureSlider_->setTickInterval(5);
    
    tempLayout->addWidget(agentTemperatureSpin_);
    tempLayout->addWidget(agentTemperatureSlider_);
    agentLayout->addRow("Temperature:", tempLayout);
    
    agentEnableThinkingCheck_ = new QCheckBox("Enable thinking mode", agentModelGroup_);
    agentInterleavedThinkingCheck_ = new QCheckBox("Enable interleaved thinking", agentModelGroup_);
    agentLayout->addRow("", agentEnableThinkingCheck_);
    agentLayout->addRow("", agentInterleavedThinkingCheck_);
    
    // Grader Model Group
    graderModelGroup_ = new QGroupBox("Grader Model", widget);
    auto* graderLayout = new QFormLayout(graderModelGroup_);
    
    graderEnabledCheck_ = new QCheckBox("Enable Grader", graderModelGroup_);
    graderLayout->addRow("", graderEnabledCheck_);
    
    graderModelCombo_ = new QComboBox(graderModelGroup_);
    graderModelCombo_->addItem("Claude Opus 4.1", QVariant::fromValue(static_cast<int>(claude::Model::Opus41)));
    graderModelCombo_->addItem("Claude Sonnet 4", QVariant::fromValue(static_cast<int>(claude::Model::Sonnet4)));
    graderModelCombo_->addItem("Claude Haiku 3.5", QVariant::fromValue(static_cast<int>(claude::Model::Haiku35)));
    graderLayout->addRow("Model:", graderModelCombo_);
    
    graderMaxTokensSpin_ = new QSpinBox(graderModelGroup_);
    graderMaxTokensSpin_->setRange(1, 32000);
    graderMaxTokensSpin_->setSuffix(" tokens");
    graderLayout->addRow("Max Tokens:", graderMaxTokensSpin_);
    
    graderMaxThinkingTokensSpin_ = new QSpinBox(graderModelGroup_);
    graderMaxThinkingTokensSpin_->setRange(0, 30000);
    graderMaxThinkingTokensSpin_->setSuffix(" tokens");
    graderLayout->addRow("Max Thinking Tokens:", graderMaxThinkingTokensSpin_);
    
    graderContextLimitSpin_ = new QSpinBox(graderModelGroup_);
    graderContextLimitSpin_->setRange(1000, 200000);
    graderContextLimitSpin_->setSuffix(" tokens");
    graderLayout->addRow("Context Limit:", graderContextLimitSpin_);
    
    // Orchestrator Model Group
    orchestratorModelGroup_ = new QGroupBox("Orchestrator Model", widget);
    auto* orchLayout = new QFormLayout(orchestratorModelGroup_);
    
    orchestratorModelCombo_ = new QComboBox(orchestratorModelGroup_);
    orchestratorModelCombo_->addItem("Claude Opus 4.1", QVariant::fromValue(static_cast<int>(claude::Model::Opus41)));
    orchestratorModelCombo_->addItem("Claude Sonnet 4", QVariant::fromValue(static_cast<int>(claude::Model::Sonnet4)));
    orchestratorModelCombo_->addItem("Claude Haiku 3.5", QVariant::fromValue(static_cast<int>(claude::Model::Haiku35)));
    orchLayout->addRow("Model:", orchestratorModelCombo_);
    
    orchestratorMaxTokensSpin_ = new QSpinBox(orchestratorModelGroup_);
    orchestratorMaxTokensSpin_->setRange(1, 32000);
    orchestratorMaxTokensSpin_->setSuffix(" tokens");
    orchLayout->addRow("Max Tokens:", orchestratorMaxTokensSpin_);
    
    orchestratorMaxThinkingTokensSpin_ = new QSpinBox(orchestratorModelGroup_);
    orchestratorMaxThinkingTokensSpin_->setRange(0, 30000);
    orchestratorMaxThinkingTokensSpin_->setSuffix(" tokens");
    orchLayout->addRow("Max Thinking Tokens:", orchestratorMaxThinkingTokensSpin_);
    
    auto* orchTempLayout = new QHBoxLayout();
    orchestratorTemperatureSpin_ = new QDoubleSpinBox(orchestratorModelGroup_);
    orchestratorTemperatureSpin_->setRange(0.0, 2.0);
    orchestratorTemperatureSpin_->setSingleStep(0.1);
    orchestratorTemperatureSpin_->setDecimals(1);
    
    orchestratorTemperatureSlider_ = new QSlider(Qt::Horizontal, orchestratorModelGroup_);
    orchestratorTemperatureSlider_->setRange(0, 20);
    orchestratorTemperatureSlider_->setTickPosition(QSlider::TicksBelow);
    orchestratorTemperatureSlider_->setTickInterval(5);
    
    orchTempLayout->addWidget(orchestratorTemperatureSpin_);
    orchTempLayout->addWidget(orchestratorTemperatureSlider_);
    orchLayout->addRow("Temperature:", orchTempLayout);
    
    orchestratorEnableThinkingCheck_ = new QCheckBox("Enable thinking mode", orchestratorModelGroup_);
    orchLayout->addRow("", orchestratorEnableThinkingCheck_);
    
    layout->addWidget(agentModelGroup_);
    layout->addWidget(graderModelGroup_);
    layout->addWidget(orchestratorModelGroup_);
    layout->addStretch();
    
    tabWidget_->addTab(widget, "Models");
}

void PreferencesDialog::createAgentTab() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    
    // Analysis settings
    auto* analysisGroup = new QGroupBox("Analysis Settings", widget);
    auto* analysisLayout = new QFormLayout(analysisGroup);
    
    // Max iterations moved here
    maxIterationsSpin_ = new QSpinBox(analysisGroup);
    maxIterationsSpin_->setRange(1, 1000);
    maxIterationsSpin_->setSuffix(" iterations");
    maxIterationsSpin_->setToolTip("Maximum number of iterations for agent analysis");
    analysisLayout->addRow("Max Iterations:", maxIterationsSpin_);
    
    enableDeepAnalysisCheck_ = new QCheckBox("Enable deep analysis", analysisGroup);
    enableDeepAnalysisCheck_->setToolTip("Enables advanced binary analysis features");
    analysisLayout->addRow("", enableDeepAnalysisCheck_);
    
    enablePythonToolCheck_ = new QCheckBox("Enable Python tool", analysisGroup);
    enablePythonToolCheck_->setToolTip("Allows agent to execute Python code (security risk)");
    analysisLayout->addRow("", enablePythonToolCheck_);
    
    pythonToolWarning_ = new QLabel(
        "<font color='red'>⚠️ Warning: Enabling Python tool allows code execution. "
        "Only enable if you trust the agent's actions.</font>", analysisGroup);
    pythonToolWarning_->setWordWrap(true);
    pythonToolWarning_->setVisible(false);
    analysisLayout->addRow("", pythonToolWarning_);
    
    layout->addWidget(analysisGroup);
    layout->addStretch();
    
    // Connect Python tool warning - just show/hide the red text, no dialog
    connect(enablePythonToolCheck_, &QCheckBox::toggled, [this](bool checked) {
        pythonToolWarning_->setVisible(checked);
    });
    
    tabWidget_->addTab(widget, "Agent");
}

void PreferencesDialog::createIrcTab() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    
    // Server settings
    auto* serverGroup = new QGroupBox("IRC Server", widget);
    auto* serverLayout = new QFormLayout(serverGroup);
    
    ircServerEdit_ = new QLineEdit(serverGroup);
    ircServerEdit_->setPlaceholderText("127.0.0.1");
    serverLayout->addRow("Server Address:", ircServerEdit_);
    
    ircPortSpin_ = new QSpinBox(serverGroup);
    ircPortSpin_->setRange(1, 65535);
    ircPortSpin_->setValue(6667);
    serverLayout->addRow("Port:", ircPortSpin_);
    
    // Channel formats
    auto* formatGroup = new QGroupBox("Channel Formats", widget);
    auto* formatLayout = new QFormLayout(formatGroup);
    
    conflictChannelFormatEdit_ = new QLineEdit(formatGroup);
    conflictChannelFormatEdit_->setPlaceholderText("#conflict_{address}_{type}");
    formatLayout->addRow("Conflict Channel:", conflictChannelFormatEdit_);
    
    // Format help
    ircFormatHelp_ = new QTextEdit(widget);
    ircFormatHelp_->setReadOnly(true);
    ircFormatHelp_->setMaximumHeight(100);
    ircFormatHelp_->setHtml(
        "<b>Channel Format Placeholders:</b><br>"
        "• {address} - Memory address in hex<br>"
        "• {type} - Conflict type (name, comment, etc.)<br>"
        "• {agent1}, {agent2} - Agent IDs<br>"
        "• {timestamp} - Unix timestamp"
    );
    
    // Test connection
    auto* testLayout = new QHBoxLayout();
    testIrcButton_ = new QPushButton("Test IRC Connection", widget);
    ircStatusLabel_ = new QLabel("", widget);
    testLayout->addWidget(testIrcButton_);
    testLayout->addWidget(ircStatusLabel_);
    testLayout->addStretch();
    
    layout->addWidget(serverGroup);
    layout->addWidget(formatGroup);
    layout->addWidget(ircFormatHelp_);
    layout->addLayout(testLayout);
    layout->addStretch();
    
    tabWidget_->addTab(widget, "IRC");
}

void PreferencesDialog::connectSignals() {
    // Button box
    connect(buttonBox_, &QDialogButtonBox::accepted, this, &PreferencesDialog::onAccept);
    connect(buttonBox_, &QDialogButtonBox::rejected, this, &PreferencesDialog::onReject);
    
    QPushButton* applyButton = buttonBox_->button(QDialogButtonBox::Apply);
    if (applyButton) {
        connect(applyButton, &QPushButton::clicked, this, &PreferencesDialog::onApply);
    }
    
    // Extra buttons
    connect(resetButton_, &QPushButton::clicked, this, &PreferencesDialog::onResetDefaults);
    connect(exportButton_, &QPushButton::clicked, this, &PreferencesDialog::onExportConfig);
    connect(importButton_, &QPushButton::clicked, this, &PreferencesDialog::onImportConfig);
    
    // Test buttons
    connect(testApiButton_, &QPushButton::clicked, this, &PreferencesDialog::onTestAPIConnection);
    connect(testIrcButton_, &QPushButton::clicked, this, &PreferencesDialog::onTestIRCConnection);
    
    // OAuth token refresh button
    connect(refreshTokenButton_, &QPushButton::clicked, this, &PreferencesDialog::onRefreshOAuthToken);
    
    // Auth method radio buttons
    connect(apiKeyRadio_, &QRadioButton::toggled, this, &PreferencesDialog::onAuthMethodChanged);
    
    // Temperature sliders
    connect(agentTemperatureSlider_, &QSlider::valueChanged, [this](int value) {
        agentTemperatureSpin_->setValue(value / 10.0);
    });
    connect(agentTemperatureSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double value) {
        agentTemperatureSlider_->setValue(static_cast<int>(value * 10));
    });
    
    connect(orchestratorTemperatureSlider_, &QSlider::valueChanged, [this](int value) {
        orchestratorTemperatureSpin_->setValue(value / 10.0);
    });
    connect(orchestratorTemperatureSpin_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), [this](double value) {
        orchestratorTemperatureSlider_->setValue(static_cast<int>(value * 10));
    });
    
    // Thinking mode checkbox handlers - temperature must be 1.0 when thinking is enabled
    connect(agentEnableThinkingCheck_, &QCheckBox::toggled, [this](bool checked) {
        if (checked) {
            // When thinking is enabled, force temperature to 1.0 and disable controls
            agentTemperatureSpin_->setValue(1.0);
            agentTemperatureSpin_->setEnabled(false);
            agentTemperatureSlider_->setValue(10);
            agentTemperatureSlider_->setEnabled(false);
            agentTemperatureSpin_->setToolTip("Temperature must be 1.0 when thinking is enabled");
        } else {
            // Re-enable temperature controls when thinking is disabled
            agentTemperatureSpin_->setEnabled(true);
            agentTemperatureSlider_->setEnabled(true);
            agentTemperatureSpin_->setToolTip("");
        }
    });
    
    connect(orchestratorEnableThinkingCheck_, &QCheckBox::toggled, [this](bool checked) {
        if (checked) {
            // When thinking is enabled, force temperature to 1.0 and disable controls
            orchestratorTemperatureSpin_->setValue(1.0);
            orchestratorTemperatureSpin_->setEnabled(false);
            orchestratorTemperatureSlider_->setValue(10);
            orchestratorTemperatureSlider_->setEnabled(false);
            orchestratorTemperatureSpin_->setToolTip("Temperature must be 1.0 when thinking is enabled");
        } else {
            // Re-enable temperature controls when thinking is disabled
            orchestratorTemperatureSpin_->setEnabled(true);
            orchestratorTemperatureSlider_->setEnabled(true);
            orchestratorTemperatureSpin_->setToolTip("");
        }
    });
    
    // Model selection
    connect(agentModelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreferencesDialog::onAgentModelChanged);
    connect(graderModelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreferencesDialog::onGraderModelChanged);
    connect(orchestratorModelCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PreferencesDialog::onOrchestratorModelChanged);
    
    // Browse buttons
    connect(oauthDirBrowse_, &QPushButton::clicked, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select OAuth Config Directory",
            oauthDirEdit_->text());
        if (!dir.isEmpty()) {
            oauthDirEdit_->setText(dir);
        }
    });
    
    // Grader enabled checkbox
    connect(graderEnabledCheck_, &QCheckBox::toggled, [this](bool checked) {
        graderModelCombo_->setEnabled(checked);
        graderMaxTokensSpin_->setEnabled(checked);
        graderMaxThinkingTokensSpin_->setEnabled(checked);
        graderContextLimitSpin_->setEnabled(checked);
    });
}

void PreferencesDialog::loadConfiguration() {
    Config& config = Config::instance();
    originalConfig_ = config;
    currentConfig_ = config;
    
    // API settings
    apiKeyRadio_->setChecked(config.api.auth_method == claude::AuthMethod::API_KEY);
    oauthRadio_->setChecked(config.api.auth_method == claude::AuthMethod::OAUTH);
    apiKeyEdit_->setText(QString::fromStdString(config.api.api_key));
    oauthDirEdit_->setText(QString::fromStdString(config.api.oauth_config_dir));
    baseUrlEdit_->setText(QString::fromStdString(config.api.base_url));
    
    // Agent model settings
    agentModelCombo_->setCurrentIndex(static_cast<int>(config.agent.model));
    agentMaxTokensSpin_->setValue(config.agent.max_tokens);
    agentMaxThinkingTokensSpin_->setValue(config.agent.max_thinking_tokens);
    agentTemperatureSpin_->setValue(config.agent.temperature);
    agentTemperatureSlider_->setValue(static_cast<int>(config.agent.temperature * 10));
    agentEnableThinkingCheck_->setChecked(config.agent.enable_thinking);
    agentInterleavedThinkingCheck_->setChecked(config.agent.enable_interleaved_thinking);
    
    // Apply temperature lock if thinking is enabled
    if (config.agent.enable_thinking) {
        agentTemperatureSpin_->setValue(1.0);
        agentTemperatureSpin_->setEnabled(false);
        agentTemperatureSlider_->setValue(10);
        agentTemperatureSlider_->setEnabled(false);
        agentTemperatureSpin_->setToolTip("Temperature must be 1.0 when thinking is enabled");
    }
    
    // Grader model settings
    graderEnabledCheck_->setChecked(config.grader.enabled);
    graderModelCombo_->setCurrentIndex(static_cast<int>(config.grader.model));
    graderMaxTokensSpin_->setValue(config.grader.max_tokens);
    graderMaxThinkingTokensSpin_->setValue(config.grader.max_thinking_tokens);
    graderContextLimitSpin_->setValue(config.grader.context_limit);
    
    // Orchestrator model settings
    orchestratorModelCombo_->setCurrentIndex(static_cast<int>(config.orchestrator.model.model));
    orchestratorMaxTokensSpin_->setValue(config.orchestrator.model.max_tokens);
    orchestratorMaxThinkingTokensSpin_->setValue(config.orchestrator.model.max_thinking_tokens);
    orchestratorTemperatureSpin_->setValue(config.orchestrator.model.temperature);
    orchestratorTemperatureSlider_->setValue(static_cast<int>(config.orchestrator.model.temperature * 10));
    orchestratorEnableThinkingCheck_->setChecked(config.orchestrator.model.enable_thinking);
    
    // Apply temperature lock if thinking is enabled
    if (config.orchestrator.model.enable_thinking) {
        orchestratorTemperatureSpin_->setValue(1.0);
        orchestratorTemperatureSpin_->setEnabled(false);
        orchestratorTemperatureSlider_->setValue(10);
        orchestratorTemperatureSlider_->setEnabled(false);
        orchestratorTemperatureSpin_->setToolTip("Temperature must be 1.0 when thinking is enabled");
    }
    
    // Agent settings
    maxIterationsSpin_->setValue(config.agent.max_iterations);
    contextLimitSpin_->setValue(config.agent.context_limit);
    enableDeepAnalysisCheck_->setChecked(config.agent.enable_deep_analysis);
    enablePythonToolCheck_->setChecked(config.agent.enable_python_tool);
    
    // IRC settings
    ircServerEdit_->setText(QString::fromStdString(config.irc.server));
    ircPortSpin_->setValue(config.irc.port);
    conflictChannelFormatEdit_->setText(QString::fromStdString(config.irc.conflict_channel_format));
    
    configModified_ = false;
}

void PreferencesDialog::saveConfiguration() {
    Config& config = Config::instance();
    
    // API settings
    config.api.auth_method = apiKeyRadio_->isChecked() ? 
        claude::AuthMethod::API_KEY : claude::AuthMethod::OAUTH;
    config.api.api_key = apiKeyEdit_->text().toStdString();
    config.api.use_oauth = oauthRadio_->isChecked();
    config.api.oauth_config_dir = oauthDirEdit_->text().toStdString();
    config.api.base_url = baseUrlEdit_->text().toStdString();
    
    // Agent model settings
    config.agent.model = static_cast<claude::Model>(agentModelCombo_->currentIndex());
    config.agent.max_tokens = agentMaxTokensSpin_->value();
    config.agent.max_thinking_tokens = agentMaxThinkingTokensSpin_->value();
    // Force temperature to 1.0 when thinking is enabled
    config.agent.temperature = agentEnableThinkingCheck_->isChecked() ? 1.0 : agentTemperatureSpin_->value();
    config.agent.enable_thinking = agentEnableThinkingCheck_->isChecked();
    config.agent.enable_interleaved_thinking = agentInterleavedThinkingCheck_->isChecked();
    
    // Grader model settings
    config.grader.enabled = graderEnabledCheck_->isChecked();
    config.grader.model = static_cast<claude::Model>(graderModelCombo_->currentIndex());
    config.grader.max_tokens = graderMaxTokensSpin_->value();
    config.grader.max_thinking_tokens = graderMaxThinkingTokensSpin_->value();
    config.grader.context_limit = graderContextLimitSpin_->value();
    
    // Orchestrator model settings
    config.orchestrator.model.model = static_cast<claude::Model>(orchestratorModelCombo_->currentIndex());
    config.orchestrator.model.max_tokens = orchestratorMaxTokensSpin_->value();
    config.orchestrator.model.max_thinking_tokens = orchestratorMaxThinkingTokensSpin_->value();
    // Force temperature to 1.0 when thinking is enabled
    config.orchestrator.model.temperature = orchestratorEnableThinkingCheck_->isChecked() ? 1.0 : orchestratorTemperatureSpin_->value();
    config.orchestrator.model.enable_thinking = orchestratorEnableThinkingCheck_->isChecked();
    
    // Agent settings
    config.agent.max_iterations = maxIterationsSpin_->value();
    config.agent.context_limit = contextLimitSpin_->value();
    config.agent.enable_deep_analysis = enableDeepAnalysisCheck_->isChecked();
    config.agent.enable_python_tool = enablePythonToolCheck_->isChecked();
    
    // IRC settings
    config.irc.server = ircServerEdit_->text().toStdString();
    config.irc.port = ircPortSpin_->value();
    config.irc.conflict_channel_format = conflictChannelFormatEdit_->text().toStdString();
    
    // Save to file
    config.save();
    
    currentConfig_ = config;
    configModified_ = false;
}

bool PreferencesDialog::validateConfiguration() {
    // Validate API key if using API key auth
    if (apiKeyRadio_->isChecked()) {
        QString apiKey = apiKeyEdit_->text();
        if (apiKey.isEmpty()) {
            showValidationError("API key cannot be empty");
            tabWidget_->setCurrentIndex(0); // Switch to API tab
            apiKeyEdit_->setFocus();
            return false;
        }
        if (!apiKey.startsWith("sk-ant-")) {
            showValidationError("Invalid API key format. Should start with 'sk-ant-'");
            tabWidget_->setCurrentIndex(0);
            apiKeyEdit_->setFocus();
            return false;
        }
    }
    
    // Validate OAuth dir if using OAuth
    if (oauthRadio_->isChecked()) {
        QString oauthDir = oauthDirEdit_->text();
        if (oauthDir.isEmpty()) {
            showValidationError("OAuth config directory cannot be empty");
            tabWidget_->setCurrentIndex(0);
            oauthDirEdit_->setFocus();
            return false;
        }
    }
    
    // Validate base URL
    QString baseUrl = baseUrlEdit_->text();
    if (baseUrl.isEmpty()) {
        showValidationError("Base URL cannot be empty");
        tabWidget_->setCurrentIndex(0);
        baseUrlEdit_->setFocus();
        return false;
    }
    if (!baseUrl.startsWith("http://") && !baseUrl.startsWith("https://")) {
        showValidationError("Base URL must start with http:// or https://");
        tabWidget_->setCurrentIndex(0);
        baseUrlEdit_->setFocus();
        return false;
    }
    
    // Validate IRC settings
    if (ircServerEdit_->text().isEmpty()) {
        showValidationError("IRC server address cannot be empty");
        tabWidget_->setCurrentIndex(3); // IRC tab
        ircServerEdit_->setFocus();
        return false;
    }
    
    // Validate channel formats
    if (conflictChannelFormatEdit_->text().isEmpty()) {
        showValidationError("Conflict channel format cannot be empty");
        tabWidget_->setCurrentIndex(3);
        conflictChannelFormatEdit_->setFocus();
        return false;
    }
    
    // Validate thinking mode + temperature constraints
    if (agentEnableThinkingCheck_->isChecked() && agentTemperatureSpin_->value() != 1.0) {
        showValidationError("Temperature must be 1.0 when thinking mode is enabled for Agent");
        tabWidget_->setCurrentIndex(1); // Models tab
        agentTemperatureSpin_->setFocus();
        return false;
    }
    
    if (orchestratorEnableThinkingCheck_->isChecked() && orchestratorTemperatureSpin_->value() != 1.0) {
        showValidationError("Temperature must be 1.0 when thinking mode is enabled for Orchestrator");
        tabWidget_->setCurrentIndex(1); // Models tab
        orchestratorTemperatureSpin_->setFocus();
        return false;
    }
    
    // Validate thinking tokens minimum when thinking is enabled
    if (agentEnableThinkingCheck_->isChecked() && agentMaxThinkingTokensSpin_->value() < 1024) {
        showValidationError("Max thinking tokens must be at least 1024 when thinking is enabled");
        tabWidget_->setCurrentIndex(1); // Models tab
        agentMaxThinkingTokensSpin_->setFocus();
        return false;
    }
    
    if (orchestratorEnableThinkingCheck_->isChecked() && orchestratorMaxThinkingTokensSpin_->value() < 1024) {
        showValidationError("Max thinking tokens must be at least 1024 when thinking is enabled");
        tabWidget_->setCurrentIndex(1); // Models tab
        orchestratorMaxThinkingTokensSpin_->setFocus();
        return false;
    }
    
    return true;
}

void PreferencesDialog::onApply() {
    if (!validateConfiguration()) {
        return;
    }
    
    saveConfiguration();
    emit configurationChanged();
}

void PreferencesDialog::onAccept() {
    if (!validateConfiguration()) {
        return;
    }
    
    saveConfiguration();
    emit configurationChanged();
    accept();
}

void PreferencesDialog::onReject() {
    if (hasUnsavedChanges()) {
        int ret = QMessageBox::warning(this, "Unsaved Changes",
            "You have unsaved changes. Do you want to save them?",
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        
        if (ret == QMessageBox::Save) {
            onAccept();
            return;
        } else if (ret == QMessageBox::Cancel) {
            return;
        }
    }
    
    reject();
}

void PreferencesDialog::onResetDefaults() {
    int ret = QMessageBox::question(this, "Reset to Defaults",
        "This will reset all settings to their default values.\n"
        "Are you sure you want to continue?",
        QMessageBox::Yes | QMessageBox::No);
    
    if (ret == QMessageBox::Yes) {
        Config& config = Config::instance();
        config.reset();
        loadConfiguration();
        
        QMessageBox::information(this, "Settings Reset",
            "All settings have been reset to defaults.");
    }
}

void PreferencesDialog::onExportConfig() {
    QString fileName = QFileDialog::getSaveFileName(this, 
        "Export Configuration", "", "JSON Files (*.json)");
    
    if (!fileName.isEmpty()) {
        Config& config = Config::instance();
        if (config.save_to_file(fileName.toStdString())) {
            QMessageBox::information(this, "Export Successful",
                "Configuration exported successfully.");
        } else {
            QMessageBox::critical(this, "Export Failed",
                "Failed to export configuration.");
        }
    }
}

void PreferencesDialog::onImportConfig() {
    QString fileName = QFileDialog::getOpenFileName(this,
        "Import Configuration", "", "JSON Files (*.json)");
    
    if (!fileName.isEmpty()) {
        Config& config = Config::instance();
        if (config.load_from_file(fileName.toStdString())) {
            loadConfiguration();
            QMessageBox::information(this, "Import Successful",
                "Configuration imported successfully.");
        } else {
            QMessageBox::critical(this, "Import Failed",
                "Failed to import configuration.");
        }
    }
}

void PreferencesDialog::onTestAPIConnection() {
    testApiButton_->setEnabled(false);
    apiStatusLabel_->setText("Testing...");
    apiStatusLabel_->setStyleSheet("QLabel { color: blue; }");
    
    // Use the actual Claude SDK client for testing
    try {
        std::unique_ptr<claude::Client> client;
        std::string baseUrl = baseUrlEdit_->text().toStdString();
        
        // Create client based on authentication method
        if (apiKeyRadio_->isChecked()) {
            // API Key authentication
            std::string apiKey = apiKeyEdit_->text().toStdString();
            if (apiKey.empty()) {
                apiStatusLabel_->setText("✗ API key is empty");
                apiStatusLabel_->setStyleSheet("QLabel { color: red; }");
                testApiButton_->setEnabled(true);
                return;
            }
            client = std::make_unique<claude::Client>(apiKey, baseUrl);
        } else {
            // OAuth authentication
            std::string oauthDir = oauthDirEdit_->text().toStdString();
            if (oauthDir.empty()) {
                apiStatusLabel_->setText("✗ OAuth directory is empty");
                apiStatusLabel_->setStyleSheet("QLabel { color: red; }");
                testApiButton_->setEnabled(true);
                return;
            }
            
            // Use OAuth manager to get credentials
            claude::auth::OAuthManager oauthMgr(oauthDir);
            
            if (!oauthMgr.has_credentials()) {
                apiStatusLabel_->setText("✗ No OAuth credentials found in directory");
                apiStatusLabel_->setStyleSheet("QLabel { color: red; }");
                testApiButton_->setEnabled(true);
                return;
            }
            
            auto oauthCreds = oauthMgr.get_credentials();
            if (!oauthCreds) {
                apiStatusLabel_->setText("✗ Failed to load OAuth credentials");
                apiStatusLabel_->setStyleSheet("QLabel { color: red; }");
                testApiButton_->setEnabled(true);
                return;
            }
            
            client = std::make_unique<claude::Client>(oauthCreds, baseUrl);
        }
        
        // Create a simple test message
        claude::ChatRequest request;
        request.model = claude::Model::Haiku35;
        request.max_tokens = 10;
        request.enable_thinking = false;  // Haiku doesn't support thinking
        
        // Add a simple test message
        request.messages.push_back(claude::messages::Message::user_text("Test"));
        
        // Try to send the request  
        auto response = client->send_request(request);
        
        if (response.success) {
            apiStatusLabel_->setText("✓ Connection successful");
            apiStatusLabel_->setStyleSheet("QLabel { color: green; }");
        } else {
            // Check if there's an error message
            if (response.error.has_value()) {
                QString errorMsg = QString::fromStdString(response.error.value());
                if (errorMsg.length() > 50) {
                    errorMsg = errorMsg.left(50) + "...";
                }
                apiStatusLabel_->setText(QString("✗ %1").arg(errorMsg));
            } else {
                apiStatusLabel_->setText("✗ Connection failed");
            }
            apiStatusLabel_->setStyleSheet("QLabel { color: red; }");
        }
        
    } catch (const std::exception& e) {
        QString error = QString::fromStdString(e.what());
        
        // Parse the error to provide better feedback
        if (error.contains("401") || error.contains("Unauthorized")) {
            if (apiKeyRadio_->isChecked()) {
                apiStatusLabel_->setText("✗ Invalid API key");
            } else {
                apiStatusLabel_->setText("✗ OAuth authentication failed");
            }
        } else if (error.contains("404")) {
            apiStatusLabel_->setText("✗ Invalid API endpoint");
        } else if (error.contains("OAuth") || error.contains("token")) {
            apiStatusLabel_->setText("✗ OAuth token error - check config directory");
        } else if (error.contains("Connection refused") || error.contains("Couldn't connect")) {
            apiStatusLabel_->setText("✗ Connection failed - check URL and network");
        } else if (error.contains("SSL") || error.contains("certificate")) {
            apiStatusLabel_->setText("✗ SSL/TLS error - check certificates");
        } else {
            // Truncate very long error messages
            if (error.length() > 100) {
                error = error.left(100) + "...";
            }
            apiStatusLabel_->setText(QString("✗ Error: %1").arg(error));
        }
        apiStatusLabel_->setStyleSheet("QLabel { color: red; }");
    }
    
    testApiButton_->setEnabled(true);
}

void PreferencesDialog::onTestIRCConnection() {
    testIrcButton_->setEnabled(false);
    ircStatusLabel_->setText("Testing...");
    ircStatusLabel_->setStyleSheet("QLabel { color: blue; }");
    
    // Simple TCP connection test - use QPointer for safe memory management
    QTcpSocket* rawSocket = new QTcpSocket(this);
    QPointer<QTcpSocket> socket(rawSocket);
    
    // Flag to track if we've already handled the result
    auto resultHandled = std::make_shared<bool>(false);
    
    connect(rawSocket, &QTcpSocket::connected, [this, socket, resultHandled]() {
        if (*resultHandled || !socket) return;
        *resultHandled = true;
        
        ircStatusLabel_->setText("✓ Connection successful");
        ircStatusLabel_->setStyleSheet("QLabel { color: green; }");
        testIrcButton_->setEnabled(true);
        socket->disconnectFromHost();
        socket->deleteLater();
    });
    
    connect(rawSocket, &QTcpSocket::errorOccurred, [this, socket, resultHandled](QAbstractSocket::SocketError error) {
        Q_UNUSED(error);
        if (*resultHandled || !socket) return;
        *resultHandled = true;
        
        ircStatusLabel_->setText(QString("✗ %1").arg(socket->errorString()));
        ircStatusLabel_->setStyleSheet("QLabel { color: red; }");
        testIrcButton_->setEnabled(true);
        socket->deleteLater();
    });
    
    rawSocket->connectToHost(ircServerEdit_->text(), ircPortSpin_->value());
    
    // Timeout after 5 seconds - use QPointer to safely check if socket still exists
    QTimer::singleShot(5000, [this, socket, resultHandled]() {
        if (*resultHandled || !socket) return;  // Socket was already deleted or result handled
        *resultHandled = true;
        
        if (socket->state() != QTcpSocket::ConnectedState) {
            ircStatusLabel_->setText("✗ Connection timeout");
            ircStatusLabel_->setStyleSheet("QLabel { color: red; }");
            testIrcButton_->setEnabled(true);
            socket->abort();
            socket->deleteLater();
        }
    });
}

void PreferencesDialog::onAuthMethodChanged() {
    bool useApiKey = apiKeyRadio_->isChecked();
    
    // The QStackedWidget automatically handles showing/hiding the appropriate page
    // based on the radio button connections we set up in createApiTab()
    
    // Update token status when switching to OAuth
    if (!useApiKey) {
        updateTokenStatus();
    } else {
        // Clear token status when switching to API Key mode
        tokenExpirationLabel_->setText("Token Status: N/A (Using API Key)");
        tokenExpirationLabel_->setStyleSheet("QLabel { color: #666666; }");
    }
}

void PreferencesDialog::onAgentModelChanged(int index) {
    Q_UNUSED(index);
    // Could adjust default token limits based on model
}

void PreferencesDialog::onGraderModelChanged(int index) {
    Q_UNUSED(index);
    // Could adjust default token limits based on model
}

void PreferencesDialog::onOrchestratorModelChanged(int index) {
    Q_UNUSED(index);
    // Could adjust default token limits based on model
}

void PreferencesDialog::validateApiKey() {
    // Real-time validation
    QString apiKey = apiKeyEdit_->text();
    if (!apiKey.isEmpty() && !apiKey.startsWith("sk-ant-")) {
        apiKeyEdit_->setStyleSheet("QLineEdit { border: 2px solid red; }");
    } else {
        apiKeyEdit_->setStyleSheet("");
    }
}

void PreferencesDialog::validateBaseUrl() {
    QString url = baseUrlEdit_->text();
    if (!url.isEmpty() && !url.startsWith("http://") && !url.startsWith("https://")) {
        baseUrlEdit_->setStyleSheet("QLineEdit { border: 2px solid red; }");
    } else {
        baseUrlEdit_->setStyleSheet("");
    }
}

void PreferencesDialog::validateOAuthDir() {
    QString dir = oauthDirEdit_->text();
    if (!dir.isEmpty() && !QDir(dir).exists()) {
        oauthDirEdit_->setStyleSheet("QLineEdit { border: 2px solid orange; }");
    } else {
        oauthDirEdit_->setStyleSheet("");
    }
}

void PreferencesDialog::showValidationError(const QString& message) {
    QMessageBox::critical(this, "Validation Error", message);
}

QString PreferencesDialog::getConfigPath() const {
    // Get IDA plugins directory for config file
    const char* ida_dir = idadir(nullptr);
    if (ida_dir) {
        return QString("%1/plugins/llm_re_config.json").arg(ida_dir);
    }
    return "llm_re_config.json";
}

bool PreferencesDialog::hasUnsavedChanges() {
    // Check if any field has been modified
    // For simplicity, we'll track this with configModified_ flag
    return configModified_;
}

void PreferencesDialog::onRefreshOAuthToken() {
    // Disable button during refresh
    refreshTokenButton_->setEnabled(false);
    refreshTokenButton_->setText("Refreshing...");
    
    // Create OAuth manager to refresh token
    auto oauth_manager = Config::create_oauth_manager(oauthDirEdit_->text().toStdString());
    if (!oauth_manager) {
        tokenExpirationLabel_->setText("Token Status: <b>Error - Failed to create OAuth manager</b>");
        tokenExpirationLabel_->setStyleSheet("QLabel { color: #ff0000; }");
        refreshTokenButton_->setEnabled(true);
        refreshTokenButton_->setText("Refresh Token");
        return;
    }
    
    // Force refresh the token
    auto refreshed_creds = oauth_manager->force_refresh();
    if (refreshed_creds) {
        // Just update the status display - no dialog
        updateTokenStatus();
        
        // Show success briefly in the status label
        tokenExpirationLabel_->setText("Token Status: <b>Successfully Refreshed!</b>");
        tokenExpirationLabel_->setStyleSheet("QLabel { color: #00aa00; }");
        
        // Use a timer to update to the actual expiry time after 2 seconds
        QTimer::singleShot(2000, this, &PreferencesDialog::updateTokenStatus);
    } else {
        // Show error in the status label instead of dialog
        QString errorMsg = QString("Token Status: <b>Refresh Failed - %1</b>")
            .arg(QString::fromStdString(oauth_manager->get_last_error()));
        tokenExpirationLabel_->setText(errorMsg);
        tokenExpirationLabel_->setStyleSheet("QLabel { color: #ff0000; }");
    }
    
    // Re-enable button
    refreshTokenButton_->setEnabled(true);
    refreshTokenButton_->setText("Refresh Token");
}

void PreferencesDialog::updateTokenStatus() {
    // Only update if OAuth is selected
    if (!oauthRadio_->isChecked()) {
        tokenExpirationLabel_->setText("Token Status: N/A (Using API Key)");
        tokenExpirationLabel_->setStyleSheet("QLabel { color: #666666; }");
        return;
    }
    
    // Create OAuth manager to check token status
    auto oauth_manager = Config::create_oauth_manager(oauthDirEdit_->text().toStdString());
    if (!oauth_manager) {
        tokenExpirationLabel_->setText("Token Status: No OAuth configuration found");
        tokenExpirationLabel_->setStyleSheet("QLabel { color: #999999; }");
        return;
    }
    
    // Get current credentials
    auto creds = oauth_manager->get_credentials();
    if (!creds) {
        tokenExpirationLabel_->setText("Token Status: No credentials available");
        tokenExpirationLabel_->setStyleSheet("QLabel { color: #999999; }");
        return;
    }
    
    // Calculate time until expiration
    auto now = std::chrono::system_clock::now();
    auto now_timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        now.time_since_epoch()).count();
    
    double seconds_until_expiry = creds->expires_at - now_timestamp;
    
    // Format the expiration message
    QString status_text;
    QString style_sheet;
    
    if (seconds_until_expiry <= 0) {
        status_text = "Token Status: <b>EXPIRED</b>";
        style_sheet = "QLabel { color: #ff0000; }"; // Red
    } else if (seconds_until_expiry < 300) { // Less than 5 minutes
        int minutes = static_cast<int>(seconds_until_expiry / 60);
        status_text = QString("Token Status: Expires in <b>%1 minutes</b>").arg(minutes);
        style_sheet = "QLabel { color: #ff6600; }"; // Orange/Red
    } else if (seconds_until_expiry < 3600) { // Less than 1 hour
        int minutes = static_cast<int>(seconds_until_expiry / 60);
        status_text = QString("Token Status: Expires in <b>%1 minutes</b>").arg(minutes);
        style_sheet = "QLabel { color: #ff9900; }"; // Orange
    } else if (seconds_until_expiry < 86400) { // Less than 24 hours
        int hours = static_cast<int>(seconds_until_expiry / 3600);
        int minutes = static_cast<int>((seconds_until_expiry - hours * 3600) / 60);
        if (minutes > 0) {
            status_text = QString("Token Status: Expires in <b>%1h %2m</b>").arg(hours).arg(minutes);
        } else {
            status_text = QString("Token Status: Expires in <b>%1 hours</b>").arg(hours);
        }
        style_sheet = "QLabel { color: #009900; }"; // Green
    } else {
        int days = static_cast<int>(seconds_until_expiry / 86400);
        int hours = static_cast<int>((seconds_until_expiry - days * 86400) / 3600);
        if (hours > 0) {
            status_text = QString("Token Status: Expires in <b>%1d %2h</b>").arg(days).arg(hours);
        } else {
            status_text = QString("Token Status: Expires in <b>%1 days</b>").arg(days);
        }
        style_sheet = "QLabel { color: #009900; }"; // Green
    }
    
    tokenExpirationLabel_->setText(status_text);
    tokenExpirationLabel_->setStyleSheet(style_sheet);
}

} // namespace llm_re::ui