//
// Created by user on 6/29/25.
//

#include "ida_utils.h"

namespace llm_re {

std::vector<ea_t> IDAUtils::get_xrefs_to(ea_t address) {
    return execute_sync_wrapper([address]() {
        std::vector<ea_t> result;
        // TODO: Implement using get_first_cref_to, get_next_cref_to, etc.
        // xrefblk_t xb;
        // for (bool ok = xb.first_to(address, XREF_ALL); ok; ok = xb.next_to()) {
        //     result.push_back(xb.from);
        // }
        return result;
    });
}

std::vector<ea_t> IDAUtils::get_xrefs_from(ea_t address) {
    return execute_sync_wrapper([address]() {
        std::vector<ea_t> result;
        // TODO: Implement using get_first_cref_from, get_next_cref_from, etc.
        return result;
    });
}

std::string IDAUtils::get_function_disassembly(ea_t address) {
    return execute_sync_wrapper([address]() {
        std::string result;
        // TODO: Implement using generate_disasm_line, get_func, etc.
        return result;
    });
}

std::string IDAUtils::get_function_decompilation(ea_t address) {
    return execute_sync_wrapper([address]() {
        std::string result;
        // TODO: Implement using Hex-Rays decompiler API
        return result;
    });
}

ea_t IDAUtils::get_function_address(const std::string& name) {
    return execute_sync_wrapper([&name]() {
        ea_t result = BADADDR;
        // TODO: Implement using get_name_ea_simple
        return result;
    });
}

std::string IDAUtils::get_function_name(ea_t address) {
    return execute_sync_wrapper([address]() {
        std::string result;
        // TODO: Implement using get_name
        return result;
    });
}

bool IDAUtils::set_function_name(ea_t address, const std::string& name) {
    return execute_sync_wrapper([address, &name]() {
        bool result = false;
        // TODO: Implement using set_name
        return result;
    });
}

std::vector<std::string> IDAUtils::get_function_string_refs(ea_t address) {
    return execute_sync_wrapper([address]() {
        std::vector<std::string> result;
        // TODO: Implement by iterating through function and finding string references
        return result;
    });
}

std::vector<ea_t> IDAUtils::get_function_data_refs(ea_t address) {
    return execute_sync_wrapper([address]() {
        std::vector<ea_t> result;
        // TODO: Implement by iterating through function and finding data references
        return result;
    });
}

std::string IDAUtils::get_data_name(ea_t address) {
    return execute_sync_wrapper([address]() {
        std::string result;
        // TODO: Implement using get_name
        return result;
    });
}

bool IDAUtils::set_data_name(ea_t address, const std::string& name) {
    return execute_sync_wrapper([address, &name]() {
        bool result = false;
        // TODO: Implement using set_name
        return result;
    });
}

bool IDAUtils::add_disassembly_comment(ea_t address, const std::string& comment) {
    return execute_sync_wrapper([address, &comment]() {
        bool result = false;
        // TODO: Implement using set_cmt
        return result;
    });
}

bool IDAUtils::add_pseudocode_comment(ea_t address, const std::string& comment) {
    return execute_sync_wrapper([address, &comment]() {
        bool result = false;
        // TODO: Implement using Hex-Rays API
        return result;
    });
}

bool IDAUtils::clear_disassembly_comment(ea_t address) {
    return execute_sync_wrapper([address]() {
        bool result = false;
        // TODO: Implement using set_cmt with empty string
        return result;
    });
}

bool IDAUtils::clear_pseudocode_comments(ea_t address) {
    return execute_sync_wrapper([address]() {
        bool result = false;
        // TODO: Implement using Hex-Rays API
        return result;
    });
}

std::map<std::string, std::vector<std::string>> IDAUtils::get_imports() {
    return execute_sync_wrapper([]() {
        std::map<std::string, std::vector<std::string>> result;
        // TODO: Implement using nimps, get_import_module_name, get_import_ordinal_name
        return result;
    });
}

std::vector<std::pair<std::string, ea_t>> IDAUtils::get_exports() {
    return execute_sync_wrapper([]() {
        std::vector<std::pair<std::string, ea_t>> result;
        // TODO: Implement using get_entry_qty, get_entry_ordinal, get_entry_name
        return result;
    });
}

std::vector<std::string> IDAUtils::get_strings() {
    return execute_sync_wrapper([]() {
        std::vector<std::string> result;
        // TODO: Implement using string window APIs or by scanning segments
        return result;
    });
}

std::vector<std::string> IDAUtils::search_strings(const std::string& text, bool is_case_sensitive) {
    return execute_sync_wrapper([&text, is_case_sensitive]() {
        std::vector<std::string> result;
        // TODO: Implement string search
        return result;
    });
}

ea_t IDAUtils::get_function_containing(ea_t address) {
    return execute_sync_wrapper([address]() {
        ea_t result = BADADDR;
        // TODO: Implement using get_func
        return result;
    });
}

std::vector<ea_t> IDAUtils::get_all_functions() {
    return execute_sync_wrapper([]() {
        std::vector<ea_t> result;
        // TODO: Implement using get_func_qty, getn_func
        return result;
    });
}

} // namespace llm_re

