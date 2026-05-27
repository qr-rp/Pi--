#pragma once

#include "provider/provider.hpp"

#include <curl/curl.h>
#include <string>

namespace pi {

// ── OpenAI-compatible provider ───────────────────────────────────────────
// Supports OpenAI, DeepSeek, and any OpenAI-compatible chat completion API.
class OpenAIProvider final : public Provider {
public:
    explicit OpenAIProvider(std::string_view api_key, std::string_view base_url,
                            std::string_view provider_name = "openai",
                            std::unordered_map<std::string, std::string> extra_headers = {});

    ~OpenAIProvider() override;

    [[nodiscard]] std::string_view name() const noexcept override { return provider_name_; }

    Result<void> chat_completion(
        const std::vector<Message>& messages,
        const ChatOptions& options,
        const std::vector<ToolDefinition>& tools,
        StreamCallback callback
    ) override;

    // Discover models via GET /v1/models
    [[nodiscard]] Result<std::vector<Model>> discover_models() override;

    [[nodiscard]] bool is_configured() const noexcept override {
        return !api_key_.empty();
    }

    [[nodiscard]] std::unordered_map<std::string, std::string> default_headers() const override {
        return extra_headers_;
    }

private:
    std::string api_key_;
    std::string base_url_;
    std::string provider_name_;
    std::unordered_map<std::string, std::string> extra_headers_;

    // Build the request body JSON
    nlohmann::json build_request_body(
        const std::vector<Message>& messages,
        const ChatOptions& options,
        const std::vector<ToolDefinition>& tools
    ) const;

    // Convert internal Message to OpenAI API message format
    nlohmann::json message_to_json(const Message& msg) const;

    // Parse a streaming SSE line and emit events
    bool parse_sse_line(std::string_view line, StreamCallback& callback, std::string& buffer) const;

    // CURL write callback
    static size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata);

    struct StreamContext {
        OpenAIProvider* self;
        StreamCallback* callback;
        std::string*    line_buffer;
        bool            ok;
    };
};

} // namespace pi
