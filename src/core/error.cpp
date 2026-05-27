#include "error.hpp"

namespace pi {

std::string PiErrorCategory::message(int ev) const {
    switch (static_cast<ErrCode>(ev)) {
        case ErrCode::kOk:                   return "ok";
        case ErrCode::kConfigNotFound:       return "configuration not found";
        case ErrCode::kConfigParseError:     return "configuration parse error";
        case ErrCode::kConfigValidationError:return "configuration validation error";
        case ErrCode::kProviderNotFound:     return "provider not found";
        case ErrCode::kProviderInitFailed:   return "provider initialization failed";
        case ErrCode::kModelNotFound:        return "model not found";
        case ErrCode::kApiKeyMissing:        return "API key is missing";
        case ErrCode::kHttpError:            return "HTTP error";
        case ErrCode::kNetworkError:         return "network error";
        case ErrCode::kStreamError:          return "stream error";
        case ErrCode::kToolNotFound:         return "tool not found";
        case ErrCode::kToolExecutionError:   return "tool execution error";
        case ErrCode::kToolParameterError:   return "tool parameter error";
        case ErrCode::kSessionNotFound:      return "session not found";
        case ErrCode::kSessionSaveError:     return "session save error";
        case ErrCode::kLocaleNotFound:       return "locale not found";
        case ErrCode::kLocaleParseError:     return "locale parse error";
        case ErrCode::kLocaleKeyNotFound:    return "locale key not found";
        case ErrCode::kInternalError:        return "internal error";
        case ErrCode::kNotImplemented:       return "not implemented";
    }
    return "unknown error";
}

const std::error_category& pi_error_category() noexcept {
    static PiErrorCategory instance;
    return instance;
}

std::error_code make_error_code(ErrCode e) noexcept {
    return std::error_code(static_cast<int>(e), pi_error_category());
}

} // namespace pi
