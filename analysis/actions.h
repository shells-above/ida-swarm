//
// Created by user on 6/29/25.
//

#ifndef ACTIONS_H
#define ACTIONS_H

#include "core/common.h"
#include "analysis/memory.h"

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

    // Info actions
    json get_function_info(ea_t address);
    json get_data_info(ea_t address, int max_xrefs = 20);
    json dump_data(ea_t address, size_t size, int bytes_per_line = 16);
    json analyze_function(ea_t address, bool include_disasm, bool include_decomp, int max_xrefs);

    // xrefs
    json get_xrefs(ea_t address, int max_results);

    // name/comment actions
    json set_name(ea_t address, const std::string& name);
    json set_comment(ea_t address, const std::string& comment);

    // Binary info actions
    json get_imports(int max_results);
    json get_exports(int max_results);

    // Decompilation-related actions
    json get_function_prototype(ea_t address);
    json set_function_prototype(ea_t address, const std::string& prototype);
    json get_variables(ea_t address);
    json set_variable(ea_t address, const std::string& variable_name, const std::string& new_name, const std::string& new_type);

    // local type actions
    json search_local_types(const std::string& pattern, const std::string& type_kind, int max_results);
    json get_local_type(const std::string& type_name);
    json set_local_type(const std::string& definition, bool replace_existing);

    // Consolidated knowledge management
    json store_analysis(const std::string& key, const std::string& content, std::optional<ea_t> address, const std::string& type, const std::vector<ea_t>& related_addresses);
    json get_analysis(const std::string& key, std::optional<ea_t> address, const std::string& type, const std::string& pattern);

private:
    std::shared_ptr<BinaryMemory> memory;
};

} // namespace llm_re

#endif //ACTIONS_H