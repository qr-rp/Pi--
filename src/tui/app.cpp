#include "tui/app.hpp"
#include "tui/term.hpp"

#include <iostream>
#include <sstream>

namespace pi {

TuiApp::TuiApp(Config* config, ProviderRegistry* providers, ToolRegistry* tools)
    : config_(config)
    , providers_(providers)
    , tools_(tools)
    , agent_(config, providers, tools)
    , chat_markdown_("", 1, 1)
    , status_text_(" Ready")
{
    // Build layout matching omp InteractiveMode:
    // Chat messages (Markdown) → Status text → Editor → Status line
    session_.set_system_prompt(
        "You are pi-coding-agent, a coding agent with tools: read/write/edit/bash/search.");

    // Wire up editor submit
    editor_.on_submit = [this](const std::string& text) {
        on_editor_submit(text);
    };

    // Build component tree
    chat_container_.add(&chat_markdown_);
    root_.add(&chat_container_);
    root_.add(&status_text_);
    root_.add(&editor_);
    root_.add(&status_line_);

    // Set initial status
    auto& cfg = config->agent();
    status_line_.set_model(cfg.default_model);
    status_line_.set_provider(cfg.default_provider);
    status_line_.set_locale(cfg.locale);

    // Welcome message
    append_chat("Welcome to pi-coding-agent");
    append_chat("Type a message and press Enter. Use /help for commands.");
}

TuiApp::~TuiApp() {
    if (agent_thread_.joinable()) agent_thread_.join();
}

int TuiApp::run() {
    engine_.run(&root_);
    return 0;
}

void TuiApp::append_chat(std::string_view text) {
    if (!chat_markdown_.text().empty())
        chat_markdown_.set_text(chat_markdown_.text() + "\n" + std::string(text));
    else
        chat_markdown_.set_text(std::string(text));
    engine_.request_render();
}

void TuiApp::on_editor_submit(const std::string& text) {
    if (text.empty() || agent_busy_) return;

    // Handle commands
    if (text == "/exit" || text == "/quit") {
        engine_.stop();
        return;
    }
    if (text == "/help") {
        append_chat("Commands:\n"
                    "  /exit, /quit     Exit\n"
                    "  /help            This help\n"
                    "  /reset           Reset session\n"
                    "  /config          Show configuration\n"
                    "  /model <id>      Set model\n"
                    "  /provider <n>    Set provider\n"
                    "  Ctrl+C           Cancel generation");
        return;
    }
    if (text == "/reset") {
        session_.reset();
        chat_markdown_.set_text("");
        append_chat("Session reset.");
        return;
    }
    if (text == "/config") {
        auto& cfg = config_->agent();
        std::string out = "Configuration:";
        out += "\n  Model:    " + cfg.default_model;
        out += "\n  Provider: " + cfg.default_provider;
        out += "\n  Locale:   " + cfg.locale;
        for (auto& [pn, pc] : cfg.providers) {
            out += "\n  " + pn + ": " + pc.base_url
                 + (pc.api_key.empty() ? " (no key)" : " (key set)");
        }
        append_chat(out);
        return;
    }
    if (text.starts_with("/model ")) {
        config_->agent().default_model = text.substr(7);
        (void)config_->save();
        status_line_.set_model(config_->agent().default_model);
        append_chat("Model set to: " + text.substr(7));
        return;
    }
    if (text.starts_with("/provider ")) {
        std::string p = text.substr(10);
        if (config_->find_provider(p)) {
            config_->agent().default_provider = p;
            (void)config_->save();
            status_line_.set_provider(config_->agent().default_provider);
            append_chat("Provider set to: " + p);
        } else {
            append_chat("Unknown provider: " + p);
        }
        return;
    }

    // Start agent
    start_agent(text);
}

void TuiApp::start_agent(const std::string& input) {
    if (agent_busy_) return;
    agent_busy_ = true;
    is_streaming_ = true;
    streaming_buffer_.clear();

    append_chat(std::format(">> {}", input));
    status_text_.set_text(" Working...");
    status_line_.set_status(" Working...");
    engine_.request_render();

    agent_thread_ = std::thread([this, input] {
        AgentCallbacks callbacks{
            .on_chunk = [this](std::string_view c) { on_chunk(c); },
            .on_tool_start = [this](std::string_view n) { on_tool_start(n); },
            .on_tool_result = [this](const ToolResult& r) { on_tool_result(r); },
            .on_error = [this](std::string_view e) { on_error(e); },
            .on_turn_complete = []{},
        };
        auto result = agent_.run_conversation(session_, input, callbacks);
        if (!result) on_error(result.error().message);
        on_done();
    });
    agent_thread_.detach();
}

void TuiApp::on_chunk(std::string_view chunk) {
    streaming_buffer_ += chunk;
    if (streaming_buffer_.size() > 50000)
        streaming_buffer_ = streaming_buffer_.substr(streaming_buffer_.size() - 25000);
    // Update chat on last chunk of a coherent block
    engine_.request_render();
}

void TuiApp::on_tool_start(std::string_view name) {
    if (!streaming_buffer_.empty()) {
        append_chat(streaming_buffer_);
        streaming_buffer_.clear();
    }
    is_streaming_ = false;
    append_chat(std::format("[Using tool: {}]", name));
    status_text_.set_text(std::format(" Running tool: {}...", name));
    engine_.request_render();
}

void TuiApp::on_tool_result(const ToolResult& result) {
    if (!result.success) {
        append_chat(std::format("[Tool error: {}]", result.error_message));
    }
}

void TuiApp::on_error(std::string_view err) {
    append_chat(std::format("[Error: {}]", err));
    is_streaming_ = false;
    engine_.request_render();
}

void TuiApp::on_done() {
    if (!streaming_buffer_.empty()) {
        append_chat(streaming_buffer_);
        streaming_buffer_.clear();
    }
    is_streaming_ = false;
    agent_busy_ = false;
    status_text_.set_text(" Ready");
    status_line_.set_status(" Ready");
    engine_.request_render();
}

} // namespace pi
