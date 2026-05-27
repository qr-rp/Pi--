#pragma once

#include "core/error.hpp"
#include "core/types.hpp"

#include <filesystem>
#include <string>
#include <unordered_map>

namespace pi {

namespace fs = std::filesystem;

// ── Configuration manager ─────────────────────────────────────────────────
class Config {
public:
    Config();

    // Load from standard paths (XGD_CONFIG_HOME, ~/.config/pi-agent/)
    Result<void> load();

    // Load from specific directory
    Result<void> load_from(const fs::path& dir);

    // Load from JSON string
    Result<void> load_json(std::string_view json_str);

    // Save current config
    Result<void> save() const;

    // Getters
    [[nodiscard]] const AgentConfig& agent() const noexcept { return agent_; }
    [[nodiscard]] AgentConfig& agent() noexcept { return agent_; }

    [[nodiscard]] ProviderConfig* find_provider(std::string_view name) noexcept;
    [[nodiscard]] const ProviderConfig* find_provider(std::string_view name) const noexcept;

    [[nodiscard]] std::string get_api_key(std::string_view provider_name) const;

    // Find model in custom_models or bundled models
    [[nodiscard]] const Model* find_model(std::string_view model_id) const;

    // Get all wrapped tools from config
    [[nodiscard]] const std::vector<ToolDefinition>& builtin_tools() const noexcept { return builtin_tools_; }

    [[nodiscard]] const fs::path& config_dir() const noexcept { return config_dir_; }

private:
    AgentConfig agent_;
    fs::path    config_dir_;
    std::vector<ToolDefinition> builtin_tools_;

    Result<void> load_models_config(const fs::path& models_json);
    Result<void> load_builtin_tools();
    fs::path find_config_file() const;
};

} // namespace pi
