#pragma once

#include "tools/tool.hpp"
#include "core/config.hpp"

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <functional>

namespace pi {

// ── Tool Registry ─────────────────────────────────────────────────────────
class ToolRegistry {
public:
    ToolRegistry();

    // Register a tool
    void register_tool(std::unique_ptr<Tool> tool);

    // Get a tool by name
    Result<Tool*> get_tool(std::string_view name);

    // Execute a tool by name with JSON args
    Result<ToolResult> execute_tool(std::string_view name, std::string_view args_json);

    // Get all tool definitions (for LLM function calling)
    std::vector<ToolDefinition> all_definitions() const;

    // Register all built-in tools
    void register_builtins();

    // Set config reference (some tools might need it)
    void set_config(Config* config) { config_ = config; }

private:
    Config* config_ = nullptr;
    std::unordered_map<std::string, std::unique_ptr<Tool>> tools_;
};

} // namespace pi
