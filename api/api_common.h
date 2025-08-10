//
// Common definitions for the API module
// This file should have NO external dependencies except standard library and nlohmann/json
//

#ifndef API_COMMON_H
#define API_COMMON_H

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <sstream>

#undef getenv
#include <cstdlib>

namespace llm_re {
    // Logging levels
    enum class LogLevel {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };
} // namespace llm_re

using json = nlohmann::json;

#endif // API_COMMON_H