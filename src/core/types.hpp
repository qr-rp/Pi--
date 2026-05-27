#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>
#include <optional>
#include <unordered_map>
#include <functional>

namespace pi {

// ── Forward declarations ──────────────────────────────────────────────────
struct Error;

// ── Role ──────────────────────────────────────────────────────────────────
enum class Role : uint8_t {
    kSystem,
    kUser,
    kAssistant,
    kTool,
};

[[nodiscard]] inline std::string_view role_name(Role r) noexcept {
    switch (r) {
        case Role::kSystem:    return "system";
        case Role::kUser:      return "user";
        case Role::kAssistant: return "assistant";
        case Role::kTool:      return "tool";
    }
    return "unknown";
}

[[nodiscard]] inline Role role_from_name(std::string_view name) noexcept {
    if (name == "user")      return Role::kUser;
    if (name == "assistant") return Role::kAssistant;
    if (name == "tool")      return Role::kTool;
    return Role::kSystem;
}

// ── Tool call ─────────────────────────────────────────────────────────────
struct ToolCall {
    std::string id;
    std::string name;
    std::string arguments; // JSON string
};

// ── Message ───────────────────────────────────────────────────────────────
struct Message {
    Role        role;
    std::string content;
    std::string tool_call_id;        // for tool result messages
    std::string name;                // for tool result messages
    std::vector<ToolCall> tool_calls; // for assistant messages

    Message() = default;

    Message(Role r, std::string_view c)
        : role(r), content(c) {}

    static Message system(std::string_view c)    { return Message(Role::kSystem, c); }
    static Message user(std::string_view c)      { return Message(Role::kUser, c); }
    static Message assistant(std::string_view c) { return Message(Role::kAssistant, c); }
    static Message tool(std::string_view c, std::string_view id, std::string_view name) {
        Message m(Role::kTool, c);
        m.tool_call_id = id;
        m.name = name;
        return m;
    }
};

// ── Provider types ────────────────────────────────────────────────────────
using KnownApi = std::string;  // "openai-completions", "anthropic-messages", etc.

struct Model {
    std::string id;
    std::string provider;
    KnownApi    api = "openai-completions";
    std::string base_url;
    std::string name;
    bool        reasoning = false;
    std::optional<int64_t> context_window;
    std::optional<int64_t> max_tokens;
    std::vector<std::string> capabilities; // "text", "image"
};

// ── Streaming event ───────────────────────────────────────────────────────
enum class StreamEventType : uint8_t {
    kChunk,
    kToolCall,
    kDone,
    kError,
};

struct StreamEvent {
    StreamEventType type;
    std::string     content;      // text delta for kChunk
    ToolCall        tool_call;    // for kToolCall
    // Error is forward-declared; use a string here to avoid circular dep
    std::string     error_message;
};

// Callback signature for streaming responses
using StreamCallback = std::function<void(const StreamEvent&)>;

// ── Chat completion options ───────────────────────────────────────────────
struct ChatOptions {
    std::string        model;
    double             temperature = 0.7;
    std::optional<int> max_tokens;
    bool               stream = true;
    std::optional<std::string> stop;
    std::optional<double> top_p;
    std::optional<std::string> thinking_mode;
    std::unordered_map<std::string, std::string> extra_headers;
    nlohmann::json extra_body = nlohmann::json::object();
};

// ── Tool interface ────────────────────────────────────────────────────────
struct ToolResult {
    bool        success = true;
    std::string output;
    std::string error_message;
};

struct ToolParameterProperty {
    std::string type;        // "string", "number", "boolean", "array", "object"
    std::string description;
    bool        required = false;
    std::optional<std::string> enum_values; // comma-separated
};

struct ToolDefinition {
    std::string  name;
    std::string  description;
    std::unordered_map<std::string, ToolParameterProperty> parameters;
};

// ── Config types ──────────────────────────────────────────────────────────
struct ProviderConfig {
    std::string api_key;
    std::string base_url;
    std::optional<std::string> api_type;
    std::unordered_map<std::string, std::string> headers;
};

struct AgentConfig {
    std::string default_model;
    std::string default_provider = "openai";
    double      temperature = 0.7;
    int         max_turns = 50;
    std::string locale = "en";
    std::unordered_map<std::string, ProviderConfig> providers;
    std::vector<Model> custom_models;
    std::string config_dir;
};

} // namespace pi
