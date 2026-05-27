#pragma once

#include "tools/tool.hpp"

namespace pi {

// ── Edit tool ─────────────────────────────────────────────────────────────
class EditTool final : public Tool {
public:
    std::string_view name() const noexcept override { return "edit"; }
    std::string_view description() const noexcept override {
        return "Edit a file by replacing exact text matches.";
    }
    ToolDefinition definition() const override;
    Result<ToolResult> execute(std::string_view args_json) override;
};

} // namespace pi
