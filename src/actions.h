//
// Created by user on 6/29/25.
//

#ifndef ACTIONS_H
#define ACTIONS_H

#include "common.h"
#include "memory.h"

namespace llm_re {

struct AuditEntry {
    std::time_t timestamp;
    std::string action;
    ea_t address;
    std::string old_value;
    std::string new_value;
    bool success;
    std::string error_message;
};

class ActionExecutor {
public:
    using ActionFunction = std::function<json(const json&)>;
    using ActionMap = std::unordered_map<std::string, ActionFunction>;

    explicit ActionExecutor(std::shared_ptr<BinaryMemory> mem);

    // Main action execution interface
    json execute_action(const std::string& action_name, const json& params);

    // IDA Core Actions
    json get_xrefs_to(ea_t address);
    json get_xrefs_from(ea_t address);
    json get_function_disassembly(ea_t address);
    json get_function_decompilation(ea_t address);
    json get_function_address(const std::string& name);
    json get_function_name(ea_t address);
    json set_function_name(ea_t address, const std::string& name);
    json get_function_string_refs(ea_t address);
    json get_function_data_refs(ea_t address);
    json get_data_name(ea_t address);
    json set_data_name(ea_t address, const std::string& name);
    json add_disassembly_comment(ea_t address, const std::string& comment);
    json add_pseudocode_comment(ea_t address, const std::string& comment);
    json clear_disassembly_comment(ea_t address);
    json clear_pseudocode_comments(ea_t address);
    json get_imports();
    json get_exports();
    json search_strings(const std::string& text, bool is_case_sensitive = false);

    // Memory System Actions
    json set_global_note(const std::string& key, const std::string& content);
    json get_global_note(const std::string& key);
    json list_global_notes();
    json search_notes(const std::string& query);
    json set_function_analysis(ea_t address, int level, const std::string& analysis);
    json get_function_analysis(ea_t address, int level = 0);
    json get_memory_context(ea_t address, int radius = 2);
    json get_analyzed_functions();
    json find_functions_by_pattern(const std::string& pattern);
    json get_exploration_frontier();
    json mark_for_analysis(ea_t address, const std::string& reason, int priority = 5);
    json get_analysis_queue();
    json set_current_focus(ea_t address);
    json add_insight(const std::string& type, const std::string& description, const std::vector<ea_t>& related_addresses);
    json get_insights(const std::string& type = "");
    json analyze_cluster(const std::vector<ea_t>& addresses, const std::string& cluster_name, int initial_level);
    json get_cluster_analysis(const std::string& cluster_name);
    json summarize_region(ea_t start_addr, ea_t end_addr);

    // Audit system
    void log_action(const std::string& action, ea_t address = BADADDR,
                   const std::string& old_value = "", const std::string& new_value = "",
                   bool success = true, const std::string& error_msg = "");
    void save_audit_log(const std::string& filename) const;
    std::vector<AuditEntry> get_recent_audit_entries(size_t count = 100) const;

private:
    void register_actions();

    std::shared_ptr<BinaryMemory> memory;
    ActionMap action_map;

    // Audit system
    mutable std::mutex audit_mutex;
    std::vector<AuditEntry> audit_log;
};

} // namespace llm_re

#endif //ACTIONS_H