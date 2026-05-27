#pragma once

#include "tools/tool.hpp"

namespace pi {

// ── Write tool ────────────────────────────────────────────────────────────
class WriteTool final : public Tool {
public:
    std::string_view name() const noexcept override { return "write"; }
    std::string_view description() const noexcept override {
        return "Write content to a file, creating parent directories if needed.";
    }
    ToolDefinition definition() const override;
    Result<ToolResult> execute(std::string_view args_json) override;
};

} // namespace pi
