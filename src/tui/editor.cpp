#include "editor.hpp"
#include "tui.hpp"
#include "term.hpp"
#include <algorithm>

namespace pi::tui {

Editor::Editor() { text_.reserve(4096); }

void Editor::set_text(std::string_view t) {
    text_ = t;
    cursor_x_ = cursor_y_ = 0;
    scroll_col_ = 0;
    invalidate();
}

void Editor::focus(bool f) {
    focused_ = f;
    if (f) {
        cursor_x_ = (int)text_.size();
        cursor_y_ = 0;
    }
}

std::vector<std::string> Editor::compute_lines(int width) const {
    if (cache_width_ == width && !line_cache_.empty())
        return line_cache_;

    line_cache_.clear();
    cache_width_ = width;
    int avail = std::max(1, width - 6); // " N | "

    if (text_.empty()) {
        line_cache_.push_back("");
        return line_cache_;
    }

    std::string_view sv(text_);
    size_t pos = 0;
    while (pos < sv.size()) {
        auto nl = sv.find('\n', pos);
        size_t end = (nl == std::string_view::npos) ? sv.size() : nl;
        std::string_view seg(sv.data() + pos, end - pos);

        // Word wrap
        size_t start = 0;
        while (start < seg.size()) {
            size_t chunk = std::min(start + avail, seg.size());
            line_cache_.push_back(std::string(seg.substr(start, chunk - start)));
            start = chunk;
        }
        if (nl != std::string_view::npos) {
            if (end == pos) line_cache_.push_back("");
            pos = nl + 1;
        } else break;
    }
    if (line_cache_.empty()) line_cache_.push_back("");
    return line_cache_;
}

int Editor::cursor_index() const {
    if (text_.empty()) return 0;
    std::string_view sv(text_);
    int idx = 0;
    int line = 0, col = 0;
    for (size_t i = 0; i < sv.size();) {
        if (line == cursor_y_ && col == cursor_x_) return (int)i;
        if (sv[i] == '\n') { line++; col = 0; i++; }
        else { col++; i++; }
        if (col > 100000) break; // safety
    }
    return (int)sv.size();
}

void Editor::cursor_from_index(int idx) {
    cursor_y_ = cursor_x_ = 0;
    for (int i = 0; i < idx && i < (int)text_.size(); ++i) {
        if (text_[i] == '\n') { cursor_y_++; cursor_x_ = 0; }
        else cursor_x_++;
    }
}

void Editor::insert(char c) {
    int idx = cursor_index();
    if (c == '\n') {
        text_.insert(text_.begin() + idx, '\n');
        cursor_y_++;
        cursor_x_ = 0;
    } else {
        text_.insert(text_.begin() + idx, c);
        cursor_x_++;
    }
    line_cache_.clear();
    invalidate();
}



void Editor::backspace() {
    if (text_.empty() || (cursor_y_ == 0 && cursor_x_ == 0)) return;
    int idx = cursor_index();
    if (idx <= 0) return;
    // Determine if we're deleting a newline
    if (idx > 0 && text_[idx - 1] == '\n') {
        cursor_y_--;
        // Find start of current line
        int prev_nl = (int)text_.rfind('\n', idx - 2);
        cursor_x_ = (prev_nl < 0) ? idx - 1 : idx - prev_nl - 2;
    } else {
        cursor_x_--;
    }
    text_.erase(idx - 1, 1);
    line_cache_.clear();
    invalidate();
}

void Editor::del() {
    if (text_.empty()) return;
    int idx = cursor_index();
    if (idx >= (int)text_.size()) return;
    text_.erase(idx, 1);
    line_cache_.clear();
    invalidate();
}

void Editor::move_left() {
    if (cursor_x_ > 0) { cursor_x_--; return; }
    if (cursor_y_ > 0) {
        cursor_y_--;
        // Find end of previous line
        cursor_x_ = 0;
        int idx = cursor_index();
        // Walk to end of line
        while (idx < (int)text_.size() && text_[idx] != '\n') { cursor_x_++; idx++; }
    }
}

void Editor::move_right() {
    int idx = cursor_index();
    if (idx >= (int)text_.size()) return;
    if (text_[idx] == '\n') { cursor_y_++; cursor_x_ = 0; }
    else cursor_x_++;
}

void Editor::move_up() {
    if (cursor_y_ <= 0) return;
    cursor_y_--;
    int idx = cursor_index();
    // Clamp x to line length
    int max_x = 0;
    while (idx < (int)text_.size() && text_[idx] != '\n') { max_x++; idx++; }
    if (cursor_x_ > max_x) cursor_x_ = max_x;
}

void Editor::move_down() {
    int idx = cursor_index();
    // Find next newline
    while (idx < (int)text_.size() && text_[idx] != '\n') idx++;
    if (idx >= (int)text_.size()) return;
    idx++; // skip newline
    cursor_y_++;
    // Clamp x
    int max_x = 0;
    while (idx < (int)text_.size() && text_[idx] != '\n') { max_x++; idx++; }
    if (cursor_x_ > max_x) cursor_x_ = max_x;
}

void Editor::move_home() { cursor_x_ = 0; }
void Editor::move_end() {
    int idx = cursor_index();
    cursor_x_ = 0;
    while (idx < (int)text_.size() && text_[idx] != '\n') { cursor_x_++; idx++; }
}


std::vector<std::string> Editor::render(int width) {
    auto lines = compute_lines(width);
    std::vector<std::string> result;

    int line_num = 1;
    for (auto& l : lines) {
        std::string prefix;
        if (focused_)
            prefix = term::fg(term::GRAY) + std::format("{:>3}|", line_num) + term::RESET;
        else
            prefix = std::format("{:>3} ", line_num);

        std::string line = prefix + l;
        if ((int)line.size() < width) line.append(width - line.size(), ' ');
        result.push_back(line);
        line_num++;
    }

    // Ensure minimum edit height
    while ((int)result.size() < edit_height_) {
        std::string prefix;
        if (focused_)
            prefix = term::fg(term::GRAY) + std::format("{:>3}|", line_num) + term::RESET;
        else
            prefix = std::format("{:>3} ", line_num);
        std::string line = prefix + std::string(std::max(0, width - (int)prefix.size()), ' ');
        result.push_back(line);
        line_num++;
    }

    // Bottom border
    std::string border(width, '-');
    if (focused_)
        result.push_back(term::fg(term::GRAY) + border + term::RESET);
    else
        result.push_back(border);

    return result;
}

bool Editor::handle_input(const InputEvent& ev) {
    if (!focused_) return false;

    // Tab autocomplete
    if (ev.key == Key::Tab) {
        std::string prefix;
        int idx = cursor_index();
        // Find start of current word
        int start = idx;
        while (start > 0 && text_[start - 1] != ' ' && text_[start - 1] != '\n') start--;
        prefix = text_.substr(start, idx - start);

        if (on_tab && !prefix.empty()) {
            auto completions = on_tab(prefix);
            if (completions.size() == 1) {
                // Replace prefix with completion
                text_.erase(start, idx - start);
                text_.insert(start, completions[0]);
                cursor_from_index(start + (int)completions[0].size());
                line_cache_.clear();
                invalidate();
                return true;
            }
        }
        // Default: insert tab
        insert(' ');
        return true;
    }

    // Enter
    if (ev.key == Key::Enter) {
        if (on_submit && !text_.empty()) {
            std::string input = text_;
            history_.push_back(input);
            history_pos_ = (int)history_.size();
            text_.clear();
            cursor_x_ = cursor_y_ = 0;
            line_cache_.clear();
            invalidate();
            on_submit(input);
        }
        return true;
    }

    // Arrow keys
    if (ev.key == Key::Up) {
        if (history_.empty()) return true;
        if (history_pos_ > 0) {
            if (history_pos_ == (int)history_.size()) {
                // Save current input
                saved_input_ = text_;
            }
            history_pos_--;
            set_text(history_[history_pos_]);
        }
        return true;
    }
    if (ev.key == Key::Down) {
        if (history_pos_ < (int)history_.size() - 1) {
            history_pos_++;
            set_text(history_[history_pos_]);
        } else if (history_pos_ == (int)history_.size() - 1) {
            history_pos_ = (int)history_.size();
            set_text(saved_input_);
        }
        return true;
    }
    if (ev.key == Key::Left) { move_left(); return true; }
    if (ev.key == Key::Right) { move_right(); return true; }
    if (ev.key == Key::Home) { move_home(); return true; }
    if (ev.key == Key::End) { move_end(); return true; }

    // Backspace / Delete
    if (ev.key == Key::Backspace) { backspace(); return true; }
    if (ev.key == Key::Delete) { del(); return true; }

    // Ctrl+U: delete line
    if (ev.key == Key::CtrlU) {
        text_.clear(); cursor_x_ = cursor_y_ = 0; line_cache_.clear(); invalidate(); return true;
    }
    // Ctrl+W: delete word
    if (ev.key == Key::CtrlW) {
        int idx = cursor_index();
        int start = idx;
        while (start > 0 && text_[start - 1] == ' ') start--;
        while (start > 0 && text_[start - 1] != ' ') start--;
        text_.erase(start, idx - start);
        cursor_from_index(start);
        line_cache_.clear(); invalidate(); return true;
    }

    // Printable character
    if (ev.ch >= 0x20) {
        insert(ev.ch);
        return true;
    }

    return true; // claim all input when focused
}

} // namespace pi::tui
