#include "config.hpp"
#include <algorithm>
#include <fstream>
#include <cstdlib>

namespace pi {

Config::Config() {
    // Default config dir
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) {
        config_dir_ = fs::path(xdg) / "pi-agent";
    } else {
        const char* home = std::getenv("HOME");
        if (home) {
            config_dir_ = fs::path(home) / ".config" / "pi-agent";
        } else {
            config_dir_ = fs::path(".") / ".config" / "pi-agent";
        }
    }
    agent_.config_dir = config_dir_.string();

    // Default provider: OpenAI
    agent_.providers["openai"] = ProviderConfig{
        .api_key = std::getenv("OPENAI_API_KEY") ? std::getenv("OPENAI_API_KEY") : "",
        .base_url = "https://api.openai.com/v1",
        .api_type = "openai-completions",
    };
    agent_.default_provider = "openai";
    agent_.default_model = "gpt-4o";

    // DeepSeek default config (env key or empty)
    agent_.providers["deepseek"] = ProviderConfig{
        .api_key = std::getenv("DEEPSEEK_API_KEY") ? std::getenv("DEEPSEEK_API_KEY") : "",
        .base_url = "https://api.deepseek.com",
        .api_type = "openai-completions",
    };
}

fs::path Config::find_config_file() const {
    // Check: config_dir/config.json
    fs::path candidate = config_dir_ / "config.json";
    if (fs::exists(candidate)) return candidate;

    // Check: config_dir/config.yml / config.yaml
    candidate = config_dir_ / "config.yaml";
    if (fs::exists(candidate)) return candidate;

    // Check: config_dir/models.json
    candidate = config_dir_ / "models.json";
    if (fs::exists(candidate)) return candidate;

    return {};
}

Result<void> Config::load() {
    return load_from(config_dir_);
}

Result<void> Config::load_from(const fs::path& dir) {
    config_dir_ = dir;
    agent_.config_dir = dir.string();

    // Try config files
    auto config_path = find_config_file();
    if (!config_path.empty() && config_path.extension() == ".json") {
        std::ifstream ifs(config_path);
        if (!ifs.is_open()) {
            return make_result_error(ErrCode::kConfigNotFound, "Cannot open config file: {}", config_path.string());
        }
        std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        auto r = load_json(content);
        if (!r) return r;
    }

    // Load models config from user config directory
    auto models_path = config_dir_ / "models.json";
    if (fs::exists(models_path)) {
        auto r = load_models_config(models_path);
        if (!r) return r;
    }
    auto r = load_builtin_tools();
    if (!r) return r;

    // Environment variable overrides
    const char* openai_key = std::getenv("OPENAI_API_KEY");
    if (openai_key && *openai_key) {
        agent_.providers["openai"].api_key = openai_key;
    }
    const char* deepseek_key = std::getenv("DEEPSEEK_API_KEY");
    if (deepseek_key && *deepseek_key) {
        agent_.providers["deepseek"].api_key = deepseek_key;
    }
    const char* anthropic_key = std::getenv("ANTHROPIC_API_KEY");
    if (anthropic_key && *anthropic_key) {
        agent_.providers["anthropic"].api_key = anthropic_key;
    }

    return {};
}

Result<void> Config::load_json(std::string_view json_str) {
    try {
        auto j = nlohmann::json::parse(json_str);

        if (j.contains("default_model") && j["default_model"].is_string())
            agent_.default_model = j["default_model"].get<std::string>();

        if (j.contains("default_provider") && j["default_provider"].is_string())
            agent_.default_provider = j["default_provider"].get<std::string>();

        if (j.contains("temperature") && j["temperature"].is_number())
            agent_.temperature = j["temperature"].get<double>();

        if (j.contains("max_turns") && j["max_turns"].is_number())
            agent_.max_turns = j["max_turns"].get<int>();

        if (j.contains("locale") && j["locale"].is_string())
            agent_.locale = j["locale"].get<std::string>();

        if (j.contains("providers") && j["providers"].is_object()) {
            for (auto& [key, val] : j["providers"].items()) {
                ProviderConfig pc;
                if (val.contains("api_key") && val["api_key"].is_string())
                    pc.api_key = val["api_key"].get<std::string>();
                if (val.contains("base_url") && val["base_url"].is_string())
                    pc.base_url = val["base_url"].get<std::string>();
                if (val.contains("api_type") && val["api_type"].is_string())
                    pc.api_type = val["api_type"].get<std::string>();
                if (val.contains("headers") && val["headers"].is_object()) {
                    for (auto& [hk, hv] : val["headers"].items()) {
                        if (hv.is_string()) pc.headers[hk] = hv.get<std::string>();
                    }
                }
                agent_.providers[key] = std::move(pc);
            }
        }

    } catch (const nlohmann::json::exception& e) {
        return make_result_error(ErrCode::kConfigParseError, "JSON parse error: {}", e.what());
    }
    return {};
}

Result<void> Config::load_models_config(const fs::path& models_json) {
    std::ifstream ifs(models_json);
    if (!ifs.is_open()) {
        return make_result_error(ErrCode::kConfigNotFound, "Cannot open models file: {}", models_json.string());
    }
    try {
        auto j = nlohmann::json::parse(ifs);

        if (j.contains("models") && j["models"].is_array()) {
            for (auto& m : j["models"]) {
                Model model;
                model.id       = m.value("id", "");
                model.provider = m.value("provider", agent_.default_provider);
                model.api      = m.value("api", "openai-completions");
                model.base_url = m.value("base_url", "");
                model.name     = m.value("name", model.id);
                model.reasoning = m.value("reasoning", false);

                if (m.contains("context_window") && m["context_window"].is_number())
                    model.context_window = m["context_window"].get<int64_t>();
                if (m.contains("max_tokens") && m["max_tokens"].is_number())
                    model.max_tokens = m["max_tokens"].get<int64_t>();
                if (m.contains("capabilities") && m["capabilities"].is_array()) {
                    for (auto& c : m["capabilities"]) {
                        if (c.is_string()) model.capabilities.push_back(c.get<std::string>());
                    }
                }
                agent_.custom_models.push_back(std::move(model));
            }
        }
    } catch (const nlohmann::json::exception& e) {
        return make_result_error(ErrCode::kConfigParseError, "Models JSON parse error: {}", e.what());
    }
    return {};
}

Result<void> Config::load_builtin_tools() {
    builtin_tools_ = {
        {
            .name        = "read",
            .description = "Read file contents from the filesystem",
            .parameters  = {
                {"path", {.type = "string", .description = "File path to read", .required = true}},
            },
        },
        {
            .name        = "write",
            .description = "Write content to a file",
            .parameters  = {
                {"path",    {.type = "string", .description = "File path to write", .required = true}},
                {"content", {.type = "string", .description = "Content to write", .required = true}},
            },
        },
        {
            .name        = "edit",
            .description = "Edit a file using search-and-replace",
            .parameters  = {
                {"path",        {.type = "string", .description = "File path", .required = true}},
                {"old_text",    {.type = "string", .description = "Text to replace", .required = true}},
                {"new_text",    {.type = "string", .description = "Replacement text", .required = true}},
            },
        },
        {
            .name        = "bash",
            .description = "Execute a bash command",
            .parameters  = {
                {"command", {.type = "string", .description = "Command to execute", .required = true}},
                {"timeout", {.type = "number", .description = "Timeout in seconds", .required = false}},
            },
        },
        {
            .name        = "search",
            .description = "Search files using regex patterns",
            .parameters  = {
                {"pattern", {.type = "string", .description = "Regex pattern", .required = true}},
                {"path",    {.type = "string", .description = "Directory to search", .required = false}},
            },
        },
    };
    return {};
}

Result<void> Config::save() const {
    fs::create_directories(config_dir_);
    fs::path config_path = config_dir_ / "config.json";

    nlohmann::json j;
    j["default_model"]    = agent_.default_model;
    j["default_provider"] = agent_.default_provider;
    j["temperature"]      = agent_.temperature;
    j["max_turns"]        = agent_.max_turns;
    j["locale"]           = agent_.locale;

    nlohmann::json providers = nlohmann::json::object();
    for (auto& [name, pc] : agent_.providers) {
        nlohmann::json pj;
        pj["api_key"]  = pc.api_key;
        pj["base_url"] = pc.base_url;
        if (pc.api_type) pj["api_type"] = *pc.api_type;
        if (!pc.headers.empty()) {
            nlohmann::json h = nlohmann::json::object();
            for (auto& [hk, hv] : pc.headers) h[hk] = hv;
            pj["headers"] = h;
        }
        providers[name] = pj;
    }
    j["providers"] = providers;

    try {
        std::ofstream ofs(config_path);
        if (!ofs.is_open()) {
            return make_result_error(ErrCode::kConfigParseError, "Cannot write config: {}", config_path.string());
        }
        ofs << j.dump(2) << "\n";
    } catch (const std::exception& e) {
        return make_result_error(ErrCode::kConfigParseError, "Write error: {}", e.what());
    }
    return {};
}

ProviderConfig* Config::find_provider(std::string_view name) noexcept {
    auto it = agent_.providers.find(std::string(name));
    return it != agent_.providers.end() ? &it->second : nullptr;
}

const ProviderConfig* Config::find_provider(std::string_view name) const noexcept {
    auto it = agent_.providers.find(std::string(name));
    return it != agent_.providers.end() ? &it->second : nullptr;
}

std::string Config::get_api_key(std::string_view provider_name) const {
    auto* pc = find_provider(provider_name);
    if (pc && !pc->api_key.empty()) return pc->api_key;

    // Check env vars as fallback
    std::string env_var = std::format("{}_API_KEY", std::string(provider_name));
    std::transform(env_var.begin(), env_var.end(), env_var.begin(), ::toupper);
    const char* env = std::getenv(env_var.c_str());
    if (env) return env;

    return {};
}

const Model* Config::find_model(std::string_view model_id) const {
    for (auto& m : agent_.custom_models) {
        if (m.id == model_id) return &m;
    }
    return nullptr;
}

} // namespace pi
