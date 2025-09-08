#include "agent/grader.h"
#include "agent/agent.h"
#include <sstream>
#include <algorithm>

namespace llm_re {

AnalysisGrader::AnalysisGrader(const Config& config) : config_(config) {
    // Create our own OAuth manager if using OAuth authentication
    if (config.api.auth_method == claude::AuthMethod::OAUTH) {
        oauth_manager_ = Config::create_oauth_manager(config.api.oauth_config_dir);
    }
    
    // Create API client based on auth method
    if (config.api.auth_method == claude::AuthMethod::OAUTH && oauth_manager_) {
        std::shared_ptr<claude::OAuthCredentials> oauth_creds = oauth_manager_->get_credentials();

        if (oauth_creds) {
            // Initialize API client with OAuth - pass shared_ptr so it shares credentials
            api_client_ = std::make_unique<claude::Client>(
                oauth_creds,
                config.api.base_url
            );
        } else {
            // Fallback to API key
            api_client_ = std::make_unique<claude::Client>(
                config.api.api_key,
                config.api.base_url
            );
        }
    } else {
        // Use API key authentication
        api_client_ = std::make_unique<claude::Client>(
            config.api.api_key,
            config.api.base_url
        );
    }
    
    mutex_ = qmutex_create();
}

AnalysisGrader::~AnalysisGrader() {
    if (mutex_) {
        qmutex_free(mutex_);
    }
}

AnalysisGrader::GradeResult AnalysisGrader::evaluate_analysis(const GradingContext& context) const {
    qmutex_locker_t lock(mutex_);
    
    // Build the grading request
    claude::messages::Message grading_request = create_grading_request(context);
    
    // Create chat request for grader with extensive thinking
    claude::ChatRequestBuilder builder;
    builder.with_model(config_.grader.model)
           .with_system_prompt(GRADER_SYSTEM_PROMPT)
           .with_max_tokens(config_.grader.max_tokens)
           .with_temperature(1.0)
           .enable_thinking(true)
           .enable_interleaved_thinking(false)
           .with_max_thinking_tokens(config_.grader.max_thinking_tokens);
    
    builder.add_message(grading_request);
    
    claude::ChatRequest request = builder.build();
    
    // Send to grader API
    claude::ChatResponse response = api_client_->send_request(request);
    
    // Check for OAuth token expiry (401 authentication error)
    if (!response.success && response.error && 
        response.error->find("OAuth token has expired") != std::string::npos) {
        
        msg("Grader OAuth token expired, attempting to refresh...\n");
        
        if (refresh_oauth_credentials()) {
            // Retry the request with refreshed credentials
            msg("Retrying grader request with refreshed OAuth token...\n");
            response = api_client_->send_request(request);
        } else {
            msg("ERROR: Failed to refresh OAuth token for grader\n");
        }
    }
    
    if (!response.success) {
        // Log the actual error
        std::string error_msg = response.error.value_or("Unknown error");
        msg("ERROR: Grader API request failed: %s\n", error_msg.c_str());
        
        // On grader failure (API failure, not grader marking as a failure), send back for more analysis
        GradeResult result;
        result.complete = false;
        result.response = "Grading evaluation failed. Please continue your investigation and ensure all aspects are thoroughly analyzed.";
        return result;
    }
    
    // Parse the grader's response and include the full message
    GradeResult result = parse_grader_response(response.message);
    result.fullMessage = response.message;  // Store the complete message with thinking
    return result;
}

// Note: Now using shared TokenUtils::estimate_tokens() instead

std::vector<AnalysisGrader::MessagePriority> AnalysisGrader::prioritize_messages(
    const std::vector<claude::messages::Message>& messages) const {
    
    std::vector<MessagePriority> priorities;
    int total_messages = messages.size();
    
    for (size_t i = 0; i < messages.size(); ++i) {
        const claude::messages::Message& msg = messages[i];
        
        if (msg.role() != claude::messages::Role::Assistant) {
            continue;  // Only process assistant messages
        }
        
        MessagePriority mp;
        mp.message = &msg;
        
        // Calculate priority based on position and content
        bool is_recent = (i >= total_messages - 5);  // Last 5 messages
        bool has_tool_calls = !claude::messages::ContentExtractor::extract_tool_uses(msg).empty();
        bool has_text = claude::messages::ContentExtractor::extract_text(msg).has_value();
        
        // Assign priority
        if (is_recent || has_tool_calls) {
            mp.priority = 2;  // High priority
        } else if (has_text && i >= total_messages - 10) {
            mp.priority = 1;  // Medium priority
        } else {
            mp.priority = 0;  // Low priority
        }
        
        // Estimate tokens for this message - simple approach
        size_t token_count = 0;
        std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(msg);
        if (text) {
            token_count += text->length() / 4;
        }
        
        // Add tokens for thinking blocks
        auto thinking_blocks = claude::messages::ContentExtractor::extract_thinking_blocks(msg);
        for (const auto* block : thinking_blocks) {
            token_count += block->thinking.length() / 4;
        }
        
        // Add tokens for tool calls
        auto tool_calls = claude::messages::ContentExtractor::extract_tool_uses(msg);
        for (const auto* tool : tool_calls) {
            token_count += tool->name.length() / 4;
            token_count += tool->input.dump().length() / 4;
        }
        
        mp.estimated_tokens = token_count;
        priorities.push_back(mp);
    }
    
    return priorities;
}

claude::messages::Message AnalysisGrader::create_grading_request(const GradingContext& context) const {
    std::stringstream prompt;
    size_t total_tokens = 0;
    const size_t limit = config_.grader.context_limit;
    
    // Always include user request (high priority)
    prompt << "USER REQUEST:\n";
    prompt << context.user_request << "\n\n";
    total_tokens += context.user_request.length() / 4;
    
    prompt << "AGENT'S INVESTIGATION:\n\n";
    
    // Always include stored analyses (these are consolidated findings)
    if (!context.stored_analyses.empty()) {
        prompt << "STORED ANALYSES:\n\n";
        for (const AnalysisEntry& entry : context.stored_analyses) {
            std::string analysis_text = "[" + entry.type + ": " + entry.key + "]\n" + entry.content + "\n\n";
            prompt << analysis_text;
            total_tokens += analysis_text.length() / 4;
        }
    }
    
    // Prioritize and potentially prune agent work messages
    auto prioritized = prioritize_messages(context.agent_work);
    
    // Sort by priority (high to low) and then by recency
    std::stable_sort(prioritized.begin(), prioritized.end(),
        [](const MessagePriority& a, const MessagePriority& b) {
            return a.priority > b.priority;
        });
    
    // Track what we include
    std::vector<std::string> message_contents;
    std::vector<size_t> message_tokens;
    int pruned_count = 0;
    
    // First pass: collect all message content
    for (const auto& mp : prioritized) {
        std::stringstream msg_content;
        
        // Get thinking blocks
        auto thinking_blocks = claude::messages::ContentExtractor::extract_thinking_blocks(*mp.message);
        for (const auto* block : thinking_blocks) {
            msg_content << "[THINKING]\n" << block->thinking << "\n\n";
        }
        
        // Get tool calls
        auto tool_calls = claude::messages::ContentExtractor::extract_tool_uses(*mp.message);
        for (const auto* tool_call : tool_calls) {
            msg_content << "[TOOL_CALL]\n";
            msg_content << "Tool: " << tool_call->name << "\n";
            msg_content << "Parameters: " << tool_call->input.dump() << "\n\n";
        }
        
        // Get text content
        auto text = claude::messages::ContentExtractor::extract_text(*mp.message);
        if (text && !text->empty()) {
            msg_content << "[MESSAGE]\n" << *text << "\n\n";
        }
        
        std::string content = msg_content.str();
        if (!content.empty()) {
            message_contents.push_back(content);
            message_tokens.push_back(mp.estimated_tokens);
        }
    }
    
    // Second pass: include messages up to limit
    for (size_t i = 0; i < message_contents.size(); ++i) {
        if (total_tokens + message_tokens[i] < limit) {
            prompt << message_contents[i];
            total_tokens += message_tokens[i];
        } else {
            pruned_count++;
        }
    }
    
    // Add note about pruning if necessary
    if (pruned_count > 0) {
        prompt << "[NOTE: " << pruned_count << " older investigation messages were pruned to fit context limits]\n\n";
    }
    
    prompt << "---\n\n";
    prompt << "Evaluate whether this investigation provides what the user asked for.\n";
    prompt << "If complete, synthesize the findings into a final report for the user.\n";
    prompt << "If incomplete, identify what specific investigation is still needed.\n";
    
    return claude::messages::Message::user_text(prompt.str());
}

bool AnalysisGrader::refresh_oauth_credentials() const {
    if (!oauth_manager_ || config_.api.auth_method != claude::AuthMethod::OAUTH) {
        return false;
    }
    
    auto refreshed_creds = oauth_manager_->force_refresh();
    if (!refreshed_creds) {
        msg("ERROR: Failed to refresh OAuth token in grader: %s\n", oauth_manager_->get_last_error().c_str());
        return false;
    }
    
    // Update the API client with the shared credentials pointer
    // Note: The credentials are already updated in-place by force_refresh
    api_client_->set_oauth_credentials(refreshed_creds);
    msg("Grader successfully refreshed OAuth token\n");
    return true;
}

AnalysisGrader::GradeResult AnalysisGrader::parse_grader_response(const claude::messages::Message& response) const {
    GradeResult result;
    
    // Extract text from response
    std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(response);
    if (!text) {
        // No text, needs more work
        result.complete = false;
        result.response = "Unable to evaluate. Please continue investigation.";
        return result;
    }
    
    const std::string& response_text = *text;
    
    // Use Haiku to classify if this is complete or incomplete
    bool is_complete = classify_completion(response_text);
    
    result.complete = is_complete;
    result.response = response_text;
    
    if (is_complete) {
        msg("Grader evaluation classified as COMPLETE\n");
    } else {
        msg("Grader evaluation classified as INCOMPLETE\n");
    }
    
    return result;
}
bool AnalysisGrader::classify_completion(const std::string& grader_response) const {
    std::string classification_prompt = R"(You are a classification assistant. Read the following evaluation of a reverse engineering investigation and determine if the evaluator considers it complete or incomplete.

EVALUATION TO CLASSIFY:
)" + grader_response + R"(

Analyze the tone and content. If the evaluator is critical, pointing out gaps, asking questions, or demanding more evidence, classify as incomplete. If the evaluator is satisfied and providing a summary of findings, classify as complete.

Respond with JSON only:
{
  "reasoning": "Brief reasoning",
  "is_complete": true or false,
})";
    
    // Create request for Haiku
    claude::ChatRequestBuilder builder;
    builder.with_model(claude::Model::Haiku35)
           .with_max_tokens(200)
           .with_temperature(0.0)  // Deterministic
           .enable_thinking(false);
    
    builder.add_message(claude::messages::Message::user_text(classification_prompt));
    
    claude::ChatRequest request = builder.build();
    
    // Send to API for classification
    claude::ChatResponse response = api_client_->send_request(request);
    
    // Check for OAuth token expiry (401 authentication error)
    if (!response.success && response.error && 
        response.error->find("OAuth token has expired") != std::string::npos) {
        
        msg("Classifier OAuth token expired, attempting to refresh...\n");
        
        if (refresh_oauth_credentials()) {
            // Retry the request with refreshed credentials
            msg("Retrying classifier request with refreshed OAuth token...\n");
            response = api_client_->send_request(request);
        } else {
            msg("ERROR: Failed to refresh OAuth token for classifier\n");
        }
    }
    
    if (!response.success) {
        // On classification failure, default to incomplete (safer)
        std::string error_msg = response.error.value_or("Unknown error");
        msg("WARNING: Classification failed (%s), defaulting to incomplete\n", error_msg.c_str());
        return false;
    }
    
    // Parse JSON response
    std::optional<std::string> text = claude::messages::ContentExtractor::extract_text(response.message);
    if (!text) {
        msg("WARNING: No text in classification response, defaulting to incomplete");
        return false;
    }
    
    try {
        json result = json::parse(*text);
        return result.value("is_complete", false);
    } catch (const json::exception& e) {
        msg("%s", std::format("WARNING: Failed to parse classification JSON: {}", e.what()).c_str());
        return false;
    }
}

} // namespace llm_re