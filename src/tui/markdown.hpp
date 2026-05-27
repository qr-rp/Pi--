#pragma once

#include "component.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace pi::tui {

class Markdown : public Component {
public:
    Markdown(std::string text = "", int pad_left = 0, int pad_right = 0);
    void set_text(std::string_view t);
    void append_text(std::string_view t);
    const std::string& text() const { return text_; }

    std::vector<std::string> render(int width) override;

    // Fold control for tool call blocks
    void toggle_fold(size_t idx);
    bool folded(size_t idx) const;
    size_t tool_call_count() const { return tool_call_blocks_.size(); }
    size_t active_fold() const { return active_fold_; }
    void set_active_fold(size_t i) { active_fold_ = i; }

private:
    std::string text_;
    int pad_left_, pad_right_;

    struct Block {
        enum Type { Paragraph, Heading1, Heading2, Heading3,
                    Bullet, Numbered, Blockquote, CodeFence,
                    HorizontalRule, ToolCallStart, ToolCallEnd, ToolError };
        Type type;
        std::string content;
        std::string lang;
        int level = 0;
    };

    // A tool call group = a start marker + its content blocks
    struct ToolCallGroup {
        std::string name;
        int start_block;  // index of ToolCallStart in blocks_
        int end_block;    // index of ToolCallEnd
    };

    std::vector<Block> blocks_;
    std::vector<ToolCallGroup> tool_call_blocks_;
    bool dirty_ = true;
    std::vector<std::string> cache_;
    int cache_width_ = 0;

    // Folding: true = hidden
    std::unordered_map<size_t, bool> fold_state_;
    size_t active_fold_ = 0;

    void rebuild_blocks();
    std::string highlight_code(std::string_view code, std::string_view lang) const;
    std::string highlight_line(std::string_view line, std::string_view lang) const;
    std::string render_block(const Block& b, int width) const;
    std::vector<std::string> render_code_block(const Block& b, int width) const;
};

} // namespace pi::tui
