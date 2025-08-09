#ifndef GRADER_H
#define GRADER_H

#include "core/config.h"
#include "api/anthropic_api.h"
#include "api/message_types.h"
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
    };
    
    /**
     * Grading context - everything the grader needs to evaluate
     */
    struct GradingContext {
        std::string user_request;
        std::vector<messages::Message> agent_work;
        std::vector<AnalysisEntry> stored_analyses;
    };

private:
    std::unique_ptr<api::AnthropicClient> grader_client_;
    std::unique_ptr<api::AnthropicClient> classifier_client_;
    const Config& config_;
    mutable qmutex_t mutex_;
    
    
    // Grader system prompt
    static constexpr const char* GRADER_SYSTEM_PROMPT = R"(You are a peer reviewer examining a reverse engineering investigation.

Your colleague (the agent) has been investigating privately and believes they're done.
Review their work: their thinking, findings, and stored analyses.

USE THINKING BLOCKS EXTENSIVELY to evaluate the work.

Ask yourself:
- Has the user's request been COMPLETELY and PERFECTLY answered?
- Is there ANY aspect that could be clearer or deeper?
- Would someone be able to act on this information without ANY doubts?
- Are there assumptions that haven't been verified with evidence?
- Is the understanding deep enough that nothing is left to interpretation?

Provide your evaluation:

1. If the analysis is PERFECT and COMPLETE:
   Synthesize a clear, comprehensive response for the user that fully answers their request.
   Include all key findings from the stored analyses and actionable insights.
   
2. If there's ANY doubt, incompleteness, or room for improvement:
   Write challenging questions for the agent. Be specific and demanding:
   "You claimed X but where's the evidence from the binary?"
   "Your analysis of function F stops at the surface - what does line 42 actually do?"
   "You assumed Z without verification - prove it with concrete findings."
   
Remember:
- The bar is PERFECTION. Any doubt means it goes back.
- Demand evidence for everything. Be skeptical of unsupported claims.
- Be specific and rigorous in your evaluation.
- You're a peer reviewer seeking truth through evidence.)";
    
    // Helper methods
    GradeResult parse_grader_response(const messages::Message& response) const;
    static messages::Message create_grading_request(const GradingContext& context);
    bool classify_completion(const std::string& grader_response) const;
    
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