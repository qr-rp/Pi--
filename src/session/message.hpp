#pragma once

#include "core/types.hpp"

#include <string>

namespace pi {

// Message manipulation utilities
namespace message_utils {

// Estimate token count for a message (rough approximation: chars / 4)
int64_t estimate_tokens(const Message& msg);

// Estimate tokens for a list of messages
int64_t estimate_tokens(const std::vector<Message>& msgs);

// Truncate message list to fit within max_tokens (for context window management)
std::vector<Message> truncate_to_fit(const std::vector<Message>& messages, int64_t max_tokens);

// Parse tool call from JSON (used by agent loop)
std::vector<ToolCall> parse_tool_calls(std::string_view content);

// Format tool result for API consumption
std::string format_tool_result(const ToolResult& result);

} // namespace message_utils

} // namespace pi
