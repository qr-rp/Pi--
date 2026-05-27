#pragma once

#include "component.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace pi::tui {

// ── Full Markdown renderer with syntax highlighting ──────────────────────
class Markdown : public Component {
public:
    Markdown(std::string text = "", int pad_left = 0, int pad_right = 0);
    void set_text(std::string_view t);
    void append_text(std::string_view t);
    const std::string& text() const { return text_; }

    std::vector<std::string> render(int width) override;

    // Control tool call folding
    void toggle_fold(size_t idx);
    bool folded(size_t idx) const;

private:
    std::string text_;
    int pad_left_, pad_right_;

    struct Block {
        enum Type { Paragraph, Heading1, Heading2, Heading3,
                    Bullet, Numbered, Blockquote, CodeFence,
                    HorizontalRule, ToolCallHeader, ToolCallResult, ToolCallError };
        Type type;
        std::string content;
        std::string lang;    // for code blocks
        int level = 0;       // heading level
    };

    std::vector<Block> blocks_;
    bool dirty_ = true;
    std::vector<std::string> cache_;
    int cache_width_ = 0;

    // Folding state for tool calls
    mutable std::unordered_map<size_t, bool> fold_state_;

    void rebuild_blocks();
    std::string highlight_code(std::string_view code, std::string_view lang) const;
    std::string highlight_line(std::string_view line, std::string_view lang) const;
    std::string render_block(const Block& b, int width) const;
    std::vector<std::string> render_code_block(const Block& b, int width) const;
};

} // namespace pi::tui
