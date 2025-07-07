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

        // Refresh string list
        build_strlist();

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
        if (!IDAValidators::is_valid_address(address)) {
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
        if (!IDAValidators::is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }
        if (!IDAValidators::is_valid_name(name)) {
            throw std::invalid_argument("Invalid name: " + name);
        }

        // For functions, set at function start
        func_t *func = get_func(address);
        if (func) {
            return set_name(func->start_ea, name.c_str());
        } else {
            return set_name(address, name.c_str());
        }
    }, MFF_WRITE);
}

// Keep existing implementations for the following functions as they're still used:
std::vector<std::pair<ea_t, std::string>> IDAUtils::get_xrefs_to_with_names(ea_t address, int max_count) {
    return execute_sync_wrapper([address, max_count]() {
        if (!IDAValidators::is_valid_address(address)) {
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
        if (!IDAValidators::is_valid_address(address)) {
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
        // Validate input
        if (!IDAValidators::is_valid_address(address)) {
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

        // Get current function type to check parameter count
        tinfo_t current_type;
        func_type_data_t current_ftd;
        bool has_current_type = false;
        size_t current_param_count = 0;

        if (get_tinfo(&current_type, address)) {
            if (current_type.get_func_details(&current_ftd)) {
                has_current_type = true;
                current_param_count = current_ftd.size();
            }
        }

        // Parse the new prototype
        tinfo_t new_type;
        qstring proto_str = prototype.c_str();
        const char *ptr = proto_str.c_str();
        qstring func_name;

        if (!parse_decl(&new_type, &func_name, nullptr, ptr, PT_SIL)) {
            throw std::invalid_argument("Failed to parse function prototype");
        }

        // Verify it's a function type
        if (!new_type.is_func()) {
            throw std::invalid_argument("Parsed type is not a function");
        }

        // Get details of the new type
        func_type_data_t new_ftd;
        if (!new_type.get_func_details(&new_ftd)) {
            throw std::runtime_error("Failed to get function details from parsed prototype");
        }

        // Check parameter count matches if we have current type info
        if (has_current_type && new_ftd.size() != current_param_count) {
            throw std::invalid_argument(
                "Parameter count mismatch: current function has " +
                std::to_string(current_param_count) + " parameters, new prototype has " +
                std::to_string(new_ftd.size())
            );
        }

        // Extract parameter names from the prototype string
        // This is a bit tricky - we need to parse the prototype more carefully
        std::vector<std::string> param_names;

        // Find the opening parenthesis
        size_t paren_start = prototype.find('(');
        size_t paren_end = prototype.rfind(')');
        if (paren_start != std::string::npos && paren_end != std::string::npos && paren_start < paren_end) {
            std::string params_str = prototype.substr(paren_start + 1, paren_end - paren_start - 1);

            // Simple parameter parser - this could be more robust
            size_t pos = 0;
            int paren_depth = 0;
            size_t param_start = 0;

            for (size_t i = 0; i <= params_str.length(); i++) {
                char c = (i < params_str.length()) ? params_str[i] : ',';

                if (c == '(') paren_depth++;
                else if (c == ')') paren_depth--;

                if ((c == ',' || i == params_str.length()) && paren_depth == 0) {
                    if (i > param_start) {
                        std::string param = params_str.substr(param_start, i - param_start);

                        // Extract parameter name from the parameter declaration
                        // Look for the last identifier in the parameter
                        std::string param_name;
                        size_t last_space = param.rfind(' ');
                        size_t last_star = param.rfind('*');
                        size_t last_amp = param.rfind('&');

                        size_t name_start = 0;
                        if (last_space != std::string::npos) name_start = last_space + 1;
                        if (last_star != std::string::npos && last_star > name_start) name_start = last_star + 1;
                        if (last_amp != std::string::npos && last_amp > name_start) name_start = last_amp + 1;

                        // Skip whitespace
                        while (name_start < param.length() && std::isspace(param[name_start])) name_start++;

                        if (name_start < param.length()) {
                            param_name = param.substr(name_start);
                            // Remove any trailing whitespace or special chars
                            size_t name_end = param_name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
                            if (name_end != std::string::npos) {
                                param_name = param_name.substr(0, name_end);
                            }
                        }

                        param_names.push_back(param_name);
                    }
                    param_start = i + 1;
                }
            }
        }

        // Apply parameter names to the func_type_data_t
        for (size_t i = 0; i < new_ftd.size() && i < param_names.size(); i++) {
            if (!param_names[i].empty()) {
                new_ftd[i].name = param_names[i].c_str();
            }
        }

        // Recreate the type with the updated parameter names
        tinfo_t final_type;
        if (!final_type.create_func(new_ftd)) {
            throw std::runtime_error("Failed to create function type with parameter names");
        }

        // Apply the new type
        if (!apply_tinfo(address, final_type, TINFO_DEFINITE)) {
            return false;
        }

        // If function name was provided in prototype and different, update it
        if (!func_name.empty()) {
            qstring current_name;
            get_func_name(&current_name, address);
            if (current_name != func_name) {
                set_name(address, func_name.c_str());
            }
        }

        return true;
    }, MFF_WRITE);
}

bool IDAUtils::set_function_parameter_name(ea_t address, int param_index, const std::string& name) {
    return execute_sync_wrapper([address, param_index, &name]() {
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
        }

        // Get current function type
        tinfo_t func_type;
        if (!get_tinfo(&func_type, address)) {
            func_t *func = get_func(address);
            if (!func || !guess_tinfo(&func_type, address)) {
                throw std::runtime_error("Cannot get function type information");
            }
        }

        // Get function details
        func_type_data_t ftd;
        if (!func_type.get_func_details(&ftd)) {
            throw std::runtime_error("Cannot get function details");
        }

        // Check parameter index
        if (param_index < 0 || param_index >= ftd.size()) {
            throw std::invalid_argument("Invalid parameter index");
        }

        // Update parameter name
        ftd[param_index].name = name.c_str();

        // Create new function type with updated parameter
        tinfo_t new_type;
        if (!new_type.create_func(ftd)) {
            return false;
        }

        // Apply the updated type
        return apply_tinfo(address, new_type, TINFO_DEFINITE);
    }, MFF_WRITE);
}

FunctionLocalsInfo IDAUtils::get_function_locals(ea_t address) {
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
        cfuncptr_t cfunc = decompile(func, &hf, DECOMP_NO_WAIT);
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
bool IDAUtils::set_local_variable(ea_t address, const std::string& current_name, const std::string& new_name, const std::string& new_type) {
    return execute_sync_wrapper([address, &current_name, &new_name, &new_type]() {
        if (!IDAValidators::is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
        }

        // Initialize Hex-Rays if needed
        if (!init_hexrays_plugin()) {
            throw std::runtime_error("Hex-Rays decompiler not available");
        }

        func_t *func = get_func(address);
        if (!func) {
            throw std::runtime_error("Cannot get function");
        }

        // First, find the variable using locate_lvar
        lvar_locator_t ll;
        if (!locate_lvar(&ll, func->start_ea, current_name.c_str())) {
            throw std::invalid_argument("Variable not found: " + current_name);
        }

        // Prepare the saved info
        lvar_saved_info_t info;
        info.ll = ll;

        uint mli_flags = 0;
        bool need_update = false;

        // Update name if provided
        if (!new_name.empty() && new_name != current_name) {
            info.name = new_name.c_str();
            mli_flags |= MLI_NAME;
            need_update = true;
        }

        // Update type if provided
        if (!new_type.empty()) {
            // Parse the type
            tinfo_t new_tif;
            qstring type_str = new_type.c_str();
            const char *ptr = type_str.c_str();
            if (!parse_decl(&new_tif, nullptr, nullptr, ptr, PT_TYP | PT_SIL)) {
                throw std::invalid_argument("Failed to parse type: " + new_type);
            }

            info.type = new_tif;
            mli_flags |= MLI_TYPE;
            need_update = true;
        }

        if (!need_update) {
            return false;
        }

        // Apply the changes
        return modify_user_lvar_info(func->start_ea, mli_flags, info);
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
        qstring def_str = definition.c_str();
        const char *ptr = def_str.c_str();

        // Try to parse multiple declarations (in case of dependencies)
        while (*ptr && qisspace(*ptr)) ptr++;

        while (*ptr) {
            tinfo_t tif;
            qstring name;

            // Parse one declaration
            if (!parse_decl(&tif, &name, til, ptr, PT_TYP | PT_SIL)) {
                result.success = false;
                result.error_message = "Failed to parse type definition";
                return result;
            }

            if (name.empty()) {
                result.success = false;
                result.error_message = "Type definition must include a name";
                return result;
            }

            // Check if type already exists
            int32 existing_ord = get_type_ordinal(til, name.c_str());
            if (existing_ord > 0 && !replace_existing) {
                result.success = false;
                result.error_message = "Type already exists: " + std::string(name.c_str());
                return result;
            }

            // Set the type using tinfo_t methods
            int ntf_flags = NTF_TYPE;
            if (replace_existing) ntf_flags |= NTF_REPLACE;

            uint32 ordinal = existing_ord > 0 ? existing_ord : 0;
            tinfo_code_t code = tif.set_numbered_type(til, ordinal, ntf_flags, name.c_str());

            if (code != TERR_OK) {
                result.success = false;
                result.error_message = std::string("Failed to add type: ") + tinfo_errstr(code);
                return result;
            }

            result.type_name = name.c_str();

            // Skip whitespace and semicolons
            while (*ptr && (qisspace(*ptr) || *ptr == ';')) ptr++;
        }

        result.success = true;
        return result;
    }, MFF_WRITE);
}

} // namespace llm_re