//
// Created by user on 6/29/25.
//

#ifndef ACTIONS_H
#define ACTIONS_H

#include "common.h"
#include "memory.h"

namespace llm_re {

class ActionExecutor {
public:
    using ActionFunction = std::function<json(const json&)>;
    using ActionMap = std::unordered_map<std::string, ActionFunction>;

    explicit ActionExecutor(std::shared_ptr<BinaryMemory> mem);

    // Helpers to convert any LLM address format to ea_t
    static std::vector<ea_t> parse_list_address_param(const json &params, const std::string &key);
    static ea_t parse_single_address_value(const json &param);

    // Consolidated search actions
    json search_functions(const std::string& pattern, bool named_only, int max_results);
    json search_globals(const std::string& pattern, int max_results);
    json search_strings(const std::string& pattern, int min_length, int max_results);

    // Unified info actions
    json get_function_info(ea_t address);
    json get_data_info(ea_t address);
    json dump_data(ea_t address, size_t size, int bytes_per_line = 16);
    json analyze_function(ea_t address, bool include_disasm, bool include_decomp, int max_xrefs);

    // Simplified cross-reference action
    json get_xrefs(ea_t address, int max_results);

    // Unified name/comment actions
    json set_name(ea_t address, const std::string& name);
    json set_comment(ea_t address, const std::string& comment);

    // Binary info actions
    json get_imports(int max_results);
    json get_entry_points(int max_results);

    // Consolidated knowledge management
    json store_analysis(const std::string& key, const std::string& content, std::optional<ea_t> address, const std::string& type, const std::vector<ea_t>& related_addresses);
    json get_analysis(const std::string& key, std::optional<ea_t> address, const std::string& type, const std::string& pattern);

    // Batch operations
    json analyze_functions(const std::vector<ea_t>& addresses, int level, const std::string& group_name);

    // Context and workflow
    json get_analysis_context(std::optional<ea_t> address, int radius);
    json mark_for_analysis(ea_t address, const std::string& reason, int priority);
    json set_current_focus(ea_t address);

private:
    std::shared_ptr<BinaryMemory> memory;
};

} // namespace llm_re

#endif //ACTIONS_H