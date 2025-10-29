#pragma once

#include <functional>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>
#include "../core/common.h"
#include "../core/logger.h"

namespace llm_re {

// todo: event bus is a good idea, but i should set it up so that its simpler to work around
//  we should automatically relay events from agents -> an #events channel, then have something here which detects + re emits them as actual events so its easier to work around

// Simple event that agents emit - agents know nothing about consumers
struct AgentEvent {
    enum Type {
        // Core agent events
        MESSAGE,        // Any message (API response, etc)
        LOG,           // Log message  
        STATE,         // State change (idle/running/paused/completed)
        TOOL_CALL,     // Tool execution event
        TASK_COMPLETE, // Task completion  
        ERROR,         // Error occurred
        METRIC,        // Token usage, timing, etc
        
        // Analysis events
        ANALYSIS_RESULT, // Final analysis result/report
        GRADER_FEEDBACK, // Grader evaluation

        // User interaction
        USER_MESSAGE,    // User injected message
        
        // Orchestrator events
        ORCHESTRATOR_THINKING,    // Orchestrator is processing
        ORCHESTRATOR_RESPONSE,    // Orchestrator's response
        AGENT_SPAWNING,          // Starting to spawn agents
        AGENT_SPAWN_COMPLETE,    // Agent spawned successfully
        AGENT_SPAWN_FAILED,      // Agent spawn failed
        SWARM_RESULT,           // Collected result from swarm
        ORCHESTRATOR_INPUT,     // User input to orchestrator
        AGENT_TOKEN_UPDATE,     // Real-time token usage from agent

        // Auto-decompile events
        AUTO_DECOMPILE_STARTED,   // Auto-decompile started
        AUTO_DECOMPILE_PROGRESS,  // Progress update for auto-decompile
        AUTO_DECOMPILE_COMPLETED  // Auto-decompile completed
    };
    
    Type type;
    std::string source;  // Agent ID or "system"
    json payload;        // All event data as JSON
    std::chrono::steady_clock::time_point timestamp;
    
    // Default constructor (required for Qt meta-type system)
    AgentEvent() : type(LOG), source(""), payload(json::object()), timestamp(std::chrono::steady_clock::now()) {}
    
    // Constructor
    AgentEvent(Type t, const std::string& src, const json& data) 
        : type(t), source(src), payload(data), timestamp(std::chrono::steady_clock::now()) {}
};

// Thread-safe event bus for agent communication
class EventBus {
private:
    using Handler = std::function<void(const AgentEvent&)>;
    
    struct Subscription {
        std::string id;
        Handler handler;
        std::vector<AgentEvent::Type> filter; // Empty = receive all
    };
    
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<Subscription>> subscriptions_;
    std::atomic<int> next_id_{1};
    
public:
    EventBus() = default;
    ~EventBus() = default;
    
    // Subscribe to events with optional type filter
    std::string subscribe(Handler handler, const std::vector<AgentEvent::Type>& types = {}) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        auto sub = std::make_unique<Subscription>();
        sub->id = "sub_" + std::to_string(next_id_++);
        sub->handler = std::move(handler);
        sub->filter = types;
        
        std::string id = sub->id;
        subscriptions_.push_back(std::move(sub));
        return id;
    }
    
    // Unsubscribe 
    void unsubscribe(const std::string& subscription_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        subscriptions_.erase(
            std::remove_if(subscriptions_.begin(), subscriptions_.end(),
                [&](const std::unique_ptr<Subscription>& sub) {
                    return sub->id == subscription_id;
                }),
            subscriptions_.end()
        );
    }
    
    // Publish event to all subscribers (non-blocking)
    // Note: Qt defines 'emit' as a macro, so we avoid that name
    void publish(const AgentEvent& event) {
        std::vector<Handler> handlers_to_call;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& sub : subscriptions_) {
                // Check filter
                if (sub->filter.empty() || 
                    std::find(sub->filter.begin(), sub->filter.end(), event.type) != sub->filter.end()) {
                    handlers_to_call.push_back(sub->handler);
                }
            }
        }
        
        // Call handlers outside the lock to prevent deadlocks
        for (const auto& handler : handlers_to_call) {
            try {
                handler(event);
            } catch (const std::exception& e) {
                // Log but don't propagate exceptions from handlers
                LOG("EventBus: Handler exception: %s\n", e.what());
            }
        }
    }
    
    // Helper methods for common event types
    void emit_log(const std::string& source, LogLevel level, const std::string& message) {
        publish(AgentEvent(AgentEvent::LOG, source, {
            {"level", static_cast<int>(level)},
            {"message", message}
        }));
    }
    
    void emit_state(const std::string& source, int status) {
        publish(AgentEvent(AgentEvent::STATE, source, {
            {"status", status}
        }));
    }
    
    void emit_message(const std::string& source, const json& message_data) {
        publish(AgentEvent(AgentEvent::MESSAGE, source, message_data));
    }
    
    void emit_tool_call(const std::string& source, const json& tool_data) {
        publish(AgentEvent(AgentEvent::TOOL_CALL, source, tool_data));
    }
    
    void emit_error(const std::string& source, const std::string& error) {
        publish(AgentEvent(AgentEvent::ERROR, source, {
            {"error", error}
        }));
    }

    // Generic emit method for any event type with payload
    void emit(const std::string& source, AgentEvent::Type type, const json& payload) {
        publish(AgentEvent(type, source, payload));
    }

};

// Global event bus instance (or could be passed around)
inline EventBus& get_event_bus() {
    static EventBus instance;
    return instance;
}

} // namespace llm_re

// Qt meta-type registration for AgentEvent
// This is needed for passing AgentEvent through Qt signals with queued connections
#ifdef QT_CORE_LIB
#include <QMetaType>
Q_DECLARE_METATYPE(llm_re::AgentEvent)
#endif