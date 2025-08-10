//
// Created by user on 7/1/25.
//

#ifndef DEEP_ANALYSIS_SYSTEM_H
#define DEEP_ANALYSIS_SYSTEM_H

#include "core/common.h"
#include "core/config.h"
#include "sdk/claude_sdk.h"
#include "analysis/memory.h"

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
    claude::TokenUsage token_usage;
    // cost_estimate removed - calculate from token_usage.estimated_cost() instead
};

// Deep analysis manager
class DeepAnalysisManager {
private:
    std::shared_ptr<BinaryMemory> memory_;
    DeepAnalysisCollection current_collection_;
    std::map<std::string, DeepAnalysisResult> completed_analyses_;
    mutable qmutex_t mutex_;

    std::unique_ptr<claude::Client> deep_analysis_client_;

public:
    // Constructor with Config for OAuth support
    DeepAnalysisManager(std::shared_ptr<BinaryMemory> memory, const Config& config) : memory_(memory) {
        // Create API client based on config
        if (config.api.auth_method == claude::AuthMethod::OAUTH) {
            claude::auth::OAuthManager oauth_mgr(config.api.oauth_config_dir);
            std::optional<claude::OAuthCredentials> oauth_creds = oauth_mgr.get_credentials();
            if (oauth_creds) {
                deep_analysis_client_ = std::make_unique<claude::Client>(*oauth_creds, config.api.base_url);
            } else {
                deep_analysis_client_ = std::make_unique<claude::Client>(config.api.api_key, config.api.base_url);
            }
        } else {
            deep_analysis_client_ = std::make_unique<claude::Client>(config.api.api_key, config.api.base_url);
        }
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