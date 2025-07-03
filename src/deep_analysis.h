//
// Created by user on 7/1/25.
//

#ifndef DEEP_ANALYSIS_SYSTEM_H
#define DEEP_ANALYSIS_SYSTEM_H

#include "common.h"
#include "memory.h"
#include "anthropic_api.h"

namespace llm_re {

// Forward declaration
class ActionExecutor;

// Deep analysis collection state
struct DeepAnalysisCollection {
    std::string topic;
    std::string description;
    std::vector<ea_t> related_functions;
    std::map<std::string, std::string> collected_info;
    std::chrono::steady_clock::time_point started_at;
    bool is_active = false;
};

// Deep analysis result
struct DeepAnalysisResult {
    std::string key;
    std::string topic;
    std::string task_description;
    std::string analysis;
    std::chrono::system_clock::time_point completed_at;
    api::TokenUsage token_usage;
    double cost_estimate;
};

// Deep analysis manager
class DeepAnalysisManager {
private:
    std::shared_ptr<BinaryMemory> memory_;
    DeepAnalysisCollection current_collection_;
    std::map<std::string, DeepAnalysisResult> completed_analyses_;
    mutable qmutex_t mutex_;

    std::unique_ptr<api::AnthropicClient> deep_analysis_client_;

public:
    DeepAnalysisManager(std::shared_ptr<BinaryMemory> memory, const std::string& api_key)
        : memory_(memory) {
        deep_analysis_client_ = std::make_unique<api::AnthropicClient>(api_key);
        mutex_ = qmutex_create();
    }

    ~DeepAnalysisManager() {
        qmutex_free(mutex_);
    }

    // Collection management
    void start_collection(const std::string& topic, const std::string& description);
    void add_to_collection(const std::string& key, const std::string& value);
    void add_function_to_collection(ea_t function_addr);
    bool has_active_collection() const;
    DeepAnalysisCollection get_current_collection() const;
    void clear_collection();

    // Deep analysis execution
    DeepAnalysisResult execute_deep_analysis(
        const std::string& task,
        std::shared_ptr<ActionExecutor> executor,
        std::function<void(const std::string&)> progress_callback = nullptr
    );

    // Result management
    void store_analysis_result(const DeepAnalysisResult& result);
    std::vector<std::pair<std::string, std::string>> list_analyses() const;
    std::optional<DeepAnalysisResult> get_analysis(const std::string& key) const;

private:
    std::string build_context(
        const DeepAnalysisCollection& collection,
        const std::shared_ptr<ActionExecutor>& executor
    );

    // Helper to create a sanitized key from topic
    static std::string create_analysis_key(const std::string& topic);
};

} // namespace llm_re

#endif //DEEP_ANALYSIS_SYSTEM_H