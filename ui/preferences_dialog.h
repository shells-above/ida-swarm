#pragma once

#include "ui_common.h"
#include "../core/config.h"
#include <QDialog>
#include <QTabWidget>

QT_BEGIN_NAMESPACE
class QRadioButton;
class QLineEdit;
class QComboBox;
class QSpinBox;
class QDoubleSpinBox;
class QSlider;
class QCheckBox;
class QPushButton;
class QLabel;
class QFormLayout;
class QGroupBox;
class QTextEdit;
class QDialogButtonBox;
class QTimer;
QT_END_NAMESPACE

namespace llm_re::ui {

class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(QWidget* parent = nullptr);
    ~PreferencesDialog() = default;

signals:
    void configurationChanged();

private slots:
    // Button handlers
    void onApply();
    void onAccept();
    void onReject();
    void onResetDefaults();
    void onExportConfig();
    void onImportConfig();
    
    // Test buttons
    void onTestAPIConnection();
    void onTestIRCConnection();
    
    // OAuth management
    void onRefreshOAuthToken();
    void updateTokenStatus();
    
    // Radio button handlers
    void onAuthMethodChanged();
    
    // Model selection handlers
    void onAgentModelChanged(int index);
    void onGraderModelChanged(int index);
    void onOrchestratorModelChanged(int index);
    
    // Validation
    void validateApiKey();
    void validateBaseUrl();
    void validateOAuthDir();
    
private:
    // Setup methods
    void setupUi();
    void createApiTab();
    void createModelsTab();
    void createAgentTab();
    void createIrcTab();
    void connectSignals();
    
    // Configuration methods
    void loadConfiguration();
    void saveConfiguration();
    bool validateConfiguration();
    void applyConfiguration();
    bool hasUnsavedChanges();
    
    // Helper methods
    void updateModelDefaults(QComboBox* combo, QSpinBox* tokenSpin, QSpinBox* thinkingSpin);
    void setFieldsEnabled(bool apiKeyMode);
    void showValidationError(const QString& message);
    QString getConfigPath() const;
    
    // Main layout
    QTabWidget* tabWidget_;
    QDialogButtonBox* buttonBox_;
    
    // API Tab widgets
    QRadioButton* apiKeyRadio_;
    QRadioButton* oauthRadio_;
    QLineEdit* apiKeyEdit_;
    QLineEdit* oauthDirEdit_;
    QPushButton* oauthDirBrowse_;
    QLineEdit* baseUrlEdit_;
    QPushButton* testApiButton_;
    QLabel* apiStatusLabel_;
    
    // OAuth token status widgets
    QLabel* tokenExpirationLabel_;
    QPushButton* refreshTokenButton_;
    QTimer* tokenStatusTimer_;
    
    // Models Tab widgets - Agent
    QGroupBox* agentModelGroup_;
    QComboBox* agentModelCombo_;
    QSpinBox* agentMaxTokensSpin_;
    QSpinBox* agentMaxThinkingTokensSpin_;
    QDoubleSpinBox* agentTemperatureSpin_;
    QSlider* agentTemperatureSlider_;
    QCheckBox* agentEnableThinkingCheck_;
    QCheckBox* agentInterleavedThinkingCheck_;
    
    // Models Tab widgets - Grader
    QGroupBox* graderModelGroup_;
    QCheckBox* graderEnabledCheck_;
    QComboBox* graderModelCombo_;
    QSpinBox* graderMaxTokensSpin_;
    QSpinBox* graderMaxThinkingTokensSpin_;
    QSpinBox* graderContextLimitSpin_;
    
    // Models Tab widgets - Orchestrator
    QGroupBox* orchestratorModelGroup_;
    QComboBox* orchestratorModelCombo_;
    QSpinBox* orchestratorMaxTokensSpin_;
    QSpinBox* orchestratorMaxThinkingTokensSpin_;
    QDoubleSpinBox* orchestratorTemperatureSpin_;
    QSlider* orchestratorTemperatureSlider_;
    QCheckBox* orchestratorEnableThinkingCheck_;
    
    // Agent Tab widgets
    QSpinBox* maxIterationsSpin_;
    QSpinBox* contextLimitSpin_;
    QCheckBox* enableDeepAnalysisCheck_;
    QCheckBox* enablePythonToolCheck_;
    QLabel* pythonToolWarning_;
    
    // IRC Tab widgets
    QLineEdit* ircServerEdit_;
    QSpinBox* ircPortSpin_;
    QLineEdit* conflictChannelFormatEdit_;
    QPushButton* testIrcButton_;
    QLabel* ircStatusLabel_;
    QTextEdit* ircFormatHelp_;
    
    // Additional buttons
    QPushButton* exportButton_;
    QPushButton* importButton_;
    QPushButton* resetButton_;
    
    // Configuration tracking
    Config originalConfig_;
    Config currentConfig_;
    bool configModified_ = false;
};

} // namespace llm_re::ui