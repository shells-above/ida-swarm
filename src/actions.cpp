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



ActionExecutor::ActionExecutor(std::shared_ptr<BinaryMemory> mem) : memory(std::move(mem)) {
    register_actions();
}

void ActionExecutor::register_actions() {
    // IDA Core Actions
    action_map["get_xrefs_to"] = [this](const json& params) -> json {
        return get_xrefs_to(parse_single_address_value(params["address"]));
    };

    action_map["get_xrefs_from"] = [this](const json& params) -> json {
        return get_xrefs_from(parse_single_address_value(params["address"]));
    };

    action_map["get_function_disassembly"] = [this](const json& params) -> json {
        return get_function_disassembly(parse_single_address_value(params["address"]));
    };

    action_map["get_function_decompilation"] = [this](const json& params) -> json {
        return get_function_decompilation(parse_single_address_value(params["address"]));
    };

    action_map["get_function_address"] = [this](const json& params) -> json {
        return get_function_address(params["name"]);
    };

    action_map["get_function_name"] = [this](const json& params) -> json {
        return get_function_name(parse_single_address_value(params["address"]));
    };

    action_map["set_function_name"] = [this](const json& params) -> json {
        return set_function_name(parse_single_address_value(params["address"]), params["name"]);
    };

    action_map["get_function_string_refs"] = [this](const json& params) -> json {
        return get_function_string_refs(parse_single_address_value(params["address"]));
    };

    action_map["get_function_data_refs"] = [this](const json& params) -> json {
        return get_function_data_refs(parse_single_address_value(params["address"]));
    };

    action_map["get_data_name"] = [this](const json& params) -> json {
        return get_data_name(parse_single_address_value(params["address"]));
    };

    action_map["set_data_name"] = [this](const json& params) -> json {
        return set_data_name(parse_single_address_value(params["address"]), params["name"]);
    };

    action_map["get_data"] = [this](const json& params) -> json {
        return get_data(parse_single_address_value(params["address"]));
    };

    action_map["add_disassembly_comment"] = [this](const json& params) -> json {
        return add_disassembly_comment(parse_single_address_value(params["address"]), params["comment"]);
    };

    action_map["add_pseudocode_comment"] = [this](const json& params) -> json {
        return add_pseudocode_comment(parse_single_address_value(params["address"]), params["comment"]);
    };

    action_map["clear_disassembly_comment"] = [this](const json& params) -> json {
        return clear_disassembly_comment(parse_single_address_value(params["address"]));
    };

    action_map["clear_pseudocode_comments"] = [this](const json& params) -> json {
        return clear_pseudocode_comments(parse_single_address_value(params["address"]));
    };

    action_map["get_imports"] = [this](const json& params) -> json {
        return get_imports();
    };

    action_map["get_exports"] = [this](const json& params) -> json {
        return get_exports();
    };

    action_map["search_strings"] = [this](const json& params) -> json {
        return search_strings(params["text"], params.value("is_case_sensitive", false));
    };

    // Memory System Actions
    action_map["set_global_note"] = [this](const json& params) -> json {
        return set_global_note(params["key"], params["content"]);
    };

    action_map["get_global_note"] = [this](const json& params) -> json {
        return get_global_note(params["key"]);
    };

    action_map["list_global_notes"] = [this](const json& params) -> json {
        return list_global_notes();
    };

    action_map["search_notes"] = [this](const json& params) -> json {
        return search_notes(params["query"]);
    };

    action_map["set_function_analysis"] = [this](const json& params) -> json {
        return set_function_analysis(parse_single_address_value(params["address"]), params["level"], params["analysis"]);
    };

    action_map["get_function_analysis"] = [this](const json& params) -> json {
        return get_function_analysis(parse_single_address_value(params["address"]), params.value("level", 0));
    };

    action_map["get_memory_context"] = [this](const json& params) -> json {
        return get_memory_context(parse_single_address_value(params["address"]), params.value("radius", 2));
    };

    action_map["get_analyzed_functions"] = [this](const json& params) -> json {
        return get_analyzed_functions();
    };

    action_map["find_functions_by_pattern"] = [this](const json& params) -> json {
        return find_functions_by_pattern(params["pattern"]);
    };

    action_map["get_exploration_frontier"] = [this](const json& params) -> json {
        return get_exploration_frontier();
    };

    action_map["mark_for_analysis"] = [this](const json& params) -> json {
        return mark_for_analysis(parse_single_address_value(params["address"]), params["reason"], params.value("priority", 5));
    };

    action_map["get_analysis_queue"] = [this](const json& params) -> json {
        return get_analysis_queue();
    };

    action_map["set_current_focus"] = [this](const json& params) -> json {
        return set_current_focus(parse_single_address_value(params["address"]));
    };

    action_map["add_insight"] = [this](const json& params) -> json {
        return add_insight(params["type"], params["description"], parse_list_address_param(params, "related_addresses"));
    };

    action_map["get_insights"] = [this](const json& params) -> json {
        return get_insights(params.value("type", ""));
    };

    action_map["analyze_cluster"] = [this](const json& params) -> json {
        return analyze_cluster(params["addresses"].get<std::vector<ea_t>>(), params["cluster_name"], params["initial_level"]);
    };

    action_map["get_cluster_analysis"] = [this](const json& params) -> json {
        return get_cluster_analysis(params["cluster_name"]);
    };

    action_map["summarize_region"] = [this](const json& params) -> json {
        return summarize_region(params["start_addr"], params["end_addr"]);
    };
}

json ActionExecutor::get_xrefs_to(ea_t address) {
    json result;
    try {
        std::vector<ea_t> xrefs = IDAUtils::get_xrefs_to(address);
        result["success"] = true;
        json xrefs_json = json::array();
        for (ea_t addr : xrefs) {
            xrefs_json.push_back(HexAddress(addr));
        }
        result["xrefs"] = xrefs_json;

        // Update memory with caller information
        std::set<ea_t> callers(xrefs.begin(), xrefs.end());
        memory->update_function_relationships(address, callers, {});
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_xrefs_from(ea_t address) {
    json result;
    try {
        std::vector<ea_t> xrefs = IDAUtils::get_xrefs_from(address);
        result["success"] = true;
        json xrefs_json = json::array();
        for (ea_t addr : xrefs) {
            xrefs_json.push_back(HexAddress(addr));
        }
        result["xrefs"] = xrefs_json;

        // Update memory with callee information
        std::set<ea_t> callees(xrefs.begin(), xrefs.end());
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

json ActionExecutor::get_function_string_refs(ea_t address) {
    json result;
    try {
        std::vector<std::string> strings = IDAUtils::get_function_string_refs(address);
        result["success"] = true;
        result["strings"] = strings;

        // Update memory
        memory->update_function_refs(address, strings, {});
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_function_data_refs(ea_t address) {
    json result;
    try {
        std::vector<ea_t> data_refs = IDAUtils::get_function_data_refs(address);
        result["success"] = true;
        json data_refs_json = json::array();
        for (ea_t addr : data_refs) {
            data_refs_json.push_back(HexAddress(addr));
        }
        result["data_refs"] = data_refs_json;

        // Update memory
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

json ActionExecutor::add_disassembly_comment(ea_t address, const std::string& comment) {
    json result;
    try {
        bool success = IDAUtils::add_disassembly_comment(address, comment);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::add_pseudocode_comment(ea_t address, const std::string& comment) {
    json result;
    try {
        bool success = IDAUtils::add_pseudocode_comment(address, comment);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::clear_disassembly_comment(ea_t address) {
    json result;
    try {
        bool success = IDAUtils::clear_disassembly_comment(address);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::clear_pseudocode_comments(ea_t address) {
    json result;
    try {
        bool success = IDAUtils::clear_pseudocode_comments(address);
        result["success"] = success;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_imports() {
    json result;
    try {
        std::map<std::string, std::vector<std::string>> imports = IDAUtils::get_imports();
        result["success"] = true;
        result["imports"] = imports;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::get_exports() {
    json result;
    try {
        std::vector<std::pair<std::string, ea_t>> exports = IDAUtils::get_exports();
        result["success"] = true;
        json exports_json = json::array();
        for (const std::pair<std::string, ea_t>& exp: exports) {
            json exp_obj;
            exp_obj["name"] = exp.first;
            exp_obj["address"] = HexAddress(exp.second);
            exports_json.push_back(exp_obj);
        }
        result["exports"] = exports_json;
    } catch (const std::exception& e) {
        result["success"] = false;
        result["error"] = e.what();
    }
    return result;
}

json ActionExecutor::search_strings(const std::string& text, bool is_case_sensitive) {
    json result;
    try {
        std::vector<std::string> strings = IDAUtils::search_strings(text, is_case_sensitive);
        result["success"] = true;
        result["strings"] = strings;
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
        json addresses_json = json::array();
        for (ea_t addr : addresses) {
            addresses_json.push_back(HexAddress(addr));
        }
        result["addresses"] = addresses_json;
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
            item_obj["address"] = HexAddress(std::get<0>(item));
            item_obj["reason"] = std::get<1>(item);
            item_obj["priority"] = std::get<2>(item);
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

json ActionExecutor::execute_action(const std::string& action_name, const json& params) {
    auto it = action_map.find(action_name);
    if (it != action_map.end()) {
        try {
            return it->second(params);
        } catch (const std::exception& e) {
            json error_result;
            error_result["success"] = false;
            error_result["error"] = "Action execution failed: " + std::string(e.what());
            return error_result;
        }
    } else {
        json error_result;
        error_result["success"] = false;
        error_result["error"] = "Unknown action: " + action_name;
        return error_result;
    }
}

void ActionExecutor::log_action(const std::string& action, ea_t address,
                               const std::string& old_value, const std::string& new_value,
                               bool success, const std::string& error_msg) {
    std::lock_guard<std::mutex> lock(audit_mutex);

    AuditEntry entry{
        .timestamp = std::time(nullptr),
        .action = action,
        .address = address,
        .old_value = old_value,
        .new_value = new_value,
        .success = success,
        .error_message = error_msg
    };

    audit_log.push_back(entry);

    // Keep only last 10000 entries to prevent unbounded growth
    if (audit_log.size() > 10000) {
        audit_log.erase(audit_log.begin(), audit_log.begin() + 1000);
    }
}

void ActionExecutor::save_audit_log(const std::string& filename) const {
    std::lock_guard<std::mutex> lock(audit_mutex);

    json log_json = json::array();
    for (const auto& entry : audit_log) {
        json entry_json{
                {"timestamp", entry.timestamp},
                {"action", entry.action},
                {"address", entry.address},
                {"old_value", entry.old_value},
                {"new_value", entry.new_value},
                {"success", entry.success},
                {"error_message", entry.error_message}
        };
        log_json.push_back(entry_json);
    }

    std::ofstream file(filename);
    if (file.is_open()) {
        file << log_json.dump(2);
        file.close();
    }
}

std::vector<AuditEntry> ActionExecutor::get_recent_audit_entries(size_t count) const {
    std::lock_guard<std::mutex> lock(audit_mutex);

    std::vector<AuditEntry> recent;
    size_t start = (audit_log.size() > count) ? audit_log.size() - count : 0;

    for (size_t i = start; i < audit_log.size(); ++i) {
        recent.push_back(audit_log[i]);
    }

    return recent;
}

} // namespace llm_re