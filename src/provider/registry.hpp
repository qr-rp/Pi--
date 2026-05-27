#pragma once

#include "provider/provider.hpp"
#include "core/config.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>

namespace pi {

// ── Provider Registry ─────────────────────────────────────────────────────
// Manages available providers and creates instances.
class ProviderRegistry {
public:
    ProviderRegistry();

    // Register a provider factory
    void register_factory(std::string_view provider_type, ProviderFactory factory);

    // Create a provider from config
    Result<std::unique_ptr<Provider>> create_provider(
        std::string_view provider_name,
        const ProviderConfig& config
    );

    // Get or create a provider by name, using global config
    Result<Provider*> get_or_create(std::string_view provider_name);

    // Register built-in providers (OpenAI-compatible, etc.)
    void register_builtins();

    // Set the config reference
    void set_config(Config* config) { config_ = config; }

    // List available provider names
    [[nodiscard]] std::vector<std::string> available_providers() const;

    // Discover models from all configured providers
    [[nodiscard]] std::vector<Model> discover_all_models();

private:
    Config* config_ = nullptr;
    std::unordered_map<std::string, ProviderFactory> factories_;
    std::unordered_map<std::string, std::unique_ptr<Provider>> instances_;
};

} // namespace pi
