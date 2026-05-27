#pragma once

#include "core/error.hpp"
#include "core/types.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <functional>

namespace pi {

// ── Abstract Provider base class ──────────────────────────────────────────
class Provider {
public:
    virtual ~Provider() = default;

    // Name of the provider (e.g., "openai", "deepseek", "anthropic")
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    // Create a streaming chat completion
    // Messages are in OpenAI-compatible format internally.
    // Tools are optional tool definitions the model can call.
    // Returns true if stream started successfully; errors are reported via callback.
    virtual Result<void> chat_completion(
        const std::vector<Message>& messages,
        const ChatOptions& options,
        const std::vector<ToolDefinition>& tools,
        StreamCallback callback
    ) = 0;

    // Non-streaming convenience wrapper
    Result<std::string> chat_completion_sync(
        const std::vector<Message>& messages,
        const ChatOptions& options,
        const std::vector<ToolDefinition>& tools = {}
    );

    // Check if the provider is properly configured (has API key, etc.)
    [[nodiscard]] virtual bool is_configured() const noexcept = 0;

    // Discover available models from the provider API (e.g., GET /v1/models)
    [[nodiscard]] virtual Result<std::vector<Model>> discover_models() {
        return std::vector<Model>{}; // default: no discovery
    }

    // Get provider-specific headers
    [[nodiscard]] virtual std::unordered_map<std::string, std::string> default_headers() const {
        return {};
    }
};

// ── Factory function type ─────────────────────────────────────────────────
using ProviderFactory = std::function<std::unique_ptr<Provider>(const ProviderConfig&)>;

} // namespace pi
