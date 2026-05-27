#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <source_location>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

namespace pi {

// ── Error codes ──────────────────────────────────────────────────────────
enum class ErrCode : int32_t {
    kOk = 0,
    kConfigNotFound,
    kConfigParseError,
    kConfigValidationError,
    kProviderNotFound,
    kProviderInitFailed,
    kModelNotFound,
    kApiKeyMissing,
    kHttpError,
    kNetworkError,
    kStreamError,
    kToolNotFound,
    kToolExecutionError,
    kToolParameterError,
    kSessionNotFound,
    kSessionSaveError,
    kLocaleNotFound,
    kLocaleParseError,
    kLocaleKeyNotFound,
    kInternalError,
    kNotImplemented,
};

// ── Error ─────────────────────────────────────────────────────────────────
struct Error {
    ErrCode       code;
    std::string   message;

    Error(ErrCode c, std::string_view msg) noexcept
        : code(c), message(msg) {}

    [[nodiscard]] std::string to_string() const noexcept {
        return std::format("[{}] {}", static_cast<int>(code), message);
    }

    [[nodiscard]] explicit operator bool() const noexcept { return code != ErrCode::kOk; }
};

// ── Result<T> ─────────────────────────────────────────────────────────────
template <typename T>
using Result = std::expected<T, Error>;

// ── Convenience error factories ──────────────────────────────────────────
inline Error make_error(ErrCode code, std::string_view message) noexcept {
    return Error(code, message);
}

template <typename... Args>
inline Error make_error(ErrCode code, std::format_string<Args...> fmt, Args&&... args) noexcept {
    return Error(code, std::format(fmt, std::forward<Args>(args)...));
}

// ── Helper for Result<T> returning functions ───────────────────────────
template <typename... Args>
inline auto make_result_error(ErrCode code, std::format_string<Args...> fmt, Args&&... args) {
    return std::unexpected(Error(code, std::format(fmt, std::forward<Args>(args)...)));
}

inline auto make_result_error(ErrCode code, std::string_view message) {
    return std::unexpected(Error(code, message));
}

// ── Error category for std::error_code interop ───────────────────────────
class PiErrorCategory final : public std::error_category {
public:
    [[nodiscard]] const char* name() const noexcept override { return "pi-agent"; }
    [[nodiscard]] std::string message(int ev) const override;
};

const std::error_category& pi_error_category() noexcept;
std::error_code make_error_code(ErrCode e) noexcept;

} // namespace pi

// Allow std::error_code construction from ErrCode
namespace std {
template <>
struct is_error_code_enum<pi::ErrCode> : true_type {};
} // namespace std
