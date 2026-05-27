#pragma once

#include "tools/tool.hpp"

namespace pi {

// ── Search tool ───────────────────────────────────────────────────────────
class SearchTool final : public Tool {
public:
    std::string_view name() const noexcept override { return "search"; }
    std::string_view description() const noexcept override {
        return "Search files using regex patterns across the filesystem.";
    }
    ToolDefinition definition() const override;
    Result<ToolResult> execute(std::string_view args_json) override;
};

} // namespace pi
