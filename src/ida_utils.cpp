//
// Created by user on 6/29/25.
//

#include "ida_utils.h"
#include "ida_validators.h"

namespace llm_re {

// Helper function to format addresses as hex strings for error messages
std::string format_address_hex(ea_t address) {
    std::stringstream ss;
    ss << "0x" << std::hex << address;
    return ss.str();
}

std::vector<std::pair<ea_t, std::string>> IDAUtils::get_xrefs_to_with_names(ea_t address) {
    return execute_sync_wrapper([address]() {
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }

        std::vector<std::pair<ea_t, std::string>> result;
        xrefblk_t xb;
        for (bool ok = xb.first_to(address, XREF_ALL); ok; ok = xb.next_to()) {
            qstring name;
            std::string func_name;

            // Try to get function name at the xref source
            if (get_func_name(&name, xb.from) > 0) {
                func_name = name.c_str();
            } else if (get_name(&name, xb.from) > 0) {
                func_name = name.c_str();
            }

            result.push_back({xb.from, func_name});
        }
        return result;
    });
}

std::vector<std::pair<ea_t, std::string>> IDAUtils::get_xrefs_from_with_names(ea_t address) {
    return execute_sync_wrapper([address]() {
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }

        std::vector<std::pair<ea_t, std::string>> result;
        xrefblk_t xb;
        for (bool ok = xb.first_from(address, XREF_ALL); ok; ok = xb.next_from()) {
            qstring name;
            std::string func_name;

            // Try to get function name at the xref target
            if (get_func_name(&name, xb.to) > 0) {
                func_name = name.c_str();
            } else if (get_name(&name, xb.to) > 0) {
                func_name = name.c_str();
            }

            result.push_back({xb.to, func_name});
        }
        return result;
    });
}

std::string IDAUtils::get_function_disassembly(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
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
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
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
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
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
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
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
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
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
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
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
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
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
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
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
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }

        flags_t flags = get_flags(address);
        if (!is_data(flags)) {
            throw std::invalid_argument("Address is not a data location: " + format_address_hex(address));
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
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
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
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
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
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }

        return set_cmt(address, "", false);
    });
}

bool IDAUtils::clear_pseudocode_comments(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
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


std::vector<std::pair<ea_t, std::string>> IDAUtils::get_named_functions() {
    return execute_sync_wrapper([]() {
        std::vector<std::pair<ea_t, std::string>> result;

        // Iterate through all functions
        size_t qty = get_func_qty();
        for (size_t i = 0; i < qty; i++) {
            func_t *func = getn_func(i);
            if (func) {
                qstring name;
                if (get_func_name(&name, func->start_ea) > 0) {
                    std::string func_name = name.c_str();

                    // Filter out auto-generated names
                    if (func_name.substr(0, 4) != "sub_" &&
                        func_name.substr(0, 2) != "j_" &&
                        func_name.substr(0, 4) != "loc_" &&
                        func_name.substr(0, 7) != "nullsub_" &&
                        func_name.substr(0, 4) != "def_") {
                        result.push_back({func->start_ea, func_name});
                    }
                }
            }
        }

        // Sort by address
        std::sort(result.begin(), result.end());

        return result;
    });
}

std::vector<std::pair<ea_t, std::string>> IDAUtils::search_named_globals(const std::string& pattern, bool is_regex) {
    return execute_sync_wrapper([&pattern, is_regex]() {
        // Validate input
        if (pattern.empty()) {
            throw std::invalid_argument("Search pattern cannot be empty");
        }
        if (pattern.length() > 1024) {
            throw std::invalid_argument("Search pattern too long (max 1024 characters)");
        }

        std::vector<std::pair<ea_t, std::string>> result;

        // Prepare regex if needed
        std::regex regex_pattern;
        if (is_regex) {
            try {
                regex_pattern = std::regex(pattern);
            } catch (const std::regex_error& e) {
                throw std::invalid_argument("Invalid regex pattern: " + std::string(e.what()));
            }
        }

        // Iterate through all names
        size_t qty = get_nlist_size();
        for (size_t i = 0; i < qty; i++) {
            ea_t ea = get_nlist_ea(i);
            if (ea != BADADDR) {
                // Check if it's not a function
                func_t *func = get_func(ea);
                if (!func) {
                    qstring name;
                    if (get_name(&name, ea) > 0) {
                        std::string str_name = name.c_str();

                        // Filter out auto-generated names
                        if (str_name.substr(0, 4) != "unk_" &&
                            str_name.substr(0, 5) != "byte_" &&
                            str_name.substr(0, 5) != "word_" &&
                            str_name.substr(0, 6) != "dword_" &&
                            str_name.substr(0, 6) != "qword_" &&
                            str_name.substr(0, 4) != "off_" &&
                            str_name.substr(0, 4) != "seg_" &&
                            str_name.substr(0, 4) != "asc_" &&
                            str_name.substr(0, 5) != "stru_") {

                            bool matches = false;
                            if (is_regex) {
                                matches = std::regex_search(str_name, regex_pattern);
                            } else {
                                // Case-insensitive substring search
                                std::string lower_name = str_name;
                                std::string lower_pattern = pattern;
                                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                                std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);
                                matches = lower_name.find(lower_pattern) != std::string::npos;
                            }

                            if (matches) {
                                result.push_back({ea, str_name});
                            }
                        }
                    }
                }
            }
        }

        // Sort by address
        std::sort(result.begin(), result.end());

        return result;
    });
}

std::vector<std::pair<ea_t, std::string>> IDAUtils::get_named_globals() {
    return execute_sync_wrapper([]() {
        std::vector<std::pair<ea_t, std::string>> result;

        // Iterate through all names
        size_t qty = get_nlist_size();
        for (size_t i = 0; i < qty; i++) {
            ea_t ea = get_nlist_ea(i);
            if (ea != BADADDR) {
                // Check if it's not a function
                func_t *func = get_func(ea);
                if (!func) {
                    qstring name;
                    if (get_name(&name, ea) > 0) {
                        std::string str_name = name.c_str();

                        // Filter out auto-generated names
                        if (str_name.substr(0, 4) != "unk_" &&
                            str_name.substr(0, 5) != "byte_" &&
                            str_name.substr(0, 5) != "word_" &&
                            str_name.substr(0, 6) != "dword_" &&
                            str_name.substr(0, 6) != "qword_" &&
                            str_name.substr(0, 4) != "off_" &&
                            str_name.substr(0, 4) != "seg_" &&
                            str_name.substr(0, 4) != "asc_" &&
                            str_name.substr(0, 5) != "stru_") {
                            result.push_back({ea, str_name});
                        }
                    }
                }
            }
        }

        // Sort by address
        std::sort(result.begin(), result.end());

        return result;
    });
}

std::vector<std::pair<ea_t, std::string>> IDAUtils::get_strings_with_addresses(int min_length) {
    return execute_sync_wrapper([min_length]() {
        // Validate input
        if (min_length < 1 || min_length > 1024) {
            throw std::invalid_argument("Invalid minimum length (must be 1-1024)");
        }

        std::vector<std::pair<ea_t, std::string>> result;

        // Refresh string list
        build_strlist();

        // Get all strings
        size_t qty = get_strlist_qty();
        for (size_t i = 0; i < qty; i++) {
            string_info_t si;
            if (get_strlist_item(&si, i)) {
                if (si.length >= min_length) {
                    qstring str;
                    if (get_strlit_contents(&str, si.ea, si.length, si.type) > 0) {
                        result.push_back({si.ea, str.c_str()});
                    }
                }
            }
        }

        return result;
    });
}
std::vector<std::tuple<ea_t, std::string, std::string>> IDAUtils::get_entry_points() {
    return execute_sync_wrapper([]() {
        std::vector<std::tuple<ea_t, std::string, std::string>> result;

        // Helper function to get function name
        auto get_func_name_at = [](ea_t ea) -> std::string {
            qstring name;
            if (get_func_name(&name, ea) > 0) {
                return name.c_str();
            } else if (get_name(&name, ea) > 0) {
                return name.c_str();
            }
            return "";
        };

        // Get main entry point
        ea_t main_ea = inf_get_main();
        if (main_ea != BADADDR) {
            std::string name = get_func_name_at(main_ea);
            result.push_back({main_ea, "main", name});
        }

        // Get start address (program entry)
        ea_t start_ea = inf_get_start_ea();
        if (start_ea != BADADDR && start_ea != main_ea) {
            std::string name = get_func_name_at(start_ea);
            result.push_back({start_ea, "start", name});
        }

        // Get all exported functions
        for (size_t i = 0; i < get_entry_qty(); i++) {
            uval_t ord = get_entry_ordinal(i);
            ea_t ea = get_entry(ord);

            if (ea != BADADDR) {
                // Skip if already added
                bool already_added = false;
                for (const auto& entry : result) {
                    if (std::get<0>(entry) == ea) {
                        already_added = true;
                        break;
                    }
                }
                if (!already_added) {
                    std::string name = get_func_name_at(ea);
                    result.push_back({ea, "export", name});
                }
            }
        }

        // Get TLS callbacks if present
        ea_t tls_ea = get_name_ea(BADADDR, "_tls_used");
        if (tls_ea != BADADDR) {
            // TLS directory structure has callbacks array
            ea_t callbacks_ea = tls_ea + 0x18; // Offset to AddressOfCallBacks
            ea_t callback_ptr = get_qword(callbacks_ea);
            if (callback_ptr) {
                while (callback_ptr != 0) {
                    ea_t callback_ea = get_qword(callback_ptr);
                    if (callback_ea) {
                        if (callback_ea != 0) {
                            std::string name = get_func_name_at(callback_ea);
                            result.push_back({callback_ea, "tls_callback", name});
                        }
                    }
                    callback_ptr += sizeof(ea_t);
                }
            }
        }

        // Sort by address
        std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            return std::get<0>(a) < std::get<0>(b);
        });

        return result;
    });
}

} // namespace llm_re