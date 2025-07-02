//
// Created by user on 6/29/25.
//

#include "actions.h"
#include "ida_utils.h"

namespace llm_re {

// New function to parse address lists (handles both single values and arrays)
std::vector<ea_t> ActionExecutor::parse_list_address_param(const json& params, const std::string& key) {
    if (!params.contains(key)) {
        throw std::invalid_argument("Missing parameter: " + key);
    }

    std::vector<ea_t> addresses;

    try {
        const auto& param = params[key];

        // Check if it's an array
        if (param.is_array()) {
            // Process each element in the array
            for (const auto& element : param) {
                // Parse each element as a single address using existing logic
                ea_t addr = parse_single_address_value(element);
                addresses.push_back(addr);
            }
        } else if (param.is_string()) {
            std::string str = param.get<std::string>();

            // Trim whitespace
            str.erase(str.find_last_not_of(" \t\n\r\f\v") + 1);
            str.erase(0, str.find_first_not_of(" \t\n\r\f\v"));

            // Check if it's a JSON array string
            if (!str.empty() && str[0] == '[' && str.back() == ']') {
                try {
                    // Parse the string as JSON
                    json array_json = json::parse(str);
                    if (array_json.is_array()) {
                        // Process each element in the parsed array
                        for (const auto& element : array_json) {
                            ea_t addr = parse_single_address_value(element);
                            addresses.push_back(addr);
                        }
                    } else {
                        // Not a valid array, treat as single address
                        ea_t addr = parse_single_address_value(param);
                        addresses.push_back(addr);
                    }
                } catch (const json::parse_error& e) {
                    // Failed to parse as JSON array, treat as single address
                    ea_t addr = parse_single_address_value(param);
                    addresses.push_back(addr);
                }
            } else {
                // Regular string address
                ea_t addr = parse_single_address_value(param);
                addresses.push_back(addr);
            }
        } else {
            // Single value - parse and return as single-element vector
            ea_t addr = parse_single_address_value(param);
            addresses.push_back(addr);
        }

        return addresses;

    } catch (const std::invalid_argument& e) {
        throw;
    } catch (const std::exception& e) {
        throw std::invalid_argument("Failed to parse address list parameter: " + std::string(e.what()));
    }
}

// Extract the core parsing logic into a separate function that handles a single value
ea_t ActionExecutor::parse_single_address_value(const json& param) {
    if (param.is_string()) {
        std::string str = param.get<std::string>();

        // Trim whitespace
        str.erase(str.find_last_not_of(" \t\n\r\f\v") + 1);
        str.erase(0, str.find_first_not_of(" \t\n\r\f\v"));

        if (str.empty()) {
            throw std::invalid_argument("Empty address string");
        }

        // Remove underscores used as separators (e.g., 0x1234_5678)
        str.erase(std::remove(str.begin(), str.end(), '_'), str.end());

        // Remove commas used as thousands separators
        str.erase(std::remove(str.begin(), str.end(), ','), str.end());

        // Handle optional + prefix
        bool has_plus = false;
        if (!str.empty() && str[0] == '+') {
            has_plus = true;
            str = str.substr(1);
        }

        // Binary format: 0b or 0B prefix
        if (str.length() >= 3 && (str.substr(0, 2) == "0b" || str.substr(0, 2) == "0B")) {
            std::string bin_part = str.substr(2);
            for (char c : bin_part) {
                if (c != '0' && c != '1') {
                    throw std::invalid_argument("Invalid binary character in: " + str);
                }
            }
            return std::stoull(bin_part, nullptr, 2);
        }

        // Hex formats: 0x, 0X, $, &, suffix h/H
        if ((str.length() >= 3 && (str.substr(0, 2) == "0x" || str.substr(0, 2) == "0X")) ||
            (str.length() >= 2 && (str[0] == '$' || str[0] == '&')) ||
            (str.length() >= 2 && (str.back() == 'h' || str.back() == 'H'))) {

            std::string hex_part;
            if (str.back() == 'h' || str.back() == 'H') {
                hex_part = str.substr(0, str.length() - 1);
            } else if (str[0] == '$' || str[0] == '&') {
                hex_part = str.substr(1);
            } else {
                hex_part = str.substr(2);
            }

            // Validate hex characters
            for (char c : hex_part) {
                if (!std::isxdigit(c)) {
                    throw std::invalid_argument("Invalid hex character in: " + str);
                }
            }

            return std::stoull(hex_part, nullptr, 16);
        }

        // Octal format: 0 prefix (not 0x) or 0o prefix
        if (str.length() >= 2) {
            if (str.substr(0, 2) == "0o" || str.substr(0, 2) == "0O") {
                std::string oct_part = str.substr(2);
                for (char c : oct_part) {
                    if (c < '0' || c > '7') {
                        throw std::invalid_argument("Invalid octal character in: " + str);
                    }
                }
                return std::stoull(oct_part, nullptr, 8);
            } else if (str[0] == '0' && str.length() > 1 && std::isdigit(str[1])) {
                // Traditional octal (leading 0)
                for (size_t i = 1; i < str.length(); ++i) {
                    if (str[i] < '0' || str[i] > '7') {
                        // Not a valid octal, treat as decimal with leading zero
                        goto decimal_parse;
                    }
                }
                return std::stoull(str, nullptr, 8);
            }
        }

        decimal_parse:
        // Decimal - validate all digits
        for (char c : str) {
            if (!std::isdigit(c)) {
                throw std::invalid_argument("Invalid decimal character in: " + str);
            }
        }
        return std::stoull(str, nullptr, 10);

    } else if (param.is_number_integer()) {
        if (param.is_number_unsigned()) {
            uint64_t val = param.get<uint64_t>();
            if (val > std::numeric_limits<ea_t>::max()) {
                throw std::invalid_argument("Address value too large");
            }
            return static_cast<ea_t>(val);
        } else {
            int64_t val = param.get<int64_t>();
            if (val < 0) {
                throw std::invalid_argument("Address cannot be negative");
            }
            if (static_cast<uint64_t>(val) > std::numeric_limits<ea_t>::max()) {
                throw std::invalid_argument("Address value too large");
            }
            return static_cast<ea_t>(val);
        }

    } else if (param.is_number_float()) {
        double val = param.get<double>();
        if (val < 0) {
            throw std::invalid_argument("Address cannot be negative");
        }
        if (val > std::numeric_limits<ea_t>::max()) {
            throw std::invalid_argument("Address value too large");
        }
        // Check if it's a whole number
        if (val != std::floor(val)) {
            throw std::invalid_argument("Address must be a whole number");
        }
        return static_cast<ea_t>(val);

    } else {
        throw std::invalid_argument("Address parameter must be a number or string");
    }
}

ActionExecutor::ActionExecutor(std::shared_ptr<BinaryMemory> mem) : memory(std::move(mem)) { }

// Consolidated search actions
json ActionExecutor::search_functions(const std::string& pattern, bool named_only, int max_results) {
    json result;
    try {
        std::vector<std::tuple<ea_t, std::string, bool>> functions =
            IDAUtils::search_functions(pattern, named_only, max_results);

        result["success"] = true;
        json funcs_json = json::array();
        for (const auto& func : functions) {
            json func_obj;
            func_obj["address"] = HexAddress(std::get<0>(func));
            func_obj["name"] = std::get<1>(func);
            func_obj["is_user_named"] = std::get<2>(func);
            funcs_json.push_back(func_obj);
        }
        result["functions"] = funcs_json;
        result["count"] = funcs_json.size();

        if (functions.size() == max_results) {
            result["truncated"] = true;
            result["truncated_at"] = max_results;
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::search_globals(const std::string& pattern, int max_results) {
    json result;
    try {
        std::vector<std::tuple<ea_t, std::string, std::string, std::string>> globals =
            IDAUtils::search_globals(pattern, max_results);

        result["success"] = true;
        json globals_json = json::array();
        for (const auto& global : globals) {
            json global_obj;
            global_obj["address"] = HexAddress(std::get<0>(global));
            global_obj["name"] = std::get<1>(global);
            global_obj["value_preview"] = std::get<2>(global);
            global_obj["type"] = std::get<3>(global);
            globals_json.push_back(global_obj);
        }
        result["globals"] = globals_json;
        result["count"] = globals_json.size();

        if (globals.size() == max_results) {
            result["truncated"] = true;
            result["truncated_at"] = max_results;
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::search_strings(const std::string& pattern, int min_length, int max_results) {
    json result;
    try {
        std::vector<std::pair<ea_t, std::string>> strings =
            IDAUtils::search_strings_unified(pattern, min_length, max_results);

        result["success"] = true;
        json strings_json = json::array();
        for (const auto& str : strings) {
            json str_obj;
            str_obj["address"] = HexAddress(str.first);
            str_obj["content"] = str.second;
            strings_json.push_back(str_obj);
        }
        result["strings"] = strings_json;
        result["count"] = strings_json.size();

        if (strings.size() == max_results) {
            result["truncated"] = true;
            result["truncated_at"] = max_results;
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

// Unified info actions
json ActionExecutor::get_function_info(ea_t address) {
    json result;
    try {
        auto info = IDAUtils::get_function_info(address);
        result["success"] = true;
        result["address"] = HexAddress(address);
        result["name"] = info.name;
        result["start_address"] = HexAddress(info.start_ea);
        result["end_address"] = HexAddress(info.end_ea);
        result["size"] = info.size;
        result["xrefs_to_count"] = info.xrefs_to_count;
        result["xrefs_from_count"] = info.xrefs_from_count;
        result["string_refs_count"] = info.string_refs_count;
        result["data_refs_count"] = info.data_refs_count;
        result["is_library"] = info.is_library;
        result["is_thunk"] = info.is_thunk;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_data_info(ea_t address, int max_xrefs) {
    json result;
    try {
        auto info = IDAUtils::get_data_info(address, max_xrefs);
        result["success"] = true;
        result["address"] = HexAddress(address);
        result["name"] = info.name;
        result["value"] = info.value;
        result["type"] = info.type;
        result["size"] = info.size;

        // Add xrefs
        json xrefs_to_json = json::array();
        for (const auto& xref : info.xrefs_to) {
            json xref_obj;
            xref_obj["address"] = HexAddress(xref.first);
            xref_obj["name"] = xref.second;
            xrefs_to_json.push_back(xref_obj);
        }
        result["xrefs_to"] = xrefs_to_json;
        result["xrefs_to_count"] = info.xrefs_to.size();

        // Add truncation logging
        if (info.xrefs_truncated) {
            result["truncated"] = true;
            result["truncated_at"] = info.xrefs_truncated_at;
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::dump_data(ea_t address, size_t size, int bytes_per_line) {
    json result;
    try {
        std::string hex_dump = IDAUtils::dump_data(address, size, bytes_per_line);

        result["success"] = true;
        result["address"] = HexAddress(address);
        result["size"] = size;
        result["hex_dump"] = hex_dump;

        // // Also include just the raw hex bytes for easier processing
        // bytevec_t bytes;
        // bytes.resize(size);
        // if (IDAUtils::get_bytes_raw(address, &bytes[0], size)) {
        //     std::stringstream raw_hex;
        //     raw_hex << std::hex << std::setfill('0');
        //     for (size_t i = 0; i < size; i++) {
        //         raw_hex << std::setw(2) << static_cast<int>(bytes[i]);
        //         if (i < size - 1) raw_hex << " ";
        //     }
        //     result["raw_hex"] = raw_hex.str();
        // }

    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::analyze_function(ea_t address, bool include_disasm, bool include_decomp, int max_xrefs) {
    json result;
    try {
        // Get basic info first
        auto info = IDAUtils::get_function_info(address);
        result["success"] = true;
        result["address"] = HexAddress(address);
        result["name"] = info.name;
        result["start_address"] = HexAddress(info.start_ea);
        result["end_address"] = HexAddress(info.end_ea);
        result["size"] = info.size;

        // Get cross-references
        auto xrefs_to = IDAUtils::get_xrefs_to_with_names(address, max_xrefs);
        auto xrefs_from = IDAUtils::get_xrefs_from_with_names(address, max_xrefs);

        json xrefs_to_json = json::array();
        for (const auto& xref : xrefs_to) {
            json xref_obj;
            xref_obj["address"] = HexAddress(xref.first);
            xref_obj["name"] = xref.second;
            xrefs_to_json.push_back(xref_obj);
        }
        result["xrefs_to"] = xrefs_to_json;

        json xrefs_from_json = json::array();
        for (const auto& xref : xrefs_from) {
            json xref_obj;
            xref_obj["address"] = HexAddress(xref.first);
            xref_obj["name"] = xref.second;
            xrefs_from_json.push_back(xref_obj);
        }
        result["xrefs_from"] = xrefs_from_json;

        // Get string and data refs
        auto string_refs = IDAUtils::get_function_string_refs(address, max_xrefs);
        result["string_refs"] = string_refs;

        auto data_refs = IDAUtils::get_function_data_refs(address, max_xrefs);
        json data_refs_json = json::array();
        for (ea_t addr : data_refs) {
            data_refs_json.push_back(HexAddress(addr));
        }
        result["data_refs"] = data_refs_json;

        // Include code if requested
        if (include_decomp) {
            result["decompilation"] = IDAUtils::get_function_decompilation(address);
        }
        if (include_disasm) {
            result["disassembly"] = IDAUtils::get_function_disassembly(address);
        }

        // Update memory
        std::set<ea_t> callers, callees;
        for (const auto& xref : xrefs_to) callers.insert(xref.first);
        for (const auto& xref : xrefs_from) callees.insert(xref.first);
        memory->update_function_relationships(address, callers, callees);
        memory->update_function_refs(address, string_refs, data_refs);

    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

// Simplified cross-reference action
json ActionExecutor::get_xrefs(ea_t address, int max_results) {
    json result;
    try {
        auto xrefs_to = IDAUtils::get_xrefs_to_with_names(address, max_results);
        auto xrefs_from = IDAUtils::get_xrefs_from_with_names(address, max_results);

        result["success"] = true;

        json xrefs_to_json = json::array();
        for (const auto& xref : xrefs_to) {
            json xref_obj;
            xref_obj["address"] = HexAddress(xref.first);
            xref_obj["name"] = xref.second;
            xrefs_to_json.push_back(xref_obj);
        }
        result["xrefs_to"] = xrefs_to_json;
        result["xrefs_to_count"] = xrefs_to.size();

        json xrefs_from_json = json::array();
        for (const auto& xref : xrefs_from) {
            json xref_obj;
            xref_obj["address"] = HexAddress(xref.first);
            xref_obj["name"] = xref.second;
            xrefs_from_json.push_back(xref_obj);
        }
        result["xrefs_from"] = xrefs_from_json;
        result["xrefs_from_count"] = xrefs_from.size();

        if (xrefs_to.size() == max_results || xrefs_from.size() == max_results) {
            result["truncated"] = true;
            result["truncated_at"] = max_results;
        }

        // Update memory
        std::set<ea_t> callers, callees;
        for (const auto& xref : xrefs_to) callers.insert(xref.first);
        for (const auto& xref : xrefs_from) callees.insert(xref.first);
        memory->update_function_relationships(address, callers, callees);

    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

// Unified name/comment actions
json ActionExecutor::set_name(ea_t address, const std::string& name) {
    json result;
    try {
        bool success = IDAUtils::set_name(address, name);
        result["success"] = success;
        if (!success) {
            result["error"] = "Failed to set name";
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::set_comment(ea_t address, const std::string& comment) {
    json result;
    try {
        bool success;
        if (comment.empty()) {
            // Clear comment
            success = IDAUtils::clear_disassembly_comment(address) &&
                     IDAUtils::clear_pseudocode_comments(address);
        } else {
            // Set comment
            success = IDAUtils::add_disassembly_comment(address, comment) &&
                     IDAUtils::add_pseudocode_comment(address, comment);
        }
        result["success"] = success;
        if (!success) {
            result["error"] = "Failed to set comment";
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

// Binary info actions
// Binary info actions
json ActionExecutor::get_imports(int max_results) {
    json result;
    try {
        std::map<std::string, std::vector<std::string>> imports = IDAUtils::get_imports();
        result["success"] = true;

        int count = 0;
        json imports_json = json::object();
        bool truncated = false;

        for (const auto& [library, functions] : imports) {
            std::vector<std::string> limited_functions;

            for (const std::string& function : functions) {
                if (count >= max_results) {
                    truncated = true;
                    break;
                }
                limited_functions.push_back(function);
                count++;
            }

            if (!limited_functions.empty()) {
                imports_json[library] = limited_functions;
            }

            if (truncated) break;
        }

        result["imports"] = imports_json;
        result["count"] = count;

        if (truncated) {
            result["truncated"] = true;
            result["truncated_at"] = max_results;
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_entry_points(int max_results) {
    json result;
    try {
        std::vector<std::tuple<ea_t, std::string, std::string>> entries = IDAUtils::get_entry_points();
        result["success"] = true;
        json entries_json = json::array();

        int count = 0;
        bool truncated = false;

        for (const auto& entry : entries) {
            if (count >= max_results) {
                truncated = true;
                break;
            }

            json entry_obj;
            entry_obj["address"] = HexAddress(std::get<0>(entry));
            entry_obj["type"] = std::get<1>(entry);
            entry_obj["name"] = std::get<2>(entry);
            entries_json.push_back(entry_obj);
            count++;
        }

        result["entry_points"] = entries_json;
        result["count"] = count;

        if (truncated) {
            result["truncated"] = true;
            result["truncated_at"] = max_results;
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

// Consolidated knowledge management
json ActionExecutor::store_analysis(const std::string& key, const std::string& content,
                                   std::optional<ea_t> address, const std::string& type,
                                   const std::vector<ea_t>& related_addresses) {
    json result;
    try {
        memory->store_analysis(key, content, address, type, related_addresses);
        result["success"] = true;
        result["key"] = key;
        result["type"] = type;
        if (address) {
            result["address"] = HexAddress(*address);
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_analysis(const std::string& key, std::optional<ea_t> address,
                                 const std::string& type, const std::string& pattern) {
    json result;
    try {
        auto analyses = memory->get_analysis(key, address, type, pattern);
        result["success"] = true;

        json analyses_json = json::array();
        for (const auto& analysis : analyses) {
            json analysis_obj;
            analysis_obj["key"] = analysis.key;
            analysis_obj["content"] = analysis.content;
            analysis_obj["type"] = analysis.type;
            if (analysis.address) {
                analysis_obj["address"] = HexAddress(*analysis.address);
            }
            if (!analysis.related_addresses.empty()) {
                json related = json::array();
                for (ea_t addr : analysis.related_addresses) {
                    related.push_back(HexAddress(addr));
                }
                analysis_obj["related_addresses"] = related;
            }
            analyses_json.push_back(analysis_obj);
        }

        result["analyses"] = analyses_json;
        result["count"] = analyses_json.size();
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

// Batch operations
json ActionExecutor::analyze_functions(const std::vector<ea_t>& addresses, int level, const std::string& group_name) {
    json result;
    try {
        result["success"] = true;
        result["group_name"] = group_name;

        json functions_json = json::array();

        for (ea_t address : addresses) {
            json func_result;
            func_result["address"] = HexAddress(address);

            try {
                // Get basic info
                auto info = IDAUtils::get_function_info(address);
                func_result["name"] = info.name;
                func_result["size"] = info.size;

                if (level >= 1) {
                    // Include decompilation
                    func_result["decompilation"] = IDAUtils::get_function_decompilation(address);

                    // Include string refs
                    auto string_refs = IDAUtils::get_function_string_refs(address, 10);
                    func_result["string_refs"] = string_refs;
                }

                if (level >= 2) {
                    // Include disassembly
                    func_result["disassembly"] = IDAUtils::get_function_disassembly(address);

                    // Include cross-references
                    auto xrefs_to = IDAUtils::get_xrefs_to_with_names(address, 10);
                    auto xrefs_from = IDAUtils::get_xrefs_from_with_names(address, 10);

                    json xrefs_to_json = json::array();
                    for (const auto& xref : xrefs_to) {
                        json xref_obj;
                        xref_obj["address"] = HexAddress(xref.first);
                        xref_obj["name"] = xref.second;
                        xrefs_to_json.push_back(xref_obj);
                    }
                    func_result["xrefs_to"] = xrefs_to_json;

                    json xrefs_from_json = json::array();
                    for (const auto& xref : xrefs_from) {
                        json xref_obj;
                        xref_obj["address"] = HexAddress(xref.first);
                        xref_obj["name"] = xref.second;
                        xrefs_from_json.push_back(xref_obj);
                    }
                    func_result["xrefs_from"] = xrefs_from_json;
                }

                func_result["success"] = true;

                // Update memory
                memory->set_function_analysis(address, static_cast<DetailLevel>(level),
                                            "Analyzed as part of batch: " + group_name);

            } catch (const std::exception& e) {
                func_result["success"] = false;
                func_result["error"] = e.what();
            }

            functions_json.push_back(func_result);
        }

        result["functions"] = functions_json;
        result["count"] = functions_json.size();

        // Store as cluster if group name provided
        if (!group_name.empty()) {
            memory->analyze_cluster(addresses, group_name, static_cast<DetailLevel>(level));
        }

    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

// Context and workflow
json ActionExecutor::get_analysis_context(std::optional<ea_t> address, int radius) {
    json result;
    try {
        // Use current focus if no address provided
        ea_t context_addr = address.value_or(memory->get_current_focus());

        result["success"] = true;
        result["center_address"] = HexAddress(context_addr);

        // Get memory context
        MemoryContext context = memory->get_memory_context(context_addr, radius);

        // Add nearby functions
        json nearby = json::array();
        for (const FunctionMemory& func : context.nearby_functions) {
            json func_obj;
            func_obj["address"] = HexAddress(func.address);
            func_obj["name"] = func.name;
            func_obj["distance_from_center"] = func.distance_from_anchor;
            func_obj["analysis_level"] = static_cast<int>(func.current_level);
            nearby.push_back(func_obj);
        }
        result["nearby_functions"] = nearby;

        // Add context functions
        json context_funcs = json::array();
        for (const FunctionMemory& func : context.context_functions) {
            json func_obj;
            func_obj["address"] = HexAddress(func.address);
            func_obj["name"] = func.name;
            func_obj["distance_from_center"] = func.distance_from_anchor;
            func_obj["analysis_level"] = static_cast<int>(func.current_level);
            context_funcs.push_back(func_obj);
        }
        result["context_functions"] = context_funcs;

        // Add exploration frontier
        auto frontier = memory->get_exploration_frontier();
        json frontier_json = json::array();
        for (const auto& item : frontier) {
            json item_obj;
            item_obj["address"] = HexAddress(std::get<0>(item));
            item_obj["name"] = std::get<1>(item);
            item_obj["reason"] = std::get<2>(item);
            frontier_json.push_back(item_obj);
        }
        result["exploration_frontier"] = frontier_json;

        // Add analysis queue
        auto queue = memory->get_analysis_queue();
        json queue_json = json::array();
        for (const auto& item : queue) {
            json item_obj;
            item_obj["address"] = HexAddress(std::get<0>(item));
            item_obj["reason"] = std::get<1>(item);
            item_obj["priority"] = std::get<2>(item);

            // Get function name
            std::string name = IDAUtils::get_function_name(std::get<0>(item));
            item_obj["name"] = name;

            queue_json.push_back(item_obj);
        }
        result["analysis_queue"] = queue_json;

        // Add LLM memory summary
        result["memory_summary"] = context.llm_memory;

        // Add stats
        auto analyzed = memory->get_analyzed_functions();
        result["total_analyzed_functions"] = analyzed.size();

    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::mark_for_analysis(ea_t address, const std::string& reason, int priority) {
    json result;
    try {
        memory->mark_for_analysis(address, reason, priority);
        result["success"] = true;
        result["address"] = HexAddress(address);
        result["reason"] = reason;
        result["priority"] = priority;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::set_current_focus(ea_t address) {
    json result;
    try {
        memory->set_current_focus(address);
        result["success"] = true;
        result["address"] = HexAddress(address);
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

} // namespace llm_re