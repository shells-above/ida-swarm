//
// Created by user on 7/1/25.
//

#include "deep_analysis.h"
#include "actions.h"

namespace llm_re {

void DeepAnalysisManager::start_collection(const std::string& topic, const std::string& description) {
    std::lock_guard<std::mutex> lock(mutex_);

    current_collection_ = DeepAnalysisCollection();
    current_collection_.topic = topic;
    current_collection_.description = description;
    current_collection_.started_at = std::chrono::steady_clock::now();
    current_collection_.is_active = true;
}

void DeepAnalysisManager::add_to_collection(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!current_collection_.is_active) {
        throw std::runtime_error("No active deep analysis collection. Call start_collection first.");
    }

    current_collection_.collected_info[key] = value;
}

void DeepAnalysisManager::add_function_to_collection(ea_t function_addr) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!current_collection_.is_active) {
        throw std::runtime_error("No active deep analysis collection. Call start_collection first.");
    }

    // Avoid duplicates
    if (std::find(current_collection_.related_functions.begin(),
                  current_collection_.related_functions.end(),
                  function_addr) == current_collection_.related_functions.end()) {
        current_collection_.related_functions.push_back(function_addr);
    }
}

bool DeepAnalysisManager::has_active_collection() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_collection_.is_active;
}

DeepAnalysisCollection DeepAnalysisManager::get_current_collection() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_collection_;
}

void DeepAnalysisManager::clear_collection() {
    std::lock_guard<std::mutex> lock(mutex_);
    current_collection_ = DeepAnalysisCollection();
}

DeepAnalysisResult DeepAnalysisManager::execute_deep_analysis(
    const std::string& task,
    std::shared_ptr<ActionExecutor> executor,
    std::function<void(const std::string&)> progress_callback) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!current_collection_.is_active) {
        throw std::runtime_error("No active deep analysis collection to analyze");
    }

    if (progress_callback) {
        progress_callback("Building comprehensive context for Opus 4...");
    }

    // Build the comprehensive context
    std::string context = build_opus_context(current_collection_, executor);

    // Create the system prompt for Opus
    std::string system_prompt = R"(You are an expert reverse engineer tasked with performing deep analysis on a complex binary system. You have been provided with:

1. Collected information and observations from initial analysis
2. Complete memory dump of all previous analysis findings
3. Full decompilations and disassembly of relevant functions
4. Known cross-references and relationships between functions

Your task is to provide a comprehensive, detailed analysis that:
- Identifies the overall purpose and architecture of the system
- Explains how the components work together
- Identifies any security implications, algorithms, or protocols
- Provides actionable insights that couldn't be determined through surface-level analysis
- Fully answers the provided task

Be extremely thorough and technical. This is a deep dive analysis where detail and accuracy are paramount.)";

    // Add the specific task
    std::string user_prompt = "Task: " + task + "\n\nContext and collected information:\n\n" + context;

    if (progress_callback) {
        progress_callback("Sending request to Opus 4 for deep analysis...");
    }

    // Build and send the request to Opus 4
    api::ChatRequest request = api::ChatRequestBuilder()
        .with_model(api::Model::Opus4)
        .with_system_prompt(system_prompt, true)
        .add_message(messages::Message::user_text(user_prompt))
        .with_max_tokens(32768)  // Large token budget for comprehensive analysis
        .with_temperature(0.0)   // We want consistency for technical analysis
        .enable_thinking(true)   // Enable thinking for complex reasoning
        .build();

    api::ChatResponse response = opus_client_->send_request(request);

    if (!response.success) {
        throw std::runtime_error("Opus 4 analysis failed: " + response.error.value_or("Unknown error"));
    }

    // Extract the analysis text
    std::string analysis_text = response.get_text().value_or("No analysis text returned");

    // Create the result
    DeepAnalysisResult result;
    result.key = create_analysis_key(current_collection_.topic);
    result.topic = current_collection_.topic;
    result.task_description = task;
    result.analysis = analysis_text;
    result.completed_at = std::chrono::system_clock::now();
    result.token_usage = response.usage;
    result.cost_estimate = response.usage.estimated_cost();

    // Store the result
    store_analysis_result(result);

    // Clear the collection
    current_collection_ = DeepAnalysisCollection();

    if (progress_callback) {
        progress_callback("Deep analysis completed successfully");
    }

    return result;
}

void DeepAnalysisManager::store_analysis_result(const DeepAnalysisResult& result) {
    // Store in our map
    completed_analyses_[result.key] = result;

    // Also store in BinaryMemory for persistence
    memory_->set_global_note("deep_analysis_" + result.key, result.analysis);

    // Store metadata
    json metadata;
    metadata["topic"] = result.topic;
    metadata["task"] = result.task_description;
    metadata["completed_at"] = std::chrono::system_clock::to_time_t(result.completed_at);
    metadata["token_usage"] = result.token_usage.to_json();
    metadata["cost_estimate"] = result.cost_estimate;

    memory_->set_global_note("deep_analysis_meta_" + result.key, metadata.dump());
}

std::vector<std::pair<std::string, std::string>> DeepAnalysisManager::list_analyses() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::pair<std::string, std::string>> results;
    for (const auto& [key, analysis] : completed_analyses_) {
        results.push_back({key, analysis.topic + " - " + analysis.task_description});
    }

    // Also check BinaryMemory for any stored analyses we don't have loaded
    std::vector<std::string> all_notes = memory_->list_global_notes();
    for (const std::string& note_key : all_notes) {
        if (note_key.find("deep_analysis_meta_") == 0) {
            std::string key = note_key.substr(19); // Remove "deep_analysis_meta_" prefix
            if (completed_analyses_.find(key) == completed_analyses_.end()) {
                // Load metadata to get description
                std::string meta_json = memory_->get_global_note(note_key);
                try {
                    json metadata = json::parse(meta_json);
                    std::string description = metadata["topic"].get<std::string>() + " - " +
                                            metadata["task"].get<std::string>();
                    results.push_back({key, description});
                } catch (...) {
                    results.push_back({key, "Unknown analysis"});
                }
            }
        }
    }

    return results;
}

std::optional<DeepAnalysisResult> DeepAnalysisManager::get_analysis(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check loaded analyses first
    auto it = completed_analyses_.find(key);
    if (it != completed_analyses_.end()) {
        return it->second;
    }

    // Try to load from BinaryMemory
    std::string analysis = memory_->get_global_note("deep_analysis_" + key);
    std::string meta_json = memory_->get_global_note("deep_analysis_meta_" + key);

    if (!analysis.empty() && !meta_json.empty()) {
        try {
            json metadata = json::parse(meta_json);

            DeepAnalysisResult result;
            result.key = key;
            result.topic = metadata["topic"];
            result.task_description = metadata["task"];
            result.analysis = analysis;
            result.completed_at = std::chrono::system_clock::from_time_t(metadata["completed_at"]);
            result.token_usage = api::TokenUsage::from_json(metadata["token_usage"]);
            result.cost_estimate = metadata["cost_estimate"];

            return result;
        } catch (...) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

double DeepAnalysisManager::estimate_cost(int estimated_tokens) {
    // Opus 4 pricing estimate
    const double opus_input_price = 15.0;   // per million tokens
    const double opus_output_price = 75.0; // per million tokens

    // Assume output is about 20% of input for analysis
    double input_cost = (estimated_tokens / 1000000.0) * opus_input_price;
    double output_cost = (estimated_tokens * 0.2 / 1000000.0) * opus_output_price;

    return input_cost + output_cost;
}

std::string DeepAnalysisManager::build_opus_context(
    const DeepAnalysisCollection& collection,
    std::shared_ptr<ActionExecutor> executor) {

    std::stringstream context;

    // Add collection metadata
    context << "=== ANALYSIS TOPIC ===\n";
    context << "Topic: " << collection.topic << "\n";
    context << "Description: " << collection.description << "\n\n";

    // Add collected information
    if (!collection.collected_info.empty()) {
        context << "=== COLLECTED OBSERVATIONS ===\n";
        for (const auto& [key, value] : collection.collected_info) {
            context << key << ":\n" << value << "\n\n";
        }
    }

    // Add all relevant memory context
    context << "=== BINARY ANALYSIS MEMORY ===\n";

    // Export all global notes
    std::vector<std::string> note_keys = memory_->list_global_notes();
    for (const std::string& key : note_keys) {
        // Skip deep analysis results to avoid recursion
        if (key.find("deep_analysis_") != 0) {
            std::string content = memory_->get_global_note(key);
            if (!content.empty()) {
                context << "Note [" << key << "]:\n" << content << "\n\n";
            }
        }
    }

    // Add all analyzed functions summary
    auto analyzed_functions = memory_->get_analyzed_functions();
    if (!analyzed_functions.empty()) {
        context << "=== ANALYZED FUNCTIONS ===\n";
        for (const auto& [addr, name, level] : analyzed_functions) {
            context << std::hex << "0x" << addr << " " << name << ":\n";
            std::string analysis = memory_->get_function_analysis(addr);
            if (!analysis.empty()) {
                context << analysis << "\n\n";
            }
        }
    }

    // Add all insights
    auto insights = memory_->get_insights();
    if (!insights.empty()) {
        context << "=== INSIGHTS AND FINDINGS ===\n";
        for (const auto& [description, addresses] : insights) {
            context << description << "\n";
            if (!addresses.empty()) {
                context << "Related addresses: ";
                for (ea_t addr : addresses) {
                    context << std::hex << "0x" << addr << " ";
                }
                context << "\n";
            }
            context << "\n";
        }
    }

    // Add decompilations for all related functions
    if (!collection.related_functions.empty()) {
        context << "=== FUNCTION DECOMPILATIONS AND DISASSEMBLY ===\n";
        for (ea_t func_addr : collection.related_functions) {
            json decompilation = executor->get_function_decompilation(func_addr);

            context << "\n--- Function at 0x" << std::hex << func_addr;
            if (decompilation.contains("name")) {
                context << " (" << decompilation["name"].get<std::string>() << ")";
            }
            context << " ---\n";

            if (decompilation.contains("decompilation")) {
                context << decompilation["decompilation"].get<std::string>() << "\n";
            }

            // Also add disassembly for critical functions
            json disassembly = executor->get_function_disassembly(func_addr);
            if (disassembly.contains("disassembly")) {
                context << "\nDisassembly:\n";
                context << disassembly["disassembly"].get<std::string>() << "\n";
            }

            // Add cross-references
            json xrefs_to = executor->get_xrefs_to(func_addr);
            json xrefs_from = executor->get_xrefs_from(func_addr);

            if (xrefs_to.contains("xrefs") && !xrefs_to["xrefs"].empty()) {
                context << "\nCalled by: ";
                for (const auto& xref : xrefs_to["xrefs"]) {
                    context << xref["from_name"].get<std::string>() << " ";
                }
                context << "\n";
            }

            if (xrefs_from.contains("xrefs") && !xrefs_from["xrefs"].empty()) {
                context << "Calls: ";
                for (const auto& xref : xrefs_from["xrefs"]) {
                    context << xref["to_name"].get<std::string>() << " ";
                }
                context << "\n";
            }
        }
    }

    return context.str();
}

std::string DeepAnalysisManager::create_analysis_key(const std::string& topic) {
    std::string key = topic;

    // Convert to lowercase
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);

    // Replace spaces and special characters with underscores
    for (char& c : key) {
        if (!std::isalnum(c)) {
            c = '_';
        }
    }

    // Remove consecutive underscores
    key.erase(std::unique(key.begin(), key.end(),
                         [](char a, char b) { return a == '_' && b == '_'; }),
              key.end());

    // Trim underscores from beginning and end
    while (!key.empty() && key.front() == '_') key.erase(0, 1);
    while (!key.empty() && key.back() == '_') key.pop_back();

    // Limit length
    if (key.length() > 50) {
        key = key.substr(0, 50);
    }

    // Add timestamp to ensure uniqueness
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    key += "_" + std::to_string(time_t);

    return key;
}

} // namespace llm_re