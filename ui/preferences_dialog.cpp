// Include order is critical! ui_common.h handles the proper ordering
#include "ui_common.h"
#include "preferences_dialog.h"
#include "device_editor_dialog.h"

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
#include <QFile>
#include <QClipboard>
#include <QApplication>

#include "../sdk/client/client.h"
#include "../sdk/auth/oauth_authorizer.h"
#include "../sdk/messages/types.h"
#include "../core/common_base.h"
#include "../orchestrator/remote_sync_manager.h"
#include "../core/ssh_key_manager.h"

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
    createSwarmTab();
    createLLDBTab();

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
    agentModelCombo_->addItem("Claude Opus 4.5", QVariant::fromValue(static_cast<int>(claude::Model::Opus45)));
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
    graderModelCombo_->addItem("Claude Opus 4.5", QVariant::fromValue(static_cast<int>(claude::Model::Opus45)));
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
    orchestratorModelCombo_->addItem("Claude Opus 4.5", QVariant::fromValue(static_cast<int>(claude::Model::Opus45)));
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

void PreferencesDialog::createSwarmTab() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);

    // Full Binary Analysis settings
    auto* fullAnalysisGroup = new QGroupBox("Full Binary Analysis", widget);
    auto* fullAnalysisLayout = new QFormLayout(fullAnalysisGroup);

    // Max parallel agents
    maxParallelAgentsSpin_ = new QSpinBox(fullAnalysisGroup);
    maxParallelAgentsSpin_->setRange(1, 20);
    maxParallelAgentsSpin_->setValue(10);
    maxParallelAgentsSpin_->setToolTip("Maximum number of agents to run in parallel during full binary analysis");
    fullAnalysisLayout->addRow("Max Parallel Agents:", maxParallelAgentsSpin_);

    // Function Prioritization Heuristics
    auto* prioritizationGroup = new QGroupBox("Function Prioritization Heuristics", widget);
    auto* prioritizationLayout = new QVBoxLayout(prioritizationGroup);

    auto* heuristicsInfo = new QLabel(
        "Configure how functions are prioritized for analysis. "
        "Positive weights = analyze first, negative weights = analyze last.");
    heuristicsInfo->setWordWrap(true);
    heuristicsInfo->setStyleSheet("QLabel { color: gray; margin-bottom: 10px; }");
    prioritizationLayout->addWidget(heuristicsInfo);

    auto* heuristicsForm = new QFormLayout();

    // API Call Heuristic
    auto* apiCallLayout = new QHBoxLayout();
    apiCallHeuristicCheck_ = new QCheckBox("Enable");
    apiCallHeuristicCheck_->setToolTip("Functions calling APIs (fopen, malloc, etc.) - API names reveal purpose");
    apiCallWeightSpin_ = new QDoubleSpinBox();
    apiCallWeightSpin_->setRange(-10.0, 10.0);
    apiCallWeightSpin_->setSingleStep(0.1);
    apiCallWeightSpin_->setValue(2.0);
    apiCallWeightSpin_->setPrefix("Weight: ");
    apiCallLayout->addWidget(apiCallHeuristicCheck_);
    apiCallLayout->addWidget(apiCallWeightSpin_);
    apiCallLayout->addStretch();
    heuristicsForm->addRow("API Calls:", apiCallLayout);

    // Caller Count Heuristic
    auto* callerCountLayout = new QHBoxLayout();
    callerCountHeuristicCheck_ = new QCheckBox("Enable");
    callerCountHeuristicCheck_->setToolTip("Functions called by many others (high-impact utilities)");
    callerCountWeightSpin_ = new QDoubleSpinBox();
    callerCountWeightSpin_->setRange(-10.0, 10.0);
    callerCountWeightSpin_->setSingleStep(0.1);
    callerCountWeightSpin_->setValue(1.5);
    callerCountWeightSpin_->setPrefix("Weight: ");
    callerCountLayout->addWidget(callerCountHeuristicCheck_);
    callerCountLayout->addWidget(callerCountWeightSpin_);
    callerCountLayout->addStretch();
    heuristicsForm->addRow("Caller Count:", callerCountLayout);

    // String-Heavy Heuristic
    auto* stringHeavyLayout = new QHBoxLayout();
    stringHeavyHeuristicCheck_ = new QCheckBox("Enable");
    stringHeavyHeuristicCheck_->setToolTip("Functions with many strings (self-documenting)");
    stringHeavyWeightSpin_ = new QDoubleSpinBox();
    stringHeavyWeightSpin_->setRange(-10.0, 10.0);
    stringHeavyWeightSpin_->setSingleStep(0.1);
    stringHeavyWeightSpin_->setValue(2.0);
    stringHeavyWeightSpin_->setPrefix("Weight: ");
    minStringLengthSpin_ = new QSpinBox();
    minStringLengthSpin_->setRange(5, 100);
    minStringLengthSpin_->setValue(10);
    minStringLengthSpin_->setPrefix("Min length: ");
    minStringLengthSpin_->setToolTip("Minimum string length to count");
    stringHeavyLayout->addWidget(stringHeavyHeuristicCheck_);
    stringHeavyLayout->addWidget(stringHeavyWeightSpin_);
    stringHeavyLayout->addWidget(minStringLengthSpin_);
    stringHeavyLayout->addStretch();
    heuristicsForm->addRow("String-Heavy:", stringHeavyLayout);

    // Function Size Heuristic
    auto* functionSizeLayout = new QHBoxLayout();
    functionSizeHeuristicCheck_ = new QCheckBox("Enable");
    functionSizeHeuristicCheck_->setToolTip("Smaller functions first (easier wins, builds momentum)");
    functionSizeWeightSpin_ = new QDoubleSpinBox();
    functionSizeWeightSpin_->setRange(-10.0, 10.0);
    functionSizeWeightSpin_->setSingleStep(0.1);
    functionSizeWeightSpin_->setValue(1.5);
    functionSizeWeightSpin_->setPrefix("Weight: ");
    functionSizeLayout->addWidget(functionSizeHeuristicCheck_);
    functionSizeLayout->addWidget(functionSizeWeightSpin_);
    functionSizeLayout->addStretch();
    heuristicsForm->addRow("Function Size:", functionSizeLayout);

    // Internal Callee Heuristic
    auto* internalCalleeLayout = new QHBoxLayout();
    internalCalleeHeuristicCheck_ = new QCheckBox("Enable");
    internalCalleeHeuristicCheck_->setToolTip("Functions calling many internals (use NEGATIVE weight for bottom-up analysis)");
    internalCalleeWeightSpin_ = new QDoubleSpinBox();
    internalCalleeWeightSpin_->setRange(-10.0, 10.0);
    internalCalleeWeightSpin_->setSingleStep(0.1);
    internalCalleeWeightSpin_->setValue(-1.0);
    internalCalleeWeightSpin_->setPrefix("Weight: ");
    internalCalleeLayout->addWidget(internalCalleeHeuristicCheck_);
    internalCalleeLayout->addWidget(internalCalleeWeightSpin_);
    internalCalleeLayout->addStretch();
    heuristicsForm->addRow("Internal Callees:", internalCalleeLayout);

    // Entry Point Heuristic
    auto* entryPointLayout = new QHBoxLayout();
    entryPointHeuristicCheck_ = new QCheckBox("Enable");
    entryPointHeuristicCheck_->setToolTip("Entry points (main, DllMain, exports)");
    entryPointWeightSpin_ = new QDoubleSpinBox();
    entryPointWeightSpin_->setRange(-10.0, 10.0);
    entryPointWeightSpin_->setSingleStep(0.1);
    entryPointWeightSpin_->setValue(-1.0);
    entryPointWeightSpin_->setPrefix("Weight: ");
    entryPointModeCombo_ = new QComboBox();
    entryPointModeCombo_->addItem("Bottom-Up (analyze last)", 0);
    entryPointModeCombo_->addItem("Top-Down (analyze first)", 1);
    entryPointModeCombo_->addItem("Neutral (no priority)", 2);
    entryPointModeCombo_->setToolTip("Bottom-Up: good for executables\nTop-Down: good for libraries (exports are the API)");
    entryPointLayout->addWidget(entryPointHeuristicCheck_);
    entryPointLayout->addWidget(entryPointWeightSpin_);
    entryPointLayout->addWidget(entryPointModeCombo_);
    entryPointLayout->addStretch();
    heuristicsForm->addRow("Entry Points:", entryPointLayout);

    prioritizationLayout->addLayout(heuristicsForm);

    layout->addWidget(fullAnalysisGroup);
    layout->addWidget(prioritizationGroup);
    layout->addStretch();

    tabWidget_->addTab(widget, "Swarm");
}

void PreferencesDialog::createLLDBTab() {
    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);

    // Global Settings
    auto* globalGroup = new QGroupBox("Global LLDB Settings", widget);
    auto* globalLayout = new QFormLayout(globalGroup);

    lldbEnabledCheck_ = new QCheckBox("Enable remote debugging", globalGroup);
    lldbEnabledCheck_->setToolTip("Enable LLDB remote debugging capability for agents");
    globalLayout->addRow("", lldbEnabledCheck_);

    lldbPathEdit_ = new QLineEdit(globalGroup);
    lldbPathEdit_->setPlaceholderText("/usr/bin/lldb");
    lldbPathEdit_->setToolTip("Path to the LLDB executable");
    globalLayout->addRow("LLDB Path:", lldbPathEdit_);

    layout->addWidget(globalGroup);

    // Remote Devices
    auto* devicesGroup = new QGroupBox("Remote Debugger Devices", widget);
    auto* devicesLayout = new QVBoxLayout(devicesGroup);

    // Devices table (global registry - enabled/path are workspace-specific)
    devicesTable_ = new QTableWidget(0, 4, devicesGroup);
    devicesTable_->setHorizontalHeaderLabels({"Enabled", "Name", "Host", "SSH Port"});
    devicesTable_->horizontalHeader()->setStretchLastSection(false);
    devicesTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    devicesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    devicesTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    devicesTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    devicesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    devicesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    devicesTable_->setMinimumHeight(200);
    connect(devicesTable_, &QTableWidget::itemSelectionChanged, this, &PreferencesDialog::onDeviceSelectionChanged);
    connect(devicesTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        // Only handle name column (column 1)
        if (item->column() == 1) {
            int row = item->row();
            if (row >= 0 && row < static_cast<int>(devices_.size())) {
                devices_[row].name = item->text().toStdString();
                configModified_ = true;
            }
        }
    });
    devicesLayout->addWidget(devicesTable_);

    // Device management buttons
    auto* buttonLayout = new QHBoxLayout();

    addDeviceButton_ = new QPushButton("Add Device", devicesGroup);
    addDeviceButton_->setToolTip("Add a new remote debugging device");
    connect(addDeviceButton_, &QPushButton::clicked, this, &PreferencesDialog::onAddDevice);
    buttonLayout->addWidget(addDeviceButton_);

    editDeviceButton_ = new QPushButton("Edit Device", devicesGroup);
    editDeviceButton_->setToolTip("Edit selected device");
    editDeviceButton_->setEnabled(false);
    connect(editDeviceButton_, &QPushButton::clicked, this, &PreferencesDialog::onEditDevice);
    buttonLayout->addWidget(editDeviceButton_);

    removeDeviceButton_ = new QPushButton("Remove Device", devicesGroup);
    removeDeviceButton_->setToolTip("Remove selected device");
    removeDeviceButton_->setEnabled(false);
    connect(removeDeviceButton_, &QPushButton::clicked, this, &PreferencesDialog::onRemoveDevice);
    buttonLayout->addWidget(removeDeviceButton_);

    testDeviceButton_ = new QPushButton("Test Selected", devicesGroup);
    testDeviceButton_->setToolTip("Test connectivity to selected device");
    testDeviceButton_->setEnabled(false);
    connect(testDeviceButton_, &QPushButton::clicked, this, &PreferencesDialog::onTestDevice);
    buttonLayout->addWidget(testDeviceButton_);

    testAllDevicesButton_ = new QPushButton("Test All", devicesGroup);
    testAllDevicesButton_->setToolTip("Test connectivity to all enabled devices");
    connect(testAllDevicesButton_, &QPushButton::clicked, this, &PreferencesDialog::onTestAllDevices);
    buttonLayout->addWidget(testAllDevicesButton_);

    testDebugserverButton_ = new QPushButton("Test Debugserver", devicesGroup);
    testDebugserverButton_->setToolTip("Test debugserver startup on selected device (diagnostic)");
    testDebugserverButton_->setEnabled(false);
    connect(testDebugserverButton_, &QPushButton::clicked, this, &PreferencesDialog::onTestDebugserver);
    buttonLayout->addWidget(testDebugserverButton_);

    buttonLayout->addStretch();
    devicesLayout->addLayout(buttonLayout);

    layout->addWidget(devicesGroup);

    // SSH Key Management
    auto* sshKeyGroup = new QGroupBox("SSH Key Setup", widget);
    auto* sshKeyLayout = new QVBoxLayout(sshKeyGroup);

    auto* sshKeyInfo = new QLabel("SSH keys are automatically generated on orchestrator startup. Use the button below to copy the ssh-copy-id command");
    sshKeyInfo->setWordWrap(true);
    sshKeyInfo->setStyleSheet("QLabel { color: gray; margin-bottom: 10px; }");
    sshKeyLayout->addWidget(sshKeyInfo);

    copySSHCopyIdButton_ = new QPushButton("Copy ssh-copy-id Command", sshKeyGroup);
    copySSHCopyIdButton_->setToolTip("Copy ssh-copy-id command to clipboard for selected device");
    sshKeyLayout->addWidget(copySSHCopyIdButton_);

    layout->addWidget(sshKeyGroup);
    layout->addStretch();

    tabWidget_->addTab(widget, "LLDB");
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
    connect(copySSHCopyIdButton_, &QPushButton::clicked, this, &PreferencesDialog::onCopySSHCopyIdCommand);

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
        auto* thread = QThread::create([this, progressDialog, account_uuid]() {
            // Refresh using new Client static method (uses global pool)
            bool success = claude::Client::refresh_account_tokens(account_uuid.toStdString());

            QString error_message;
            if (!success) {
                error_message = "Failed to refresh token - check that credentials exist at ~/.claude_cpp_sdk/credentials.json";
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

    // Swarm settings
    maxParallelAgentsSpin_->setValue(config.swarm.max_parallel_auto_decompile_agents);

    // Heuristic settings
    apiCallHeuristicCheck_->setChecked(config.swarm.enable_api_call_heuristic);
    apiCallWeightSpin_->setValue(config.swarm.api_call_weight);

    callerCountHeuristicCheck_->setChecked(config.swarm.enable_caller_count_heuristic);
    callerCountWeightSpin_->setValue(config.swarm.caller_count_weight);

    stringHeavyHeuristicCheck_->setChecked(config.swarm.enable_string_heavy_heuristic);
    stringHeavyWeightSpin_->setValue(config.swarm.string_heavy_weight);
    minStringLengthSpin_->setValue(config.swarm.min_string_length_for_priority);

    functionSizeHeuristicCheck_->setChecked(config.swarm.enable_function_size_heuristic);
    functionSizeWeightSpin_->setValue(config.swarm.function_size_weight);

    internalCalleeHeuristicCheck_->setChecked(config.swarm.enable_internal_callee_heuristic);
    internalCalleeWeightSpin_->setValue(config.swarm.internal_callee_weight);

    entryPointHeuristicCheck_->setChecked(config.swarm.enable_entry_point_heuristic);
    entryPointWeightSpin_->setValue(config.swarm.entry_point_weight);
    entryPointModeCombo_->setCurrentIndex(static_cast<int>(config.swarm.entry_point_mode));

    // LLDB settings - Global
    lldbEnabledCheck_->setChecked(config.lldb.enabled);
    lldbPathEdit_->setText(QString::fromStdString(config.lldb.lldb_path));

    // LLDB settings - Device Pool (per-workspace)
    loadDevicePool();

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

    // Swarm settings
    config.swarm.max_parallel_auto_decompile_agents = maxParallelAgentsSpin_->value();

    // Heuristic settings
    config.swarm.enable_api_call_heuristic = apiCallHeuristicCheck_->isChecked();
    config.swarm.api_call_weight = apiCallWeightSpin_->value();

    config.swarm.enable_caller_count_heuristic = callerCountHeuristicCheck_->isChecked();
    config.swarm.caller_count_weight = callerCountWeightSpin_->value();

    config.swarm.enable_string_heavy_heuristic = stringHeavyHeuristicCheck_->isChecked();
    config.swarm.string_heavy_weight = stringHeavyWeightSpin_->value();
    config.swarm.min_string_length_for_priority = minStringLengthSpin_->value();

    config.swarm.enable_function_size_heuristic = functionSizeHeuristicCheck_->isChecked();
    config.swarm.function_size_weight = functionSizeWeightSpin_->value();

    config.swarm.enable_internal_callee_heuristic = internalCalleeHeuristicCheck_->isChecked();
    config.swarm.internal_callee_weight = internalCalleeWeightSpin_->value();

    config.swarm.enable_entry_point_heuristic = entryPointHeuristicCheck_->isChecked();
    config.swarm.entry_point_weight = entryPointWeightSpin_->value();
    config.swarm.entry_point_mode = static_cast<Config::SwarmSettings::EntryPointMode>(entryPointModeCombo_->currentIndex());

    // LLDB settings - Global
    config.lldb.enabled = lldbEnabledCheck_->isChecked();
    config.lldb.lldb_path = lldbPathEdit_->text().toStdString();

    // Save to file
    config.save();

    // LLDB settings - Device Pool (per-workspace)
    saveDevicePool();
    
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
            client = std::make_unique<claude::Client>(
                claude::AuthMethod::API_KEY,
                apiKey,
                baseUrl
            );
        } else {
            // OAuth authentication
            std::string oauthDir = oauthDirEdit_->text().toStdString();
            if (oauthDir.empty()) {
                apiStatusLabel_->setText("✗ OAuth directory is empty");
                apiStatusLabel_->setStyleSheet("QLabel { color: red; }");
                testApiButton_->setEnabled(true);
                return;
            }
            
            // Try to create OAuth client (uses global pool - no manager needed!)
            // Note: OAuth config dir is now ignored (always uses ~/.claude_cpp_sdk)
            if (!claude::Client::has_oauth_credentials()) {
                apiStatusLabel_->setText("✗ No OAuth credentials found at ~/.claude_cpp_sdk/credentials.json");
                apiStatusLabel_->setStyleSheet("QLabel { color: red; }");
                testApiButton_->setEnabled(true);
                return;
            }

            try {
                client = std::make_unique<claude::Client>(
                    claude::AuthMethod::OAUTH,
                    "",  // Credential not needed for OAuth
                    baseUrl
                );
            } catch (const std::exception& e) {
                apiStatusLabel_->setText("✗ Failed to initialize OAuth client: " +
                                        QString::fromStdString(e.what()));
                apiStatusLabel_->setStyleSheet("QLabel { color: red; }");
                testApiButton_->setEnabled(true);
                return;
            }
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

    // Use global OAuth pool (no manager needed)
    claude::auth::OAuthAccountPool pool;  // Uses default ~/.claude_cpp_sdk

    if (!pool.load_from_disk()) {
        QMessageBox::warning(this, "Error", "Failed to load OAuth credentials");
        return;
    }

    // Remove account
    if (pool.remove_account(account_uuid.toStdString())) {
        if (pool.save_to_disk()) {
            refreshAccountsList();
        } else {
            QMessageBox::warning(this, "Error", "Failed to save credentials after removal");
        }
    } else {
        QMessageBox::warning(this, "Error", "Failed to remove account");
    }
}

void PreferencesDialog::onMoveAccountUp() {
    int row = accountsTable_->currentRow();
    if (row <= 0) return;  // Already at top or no selection

    // Get UUIDs of both accounts
    QString uuid1 = accountsTable_->item(row - 1, 1)->data(Qt::UserRole).toString();
    QString uuid2 = accountsTable_->item(row, 1)->data(Qt::UserRole).toString();

    // Use global OAuth pool (no manager needed)
    claude::auth::OAuthAccountPool pool;
    if (!pool.load_from_disk()) return;

    // Swap priorities
    if (pool.swap_priorities(uuid1.toStdString(), uuid2.toStdString())) {
        if (pool.save_to_disk()) {
            refreshAccountsList();
            accountsTable_->selectRow(row - 1);
        }
    }
}

void PreferencesDialog::onMoveAccountDown() {
    int row = accountsTable_->currentRow();
    if (row < 0 || row >= accountsTable_->rowCount() - 1) return;  // At bottom or no selection

    // Get UUIDs of both accounts
    QString uuid1 = accountsTable_->item(row, 1)->data(Qt::UserRole).toString();
    QString uuid2 = accountsTable_->item(row + 1, 1)->data(Qt::UserRole).toString();

    // Use global OAuth pool (no manager needed)
    claude::auth::OAuthAccountPool pool;
    if (!pool.load_from_disk()) return;

    // Swap priorities
    if (pool.swap_priorities(uuid1.toStdString(), uuid2.toStdString())) {
        if (pool.save_to_disk()) {
            refreshAccountsList();
            accountsTable_->selectRow(row + 1);
        }
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

        // Use global OAuth pool (no manager needed - always uses ~/.claude_cpp_sdk)
        claude::auth::OAuthAccountPool pool;

        if (!pool.load_from_disk()) {
            return;  // No credentials file yet
        }

        // Get all accounts info
        auto accounts_info = pool.get_all_accounts_info();

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

// LLDB global device registry helpers
void PreferencesDialog::loadDevicePool() {
    devices_.clear();

    // Load from GLOBAL config (not workspace)
    const Config& config = Config::instance();

    for (const auto& global_device : config.lldb.devices) {
        RemoteDevice device;
        device.id = global_device.id;
        device.name = global_device.name;
        device.host = global_device.host;
        device.ssh_port = global_device.ssh_port;
        device.ssh_user = global_device.ssh_user;
        // debugserver_port will be auto-assigned from IRC port at runtime

        // Copy cached device info
        if (global_device.device_info) {
            DeviceInfo info;
            info.udid = global_device.device_info->udid;
            info.model = global_device.device_info->model;
            info.ios_version = global_device.device_info->ios_version;
            info.name = global_device.device_info->name;
            device.device_info = info;
        }

        // NOTE: enabled and remote_binary_path are workspace-specific
        // Load them from workspace config if available, otherwise default to disabled
        device.enabled = false;  // Default to disabled for new workspaces
        device.remote_binary_path = "";

        // Try to load workspace-specific settings
        std::string workspace_config_path = getWorkspaceConfigPath().toStdString();
        if (!workspace_config_path.empty()) {
            std::ifstream workspace_file(workspace_config_path + "/lldb_config.json");
            if (workspace_file) {
                try {
                    json workspace_json;
                    workspace_file >> workspace_json;
                    if (workspace_json.contains("device_overrides") &&
                        workspace_json["device_overrides"].contains(device.id)) {
                        const auto& override = workspace_json["device_overrides"][device.id];
                        device.enabled = override.value("enabled", false);
                        device.remote_binary_path = override.value("remote_binary_path", "");
                    }
                } catch (const std::exception& e) {
                    LOG("PreferencesDialog: Error loading workspace config for device %s: %s\n",
                        device.id.c_str(), e.what());
                }
            }
        }

        devices_.push_back(device);
    }

    refreshDevicesTable();
}

void PreferencesDialog::saveDevicePool() {
    // Save to GLOBAL config (connection info only)
    Config& config = Config::instance();
    config.lldb.devices.clear();

    for (const auto& device : devices_) {
        Config::LLDBSettings::GlobalDevice global_device;
        global_device.id = device.id;
        global_device.name = device.name;
        global_device.host = device.host;
        global_device.ssh_port = device.ssh_port;
        global_device.ssh_user = device.ssh_user;
        // debugserver_port not saved - auto-derived from IRC port at runtime

        // Save cached device info if available
        if (device.device_info) {
            Config::LLDBSettings::GlobalDevice::DeviceInfo info;
            info.udid = device.device_info->udid;
            info.model = device.device_info->model;
            info.ios_version = device.device_info->ios_version;
            info.name = device.device_info->name;
            global_device.device_info = info;
        }

        config.lldb.devices.push_back(global_device);
    }

    // Save global config to disk
    try {
        config.save();
        LOG("LLM RE: Global device registry saved successfully\n");
    } catch (const std::exception& e) {
        LOG("LLM RE: Error saving global device registry: %s\n", e.what());
    }

    // Save workspace-specific settings (enabled + remote_binary_path)
    std::string workspace_config_path = getWorkspaceConfigPath().toStdString();
    LOG("PreferencesDialog: Workspace config path: %s\n", workspace_config_path.c_str());

    if (!workspace_config_path.empty()) {
        // Ensure workspace directory exists
        qmkdir(workspace_config_path.c_str(), 0755);

        json workspace_config;
        json device_overrides = json::object();

        for (const auto& device : devices_) {
            json override;
            override["enabled"] = device.enabled;
            override["remote_binary_path"] = device.remote_binary_path;
            device_overrides[device.id] = override;
            LOG("PreferencesDialog: Saving device %s - enabled=%d, path=%s\n",
                device.id.c_str(), device.enabled, device.remote_binary_path.c_str());
        }

        workspace_config["device_overrides"] = device_overrides;

        // Write to workspace config
        try {
            std::string config_file_path = workspace_config_path + "/lldb_config.json";
            LOG("PreferencesDialog: Writing to %s\n", config_file_path.c_str());

            std::ofstream workspace_file(config_file_path);
            if (workspace_file) {
                workspace_file << workspace_config.dump(4);
                workspace_file.close();
                LOG("LLM RE: Workspace device config saved to %s\n", config_file_path.c_str());
            } else {
                LOG("LLM RE: Error: Could not open workspace config file for writing: %s\n", config_file_path.c_str());
            }
        } catch (const std::exception& e) {
            LOG("LLM RE: Error saving workspace device config: %s\n", e.what());
        }
    } else {
        LOG("PreferencesDialog: Warning - workspace config path is empty, not saving workspace-specific settings\n");
    }
}

QString PreferencesDialog::getWorkspaceConfigPath() const {
    // Compute workspace path the same way orchestrator does
    // Format: /tmp/ida_swarm_workspace/<binary_name>

    // Get current binary name from IDA
    char binary_path[QMAXPATH];
    get_input_file_path(binary_path, sizeof(binary_path));

    // Extract just the filename without path
    const char* binary_name = qbasename(binary_path);

    if (!binary_name || binary_name[0] == '\0') {
        LOG("PreferencesDialog: Warning - Could not determine binary name\n");
        return QString();
    }

    // Build workspace path: /tmp/ida_swarm_workspace/<binary_name>
    return QString("/tmp/ida_swarm_workspace/%1").arg(binary_name);
}

// LLDB device management slots
void PreferencesDialog::refreshDevicesTable() {
    devicesTable_->setRowCount(0);

    for (size_t i = 0; i < devices_.size(); ++i) {
        const auto& device = devices_[i];

        int row = devicesTable_->rowCount();
        devicesTable_->insertRow(row);

        // Enabled - use a checkbox widget
        auto* enabledWidget = new QWidget();
        auto* enabledLayout = new QHBoxLayout(enabledWidget);
        auto* enabledCheck = new QCheckBox();
        enabledCheck->setChecked(device.enabled);
        enabledCheck->setProperty("row", row);
        connect(enabledCheck, &QCheckBox::toggled, this, [this, i](bool checked) {
            if (i < devices_.size()) {
                devices_[i].enabled = checked;
                configModified_ = true;
            }
        });
        enabledLayout->addWidget(enabledCheck);
        enabledLayout->setAlignment(Qt::AlignCenter);
        enabledLayout->setContentsMargins(0, 0, 0, 0);
        devicesTable_->setCellWidget(row, 0, enabledWidget);

        // Name - editable
        auto* nameItem = new QTableWidgetItem(QString::fromStdString(device.name));
        nameItem->setFlags(nameItem->flags() | Qt::ItemIsEditable);
        devicesTable_->setItem(row, 1, nameItem);

        // Host - read-only
        auto* hostItem = new QTableWidgetItem(QString::fromStdString(device.host));
        hostItem->setFlags(hostItem->flags() & ~Qt::ItemIsEditable);
        devicesTable_->setItem(row, 2, hostItem);

        // SSH Port - read-only
        auto* sshPortItem = new QTableWidgetItem(QString::number(device.ssh_port));
        sshPortItem->setFlags(sshPortItem->flags() & ~Qt::ItemIsEditable);
        devicesTable_->setItem(row, 3, sshPortItem);

        // debugserver_port removed - auto-derived from IRC port at runtime
    }
}

void PreferencesDialog::onDeviceSelectionChanged() {
    bool hasSelection = !devicesTable_->selectedItems().isEmpty();
    editDeviceButton_->setEnabled(hasSelection);
    removeDeviceButton_->setEnabled(hasSelection);
    testDeviceButton_->setEnabled(hasSelection);
    testDebugserverButton_->setEnabled(hasSelection);
}

void PreferencesDialog::onAddDevice() {
    DeviceEditorDialog dialog(this, nullptr);
    if (dialog.exec() == QDialog::Accepted) {
        devices_.push_back(dialog.get_device());
        refreshDevicesTable();
        configModified_ = true;
    }
}

void PreferencesDialog::onEditDevice() {
    int row = devicesTable_->currentRow();
    if (row < 0 || row >= static_cast<int>(devices_.size())) return;

    DeviceEditorDialog dialog(this, &devices_[row]);
    if (dialog.exec() == QDialog::Accepted) {
        devices_[row] = dialog.get_device();
        refreshDevicesTable();
        configModified_ = true;
    }
}

void PreferencesDialog::onRemoveDevice() {
    int row = devicesTable_->currentRow();
    if (row < 0 || row >= static_cast<int>(devices_.size())) return;

    auto reply = QMessageBox::question(this, "Remove Device",
        QString("Are you sure you want to remove device '%1'?")
            .arg(QString::fromStdString(devices_[row].name)),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        devices_.erase(devices_.begin() + row);
        refreshDevicesTable();
        configModified_ = true;
    }
}

void PreferencesDialog::onTestDevice() {
    int row = devicesTable_->currentRow();
    if (row < 0 || row >= static_cast<int>(devices_.size())) return;

    const auto& device = devices_[row];

    QApplication::setOverrideCursor(Qt::WaitCursor);

    orchestrator::RemoteConfig remote_cfg;
    remote_cfg.host = device.host;
    remote_cfg.ssh_port = device.ssh_port;
    remote_cfg.ssh_user = device.ssh_user;
    remote_cfg.debugserver_port = 0;  // Not needed for SSH-only test

    auto result = orchestrator::RemoteSyncManager::validate_connectivity(remote_cfg);

    QApplication::restoreOverrideCursor();

    if (result.is_valid()) {
        QMessageBox::information(this, "Connection Test Passed",
            QString("✅ Device '%1' SSH connection successful!\n\n"
                    "The device is ready for debugging.\n"
                    "Debugserver will be started automatically when needed.")
                .arg(QString::fromStdString(device.name)));
    } else {
        QMessageBox::warning(this, "Connection Test Failed",
            QString("Device '%1' SSH connection test failed:\n\n%2")
                .arg(QString::fromStdString(device.name))
                .arg(QString::fromStdString(result.error_message)));
    }
}

void PreferencesDialog::onTestDebugserver() {
    int row = devicesTable_->currentRow();
    if (row < 0 || row >= static_cast<int>(devices_.size())) return;

    const auto& device = devices_[row];

    QApplication::setOverrideCursor(Qt::WaitCursor);

    QString report;
    bool success = true;

    // Connect via SSH
    orchestrator::SSH2SessionGuard ssh;
    std::string error;
    if (!ssh.connect(device.host, device.ssh_port, device.ssh_user, error)) {
        QApplication::restoreOverrideCursor();
        QMessageBox::critical(this, "Debugserver Test Failed",
            QString("Failed to connect via SSH:\n\n%1")
                .arg(QString::fromStdString(error)));
        return;
    }

    report += "=== Debugserver Diagnostic Test ===\n\n";
    report += QString("Device: %1\n").arg(QString::fromStdString(device.name));
    report += QString("Host: %1:%2\n\n").arg(QString::fromStdString(device.host)).arg(device.ssh_port);

    // Test 1: Check PATH
    report += "[1] Checking PATH...\n";
    error.clear();
    std::string path_output = ssh.exec("echo $PATH", error);
    if (!error.empty()) {
        report += QString("    ❌ ERROR: %1\n\n").arg(QString::fromStdString(error));
        success = false;
    } else {
        report += QString("    PATH = %1\n\n").arg(QString::fromStdString(path_output));
    }

    // Test 2: Check debugserver location
    report += "[2] Finding debugserver...\n";
    error.clear();
    std::string which_output = ssh.exec("which debugserver", error);
    if (!error.empty()) {
        report += QString("    ❌ ERROR: %1\n\n").arg(QString::fromStdString(error));
        success = false;
    } else {
        report += QString("    ✅ debugserver: %1\n\n").arg(QString::fromStdString(which_output));
    }

    // Test 3: Check nohup location
    report += "[3] Finding nohup...\n";
    error.clear();
    std::string nohup_output = ssh.exec("which nohup", error);
    if (!error.empty()) {
        report += QString("    ❌ ERROR: %1\n\n").arg(QString::fromStdString(error));
        success = false;
    } else {
        report += QString("    ✅ nohup: %1\n\n").arg(QString::fromStdString(nohup_output));
    }

    // Test 4: Check if test binary exists (using configured remote_binary_path)
    report += "[4] Checking test binary...\n";
    if (device.remote_binary_path.empty()) {
        report += "    ❌ ERROR: remote_binary_path is not configured for this device\n";
        report += "    Please set the path to the target binary you want to debug\n\n";
        success = false;
    } else {
        report += QString("    Configured path: %1\n").arg(QString::fromStdString(device.remote_binary_path));
        error.clear();
        std::string ls_check = ssh.exec(std::format("ls -la \"{}\" 2>&1", device.remote_binary_path), error);
        if (!error.empty()) {
            report += QString("    ❌ ERROR: %1\n\n").arg(QString::fromStdString(error));
            success = false;
        } else if (ls_check.find("No such file") != std::string::npos) {
            report += QString("    ❌ ERROR: Binary does not exist at specified path\n");
            report += QString("    %1\n\n").arg(QString::fromStdString(ls_check));
            success = false;
        } else {
            report += QString("    ✅ Binary exists: %1\n\n").arg(QString::fromStdString(ls_check));
        }
    }

    // Test 5: Test redirection without nohup first
    report += "[5] Testing redirection (without nohup)...\n";
    error.clear();
    ssh.exec("rm -f /tmp/redir_test.log", error);  // Clean up
    std::string redir_test = ssh.exec("echo 'test output' > /tmp/redir_test.log 2>&1; cat /tmp/redir_test.log", error);
    if (!error.empty()) {
        report += QString("    ❌ ERROR: %1\n\n").arg(QString::fromStdString(error));
    } else {
        report += QString("    Redirection works: %1\n\n").arg(QString::fromStdString(redir_test));
    }

    // Test 6: Test debugserver WITHOUT nohup or backgrounding
    report += "[6] Testing debugserver directly (foreground, 2sec timeout)...\n";
    int test_port = 9999;

    // Use configured remote_binary_path, or skip test if not set
    if (device.remote_binary_path.empty()) {
        report += "    ⊘ SKIPPED: remote_binary_path not configured\n\n";
    } else {
        std::string test_binary = device.remote_binary_path;

        error.clear();
        ssh.exec("rm -f /tmp/debugserver_direct.log", error);  // Clean up

        // Run debugserver in foreground with timeout, capture output
        std::string direct_cmd = std::format(
            "timeout 2 debugserver 0.0.0.0:{} \"{}\" > /tmp/debugserver_direct.log 2>&1; cat /tmp/debugserver_direct.log",
            test_port,
            test_binary
        );
        std::string direct_output = ssh.exec(direct_cmd, error);
        if (!error.empty()) {
            report += QString("    Command error: %1\n").arg(QString::fromStdString(error));
            report += QString("    Output: %1\n\n").arg(QString::fromStdString(direct_output));
        } else {
            report += QString("    Output captured:\n    %1\n\n").arg(QString::fromStdString(direct_output));
        }
    }

    // Test 7: Try with nohup + backgrounding
    report += "[7] Testing debugserver with nohup + background...\n";

    if (device.remote_binary_path.empty()) {
        report += "    ⊘ SKIPPED: remote_binary_path not configured\n\n";
    } else {
        std::string test_binary = device.remote_binary_path;

        error.clear();
        ssh.exec("rm -f /tmp/debugserver_test.log", error);  // Clean up

        std::string start_cmd = std::format(
            "nohup debugserver 0.0.0.0:{} \"{}\" > /tmp/debugserver_test.log 2>&1 & echo $!",
            test_port,
            test_binary
        );
        std::string pid_output = ssh.exec(start_cmd, error);

        if (!error.empty()) {
            report += QString("    ❌ Failed to start: %1\n\n").arg(QString::fromStdString(error));
            success = false;
        } else {
            int debugserver_pid = -1;
            try {
                pid_output.erase(pid_output.find_last_not_of(" \n\r\t") + 1);
                debugserver_pid = std::stoi(pid_output);
                report += QString("    Debugserver PID: %1\n").arg(debugserver_pid);

                // Wait a moment
                std::this_thread::sleep_for(std::chrono::milliseconds(500));

                // Check if still running
                error.clear();
                std::string ps_output = ssh.exec(std::format("ps -p {} -o comm=", debugserver_pid), error);
                if (!error.empty() || ps_output.find("debugserver") == std::string::npos) {
                    report += QString("    ❌ Debugserver not running (crashed immediately)\n");
                    report += QString("    ps output: '%1'\n").arg(QString::fromStdString(ps_output));

                    // Fetch log
                    error.clear();
                    std::string log_output = ssh.exec("cat /tmp/debugserver_test.log 2>&1", error);
                    if (!error.empty()) {
                        report += QString("    Failed to fetch log: %1\n\n").arg(QString::fromStdString(error));
                    } else if (log_output.empty()) {
                        report += QString("    Log is empty (debugserver never ran)\n\n");
                    } else {
                        report += QString("    Log:\n    %1\n\n").arg(QString::fromStdString(log_output));
                    }
                    success = false;
                } else {
                    report += QString("    ✅ Debugserver running: %1\n").arg(QString::fromStdString(ps_output));

                    // Cleanup
                    error.clear();
                    ssh.exec(std::format("kill -9 {}", debugserver_pid), error);
                    if (!error.empty()) {
                        report += QString("    WARNING: Failed to kill debugserver: %1\n\n").arg(QString::fromStdString(error));
                    } else {
                        report += QString("    ✅ Debugserver killed successfully\n\n");
                    }
                }
            } catch (const std::exception& e) {
                report += QString("    ❌ Failed to parse PID from: '%1'\n\n").arg(QString::fromStdString(pid_output));
                success = false;
            }
        }
    }

    QApplication::restoreOverrideCursor();

    if (success) {
        report += "=== All tests passed! ✅ ===\n";
        QMessageBox::information(this, "Debugserver Test Passed", report);
    } else {
        report += "=== Some tests failed ❌ ===\n";
        QMessageBox::warning(this, "Debugserver Test Failed", report);
    }
}

void PreferencesDialog::onTestAllDevices() {
    if (devices_.empty()) {
        QMessageBox::information(this, "No Devices", "No devices configured to test.");
        return;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);

    int passed = 0;
    int failed = 0;
    QString report;

    for (const auto& device : devices_) {
        orchestrator::RemoteConfig remote_cfg;
        remote_cfg.host = device.host;
        remote_cfg.ssh_port = device.ssh_port;
        remote_cfg.ssh_user = device.ssh_user;
        remote_cfg.debugserver_port = 0;  // Not needed for SSH-only test

        auto result = orchestrator::RemoteSyncManager::validate_connectivity(remote_cfg);

        if (result.is_valid()) {
            passed++;
            report += QString("✅ %1: PASSED\n").arg(QString::fromStdString(device.name));
        } else {
            failed++;
            report += QString("❌ %1: FAILED (%2)\n")
                .arg(QString::fromStdString(device.name))
                .arg(QString::fromStdString(result.error_message));
        }
    }

    QApplication::restoreOverrideCursor();

    QMessageBox::information(this, "Test All Devices",
        QString("Test Results:\n\nPassed: %1\nFailed: %2\n\n%3")
            .arg(passed).arg(failed).arg(report));
}

void PreferencesDialog::onCopySSHCopyIdCommand() {
    int row = devicesTable_->currentRow();
    if (row < 0 || row >= static_cast<int>(devices_.size())) {
        QMessageBox::warning(this, "No Device Selected",
            "Please select a device from the table first.");
        return;
    }

    const auto& device = devices_[row];
    std::string key_path = std::string(get_user_idadir()) + "/ida_swarm_ssh_key.pub";

    // Build ssh-copy-id command
    std::string command;
    if (device.ssh_port == 22) {
        command = std::format("ssh-copy-id -i {} {}@{}",
                            key_path, device.ssh_user, device.host);
    } else {
        command = std::format("ssh-copy-id -i {} -p {} {}@{}",
                            key_path, device.ssh_port, device.ssh_user, device.host);
    }

    QApplication::clipboard()->setText(QString::fromStdString(command));
    QMessageBox::information(this, "Command Copied",
        QString("ssh-copy-id command copied to clipboard.\n\n"
                "Run this command in your terminal to copy the SSH key to your remote device.\n"
                "You'll be prompted for the remote device's password."));
}

} // namespace llm_re::ui