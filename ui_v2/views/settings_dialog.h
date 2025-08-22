#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"
#include "../../sdk/auth/oauth_manager.h"

namespace llm_re::ui_v2 {

// Settings dialog for configuring the plugin
class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() = default;

    // Load current settings into UI
    void loadSettings();
    
    // Apply settings from UI
    void applySettings();

private:
    void setupUI();
    void createAPITab();
    void createAgentTab();
    void createGraderTab();
    void createUITab();
    void createAdvancedTab();
    void createIRCTab();
    void createOrchestratorTab();
    void createSwarmTab();
    
    // Tab widget
    QTabWidget* tab_widget_ = nullptr;
    
    // API settings widgets
    QCheckBox* use_oauth_check_ = nullptr;
    QLineEdit* oauth_config_dir_edit_ = nullptr;
    QLabel* oauth_status_label_ = nullptr;
    QPushButton* authorize_button_ = nullptr;
    QPushButton* refresh_token_button_ = nullptr;
    QLineEdit* api_key_edit_ = nullptr;
    QLineEdit* base_url_edit_ = nullptr;
    QComboBox* model_combo_ = nullptr;
    QSpinBox* max_tokens_spin_ = nullptr;
    QSpinBox* max_thinking_tokens_spin_ = nullptr;
    QDoubleSpinBox* temperature_spin_ = nullptr;
    QPushButton* test_api_button_ = nullptr;
    QLabel* api_status_label_ = nullptr;
    
    // Agent settings widgets
    QSpinBox* max_iterations_spin_ = nullptr;
    QCheckBox* enable_thinking_check_ = nullptr;
    QCheckBox* enable_interleaved_thinking_check_ = nullptr;
    QCheckBox* enable_deep_analysis_check_ = nullptr;
    QCheckBox* enable_python_tool_check_ = nullptr;
    
    // Grader settings widgets
    QCheckBox* grader_enabled_check_ = nullptr;
    QComboBox* grader_model_combo_ = nullptr;
    QSpinBox* grader_max_tokens_spin_ = nullptr;
    QSpinBox* grader_max_thinking_tokens_spin_ = nullptr;

    // UI settings widgets
    QSpinBox* log_buffer_spin_ = nullptr;
    QCheckBox* auto_scroll_check_ = nullptr;
    QComboBox* theme_combo_ = nullptr;
    QSpinBox* font_size_spin_ = nullptr;
    QCheckBox* show_timestamps_check_ = nullptr;
    QCheckBox* show_tool_details_check_ = nullptr;
    
    // Window management widgets
    QCheckBox* start_minimized_check_ = nullptr;
    QCheckBox* remember_window_state_check_ = nullptr;
    
    // Conversation view widgets
    QCheckBox* auto_save_conversations_check_ = nullptr;
    QSpinBox* auto_save_interval_spin_ = nullptr;
    QComboBox* density_mode_combo_ = nullptr;
    
    // IRC settings widgets
    QLineEdit* irc_server_edit_ = nullptr;
    QSpinBox* irc_port_spin_ = nullptr;
    QLineEdit* irc_conflict_channel_format_edit_ = nullptr;
    QLineEdit* irc_private_channel_format_edit_ = nullptr;
    
    // Orchestrator settings widgets
    QComboBox* orchestrator_model_combo_ = nullptr;
    QSpinBox* orchestrator_max_tokens_spin_ = nullptr;
    QSpinBox* orchestrator_max_thinking_tokens_spin_ = nullptr;
    QDoubleSpinBox* orchestrator_temperature_spin_ = nullptr;
    QCheckBox* orchestrator_enable_thinking_check_ = nullptr;

    // Buttons
    QPushButton* ok_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    QPushButton* apply_button_ = nullptr;
    QPushButton* reset_button_ = nullptr;
    
    // Track changes
    bool has_changes_ = false;
    
    // OAuth manager for this dialog instance
    std::shared_ptr<claude::auth::OAuthManager> oauth_manager_;
    
private:
    bool validateApiKey(const std::string& apiKey);
    bool validateOAuth();

private slots:
    void onTestAPI();
    void checkOAuthStatus();
    void onAuthorize();
    void onRefreshToken();
    void onResetDefaults();
    void onSettingChanged();
    void onOK();
    void onCancel();
    void onApply();
};

} // namespace llm_re::ui_v2