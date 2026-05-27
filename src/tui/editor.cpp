#include "editor.hpp"
#include "tui.hpp"
#include "term.hpp"
#include <algorithm>

namespace pi::tui {

Editor::Editor() { text_.reserve(4096); }

void Editor::set_text(std::string_view t) { text_ = t; cursor_x_ = 0; cursor_y_ = 0; line_cache_.clear(); invalidate(); }
void Editor::focus(bool f) { focused_ = f; if (f) { cursor_x_ = (int)text_.size(); cursor_y_ = 0; } }

std::vector<std::string> Editor::compute_lines(int width) const {
    if (cache_width_ == width && !line_cache_.empty()) return line_cache_;
    line_cache_.clear(); cache_width_ = width;
    int avail = std::max(1, width - 6);
    if (text_.empty()) { line_cache_.push_back(""); return line_cache_; }
    std::string_view sv(text_); size_t pos = 0;
    while (pos < sv.size()) {
        auto nl = sv.find('\n', pos);
        size_t end = (nl == std::string_view::npos) ? sv.size() : nl;
        std::string_view seg(sv.data() + pos, end - pos);
        size_t start = 0;
        while (start < seg.size()) {
            size_t chunk = std::min(start + avail, seg.size());
            line_cache_.push_back(std::string(seg.substr(start, chunk - start)));
            start = chunk;
        }
        if (nl != std::string_view::npos) { if (end == pos) line_cache_.push_back(""); pos = nl + 1; }
        else break;
    }
    if (line_cache_.empty()) line_cache_.push_back("");
    return line_cache_;
}

int Editor::cursor_index() const {
    if (text_.empty()) return 0;
    int ln = 0, col = 0;
    for (size_t i = 0; i < text_.size(); ++i) {
        if (ln == cursor_y_ && col == cursor_x_) return (int)i;
        if (text_[i] == '\n') { ln++; col = 0; }
        else col++;
    }
    return (int)text_.size();
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
    if (c == '\n') { text_.insert(text_.begin() + idx, '\n'); cursor_y_++; cursor_x_ = 0; }
    else { text_.insert(text_.begin() + idx, c); cursor_x_++; }
    line_cache_.clear(); invalidate();
}

void Editor::backspace() {
    if (text_.empty() || (cursor_y_ == 0 && cursor_x_ == 0)) return;
    int idx = cursor_index();
    if (idx <= 0) return;
    if (idx > 0 && text_[idx - 1] == '\n') {
        cursor_y_--;
        int pn = (int)text_.rfind('\n', idx - 2);
        cursor_x_ = (pn < 0) ? idx - 1 : idx - pn - 2;
    } else { cursor_x_--; }
    text_.erase(idx - 1, 1); line_cache_.clear(); invalidate();
}

void Editor::del() {
    if (text_.empty()) return;
    int idx = cursor_index();
    if (idx >= (int)text_.size()) return;
    text_.erase(idx, 1); line_cache_.clear(); invalidate();
}

void Editor::move_left() {
    if (cursor_x_ > 0) { cursor_x_--; return; }
    if (cursor_y_ > 0) {
        cursor_y_--;
        int idx = (int)text_.rfind('\n', cursor_index() - 2);
        cursor_x_ = (idx < 0) ? cursor_index() : cursor_index() - idx - 2;
        // Nope, simpler: find end of previous line
        cursor_x_ = 0;
        int ci = cursor_index();
        while (ci < (int)text_.size() && text_[ci] != '\n') { cursor_x_++; ci++; }
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
    int idx = cursor_index(), mx = 0;
    while (idx < (int)text_.size() && text_[idx] != '\n') { mx++; idx++; }
    if (cursor_x_ > mx) cursor_x_ = mx;
}

void Editor::move_down() {
    int idx = cursor_index();
    while (idx < (int)text_.size() && text_[idx] != '\n') idx++;
    if (idx >= (int)text_.size()) return;
    idx++; cursor_y_++;
    int mx = 0;
    while (idx < (int)text_.size() && text_[idx] != '\n') { mx++; idx++; }
    if (cursor_x_ > mx) cursor_x_ = mx;
}

void Editor::move_home() { cursor_x_ = 0; }
void Editor::move_end() {
    int idx = cursor_index(); cursor_x_ = 0;
    while (idx < (int)text_.size() && text_[idx] != '\n') { cursor_x_++; idx++; }
}

std::vector<std::string> Editor::render(int width) {
    auto lines = compute_lines(width);
    std::vector<std::string> result;
    int ln = 1;
    for (auto& l : lines) {
        std::string p = focused_ ? term::fg(term::GRAY) + std::format("{:>3}|", ln) + term::RESET
                                 : std::format("{:>3} ", ln);
        std::string line = p + l;
        if ((int)line.size() < width) line.append(width - line.size(), ' ');
        result.push_back(line); ln++;
    }
    while ((int)result.size() < edit_height_) {
        std::string p = focused_ ? term::fg(term::GRAY) + std::format("{:>3}|", ln) + term::RESET
                                 : std::format("{:>3} ", ln);
        result.push_back(p + std::string(std::max(0, width - (int)p.size()), ' ')); ln++;
    }
    std::string bdr(width, '-');
    result.push_back(focused_ ? term::fg(term::GRAY) + bdr + term::RESET : bdr);
    return result;
}

bool Editor::handle_input(const InputEvent& ev) {
    if (!focused_) return false;

    // Emacs keys
    if (ev.key == Key::CtrlA) { move_home(); return true; }
    if (ev.key == Key::CtrlE) { move_end(); return true; }
    if (ev.key == Key::CtrlF) { move_right(); return true; }
    if (ev.key == Key::CtrlB) { move_left(); return true; }
    if (ev.key == Key::CtrlD) { del(); return true; }
    if (ev.key == Key::CtrlH || ev.key == Key::Backspace) { backspace(); return true; }
    if (ev.key == Key::Delete) { del(); return true; }
    if (ev.key == Key::CtrlK) {
        int idx = cursor_index();
        auto nl = text_.find('\n', idx);
        if (nl == std::string_view::npos) text_.erase(idx);
        else text_.erase(idx, nl - idx);
        line_cache_.clear(); invalidate(); return true;
    }
    if (ev.key == Key::CtrlU) { text_.clear(); cursor_x_=cursor_y_=0; line_cache_.clear(); invalidate(); return true; }
    if (ev.key == Key::CtrlW) {
        int idx = cursor_index(), st = idx;
        while (st > 0 && text_[st-1] == ' ') st--;
        while (st > 0 && text_[st-1] != ' ') st--;
        text_.erase(st, idx-st); cursor_from_index(st); line_cache_.clear(); invalidate(); return true;
    }
    if (ev.key == Key::CtrlT) {
        int idx = cursor_index();
        if (idx > 0 && idx < (int)text_.size()) {
            std::swap(text_[idx-1], text_[idx]);
            if (text_[idx] != '\n') cursor_x_++;
            line_cache_.clear(); invalidate();
        }
        return true;
    }

    // Tab
    if (ev.key == Key::Tab) {
        int idx = cursor_index(), st = idx;
        while (st > 0 && text_[st-1] != ' ' && text_[st-1] != '\n') st--;
        std::string pref = text_.substr(st, idx-st);
        if (on_tab && !pref.empty()) {
            auto c = on_tab(pref);
            if (c.size() == 1) {
                text_.erase(st, idx-st); text_.insert(st, c[0]);
                cursor_from_index(st + (int)c[0].size());
                line_cache_.clear(); invalidate(); return true;
            }
        }
        insert(' '); return true;
    }

    // Enter
    if (ev.key == Key::Enter) {
        if (on_submit && !text_.empty()) {
            history_.push_back(text_);
            history_pos_ = (int)history_.size();
            text_.clear(); cursor_x_=cursor_y_=0; line_cache_.clear(); invalidate();
            on_submit(history_.back());
        }
        return true;
    }

    // Arrows / History
    if (ev.key == Key::Up) {
        if (history_.empty()) return true;
        if (history_pos_ > 0) {
            if (history_pos_ == (int)history_.size()) saved_input_ = text_;
            history_pos_--;
            set_text(history_[history_pos_]);
        }
        return true;
    }
    if (ev.key == Key::Down) {
        if (history_pos_ < (int)history_.size()-1) { history_pos_++; set_text(history_[history_pos_]); }
        else if (history_pos_ == (int)history_.size()-1) { history_pos_ = (int)history_.size(); set_text(saved_input_); }
        return true;
    }
    if (ev.key == Key::Left)  { move_left();  return true; }
    if (ev.key == Key::Right) { move_right(); return true; }
    if (ev.key == Key::Home)  { move_home();  return true; }
    if (ev.key == Key::End)   { move_end();   return true; }

    // UTF-8 / printable
    if (!ev.text.empty()) {
        int idx = cursor_index();
        text_.insert(idx, ev.text);
        cursor_x_++;
        line_cache_.clear(); invalidate();
        return true;
    }
    if (ev.ch >= 0x20) { insert(ev.ch); return true; }

    return true;
}

} // namespace pi::tui
