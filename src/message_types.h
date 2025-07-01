//
// Created by user on 6/30/25.
//

#ifndef MESSAGE_TYPES_H
#define MESSAGE_TYPES_H

#include "common.h"

#include <memory>
#include <optional>

namespace llm_re::messages {

// Forward declarations
struct TextContent;
struct ToolUseContent;
struct ToolResultContent;

// Visitor pattern for type-safe content handling
class ContentVisitor {
public:
    virtual ~ContentVisitor() = default;
    virtual void visit(const TextContent& content) = 0;
    virtual void visit(const ToolUseContent& content) = 0;
    virtual void visit(const ToolResultContent& content) = 0;
};

// Base content class
class Content {
public:
    virtual ~Content() = default;
    virtual void accept(ContentVisitor& visitor) const = 0;
    virtual json to_json() const = 0;
    virtual std::unique_ptr<Content> clone() const = 0;
    virtual std::string get_type() const = 0;
};

// Cache control for prompt caching
struct CacheControl {
    enum class Type { Ephemeral };
    Type type = Type::Ephemeral;

    json to_json() const {
        return {{"type", "ephemeral"}};
    }

    static std::optional<CacheControl> from_json(const json& j) {
        if (!j.is_object() || !j.contains("type")) return std::nullopt;
        if (j["type"] == "ephemeral") {
            return CacheControl{Type::Ephemeral};
        }
        return std::nullopt;
    }
};

// Text content
struct TextContent : public Content {
    std::string text;
    std::optional<CacheControl> cache_control;

    explicit TextContent(std::string t) : text(std::move(t)) {}

    TextContent(std::string t, CacheControl cc)
        : text(std::move(t)), cache_control(cc) {}

    void accept(ContentVisitor& visitor) const override {
        visitor.visit(*this);
    }

    json to_json() const override {
        json j;
        j["type"] = "text";
        j["text"] = text;
        if (cache_control) {
            j["cache_control"] = cache_control->to_json();
        }
        return j;
    }

    std::unique_ptr<Content> clone() const override {
        return std::make_unique<TextContent>(*this);
    }

    std::string get_type() const override { return "text"; }

    static std::unique_ptr<TextContent> from_json(const json& j) {
        if (!j.contains("text")) return nullptr;
        std::unique_ptr<TextContent> content = std::make_unique<TextContent>(j["text"]);
        if (j.contains("cache_control")) {
            content->cache_control = CacheControl::from_json(j["cache_control"]);
        }
        return content;
    }
};

// Tool use content
struct ToolUseContent : public Content {
    std::string id;
    std::string name;
    json input;

    ToolUseContent(std::string id_, std::string name_, json input_)
        : id(std::move(id_)), name(std::move(name_)), input(std::move(input_)) {}

    void accept(ContentVisitor& visitor) const override {
        visitor.visit(*this);
    }

    json to_json() const override {
        return {
            {"type", "tool_use"},
            {"id", id},
            {"name", name},
            {"input", input}
        };
    }

    std::unique_ptr<Content> clone() const override {
        return std::make_unique<ToolUseContent>(*this);
    }

    std::string get_type() const override { return "tool_use"; }

    static std::unique_ptr<ToolUseContent> from_json(const json& j) {
        if (!j.contains("id") || !j.contains("name") || !j.contains("input")) {
            return nullptr;
        }
        return std::make_unique<ToolUseContent>(j["id"], j["name"], j["input"]);
    }
};

// Tool result content
struct ToolResultContent : public Content {
    std::string tool_use_id;
    std::string content;
    bool is_error = false;

    ToolResultContent(std::string id, std::string c)
        : tool_use_id(std::move(id)), content(std::move(c)) {}

    ToolResultContent(std::string id, std::string c, bool error)
        : tool_use_id(std::move(id)), content(std::move(c)), is_error(error) {}

    void accept(ContentVisitor& visitor) const override {
        visitor.visit(*this);
    }

    json to_json() const override {
        json j = {
            {"type", "tool_result"},
            {"tool_use_id", tool_use_id},
            {"content", content}
        };
        if (is_error) {
            j["is_error"] = true;
        }
        return j;
    }

    std::unique_ptr<Content> clone() const override {
        return std::make_unique<ToolResultContent>(*this);
    }

    std::string get_type() const override { return "tool_result"; }

    static std::unique_ptr<ToolResultContent> from_json(const json& j) {
        if (!j.contains("tool_use_id") || !j.contains("content")) {
            return nullptr;
        }
        bool is_error = j.contains("is_error") && j["is_error"].get<bool>();
        return std::make_unique<ToolResultContent>(
            j["tool_use_id"], j["content"], is_error
        );
    }
};

// Message role enum
enum class Role {
    User,
    Assistant,
    System
};

inline std::string role_to_string(Role role) {
    switch (role) {
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::System: return "system";
    }
    return "unknown";
}

inline Role role_from_string(const std::string& s) {
    if (s == "user") return Role::User;
    if (s == "assistant") return Role::Assistant;
    if (s == "system") return Role::System;
    throw std::runtime_error("Unknown role: " + s);
}

// Message class
class Message {
    Role role_;
    std::vector<std::unique_ptr<Content>> contents_;

public:
    Message(Role r) : role_(r) {}

    // Copy constructor
    Message(const Message& other) : role_(other.role_) {
        for (const auto& content : other.contents_) {
            contents_.push_back(content->clone());
        }
    }

    // Move constructor
    Message(Message&&) = default;

    // Assignment operators
    Message& operator=(const Message& other) {
        if (this != &other) {
            role_ = other.role_;
            contents_.clear();
            for (const auto& content : other.contents_) {
                contents_.push_back(content->clone());
            }
        }
        return *this;
    }

    Message& operator=(Message&&) = default;

    Role role() const { return role_; }
    const std::vector<std::unique_ptr<Content>>& contents() const { return contents_; }
    std::vector<std::unique_ptr<Content>>& mutable_contents() { return contents_; }

    void add_content(std::unique_ptr<Content> content) {
        contents_.push_back(std::move(content));
    }

    // Helper to check if message has any tool calls
    bool has_tool_calls() const {
        for (const std::unique_ptr<Content>& content: contents_) {
            if (dynamic_cast<const ToolUseContent*>(content.get())) {
                return true;
            }
        }
        return false;
    }

    // Helper to check if message is empty
    bool is_empty() const {
        if (contents_.empty()) return true;
        for (const std::unique_ptr<Content>& content: contents_) {
            if (const TextContent* text = dynamic_cast<const TextContent *>(content.get())) {
                if (!text->text.empty()) return false;
            }
        }
        return true;
    }

    // Get first text content if exists
    std::optional<std::string> get_text() const {
        for (const std::unique_ptr<Content>& content: contents_) {
            if (const TextContent* text = dynamic_cast<const TextContent *>(content.get())) {
                return text->text;
            }
        }
        return std::nullopt;
    }

    // Factory methods for common message types
    static Message user_text(const std::string& text) {
        Message msg(Role::User);
        msg.add_content(std::make_unique<TextContent>(text));
        return msg;
    }

    static Message tool_result(const std::string& tool_id, const std::string& result, bool is_error = false) {
        Message msg(Role::User);  // Tool results are sent as user messages
        msg.add_content(std::make_unique<ToolResultContent>(tool_id, result, is_error));
        return msg;
    }

    static Message assistant_text(const std::string& text) {
        Message msg(Role::Assistant);
        msg.add_content(std::make_unique<TextContent>(text));
        return msg;
    }

    static Message assistant_with_tools(const std::string& text, std::vector<ToolUseContent> tools) {
        Message msg(Role::Assistant);
        if (!text.empty()) {
            msg.add_content(std::make_unique<TextContent>(text));
        }
        for (auto& tool : tools) {
            msg.add_content(std::make_unique<ToolUseContent>(std::move(tool)));
        }
        return msg;
    }

    static Message system(const std::string& text, bool cache = false) {
        Message msg(Role::System);
        if (cache) {
            msg.add_content(std::make_unique<TextContent>(text, CacheControl{CacheControl::Type::Ephemeral}));
        } else {
            msg.add_content(std::make_unique<TextContent>(text));
        }
        return msg;
    }

    // Convert to JSON for API - handles all the special cases
    json to_json() const {
        json j;
        j["role"] = role_to_string(role_);

        if (contents_.size() == 1) {
            // Single content can be sent as string (for simple text messages)
            if (const TextContent* text = dynamic_cast<const TextContent*>(contents_[0].get())) {
                if (!text->cache_control) {  // todo should i be checking this?
                    j["content"] = text->text;
                    return j;
                }
            }
        }

        // Multiple contents or special content types need array format
        json content_array = json::array();
        for (const std::unique_ptr<Content>& content: contents_) {
            content_array.push_back(content->to_json());
        }
        j["content"] = content_array;

        return j;
    }

    // Parse from API response
    static Message from_json(const json& j) {
        Role role = role_from_string(j["role"]);
        Message msg(role);

        const nlohmann::basic_json<>& content = j["content"];
        if (content.is_string()) {
            msg.add_content(std::make_unique<TextContent>(content));
        } else if (content.is_array()) {
            for (const nlohmann::basic_json<>& item: content) {
                if (!item.contains("type")) continue;

                std::string type = item["type"];
                if (type == "text") {
                    if (std::unique_ptr<TextContent> text = TextContent::from_json(item)) {
                        msg.add_content(std::move(text));
                    }
                } else if (type == "tool_use") {
                    if (std::unique_ptr<ToolUseContent> tool_use = ToolUseContent::from_json(item)) {
                        msg.add_content(std::move(tool_use));
                    }
                } else if (type == "tool_result") {
                    if (std::unique_ptr<ToolResultContent> tool_result = ToolResultContent::from_json(item)) {
                        msg.add_content(std::move(tool_result));
                    }
                }
            }
        }

        return msg;
    }
};

// Helper class to extract specific content types from a message
class ContentExtractor : public ContentVisitor {
    std::vector<const TextContent*> texts;
    std::vector<const ToolUseContent*> tool_uses;
    std::vector<const ToolResultContent*> tool_results;

public:
    void visit(const TextContent& content) override {
        texts.push_back(&content);
    }

    void visit(const ToolUseContent& content) override {
        tool_uses.push_back(&content);
    }

    void visit(const ToolResultContent& content) override {
        tool_results.push_back(&content);
    }

    const std::vector<const TextContent*>& get_texts() const { return texts; }
    const std::vector<const ToolUseContent*>& get_tool_uses() const { return tool_uses; }
    const std::vector<const ToolResultContent*>& get_tool_results() const { return tool_results; }

    void clear() {
        texts.clear();
        tool_uses.clear();
        tool_results.clear();
    }

    // Static helper methods for common extractions
    static std::vector<const ToolUseContent*> extract_tool_uses(const Message& msg) {
        ContentExtractor extractor;
        for (const std::unique_ptr<Content>& content: msg.contents()) {
            content->accept(extractor);
        }
        return extractor.get_tool_uses();
    }

    static std::optional<std::string> extract_text(const Message& msg) {
        ContentExtractor extractor;
        for (const auto& content : msg.contents()) {
            content->accept(extractor);
        }
        if (!extractor.get_texts().empty()) {
            return extractor.get_texts()[0]->text;
        }
        return std::nullopt;
    }
};

// Message pruning visitor for large content
class ContentPruner : public ContentVisitor {
    std::map<std::string, int> tool_call_iterations;  // tool_use_id -> iteration it was used
    int current_iteration;
    bool should_prune = false;
    std::unique_ptr<Content> pruned_content;

public:
    ContentPruner(const std::map<std::string, int>& iterations, int current)
        : tool_call_iterations(iterations), current_iteration(current) {}

    void visit(const TextContent& content) override {
        should_prune = false;
        pruned_content = content.clone();
    }

    void visit(const ToolUseContent& content) override {
        should_prune = false;
        pruned_content = content.clone();
    }

    void visit(const ToolResultContent& content) override {
        auto it = tool_call_iterations.find(content.tool_use_id);
        if (it != tool_call_iterations.end() && it->second < current_iteration - 1) {
            // This is an old tool result, prune it
            should_prune = true;

            try {
                json result = json::parse(content.content);

                // Prune large outputs from various tools
                if (result.contains("decompilation")) {
                    result["decompilation"] = "[Decompilation pruned - previously shown to LLM. You can request again if you need to analyze it deeper.]";
                }
                if (result.contains("disassembly")) {
                    result["disassembly"] = "[Disassembly pruned - previously shown to LLM. You can request again if you need to analyze it deeper.]";
                }
                if (result.contains("imports")) {
                    result["imports"] = "[Imports list pruned - previously shown to LLM. You can request again if needed.]";
                }
                if (result.contains("functions")) {
                    result["functions"] = "[Functions list pruned - previously shown to LLM. You can request again if needed.]";
                }
                if (result.contains("globals")) {
                    result["globals"] = "[Globals list pruned - previously shown to LLM. You can request again if needed.]";
                }
                if (result.contains("strings")) {
                    result["strings"] = "[Strings list pruned - previously shown to LLM. You can request again if needed.]";
                }
                if (result.contains("entry_points")) {
                    result["entry_points"] = "[Entry points list pruned - previously shown to LLM. You can request again if needed.]";
                }

                pruned_content = std::make_unique<ToolResultContent>(
                    content.tool_use_id, result.dump(), content.is_error
                );
            } catch (...) {
                // If parsing fails, keep original
                pruned_content = content.clone();
            }
        } else {
            should_prune = false;
            pruned_content = content.clone();
        }
    }

    bool was_pruned() const { return should_prune; }
    std::unique_ptr<Content> get_result() { return std::move(pruned_content); }
};

} // namespace llm_re::messages

#endif //MESSAGE_TYPES_H
