#include "markdown.hpp"
#include "tui.hpp"
#include "term.hpp"
#include <sstream>
#include <cctype>
#include <unordered_set>

namespace pi::tui {

static const std::unordered_set<std::string> kKeywords = {
    "if","else","for","while","do","switch","case","break","continue",
    "return","goto","try","catch","throw","new","delete","class","struct",
    "enum","union","namespace","using","template","typename","public",
    "private","protected","virtual","override","const","static","extern",
    "inline","explicit","friend","operator","this","true","false","nullptr",
    "NULL","int","float","double","char","void","bool","auto","constexpr",
    "import","export","def","let","var","function","async","await","yield",
    "from","as","fn","mut","impl","trait","where","pub","unsafe","macro",
};

Markdown::Markdown(std::string text, int pad_left, int pad_right)
    : text_(std::move(text)), pad_left_(pad_left), pad_right_(pad_right) {}

void Markdown::set_text(std::string_view t) {
    text_ = t;
    dirty_ = true;
}

void Markdown::append_text(std::string_view t) {
    text_ += t;
    dirty_ = true;
}

void Markdown::toggle_fold(size_t idx) {
    fold_state_[idx] = !fold_state_[idx];
    dirty_ = true;
}

bool Markdown::folded(size_t idx) const {
    auto it = fold_state_.find(idx);
    return it != fold_state_.end() && it->second;
}

void Markdown::rebuild_blocks() {
    blocks_.clear();
    dirty_ = false;

    std::stringstream ss(text_);
    std::string line;
    Block current_fence;
    bool in_code = false;

    while (std::getline(ss, line)) {
        if (!in_code && line.starts_with("```")) {
            // Start code fence
            in_code = true;
            current_fence.type = Block::CodeFence;
            current_fence.lang = line.size() > 3 ? line.substr(3) : "";
            current_fence.content.clear();
            continue;
        }
        if (in_code) {
            if (line.starts_with("```")) {
                in_code = false;
                blocks_.push_back(std::move(current_fence));
                continue;
            }
            if (!current_fence.content.empty()) current_fence.content += '\n';
            current_fence.content += line;
            continue;
        }

        Block b;
        if (line.empty()) { blocks_.push_back(b); continue; }

        if (line.starts_with("# ")) { b.type = Block::Heading1; b.content = line.substr(2); }
        else if (line.starts_with("## ")) { b.type = Block::Heading2; b.content = line.substr(3); }
        else if (line.starts_with("### ")) { b.type = Block::Heading3; b.content = line.substr(4); }
        else if (line.starts_with("- ") || line.starts_with("* ")) { b.type = Block::Bullet; b.content = line.substr(2); }
        else if (std::isdigit(line[0]) && line.size() > 1 && line[1] == '.') { b.type = Block::Numbered; b.content = line; }
        else if (line.starts_with("> ")) { b.type = Block::Blockquote; b.content = line.substr(2); }
        else if (line.starts_with("---") || line.starts_with("***")) { b.type = Block::HorizontalRule; }
        else if (line.starts_with("[Tool]") || line.starts_with("[tool]")) { b.type = Block::ToolCallHeader; b.content = line; }
        else { b.type = Block::Paragraph; b.content = line; }

        blocks_.push_back(std::move(b));
    }
    if (in_code) blocks_.push_back(std::move(current_fence));
}

std::string Markdown::highlight_line(std::string_view line, std::string_view) const {
    std::string result;
    size_t i = 0;
    while (i < line.size()) {
        // Comments //
        if (i + 1 < line.size() && line[i] == '/' && line[i+1] == '/') {
            result += term::fg(term::GRAY) + std::string(line.substr(i)) + term::RESET;
            break;
        }

        // Strings
        if (line[i] == '"' || line[i] == '\'') {
            char quote = line[i];
            result += term::fg(term::BRIGHT_GREEN);
            result += quote;
            i++;
            while (i < line.size() && line[i] != quote) {
                if (line[i] == '\\') { result += line[i]; i++; if (i < line.size()) { result += line[i]; i++; } }
                else { result += line[i]; i++; }
            }
            if (i < line.size()) { result += line[i]; i++; }
            result += term::RESET;
            continue;
        }

        // Numbers
        if (std::isdigit(line[i])) {
            result += term::fg(term::BRIGHT_MAGENTA);
            while (i < line.size() && (std::isalnum(line[i]) || line[i] == '.')) {
                result += line[i]; i++;
            }
            result += term::RESET;
            continue;
        }

        // Identifiers (potential keywords)
        if (std::isalpha(line[i]) || line[i] == '_') {
            size_t start = i;
            while (i < line.size() && (std::isalnum(line[i]) || line[i] == '_')) i++;
            std::string_view word(line.data() + start, i - start);
            if (kKeywords.count(std::string(word)))
                result += term::fg(term::BRIGHT_CYAN) + term::BOLD + std::string(word) + term::RESET;
            else
                result += std::string(word);
            continue;
        }

        // Preprocessor
        if (line[i] == '#') {
            result += term::fg(term::BRIGHT_YELLOW);
            while (i < line.size() && line[i] != '\n') { result += line[i]; i++; }
            result += term::RESET;
            continue;
        }

        result += line[i];
        i++;
    }
    return result;
}

std::string Markdown::highlight_code(std::string_view code, std::string_view lang) const {
    std::string result;
    std::stringstream ss{std::string(code)};
    std::string line;
    bool first = true;
    while (std::getline(ss, line)) {
        if (!first) result += '\n';
        first = false;
        result += highlight_line(line, lang);
    }
    return result;
}

std::string Markdown::render_block(const Block& b, int width) const {
    int w = std::max(0, width - pad_left_ - pad_right_);
    std::string pad(pad_left_, ' ');

    switch (b.type) {
        case Block::Heading1:
            return pad + term::styled(b.content, term::BOLD + term::fg(term::BRIGHT_CYAN));
        case Block::Heading2:
            return pad + term::styled(b.content, term::BOLD + term::fg(term::CYAN));
        case Block::Heading3:
            return pad + term::styled(b.content, term::fg(term::CYAN));
        case Block::Bullet:
            return pad + " " + term::fg(term::YELLOW) + "\u2022" + term::RESET + " " + b.content;
        case Block::Numbered:
            return pad + " " + b.content;
        case Block::Blockquote:
            return pad + " " + term::styled(b.content, term::fg(term::GRAY) + term::ITALIC);
        case Block::HorizontalRule:
            return pad + std::string(std::min(w, 60), '-');
        case Block::ToolCallHeader: {
            // Check if folded
            std::string status = "";
            if (fold_state_.size() > 0) {
                // TODO: track per-tool fold state
            }
            return pad + term::styled(b.content, term::fg(term::BRIGHT_YELLOW));
        }
        default:
            return pad + b.content;
    }
}

std::vector<std::string> Markdown::render_code_block(const Block& b, int width) const {
    std::vector<std::string> lines;
    int w = width - pad_left_ - pad_right_;

    // Lang header
    if (!b.lang.empty()) {
        std::string hdr = std::string(pad_left_, ' ') + term::fg(term::GRAY) + "``` " + b.lang + term::RESET;
        if ((int)hdr.size() < width) hdr.append(width - hdr.size(), ' ');
        lines.push_back(hdr);
    }

    // Highlighted code
    std::string highlighted = highlight_code(b.content, b.lang);
    std::stringstream ss(highlighted);
    std::string line;
    while (std::getline(ss, line)) {
        std::string l = std::string(pad_left_, ' ') + "  " + line;
        if ((int)l.size() < width) l.append(width - l.size(), ' ');
        lines.push_back(l);
    }

    // Footer
    std::string ftr = std::string(pad_left_, ' ') + term::fg(term::GRAY) + "```" + term::RESET;
    if ((int)ftr.size() < width) ftr.append(width - ftr.size(), ' ');
    lines.push_back(ftr);

    return lines;
}

std::vector<std::string> Markdown::render(int width) {
    if (dirty_ || cache_width_ != width) {
        rebuild_blocks();
        std::vector<std::string> result;

        for (auto& b : blocks_) {
            if (b.type == Block::CodeFence) {
                auto code_lines = render_code_block(b, width);
                result.insert(result.end(), code_lines.begin(), code_lines.end());
            } else {
                std::string line = render_block(b, width);
                if ((int)line.size() < width)
                    line.append(width - line.size(), ' ');
                result.push_back(std::move(line));
            }
        }

        cache_ = std::move(result);
        cache_width_ = width;
    }
    return cache_;
}

} // namespace pi::tui
