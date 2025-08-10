//
// Claude SDK - Main include file
// Provides a complete C++ SDK for the Claude API
//

#ifndef CLAUDE_SDK_H
#define CLAUDE_SDK_H

// Core SDK components
#include "sdk/common.h"
#include "sdk/client/client.h"
#include "sdk/messages/types.h"
#include "sdk/tools/registry.h"

// Authentication
#include "sdk/auth/oauth_manager.h"
#include "sdk/auth/oauth_authorizer.h"
#include "sdk/auth/oauth_flow.h"

// Usage tracking
#include "sdk/usage/stats.h"
#include "sdk/usage/pricing.h"

// // Convenience namespace aliases
// namespace claude_messages = claude::messages;
// namespace claude_tools = claude::tools;
// namespace claude_auth = claude::auth;
// namespace claude_usage = claude::usage;

#endif // CLAUDE_SDK_H