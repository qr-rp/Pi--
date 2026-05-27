#pragma once

#include "tools/tool.hpp"

namespace pi {

// ── Read tool ─────────────────────────────────────────────────────────────
class ReadTool final : public Tool {
public:
    std::string_view name() const noexcept override { return "read"; }
    std::string_view description() const noexcept override {
        return "Read file contents from the filesystem. Supports line ranges.";
    }
    ToolDefinition definition() const override;
    Result<ToolResult> execute(std::string_view args_json) override;
};

} // namespace pi
