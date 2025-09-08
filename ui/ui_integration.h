#pragma once

// Use ui_common.h for proper include ordering
#include "ui_common.h"
#include "orchestrator_ui.h"
#include "../core/common_base.h"  // For ask_str

namespace llm_re::ui {

// Helper class to integrate UI with existing orchestrator functionality
class UIIntegration {
public:
    static void show_orchestrator_ui() {
        static OrchestratorUI* ui = nullptr;
        if (!ui) {
            ui = new OrchestratorUI(nullptr);
        }
        ui->show_ui();
    }
    
    static void submit_task_from_ida(orchestrator::Orchestrator* orch) {
        if (!orch) return;
        
        // Use IDA's ask_str dialog to get user input
        qstring user_input;
        if (ask_str(&user_input, 0, "What would you like me to investigate?")) {
            if (!user_input.empty()) {
                orch->process_user_input(user_input.c_str());
            }
        }
    }
};

} // namespace llm_re::ui