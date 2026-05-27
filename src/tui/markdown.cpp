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

void Markdown::set_text(std::string_view t) { text_ = t; dirty_ = true; }
void Markdown::append_text(std::string_view t) { text_ += t; dirty_ = true; }
void Markdown::toggle_fold(size_t idx) {
    auto it = fold_state_.find(idx);
    fold_state_[idx] = (it == fold_state_.end()) ? true : !it->second;
    dirty_ = true;
}
bool Markdown::folded(size_t idx) const {
    auto it = fold_state_.find(idx);
    return it != fold_state_.end() && it->second;
}

void Markdown::rebuild_blocks() {
    blocks_.clear();
    tool_call_blocks_.clear();
    dirty_ = false;

    std::stringstream ss(text_);
    std::string line;
    Block current_fence;
    bool in_code = false;
    bool in_tool = false;
    ToolCallGroup current_tc;

    auto flush_fence = [&] {
        if (!in_code) return;
        in_code = false;
        blocks_.push_back(std::move(current_fence));
    };

    while (std::getline(ss, line)) {
        // Code fences
        if (!in_tool && line.starts_with("```")) {
            if (!in_code) {
                flush_fence();
                in_code = true;
                current_fence = {};
                current_fence.type = Block::CodeFence;
                current_fence.lang = line.size() > 3 ? line.substr(3) : "";
                continue;
            } else { flush_fence(); continue; }
        }
        if (in_code) {
            if (!current_fence.content.empty()) current_fence.content += '\n';
            current_fence.content += line;
            continue;
        }

        // Empty line
        if (line.empty()) { blocks_.push_back({Block::Paragraph, ""}); continue; }

        // Tool call header
        if (line.starts_with("[Tool:") || line.starts_with("[tool:")) {
            flush_fence();
            if (in_tool) {
                blocks_.push_back({Block::ToolCallEnd, ""});
                current_tc.end_block = (int)blocks_.size() - 1;
                tool_call_blocks_.push_back(std::move(current_tc));
                current_tc = {};
            }
            in_tool = true;
            current_tc.name = line.substr(line.find(':') + 1);
            if (current_tc.name.back() == ']') current_tc.name.pop_back();
            current_tc.start_block = (int)blocks_.size();
            blocks_.push_back({Block::ToolCallStart, line});
            continue;
        }

        // Tool error
        if (in_tool && line.starts_with("[Error]")) {
            blocks_.push_back({Block::ToolError, line});
            continue;
        }

        // Tool end marker (another tool or end of tool section)
        if (in_tool && (line.starts_with("---") || line.starts_with("```") || line.starts_with("#"))) {
            // Don't end tool section on these, just pass through
        }

        // Normal markdown blocks
        Block b;
        if (line.starts_with("# ")) { b.type = Block::Heading1; b.content = line.substr(2); }
        else if (line.starts_with("## ")) { b.type = Block::Heading2; b.content = line.substr(3); }
        else if (line.starts_with("### ")) { b.type = Block::Heading3; b.content = line.substr(4); }
        else if (line.starts_with("- ") || line.starts_with("* ")) { b.type = Block::Bullet; b.content = line.substr(2); }
        else if (std::isdigit(line[0]) && line.size() > 1 && line[1] == '.') { b.type = Block::Numbered; b.content = line; }
        else if (line.starts_with("> ")) { b.type = Block::Blockquote; b.content = line.substr(2); }
        else if (line.starts_with("---") || line.starts_with("***")) { b.type = Block::HorizontalRule; }
        else { b.type = Block::Paragraph; b.content = line; }

        blocks_.push_back(std::move(b));
    }

    flush_fence();

    // Close pending tool call
    if (in_tool) {
        blocks_.push_back({Block::ToolCallEnd, ""});
        current_tc.end_block = (int)blocks_.size() - 1;
        tool_call_blocks_.push_back(std::move(current_tc));
    }
}

// ── Syntax highlighting ───────────────────────────────────────────────────
std::string Markdown::highlight_line(std::string_view line, std::string_view) const {
    std::string result;
    size_t i = 0;
    while (i < line.size()) {
        if (i+1 < line.size() && line[i] == '/' && line[i+1] == '/') {
            result += term::fg(term::GRAY) + std::string(line.substr(i)) + term::RESET; break;
        }
        if (line[i] == '"' || line[i] == '\'') {
            char q = line[i];
            result += term::fg(term::BRIGHT_GREEN) + q; i++;
            while (i < line.size() && line[i] != q) {
                if (line[i] == '\\') { result += line[i]; i++; if (i < line.size()) { result += line[i]; i++; } }
                else { result += line[i]; i++; }
            }
            if (i < line.size()) { result += line[i]; i++; }
            result += term::RESET; continue;
        }
        if (std::isdigit(line[i])) {
            result += term::fg(term::BRIGHT_MAGENTA);
            while (i < line.size() && (std::isalnum(line[i]) || line[i] == '.')) { result += line[i]; i++; }
            result += term::RESET; continue;
        }
        if (std::isalpha(line[i]) || line[i] == '_') {
            size_t s = i;
            while (i < line.size() && (std::isalnum(line[i]) || line[i] == '_')) i++;
            std::string_view w(line.data() + s, i - s);
            if (kKeywords.count(std::string(w)))
                result += term::fg(term::BRIGHT_CYAN) + term::BOLD + std::string(w) + term::RESET;
            else
                result += std::string(w);
            continue;
        }
        if (line[i] == '#') {
            result += term::fg(term::BRIGHT_YELLOW);
            while (i < line.size() && line[i] != '\n') { result += line[i]; i++; }
            result += term::RESET; continue;
        }
        result += line[i]; i++;
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

// ── Block rendering ───────────────────────────────────────────────────────
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
            return pad + " " + term::fg(term::YELLOW) + "*" + term::RESET + " " + b.content;
        case Block::Numbered:
            return pad + " " + b.content;
        case Block::Blockquote:
            return pad + " " + term::styled(b.content, term::fg(term::GRAY) + term::ITALIC);
        case Block::HorizontalRule:
            return pad + std::string(std::min(w, 60), '-');
        case Block::ToolCallStart:
            return pad + term::fg(term::BRIGHT_YELLOW) + term::BOLD + "\u25B6 " + term::RESET
                   + term::styled(b.content, term::fg(term::YELLOW));
        case Block::ToolCallEnd:
            return {}; // invisible
        case Block::ToolError:
            return pad + term::styled(b.content, term::fg(term::RED));
        default:
            return pad + b.content;
    }
}

std::vector<std::string> Markdown::render_code_block(const Block& b, int width) const {
    std::vector<std::string> lines;
    int w = width - pad_left_ - pad_right_;
    if (w < 0) w = 0;

    std::string pad(pad_left_, ' ');

    if (!b.lang.empty()) {
        std::string hdr = pad + term::fg(term::GRAY) + "``` " + b.lang + term::RESET;
        lines.push_back(hdr);
    }

    std::string highlighted = highlight_code(b.content, b.lang);
    std::stringstream ss(highlighted);
    std::string line;
    while (std::getline(ss, line)) {
        std::string l = pad + "  " + line;
        if ((int)l.size() < width) l.append(width - l.size(), ' ');
        lines.push_back(l);
    }

    std::string ftr = pad + term::fg(term::GRAY) + "```" + term::RESET;
    lines.push_back(ftr);
    return lines;
}

// ── Main render ───────────────────────────────────────────────────────────
std::vector<std::string> Markdown::render(int width) {
    if (dirty_ || cache_width_ != width) {
        rebuild_blocks();
        std::vector<std::string> result;

        for (size_t bi = 0; bi < blocks_.size(); ++bi) {
            auto& b = blocks_[bi];

            if (b.type == Block::ToolCallEnd) continue; // invisible

            // Check if this block is inside a folded tool call
            bool inside_folded = false;
            for (auto& tc : tool_call_blocks_) {
                if (fold_state_[tc.start_block] && bi > tc.start_block && bi <= (size_t)tc.end_block) {
                    inside_folded = true;
                    break;
                }
            }
            if (inside_folded) continue;

            if (b.type == Block::ToolCallStart) {
                // Find if this is the start of a tool call with fold state
                size_t tcidx = 0;
                for (size_t j = 0; j < tool_call_blocks_.size(); ++j) {
                    if (tool_call_blocks_[j].start_block == (int)bi) { tcidx = j; break; }
                }
                bool is_folded = fold_state_[tcidx];
                std::string line = render_block(b, width);
                // Add fold indicator
                line += term::fg(term::GRAY) + (is_folded ? " [+]" : " [-]") + term::RESET;
                result.push_back(line);

                // If not folded, show a brief preview line
                if (!is_folded) {
                    // Show "Result:" line
                    result.push_back(std::string(pad_left_, ' ') + term::fg(term::GRAY) + "  Result:" + term::RESET);
                }
                continue;
            }

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
