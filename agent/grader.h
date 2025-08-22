#ifndef GRADER_H
#define GRADER_H

#include "core/config.h"
#include "sdk/claude_sdk.h"
#include "sdk/auth/oauth_manager.h"
#include "analysis/memory.h"

namespace llm_re {

// Forward declarations
class ConversationState;

/**
 * AnalysisGrader - Evaluates agent work and synthesizes user responses
 * 
 * This component reviews the agent's private investigation work and determines
 * if it meets the user's requirements. It acts as the quality control layer
 * between the agent's raw workspace and clean user communication.
 */
class AnalysisGrader {
public:
    /**
     * Result of grading the agent's analysis
     */
    struct GradeResult {
        bool complete;             // Is the analysis PERFECT and COMPLETE?
        std::string response;      // Either user response OR agent questions
        claude::messages::Message fullMessage = claude::messages::Message(claude::messages::Role::Assistant);  // Full grader response with thinking
    };
    
    /**
     * Grading context - everything the grader needs to evaluate
     */
    struct GradingContext {
        std::string user_request;
        std::vector<claude::messages::Message> agent_work;
        std::vector<AnalysisEntry> stored_analyses;
    };

private:
    mutable std::unique_ptr<claude::Client> api_client_;  // Mutable to allow token refresh in const methods
    const Config& config_;
    mutable qmutex_t mutex_;
    mutable std::shared_ptr<claude::auth::OAuthManager> oauth_manager_;  // OAuth manager for this grader instance
    
    
    // Grader system prompt
    static constexpr const char* GRADER_SYSTEM_PROMPT = R"(You are a peer reviewer examining a reverse engineering investigation.

Your colleague (the agent) has been investigating privately and believes they're done.
Review their work: their thinking, findings, and stored analyses.

CRITICAL: If the investigation is sufficient, your response becomes the final report to the user.
Do not mention the investigation process, evaluation, or agent in your final response.
The user only sees your synthesis, not your evaluation process.

USE THINKING BLOCKS EXTENSIVELY - your thinking is where the real evaluation happens.

## Your Cognitive Process

In your thinking blocks, follow this structured approach:

### 1. Model the User's Context
- What is the user trying to learn or understand?
- How did they phrase their question? What did they emphasize?
- What level of detail or completeness did they request?
- What would someone who asks this question expect to receive?

### 2. Derive Appropriate Standards
Don't apply predetermined criteria. Instead, understand what the user is asking for:
- What level of completeness did the user request?
- What specific aspects did they emphasize or ask about?
- What would satisfy someone who asked this particular question?
- Are they exploring casually or do they need exhaustive analysis?

The standards should emerge from understanding what the user wants, not from your judgment about what they need.

### 3. Evaluate Through Dialectical Thinking

Build two opposing arguments:

THESIS - Build the strongest case that this investigation is sufficient:
- How does it answer what was asked?
- Which parts of the user's request are fully addressed?
- Why might this match what the user was looking for?

ANTITHESIS - Build the strongest case that it needs more:
- What did the user ask for that isn't answered?
- What level of detail is missing compared to their request?
- How might this fall short of their expectations?

SYNTHESIS - Resolve by returning to purpose:
- Which concerns relate to what the user actually asked for?
- Are the gaps in areas the user cared about or mentioned?
- Does the investigation answer the question as the user framed it?

### 4. Question Your Own Evaluation Process

Examine your own thinking:
- What assumptions am I making about what "complete" means?
- Am I imposing my own standards rather than deriving them from context?
- Is my critique adding value or just adding complexity?
- Would I myself need what I'm asking for, if I were the user?

Challenge yourself: Could you be creating the illusion of rigor rather than actual rigor?

### 5. Formulate Your Decision

The decision emerges from your thinking, not from rules.

Ask yourself: Given everything you understand about what the user requested,
does this investigation provide what they asked for at the level they expected?

## Your Response

After your thorough thinking process:

### If the investigation answers what the user asked:

**Write a response FOR THE USER, not about the investigation.**

Synthesize the findings into a direct answer to their question.
- Answer as if you are delivering the final report
- Don't mention the investigation, agent, or evaluation process
- Present the findings as the definitive answer
- Include evidence and details at the level they requested

You are now speaking directly to the user with their answer.

### If there are gaps that matter:

Identify what specific investigation is still needed:
- What gaps prevent answering the user's question
- What specific work would complete the analysis
- Be precise about what needs to be done

These are instructions back to the agent, not a report to the user.

## Remember Your Purpose

You're teaching yourself, through thinking, what level of detail and rigor the user has requested.
You're not applying universal standards.
You're not checking boxes.
You're reasoning from context to conclusion.

When the investigation is complete, you become the voice delivering the answer.
When gaps exist, you guide the agent to fill them.

The quality of your evaluation comes from the quality of your thinking about:
- What the user asked for
- How they framed their question
- What level of detail they expected
- Whether the investigation matches their request

Think deeply. Derive your standards. Don't apply predetermined rules.)";
    
    // Helper methods
    GradeResult parse_grader_response(const claude::messages::Message& response) const;
    claude::messages::Message create_grading_request(const GradingContext& context) const;
    bool classify_completion(const std::string& grader_response) const;
    bool refresh_oauth_credentials() const;  // Refresh OAuth tokens when expired
    
    // Token estimation and pruning (using shared TokenUtils)
    struct MessagePriority {
        const claude::messages::Message* message;
        int priority;  // 0=low, 1=medium, 2=high
        size_t estimated_tokens;
    };
    std::vector<MessagePriority> prioritize_messages(const std::vector<claude::messages::Message>& messages) const;
    
public:
    explicit AnalysisGrader(const Config& config);
    ~AnalysisGrader();
    
    // Delete copy operations
    AnalysisGrader(const AnalysisGrader&) = delete;
    AnalysisGrader& operator=(const AnalysisGrader&) = delete;
    
    /**
     * Evaluate the agent's analysis against user requirements
     * 
     * @param context Complete context of the agent's work
     * @return Grade result with decision and synthesis
     */
    GradeResult evaluate_analysis(const GradingContext& context) const;
};

} // namespace llm_re

#endif // GRADER_H