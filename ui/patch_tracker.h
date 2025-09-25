#pragma once

#include "ui_common.h"
#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <map>
#include <chrono>

namespace llm_re::ui {

// Custom QTableWidgetItem for numeric sorting (from orchestrator_ui.h)
class NumericPatchItem : public QTableWidgetItem {
public:
    NumericPatchItem(const QString& text, int sortValue)
        : QTableWidgetItem(text), sortValue_(sortValue) {}

    bool operator<(const QTableWidgetItem& other) const override {
        const NumericPatchItem* numericOther =
            dynamic_cast<const NumericPatchItem*>(&other);
        if (numericOther) {
            return sortValue_ < numericOther->sortValue_;
        }
        return QTableWidgetItem::operator<(other);
    }

private:
    int sortValue_;
};

// Widget to track and display all patches made by agents
class PatchTracker : public QWidget {
    Q_OBJECT

public:
    explicit PatchTracker(QWidget* parent = nullptr);

    // Add a new patch to the tracker
    void add_patch(const std::string& agent_id,
                   const std::string& address_hex,
                   bool is_assembly,
                   const std::string& original,
                   const std::string& patched,
                   const std::string& description,
                   int64_t timestamp = 0);

    // Clear all patches
    void clear_all();

    // Get total number of patches
    size_t get_patch_count() const { return patches_.size(); }

private:
    struct PatchEntry {
        std::string agent_id;
        std::string address_hex;
        bool is_assembly;
        std::string original;
        std::string patched;
        std::string description;
        std::chrono::system_clock::time_point timestamp;
    };

    // UI Components
    QTableWidget* patch_table_;
    QLabel* total_patches_label_;

    // Data
    std::vector<PatchEntry> patches_;

    // Setup UI
    void setup_ui();
    void refresh_display();
    QString format_timestamp(const std::chrono::system_clock::time_point& time);
    int extract_agent_number(const std::string& agent_id) const;
};

} // namespace llm_re::ui