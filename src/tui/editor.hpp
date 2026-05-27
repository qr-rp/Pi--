#pragma once

#include "component.hpp"
#include <string>
#include <vector>
#include <functional>

namespace pi::tui {

// ── Multi-line editor with line numbers ──────────────────────────────────
class Editor : public Component {
public:
    Editor();

    void set_text(std::string_view t);
    const std::string& text() const { return text_; }
    void clear() { set_text(""); }

    // Called when user presses Enter (after editor reads the line)
    std::function<void(const std::string&)> on_submit;

    std::vector<std::string> render(int width) override;
    bool handle_input(std::string_view data) override;
    bool focusable() const override { return true; }
    bool focused() const override { return focused_; }
    void focus(bool f) override { focused_ = f; }

    // Cursor position (1-indexed)
    int cursor_line() const { return cursor_line_; }
    int cursor_col() const { return cursor_col_; }

private:
    std::string text_;
    bool focused_ = false;
    int cursor_line_ = 1;
    int cursor_col_ = 1;
    int scroll_offset_ = 0; // scroll offset for rendered lines

    // Calculate display lines with word wrap
    std::vector<std::string> display_lines(int width) const;

    // Adjust cursor to valid position
    void clamp_cursor();
};

} // namespace pi::tui
