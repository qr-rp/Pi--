#pragma once

#include "tools/tool.hpp"

namespace pi {

// ── Bash tool ─────────────────────────────────────────────────────────────
class BashTool final : public Tool {
public:
    std::string_view name() const noexcept override { return "bash"; }
    std::string_view description() const noexcept override {
        return "Execute a bash command with optional timeout.";
    }
    ToolDefinition definition() const override;
    Result<ToolResult> execute(std::string_view args_json) override;
};

} // namespace pi
