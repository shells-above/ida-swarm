#include "device_editor_dialog.h"
#include "../orchestrator/remote_device_info.h"
#include "../orchestrator/remote_sync_manager.h"
#include "../core/logger.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QGroupBox>
#include <QDialogButtonBox>
#include <QMessageBox>
#include <QApplication>

namespace llm_re::ui {

DeviceEditorDialog::DeviceEditorDialog(QWidget* parent, RemoteDevice* device)
    : QDialog(parent), is_new_device_(device == nullptr) {

    if (device) {
        device_ = *device;
    } else {
        // Initialize with defaults for new device
        device_.ssh_port = 22;
        device_.ssh_user = "root";
        // debugserver_port will be auto-assigned from IRC port at runtime
        device_.enabled = true;
    }

    setup_ui();
    setWindowTitle(is_new_device_ ? "Add Remote Debugger" : "Edit Remote Debugger");
    setMinimumWidth(500);
}

void DeviceEditorDialog::setup_ui() {
    auto* main_layout = new QVBoxLayout(this);

    // Connection settings group
    auto* connection_group = new QGroupBox("Connection Settings", this);
    auto* connection_layout = new QFormLayout(connection_group);

    name_edit_ = new QLineEdit(QString::fromStdString(device_.name), connection_group);
    name_edit_->setPlaceholderText("My iPhone");
    connection_layout->addRow("Name:", name_edit_);

    host_edit_ = new QLineEdit(QString::fromStdString(device_.host), connection_group);
    host_edit_->setPlaceholderText("192.168.1.100");
    connection_layout->addRow("Host:", host_edit_);

    ssh_port_spin_ = new QSpinBox(connection_group);
    ssh_port_spin_->setRange(1, 65535);
    ssh_port_spin_->setValue(device_.ssh_port);
    connection_layout->addRow("SSH Port:", ssh_port_spin_);

    ssh_user_edit_ = new QLineEdit(QString::fromStdString(device_.ssh_user), connection_group);
    ssh_user_edit_->setPlaceholderText("ro");
    connection_layout->addRow("SSH User:", ssh_user_edit_);

    remote_binary_path_edit_ = new QLineEdit(QString::fromStdString(device_.remote_binary_path), connection_group);
    remote_binary_path_edit_->setPlaceholderText("/var/containers/Bundle/...");
    connection_layout->addRow("Remote Binary Path:", remote_binary_path_edit_);

    enabled_check_ = new QCheckBox("Enabled", connection_group);
    enabled_check_->setChecked(device_.enabled);
    connection_layout->addRow("", enabled_check_);

    main_layout->addWidget(connection_group);

    // Action buttons (Fetch Info, Test Connection)
    auto* action_layout = new QHBoxLayout();

    fetch_info_button_ = new QPushButton("Fetch Device Info", this);
    fetch_info_button_->setToolTip("SSH to device and auto-discover name, UDID, model, iOS version");
    connect(fetch_info_button_, &QPushButton::clicked, this, &DeviceEditorDialog::on_fetch_device_info);
    action_layout->addWidget(fetch_info_button_);

    test_connection_button_ = new QPushButton("Test Connection", this);
    test_connection_button_->setToolTip("Test SSH and debugserver connectivity");
    connect(test_connection_button_, &QPushButton::clicked, this, &DeviceEditorDialog::on_test_connection);
    action_layout->addWidget(test_connection_button_);

    action_layout->addStretch();
    main_layout->addLayout(action_layout);

    // Device info group (read-only, populated by fetch)
    device_info_group_ = new QGroupBox("Device Information (Auto-discovered)", this);
    auto* info_layout = new QFormLayout(device_info_group_);

    udid_label_ = new QLabel(this);
    udid_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    udid_label_->setWordWrap(true);
    udid_label_->setMinimumWidth(350);  // Ensure UDID has enough space to display fully
    info_layout->addRow("UDID:", udid_label_);

    model_label_ = new QLabel(this);
    model_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info_layout->addRow("Model:", model_label_);

    ios_version_label_ = new QLabel(this);
    ios_version_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    info_layout->addRow("iOS Version:", ios_version_label_);

    main_layout->addWidget(device_info_group_);

    // Update device info display
    update_device_info_display();

    // Dialog buttons
    auto* button_box = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Cancel,
        this
    );
    connect(button_box, &QDialogButtonBox::accepted, this, &DeviceEditorDialog::on_accept);
    connect(button_box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    main_layout->addWidget(button_box);
}

void DeviceEditorDialog::update_device_info_display() {
    if (device_.device_info) {
        udid_label_->setText(QString::fromStdString(device_.device_info->udid));
        model_label_->setText(QString::fromStdString(device_.device_info->model));
        ios_version_label_->setText(QString::fromStdString(device_.device_info->ios_version));
        device_info_group_->setEnabled(true);
    } else {
        udid_label_->setText("<not fetched>");
        model_label_->setText("<not fetched>");
        ios_version_label_->setText("<not fetched>");
        device_info_group_->setEnabled(false);
    }
}

void DeviceEditorDialog::on_fetch_device_info() {
    // Validate minimum required fields
    std::string host = host_edit_->text().toStdString();
    if (host.empty()) {
        QMessageBox::warning(this, "Invalid Input", "Please enter a host address first.");
        return;
    }

    fetch_info_button_->setEnabled(false);
    fetch_info_button_->setText("Fetching...");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();

    // Fetch device info
    std::string error;
    auto info = orchestrator::RemoteDeviceInfoFetcher::fetch_device_info(
        host,
        ssh_port_spin_->value(),
        ssh_user_edit_->text().toStdString(),
        error
    );

    QApplication::restoreOverrideCursor();
    fetch_info_button_->setEnabled(true);
    fetch_info_button_->setText("Fetch Device Info");

    if (info) {
        device_.device_info = *info;

        // Auto-populate ID and name if not set
        if (device_.id.empty() || device_.id.find("legacy") != std::string::npos) {
            device_.id = info->udid;
        }

        // Update name if it's still the default (empty or same as host)
        std::string current_name = name_edit_->text().toStdString();
        if (current_name.empty() || current_name == host) {
            device_.name = info->name;
            name_edit_->setText(QString::fromStdString(info->name));
        }

        update_device_info_display();
    } else {
        QMessageBox::warning(this, "Fetch Failed",
            QString("Failed to fetch device information:\n\n%1\n\n"
                    "Make sure:\n"
                    "- Device is reachable\n"
                    "- SSH keys are set up (use 'Copy ssh-copy-id Command' in preferences)\n"
                    "- SSH service is running\n"
                    "- Debugserver is running")
                .arg(QString::fromStdString(error))
        );
    }
}

void DeviceEditorDialog::on_test_connection() {
    // Validate minimum required fields
    std::string host = host_edit_->text().toStdString();
    if (host.empty()) {
    QMessageBox::warning(this, "Invalid Input", "Please enter a host address first.");
        return;
    }

    test_connection_button_->setEnabled(false);
    test_connection_button_->setText("Testing...");
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();

    // Build config for testing
    orchestrator::RemoteConfig remote_cfg;
    remote_cfg.host = host;
    remote_cfg.ssh_port = ssh_port_spin_->value();
    remote_cfg.ssh_user = ssh_user_edit_->text().toStdString();
    remote_cfg.debugserver_port = 0;  // Not needed for SSH-only test

    // Test connectivity
    auto result = orchestrator::RemoteSyncManager::validate_connectivity(remote_cfg);

    QApplication::restoreOverrideCursor();
    test_connection_button_->setEnabled(true);
    test_connection_button_->setText("Test Connection");

    if (result.is_valid()) {
        QMessageBox::information(this, "Connection Test Passed",
            "✅ SSH connection successful!\n\n"
            "The device is ready for debugging.\n"
            "Debugserver will be started automatically when needed.");
    } else {
        QMessageBox::warning(this, "Connection Test Failed",
            QString("SSH connection test failed:\n\n%1")
                .arg(QString::fromStdString(result.error_message))
        );
    }
}

bool DeviceEditorDialog::validate_input() {
    if (host_edit_->text().isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "Host cannot be empty.");
        return false;
    }

    if (ssh_user_edit_->text().isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "SSH user cannot be empty.");
        return false;
    }

    // Validate remote binary path ONLY if device is enabled for this workspace
    // If disabled, user is just adding to global registry for later use
    if (enabled_check_->isChecked()) {
        QString remote_path = remote_binary_path_edit_->text();
        if (remote_path.isEmpty()) {
            QMessageBox::warning(this, "Validation Error",
                "Remote binary path cannot be empty when device is enabled.\n\n"
                "Either:\n"
                "- Set the binary path, OR\n"
                "- Uncheck 'Enabled' to add device to global registry only");
            return false;
        }

        // Check path is absolute (Unix-style paths must start with /)
        if (!remote_path.startsWith('/')) {
            QMessageBox::warning(this, "Validation Error",
                "Remote binary path must be an absolute path (start with '/').\n"
                "Example: /var/mobile/debug/my_binary");
            return false;
        }

        // Check for dangerous characters
        if (remote_path.contains(';') || remote_path.contains('&') ||
            remote_path.contains('|') || remote_path.contains('$') ||
            remote_path.contains('`') || remote_path.contains('\n')) {
            QMessageBox::warning(this, "Validation Error",
                "Remote binary path contains invalid characters.\n"
                "Path should not contain: ; & | $ ` or newlines");
            return false;
        }

        // Check path is not just root
        if (remote_path == "/") {
            QMessageBox::warning(this, "Validation Error",
                "Remote binary path cannot be the root directory.\n"
                "Please specify the full path to the binary file.");
            return false;
        }
    }

    return true;
}

void DeviceEditorDialog::on_accept() {
    if (!validate_input()) {
        return;
    }

    // Update device from UI
    device_.name = name_edit_->text().toStdString();
    device_.host = host_edit_->text().toStdString();
    device_.ssh_port = ssh_port_spin_->value();
    device_.ssh_user = ssh_user_edit_->text().toStdString();
    // debugserver_port removed - auto-derived from IRC port at runtime
    device_.remote_binary_path = remote_binary_path_edit_->text().toStdString();
    device_.enabled = enabled_check_->isChecked();

    // Generate ID if not set
    if (device_.id.empty()) {
        if (device_.device_info) {
            device_.id = device_.device_info->udid;
        } else {
            // Generate ID from host
            device_.id = "device_" + device_.host;
        }
    }

    // Use name from edit field, fallback to host if empty
    if (device_.name.empty()) {
        device_.name = device_.host;
    }

    accept();
}

} // namespace llm_re::ui
