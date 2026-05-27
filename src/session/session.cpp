#include "session.hpp"

#include <random>
#include <sstream>

namespace pi {

static std::string generate_session_id() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    std::stringstream ss;
    ss << std::hex << dist(gen) << dist(gen);
    return ss.str();
}

Session::Session()
    : id_(generate_session_id())
    , system_prompt_("You are a helpful coding assistant. You have access to tools.")
{
}

void Session::add_message(const Message& msg) {
    messages_.push_back(msg);
}

std::vector<Message> Session::build_api_messages() const {
    std::vector<Message> api_msgs;

    // System prompt first
    if (!system_prompt_.empty()) {
        api_msgs.push_back(Message::system(system_prompt_));
    }

    // Then conversation history
    api_msgs.insert(api_msgs.end(), messages_.begin(), messages_.end());

    return api_msgs;
}

void Session::reset() {
    messages_.clear();
    total_tokens_ = 0;
    turn_count_ = 0;
}

} // namespace pi
