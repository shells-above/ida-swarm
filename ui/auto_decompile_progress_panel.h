#pragma once

#include "ui_common.h"
#include "../orchestrator/auto_decompile_manager.h"
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>

namespace llm_re::ui {

// Progress panel for auto-decompile
class AutoDecompileProgressPanel : public QWidget {
    Q_OBJECT

public:
    explicit AutoDecompileProgressPanel(QWidget* parent = nullptr);

    // Update progress
    void update_progress(const orchestrator::AnalysisProgress& progress);

    // Analysis state changes
    void on_analysis_started(size_t total_functions);
    void on_analysis_completed();
    void on_function_completed(ea_t function_ea);

signals:
    void start_analysis_requested();
    void stop_analysis_requested();

private slots:
    void on_stop_button_clicked();
    void update_time_display();

private:
    // UI components
    QProgressBar* progress_bar_;
    QLabel* status_label_;
    QLabel* stats_label_;
    QLabel* time_label_;
    QPushButton* stop_button_;
    QTableWidget* active_functions_table_;

    // Timer for updating elapsed/remaining time
    QTimer* time_update_timer_;

    // State
    bool analysis_active_ = false;
    orchestrator::AnalysisProgress current_progress_;

    // Helper methods
    QString format_time(double seconds);
    QString format_function_name(ea_t function_ea);
    void update_stats_display();
};

} // namespace llm_re::ui
