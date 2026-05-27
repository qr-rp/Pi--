#include "registry.hpp"
#include "read.hpp"
#include "edit.hpp"
#include "write.hpp"
#include "bash.hpp"
#include "search.hpp"

namespace pi {

ToolRegistry::ToolRegistry() {
    register_builtins();
}

void ToolRegistry::register_tool(std::unique_ptr<Tool> tool) {
    std::string name(tool->name());
    tools_[name] = std::move(tool);
}

Result<Tool*> ToolRegistry::get_tool(std::string_view name) {
    auto it = tools_.find(std::string(name));
    if (it == tools_.end()) {
        return make_result_error(ErrCode::kToolNotFound, "Tool '{}' not found", name);
    }
    return it->second.get();
}

Result<ToolResult> ToolRegistry::execute_tool(std::string_view name, std::string_view args_json) {
    auto tool = get_tool(name);
    if (!tool) return std::unexpected(tool.error());
    return (*tool)->execute(args_json);
}

std::vector<ToolDefinition> ToolRegistry::all_definitions() const {
    std::vector<ToolDefinition> defs;
    defs.reserve(tools_.size());
    for (auto& [_, tool] : tools_) {
        defs.push_back(tool->definition());
    }
    return defs;
}

void ToolRegistry::register_builtins() {
    register_tool(std::make_unique<ReadTool>());
    register_tool(std::make_unique<WriteTool>());
    register_tool(std::make_unique<EditTool>());
    register_tool(std::make_unique<BashTool>());
    register_tool(std::make_unique<SearchTool>());
}

} // namespace pi
