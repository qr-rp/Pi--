#include "editor.hpp"
#include "term.hpp"
#include <algorithm>
#include <sys/ioctl.h>

namespace pi::tui {

Editor::Editor() {
    text_.reserve(4096);
}

void Editor::set_text(std::string_view t) {
    text_ = t;
    cursor_line_ = 1;
    cursor_col_ = 1;
    scroll_offset_ = 0;
    invalidate();
}

std::vector<std::string> Editor::display_lines(int width) const {
    std::vector<std::string> lines;
    if (text_.empty()) {
        lines.push_back("");
        return lines;
    }

    int avail = std::max(1, width - 2); // leave room for line number gutter
    std::string_view sv(text_);
    size_t pos = 0;

    while (pos < sv.size()) {
        auto nl = sv.find('\n', pos);
        size_t end = (nl == std::string_view::npos) ? sv.size() : nl;
        std::string_view seg(sv.data() + pos, end - pos);

        // Wrap long lines
        size_t seg_start = 0;
        while (seg_start < seg.size()) {
            size_t chunk_end = std::min(seg_start + avail, seg.size());
            lines.push_back(std::string(seg.substr(seg_start, chunk_end - seg_start)));
            seg_start = chunk_end;
        }

        if (nl != std::string_view::npos) {
            // Represent newline as empty displayed line
            if (end == pos) {
                lines.push_back("");
            }
            pos = nl + 1;
        } else {
            break;
        }
    }

    if (lines.empty()) lines.push_back("");
    return lines;
}

void Editor::clamp_cursor() {
    auto lines = display_lines(80); // use any width, just for line count
    if (cursor_line_ < 1) cursor_line_ = 1;
    if (cursor_line_ > (int)lines.size()) cursor_line_ = (int)lines.size();
}

std::vector<std::string> Editor::render(int width) {
    auto lines = display_lines(width);
    std::vector<std::string> result;

    int line_num = 1;
    int avail = std::max(1, width - 5); // " N | "
    for (auto& l : lines) {
        std::string line;
        if (focused_) {
            line += term::styled(std::format("{:>3}|", line_num), term::fg(term::GRAY));
        } else {
            line += std::format("{:>3} ", line_num);
        }
        // Truncate to width
        if ((int)l.size() > avail) l = l.substr(0, avail);
        line += l;
        if ((int)line.size() < width)
            line.append(width - line.size(), ' ');
        result.push_back(line);
        line_num++;
    }

    // Minimum 3 lines for the editor area
    while ((int)result.size() < 3) {
        if (focused_)
            result.push_back(std::format("{:>3}|", line_num++) + std::string(std::max(0, width-5), ' '));
        else
            result.push_back(std::format("{:>3} ", line_num++) + std::string(std::max(0, width-4), ' '));
        line_num++;
    }

    // Add bottom border
    if (focused_)
        result.push_back(std::format("{:>3}|{}", "+", std::string(width-5, '-')) + term::fg(term::GRAY));
    else
        result.push_back(std::string(width, '-'));

    return result;
}

bool Editor::handle_input(std::string_view data) {
    if (!focused_) return false;

    // Handle raw bytes
    for (char c : data) {
        if (c == '\r' || c == '\n') {
            if (on_submit) {
                std::string input = text_;
                text_.clear();
                cursor_line_ = 1;
                cursor_col_ = 1;
                on_submit(input);
            }
            continue;
        }

        if (c == 127 || c == '\b') { // Backspace
            if (!text_.empty()) {
                // Delete previous UTF-8 char
                auto& t = text_;
                if (!t.empty()) {
                    // Find last UTF-8 start byte
                    size_t pos = t.size() - 1;
                    while (pos > 0 && (t[pos] & 0xC0) == 0x80) pos--;
                    t.erase(pos);
                }
            }
            continue;
        }

        if (c >= 0x20) {
            text_ += c;
        }
    }
    invalidate();
    return true;
}

} // namespace pi::tui
