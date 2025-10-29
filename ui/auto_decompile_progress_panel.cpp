#include "auto_decompile_progress_panel.h"
#include "../core/logger.h"
#include "../core/ida_utils.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <ida.hpp>
#include <name.hpp>
#include <sstream>
#include <iomanip>

namespace llm_re::ui {

AutoDecompileProgressPanel::AutoDecompileProgressPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    // Status group
    auto* status_group = new QGroupBox("Auto-Decompile Progress", this);
    auto* status_layout = new QVBoxLayout(status_group);

    // Progress bar
    progress_bar_ = new QProgressBar(this);
    progress_bar_->setMinimum(0);
    progress_bar_->setMaximum(100);
    progress_bar_->setValue(0);
    progress_bar_->setTextVisible(true);
    status_layout->addWidget(progress_bar_);

    // Status label
    status_label_ = new QLabel("No analysis running", this);
    status_label_->setStyleSheet("QLabel { color: gray; font-weight: bold; }");
    status_layout->addWidget(status_label_);

    // Stats label
    stats_label_ = new QLabel("", this);
    status_layout->addWidget(stats_label_);

    // Time label
    time_label_ = new QLabel("", this);
    status_layout->addWidget(time_label_);

    // Start/Stop button (toggles between states)
    stop_button_ = new QPushButton("Start Analysis", this);
    stop_button_->setEnabled(true);
    connect(stop_button_, &QPushButton::clicked, this, &AutoDecompileProgressPanel::on_stop_button_clicked);
    status_layout->addWidget(stop_button_);

    layout->addWidget(status_group);

    // Active functions table
    auto* active_group = new QGroupBox("Currently Analyzing", this);
    auto* active_layout = new QVBoxLayout(active_group);

    active_functions_table_ = new QTableWidget(0, 3, this);
    active_functions_table_->setHorizontalHeaderLabels({"Agent ID", "Function", "Address"});
    active_functions_table_->horizontalHeader()->setStretchLastSection(true);
    active_functions_table_->setAlternatingRowColors(true);
    active_functions_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    active_functions_table_->setSelectionBehavior(QAbstractItemView::SelectRows);

    active_layout->addWidget(active_functions_table_);
    layout->addWidget(active_group);

    // Time update timer
    time_update_timer_ = new QTimer(this);
    connect(time_update_timer_, &QTimer::timeout, this, &AutoDecompileProgressPanel::update_time_display);
}

void AutoDecompileProgressPanel::on_analysis_started(size_t total_functions) {
    LOG("AutoDecompileProgressPanel: Analysis started with %zu functions", total_functions);

    analysis_active_ = true;
    current_progress_ = orchestrator::AnalysisProgress();
    current_progress_.total_functions = total_functions;
    current_progress_.start_time = std::chrono::steady_clock::now();

    status_label_->setText("Analysis running...");
    status_label_->setStyleSheet("QLabel { color: green; font-weight: bold; }");

    stop_button_->setText("Stop Analysis");
    stop_button_->setEnabled(true);

    progress_bar_->setValue(0);

    active_functions_table_->setRowCount(0);

    // Start time update timer
    time_update_timer_->start(1000);  // Update every second

    update_stats_display();
}

void AutoDecompileProgressPanel::on_analysis_completed() {
    LOG("AutoDecompileProgressPanel: Analysis completed");

    analysis_active_ = false;

    status_label_->setText("Analysis completed!");
    status_label_->setStyleSheet("QLabel { color: #22863a; font-weight: bold; }");

    stop_button_->setText("Start Analysis");
    stop_button_->setEnabled(true);

    progress_bar_->setValue(100);

    // Stop time update timer
    time_update_timer_->stop();

    update_time_display();  // One final update
    update_stats_display();

    active_functions_table_->setRowCount(0);
}

void AutoDecompileProgressPanel::on_function_completed(ea_t function_ea) {
    // This will be handled by update_progress
}

void AutoDecompileProgressPanel::update_progress(const orchestrator::AnalysisProgress& progress) {
    // Preserve start_time (set in on_analysis_started)
    auto saved_start_time = current_progress_.start_time;

    current_progress_ = progress;
    current_progress_.start_time = saved_start_time;

    // Update progress bar
    progress_bar_->setValue(static_cast<int>(progress.percent_complete));

    // Update active functions table
    active_functions_table_->setRowCount(progress.active_agents.size());

    int row = 0;
    for (const auto& [agent_id, function_ea] : progress.active_agents) {
        active_functions_table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(agent_id)));
        active_functions_table_->setItem(row, 1, new QTableWidgetItem(format_function_name(function_ea)));

        QString addr_str = QString("0x%1").arg(function_ea, 0, 16, QChar('0')).toUpper();
        active_functions_table_->setItem(row, 2, new QTableWidgetItem(addr_str));

        row++;
    }

    update_stats_display();
}

void AutoDecompileProgressPanel::update_stats_display() {
    QString stats = QString("Completed: %1 / %2 | Active: %3 | Pending: %4")
        .arg(current_progress_.completed_functions)
        .arg(current_progress_.total_functions)
        .arg(current_progress_.active_functions)
        .arg(current_progress_.pending_functions);

    stats_label_->setText(stats);
}

void AutoDecompileProgressPanel::update_time_display() {
    if (!analysis_active_ && current_progress_.total_functions == 0) {
        time_label_->setText("");
        return;
    }

    double elapsed = current_progress_.elapsed_seconds();
    QString time_str = QString("Elapsed: %1").arg(format_time(elapsed));

    // Add functions per minute rate if we have completed functions
    if (current_progress_.completed_functions > 0) {
        double rate = current_progress_.functions_per_minute();
        time_str += QString(" (%1 func/min)").arg(QString::number(rate, 'f', 1));
    }

    if (analysis_active_ && current_progress_.completed_functions > 0) {
        double estimated_remaining = current_progress_.estimated_remaining_seconds();
        if (estimated_remaining > 0) {
            time_str += QString(" | Estimated remaining: %1").arg(format_time(estimated_remaining));
        }
    }

    time_label_->setText(time_str);
}

QString AutoDecompileProgressPanel::format_time(double seconds) {
    if (seconds < 60) {
        return QString("%1s").arg(static_cast<int>(seconds));
    } else if (seconds < 3600) {
        int mins = static_cast<int>(seconds) / 60;
        int secs = static_cast<int>(seconds) % 60;
        return QString("%1m %2s").arg(mins).arg(secs);
    } else {
        int hours = static_cast<int>(seconds) / 3600;
        int mins = (static_cast<int>(seconds) % 3600) / 60;
        return QString("%1h %2m").arg(hours).arg(mins);
    }
}

QString AutoDecompileProgressPanel::format_function_name(ea_t function_ea) {
    return IDAUtils::execute_sync_wrapper([&]() -> QString {
        qstring func_name;
        if (get_name(&func_name, function_ea) > 0) {
            return QString::fromUtf8(func_name.c_str());
        }

        // No name, return "sub_ADDRESS"
        return QString("sub_%1").arg(function_ea, 0, 16, QChar('0')).toUpper();
    }, MFF_READ);
}

void AutoDecompileProgressPanel::on_stop_button_clicked() {
    if (stop_button_->text() == "Start Analysis") {
        LOG("AutoDecompileProgressPanel: Start button clicked");
        emit start_analysis_requested();
        // Button state will be updated by on_analysis_started()
    } else {
        LOG("AutoDecompileProgressPanel: Stop button clicked");
        emit stop_analysis_requested();
        stop_button_->setEnabled(false);
        status_label_->setText("Stopping analysis...");
        status_label_->setStyleSheet("QLabel { color: orange; font-weight: bold; }");
    }
}

} // namespace llm_re::ui
