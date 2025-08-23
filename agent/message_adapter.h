#pragma once

#include "event_bus.h"
#include "../sdk/messages/types.h"
#include <fstream>
#include <iomanip>
#include <sstream>

namespace llm_re {

// Base class for message adapters that consume events from the EventBus
class MessageAdapter {
protected:
    std::string subscription_id_;
    EventBus& bus_;
    
public:
    explicit MessageAdapter(EventBus& bus = get_event_bus()) : bus_(bus) {}
    
    virtual ~MessageAdapter() {
        if (!subscription_id_.empty()) {
            bus_.unsubscribe(subscription_id_);
        }
    }
    
    // Start listening to events
    virtual void start() = 0;
    
    // Stop listening
    virtual void stop() {
        if (!subscription_id_.empty()) {
            bus_.unsubscribe(subscription_id_);
            subscription_id_.clear();
        }
    }
};

// Console/IDA message window adapter
class ConsoleAdapter : public MessageAdapter {
public:
    explicit ConsoleAdapter(EventBus& bus = get_event_bus()) : MessageAdapter(bus) {}
    
    void start() override {
        subscription_id_ = bus_.subscribe([this](const AgentEvent& event) {
            handle_event(event);
        });
    }
    
private:
    void handle_event(const AgentEvent& event) {
        switch (event.type) {
            case AgentEvent::LOG: {
                int level = event.payload.value("level", 0);
                std::string message = event.payload.value("message", "");
                std::string prefix = format_log_prefix(static_cast<LogLevel>(level), event.source);
                msg("%s: %s\n", prefix.c_str(), message.c_str());
                break;
            }
            
            case AgentEvent::STATE: {
                int status = event.payload.value("status", -1);
                std::string status_str = format_status(status);
                msg("[%s] State: %s\n", event.source.c_str(), status_str.c_str());
                break;
            }
            
            case AgentEvent::TOOL_CALL: {
                std::string phase = event.payload.value("phase", "");
                std::string tool_name = event.payload.value("tool_name", "unknown");
                
                if (phase == "started") {
                    msg("[%s] Tool: Starting %s\n", event.source.c_str(), tool_name.c_str());
                } else if (phase == "completed") {
                    msg("[%s] Tool: Completed %s\n", event.source.c_str(), tool_name.c_str());
                }
                break;
            }
            
            case AgentEvent::ERROR: {
                std::string error = event.payload.value("error", "Unknown error");
                msg("[%s] ERROR: %s\n", event.source.c_str(), error.c_str());
                break;
            }
            
            case AgentEvent::ANALYSIS_RESULT: {
                std::string report = event.payload.value("report", "");
                msg("[%s] Final Report: %s\n", event.source.c_str(), report.c_str());
                break;
            }
            
            default:
                // Ignore other events for console
                break;
        }
    }
    
    std::string format_log_prefix(LogLevel level, const std::string& source) {
        switch (level) {
            case LogLevel::DEBUG: return "[" + source + "][DEBUG]";
            case LogLevel::INFO: return "[" + source + "][INFO]";
            case LogLevel::WARNING: return "[" + source + "][WARN]";
            case LogLevel::ERROR: return "[" + source + "][ERROR]";
            default: return "[" + source + "]";
        }
    }
    
    std::string format_status(int status) {
        switch (status) {
            case 0: return "Idle";
            case 1: return "Running";
            case 2: return "Paused";
            case 3: return "Completed";
            default: return "Unknown";
        }
    }
};

// File logging adapter
class FileLogAdapter : public MessageAdapter {
private:
    std::ofstream log_file_;
    std::string filename_;
    
public:
    explicit FileLogAdapter(const std::string& filename, EventBus& bus = get_event_bus()) 
        : MessageAdapter(bus), filename_(filename) {}
    
    void start() override {
        log_file_.open(filename_, std::ios::app);
        if (!log_file_.is_open()) {
            msg("FileLogAdapter: Failed to open log file %s\n", filename_.c_str());
            return;
        }
        
        subscription_id_ = bus_.subscribe([this](const AgentEvent& event) {
            handle_event(event);
        });
    }
    
    void stop() override {
        MessageAdapter::stop();
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }
    
private:
    void handle_event(const AgentEvent& event) {
        if (!log_file_.is_open()) return;
        
        // Create timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
        
        // Write structured log entry
        json log_entry = {
            {"timestamp", ss.str()},
            {"source", event.source},
            {"type", event_type_to_string(event.type)},
            {"data", event.payload}
        };
        
        log_file_ << log_entry.dump() << std::endl;
        log_file_.flush();
    }
    
    std::string event_type_to_string(AgentEvent::Type type) {
        switch (type) {
            case AgentEvent::MESSAGE: return "MESSAGE";
            case AgentEvent::LOG: return "LOG";
            case AgentEvent::STATE: return "STATE";
            case AgentEvent::TOOL_CALL: return "TOOL_CALL";
            case AgentEvent::TASK_COMPLETE: return "TASK_COMPLETE";
            case AgentEvent::ERROR: return "ERROR";
            case AgentEvent::METRIC: return "METRIC";
            case AgentEvent::ANALYSIS_RESULT: return "ANALYSIS_RESULT";
            case AgentEvent::GRADER_FEEDBACK: return "GRADER_FEEDBACK";
            case AgentEvent::CONTEXT_CONSOLIDATION: return "CONTEXT_CONSOLIDATION";
            case AgentEvent::USER_MESSAGE: return "USER_MESSAGE";
            default: return "UNKNOWN";
        }
    }
};

// IRC adapter for swarm communication
class IRCAdapter : public MessageAdapter {
private:
    std::function<void(const std::string&, const std::string&)> send_message_fn_;
    std::string channel_;
    
public:
    IRCAdapter(const std::string& channel,
               std::function<void(const std::string&, const std::string&)> send_fn,
               EventBus& bus = get_event_bus()) 
        : MessageAdapter(bus), channel_(channel), send_message_fn_(send_fn) {}
    
    void start() override {
        // Only subscribe to specific events relevant for IRC
        subscription_id_ = bus_.subscribe([this](const AgentEvent& event) {
            handle_event(event);
        }, {AgentEvent::ANALYSIS_RESULT, AgentEvent::STATE, AgentEvent::ERROR});
    }
    
private:
    void handle_event(const AgentEvent& event) {
        if (!send_message_fn_) return;
        
        switch (event.type) {
            case AgentEvent::ANALYSIS_RESULT: {
                // Send final results to orchestrator
                json result_json = {
                    {"agent_id", event.source},
                    {"report", event.payload.value("report", "")}
                };
                std::string msg = "AGENT_RESULT:" + result_json.dump();
                send_message_fn_("#results", msg);
                break;
            }
            
            case AgentEvent::STATE: {
                int status = event.payload.value("status", -1);
                if (status == 3) { // Completed
                    std::string msg = "AGENT_COMPLETE:" + event.source;
                    send_message_fn_(channel_, msg);
                }
                break;
            }
            
            case AgentEvent::ERROR: {
                std::string error = event.payload.value("error", "");
                std::string msg = "AGENT_ERROR:" + event.source + ":" + error;
                send_message_fn_(channel_, msg);
                break;
            }
            
            default:
                break;
        }
    }
};

// Metrics collection adapter
class MetricsAdapter : public MessageAdapter {
private:
    struct AgentMetrics {
        int total_tool_calls = 0;
        int total_messages = 0;
        int total_errors = 0;
        json token_usage;
        std::chrono::steady_clock::time_point start_time;
        std::chrono::steady_clock::time_point last_activity;
    };
    
    std::map<std::string, AgentMetrics> metrics_;
    mutable std::mutex metrics_mutex_;
    
public:
    explicit MetricsAdapter(EventBus& bus = get_event_bus()) : MessageAdapter(bus) {}
    
    void start() override {
        subscription_id_ = bus_.subscribe([this](const AgentEvent& event) {
            handle_event(event);
        });
    }
    
    json get_metrics() const {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        json result;
        for (const auto& [agent_id, metrics] : metrics_) {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                metrics.last_activity - metrics.start_time).count();
            
            result[agent_id] = {
                {"tool_calls", metrics.total_tool_calls},
                {"messages", metrics.total_messages},
                {"errors", metrics.total_errors},
                {"duration_seconds", duration},
                {"token_usage", metrics.token_usage}
            };
        }
        return result;
    }
    
private:
    void handle_event(const AgentEvent& event) {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        
        auto& metrics = metrics_[event.source];
        metrics.last_activity = std::chrono::steady_clock::now();
        
        if (metrics.total_messages == 0) {
            metrics.start_time = std::chrono::steady_clock::now();
        }
        
        switch (event.type) {
            case AgentEvent::MESSAGE:
                metrics.total_messages++;
                break;
            
            case AgentEvent::TOOL_CALL:
                if (event.payload.value("phase", "") == "started") {
                    metrics.total_tool_calls++;
                }
                break;
            
            case AgentEvent::ERROR:
                metrics.total_errors++;
                break;
            
            case AgentEvent::METRIC:
                if (event.payload.contains("tokens")) {
                    metrics.token_usage = event.payload["tokens"];
                }
                break;
            
            default:
                break;
        }
    }
};

} // namespace llm_re