#pragma once

#include "component.hpp"
#include <string>
#include <vector>

namespace pi::tui {

// ── Text component: renders plain text with optional padding ─────────────
class Text : public Component {
public:
    Text(std::string text, int pad_left = 0, int pad_right = 0)
        : text_(std::move(text)), pad_left_(pad_left), pad_right_(pad_right) {}

    void set_text(std::string_view t) { text_ = t; invalidate(); }
    void append_text(std::string_view t) { text_ += t; invalidate(); }
    const std::string& text() const { return text_; }

    std::vector<std::string> render(int width) override;

private:
    std::string text_;
    int pad_left_;
    int pad_right_;
    std::vector<std::string> cache_;
};

} // namespace pi::tui
