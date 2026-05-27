#include "i18n.hpp"
#include <mutex>
#include <fstream>
#include <algorithm>
#include <cstdlib>

namespace pi {

void Translator::flatten_json(const nlohmann::json& j, const std::string& prefix,
                               std::unordered_map<std::string, std::string>& out) {
    if (j.is_object()) {
        for (auto& [key, val] : j.items()) {
            std::string full_key = prefix.empty() ? key : prefix + "." + key;
            flatten_json(val, full_key, out);
        }
    } else if (j.is_string()) {
        out[prefix] = j.get<std::string>();
    }
}

Result<void> Translator::load_locale(std::string_view locale_name, const fs::path& file_path) {
    std::ifstream ifs(file_path);
    if (!ifs.is_open()) {
        return make_result_error(ErrCode::kLocaleNotFound, "Cannot open locale file: {}", file_path.string());
    }

    try {
        nlohmann::json j;
        ifs >> j;
        std::unordered_map<std::string, std::string> flat;
        flatten_json(j, "", flat);
        locales_[std::string(locale_name)] = std::move(flat);
    } catch (const nlohmann::json::exception& e) {
        return make_result_error(ErrCode::kLocaleParseError, "Locale JSON error in {}: {}", file_path.string(), e.what());
    }
    return {};
}

Result<void> Translator::load_all(const fs::path& locales_dir) {
    if (!fs::exists(locales_dir) || !fs::is_directory(locales_dir)) {
        return make_result_error(ErrCode::kLocaleNotFound, "Locales directory not found: {}", locales_dir.string());
    }
    for (auto& entry : fs::directory_iterator(locales_dir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            auto r = load_locale(entry.path().stem().string(), entry.path());
            if (!r) { /* skip bad locale */ }
        }
    }
    if (locales_.find("en") == locales_.end()) {
        locales_["en"] = {};
    }
    return {};
}

Result<void> Translator::set_locale(std::string_view locale) {
    if (locales_.find(std::string(locale)) == locales_.end()) {
        return make_result_error(ErrCode::kLocaleKeyNotFound, "Locale '{}' not loaded", locale);
    }
    current_locale_ = locale;
    return {};
}

std::string Translator::detect_locale() const {
    const char* lang = std::getenv("LANG");
    if (lang && *lang) {
        std::string l(lang);
        auto dot_pos = l.find('.');
        if (dot_pos != std::string::npos) l = l.substr(0, dot_pos);
        std::replace(l.begin(), l.end(), '_', '-');
        if (locales_.find(l) != locales_.end()) return l;
        auto dash_pos = l.find('-');
        if (dash_pos != std::string::npos) {
            std::string lang_only = l.substr(0, dash_pos);
            if (locales_.find(lang_only) != locales_.end()) return lang_only;
        }
    }
    const char* lc_messages = std::getenv("LC_MESSAGES");
    if (lc_messages && *lc_messages) {
        std::string l(lc_messages);
        auto dot_pos = l.find('.');
        if (dot_pos != std::string::npos) l = l.substr(0, dot_pos);
        std::replace(l.begin(), l.end(), '_', '-');
        if (locales_.find(l) != locales_.end()) return l;
    }
    return "en";
}

std::vector<std::string> Translator::available_locales() const {
    std::vector<std::string> keys;
    keys.reserve(locales_.size());
    for (auto& [k, _] : locales_) keys.push_back(k);
    return keys;
}

std::string Translator::get_nested(const std::unordered_map<std::string, std::string>& flat_map,
                                    std::string_view key) noexcept {
    auto it = flat_map.find(std::string(key));
    if (it != flat_map.end()) return it->second;
    return {};
}

std::string Translator::translate(std::string_view key) const noexcept {
    auto locale_it = locales_.find(current_locale_);
    if (locale_it != locales_.end()) {
        auto val = get_nested(locale_it->second, key);
        if (!val.empty()) return val;
    }
    auto en_it = locales_.find("en");
    if (en_it != locales_.end() && current_locale_ != "en") {
        auto val = get_nested(en_it->second, key);
        if (!val.empty()) return val;
    }
    return std::string(key);
}

std::string Translator::translate_fmt(std::string_view key, std::vector<std::string> args) const noexcept {
    std::string result = translate(key);
    for (size_t i = 0; i < args.size(); ++i) {
        std::string placeholder = std::format("{{{}}}", i);
        size_t pos = 0;
        while ((pos = result.find(placeholder, pos)) != std::string::npos) {
            result.replace(pos, placeholder.length(), args[i]);
            pos += args[i].length();
        }
    }
    return result;
}

Translator& t() {
    static Translator instance;
    static std::once_flag init_flag;
    std::call_once(init_flag, [] {
        std::vector<fs::path> paths;
        const char* xdg = std::getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg) paths.push_back(fs::path(xdg) / "pi-agent" / "locales");
        const char* home = std::getenv("HOME");
        if (home) paths.push_back(fs::path(home) / ".config" / "pi-agent" / "locales");
        paths.push_back("locales");
        paths.push_back("/usr/share/pi-coding-agent/locales");
        for (auto& p : paths) {
            if (fs::exists(p) && fs::is_directory(p)) {
                (void)instance.load_all(p);
                break;
            }
        }
        std::string detected = instance.detect_locale();
        (void)instance.set_locale(detected);
    });
    return instance;
}

} // namespace pi
