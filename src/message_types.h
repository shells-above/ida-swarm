//
// Created by user on 6/30/25.
//

#ifndef MESSAGE_TYPES_H
#define MESSAGE_TYPES_H

#include "common.h"

// Forward declaration for ChatResponse
namespace llm_re::api {
    struct ChatResponse;
}

namespace llm_re::messages {

// Forward declarations
struct TextContent;
struct ToolUseContent;
struct ToolResultContent;
struct ThinkingContent;
struct RedactedThinkingContent;

// Visitor pattern for type-safe content handling
class ContentVisitor {
public:
    virtual ~ContentVisitor() = default;
    virtual void visit(const TextContent& content) = 0;
    virtual void visit(const ToolUseContent& content) = 0;
    virtual void visit(const ToolResultContent& content) = 0;
    virtual void visit(const ThinkingContent& content) = 0;
    virtual void visit(const RedactedThinkingContent& content) = 0;
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
    std::optional<CacheControl> cache_control;

    ToolResultContent(std::string id, std::string c, bool error, std::optional<CacheControl> cache = std::nullopt)
        : tool_use_id(std::move(id)), content(std::move(c)), is_error(error), cache_control(cache) {}

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
        if (cache_control) {
            j["cache_control"] = cache_control->to_json();
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

        std::optional<CacheControl> cache_control;
        if (j.contains("cache_control")) {
            cache_control = CacheControl::from_json(j["cache_control"]);
        }

        return std::make_unique<ToolResultContent>(
            j["tool_use_id"],
            j["content"],
            is_error,
            cache_control
        );
    }
};

// Thinking content
struct ThinkingContent : public Content {
    std::string thinking;
    std::optional<std::string> signature;

    explicit ThinkingContent(std::string t) : thinking(std::move(t)) {}

    ThinkingContent(std::string t, std::string sig)
        : thinking(std::move(t)), signature(std::move(sig)) {}

    void accept(ContentVisitor& visitor) const override {
        visitor.visit(*this);
    }

    json to_json() const override {
        json j;
        j["type"] = "thinking";
        j["thinking"] = thinking;
        if (signature) {
            j["signature"] = *signature;
        }
        return j;
    }

    std::unique_ptr<Content> clone() const override {
        return std::make_unique<ThinkingContent>(*this);
    }

    std::string get_type() const override { return "thinking"; }

    static std::unique_ptr<ThinkingContent> from_json(const json& j) {
        if (!j.contains("thinking")) return nullptr;
        std::unique_ptr<ThinkingContent> content = std::make_unique<ThinkingContent>(j["thinking"]);
        if (j.contains("signature")) {
            content->signature = j["signature"];
        }
        return content;
    }
};

// Redacted thinking content
struct RedactedThinkingContent : public Content {
    std::string data; // Encrypted thinking data

    explicit RedactedThinkingContent(std::string d) : data(std::move(d)) {}

    void accept(ContentVisitor& visitor) const override {
        visitor.visit(*this);
    }

    json to_json() const override {
        json j;
        j["type"] = "redacted_thinking";
        j["data"] = data;
        return j;
    }

    std::unique_ptr<Content> clone() const override {
        return std::make_unique<RedactedThinkingContent>(*this);
    }

    std::string get_type() const override { return "redacted_thinking"; }

    static std::unique_ptr<RedactedThinkingContent> from_json(const json& j) {
        if (!j.contains("data")) return nullptr;
        return std::make_unique<RedactedThinkingContent>(j["data"]);
    }
};

// Message role enum
enum class Role {
    User,
    Assistant,
    System
};

inline std::string role_to_string(const Role role) {
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

    const Role role() const { return role_; }
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

    // Helper to create assistant message with specific content blocks preserved
    // This is essential for tool use with thinking enabled - you must preserve
    // thinking blocks from the assistant's previous response when sending tool results
    static Message assistant_with_preserved_content(const std::vector<std::unique_ptr<Content>>& preserved_contents) {
        Message msg(Role::Assistant);
        for (const auto& content : preserved_contents) {
            msg.add_content(content->clone());
        }
        return msg;
    }

    // Helper to check if this message contains thinking blocks
    bool has_thinking_blocks() const {
        for (const auto& content : contents_) {
            if (dynamic_cast<const ThinkingContent*>(content.get()) || 
                dynamic_cast<const RedactedThinkingContent*>(content.get())) {
                return true;
            }
        }
        return false;
    }

    static Message system(const std::string& text) {
        Message msg(Role::System);
        msg.add_content(std::make_unique<TextContent>(text, CacheControl{CacheControl::Type::Ephemeral}));
        return msg;
    }

    // Convert to JSON for API - handles all the special cases
    json to_json() const {
        json j;
        j["role"] = role_to_string(role_);

        // Always use array format if we have cache control anywhere
        bool has_cache_control = false;
        for (const std::unique_ptr<Content>& content: contents_) {
            if (auto* text = dynamic_cast<const TextContent*>(content.get())) {
                if (text->cache_control) {
                    has_cache_control = true;
                    break;
                }
            } else if (auto* tool_result = dynamic_cast<const ToolResultContent*>(content.get())) {
                if (tool_result->cache_control) {
                    has_cache_control = true;
                    break;
                }
            }
        }

        if (contents_.size() == 1 && !has_cache_control) {
            // Single content without cache control can be sent as string
            if (const TextContent* text = dynamic_cast<const TextContent*>(contents_[0].get())) {
                j["content"] = text->text;
                return j;
            }
        }

        // Use array format for multiple contents or when cache control is present
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
                } else if (type == "thinking") {
                    if (std::unique_ptr<ThinkingContent> thinking = ThinkingContent::from_json(item)) {
                        msg.add_content(std::move(thinking));
                    }
                } else if (type == "redacted_thinking") {
                    if (std::unique_ptr<RedactedThinkingContent> redacted_thinking = RedactedThinkingContent::from_json(item)) {
                        msg.add_content(std::move(redacted_thinking));
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
    std::vector<const ThinkingContent*> thinking_blocks;
    std::vector<const RedactedThinkingContent*> redacted_thinking_blocks;

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

    void visit(const ThinkingContent& content) override {
        thinking_blocks.push_back(&content);
    }

    void visit(const RedactedThinkingContent& content) override {
        redacted_thinking_blocks.push_back(&content);
    }

    const std::vector<const TextContent*>& get_texts() const { return texts; }
    const std::vector<const ToolUseContent*>& get_tool_uses() const { return tool_uses; }
    const std::vector<const ToolResultContent*>& get_tool_results() const { return tool_results; }
    const std::vector<const ThinkingContent*>& get_thinking_blocks() const { return thinking_blocks; }
    const std::vector<const RedactedThinkingContent*>& get_redacted_thinking_blocks() const { return redacted_thinking_blocks; }

    void clear() {
        texts.clear();
        tool_uses.clear();
        tool_results.clear();
        thinking_blocks.clear();
        redacted_thinking_blocks.clear();
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

    static std::vector<const ThinkingContent*> extract_thinking_blocks(const Message& msg) {
        ContentExtractor extractor;
        for (const std::unique_ptr<Content>& content: msg.contents()) {
            content->accept(extractor);
        }
        return extractor.get_thinking_blocks();
    }

    static std::vector<const RedactedThinkingContent*> extract_redacted_thinking_blocks(const Message& msg) {
        ContentExtractor extractor;
        for (const std::unique_ptr<Content>& content: msg.contents()) {
            content->accept(extractor);
        }
        return extractor.get_redacted_thinking_blocks();
    }
};

} // namespace llm_re::messages

#endif //MESSAGE_TYPES_H