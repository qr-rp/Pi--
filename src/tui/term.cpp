#include "term.hpp"
#include <algorithm>

namespace pi::term {

int visible_width(std::string_view s) {
    int w = 0;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\x1b') {
            auto end = s.find('m', i);
            if (end == std::string_view::npos) end = s.find('H', i);
            if (end == std::string_view::npos) end = s.find('J', i);
            if (end == std::string_view::npos) end = s.find('K', i);
            if (end != std::string_view::npos) { i = end + 1; continue; }
        }
        // Skip non-C0 control chars besides tab/newline
        if (s[i] < 0x20 && s[i] != '\t' && s[i] != '\n') { i++; continue; }
        // TODO: handle wide chars for CJK
        w++;
        i++;
    }
    return w;
}

std::string strip_ansi(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\x1b') {
            auto end = s.find('m', i);
            if (end == std::string_view::npos) {
                // Try other escape terminators
                if (s[i+1] == ']') {
                    end = s.find('\x07', i);
                    if (end != std::string_view::npos) { i = end + 1; continue; }
                }
                end = s.find_first_of("ABCDGJKHlhrst", i+1);
            }
            if (end != std::string_view::npos) { i = end + 1; continue; }
        }
        out += s[i++];
    }
    return out;
}

std::string scroll_indicator(int shown, int total, int width) {
    if (shown >= total) return "";
    int hidden = total - shown;
    std::string txt = std::format(" \u2191 {} more lines ", hidden);
    if ((int)txt.size() > width) return "";
    std::string line(width - txt.size(), ' ');
    line += txt;
    return fg(GRAY) + line + RESET;
}
} // namespace pi::term
