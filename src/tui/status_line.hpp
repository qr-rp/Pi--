#pragma once

#include "component.hpp"
#include <string>

namespace pi::tui {

// ── Status line component ────────────────────────────────────────────────
// Shows model/provider/locale + current status message at the bottom
class StatusLine : public Component {
public:
    StatusLine();

    void set_status(std::string_view s) { status_ = s; invalidate(); }
    void set_model(std::string_view m) { model_ = m; invalidate(); }
    void set_provider(std::string_view p) { provider_ = p; invalidate(); }
    void set_locale(std::string_view l) { locale_ = l; invalidate(); }

    std::vector<std::string> render(int width) override;

private:
    std::string status_;
    std::string model_;
    std::string provider_;
    std::string locale_;
};

} // namespace pi::tui
