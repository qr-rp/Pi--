#include "provider.hpp"

#include "core/error.hpp"

#include <condition_variable>
#include <mutex>

namespace pi {

Result<std::string> Provider::chat_completion_sync(
    const std::vector<Message>& messages,
    const ChatOptions& options,
    const std::vector<ToolDefinition>& tools
) {
    std::string full_content;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    std::string error_message;

    ChatOptions stream_opts = options;
    stream_opts.stream = true;

    auto cb = [&](const StreamEvent& event) {
        switch (event.type) {
            case StreamEventType::kChunk:
                full_content += event.content;
                break;
            case StreamEventType::kToolCall:
                if (!full_content.empty() && !full_content.ends_with('\n')) full_content += '\n';
                full_content += std::format("<tool_call>\n{}\n{}\n{}\n</tool_call>",
                                            event.tool_call.id,
                                            event.tool_call.name,
                                            event.tool_call.arguments);
                break;
            case StreamEventType::kDone:
                {
                    std::lock_guard lk(mtx);
                    done = true;
                    cv.notify_one();
                }
                break;
            case StreamEventType::kError:
                {
                    std::lock_guard lk(mtx);
                    error_message = event.error_message;
                    done = true;
                    cv.notify_one();
                }
                break;
        }
    };

    auto result = chat_completion(messages, stream_opts, tools, cb);
    if (!result) return std::unexpected(result.error());

    {
        std::unique_lock lk(mtx);
        cv.wait(lk, [&] { return done; });
    }

    if (!error_message.empty()) {
        return std::unexpected(Error(ErrCode::kStreamError, error_message));
    }
    return full_content;
}

} // namespace pi
