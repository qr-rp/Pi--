#pragma once

#include "component.hpp"
#include <string>

namespace pi::tui {

class StatusLine : public Component {
public:
    StatusLine();

    void set_status(std::string_view s) { status_ = s; invalidate(); }
    void set_model(std::string_view m) { model_ = m; invalidate(); }
    void set_provider(std::string_view p) { provider_ = p; invalidate(); }
    void set_locale(std::string_view l) { locale_ = l; invalidate(); }
    void set_tokens(int64_t in, int64_t out) { tokens_in_ = in; tokens_out_ = out; invalidate(); }
    void set_tool_count(int n) { tool_count_ = n; invalidate(); }
    void set_hint(std::string_view h) { hint_ = h; invalidate(); }

    std::vector<std::string> render(int width) override;
    void invalidate() override { dirty_ = true; Component::invalidate(); }

private:
    std::string status_ = " Ready";
    std::string model_;
    std::string provider_;
    std::string locale_;
    std::string hint_;
    int64_t tokens_in_ = 0, tokens_out_ = 0;
    int tool_count_ = 0;
    bool dirty_ = true;
    std::vector<std::string> cache_;
    int cache_width_ = 0;
};

} // namespace pi::tui
