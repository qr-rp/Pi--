#include "registry.hpp"
#include "openai.hpp"

namespace pi {

ProviderRegistry::ProviderRegistry() {
    register_builtins();
}

void ProviderRegistry::register_factory(std::string_view provider_type, ProviderFactory factory) {
    factories_[std::string(provider_type)] = std::move(factory);
}

Result<std::unique_ptr<Provider>> ProviderRegistry::create_provider(
    std::string_view provider_name,
    const ProviderConfig& config
) {
    std::string type = config.api_type.value_or(std::string(provider_name));
    if (type == "openai-completions" || type == "openai-responses" || type == "openai-codex-responses") {
        type = "openai";
    }
    if (type == "deepseek" || type == "deepseek-chat") {
        type = "openai";
    }

    auto it = factories_.find(type);
    if (it == factories_.end()) {
        it = factories_.find(std::string(provider_name));
        if (it == factories_.end()) {
            return make_result_error(ErrCode::kProviderNotFound,
                                     "No factory for provider type '{}' (name: {})", type, provider_name);
        }
    }
    return it->second(config);
}

Result<Provider*> ProviderRegistry::get_or_create(std::string_view provider_name) {
    std::string key(provider_name);
    auto it = instances_.find(key);
    if (it != instances_.end()) return it->second.get();

    if (!config_) {
        return make_result_error(ErrCode::kProviderNotFound, "Config not set in ProviderRegistry");
    }
    auto* pc = config_->find_provider(provider_name);
    if (!pc) {
        return make_result_error(ErrCode::kProviderNotFound, "Provider '{}' not found in config", provider_name);
    }

    auto result = create_provider(provider_name, *pc);
    if (!result) return std::unexpected(result.error());

    auto* ptr = result->get();
    instances_[key] = std::move(*result);
    return ptr;
}

void ProviderRegistry::register_builtins() {
    register_factory("openai", [](const ProviderConfig& config) -> std::unique_ptr<Provider> {
        std::string provider_name = "openai";
        if (config.base_url.find("deepseek") != std::string::npos) {
            provider_name = "deepseek";
        }
        std::unordered_map<std::string, std::string> extra_headers = config.headers;
        return std::make_unique<OpenAIProvider>(
            config.api_key,
            config.base_url.empty() ? "https://api.openai.com/v1" : config.base_url,
            provider_name,
            std::move(extra_headers)
        );
    });
}

std::vector<std::string> ProviderRegistry::available_providers() const {
    std::vector<std::string> names;
    for (auto& [key, _] : factories_) {
        names.push_back(key);
    }
    if (config_) {
        for (auto& [key, _] : config_->agent().providers) {
            if (std::find(names.begin(), names.end(), key) == names.end()) {
                names.push_back(key);
            }
        }
    }
    return names;
}

std::vector<Model> ProviderRegistry::discover_all_models() {
    std::vector<Model> all;
    if (!config_) return all;
    for (auto& [name, _] : config_->agent().providers) {
        // Skip providers without API key — discovery will likely fail
        auto* pc = config_->find_provider(name);
        if (!pc || pc->api_key.empty()) continue;

        auto r = get_or_create(name);
        if (!r) continue;
        auto* provider = *r;
        auto models_r = provider->discover_models();
        if (!models_r) continue;
        for (auto& m : *models_r) {
            bool dup = false;
            for (auto& existing : all) {
                if (existing.id == m.id && existing.provider == m.provider) {
                    dup = true; break;
                }
            }
            if (!dup) all.push_back(std::move(m));
        }
    }
    return all;
}

} // namespace pi
