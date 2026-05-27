#pragma once

#include "component.hpp"
#include <string>
#include <vector>
#include <deque>
#include <functional>

namespace pi::tui {

class Editor : public Component {
public:
    Editor();

    void set_text(std::string_view t);
    const std::string& text() const { return text_; }
    void clear_text() { text_.clear(); cursor_x_ = cursor_y_ = 0; line_cache_.clear(); invalidate(); }

    std::function<void(const std::string&)> on_submit;
    std::function<std::vector<std::string>(std::string_view)> on_tab;

    std::vector<std::string> render(int width) override;
    bool handle_input(const InputEvent& ev) override;
    bool focusable() const override { return true; }
    bool focused() const override { return focused_; }
    void focus(bool f) override;

    int cursor_x() const { return cursor_x_; }
    int cursor_y() const { return cursor_y_; }
    int edit_height() const { return edit_height_; }
    void set_edit_height(int h) { edit_height_ = h; }

private:
    std::string text_;
    bool focused_ = false;
    int cursor_x_ = 0, cursor_y_ = 0;
    int scroll_col_ = 0;
    int edit_height_ = 4;

    std::deque<std::string> history_;
    int history_pos_ = -1;
    std::string saved_input_;

    mutable std::vector<std::string> line_cache_;
    mutable int cache_width_ = 0;

    std::vector<std::string> compute_lines(int width) const;
    int cursor_index() const;
    void cursor_from_index(int idx);
    void insert(char c);
    void backspace();
    void del();
    void move_left();
    void move_right();
    void move_up();
    void move_down();
    void move_home();
    void move_end();
};

} // namespace pi::tui
