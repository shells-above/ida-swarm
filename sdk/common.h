//
// Common definitions for the Claude SDK
// This file should have NO external dependencies except standard library and nlohmann/json
//

#ifndef CLAUDE_SDK_COMMON_H
#define CLAUDE_SDK_COMMON_H

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <sstream>

#undef getenv
#include <cstdlib>

namespace claude {
    // Logging levels
    enum class LogLevel {
        DEBUG,
        INFO,
        WARNING,
        ERROR
    };
} // namespace claude

using json = nlohmann::json;

#endif // CLAUDE_SDK_COMMON_H