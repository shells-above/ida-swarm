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
    // Helper function to format addresses as hex strings for error messages
    static std::string format_address_hex(ea_t address) {
        std::stringstream ss;
        ss << "0x" << std::hex << address;
        return ss.str();
    }

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
        
        ea_t address = BADADDR;
        
        try {
            if (params[key].is_string()) {
                std::string str = params[key].get<std::string>();
                
                // Trim whitespace
                str.erase(str.find_last_not_of(" \t\n\r\f\v") + 1);
                str.erase(0, str.find_first_not_of(" \t\n\r\f\v"));
                
                if (str.empty()) {
                    throw std::invalid_argument("Empty address string");
                }
                
                // Handle various hex formats: 0x4000, 0X4000, 4000h, 4000H
                if ((str.length() >= 3 && (str.substr(0, 2) == "0x" || str.substr(0, 2) == "0X")) ||
                    (str.length() >= 2 && (str.back() == 'h' || str.back() == 'H'))) {
                    
                    std::string hex_part = str;
                    if (str.back() == 'h' || str.back() == 'H') {
                        hex_part = str.substr(0, str.length() - 1);  // Remove 'h' or 'H'
                    } else {
                        hex_part = str.substr(2);  // Remove '0x' or '0X'
                    }
                    
                    // Validate hex characters
                    for (char c : hex_part) {
                        if (!std::isxdigit(c)) {
                            throw std::invalid_argument("Invalid hex character in: " + str);
                        }
                    }
                    
                    address = std::stoull(hex_part, nullptr, 16);
                } else {
                    // Try decimal - validate all digits
                    for (char c : str) {
                        if (!std::isdigit(c)) {
                            throw std::invalid_argument("Invalid decimal character in: " + str);
                        }
                    }
                    address = std::stoull(str, nullptr, 10);
                }
                
            } else if (params[key].is_number_integer()) {
                // Handle integer types
                if (params[key].is_number_unsigned()) {
                    uint64_t val = params[key].get<uint64_t>();
                    if (val > std::numeric_limits<ea_t>::max()) {
                        throw std::invalid_argument("Address value too large: " + std::to_string(val));
                    }
                    address = static_cast<ea_t>(val);
                } else {
                    int64_t val = params[key].get<int64_t>();
                    if (val < 0) {
                        throw std::invalid_argument("Address cannot be negative: " + std::to_string(val));
                    }
                    if (static_cast<uint64_t>(val) > std::numeric_limits<ea_t>::max()) {
                        throw std::invalid_argument("Address value too large: " + std::to_string(val));
                    }
                    address = static_cast<ea_t>(val);
                }
                
            } else if (params[key].is_number_float()) {
                // Handle floating point - convert to integer
                double val = params[key].get<double>();
                if (val < 0) {
                    throw std::invalid_argument("Address cannot be negative: " + std::to_string(val));
                }
                if (val > std::numeric_limits<ea_t>::max()) {
                    throw std::invalid_argument("Address value too large: " + std::to_string(val));
                }
                address = static_cast<ea_t>(val);
                
            } else {
                throw std::invalid_argument("Address parameter must be a number or string, got: " + params[key].dump());
            }
            
        } catch (const std::invalid_argument& e) {
            // Re-throw our custom errors
            throw;
        } catch (const std::out_of_range& e) {
            throw std::invalid_argument("Address value out of range: " + params[key].dump());
        } catch (const std::exception& e) {
            throw std::invalid_argument("Failed to parse address: " + params[key].dump() + " (" + e.what() + ")");
        }
        
        // Validate the parsed address
        if (address == BADADDR) {
            throw std::invalid_argument("Parsed address is invalid (BADADDR)");
        }
        
        if (!is_valid_address(address)) {
            throw std::invalid_argument("Invalid address: " + format_address_hex(address));
        }
        
        return address;
    }

    static ea_t validate_function_address(const json& params, const std::string& key) {
        ea_t address = validate_address_param(params, key);
        if (!is_valid_function(address)) {
            throw std::invalid_argument("Address is not a valid function: " + format_address_hex(address));
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