//
// Created by user on 6/29/25.
//

#ifndef IDA_VALIDATORS_H
#define IDA_VALIDATORS_H

#include "common.h"

namespace llm_re {

// NOT THREAD SAFE, MUST be used in execute_sync_wrapper
class IDAValidators {
public:
    static bool is_valid_function(ea_t address) {
        if (address == BADADDR) return false;
        func_t* func = get_func(address);
        return func != nullptr;
    }

    static bool is_valid_address(ea_t address) {
        if (address == BADADDR) return false;
        return is_mapped(address);
    }

    static bool is_valid_data_address(ea_t address) {
        if (!is_valid_address(address)) return false;
        flags_t flags = get_flags(address);
        return is_data(flags);
    }

    static bool is_valid_name(const std::string& name) {
        if (name.empty() || name.length() > 256) return false;
        // Check for invalid characters
        for (char c : name) {
            if (!isalnum(c) && c != '_' && c != '@' && c != '?' && c != '$') {
                return false;
            }
        }
        return true;
    }

    static ea_t validate_address_param(const json& params, const std::string& key) {
        if (!params.contains(key)) {
            throw std::invalid_argument("Missing parameter: " + key);
        }
        ea_t address = params[key].get<ea_t>();
        if (!is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: 0x" + std::to_string(address));
        }
        return address;
    }

    static ea_t validate_function_address(const json& params, const std::string& key) {
        ea_t address = validate_address_param(params, key);
        if (!is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: 0x" + std::to_string(address));
        }
        return address;
    }

    static std::string validate_string_param(const json& params, const std::string& key, size_t max_length = 1024) {
        if (!params.contains(key)) {
            throw std::invalid_argument("Missing parameter: " + key);
        }
        std::string value = params[key].get<std::string>();
        if (value.length() > max_length) {
            throw std::invalid_argument("String too long for " + key + " (max " + std::to_string(max_length) + ")");
        }
        return value;
    }
};

} // namespace llm_re

#endif //IDA_VALIDATORS_H