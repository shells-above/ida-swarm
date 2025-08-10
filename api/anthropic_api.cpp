//
// Created by user on 7/1/25.
//

#include "api/anthropic_api.h"

namespace llm_re::api {

ChatRequestBuilder& ChatRequestBuilder::with_tools(const tools::ToolRegistry& registry) {
    request.tool_definitions = registry.get_api_definitions();

    // Add cache control to the last tool definition for prompt caching
    if (!request.tool_definitions.empty()) {
        request.tool_definitions.back()["cache_control"] = {{"type", "ephemeral"}};
    }

    return *this;
}

} // namespace llm_re::api