#include "message.hpp"

#include <sstream>

namespace pi {
namespace message_utils {

int64_t estimate_tokens(const Message& msg) {
    // Rough estimate: ~4 chars per token for English text
    int64_t count = static_cast<int64_t>(msg.content.size()) / 4;
    for (auto& tc : msg.tool_calls) {
        count += tc.name.size() / 4;
        count += tc.arguments.size() / 4;
    }
    if (count < 1) count = 1;
    return count;
}

int64_t estimate_tokens(const std::vector<Message>& msgs) {
    int64_t total = 0;
    for (auto& msg : msgs) {
        total += estimate_tokens(msg);
    }
    return total;
}

std::vector<Message> truncate_to_fit(const std::vector<Message>& messages, int64_t max_tokens) {
    if (max_tokens <= 0) return messages;

    auto result = messages;
    while (estimate_tokens(result) > max_tokens && result.size() > 1) {
        // Remove the oldest user+assistant pair (keep system prompt at index 0)
        if (result.size() >= 2) {
            result.erase(result.begin() + 1); // Remove first non-system message
        } else {
            break;
        }
    }
    return result;
}

std::vector<ToolCall> parse_tool_calls(std::string_view content) {
    std::vector<ToolCall> calls;
    std::string_view remaining = content;

    // Parse <tool_call> blocks from content
    // Format: <tool_call>\n<id>\n<name>\n<arguments>\n</tool_call>
    while (true) {
        auto start = remaining.find("<tool_call>\n");
        if (start == std::string_view::npos) break;

        auto inner = remaining.substr(start + 12); // skip <tool_call>\n
        auto end = inner.find("\n</tool_call>");
        if (end == std::string_view::npos) break;

        auto block = inner.substr(0, end);
        remaining = inner.substr(end + 13); // skip past </tool_call>

        ToolCall tc;
        auto first_nl = block.find('\n');
        if (first_nl == std::string_view::npos) continue;
        tc.id = block.substr(0, first_nl);

        auto rest = block.substr(first_nl + 1);
        auto second_nl = rest.find('\n');
        if (second_nl == std::string_view::npos) continue;
        tc.name = rest.substr(0, second_nl);
        tc.arguments = rest.substr(second_nl + 1);

        calls.push_back(std::move(tc));
    }

    return calls;
}

std::string format_tool_result(const ToolResult& result) {
    if (result.success) {
        return result.output;
    }
    return std::format("Error: {}\n{}", result.error_message, result.output);
}

} // namespace message_utils
} // namespace pi
