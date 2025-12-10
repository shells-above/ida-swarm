#pragma once

#include "ui_common.h"
#include "../orchestrator/lldb_manager.h"
#include <QDialog>

QT_BEGIN_NAMESPACE
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QLabel;
class QPushButton;
class QGroupBox;
QT_END_NAMESPACE

namespace llm_re::ui {

/**
 * Dialog for adding or editing a remote debugging device
 */
class DeviceEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit DeviceEditorDialog(QWidget* parent = nullptr, RemoteDevice* device = nullptr);
    ~DeviceEditorDialog() = default;

    // Get the configured device (only valid after accepted)
    RemoteDevice get_device() const { return device_; }

private slots:
    void on_fetch_device_info();
    void on_test_connection();
    void on_accept();
    void update_device_info_display();

private:
    void setup_ui();
    bool validate_input();

    // Input widgets
    QLineEdit* name_edit_;
    QLineEdit* host_edit_;
    QSpinBox* ssh_port_spin_;
    QLineEdit* ssh_user_edit_;
    QLineEdit* remote_binary_path_edit_;
    QCheckBox* enabled_check_;

    // Device info display (read-only)
    QGroupBox* device_info_group_;
    QLabel* udid_label_;
    QLabel* model_label_;
    QLabel* ios_version_label_;

    // Action buttons
    QPushButton* fetch_info_button_;
    QPushButton* test_connection_button_;

    // Device being edited (nullptr if adding new)
    RemoteDevice device_;
    bool is_new_device_;
};

} // namespace llm_re::ui
