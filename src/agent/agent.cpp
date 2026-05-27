#include "agent.hpp"
#include "session/message.hpp"
#include <chrono>

namespace pi {

Agent::Agent(Config* config, ProviderRegistry* providers, ToolRegistry* tools)
    : config_(config)
    , providers_(providers)
    , tools_(tools)
{
}

Result<std::string> Agent::run(
    Session& session,
    std::string_view user_input,
    const AgentCallbacks& callbacks,
    std::string_view model_override
) {
    cancelled_ = false;

    // Add user message to session
    session.add_message(Message::user(user_input));

    // Determine model
    std::string model_id;
    std::string provider_name = config_->agent().default_provider;
    if (!model_override.empty()) {
        model_id = model_override;
        // Try to extract provider from model id (provider/model format)
        auto slash_pos = model_id.find('/');
        if (slash_pos != std::string::npos) {
            provider_name = model_id.substr(0, slash_pos);
            model_id = model_id.substr(slash_pos + 1);
        }
    } else {
        model_id = config_->agent().default_model;
    }

    // Get or create provider
    auto provider_result = providers_->get_or_create(provider_name);
    if (!provider_result) {
        session.add_message(Message::assistant(
            std::format("Error: Provider '{}' not available: {}",
                        provider_name, provider_result.error().message)));
        return std::unexpected(provider_result.error());
    }
    auto* provider = *provider_result;

    // Prepare API messages
    auto api_messages = session.build_api_messages();

    // Prepare options
    ChatOptions options;
    options.model = model_id;
    options.temperature = config_->agent().temperature;
    options.stream = true;

    // Add provider-specific headers for DeepSeek
    if (provider_name == "deepseek") {
        // DeepSeek-specific configurations
        options.extra_body["max_tokens"] = 8192;

        // For DeepSeek-R1 reasoning model
        options.thinking_mode = model_id == "deepseek-reasoner" ? "deepseek-reasoner" : "";
    }

    // Get tool definitions
    auto tool_defs = tools_->all_definitions();

    // Accumulate assistant response
    std::string assistant_content;
    std::vector<ToolCall> tool_calls;
    ToolCall current_tool_call;
    bool in_tool_call = false;

    // Stream completion
    auto stream_cb = [&](const StreamEvent& event) {
        switch (event.type) {
            case StreamEventType::kChunk:
                assistant_content += event.content;
                if (callbacks.on_chunk) callbacks.on_chunk(event.content);
                break;

            case StreamEventType::kToolCall:
                if (!in_tool_call) {
                    current_tool_call = event.tool_call;
                    in_tool_call = true;
                } else {
                    // Accumulate arguments from streaming chunks
                    current_tool_call.arguments += event.tool_call.arguments;
                    if (!event.tool_call.name.empty()) current_tool_call.name = event.tool_call.name;
                    if (!event.tool_call.id.empty()) current_tool_call.id = event.tool_call.id;
                }
                break;

            case StreamEventType::kDone:
                if (in_tool_call) {
                    tool_calls.push_back(std::move(current_tool_call));
                    current_tool_call = {};
                    in_tool_call = false;
                }
                break;

            case StreamEventType::kError:
                if (callbacks.on_error) callbacks.on_error(event.error_message);
                break;
        }
    };

    // Make API call
    auto result = provider->chat_completion(api_messages, options, tool_defs, stream_cb);
    if (!result) {
        return std::unexpected(result.error());
    }

    // Store assistant message
    Message assistant_msg(Role::kAssistant, assistant_content);
    if (!tool_calls.empty()) {
        assistant_msg.tool_calls = tool_calls;
    }
    session.add_message(assistant_msg);

    // Process tool calls if any
    if (!tool_calls.empty()) {
        bool all_tool_calls_processed = true;
        auto process_result = process_assistant_response(assistant_content, tool_calls, session, callbacks);
        if (!process_result) {
            return std::unexpected(process_result.error());
        }
        all_tool_calls_processed = *process_result;

        session.increment_turn();
        if (callbacks.on_turn_complete) callbacks.on_turn_complete();
    }

    if (callbacks.on_turn_complete) callbacks.on_turn_complete();

    return assistant_content;
}

Result<bool> Agent::process_assistant_response(
    const std::string& content,
    const std::vector<ToolCall>& tool_calls,
    Session& session,
    const AgentCallbacks& callbacks
) {
    for (auto& tc : tool_calls) {
        if (cancelled_) break;

        if (callbacks.on_tool_start) callbacks.on_tool_start(tc.name);

        // Execute the tool
        auto tool_result = tools_->execute_tool(tc.name, tc.arguments);
        if (!tool_result) {
            std::string err_msg = std::format("Tool '{}' execution failed: {}",
                                              tc.name, tool_result.error().message);
            if (callbacks.on_error) callbacks.on_error(err_msg);

            // Add tool error message to session
            session.add_message(Message::tool(err_msg, tc.id, tc.name));
            continue;
        }

        if (callbacks.on_tool_result) callbacks.on_tool_result(*tool_result);

        // Add tool result to session
        std::string formatted = message_utils::format_tool_result(*tool_result);
        session.add_message(Message::tool(formatted, tc.id, tc.name));
    }

    return true;
}

Result<std::string> Agent::run_conversation(
    Session& session,
    std::string_view user_input,
    const AgentCallbacks& callbacks,
    std::string_view model_override
) {
    cancelled_ = false;

    // Add user message
    session.add_message(Message::user(user_input));

    int max_turns = config_->agent().max_turns;
    std::string last_assistant_content;

    for (int turn = 0; turn < max_turns && !cancelled_; ++turn) {
        // Determine model
        std::string model_id;
        std::string provider_name = config_->agent().default_provider;
        if (!model_override.empty()) {
            model_id = model_override;
            auto slash_pos = model_id.find('/');
            if (slash_pos != std::string::npos) {
                provider_name = model_id.substr(0, slash_pos);
                model_id = model_id.substr(slash_pos + 1);
            }
        } else {
            model_id = config_->agent().default_model;
        }

        auto provider_result = providers_->get_or_create(provider_name);
        if (!provider_result) break;
        auto* provider = *provider_result;

        auto api_messages = session.build_api_messages();
        auto tool_defs = tools_->all_definitions();

        ChatOptions options;
        options.model = model_id;
        options.temperature = config_->agent().temperature;
        options.stream = true;

        // Accumulate
        std::string assistant_content;
        std::vector<ToolCall> tool_calls;
        ToolCall current_tc;
        bool in_tc = false;

        auto stream_cb = [&](const StreamEvent& event) {
            switch (event.type) {
                case StreamEventType::kChunk:
                    assistant_content += event.content;
                    if (callbacks.on_chunk) callbacks.on_chunk(event.content);
                    break;
                case StreamEventType::kToolCall:
                    if (!in_tc) {
                        current_tc = event.tool_call;
                        in_tc = true;
                    } else {
                        current_tc.arguments += event.tool_call.arguments;
                        if (!event.tool_call.name.empty()) current_tc.name = event.tool_call.name;
                        if (!event.tool_call.id.empty()) current_tc.id = event.tool_call.id;
                    }
                    break;
                case StreamEventType::kDone:
                    if (in_tc) {
                        tool_calls.push_back(std::move(current_tc));
                        current_tc = {};
                        in_tc = false;
                    }
                    break;
                case StreamEventType::kError:
                    if (callbacks.on_error) callbacks.on_error(event.error_message);
                    break;
            }
        };

        auto r = provider->chat_completion(api_messages, options, tool_defs, stream_cb);
        if (!r) {
            if (callbacks.on_error) callbacks.on_error(r.error().message);
            break;
        }

        last_assistant_content = assistant_content;

        Message assistant_msg(Role::kAssistant, assistant_content);
        if (!tool_calls.empty()) {
            assistant_msg.tool_calls = tool_calls;
        }
        session.add_message(assistant_msg);

        if (tool_calls.empty()) {
            // No tool calls — model is done
            break;
        }

        // Process tool calls
        for (auto& tc : tool_calls) {
            if (cancelled_) break;
            if (callbacks.on_tool_start) callbacks.on_tool_start(tc.name);

            auto tool_result = tools_->execute_tool(tc.name, tc.arguments);
            if (!tool_result) {
                std::string err_msg = std::format("Tool '{}' failed: {}", tc.name, tool_result.error().message);
                if (callbacks.on_error) callbacks.on_error(err_msg);
                session.add_message(Message::tool(err_msg, tc.id, tc.name));
            } else {
                if (callbacks.on_tool_result) callbacks.on_tool_result(*tool_result);
                session.add_message(Message::tool(message_utils::format_tool_result(*tool_result), tc.id, tc.name));
            }
        }
    }

    return last_assistant_content;
}

} // namespace pi
