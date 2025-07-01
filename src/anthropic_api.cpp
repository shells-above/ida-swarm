//
// Created by user on 7/1/25.
//

#include "anthropic_api.h"
#include "tool_system.h"

// have to make a c++ file because of circular dependency tool_system -> deep analysis -> anthropic api -> ...
// could just organize differently

namespace llm_re::api {

ChatRequestBuilder& ChatRequestBuilder::with_tools(const tools::ToolRegistry& registry) {
    request.tool_definitions = registry.get_api_definitions();
    return *this;
}

} // namespace llm_re::api