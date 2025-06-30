//
// Created by user on 6/29/25.
//

#include "ida_utils.h"
#include "ida_validators.h"

namespace llm_re {

std::vector<ea_t> IDAUtils::get_xrefs_to(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: 0x" + std::to_string(address));
        }

        std::vector<ea_t> result;
        xrefblk_t xb;
        for (bool ok = xb.first_to(address, XREF_ALL); ok; ok = xb.next_to()) {
            result.push_back(xb.from);
        }
        return result;
    });
}

std::vector<ea_t> IDAUtils::get_xrefs_from(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: 0x" + std::to_string(address));
        }

        std::vector<ea_t> result;
        xrefblk_t xb;
        for (bool ok = xb.first_from(address, XREF_ALL); ok; ok = xb.next_from()) {
            result.push_back(xb.to);
        }
        return result;
    });
}

std::string IDAUtils::get_function_disassembly(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: 0x" + std::to_string(address));
        }

        std::string result;
        func_t *func = get_func(address);
        if (!func) {
            return result;
        }

        // Generate disassembly for each instruction in the function
        for (ea_t ea = func->start_ea; ea < func->end_ea; ) {
            // Get the disassembly line
            qstring line;
            if (generate_disasm_line(&line, ea, GENDSM_REMOVE_TAGS | GENDSM_MULTI_LINE)) {
                result += line.c_str();

                // Get repeatable comment
                qstring rpt_cmt;
                if (get_cmt(&rpt_cmt, ea, true)) {  // true = repeatable
                    result += " ; ";
                    result += rpt_cmt.c_str();
                }

                // Get non-repeatable comment
                qstring cmt;
                if (get_cmt(&cmt, ea, false)) {  // false = non-repeatable
                    result += " ; ";
                    result += cmt.c_str();
                }

                result += "\n";
            }
            ea = next_head(ea, func->end_ea);
        }
        return result;
    });
}

std::string IDAUtils::get_function_decompilation(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: 0x" + std::to_string(address));
        }

        std::string result;

        // Initialize Hex-Rays decompiler if not already done
        if (!init_hexrays_plugin()) {
            return result;
        }

        func_t *func = get_func(address);
        if (!func) {
            return result;
        }

        // Decompile the function
        hexrays_failure_t hf;
        cfuncptr_t cfunc = decompile(func, &hf, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
        if (cfunc) {
            qstring str;
            qstring_printer_t printer(cfunc, str, false);  // false = no tags
            cfunc->print_func(printer);

            result = str.c_str();
        }

        return result;
    });
}

ea_t IDAUtils::get_function_address(const std::string& name) {
    return execute_sync_wrapper([&name]() {
        // Validate input
        if (!IDAValidators::is_valid_name(name)) {
            throw std::invalid_argument("Invalid function name: " + name);
        }

        return get_name_ea(BADADDR, name.c_str());
    });
}

std::string IDAUtils::get_function_name(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: 0x" + std::to_string(address));
        }

        std::string result;
        qstring name;
        if (get_func_name(&name, address) > 0) {
            result = name.c_str();
        }
        return result;
    });
}

bool IDAUtils::set_function_name(ea_t address, const std::string& name) {
    return execute_sync_wrapper([address, &name]() {
        // Validate inputs
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: 0x" + std::to_string(address));
        }
        if (!IDAValidators::is_valid_name(name)) {
            throw std::invalid_argument("Invalid function name: " + name);
        }

        func_t *func = get_func(address);
        if (!func) {
            return false;
        }
        return set_name(func->start_ea, name.c_str(), SN_CHECK);
    });
}

std::vector<std::string> IDAUtils::get_function_string_refs(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: 0x" + std::to_string(address));
        }

        std::vector<std::string> result;
        func_t *func = get_func(address);
        if (!func) {
            return result;
        }

        // Iterate through function instructions
        for (ea_t ea = func->start_ea; ea < func->end_ea; ) {
            // Check for data references from this instruction
            xrefblk_t xb;
            for (bool ok = xb.first_from(ea, XREF_DATA); ok; ok = xb.next_from()) {
                // Check if the target is a string
                flags_t flags = get_flags(xb.to);
                if (is_strlit(flags)) {
                    // Get the string content
                    qstring str;
                    size_t len = get_max_strlit_length(xb.to, STRTYPE_C);
                    if (get_strlit_contents(&str, xb.to, len, STRTYPE_C) > 0) {
                        result.push_back(str.c_str());
                    }
                }
            }
            ea = next_head(ea, func->end_ea);
        }

        // Remove duplicates
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());

        return result;
    });
}

std::vector<ea_t> IDAUtils::get_function_data_refs(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: 0x" + std::to_string(address));
        }

        std::vector<ea_t> result;
        func_t *func = get_func(address);
        if (!func) {
            return result;
        }

        // Iterate through function instructions
        for (ea_t ea = func->start_ea; ea < func->end_ea; ) {
            // Get data references from this instruction
            xrefblk_t xb;
            for (bool ok = xb.first_from(ea, XREF_DATA); ok; ok = xb.next_from()) {
                result.push_back(xb.to);
            }
            ea = next_head(ea, func->end_ea);
        }

        // Remove duplicates
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());

        return result;
    });
}

std::string IDAUtils::get_data_name(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: 0x" + std::to_string(address));
        }

        std::string result;
        qstring name;
        if (get_name(&name, address) > 0) {
            result = name.c_str();
        }
        return result;
    });
}

bool IDAUtils::set_data_name(ea_t address, const std::string& name) {
    return execute_sync_wrapper([address, &name]() {
        // Validate inputs
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: 0x" + std::to_string(address));
        }
        if (!IDAValidators::is_valid_name(name)) {
            throw std::invalid_argument("Invalid data name: " + name);
        }

        return set_name(address, name.c_str(), SN_CHECK);
    });
}

std::pair<std::string, std::string> IDAUtils::get_data(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate that this is a data address
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: 0x" + std::to_string(address));
        }

        flags_t flags = get_flags(address);
        if (!is_data(flags)) {
            throw std::invalid_argument("Address is not a data location: 0x" + std::to_string(address));
        }

        std::string value;
        std::string type;

        // Check if it's a string
        if (is_strlit(flags)) {
            qstring str;
            size_t len = get_max_strlit_length(address, STRTYPE_C);
            if (get_strlit_contents(&str, address, len, STRTYPE_C) > 0) {
                value = str.c_str();
                type = "string";
            }
        } else {
            // Get the size of the data item
            asize_t item_size = get_item_size(address);
            if (item_size > 0 && item_size <= 1024) { // Reasonable size limit
                // Read raw bytes
                bytevec_t bytes;
                bytes.resize(item_size);
                if (get_bytes(&bytes[0], item_size, address)) {
                    // Convert to hex string
                    std::stringstream ss;
                    ss << std::hex << std::setfill('0');
                    for (size_t i = 0; i < item_size; i++) {
                        ss << std::setw(2) << static_cast<int>(bytes[i]);
                        if (i < item_size - 1) ss << " ";
                    }
                    value = ss.str();
                    type = "bytes";
                }
            } else {
                throw std::runtime_error("Unable to determine data size or size too large");
            }
        }

        if (value.empty()) {
            throw std::runtime_error("Unable to read data at address");
        }

        return std::make_pair(value, type);
    });
}

bool IDAUtils::add_disassembly_comment(ea_t address, const std::string& comment) {
    return execute_sync_wrapper([address, &comment]() {
        // Validate inputs
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: 0x" + std::to_string(address));
        }
        if (comment.length() > 4096) {
            throw std::invalid_argument("Comment too long (max 4096 characters)");
        }

        return set_cmt(address, comment.c_str(), false);
    });
}

bool IDAUtils::add_pseudocode_comment(ea_t address, const std::string& comment) {
    return execute_sync_wrapper([address, &comment]() {
        // Validate inputs
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: 0x" + std::to_string(address));
        }
        if (comment.length() > 4096) {
            throw std::invalid_argument("Comment too long (max 4096 characters)");
        }

        // Initialize Hex-Rays if needed
        if (!init_hexrays_plugin()) {
            return false;
        }

        func_t *func = get_func(address);
        if (!func) {
            return false;
        }

        // Get the decompiled function
        hexrays_failure_t hf;
        cfuncptr_t cfunc = decompile(func, &hf, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
        if (!cfunc) {
            return false;
        }

        // Get existing user comments or create new
        user_cmts_t *cmts = restore_user_cmts(func->start_ea);
        if (!cmts) {
            cmts = user_cmts_new();
        }

        // Create a tree location for the comment
        treeloc_t loc;
        loc.ea = address;
        // Use ITP_SEMI for expression comments, ITP_BLOCK1 for block comments
        loc.itp = ITP_SEMI;

        // Insert the comment
        user_cmts_insert(cmts, loc, comment.c_str());

        // Save the comments
        save_user_cmts(func->start_ea, cmts);
        user_cmts_free(cmts);

        // Mark the database as changed
        cfunc->refresh_func_ctext();

        return true;
    });
}

bool IDAUtils::clear_disassembly_comment(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: 0x" + std::to_string(address));
        }

        return set_cmt(address, "", false);
    });
}

bool IDAUtils::clear_pseudocode_comments(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: 0x" + std::to_string(address));
        }

        // Initialize Hex-Rays if needed
        if (!init_hexrays_plugin()) {
            return false;
        }

        func_t *func = get_func(address);
        if (!func) {
            return false;
        }

        // Clear all user comments for the function
        user_cmts_t *cmts = user_cmts_new();
        save_user_cmts(func->start_ea, cmts);
        user_cmts_free(cmts);

        return true;
    });
}

std::map<std::string, std::vector<std::string>> IDAUtils::get_imports() {
    return execute_sync_wrapper([]() {
        std::map<std::string, std::vector<std::string>> result;

        // Iterate through all import modules
        for (int i = 0; i < get_import_module_qty(); i++) {
            qstring module_name;
            if (!get_import_module_name(&module_name, i)) {
                continue;
            }

            std::vector<std::string> functions;

            // Enumerate all imports from this module
            enum_import_names(i, [](ea_t ea, const char *name, uval_t ord, void *param) -> int {
                auto *funcs = static_cast<std::vector<std::string>*>(param);
                if (name) {
                    funcs->push_back(name);
                }
                return 1; // Continue enumeration
            }, &functions);

            if (!functions.empty()) {
                result[module_name.c_str()] = functions;
            }
        }

        return result;
    });
}

std::vector<std::pair<std::string, ea_t>> IDAUtils::get_exports() {
    return execute_sync_wrapper([]() {
        std::vector<std::pair<std::string, ea_t>> result;

        // Iterate through all exports
        for (size_t i = 0; i < get_entry_qty(); i++) {
            uval_t ord = get_entry_ordinal(i);
            ea_t ea = get_entry(ord);

            qstring name;
            if (get_entry_name(&name, ord)) {
                result.push_back({name.c_str(), ea});
            }
        }

        return result;
    });
}

std::vector<std::string> IDAUtils::get_strings() {
    return execute_sync_wrapper([]() {
        std::vector<std::string> result;

        // Refresh string list
        build_strlist();

        // Get all strings
        size_t qty = get_strlist_qty();
        for (size_t i = 0; i < qty; i++) {
            string_info_t si;
            if (get_strlist_item(&si, i)) {
                qstring str;
                if (get_strlit_contents(&str, si.ea, si.length, si.type) > 0) {
                    result.push_back(str.c_str());
                }
            }
        }

        return result;
    });
}

std::vector<std::string> IDAUtils::search_strings(const std::string& text, bool is_case_sensitive) {
    return execute_sync_wrapper([&text, is_case_sensitive]() {
        // Validate input
        if (text.empty()) {
            throw std::invalid_argument("Search text cannot be empty");
        }
        if (text.length() > 1024) {
            throw std::invalid_argument("Search text too long (max 1024 characters)");
        }

        std::vector<std::string> result;

        // Get all strings first
        std::vector<std::string> all_strings = get_strings();

        // Search through them
        for (const auto& str : all_strings) {
            bool found = false;

            if (is_case_sensitive) {
                found = str.find(text) != std::string::npos;
            } else {
                // Case-insensitive search
                std::string lower_str = str;
                std::string lower_text = text;
                std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
                std::transform(lower_text.begin(), lower_text.end(), lower_text.begin(), ::tolower);
                found = lower_str.find(lower_text) != std::string::npos;
            }

            if (found) {
                result.push_back(str);
            }
        }

        return result;
    });
}

ea_t IDAUtils::get_function_containing(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: 0x" + std::to_string(address));
        }

        func_t *func = get_func(address);
        if (func) {
            return func->start_ea;
        }
        return BADADDR;
    });
}

std::vector<ea_t> IDAUtils::get_all_functions() {
    return execute_sync_wrapper([]() {
        std::vector<ea_t> result;

        // Iterate through all functions
        size_t qty = get_func_qty();
        for (size_t i = 0; i < qty; i++) {
            func_t *func = getn_func(i);
            if (func) {
                result.push_back(func->start_ea);
            }
        }

        return result;
    });
}

} // namespace llm_re