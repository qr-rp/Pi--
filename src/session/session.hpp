#pragma once

#include "core/types.hpp"

#include <string>
#include <vector>
#include <chrono>

namespace pi {

// ── Session ───────────────────────────────────────────────────────────────
// Manages conversation history and metadata for an agent run.
class Session {
public:
    Session();

    // Add a message to history
    void add_message(const Message& msg);

    // Get all messages
    [[nodiscard]] const std::vector<Message>& messages() const noexcept { return messages_; }

    // Get system prompt
    [[nodiscard]] std::string_view system_prompt() const noexcept { return system_prompt_; }
    void set_system_prompt(std::string_view prompt) { system_prompt_ = prompt; }

    // Get session ID
    [[nodiscard]] const std::string& id() const noexcept { return id_; }

    // Token count tracking (approximate)
    void set_token_count(int64_t count) noexcept { total_tokens_ = count; }
    [[nodiscard]] int64_t token_count() const noexcept { return total_tokens_; }

    // Turn count
    [[nodiscard]] int turn_count() const noexcept { return turn_count_; }
    void increment_turn() noexcept { turn_count_++; }

    // Build the full message list for API calls (system + history)
    [[nodiscard]] std::vector<Message> build_api_messages() const;

    // Reset to initial state (keep system prompt)
    void reset();

private:
    std::string id_;
    std::string system_prompt_;
    std::vector<Message> messages_;
    int64_t total_tokens_ = 0;
    int turn_count_ = 0;
};

} // namespace pi
