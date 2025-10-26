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
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QTableWidget>
#include <QHeaderView>
#include <QThread>
#include <QDir>
#include <QPointer>

#include "../sdk/client/client.h"
#include "../sdk/auth/oauth_manager.h"
#include "../sdk/auth/oauth_authorizer.h"
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
    
    // Setup account update timer (update every 5 seconds for rate limit countdowns)
    accountUpdateTimer_ = new QTimer(this);
    connect(accountUpdateTimer_, &QTimer::timeout, this, &PreferencesDialog::refreshAccountsList);
    accountUpdateTimer_->start(5000); // 5 seconds

    // Initial accounts list update
    refreshAccountsList();

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
    createProfilingTab();

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

    // Accounts group
    auto* accountsGroup = new QGroupBox("OAuth Accounts", oauthPage);
    auto* accountsLayout = new QVBoxLayout(accountsGroup);

    // Accounts table
    accountsTable_ = new QTableWidget(accountsGroup);
    accountsTable_->setColumnCount(4);
    accountsTable_->setHorizontalHeaderLabels({"Priority", "Account ID", "Status", "Expires In"});
    accountsTable_->horizontalHeader()->setStretchLastSection(false);
    accountsTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    accountsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    accountsTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    accountsTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    accountsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    accountsTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    accountsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    accountsTable_->setMinimumHeight(150);
    accountsLayout->addWidget(accountsTable_);

    // Buttons layout
    auto* buttonsLayout = new QHBoxLayout();
    addAccountButton_ = new QPushButton("Add Account", accountsGroup);
    removeAccountButton_ = new QPushButton("Remove", accountsGroup);
    moveUpButton_ = new QPushButton("Move Up", accountsGroup);
    moveDownButton_ = new QPushButton("Move Down", accountsGroup);
    refreshAccountsButton_ = new QPushButton("Refresh Tokens", accountsGroup);

    buttonsLayout->addWidget(addAccountButton_);
    buttonsLayout->addWidget(removeAccountButton_);
    buttonsLayout->addWidget(moveUpButton_);
    buttonsLayout->addWidget(moveDownButton_);
    buttonsLayout->addStretch();
    buttonsLayout->addWidget(refreshAccountsButton_);

    accountsLayout->addLayout(buttonsLayout);

    // Disable buttons initially (no selection)
    removeAccountButton_->setEnabled(false);
    moveUpButton_->setEnabled(false);
    moveDownButton_->setEnabled(false);
    refreshAccountsButton_->setEnabled(false);

    oauthPageLayout->addWidget(accountsGroup);

    // Config directory group
    auto* oauthConfigGroup = new QGroupBox("Configuration", oauthPage);
    auto* oauthConfigLayout = new QFormLayout(oauthConfigGroup);

    auto* oauthDirLayout = new QHBoxLayout();
    oauthDirEdit_ = new QLineEdit(oauthConfigGroup);
    oauthDirEdit_->setPlaceholderText("~/.claude_cpp_sdk");
    oauthDirBrowse_ = new QPushButton("Browse...", oauthConfigGroup);
    oauthDirLayout->addWidget(oauthDirEdit_);
    oauthDirLayout->addWidget(oauthDirBrowse_);
    oauthConfigLayout->addRow("Config Directory:", oauthDirLayout);

    oauthPageLayout->addWidget(oauthConfigGroup);

    // Help text
    auto* oauthHelp = new QLabel(
        "ℹ️ Accounts are used in priority order. Primary (priority 0) is preferred. "
        "Click \"Add Account\" to authorize a new account via browser.",
        oauthPage);
    oauthHelp->setWordWrap(true);
    oauthHelp->setStyleSheet("QLabel { color: #666666; font-size: 11px; padding: 10px; }");
    oauthPageLayout->addWidget(oauthHelp);

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
    agentModelCombo_->addItem("Claude Sonnet 4.5", QVariant::fromValue(static_cast<int>(claude::Model::Sonnet45)));
    agentModelCombo_->addItem("Claude Haiku 4.5", QVariant::fromValue(static_cast<int>(claude::Model::Haiku45)));
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
    contextLimitSpin_->setToolTip("Token limit for tool result size management");
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
    graderModelCombo_->addItem("Claude Sonnet 4.5", QVariant::fromValue(static_cast<int>(claude::Model::Sonnet45)));
    graderModelCombo_->addItem("Claude Haiku 4.5", QVariant::fromValue(static_cast<int>(claude::Model::Haiku45)));
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
    orchestratorModelCombo_->addItem("Claude Sonnet 4.5", QVariant::fromValue(static_cast<int>(claude::Model::Sonnet45)));
    orchestratorModelCombo_->addItem("Claude Haiku 4.5", QVariant::fromValue(static_cast<int>(claude::Model::Haiku45)));
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




    layout->addWidget(serverGroup);
    layout->addStretch();

    tabWidget_->addTab(widget, "IRC");
}

void PreferencesDialog::createProfilingTab() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);

    // Profiling settings
    auto* profilingGroup = new QGroupBox("Profiling", widget);
    auto* profilingLayout = new QFormLayout(profilingGroup);

    profilingEnabledCheck_ = new QCheckBox("Enable performance profiling", profilingGroup);
    profilingEnabledCheck_->setToolTip("Track API requests, tool execution timing, and token usage");
    profilingLayout->addRow("", profilingEnabledCheck_);

    layout->addWidget(profilingGroup);
    layout->addStretch();

    tabWidget_->addTab(widget, "Profiling");
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
    
    // OAuth account management buttons
    connect(addAccountButton_, &QPushButton::clicked, this, &PreferencesDialog::onAddAccount);
    connect(removeAccountButton_, &QPushButton::clicked, this, &PreferencesDialog::onRemoveAccount);
    connect(moveUpButton_, &QPushButton::clicked, this, &PreferencesDialog::onMoveAccountUp);
    connect(moveDownButton_, &QPushButton::clicked, this, &PreferencesDialog::onMoveAccountDown);
    connect(refreshAccountsButton_, &QPushButton::clicked, this, [this]() {
        // Get selected account
        int row = accountsTable_->currentRow();
        if (row < 0) {
            QMessageBox::information(this, "No Account Selected",
                "Please select an account to refresh its tokens.");
            return;
        }

        // Get account UUID from selected row
        auto* uuidItem = accountsTable_->item(row, 1);
        if (!uuidItem) {
            return;
        }
        QString account_uuid = uuidItem->data(Qt::UserRole).toString();

        // Get config directory
        QString config_dir = oauthDirEdit_->text();
        if (config_dir.isEmpty()) {
            config_dir = "~/.claude_cpp_sdk";
        }

        // Create progress dialog
        auto* progressDialog = new QProgressDialog(
            "Refreshing OAuth tokens...",
            "Cancel",
            0, 0,
            this
        );
        progressDialog->setWindowModality(Qt::WindowModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(nullptr);  // Can't cancel token refresh
        progressDialog->show();

        // Refresh tokens in background thread
        auto* thread = QThread::create([this, progressDialog, config_dir, account_uuid]() {
            // Create OAuth manager
            auto oauth_manager = Config::create_oauth_manager(config_dir.toStdString());

            bool success = false;
            QString error_message;

            if (oauth_manager) {
                auto refreshed_creds = oauth_manager->refresh_account(account_uuid.toStdString());
                success = (refreshed_creds != nullptr);

                if (!success) {
                    error_message = QString::fromStdString(oauth_manager->get_last_error());
                }
            } else {
                error_message = "Failed to create OAuth manager";
            }

            // Update UI on main thread
            QMetaObject::invokeMethod(this, [this, success, error_message, progressDialog]() {
                progressDialog->close();
                progressDialog->deleteLater();

                if (success) {
                    QMessageBox::information(this, "Success",
                        "OAuth tokens refreshed successfully!");
                    refreshAccountsList();
                } else {
                    QMessageBox::warning(this, "Token Refresh Failed",
                        QString("Failed to refresh OAuth tokens:\n\n%1").arg(error_message));
                }
            }, Qt::QueuedConnection);
        });

        connect(thread, &QThread::finished, thread, &QThread::deleteLater);
        thread->start();
    });
    connect(accountsTable_, &QTableWidget::itemSelectionChanged, this, &PreferencesDialog::onAccountSelectionChanged);

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
    contextLimitSpin_->setValue(config.agent.context_limit);
    enableDeepAnalysisCheck_->setChecked(config.agent.enable_deep_analysis);
    enablePythonToolCheck_->setChecked(config.agent.enable_python_tool);
    
    // IRC settings
    ircServerEdit_->setText(QString::fromStdString(config.irc.server));

    // Profiling settings
    profilingEnabledCheck_->setChecked(config.profiling.enabled);

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
    config.agent.context_limit = contextLimitSpin_->value();
    config.agent.enable_deep_analysis = enableDeepAnalysisCheck_->isChecked();
    config.agent.enable_python_tool = enablePythonToolCheck_->isChecked();
    
    // IRC settings
    config.irc.server = ircServerEdit_->text().toStdString();

    // Profiling settings
    config.profiling.enabled = profilingEnabledCheck_->isChecked();

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
            
            client = std::make_unique<claude::Client>(oauthCreds, nullptr, baseUrl);
        }
        
        // Create a simple test message
        claude::ChatRequest request;
        request.model = claude::Model::Haiku45;
        request.max_tokens = 10;
        request.enable_thinking = false;
        
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


void PreferencesDialog::onAuthMethodChanged() {
    bool useApiKey = apiKeyRadio_->isChecked();
    
    // The QStackedWidget automatically handles showing/hiding the appropriate page
    // based on the radio button connections we set up in createApiTab()
    
    // Token status is now handled by the accounts table
    // No need to update anything here
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

void PreferencesDialog::onAddAccount() {
    // Disable button during authorization
    addAccountButton_->setEnabled(false);
    addAccountButton_->setText("Authorizing...");

    // Create progress dialog
    auto* progressDialog = new QProgressDialog(
        "Waiting for authorization in browser...\n\n"
        "Please complete the OAuth flow in your browser.\n"
        "This dialog will close automatically when done.",
        "Cancel", 0, 0, this);
    progressDialog->setWindowTitle("OAuth Authorization");
    progressDialog->setWindowModality(Qt::WindowModal);
    progressDialog->setMinimumDuration(0);
    progressDialog->setValue(0);

    // Run OAuth flow in background thread
    auto* thread = QThread::create([this, progressDialog]() {
        claude::auth::OAuthAuthorizer authorizer;
        bool success = authorizer.authorize();
        std::string error = authorizer.getLastError();

        // Update UI on main thread
        QMetaObject::invokeMethod(this, [this, success, error, progressDialog]() {
            progressDialog->close();
            progressDialog->deleteLater();

            addAccountButton_->setEnabled(true);
            addAccountButton_->setText("Add Account");

            if (success) {
                QMessageBox::information(this, "Success",
                    "Account added successfully! It will appear in the list below.");
                refreshAccountsList();
            } else {
                QMessageBox::warning(this, "Authorization Failed",
                    QString("Failed to authorize account:\n\n%1")
                        .arg(QString::fromStdString(error)));
            }
        }, Qt::QueuedConnection);
    });

    // Handle cancel button
    connect(progressDialog, &QProgressDialog::canceled, [thread, this]() {
        // Note: We can't actually cancel the OAuth flow once started,
        // but we can close the dialog and re-enable the button
        addAccountButton_->setEnabled(true);
        addAccountButton_->setText("Add Account");
    });

    // Clean up thread when done
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    thread->start();
}

void PreferencesDialog::onRemoveAccount() {
    int row = accountsTable_->currentRow();
    if (row < 0) return;

    // Get account UUID from table
    QString account_uuid = accountsTable_->item(row, 1)->data(Qt::UserRole).toString();

    // Confirm deletion
    auto reply = QMessageBox::question(this, "Confirm Removal",
        QString("Remove account %1?\n\nThis cannot be undone.")
            .arg(accountsTable_->item(row, 1)->text()),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::No) return;

    // Load OAuth manager
    auto oauth_manager = Config::create_oauth_manager(
        oauthDirEdit_->text().toStdString());

    if (!oauth_manager) {
        QMessageBox::warning(this, "Error", "Failed to load OAuth manager");
        return;
    }

    // Remove account
    if (oauth_manager->remove_account(account_uuid.toStdString())) {
        refreshAccountsList();
    } else {
        QMessageBox::warning(this, "Error",
            QString("Failed to remove account:\n\n%1")
                .arg(QString::fromStdString(oauth_manager->get_last_error())));
    }
}

void PreferencesDialog::onMoveAccountUp() {
    int row = accountsTable_->currentRow();
    if (row <= 0) return;  // Already at top or no selection

    // Get UUIDs of both accounts
    QString uuid1 = accountsTable_->item(row - 1, 1)->data(Qt::UserRole).toString();
    QString uuid2 = accountsTable_->item(row, 1)->data(Qt::UserRole).toString();

    // Load OAuth manager
    auto oauth_manager = Config::create_oauth_manager(
        oauthDirEdit_->text().toStdString());

    if (!oauth_manager) return;

    // Swap priorities
    if (oauth_manager->swap_account_priorities(
            uuid1.toStdString(), uuid2.toStdString())) {
        refreshAccountsList();
        accountsTable_->selectRow(row - 1);
    }
}

void PreferencesDialog::onMoveAccountDown() {
    int row = accountsTable_->currentRow();
    if (row < 0 || row >= accountsTable_->rowCount() - 1) return;  // At bottom or no selection

    // Get UUIDs of both accounts
    QString uuid1 = accountsTable_->item(row, 1)->data(Qt::UserRole).toString();
    QString uuid2 = accountsTable_->item(row + 1, 1)->data(Qt::UserRole).toString();

    // Load OAuth manager
    auto oauth_manager = Config::create_oauth_manager(
        oauthDirEdit_->text().toStdString());

    if (!oauth_manager) return;

    // Swap priorities
    if (oauth_manager->swap_account_priorities(
            uuid1.toStdString(), uuid2.toStdString())) {
        refreshAccountsList();
        accountsTable_->selectRow(row + 1);
    }
}

void PreferencesDialog::refreshAccountsList() {
    // Don't refresh if we're not in OAuth mode
    if (!oauthRadio_ || !oauthRadio_->isChecked()) {
        return;
    }

    try {
        // Save current selection
        int currentRow = accountsTable_->currentRow();
        QString selectedUuid;
        if (currentRow >= 0 && accountsTable_->item(currentRow, 1)) {
            selectedUuid = accountsTable_->item(currentRow, 1)->data(Qt::UserRole).toString();
        }

        // Clear table
        accountsTable_->setRowCount(0);

        // Get config directory
        QString config_dir = oauthDirEdit_->text();
        if (config_dir.isEmpty()) {
            config_dir = "~/.claude_cpp_sdk";  // Default
        }

        // Load OAuth manager
        auto oauth_manager = Config::create_oauth_manager(config_dir.toStdString());

        if (!oauth_manager) {
            return;
        }

        // Get all accounts info
        auto accounts_info = oauth_manager->get_all_accounts_info();

        // Populate table
        for (const auto& info : accounts_info) {
            int row = accountsTable_->rowCount();
            accountsTable_->insertRow(row);

            // Priority
            auto* priorityItem = new QTableWidgetItem(QString::number(info.priority));
            priorityItem->setTextAlignment(Qt::AlignCenter);
            accountsTable_->setItem(row, 0, priorityItem);

            // Account UUID (full UUID displayed)
            auto* uuidItem = new QTableWidgetItem(QString::fromStdString(info.account_uuid));
            uuidItem->setData(Qt::UserRole, QString::fromStdString(info.account_uuid));
            accountsTable_->setItem(row, 1, uuidItem);

            // Status with color
            QString status_text = QString::fromStdString(info.get_status_text());
            auto* statusItem = new QTableWidgetItem(status_text);
            if (info.is_rate_limited) {
                statusItem->setForeground(QBrush(QColor(255, 0, 0)));  // Red
            } else if (info.expires_soon) {
                statusItem->setForeground(QBrush(QColor(255, 165, 0)));  // Orange
            } else {
                statusItem->setForeground(QBrush(QColor(0, 153, 0)));  // Green
            }
            statusItem->setTextAlignment(Qt::AlignCenter);
            accountsTable_->setItem(row, 2, statusItem);

            // Expires in
            QString expires_text = QString::fromStdString(info.get_expires_in_text());
            auto* expiresItem = new QTableWidgetItem(expires_text);
            expiresItem->setTextAlignment(Qt::AlignCenter);
            accountsTable_->setItem(row, 3, expiresItem);

            // Restore selection if this was the selected account
            if (!selectedUuid.isEmpty() &&
                QString::fromStdString(info.account_uuid) == selectedUuid) {
                accountsTable_->selectRow(row);
            }
        }
    } catch (const std::exception& e) {
        // Silently ignore errors during refresh to avoid crashes
        // The table will just remain empty
    }
}

void PreferencesDialog::onAccountSelectionChanged() {
    int row = accountsTable_->currentRow();
    bool hasSelection = (row >= 0);
    int rowCount = accountsTable_->rowCount();

    removeAccountButton_->setEnabled(hasSelection);
    moveUpButton_->setEnabled(hasSelection && row > 0);
    moveDownButton_->setEnabled(hasSelection && row < rowCount - 1);
    refreshAccountsButton_->setEnabled(hasSelection);
}

} // namespace llm_re::ui