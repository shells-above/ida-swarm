//
// Created by user on 7/30/25.
//
// Base common header with IDA SDK (except kernwin.hpp), std lib, and json
// This can be included by both UI and non-UI files

#ifndef COMMON_BASE_H
#define COMMON_BASE_H

// IDA headers (WITHOUT kernwin.hpp which includes Qt)
#include <ida.hpp>
#include <idp.hpp>
#include <loader.hpp>
// kernwin.hpp is NOT included here - see common.h or ui_v2_common.h
#include <bytes.hpp>
#include <name.hpp>
#include <funcs.hpp>
#include <hexrays.hpp>
#include <lines.hpp>
#include <segment.hpp>
#include <search.hpp>
#include <xref.hpp>
#include <nalt.hpp>
#include <entry.hpp>
#include <auto.hpp>
#include <strlist.hpp>
#include <diskio.hpp>

#undef fopen
#undef fclose
#undef fread
#undef fwrite
#undef wait

// std
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <queue>
#include <functional>
#include <ctime>
#include <sstream>
#include <iostream>
#include <fstream>
#include <atomic>
#include <condition_variable>
#include <utility>
#include <algorithm>
#include <cctype>
#include <regex>
#include <curl/curl.h>
#include <chrono>
#include <cmath>
#include <optional>
#include <unordered_map>

#define fopen dont_use_fopen
#define fclose dont_use_fclose
#define fread dont_use_fread
#define fwrite dont_use_fwrite
// #define wait dont_use_wait

#undef fgetc
#undef snprintf

// JSON library
#include <nlohmann/json.hpp>

// Wrapper type for addresses that should display as hex in JSON
struct HexAddress {
    ea_t addr;
    
    HexAddress() : addr(BADADDR) {}
    HexAddress(ea_t a) : addr(a) {}
    
    operator ea_t() const { return addr; }
    HexAddress& operator=(ea_t a) { addr = a; return *this; }
};

// Custom serializer for HexAddress to show as hex strings in JSON
namespace nlohmann {
    template <>
    struct adl_serializer<HexAddress> {
        static void to_json(json& j, const HexAddress& hex_addr) {
            std::stringstream ss;
            ss << "0x" << std::hex << hex_addr.addr;
            j = ss.str();
        }

        static void from_json(const json& j, HexAddress& hex_addr) {
            try {
                if (j.is_string()) {
                    std::string str = j.get<std::string>();
                    
                    // Trim whitespace
                    str.erase(str.find_last_not_of(" \t\n\r\f\v") + 1);
                    str.erase(0, str.find_first_not_of(" \t\n\r\f\v"));
                    
                    if (str.empty()) {
                        hex_addr.addr = BADADDR;
                        return;
                    }
                    
                    // Handle various hex formats: 0x4000, 0X4000, 4000h, 4000H
                    if ((str.length() >= 3 && (str.substr(0, 2) == "0x" || str.substr(0, 2) == "0X")) ||
                        (str.length() >= 2 && (str.back() == 'h' || str.back() == 'H'))) {
                        
                        std::string hex_part = str;
                        if (str.back() == 'h' || str.back() == 'H') {
                            hex_part = str.substr(0, str.length() - 1);
                        } else {
                            hex_part = str.substr(2);
                        }
                        
                        // Validate hex characters
                        for (char c : hex_part) {
                            if (!std::isxdigit(c)) {
                                hex_addr.addr = BADADDR;
                                return;
                            }
                        }
                        
                        hex_addr.addr = std::stoull(hex_part, nullptr, 16);
                    } else {
                        // Try decimal - validate all digits
                        for (char c : str) {
                            if (!std::isdigit(c)) {
                                hex_addr.addr = BADADDR;
                                return;
                            }
                        }
                        hex_addr.addr = std::stoull(str, nullptr, 10);
                    }
                    
                } else if (j.is_number_integer()) {
                    if (j.is_number_unsigned()) {
                        uint64_t val = j.get<uint64_t>();
                        if (val > std::numeric_limits<ea_t>::max()) {
                            hex_addr.addr = BADADDR;
                        } else {
                            hex_addr.addr = static_cast<ea_t>(val);
                        }
                    } else {
                        int64_t val = j.get<int64_t>();
                        if (val < 0 || static_cast<uint64_t>(val) > std::numeric_limits<ea_t>::max()) {
                            hex_addr.addr = BADADDR;
                        } else {
                            hex_addr.addr = static_cast<ea_t>(val);
                        }
                    }
                    
                } else if (j.is_number_float()) {
                    double val = j.get<double>();
                    if (val < 0 || val > std::numeric_limits<ea_t>::max()) {
                        hex_addr.addr = BADADDR;
                    } else {
                        hex_addr.addr = static_cast<ea_t>(val);
                    }
                    
                } else {
                    hex_addr.addr = BADADDR;
                }
                
            } catch (const std::exception& e) {
                hex_addr.addr = BADADDR;
            }
        }
    };
}

#define fgetc dont_use_fgetc
#define snprintf dont_use_snprintf

using json = nlohmann::ordered_json;

// Type definitions
namespace llm_re {
    // Logging
    enum class LogLevel {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };
} // namespace llm_re

#endif //COMMON_BASE_H