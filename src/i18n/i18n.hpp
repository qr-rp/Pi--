#pragma once

#include "core/error.hpp"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace pi {

namespace fs = std::filesystem;

// ── Translator: simple JSON-based i18n ────────────────────────────────────
class Translator {
public:
    Translator() = default;

    Result<void> load_locale(std::string_view locale_name, const fs::path& file_path);
    Result<void> load_all(const fs::path& locales_dir);
    Result<void> set_locale(std::string_view locale);
    std::string detect_locale() const;

    [[nodiscard]] std::string translate(std::string_view key) const noexcept;

    // Format with positional args {0}, {1}, etc.
    [[nodiscard]] std::string translate_fmt(std::string_view key, std::vector<std::string> args) const noexcept;

    [[nodiscard]] std::vector<std::string> available_locales() const;
    [[nodiscard]] const std::string& current_locale() const noexcept { return current_locale_; }

private:
    std::string current_locale_ = "en";
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> locales_;

    static std::string get_nested(const std::unordered_map<std::string, std::string>& flat_map,
                                   std::string_view key) noexcept;
    static void flatten_json(const nlohmann::json& j, const std::string& prefix,
                              std::unordered_map<std::string, std::string>& out);
};

Translator& t();

} // namespace pi

// Variadic macro: _(key) — simple key lookup only
#define _(key) pi::t().translate(key)
