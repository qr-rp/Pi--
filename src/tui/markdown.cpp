#include "markdown.hpp"
#include "term.hpp"
#include <sstream>

namespace pi::tui {

Markdown::Markdown(std::string text, int pad_left, int pad_right)
    : text_(std::move(text)), pad_left_(pad_left), pad_right_(pad_right) {}

void Markdown::set_text(std::string_view t) {
    text_ = t;
    invalidate();
}

std::string Markdown::render_line(std::string_view line, int width) const {
    if (line.empty()) return std::string(width, ' ');

    // Check code block fences
    if (line.starts_with("```")) {
        // Toggle state
        return std::string(pad_left_, ' ') + std::string(width - pad_left_, ' ');
    }

    // Headings
    if (line.starts_with("# ")) {
        return term::styled(std::string(pad_left_, ' ') + std::string(line.substr(2)), 
                            term::BOLD + term::fg(term::BRIGHT_CYAN))
               + std::string(std::max(0, width - (int)line.size() - pad_left_), ' ');
    }
    if (line.starts_with("## ")) {
        return term::styled(std::string(pad_left_, ' ') + std::string(line.substr(3)),
                            term::BOLD + term::fg(term::CYAN))
               + std::string(std::max(0, width - (int)line.size() - pad_left_), ' ');
    }
    if (line.starts_with("### ")) {
        return term::styled(std::string(pad_left_, ' ') + std::string(line.substr(4)),
                            term::fg(term::BRIGHT_CYAN))
               + std::string(std::max(0, width - (int)line.size() - pad_left_), ' ');
    }

    // Unordered list
    if (line.starts_with("- ") || line.starts_with("* ")) {
        return std::string(pad_left_, ' ') + " " + term::fg(term::BRIGHT_YELLOW) + "\u2022" + term::RESET + " " 
               + std::string(line.substr(2))
               + std::string(std::max(0, width - (int)line.size() - pad_left_), ' ');
    }

    // Code blocks (inline)
    if (line.starts_with("    ") || line.starts_with("\t")) {
        return term::styled(std::string(pad_left_, ' ') + std::string(line), term::fg(term::GREEN))
               + std::string(std::max(0, width - (int)line.size() - pad_left_), ' ');
    }

    // Horizontal rule
    if (line.starts_with("---") || line.starts_with("***")) {
        return std::string(pad_left_, ' ') + std::string(width - pad_left_, '\u2500');
    }

    // Blockquote
    if (line.starts_with("> ")) {
        return term::styled(std::string(pad_left_, ' ') + " " + std::string(line),
                            term::fg(term::GRAY) + term::ITALIC)
               + std::string(std::max(0, width - (int)line.size() - pad_left_), ' ');
    }

    // Plain text — render inline formatting
    std::string result;
    result = std::string(pad_left_, ' ') + std::string(line);
    if ((int)result.size() < width)
        result.append(width - result.size(), ' ');
    return result;
}

std::vector<std::string> Markdown::render(int width) {
    std::vector<std::string> lines;
    std::stringstream ss(text_);
    std::string line;

    while (std::getline(ss, line)) {
        // Check for code fence
        if (line.starts_with("```")) {
            in_code_block_ = !in_code_block_;
            code_block_lang_ = in_code_block_ ? line.substr(3) : "";
            lines.push_back(std::string(width, ' ')); // blank line for fence
            continue;
        }

        if (in_code_block_) {
            // Code block content — render with green foreground
            std::string padded(pad_left_, ' ');
            padded += "  " + line;
            if ((int)padded.size() < width)
                padded.append(width - padded.size(), ' ');
            lines.push_back(term::styled(padded, term::fg(term::GREEN)));
            continue;
        }

        lines.push_back(render_line(line, width));
    }

    return lines;
}

} // namespace pi::tui
