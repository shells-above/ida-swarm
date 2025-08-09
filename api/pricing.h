#pragma once

#include "anthropic_api.h"

namespace llm_re::api {

// Forward declaration
struct TokenUsage;

/**
 * Centralized pricing model for all API models
 * Single source of truth for token pricing
 */
class PricingModel {
public:
    // Pricing per million tokens
    struct ModelPricing {
        double input_price;
        double output_price;
        double cache_write_price;
        double cache_read_price;
    };

    // Get pricing for a specific model
    static constexpr ModelPricing get_pricing(Model model) {
        switch (model) {
            case Model::Opus41:
                return {15.0, 75.0, 18.75, 1.5};
            case Model::Sonnet4:
            case Model::Sonnet37:
                return {3.0, 15.0, 3.75, 0.30};
            case Model::Haiku35:
                return {0.8, 4.0, 1.0, 0.08};
        }
        return {0.0, 0.0, 0.0, 0.0};
    }

    // Calculate total cost for token usage
    static double calculate_cost(const TokenUsage& usage) {
        ModelPricing pricing = get_pricing(usage.model);
        
        return (usage.input_tokens / 1000000.0 * pricing.input_price) +
               (usage.output_tokens / 1000000.0 * pricing.output_price) +
               (usage.cache_creation_tokens / 1000000.0 * pricing.cache_write_price) +
               (usage.cache_read_tokens / 1000000.0 * pricing.cache_read_price);
    }

    // Get individual price components (for cache savings calculations)
    static double get_input_price(Model model) {
        return get_pricing(model).input_price;
    }

    static double get_cache_read_price(Model model) {
        return get_pricing(model).cache_read_price;
    }

    // Calculate savings from cache usage
    static double calculate_cache_savings(const TokenUsage& usage) {
        if (usage.cache_read_tokens <= 0) return 0.0;
        
        ModelPricing pricing = get_pricing(usage.model);
        return usage.cache_read_tokens / 1000000.0 * (pricing.input_price - pricing.cache_read_price);
    }
};

// Implementation of TokenUsage::estimated_cost()
inline double TokenUsage::estimated_cost() const {
    return PricingModel::calculate_cost(*this);
}

} // namespace llm_re::api