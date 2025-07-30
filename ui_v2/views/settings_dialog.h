#pragma once

#include "../core/ui_v2_common.h"
#include "../core/base_styled_widget.h"

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
    void createUITab();
    void createAdvancedTab();
    
    // Tab widget
    QTabWidget* tab_widget_ = nullptr;
    
    // API settings widgets
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

    // UI settings widgets
    QSpinBox* log_buffer_spin_ = nullptr;
    QCheckBox* auto_scroll_check_ = nullptr;
    QComboBox* theme_combo_ = nullptr;
    QSpinBox* font_size_spin_ = nullptr;
    QCheckBox* show_timestamps_check_ = nullptr;
    QCheckBox* show_tool_details_check_ = nullptr;
    
    // Window management widgets
    QCheckBox* show_tray_icon_check_ = nullptr;
    QCheckBox* minimize_to_tray_check_ = nullptr;
    QCheckBox* close_to_tray_check_ = nullptr;
    QCheckBox* start_minimized_check_ = nullptr;
    QCheckBox* remember_window_state_check_ = nullptr;
    
    // Conversation view widgets
    QCheckBox* auto_save_conversations_check_ = nullptr;
    QSpinBox* auto_save_interval_spin_ = nullptr;
    QComboBox* density_mode_combo_ = nullptr;
    
    // Inspector widgets
    QCheckBox* inspector_follow_cursor_check_ = nullptr;
    QSlider* inspector_opacity_slider_ = nullptr;
    QLabel* inspector_opacity_label_ = nullptr;
    QCheckBox* inspector_auto_hide_check_ = nullptr;
    QSpinBox* inspector_auto_hide_delay_spin_ = nullptr;
    
    // Buttons
    QPushButton* ok_button_ = nullptr;
    QPushButton* cancel_button_ = nullptr;
    QPushButton* apply_button_ = nullptr;
    QPushButton* reset_button_ = nullptr;
    
    // Track changes
    bool has_changes_ = false;
    
private slots:
    void onTestAPI();
    void onResetDefaults();
    void onSettingChanged();
    void onOK();
    void onCancel();
    void onApply();
};

} // namespace llm_re::ui_v2