#pragma once

#include "component.hpp"
#include <string>
#include <vector>

namespace pi::tui {

// ── Simple Markdown renderer ─────────────────────────────────────────────
// Renders markdown text with inline ANSI colors matching omp styles.
class Markdown : public Component {
public:
    Markdown(std::string text, int pad_left = 0, int pad_right = 0);

    void set_text(std::string_view t);
    const std::string& text() const { return text_; }

    std::vector<std::string> render(int width) override;

private:
    std::string text_;
    int pad_left_;
    int pad_right_;

    // Render a single markdown line
    std::string render_line(std::string_view line, int width) const;

    // Check if inside a code block
    bool in_code_block_ = false;
    std::string code_block_lang_;
};

} // namespace pi::tui
