#pragma once

#include "core/error.hpp"
#include "core/config.hpp"
#include "provider/provider.hpp"
#include "provider/registry.hpp"
#include "tools/registry.hpp"
#include "session/session.hpp"

#include <memory>
#include <functional>
#include <atomic>

namespace pi {

// ── Callbacks for agent events ───────────────────────────────────────────
struct AgentCallbacks {
    std::function<void(std::string_view)> on_chunk;       // stream text delta
    std::function<void(std::string_view)> on_tool_start;  // tool name
    std::function<void(const ToolResult&)> on_tool_result; // tool result
    std::function<void(std::string_view)> on_error;       // error message
    std::function<void()> on_turn_complete;                // after each LLM turn
};

// ── Agent ─────────────────────────────────────────────────────────────────
class Agent {
public:
    Agent(Config* config, ProviderRegistry* providers, ToolRegistry* tools);

    // Run a single interaction: send messages, process tool calls, continue
    // Returns final assistant response content
    Result<std::string> run(
        Session& session,
        std::string_view user_input,
        const AgentCallbacks& callbacks,
        std::string_view model_override = {}
    );

    // Run a complete conversation (multi-turn until done)
    Result<std::string> run_conversation(
        Session& session,
        std::string_view user_input,
        const AgentCallbacks& callbacks,
        std::string_view model_override = {}
    );

    // Cancel current execution
    void cancel() noexcept { cancelled_ = true; }

private:
    Config* config_;
    ProviderRegistry* providers_;
    ToolRegistry* tools_;
    std::atomic<bool> cancelled_{false};

    // Process a single assistant response (handle tool calls)
    Result<bool> process_assistant_response(
        const std::string& content,
        const std::vector<ToolCall>& tool_calls,
        Session& session,
        const AgentCallbacks& callbacks
    );
};

} // namespace pi
