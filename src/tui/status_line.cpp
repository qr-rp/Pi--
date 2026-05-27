#include "status_line.hpp"
#include "term.hpp"

namespace pi::tui {

StatusLine::StatusLine() {
    status_ = " Ready";
}

std::vector<std::string> StatusLine::render(int width) {
    std::string line;

    // Left: status
    line += term::fg(term::GRAY) + status_ + term::RESET;

    // Right: model | provider | locale
    std::string right;
    right += term::fg(term::WHITE) + term::BOLD + " " + model_ + term::RESET;
    right += term::fg(term::GRAY) + " |" + term::RESET;
    right += term::fg(term::WHITE) + term::BOLD + " " + provider_ + term::RESET;
    right += term::fg(term::GRAY) + " |" + term::RESET;
    right += term::fg(term::WHITE) + term::BOLD + " " + locale_ + term::RESET;

    int right_w = term::visible_width(right);
    int left_w = term::visible_width(line);

    // Pad between
    int pad = std::max(1, width - left_w - right_w - 2);
    line += std::string(pad, ' ') + right;

    if ((int)line.size() < width)
        line.append(width - line.size(), ' ');

    return {line};
}

} // namespace pi::tui
