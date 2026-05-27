#pragma once

#include "core/error.hpp"
#include "core/types.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace pi {

// ── Abstract Tool base class ──────────────────────────────────────────────
class Tool {
public:
    virtual ~Tool() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual std::string_view description() const noexcept = 0;

    // Get the JSON schema for this tool's parameters
    [[nodiscard]] virtual ToolDefinition definition() const = 0;

    // Execute the tool with JSON arguments
    virtual Result<ToolResult> execute(std::string_view args_json) = 0;
};

} // namespace pi
