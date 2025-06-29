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

// JSON library
#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Type definitions
using ea_t = uintptr_t;

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
