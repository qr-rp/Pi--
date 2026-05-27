#include "text.hpp"
#include "term.hpp"

namespace pi::tui {

std::vector<std::string> Text::render(int width) {
    if (text_.empty()) return {};

    int avail = std::max(1, width - pad_left_ - pad_right_);
    int pad_str = pad_left_ + pad_right_;

    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos < text_.size()) {
        // Find next newline or end
        auto nl = text_.find('\n', pos);
        size_t line_end = (nl == std::string::npos) ? text_.size() : nl;
        std::string_view segment(text_.data() + pos, line_end - pos);

        // Wrap long lines
        int seg_width = term::visible_width(segment);
        if (seg_width > avail) {
            // Simple word-wrap
            size_t wrap = 0;
            int cur_w = 0;
            for (size_t i = 0; i < segment.size();) {
                if (segment[i] == '\x1b') {
                    auto m = segment.find('m', i);
                    if (m != std::string::npos) { wrap = m + 1; i = m + 1; continue; }
                }
                if (cur_w >= avail) break;
                cur_w++;
                wrap = ++i;
            }
            segment = segment.substr(0, wrap);
            pos += wrap;
        } else {
            pos = line_end + 1;
        }

        std::string line(pad_left_, ' ');
        line += segment;
        if (line.size() < (size_t)width)
            line.append(width - line.size(), ' ');
        lines.push_back(line);
    }

    return lines;
}

} // namespace pi::tui
