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

/*
 * Supported formats:
 * - Single values: Same as before
 * - Arrays: ["0x100006008", "0x100007d55"], [16384, "0x4000", "$8000"]
 * - String arrays: "[0x100006008, 0x100007d55]", "[0x5000, 0x4000]"
 * - Mixed formats in arrays: ["0x4000", 16384, "040000", "0b100"]
 *
 * Examples:
 * - "address": "0x4000" -> single address
 * - "related_addresses": ["0x100006008", "0x100007d55"] -> array of addresses
 * - "related_addresses": "[0x100006008, 0x100007d55]" -> string containing array
 * - "targets": [16384, "0x4000", "$8000", "16,384"] -> mixed format array
 * - "targets": "[0x5000, 0x4000]" -> string containing hex array
 */


ActionExecutor::ActionExecutor(std::shared_ptr<BinaryMemory> mem) : memory(std::move(mem)) { }

json ActionExecutor::get_xrefs_to(ea_t address, int max_count) {
    json result;
    try {
        std::vector<std::pair<ea_t, std::string>> xrefs = IDAUtils::get_xrefs_to_with_names(address, max_count);
        result["success"] = true;
        json xrefs_json = json::array();
        for (const auto& xref : xrefs) {
            json xref_obj;
            xref_obj["address"] = HexAddress(xref.first);
            xref_obj["name"] = xref.second;
            xrefs_json.push_back(xref_obj);
        }
        result["xrefs"] = xrefs_json;
        result["count"] = xrefs_json.size();

        if (xrefs.size() == max_count) {
            result["truncated"] = true;
            result["message"] = "Results truncated to " + std::to_string(max_count) + " entries";
        }

        // Update memory with caller information (limited set)
        std::set<ea_t> callers;
        for (const auto& xref : xrefs) {
            callers.insert(xref.first);
        }
        memory->update_function_relationships(address, callers, {});
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_xrefs_from(ea_t address, int max_count) {
    json result;
    try {
        std::vector<std::pair<ea_t, std::string>> xrefs = IDAUtils::get_xrefs_from_with_names(address, max_count);
        result["success"] = true;
        json xrefs_json = json::array();
        for (const auto& xref : xrefs) {
            json xref_obj;
            xref_obj["address"] = HexAddress(xref.first);
            xref_obj["name"] = xref.second;
            xrefs_json.push_back(xref_obj);
        }
        result["xrefs"] = xrefs_json;
        result["count"] = xrefs_json.size();

        if (xrefs.size() == max_count) {
            result["truncated"] = true;
            result["message"] = "Results truncated to " + std::to_string(max_count) + " entries";
        }

        // Update memory with callee information (limited set)
        std::set<ea_t> callees;
        for (const auto& xref : xrefs) {
            callees.insert(xref.first);
        }
        memory->update_function_relationships(address, {}, callees);
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_disassembly(ea_t address) {
    json result;
    try {
        std::string disasm = IDAUtils::get_function_disassembly(address);
        result["success"] = true;
        result["disassembly"] = disasm;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_decompilation(ea_t address) {
    json result;
    try {
        std::string decomp = IDAUtils::get_function_decompilation(address);
        result["success"] = true;
        result["decompilation"] = decomp;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_address(const std::string& name) {
    json result;
    try {
        ea_t addr = IDAUtils::get_function_address(name);
        result["success"] = (addr != BADADDR);
        result["address"] = HexAddress(addr);
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_name(ea_t address) {
    json result;
    try {
        std::string name = IDAUtils::get_function_name(address);
        result["success"] = true;
        result["name"] = name;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::set_function_name(ea_t address, const std::string& name) {
    json result;
    try {
        bool success = IDAUtils::set_function_name(address, name);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}


json ActionExecutor::get_function_string_refs(ea_t address, int max_count) {
    json result;
    try {
        std::vector<std::string> strings = IDAUtils::get_function_string_refs(address, max_count);
        result["success"] = true;
        result["strings"] = strings;
        result["count"] = strings.size();

        if (strings.size() == max_count) {
            result["truncated"] = true;
            result["message"] = "Results truncated to " + std::to_string(max_count) + " entries";
        }

        // Update memory (with limited set)
        memory->update_function_refs(address, strings, {});
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}


json ActionExecutor::get_function_data_refs(ea_t address, int max_count) {
    json result;
    try {
        std::vector<ea_t> data_refs = IDAUtils::get_function_data_refs(address, max_count);
        result["success"] = true;
        json data_refs_json = json::array();
        for (ea_t addr : data_refs) {
            data_refs_json.push_back(HexAddress(addr));
        }
        result["data_refs"] = data_refs_json;
        result["count"] = data_refs_json.size();

        if (data_refs.size() == max_count) {
            result["truncated"] = true;
            result["message"] = "Results truncated to " + std::to_string(max_count) + " entries";
        }

        // Update memory (with limited set)
        memory->update_function_refs(address, {}, data_refs);
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_data_name(ea_t address) {
    json result;
    try {
        std::string name = IDAUtils::get_data_name(address);
        result["success"] = true;
        result["name"] = name;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::set_data_name(ea_t address, const std::string& name) {
    json result;
    try {
        bool success = IDAUtils::set_data_name(address, name);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_data(ea_t address) {
    json result;
    try {
        auto data = IDAUtils::get_data(address);
        result["success"] = true;
        result["value"] = data.first;
        result["type"] = data.second;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::add_comment(ea_t address, const std::string& comment) {
    json result;
    try {
        bool success_dis = IDAUtils::add_disassembly_comment(address, comment);
        bool success_dec = IDAUtils::add_pseudocode_comment(address, comment);
        result["success"] = success_dis && success_dec;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::clear_comment(ea_t address) {
    json result;
    try {
        bool success_dis = IDAUtils::clear_disassembly_comment(address);
        bool success_dec = IDAUtils::clear_pseudocode_comments(address);
        result["success"] = success_dis && success_dec;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_imports(int max_count) {
    json result;
    try {
        std::map<std::string, std::vector<std::string>> imports = IDAUtils::get_imports();
        int count = 0;
        for (const auto& [library, functions] : imports) {
            if (count >= max_count) break;

            std::vector<std::string> limited_functions;
            for (const std::string& function: functions) {
                if (count >= max_count) break;
                limited_functions.push_back(function);
                count++;
            }

            if (!limited_functions.empty()) {
                result["imports"][library] = limited_functions;
            }
        }
        result["success"] = true;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::search_strings(const std::string& text, bool is_case_sensitive, int max_count) {
    json result;
    try {
        std::vector<std::string> strings = IDAUtils::search_strings(text, is_case_sensitive, max_count);
        result["success"] = true;
        result["strings"] = strings;
        result["count"] = strings.size();

        if (strings.size() == max_count) {
            result["truncated"] = true;
            result["message"] = "Results truncated to " + std::to_string(max_count) + " entries";
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_named_functions(int max_count) {
    json result;
    try {
        std::vector<std::pair<ea_t, std::string>> functions = IDAUtils::get_named_functions(max_count);
        result["success"] = true;
        json funcs_json = json::array();
        for (const auto& func : functions) {
            json func_obj;
            func_obj["address"] = HexAddress(func.first);
            func_obj["name"] = func.second;
            funcs_json.push_back(func_obj);
        }
        result["functions"] = funcs_json;
        result["count"] = funcs_json.size();
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::search_named_functions(const std::string& text, bool is_case_sensitive, int max_count) {
    json result;
    try {
        std::vector<std::pair<ea_t, std::string>> functions = IDAUtils::search_named_functions(text, is_case_sensitive, max_count);
        result["success"] = true;
        json funcs_json = json::array();
        for (const auto& func : functions) {
            json func_obj;
            func_obj["address"] = HexAddress(func.first);
            func_obj["name"] = func.second;
            funcs_json.push_back(func_obj);
        }
        result["functions"] = funcs_json;
        result["count"] = funcs_json.size();
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::search_named_globals(const std::string& pattern, bool is_regex, int max_count) {
    json result;
    try {
        std::vector<std::pair<ea_t, std::string>> globals = IDAUtils::search_named_globals(pattern, is_regex, max_count);
        result["success"] = true;
        json globals_json = json::array();
        for (const auto& global : globals) {
            json global_obj;
            global_obj["address"] = HexAddress(global.first);
            global_obj["name"] = global.second;
            globals_json.push_back(global_obj);
        }
        result["globals"] = globals_json;
        result["count"] = globals.size();

        if (globals.size() == max_count) {
            result["truncated"] = true;
            result["message"] = "Results truncated to " + std::to_string(max_count) + " entries";
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}


json ActionExecutor::get_global_by_name(const std::string& name) {
    json result;
    try {
        ea_t addr = IDAUtils::get_name_address(name);
        if (addr == BADADDR) {
            result["success"] = false;
            result["error"] = "Global not found: " + name;
            return result;
        }

        // Verify it's not a function
        if (IDAUtils::is_function(addr)) {
            result["success"] = false;
            result["error"] = "Name refers to a function, not a global: " + name;
            return result;
        }

        result["success"] = true;
        result["address"] = HexAddress(addr);
        result["name"] = name;

        // Try to get the data value and type
        try {
            auto data = IDAUtils::get_data(addr);
            result["value"] = data.first;
            result["type"] = data.second;
        } catch (const std::exception& e) {
            result["value"] = "";
            result["type"] = "unknown";
            result["data_error"] = e.what();
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_named_globals(int max_count) {
    json result;
    try {
        std::vector<std::pair<ea_t, std::string>> globals = IDAUtils::get_named_globals();
        result["success"] = true;
        json globals_json = json::array();
        int count = 0;
        for (const auto& global : globals) {
            if (count >= max_count) break;
            json global_obj;
            global_obj["address"] = HexAddress(global.first);
            global_obj["name"] = global.second;
            globals_json.push_back(global_obj);
            count++;
        }
        result["globals"] = globals_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_strings(int min_length, int max_count) {
    json result;
    try {
        std::vector<std::pair<ea_t, std::string>> strings = IDAUtils::get_strings_with_addresses(min_length, max_count);
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

        if (strings.size() == max_count) {
            result["truncated"] = true;
            result["message"] = "Results truncated to " + std::to_string(max_count) + " entries";
        }
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_entry_points() {
    json result;
    try {
        std::vector<std::tuple<ea_t, std::string, std::string>> entries = IDAUtils::get_entry_points();
        result["success"] = true;
        json entries_json = json::array();
        for (const auto& entry : entries) {
            json entry_obj;
            entry_obj["address"] = HexAddress(std::get<0>(entry));
            entry_obj["type"] = std::get<1>(entry);
            entry_obj["name"] = std::get<2>(entry);
            entries_json.push_back(entry_obj);
        }
        result["entry_points"] = entries_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}


// Memory System Actions

json ActionExecutor::set_global_note(const std::string& key, const std::string& content) {
    json result;
    try {
        memory->set_global_note(key, content);
        result["success"] = true;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_global_note(const std::string& key) {
    json result;
    try {
        std::string content = memory->get_global_note(key);
        result["success"] = true;
        result["content"] = content;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::list_global_notes() {
    json result;
    try {
        std::vector<std::string> keys = memory->list_global_notes();
        result["success"] = true;
        result["keys"] = keys;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::search_notes(const std::string& query) {
    json result;
    try {
        std::vector<std::pair<std::string, std::string>> matches = memory->search_notes(query);
        result["success"] = true;
        json matches_json = json::array();
        for (const std::pair<std::string, std::string> &match: matches) {
            json match_obj;
            match_obj["key"] = match.first;
            match_obj["snippet"] = match.second;
            matches_json.push_back(match_obj);
        }
        result["matches"] = matches_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::set_function_analysis(ea_t address, int level, const std::string& analysis) {
    json result;
    try {
        memory->set_function_analysis(address, static_cast<DetailLevel>(level), analysis);
        result["success"] = true;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_analysis(ea_t address, int level) {
    json result;
    try {
        std::string analysis = memory->get_function_analysis(address, static_cast<DetailLevel>(level));
        result["success"] = true;
        result["analysis"] = analysis;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_memory_context(ea_t address, int radius) {
    json result;
    try {
        MemoryContext context = memory->get_memory_context(address, radius);
        result["success"] = true;

        // Convert context to JSON
        json nearby = json::array();
        for (const FunctionMemory& func: context.nearby_functions) {
            json func_obj;
            func_obj["address"] = HexAddress(func.address);
            func_obj["name"] = func.name;
            func_obj["distance_from_anchor"] = func.distance_from_anchor;
            func_obj["current_level"] = static_cast<int>(func.current_level);
            nearby.push_back(func_obj);
        }
        result["nearby_functions"] = nearby;

        json context_funcs = json::array();
        for (const FunctionMemory& func: context.context_functions) {
            json func_obj;
            func_obj["address"] = HexAddress(func.address);
            func_obj["name"] = func.name;
            func_obj["distance_from_anchor"] = func.distance_from_anchor;
            func_obj["current_level"] = static_cast<int>(func.current_level);
            context_funcs.push_back(func_obj);
        }
        result["context_functions"] = context_funcs;

        result["llm_memory"] = context.llm_memory;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_analyzed_functions() {
    json result;
    try {
        std::vector<std::tuple<ea_t, std::string, DetailLevel>> functions = memory->get_analyzed_functions();
        result["success"] = true;
        json funcs_json = json::array();
        for (const std::tuple<ea_t, std::string, DetailLevel>& func: functions) {
            json func_obj;
            func_obj["address"] = HexAddress(std::get<0>(func));
            func_obj["name"] = std::get<1>(func);
            func_obj["max_level"] = static_cast<int>(std::get<2>(func));
            funcs_json.push_back(func_obj);
        }
        result["functions"] = funcs_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

    json ActionExecutor::find_functions_by_pattern(const std::string& pattern) {
    json result;
    try {
        std::vector<ea_t> addresses = memory->find_functions_by_pattern(pattern);
        result["success"] = true;
        json functions_json = json::array();
        for (ea_t addr : addresses) {
            json func_obj;
            func_obj["address"] = HexAddress(addr);

            // Get the function name
            std::string name = IDAUtils::get_function_name(addr);
            func_obj["name"] = name;

            functions_json.push_back(func_obj);
        }
        result["functions"] = functions_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_exploration_frontier() {
    json result;
    try {
        std::vector<std::tuple<ea_t, std::string, std::string>> frontier = memory->get_exploration_frontier();
        result["success"] = true;
        json frontier_json = json::array();
        for (const std::tuple<ea_t, std::string, std::string>& item: frontier) {
            json item_obj;
            item_obj["address"] = HexAddress(std::get<0>(item));
            item_obj["name"] = std::get<1>(item);
            item_obj["reason"] = std::get<2>(item);
            frontier_json.push_back(item_obj);
        }
        result["frontier"] = frontier_json;
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
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_analysis_queue() {
    json result;
    try {
        std::vector<std::tuple<ea_t, std::string, int>> queue = memory->get_analysis_queue();
        result["success"] = true;
        json queue_json = json::array();
        for (const std::tuple<ea_t, std::string, int>& item: queue) {
            json item_obj;
            ea_t addr = std::get<0>(item);
            item_obj["address"] = HexAddress(addr);
            item_obj["reason"] = std::get<1>(item);
            item_obj["priority"] = std::get<2>(item);

            // Get the function name
            std::string name = IDAUtils::get_function_name(addr);
            item_obj["name"] = name;

            queue_json.push_back(item_obj);
        }
        result["queue"] = queue_json;
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
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::add_insight(const std::string& type, const std::string& description, const std::vector<ea_t>& related_addresses) {
    json result;
    try {
        memory->add_insight(type, description, related_addresses);
        result["success"] = true;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_insights(const std::string& type) {
    json result;
    try {
        std::vector<std::tuple<std::string, std::vector<ea_t>>> insights = memory->get_insights(type);
        result["success"] = true;
        json insights_json = json::array();
        for (const std::tuple<std::string, std::vector<ea_t>> &insight: insights) {
            json insight_obj;
            insight_obj["description"] = std::get<0>(insight);
            json addresses_json = json::array();
            for (ea_t addr : std::get<1>(insight)) {
                addresses_json.push_back(HexAddress(addr));
            }
            insight_obj["addresses"] = addresses_json;
            insights_json.push_back(insight_obj);
        }
        result["insights"] = insights_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::analyze_cluster(const std::vector<ea_t>& addresses, const std::string& cluster_name, int initial_level) {
    json result;
    try {
        memory->analyze_cluster(addresses, cluster_name, static_cast<DetailLevel>(initial_level));
        result["success"] = true;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_cluster_analysis(const std::string& cluster_name) {
    json result;
    try {
        std::map<ea_t, std::string> cluster = memory->get_cluster_analysis(cluster_name);
        result["success"] = true;
        json cluster_json = json::object();
        for (const std::pair<const unsigned long long, std::string> &pair: cluster) {
            cluster_json[std::to_string(pair.first)] = pair.second;
        }
        result["cluster"] = cluster_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::summarize_region(ea_t start_addr, ea_t end_addr) {
    json result;
    try {
        std::string summary = memory->summarize_region(start_addr, end_addr);
        result["success"] = true;
        result["summary"] = summary;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

} // namespace llm_re