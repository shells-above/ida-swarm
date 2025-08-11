//
// Created by user on 6/29/25.
//

#include "core/ida_utils.h"
#include "core/ida_validators.h"

namespace llm_re {

// Helper function to format addresses as hex strings for error messages
std::string format_address_hex(ea_t address) {
    std::stringstream ss;
    ss << "0x" << std::hex << address;
    return ss.str();
}

ea_t IDAUtils::get_name_address(const std::string& name) {
    return execute_sync_wrapper([&name]() {
        return get_name_ea(BADADDR, name.c_str());
    });
}

bool IDAUtils::is_function(ea_t address) {
    return execute_sync_wrapper([address]() {
        func_t *func = get_func(address);
        return func != nullptr;
    });
}

// Consolidated search functions
std::vector<std::tuple<ea_t, std::string, bool>> IDAUtils::search_functions(const std::string& pattern, bool named_only, int max_results) {
    return execute_sync_wrapper([&pattern, named_only, max_results]() {
        std::vector<std::tuple<ea_t, std::string, bool>> result;

        // Convert pattern to lowercase for case-insensitive search
        std::string lower_pattern = pattern;
        std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);

        size_t qty = get_func_qty();
        int count = 0;

        for (size_t i = 0; i < qty; i++) {
            if (max_results > 0 && count >= max_results) break;

            func_t *func = getn_func(i);
            if (func) {
                qstring name;
                if (get_func_name(&name, func->start_ea) > 0) {
                    std::string func_name = name.c_str();

                    // Check if it's user-named
                    bool is_user_named = (func_name.substr(0, 4) != "sub_" &&
                                         func_name.substr(0, 2) != "j_" &&
                                         func_name.substr(0, 4) != "loc_" &&
                                         func_name.substr(0, 7) != "nullsub_" &&
                                         func_name.substr(0, 4) != "def_");

                    // Skip if named_only and not user-named
                    if (named_only && !is_user_named) continue;

                    // Pattern matching
                    if (!pattern.empty()) {
                        std::string lower_name = func_name;
                        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                        if (lower_name.find(lower_pattern) == std::string::npos) continue;
                    }

                    result.push_back({func->start_ea, func_name, is_user_named});
                    count++;
                }
            }
        }

        return result;
    });
}

std::vector<std::tuple<ea_t, std::string, std::string, std::string>> IDAUtils::search_globals(const std::string& pattern, int max_results) {
    return execute_sync_wrapper([&pattern, max_results]() {
        std::vector<std::tuple<ea_t, std::string, std::string, std::string>> result;

        // Convert pattern to lowercase for case-insensitive search
        std::string lower_pattern = pattern;
        std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);

        size_t qty = get_nlist_size();
        int count = 0;

        for (size_t i = 0; i < qty; i++) {
            if (max_results > 0 && count >= max_results) break;

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

                            // Pattern matching
                            if (!pattern.empty()) {
                                std::string lower_name = str_name;
                                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                                if (lower_name.find(lower_pattern) == std::string::npos) continue;
                            }

                            // Get value preview and type
                            std::string value_preview;
                            std::string type_str = "unknown";

                            try {
                                flags_t flags = get_flags(ea);
                                if (is_data(flags)) {
                                    if (is_strlit(flags)) {
                                        qstring str;
                                        size_t len = get_max_strlit_length(ea, STRTYPE_C);
                                        if (get_strlit_contents(&str, ea, len, STRTYPE_C) > 0) {
                                            value_preview = str.c_str();
                                            if (value_preview.length() > 50) {
                                                value_preview = value_preview.substr(0, 47) + "...";
                                            }
                                            type_str = "string";
                                        }
                                    } else {
                                        // Get first few bytes
                                        asize_t item_size = get_item_size(ea);
                                        if (item_size > 0 && item_size <= 8) {
                                            uint64_t val = 0;
                                            get_bytes(&val, item_size, ea);
                                            std::stringstream ss;
                                            ss << "0x" << std::hex << val;
                                            value_preview = ss.str();
                                            type_str = "data";
                                        }
                                    }
                                }
                            } catch (...) {
                                // Ignore errors in value retrieval
                            }

                            result.push_back({ea, str_name, value_preview, type_str});
                            count++;
                        }
                    }
                }
            }
        }

        return result;
    });
}

std::vector<std::pair<ea_t, std::string>> IDAUtils::search_strings_unified(const std::string& pattern, int min_length, int max_results) {
    return execute_sync_wrapper([&pattern, min_length, max_results]() {
        std::vector<std::pair<ea_t, std::string>> result;

        // Convert pattern to lowercase for case-insensitive search
        std::string lower_pattern = pattern;
        std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);

        // Suppress dialog by temporarily setting batch mode
        bool old_batch = batch;
        batch = true;
        
        // Refresh string list
        build_strlist();
        
        // Restore batch mode
        batch = old_batch;

        size_t qty = get_strlist_qty();
        int count = 0;

        for (size_t i = 0; i < qty; i++) {
            if (max_results > 0 && count >= max_results) break;

            string_info_t si;
            if (get_strlist_item(&si, i)) {
                if (si.length >= min_length) {
                    qstring str;
                    if (get_strlit_contents(&str, si.ea, si.length, si.type) > 0) {
                        std::string str_content = str.c_str();

                        // Pattern matching
                        if (!pattern.empty()) {
                            std::string lower_str = str_content;
                            std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
                            if (lower_str.find(lower_pattern) == std::string::npos) continue;
                        }

                        result.push_back({si.ea, str_content});
                        count++;
                    }
                }
            }
        }

        return result;
    });
}

// Comprehensive info functions
FunctionInfo IDAUtils::get_function_info(ea_t address) {
    return execute_sync_wrapper([address]() {
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
        }

        FunctionInfo info;
        func_t *func = get_func(address);
        if (!func) {
            throw std::runtime_error("Failed to get function at address");
        }

        // Basic info
        qstring name;
        if (get_func_name(&name, address) > 0) {
            info.name = name.c_str();
        }
        info.start_ea = func->start_ea;
        info.end_ea = func->end_ea;
        info.size = func->end_ea - func->start_ea;

        // Count xrefs
        info.xrefs_to_count = 0;
        info.xrefs_from_count = 0;

        xrefblk_t xb;
        for (bool ok = xb.first_to(address, XREF_ALL); ok; ok = xb.next_to()) {
            info.xrefs_to_count++;
        }

        for (ea_t ea = func->start_ea; ea < func->end_ea; ) {
            xrefblk_t xb2;
            for (bool ok = xb2.first_from(ea, XREF_ALL); ok; ok = xb2.next_from()) {
                if (xb2.to < func->start_ea || xb2.to >= func->end_ea) {
                    info.xrefs_from_count++;
                }
            }
            ea = next_head(ea, func->end_ea);
        }

        // Count strings and data refs
        info.string_refs_count = 0;
        info.data_refs_count = 0;

        for (ea_t ea = func->start_ea; ea < func->end_ea; ) {
            xrefblk_t xb3;
            for (bool ok = xb3.first_from(ea, XREF_DATA); ok; ok = xb3.next_from()) {
                flags_t flags = get_flags(xb3.to);
                if (is_strlit(flags)) {
                    info.string_refs_count++;
                } else {
                    info.data_refs_count++;
                }
            }
            ea = next_head(ea, func->end_ea);
        }

        // Check if library or thunk
        info.is_library = (func->flags & FUNC_LIB) != 0;
        info.is_thunk = (func->flags & FUNC_THUNK) != 0;

        return info;
    });
}

DataInfo IDAUtils::get_data_info(ea_t address, int max_xrefs) {
    return execute_sync_wrapper([address, max_xrefs]() {
        // Use relaxed validation - data info can be queried for external addresses
        if (!IDAValidators::is_valid_xref_address(address)) {
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }

        DataInfo info;

        // Get name
        qstring name;
        if (get_name(&name, address) > 0) {
            info.name = name.c_str();
        }

        // Get value and type
        flags_t flags = get_flags(address);
        if (is_strlit(flags)) {
            qstring str;
            size_t len = get_max_strlit_length(address, STRTYPE_C);
            if (get_strlit_contents(&str, address, len, STRTYPE_C) > 0) {
                info.value = str.c_str();
                info.type = "string";
            }
            info.size = len;
        } else if (is_data(flags)) {
            asize_t item_size = get_item_size(address);
            info.size = item_size;

            if (item_size > 0) {
                bytevec_t bytes;
                bytes.resize(item_size);
                if (get_bytes(&bytes[0], item_size, address)) {
                    std::stringstream ss;
                    ss << std::hex << std::setfill('0');
                    for (size_t i = 0; i < item_size; i++) {
                        ss << std::setw(2) << static_cast<int>(bytes[i]);
                        if (i < item_size - 1) ss << " ";
                    }
                    info.value = ss.str();
                    info.type = "bytes";
                }
            }
        } else {
            info.type = "unknown";
            info.size = 0;
        }

        // Get xrefs with truncation
        xrefblk_t xb;
        int xref_count = 0;
        for (bool ok = xb.first_to(address, XREF_ALL); ok && xref_count < max_xrefs; ok = xb.next_to()) {
            qstring xref_name;
            std::string name_str;
            if (get_func_name(&xref_name, xb.from) > 0) {
                name_str = xref_name.c_str();
            } else if (get_name(&xref_name, xb.from) > 0) {
                name_str = xref_name.c_str();
            }
            info.xrefs_to.push_back({xb.from, name_str});
            xref_count++;
        }

        // Check if we hit the limit and there are more xrefs
        if (xref_count == max_xrefs) {
            bool has_more = xb.next_to();
            if (has_more) {
                info.xrefs_truncated = true;
                info.xrefs_truncated_at = max_xrefs;
            }
        }

        return info;
    });
}

std::string IDAUtils::dump_data(ea_t address, size_t size, int bytes_per_line) {
    return execute_sync_wrapper([address, size, bytes_per_line]() {
        // Validate input
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }

        if (size == 0 || size > 0x10000) { // 64KB max for safety
            throw std::invalid_argument("Invalid size: must be between 1 and 65536 bytes");
        }

        // Check if we can read the full range
        if (!is_mapped(address) || !is_mapped(address + size - 1)) {
            throw std::invalid_argument("Data range is not fully mapped");
        }

        // Read the bytes
        bytevec_t bytes;
        bytes.resize(size);
        if (!get_bytes(&bytes[0], size, address)) {
            throw std::runtime_error("Failed to read data");
        }

        // Format as hex dump
        std::stringstream result;

        for (size_t offset = 0; offset < size; offset += bytes_per_line) {
            // Address column
            result << std::hex << std::setfill('0') << std::setw(8)
                   << (address + offset) << ":  ";

            // Hex bytes column
            size_t line_bytes = std::min(static_cast<size_t>(bytes_per_line),
                                         size - offset);

            for (size_t i = 0; i < bytes_per_line; i++) {
                if (i < line_bytes) {
                    result << std::hex << std::setfill('0') << std::setw(2)
                           << static_cast<int>(bytes[offset + i]) << " ";
                } else {
                    result << "   "; // padding for incomplete lines
                }
            }

            result << " |";

            // ASCII column
            for (size_t i = 0; i < line_bytes; i++) {
                unsigned char ch = bytes[offset + i];
                if (ch >= 32 && ch < 127) {
                    result << ch;
                } else {
                    result << '.';
                }
            }

            result << "|";

            if (offset + bytes_per_line < size) {
                result << "\n";
            }
        }

        return result.str();
    });
}

// Unified name setter
bool IDAUtils::set_addr_name(ea_t address, const std::string& name) {
    return execute_sync_wrapper([address, &name]() {
        // Use relaxed validation - names can be set on external addresses
        if (!IDAValidators::is_valid_xref_address(address)) {
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }
        if (!IDAValidators::is_valid_name(name)) {
            throw std::invalid_argument("Invalid name: " + name);
        }

        // For functions, set at function start
        func_t *func = get_func(address);
        if (func) {
            return set_name(func->start_ea, name.c_str(), SN_NOCHECK | SN_NOWARN);
        } else {
            return set_name(address, name.c_str(), SN_NOCHECK | SN_NOWARN);
        }
    }, MFF_WRITE);
}

// Keep existing implementations for the following functions as they're still used:
std::vector<std::pair<ea_t, std::string>> IDAUtils::get_xrefs_to_with_names(ea_t address, int max_count) {
    return execute_sync_wrapper([address, max_count]() {
        // Use relaxed validation - xrefs can involve external addresses
        if (!IDAValidators::is_valid_xref_address(address)) {
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }

        std::vector<std::pair<ea_t, std::string>> result;
        int count = 0;
        xrefblk_t xb;
        for (bool ok = xb.first_to(address, XREF_ALL); ok; ok = xb.next_to()) {
            if (max_count > 0 && count >= max_count) {
                break;
            }

            qstring name;
            std::string func_name;

            // Try to get function name at the xref source
            if (get_func_name(&name, xb.from) > 0) {
                func_name = name.c_str();
            } else if (get_name(&name, xb.from) > 0) {
                func_name = name.c_str();
            }

            result.push_back({xb.from, func_name});
            count++;
        }
        return result;
    });
}

std::vector<std::pair<ea_t, std::string>> IDAUtils::get_xrefs_from_with_names(ea_t address, int max_count) {
    return execute_sync_wrapper([address, max_count]() {
        // Use relaxed validation - xrefs can involve external addresses
        if (!IDAValidators::is_valid_xref_address(address)) {
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }

        std::vector<std::pair<ea_t, std::string>> result;
        int count = 0;
        xrefblk_t xb;
        for (bool ok = xb.first_from(address, XREF_ALL); ok; ok = xb.next_from()) {
            if (max_count > 0 && count >= max_count) {
                break;
            }

            qstring name;
            std::string func_name;

            // Try to get function name at the xref target
            if (get_func_name(&name, xb.to) > 0) {
                func_name = name.c_str();
            } else if (get_name(&name, xb.to) > 0) {
                func_name = name.c_str();
            }

            result.push_back({xb.to, func_name});
            count++;
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

std::vector<std::string> IDAUtils::get_function_string_refs(ea_t address, int max_count) {
    return execute_sync_wrapper([address, max_count]() {
        // Validate input
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
        }

        std::vector<std::string> result;
        func_t *func = get_func(address);
        if (!func) {
            return result;
        }

        int count = 0;
        // Iterate through function instructions
        for (ea_t ea = func->start_ea; ea < func->end_ea; ) {
            if (max_count > 0 && count >= max_count) {
                break;
            }

            // Check for data references from this instruction
            xrefblk_t xb;
            for (bool ok = xb.first_from(ea, XREF_DATA); ok; ok = xb.next_from()) {
                if (max_count > 0 && count >= max_count) {
                    break;
                }

                // Check if the target is a string
                flags_t flags = get_flags(xb.to);
                if (is_strlit(flags)) {
                    // Get the string content
                    qstring str;
                    size_t len = get_max_strlit_length(xb.to, STRTYPE_C);
                    if (get_strlit_contents(&str, xb.to, len, STRTYPE_C) > 0) {
                        // Check if already in result to avoid duplicates
                        if (std::find(result.begin(), result.end(), str.c_str()) == result.end()) {
                            result.push_back(str.c_str());
                            count++;
                        }
                    }
                }
            }
            ea = next_head(ea, func->end_ea);
        }

        return result;
    });
}

std::vector<ea_t> IDAUtils::get_function_data_refs(ea_t address, int max_count) {
    return execute_sync_wrapper([address, max_count]() {
        // Validate input
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
        }

        std::vector<ea_t> result;
        func_t *func = get_func(address);
        if (!func) {
            return result;
        }

        std::set<ea_t> unique_refs; // Use set to avoid duplicates
        int count = 0;

        // Iterate through function instructions
        for (ea_t ea = func->start_ea; ea < func->end_ea; ) {
            if (max_count > 0 && count >= max_count) {
                break;
            }

            // Get data references from this instruction
            xrefblk_t xb;
            for (bool ok = xb.first_from(ea, XREF_DATA); ok; ok = xb.next_from()) {
                if (max_count > 0 && count >= max_count) {
                    break;
                }

                if (unique_refs.insert(xb.to).second) {
                    result.push_back(xb.to);
                    count++;
                }
            }
            ea = next_head(ea, func->end_ea);
        }

        // Sort by address
        std::sort(result.begin(), result.end());

        return result;
    });
}

bool IDAUtils::add_disassembly_comment(ea_t address, const std::string& comment) {
    return execute_sync_wrapper([address, &comment]() {
        // Validate inputs - use relaxed validation for comments
        if (!IDAValidators::is_valid_xref_address(address)) {
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }
        if (comment.length() > 4096) {
            throw std::invalid_argument("Comment too long (max 4096 characters)");
        }

        return set_cmt(address, comment.c_str(), false);
    }, MFF_WRITE);
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
    }, MFF_WRITE);
}

bool IDAUtils::clear_disassembly_comment(ea_t address) {
    return execute_sync_wrapper([address]() {
        // Validate input - use relaxed validation for comments
        if (!IDAValidators::is_valid_xref_address(address)) {
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }

        return set_cmt(address, "", false);
    }, MFF_WRITE);
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
    }, MFF_WRITE);
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

// Decompilation-related
FunctionPrototypeInfo IDAUtils::get_function_prototype(ea_t address) {
    return execute_sync_wrapper([address]() {
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
        }

        FunctionPrototypeInfo info;

        // Get function type info
        tinfo_t func_type;
        if (!get_tinfo(&func_type, address)) {
            // Try to guess from function
            func_t *func = get_func(address);
            if (!func || !guess_tinfo(&func_type, address)) {
                throw std::runtime_error("Cannot get function type information");
            }
        }

        // Get function name
        qstring name;
        get_func_name(&name, address);
        info.function_name = name.c_str();

        // Get the full prototype string
        qstring prototype;
        func_type.print(&prototype, info.function_name.c_str());
        info.full_prototype = prototype.c_str();

        // Parse function details
        func_type_data_t ftd;
        if (func_type.get_func_details(&ftd)) {
            // Get return type
            qstring ret_type;
            ftd.rettype.print(&ret_type);
            info.return_type = ret_type.c_str();

            // Get calling convention
            cm_t cc = ftd.get_cc();
            switch (cc) {
                case CM_CC_CDECL: info.calling_convention = "__cdecl"; break;
                case CM_CC_STDCALL: info.calling_convention = "__stdcall"; break;
                case CM_CC_PASCAL: info.calling_convention = "__pascal"; break;
                case CM_CC_FASTCALL: info.calling_convention = "__fastcall"; break;
                case CM_CC_THISCALL: info.calling_convention = "__thiscall"; break;
                case CM_CC_SPECIAL: info.calling_convention = "__usercall"; break;
                default: info.calling_convention = ""; break;
            }

            // Get parameters
            for (int i = 0; i < ftd.size(); i++) {
                FunctionParameter param;
                param.index = i;

                // Get parameter type
                qstring param_type;
                ftd[i].type.print(&param_type);
                param.type = param_type.c_str();

                // Get parameter name
                param.name = ftd[i].name.c_str();
                if (param.name.empty()) {
                    param.name = "arg" + std::to_string(i);
                }

                info.parameters.push_back(param);
            }
        }

        return info;
    });
}

bool IDAUtils::set_function_prototype(ea_t address, const std::string& prototype) {
    return execute_sync_wrapper([address, &prototype]() {
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
        }

        // Ensure the prototype ends with a semicolon (required by the parser)
        std::string proto_with_semi = prototype;
        if (!proto_with_semi.empty() && proto_with_semi.back() != ';') {
            proto_with_semi += ';';
        }

        // Try to parse as a C declaration first
        tinfo_t tif;
        qstring name;
        
        // Parse the prototype - this handles full function declarations like:
        // "int __cdecl func(int a, char *b);"
        // "void func(void)" 
        // "BOOL __stdcall WindowProc(HWND, UINT, WPARAM, LPARAM)"
        // Use PT_TYP to parse type declarations (functions are types)
        if (!parse_decl(&tif, &name, get_idati(), proto_with_semi.c_str(), PT_TYP | PT_SIL)) {
            // If that fails, try apply_cdecl as a more flexible fallback
            // apply_cdecl internally handles various prototype formats and
            // always uses TINFO_DEFINITE flag
            if (!apply_cdecl(get_idati(), address, proto_with_semi.c_str(), TINFO_DEFINITE)) {
                throw std::invalid_argument("Failed to parse function prototype. Expected format: 'return_type [calling_convention] function_name(parameters)'");
            }
            // apply_cdecl succeeded, we're done
            mark_cfunc_dirty(address);
            return true;
        }

        // Validate that we got a function type
        if (!tif.is_func()) {
            throw std::invalid_argument("Parsed type is not a function. Got: " + std::string(tif.dstr()));
        }

        // Apply the type to the function with DEFINITE flag to make it persistent
        if (!apply_tinfo(address, tif, TINFO_DEFINITE)) {
            throw std::runtime_error("Failed to apply function prototype to address " + format_address_hex(address));
        }

        // Invalidate decompiler cache to ensure changes are reflected
        mark_cfunc_dirty(address);

        // If a name was extracted and is different from current, rename
        if (!name.empty()) {
            qstring current_name;
            get_func_name(&current_name, address);
            if (current_name != name) {
                set_name(address, name.c_str(), SN_NOCHECK | SN_NOWARN);
            }
        }

        return true;
    }, MFF_WRITE);
}

FunctionLocalsInfo IDAUtils::get_variables(ea_t address) {
    return execute_sync_wrapper([address]() {
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
        }

        FunctionLocalsInfo result;

        // Initialize Hex-Rays if needed
        if (!init_hexrays_plugin()) {
            throw std::runtime_error("Hex-Rays decompiler not available");
        }

        func_t *func = get_func(address);
        if (!func) {
            throw std::runtime_error("Cannot get function");
        }

        // Decompile to get local variables
        hexrays_failure_t hf;
        cfuncptr_t cfunc = decompile(func, &hf, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
        if (!cfunc) {
            throw std::runtime_error("Failed to decompile function");
        }

        // Get local variables
        const lvars_t *lvars = cfunc->get_lvars();
        if (lvars) {
            for (size_t i = 0; i < lvars->size(); i++) {
                const lvar_t &lvar = (*lvars)[i];

                // Skip fake variables
                if (lvar.is_fake_var()) continue;

                LocalVariableInfo var_info;
                var_info.name = lvar.name.c_str();

                // Get type
                qstring type_str;
                lvar.type().print(&type_str);
                var_info.type = type_str.c_str();

                // Determine location
                if (lvar.is_stk_var()) {
                    var_info.location = "stack";
                    var_info.stack_offset = lvar.get_stkoff();
                } else if (lvar.is_reg_var()) {
                    var_info.location = "register";
                    qstring reg_name;
                    get_mreg_name(&reg_name, lvar.get_reg1(), lvar.type().get_size());
                    var_info.reg_name = reg_name.c_str();
                } else {
                    var_info.location = "other";
                }

                // Check if it's an argument
                bool is_arg = lvar.is_arg_var();
                if (is_arg) {
                    FunctionArgument arg;
                    arg.name = var_info.name;
                    arg.type = var_info.type;

                    // For arguments, we need to determine the index from the function type
                    // Get function type to match argument
                    tinfo_t func_type;
                    if (get_tinfo(&func_type, address)) {
                        func_type_data_t ftd;
                        if (func_type.get_func_details(&ftd)) {
                            // Find matching argument by name or location
                            for (int j = 0; j < ftd.size(); j++) {
                                if (ftd[j].name == lvar.name ||
                                    (ftd[j].argloc.is_reg() && lvar.is_reg_var() &&
                                     ftd[j].argloc.reg1() == lvar.get_reg1())) {
                                    arg.index = j;
                                    break;
                                }
                            }
                        }
                    }

                    result.arguments.push_back(arg);
                } else {
                    result.locals.push_back(var_info);
                }
            }
        }

        // Sort arguments by index
        std::sort(result.arguments.begin(), result.arguments.end(),
                  [](const FunctionArgument& a, const FunctionArgument& b) {
                      return a.index < b.index;
                  });

        return result;
    });
}

bool IDAUtils::set_variable(ea_t address, const std::string& variable_name, const std::string& new_name, const std::string& new_type) {
    return execute_sync_wrapper([address, &variable_name, &new_name, &new_type]() {
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
        }

        if (!init_hexrays_plugin()) {
            throw std::runtime_error("Hex-Rays decompiler not available");
        }

        func_t *func = get_func(address);
        if (!func) {
            throw std::runtime_error("Cannot get function at address");
        }

        // First try to modify as a function argument (simpler case)
        // This works even without decompiling the function
        tinfo_t func_type;
        if (get_tinfo(&func_type, address)) {
            func_type_data_t ftd;
            if (func_type.get_func_details(&ftd)) {
                // Search for argument by name
                for (size_t i = 0; i < ftd.size(); i++) {
                    if (ftd[i].name == variable_name.c_str()) {
                        // Found the argument - modify it
                        bool changed = false;

                        if (!new_type.empty()) {
                            tinfo_t new_tif;
                            qstring dummy_name;
                            
                            // For bare type expressions like "char*", "int", etc., we need to wrap
                            // them in a typedef declaration because parse_decl expects complete declarations
                            std::string typedef_decl = "typedef " + new_type + " __dummy;";
                            
                            if (!parse_decl(&new_tif, &dummy_name, get_idati(), typedef_decl.c_str(), PT_TYP | PT_SIL)) {
                                // If typedef approach fails, try as a variable declaration
                                std::string var_decl = new_type + " __dummy";
                                if (!parse_decl(&new_tif, &dummy_name, get_idati(), var_decl.c_str(), PT_VAR | PT_SIL)) {
                                    throw std::invalid_argument("Failed to parse type: '" + new_type + 
                                        "'. Expected formats: 'int', 'char*', 'struct name*', 'unsigned int', etc.");
                                }
                            }
                            
                            if (!new_tif.is_correct()) {
                                throw std::invalid_argument("Parsed type is invalid: " + new_type);
                            }
                            
                            ftd[i].type = new_tif;
                            changed = true;
                        }

                        if (!new_name.empty()) {
                            ftd[i].name = new_name.c_str();
                            changed = true;
                        }

                        if (changed) {
                            tinfo_t new_func_type;
                            if (!new_func_type.create_func(ftd)) {
                                throw std::runtime_error("Failed to create new function type");
                            }
                            if (!apply_tinfo(address, new_func_type, TINFO_DEFINITE)) {
                                throw std::runtime_error("Failed to apply function type");
                            }
                            mark_cfunc_dirty(address);
                            return true;
                        }
                        return false;  // No changes requested
                    }
                }
            }
        }

        // Not a function argument - must be a local variable
        // Now we need to decompile to access local variables
        hexrays_failure_t hf;
        cfuncptr_t cfunc = decompile(func, &hf, DECOMP_NO_WAIT | DECOMP_NO_CACHE);
        if (!cfunc) {
            throw std::runtime_error("Failed to decompile function");
        }

        // Find the local variable
        const lvars_t *lvars = cfunc->get_lvars();
        if (!lvars) {
            throw std::runtime_error("No local variables found");
        }

        const lvar_t *target_lvar = nullptr;
        for (size_t i = 0; i < lvars->size(); i++) {
            if ((*lvars)[i].name == variable_name.c_str()) {
                target_lvar = &(*lvars)[i];
                break;
            }
        }

        if (!target_lvar) {
            throw std::invalid_argument("Variable not found: " + variable_name);
        }

        if (target_lvar->is_fake_var()) {
            throw std::invalid_argument("Cannot modify compiler-generated variable");
        }

        // Use persistent modification for local variables
        lvar_saved_info_t lsi;
        lsi.ll = static_cast<lvar_locator_t>(*target_lvar);  // lvar_t inherits from lvar_locator_t
        uint mli_flags = 0;

        if (!new_type.empty()) {
            tinfo_t new_tif;
            qstring dummy_name;
            
            // For bare type expressions like "char*", "int", etc., we need to wrap
            // them in a typedef declaration because parse_decl expects complete declarations
            std::string typedef_decl = "typedef " + new_type + " __dummy;";
            
            if (!parse_decl(&new_tif, &dummy_name, get_idati(), typedef_decl.c_str(), PT_TYP | PT_SIL)) {
                // If typedef approach fails, try as a variable declaration
                std::string var_decl = new_type + " __dummy";
                if (!parse_decl(&new_tif, &dummy_name, get_idati(), var_decl.c_str(), PT_VAR | PT_SIL)) {
                    throw std::invalid_argument("Failed to parse type: '" + new_type + 
                        "'. Expected formats: 'int', 'char*', 'struct name*', 'unsigned int', etc.");
                }
            }
            
            if (!new_tif.is_correct()) {
                throw std::invalid_argument("Parsed type is invalid: " + new_type);
            }
            
            lsi.type = new_tif;
            mli_flags |= MLI_TYPE;
        }

        if (!new_name.empty()) {
            lsi.name = new_name.c_str();
            mli_flags |= MLI_NAME;
        }

        if (mli_flags == 0) {
            return false;  // No changes requested
        }

        return modify_user_lvar_info(func->start_ea, mli_flags, lsi);
    }, MFF_WRITE);
}

std::vector<LocalTypeInfo> IDAUtils::search_local_types(const std::string& pattern, const std::string& type_kind, int max_results) {
    return execute_sync_wrapper([&pattern, &type_kind, max_results]() {
        std::vector<LocalTypeInfo> result;

        // Convert pattern to lowercase for case-insensitive search
        std::string lower_pattern = pattern;
        std::transform(lower_pattern.begin(), lower_pattern.end(), lower_pattern.begin(), ::tolower);

        // Get local type library
        til_t *til = get_idati();
        if (!til) {
            throw std::runtime_error("Cannot access local type library");
        }

        int count = 0;
        uint32 limit = get_ordinal_limit(til);  // Changed from get_ordinal_qty
        if (limit == 0 || limit == uint32(-1)) {
            return result; // No ordinals
        }

        for (uint32 ordinal = 1; ordinal < limit && count < max_results; ordinal++) {
            const char *name = get_numbered_type_name(til, ordinal);
            if (!name || name[0] == '\0') continue;  // Skip unnamed types

            std::string type_name = name;

            // Pattern matching
            if (!pattern.empty()) {
                std::string lower_name = type_name;
                std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
                if (lower_name.find(lower_pattern) == std::string::npos) continue;
            }

            // Get type info
            tinfo_t tif;
            if (!tif.get_numbered_type(til, ordinal)) continue;

            // Determine type kind
            std::string kind;
            if (tif.is_struct()) kind = "struct";
            else if (tif.is_union()) kind = "union";
            else if (tif.is_enum()) kind = "enum";
            else if (tif.is_typedef()) kind = "typedef";
            else continue; // Skip other types

            // Filter by kind if specified
            if (type_kind != "any" && type_kind != kind) continue;

            LocalTypeInfo info;
            info.name = type_name;
            info.kind = kind;
            info.size = tif.get_size();

            result.push_back(info);
            count++;
        }

        return result;
    });
}

LocalTypeDefinition IDAUtils::get_local_type(const std::string& type_name) {
    return execute_sync_wrapper([&type_name]() {
        LocalTypeDefinition result;

        til_t *til = get_idati();
        if (!til) {
            throw std::runtime_error("Cannot access local type library");
        }

        // Find the type by name
        int32 ordinal = get_type_ordinal(til, type_name.c_str());
        if (ordinal <= 0) {
            throw std::invalid_argument("Type not found: " + type_name);
        }

        // Get type info
        tinfo_t tif;
        if (!tif.get_numbered_type(til, ordinal)) {
            throw std::runtime_error("Cannot get type information");
        }

        result.name = type_name;
        result.size = tif.get_size();

        // Determine kind
        if (tif.is_struct()) result.kind = "struct";
        else if (tif.is_union()) result.kind = "union";
        else if (tif.is_enum()) result.kind = "enum";
        else if (tif.is_typedef()) result.kind = "typedef";
        else result.kind = "unknown";

        // Get the C definition using tinfo_t's print method
        qstring def;
        if (!tif.print(&def, type_name.c_str(), PRTYPE_DEF | PRTYPE_MULTI)) {
            // If that fails, try a simpler format
            if (!tif.print(&def)) {
                throw std::runtime_error("Cannot format type definition");
            }
        }

        result.definition = def.c_str();

        return result;
    });
}

SetLocalTypeResult IDAUtils::set_local_type(const std::string& definition, bool replace_existing) {
    return execute_sync_wrapper([&definition, replace_existing]() {
        SetLocalTypeResult result;

        til_t *til = get_idati();
        if (!til) {
            result.success = false;
            result.error_message = "Cannot access local type library";
            return result;
        }

        // Parse the type definition
        tinfo_t tif;
        qstring type_name;
        const char* ptr = definition.c_str();

        if (!parse_decl(&tif, &type_name, til, ptr, PT_TYP | PT_SIL)) {
            result.success = false;
            result.error_message = "Failed to parse type definition";
            return result;
        }

        // Extract the actual type name
        if (type_name.empty()) {
            // For anonymous structs, generate a name
            result.error_message = "Type definition must include a name";
            result.success = false;
            return result;
        }

        // Check if type already exists
        if (!replace_existing) {
            uint32 existing_ord = get_type_ordinal(til, type_name.c_str());
            if (existing_ord != 0) {
                result.success = false;
                result.error_message = "Type '" + std::string(type_name.c_str()) + "' already exists";
                return result;
            }
        }

        // Save the type to local types
        int ntf_flags = NTF_TYPE;
        if (replace_existing) {
            ntf_flags |= NTF_REPLACE;
        }

        uint32 ord = replace_existing ? get_type_ordinal(til, type_name.c_str()) : 0;
        tinfo_code_t code = tif.set_numbered_type(til, ord, ntf_flags, type_name.c_str());

        if (code != TERR_OK) {
            result.success = false;
            result.error_message = "Failed to save type: " + std::string(tinfo_errstr(code));
            return result;
        }

        result.success = true;
        result.type_name = type_name.c_str();
        return result;
    }, MFF_WRITE);
}

} // namespace llm_re