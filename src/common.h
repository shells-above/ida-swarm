//
// Created by user on 6/29/25.
//

#ifndef COMMON_H
#define COMMON_H

#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <mutex>
#include <thread>
#include <queue>
#include <functional>
#include <ctime>
#include <sstream>
#include <iostream>
#include <fstream>
#include <atomic>
#include <condition_variable>
#include <fstream>
#include <utility>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <curl/curl.h>
#include <cmath>
#include <chrono>
#include <iomanip>


// IDA headers
#include <ida.hpp>
#include <idp.hpp>
#include <loader.hpp>
#include <kernwin.hpp>
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

#undef fgetc
#undef snprintf

// JSON library
#include <nlohmann/json.hpp>

#define fgetc dont_use_fgetc
#define snprintf dont_use_snprintf

using json = nlohmann::json;

// Type definitions
namespace llm_re {
    // Logging
    enum class LogLevel {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };

    void log(LogLevel level, const std::string& message);

    // Common structures
    struct FunctionInfo {
        ea_t address;
        std::string name;
        int distance_from_anchor;
        std::time_t last_updated;
    };

    struct AnalysisResult {
        bool success;
        std::string result;
        std::string error;
    };

} // namespace llm_re

#endif //COMMON_H
