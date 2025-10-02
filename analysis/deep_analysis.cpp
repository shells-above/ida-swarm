//
// Created by user on 7/1/25.
//

#include "analysis/deep_analysis.h"
#include "analysis/actions.h"

namespace llm_re {

void DeepAnalysisManager::start_collection(const std::string& topic, const std::string& description) {
    qmutex_locker_t lock(mutex_);

    current_collection_ = DeepAnalysisCollection();
    current_collection_.topic = topic;
    current_collection_.description = description;
    current_collection_.started_at = std::chrono::steady_clock::now();
    current_collection_.is_active = true;
}

void DeepAnalysisManager::add_to_collection(const std::string& key, const std::string& value) {
    qmutex_locker_t lock(mutex_);

    if (!current_collection_.is_active) {
        throw std::runtime_error("No active deep analysis collection. Call start_collection first.");
    }

    current_collection_.collected_info[key] = value;
}

void DeepAnalysisManager::add_function_to_collection(ea_t function_addr) {
    qmutex_locker_t lock(mutex_);

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
    qmutex_locker_t lock(mutex_);
    return current_collection_.is_active;
}

DeepAnalysisCollection DeepAnalysisManager::get_current_collection() const {
    qmutex_locker_t lock(mutex_);
    return current_collection_;
}

void DeepAnalysisManager::clear_collection() {
    qmutex_locker_t lock(mutex_);
    current_collection_ = DeepAnalysisCollection();
}

DeepAnalysisResult DeepAnalysisManager::execute_deep_analysis(
    const std::string& task,
    std::shared_ptr<ActionExecutor> executor,
    std::function<void(const std::string&)> progress_callback) {

    qmutex_locker_t lock(mutex_);

    if (!current_collection_.is_active) {
        throw std::runtime_error("No active deep analysis collection to analyze");
    }

    if (progress_callback) {
        progress_callback("Building comprehensive context...");
    }

    std::string context = build_context(current_collection_, executor);

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
        progress_callback("Sending request for deep analysis...");
    }

    // Build and send the request
    claude::ChatRequest request = claude::ChatRequestBuilder()
        .with_model(claude::Model::Sonnet45)
        .with_system_prompt(system_prompt)
        .add_message(claude::messages::Message::user_text(user_prompt))
        .with_max_tokens(32768)
        .with_max_thinking_tokens(16384)
        .with_temperature(1.0)
        .enable_thinking(true)
        .enable_interleaved_thinking(false)
        .build();

    claude::ChatResponse response = deep_analysis_client_->send_request(request);

    if (!response.success) {
        throw std::runtime_error("Deep analysis failed: " + response.error.value_or("Unknown error"));
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
    // Store in our map (Memory tool handles persistence automatically)
    completed_analyses_[result.key] = result;
}

std::vector<std::pair<std::string, std::string>> DeepAnalysisManager::list_analyses() const {
    qmutex_locker_t lock(mutex_);

    std::vector<std::pair<std::string, std::string>> results;

    // Return from in-memory cache (Memory tool handles persistence)
    for (const auto& [key, analysis] : completed_analyses_) {
        results.push_back({key, analysis.topic + " - " + analysis.task_description});
    }

    return results;
}

std::optional<DeepAnalysisResult> DeepAnalysisManager::get_analysis(const std::string& key) const {
    qmutex_locker_t lock(mutex_);

    // Check in-memory cache (Memory tool handles persistence)
    auto it = completed_analyses_.find(key);
    if (it != completed_analyses_.end()) {
        return it->second;
    }

    return std::nullopt;
}

std::string DeepAnalysisManager::build_context(
    const DeepAnalysisCollection& collection,
    const std::shared_ptr<ActionExecutor>& executor) {

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

    // Memory tool now handles context automatically via memory file system

    // Add full analysis for all related functions
    if (!collection.related_functions.empty()) {
        context << "\n=== FUNCTION DECOMPILATIONS AND ANALYSIS ===\n";

        for (ea_t func_addr : collection.related_functions) {
            // Use analyze_function to get comprehensive info
            json func_analysis = executor->analyze_function(func_addr, true, true, 50);

            if (!func_analysis["success"]) {
                context << "\n--- Function at 0x" << std::hex << func_addr << " ---\n";
                context << "Error: " << func_analysis.value("error", "Unknown error") << "\n\n";
                continue;
            }

            context << "\n--- Function at 0x" << std::hex << func_addr;
            if (func_analysis.contains("name")) {
                context << " (" << func_analysis["name"].get<std::string>() << ")";
            }
            context << " ---\n";

            // Add basic info
            context << "Size: " << func_analysis.value("size", 0) << " bytes\n";

            // Add decompilation
            if (func_analysis.contains("decompilation")) {
                context << "\nDecompilation:\n";
                context << func_analysis["decompilation"].get<std::string>() << "\n";
            }

            // Add disassembly
            if (func_analysis.contains("disassembly")) {
                context << "\nDisassembly:\n";
                context << func_analysis["disassembly"].get<std::string>() << "\n";
            }

            // Add cross-references
            if (func_analysis.contains("xrefs_to") && !func_analysis["xrefs_to"].empty()) {
                context << "\nCalled by: ";
                for (const auto& xref : func_analysis["xrefs_to"]) {
                    context << xref["name"].get<std::string>() << " ";
                }
                context << "\n";
            }

            if (func_analysis.contains("xrefs_from") && !func_analysis["xrefs_from"].empty()) {
                context << "Calls: ";
                for (const auto& xref : func_analysis["xrefs_from"]) {
                    context << xref["name"].get<std::string>() << " ";
                }
                context << "\n";
            }

            // Add string references
            if (func_analysis.contains("string_refs") && !func_analysis["string_refs"].empty()) {
                context << "\nString references:\n";
                for (const auto& str : func_analysis["string_refs"]) {
                    context << "  \"" << str.get<std::string>() << "\"\n";
                }
            }

            // Add data references
            if (func_analysis.contains("data_refs") && !func_analysis["data_refs"].empty()) {
                context << "\nData references: ";
                for (const auto& ref : func_analysis["data_refs"]) {
                    context << ref.get<std::string>() << " ";
                }
                context << "\n";
            }

            context << "\n";
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