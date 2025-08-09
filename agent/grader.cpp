#include "agent/grader.h"
#include "agent/agent.h"
#include "core/oauth_manager.h"
#include <sstream>

namespace llm_re {

AnalysisGrader::AnalysisGrader(const Config& config) : config_(config) {
    
    // Create API clients based on auth method
    if (config.api.auth_method == api::AuthMethod::OAUTH) {
        OAuthManager oauth_mgr(config.api.oauth_config_dir);
        auto oauth_creds = oauth_mgr.get_credentials();
        
        if (oauth_creds) {
            // Initialize grader client with OAuth
            grader_client_ = std::make_unique<api::AnthropicClient>(
                *oauth_creds,
                config.api.base_url
            );
            
            // Initialize classifier client with OAuth
            classifier_client_ = std::make_unique<api::AnthropicClient>(
                *oauth_creds,
                config.api.base_url
            );
        } else {
            // Fallback to API key
            grader_client_ = std::make_unique<api::AnthropicClient>(
                config.api.api_key,
                config.api.base_url
            );
            
            classifier_client_ = std::make_unique<api::AnthropicClient>(
                config.api.api_key,
                config.api.base_url
            );
        }
    } else {
        // Use API key authentication
        grader_client_ = std::make_unique<api::AnthropicClient>(
            config.api.api_key,
            config.api.base_url
        );
        
        classifier_client_ = std::make_unique<api::AnthropicClient>(
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
    messages::Message grading_request = create_grading_request(context);
    
    // Create chat request for grader with extensive thinking
    api::ChatRequestBuilder builder;
    builder.with_model(config_.grader.model)
           .with_system_prompt(GRADER_SYSTEM_PROMPT)
           .with_max_tokens(config_.grader.max_tokens)
           .with_temperature(1.0)
           .enable_thinking(true)
           .enable_interleaved_thinking(false)
           .with_max_thinking_tokens(config_.grader.max_thinking_tokens);
    
    builder.add_message(grading_request);
    
    api::ChatRequest request = builder.build();
    
    // Send to grader API
    api::ChatResponse response = grader_client_->send_request(request);
    
    if (!response.success) {
        // On grader failure (API failure, not grader marking as a failure), send back for more analysis
        GradeResult result;
        result.complete = false;
        result.response = "Grading evaluation failed. Please continue your investigation and ensure all aspects are thoroughly analyzed.";
        return result;
    }
    
    // Parse the grader's response
    return parse_grader_response(response.message);
}

messages::Message AnalysisGrader::create_grading_request(const GradingContext& context) {
    std::stringstream prompt;
    
    prompt << "USER REQUEST:\n";
    prompt << context.user_request << "\n\n";
    
    prompt << "AGENT'S INVESTIGATION:\n\n";

    // Include stored analyses
    if (!context.stored_analyses.empty()) {
        prompt << "STORED ANALYSES:\n\n";
        for (const AnalysisEntry& entry: context.stored_analyses) {
            prompt << "[" << entry.type << ": " << entry.key << "]\n";
            prompt << entry.content << "\n\n";
        }
    }

    // Include agent's thinking and responses
    for (const messages::Message& msg: context.agent_work) {
        if (msg.role() == messages::Role::Assistant) {
            // Get thinking blocks
            std::vector<const messages::ThinkingContent*> thinking_blocks = messages::ContentExtractor::extract_thinking_blocks(msg);
            for (const messages::ThinkingContent* block: thinking_blocks) {
                prompt << "[THINKING]\n" << block->thinking << "\n\n";
            }
            
            // Get text content
            std::optional<std::string> text = messages::ContentExtractor::extract_text(msg);
            if (text && !text->empty()) {
                prompt << "[MESSAGE]\n" << *text << "\n\n";
            }
        }
    }
    
    prompt << "---\n\n";
    prompt << "Evaluate if this investigation PERFECTLY and COMPLETELY answers the user's request.\n";
    prompt << "Remember: The bar is perfection. Any doubt or incompleteness means it goes back.\n";
    
    return messages::Message::user_text(prompt.str());
}

AnalysisGrader::GradeResult AnalysisGrader::parse_grader_response(const messages::Message& response) const {
    GradeResult result;
    
    // Extract text from response
    std::optional<std::string> text = messages::ContentExtractor::extract_text(response);
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
    api::ChatRequestBuilder builder;
    builder.with_model(api::Model::Haiku35)
           .with_max_tokens(200)
           .with_temperature(0.0)  // Deterministic
           .enable_thinking(false);
    
    builder.add_message(messages::Message::user_text(classification_prompt));
    
    api::ChatRequest request = builder.build();
    
    // Send to Haiku
    api::ChatResponse response = classifier_client_->send_request(request);
    
    if (!response.success) {
        // On classification failure, default to incomplete (safer)
        msg("WARNING: Classification failed, defaulting to incomplete");
        return false;
    }
    
    // Parse JSON response
    std::optional<std::string> text = messages::ContentExtractor::extract_text(response.message);
    if (!text) {
        msg("WARNING: No text in classification response, defaulting to incomplete");
        return false;
    }
    
    try {
        json result = json::parse(*text);
        return result.value("is_complete", false);
    } catch (const json::exception& e) {
        msg(std::format("WARNING: Failed to parse classification JSON: {}", e.what()).c_str());
        return false;
    }
}

} // namespace llm_re