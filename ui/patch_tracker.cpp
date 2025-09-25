#include "patch_tracker.h"
#include <QHeaderView>
#include <QFont>
#include <iomanip>
#include <sstream>

namespace llm_re::ui {

PatchTracker::PatchTracker(QWidget* parent)
    : QWidget(parent) {
    setup_ui();
}

void PatchTracker::setup_ui() {
    auto* layout = new QVBoxLayout(this);

    // Header with total patches count
    auto* header_layout = new QHBoxLayout();

    QLabel* title = new QLabel("Binary Patches", this);
    QFont title_font = title->font();
    title_font.setBold(true);
    title_font.setPointSize(title_font.pointSize() + 1);
    title->setFont(title_font);

    total_patches_label_ = new QLabel("Total: 0 patches", this);

    header_layout->addWidget(title);
    header_layout->addStretch();
    header_layout->addWidget(total_patches_label_);

    // Create table
    patch_table_ = new QTableWidget(0, 7, this);
    patch_table_->setHorizontalHeaderLabels(QStringList()
        << "Agent"
        << "Address"
        << "Type"
        << "Original"
        << "Patched"
        << "Description"
        << "Timestamp");

    // Set column widths - make Description wider since they're full sentences
    patch_table_->setColumnWidth(0, 80);   // Agent
    patch_table_->setColumnWidth(1, 100);  // Address
    patch_table_->setColumnWidth(2, 80);   // Type
    patch_table_->setColumnWidth(3, 200);  // Original
    patch_table_->setColumnWidth(4, 200);  // Patched
    patch_table_->setColumnWidth(5, 400);  // Description - wide for full sentences
    patch_table_->setColumnWidth(6, 100);  // Timestamp

    // Enable sorting
    patch_table_->setSortingEnabled(true);

    // Set selection behavior
    patch_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    patch_table_->setAlternatingRowColors(true);

    // Make original and patched columns use monospace font for hex/assembly
    QFont mono_font("Courier New", 9);
    if (!mono_font.exactMatch()) {
        mono_font = QFont("Monospace", 9);
    }

    // Style the table
    patch_table_->setStyleSheet(
        "QTableWidget { background-color: #2b2b2b; }"
        "QTableWidget::item { padding: 4px; }"
        "QHeaderView::section { background-color: #3a3a3a; padding: 4px; }"
    );

    // Make header resize to contents
    patch_table_->horizontalHeader()->setStretchLastSection(true);

    layout->addLayout(header_layout);
    layout->addWidget(patch_table_);
}

void PatchTracker::add_patch(const std::string& agent_id,
                             const std::string& address_hex,
                             bool is_assembly,
                             const std::string& original,
                             const std::string& patched,
                             const std::string& description,
                             int64_t timestamp) {
    // Create patch entry
    PatchEntry entry;
    entry.agent_id = agent_id;
    entry.address_hex = address_hex;
    entry.is_assembly = is_assembly;
    entry.original = original;
    entry.patched = patched;
    entry.description = description;

    if (timestamp > 0) {
        entry.timestamp = std::chrono::system_clock::from_time_t(timestamp);
    } else {
        entry.timestamp = std::chrono::system_clock::now();
    }

    patches_.push_back(entry);

    // Add to table
    int row = patch_table_->rowCount();
    patch_table_->insertRow(row);

    // Agent column with numeric sorting
    int agent_num = extract_agent_number(agent_id);
    auto* agent_item = new NumericPatchItem(QString::fromStdString(agent_id), agent_num);
    patch_table_->setItem(row, 0, agent_item);

    // Address column
    auto* addr_item = new QTableWidgetItem(QString::fromStdString(address_hex));
    addr_item->setFont(QFont("Monospace", 9));
    patch_table_->setItem(row, 1, addr_item);

    // Type column
    QString type_str = is_assembly ? "Assembly" : "Byte";
    auto* type_item = new QTableWidgetItem(type_str);
    if (is_assembly) {
        type_item->setForeground(QColor(52, 152, 219));  // Blue for assembly
    } else {
        type_item->setForeground(QColor(46, 204, 113));  // Green for byte
    }
    patch_table_->setItem(row, 2, type_item);

    // Original column (monospace)
    auto* orig_item = new QTableWidgetItem(QString::fromStdString(original));
    orig_item->setFont(QFont("Monospace", 9));
    orig_item->setToolTip(QString::fromStdString(original));  // Full text in tooltip
    patch_table_->setItem(row, 3, orig_item);

    // Patched column (monospace)
    auto* patch_item = new QTableWidgetItem(QString::fromStdString(patched));
    patch_item->setFont(QFont("Monospace", 9));
    patch_item->setToolTip(QString::fromStdString(patched));  // Full text in tooltip
    patch_table_->setItem(row, 4, patch_item);

    // Description column
    auto* desc_item = new QTableWidgetItem(QString::fromStdString(description));
    desc_item->setToolTip(QString::fromStdString(description));  // Full text in tooltip
    patch_table_->setItem(row, 5, desc_item);

    // Timestamp column
    auto* time_item = new QTableWidgetItem(format_timestamp(entry.timestamp));
    patch_table_->setItem(row, 6, time_item);

    // Update total count
    total_patches_label_->setText(QString("Total: %1 patches").arg(patches_.size()));
}

void PatchTracker::clear_all() {
    patches_.clear();
    patch_table_->setRowCount(0);
    total_patches_label_->setText("Total: 0 patches");
}

QString PatchTracker::format_timestamp(const std::chrono::system_clock::time_point& time) {
    auto time_t = std::chrono::system_clock::to_time_t(time);
    std::tm* tm = std::localtime(&time_t);

    std::stringstream ss;
    ss << std::put_time(tm, "%H:%M:%S");
    return QString::fromStdString(ss.str());
}

int PatchTracker::extract_agent_number(const std::string& agent_id) const {
    if (agent_id.starts_with("agent_")) {
        try {
            return std::stoi(agent_id.substr(6));
        } catch (...) {
            return 999999;
        }
    }
    return 999999;
}

} // namespace llm_re::ui