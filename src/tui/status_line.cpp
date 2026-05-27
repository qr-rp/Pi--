#include "status_line.hpp"
#include "term.hpp"

namespace pi::tui {

StatusLine::StatusLine() { status_ = " Ready"; }

std::vector<std::string> StatusLine::render(int width) {
    if (!dirty_ && cache_width_ == width && !cache_.empty())
        return cache_;
    dirty_ = false;
    cache_width_ = width;

    std::string left;
    left += term::fg(term::GRAY) + status_ + term::RESET;

    // Tool count
    if (tool_count_ > 0)
        left += " " + term::fg(term::YELLOW) + "[" + std::to_string(tool_count_) + " tools]" + term::RESET;

    // Tokens
    if (tokens_in_ > 0 || tokens_out_ > 0) {
        left += " " + term::fg(term::GRAY) + "in:" + term::RESET + std::to_string(tokens_in_)
              + term::fg(term::GRAY) + " out:" + term::RESET + std::to_string(tokens_out_);
    }

    // Right side: model | provider | locale
    std::string right;
    right += term::fg(term::WHITE) + term::BOLD + " " + model_ + term::RESET;
    right += term::fg(term::GRAY) + " |" + term::RESET;
    right += term::fg(term::WHITE) + term::BOLD + " " + provider_ + term::RESET;
    right += term::fg(term::GRAY) + " |" + term::RESET;
    right += term::fg(term::WHITE) + term::BOLD + " " + locale_ + term::RESET;

    // Hint in center (if set)
    std::string hint_part;
    if (!hint_.empty()) {
        hint_part = " " + term::fg(term::GRAY) + hint_ + term::RESET + " ";
    }

    int left_w = term::visible_width(left);
    int right_w = term::visible_width(right);
    int hint_w = term::visible_width(hint_part);

    // Layout: left ... hint ... right
    std::string line;
    line += left;

    int pad1 = std::max(1, width - left_w - hint_w - right_w - 2);
    if (hint_w > 0) {
        // Center the hint roughly
        int before_hint = (pad1 - hint_w) / 2;
        if (before_hint < 1) before_hint = 1;
        line += std::string(before_hint, ' ');
        line += hint_part;
        pad1 -= before_hint + hint_w;
    }
    line += std::string(std::max(0, pad1), ' ');
    line += right;

    if ((int)line.size() < width)
        line.append(width - line.size(), ' ');

    cache_ = {std::move(line)};
    return cache_;
}

} // namespace pi::tui
