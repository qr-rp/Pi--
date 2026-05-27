#include "tui/app.hpp"
#include "tui/term.hpp"
#include <iostream>
#include <sstream>
#include <fstream>

namespace pi {

TuiApp::TuiApp(Config* config, ProviderRegistry* providers, ToolRegistry* tools)
    : config_(config)
    , providers_(providers)
    , tools_(tools)
    , agent_(config, providers, tools)
    , chat_markdown_("", 1, 1)
    , status_text_(" Ready")
{
    session_.set_system_prompt(
        "You are pi-coding-agent, a coding agent with tools: read/write/edit/bash/search.");

    // Editor submit
    editor_.on_submit = [this](const std::string& text) {
        on_editor_submit(text);
    };

    // Tab autocomplete
    editor_.on_tab = [this](std::string_view prefix) {
        return autocomplete(prefix);
    };

    // Keyboard handler
    engine_.on_key = [this](tui::InputEvent ev) -> bool {
        if (ev.key == tui::Key::CtrlP && !engine_.overlay_showing()) {
            show_settings();
            return true;
        }
        if (ev.key == tui::Key::CtrlH && !engine_.overlay_showing()) {
            show_help();
            return true;
        }
        if (ev.key == tui::Key::CtrlQ) {
            save_session();
            engine_.stop();
            return true;
        }
        if (ev.key == tui::Key::CtrlC && agent_busy_) {
            agent_.cancel();
            agent_busy_ = false;
            append_chat("[Cancelled]");
            return true;
        }
        if (ev.key == tui::Key::CtrlZ && agent_busy_) {
            // Cycle fold state for tool calls
            return true;
        }
        return false;
    };

    // Build component tree: Chat → Status → Editor → StatusLine
    chat_container_.add(&chat_markdown_);
    root_.add(&chat_container_);
    root_.add(&status_text_);
    root_.add(&editor_);
    root_.add(&status_line_);

    // Focus editor
    editor_.focus(true);

    // Initial status
    auto& cfg = config->agent();
    status_line_.set_model(cfg.default_model);
    status_line_.set_provider(cfg.default_provider);
    status_line_.set_locale(cfg.locale);
    status_line_.set_hint("Ctrl+P:Settings  Ctrl+H:Help  Ctrl+Q:Quit");

    // Load saved session
    load_session();

    // Welcome if no session restored
    if (chat_markdown_.text().empty()) {
        append_chat("## pi-coding-agent");
        append_chat("C++23 coding agent with LLM support.");
        append_chat("  - Type a message and press Enter");
        append_chat("  - Ctrl+P: Settings panel");
        append_chat("  - Ctrl+H: Help");
        append_chat("  - Ctrl+C: Cancel generation");
    }
}

TuiApp::~TuiApp() {
    if (agent_thread_.joinable()) agent_thread_.join();
    save_session();
}

int TuiApp::run() {
    engine_.run(&root_);
    return 0;
}

void TuiApp::append_chat(std::string_view text) {
    std::string existing = chat_markdown_.text();
    if (!existing.empty()) existing += '\n';
    existing += text;
    chat_markdown_.set_text(existing);
    engine_.request_render();
}

void TuiApp::on_editor_submit(const std::string& text) {
    if (agent_busy_) return;

    if (text == "/exit" || text == "/quit") {
        save_session();
        engine_.stop();
        return;
    }
    if (text == "/help") { show_help(); return; }
    if (text == "/reset") {
        session_.reset();
        chat_markdown_.set_text("");
        append_chat("Session reset.");
        return;
    }
    if (text == "/config") { show_config(); return; }
    if (text == "/settings") { show_settings(); return; }

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
    if (text.starts_with("/set key ")) {
        // /set key <provider> <apikey>
        auto rest = text.substr(9);
        auto sp = rest.find(' ');
        if (sp != std::string::npos) {
            std::string pn = rest.substr(0, sp);
            std::string key = rest.substr(sp + 1);
            auto* pc = config_->find_provider(pn);
            if (pc) {
                pc->api_key = key;
                (void)config_->save();
                append_chat("API key saved for " + pn);
            } else {
                ProviderConfig npc;
                npc.api_key = key;
                npc.base_url = "https://api." + pn + ".com";
                npc.api_type = "openai-completions";
                config_->agent().providers[pn] = npc;
                (void)config_->save();
                append_chat("Created provider " + pn + " with API key.");
            }
        }
        return;
    }
    if (text.starts_with("/locale ")) {
        std::string l = text.substr(8);
        config_->agent().locale = l;
        (void)config_->save();
        status_line_.set_locale(l);
        append_chat("Locale set to: " + l);
        return;
    }

    start_agent(text);
}

void TuiApp::start_agent(const std::string& input) {
    if (agent_busy_) return;
    agent_busy_ = true;
    is_streaming_ = true;
    streaming_buffer_.clear();
    tool_call_count_ = 0;

    append_chat("**" + input + "**");
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
    // Update token estimate
    tokens_out_ += chunk.size() / 4;
    status_line_.set_tokens(tokens_in_, tokens_out_);
    engine_.request_render();
}

void TuiApp::on_tool_start(std::string_view name) {
    if (!streaming_buffer_.empty()) {
        append_chat(streaming_buffer_);
        streaming_buffer_.clear();
    }
    is_streaming_ = false;
    current_tool_name_ = name;
    tool_call_count_++;
    append_chat(std::format("[Tool: {}]", name));
    status_text_.set_text(std::format(" Running: {}...", name));
    status_line_.set_tool_count(tool_call_count_);
    engine_.request_render();
}

void TuiApp::on_tool_result(const ToolResult& result) {
    if (!result.success) {
        append_chat(std::format("  Error: {}", result.error_message));
    }
    tokens_in_ += result.output.size() / 4;
    status_line_.set_tokens(tokens_in_, tokens_out_);
}

void TuiApp::on_error(std::string_view err) {
    append_chat(std::format("[Error] {}", err));
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
    save_session();
}

// ── Autocomplete ─────────────────────────────────────────────────────────
std::vector<std::string> TuiApp::autocomplete(std::string_view prefix) {
    static const std::vector<std::string> kCommands = {
        "/help", "/exit", "/quit", "/reset", "/config", "/settings",
        "/model ", "/provider ", "/locale ", "/set key ",
    };
    std::vector<std::string> result;
    for (auto& cmd : kCommands) {
        if (cmd.starts_with(prefix))
            result.push_back(cmd);
    }
    if (result.empty() && prefix.starts_with("/model ")) {
        // Suggest model names from config
        for (auto& m : config_->agent().custom_models) {
            if (m.id.starts_with(prefix.substr(7)))
                result.push_back(m.id);
        }
    }
    return result;
}

// ── Settings overlay ─────────────────────────────────────────────────────
void TuiApp::show_settings() {
    auto& cfg = config_->agent();
    std::vector<std::string> lines;
    lines.push_back(std::string(" ") + term::BOLD + "Settings" + term::RESET);
    lines.push_back("");
    lines.push_back(" Model:    " + term::fg(term::BRIGHT_CYAN) + cfg.default_model + term::RESET);
    lines.push_back(" Provider: " + term::fg(term::BRIGHT_CYAN) + cfg.default_provider + term::RESET);
    lines.push_back(" Locale:   " + term::fg(term::BRIGHT_CYAN) + cfg.locale + term::RESET);
    lines.push_back(" Temperature: " + std::to_string(cfg.temperature));
    lines.push_back("");
    for (auto& [pn, pc] : cfg.providers) {
        std::string key_status = pc.api_key.empty() ? "no key" : "key set";
        lines.push_back(" " + pn + ": " + pc.base_url + " (" + key_status + ")");
    }
    lines.push_back("");
    lines.push_back(" " + term::fg(term::GRAY) + "Use /model, /provider, /set key to change" + term::RESET);
    lines.push_back(" " + term::fg(term::GRAY) + "Press Esc or q to close" + term::RESET);
    engine_.show_overlay(lines);
}

void TuiApp::show_help() {
    std::vector<std::string> lines;
    lines.push_back(std::string(" ") + term::BOLD + "Help" + term::RESET);
    lines.push_back("");
    lines.push_back(std::string(" ") + term::BOLD + "Commands:" + term::RESET);
    lines.push_back("  /help           Show this help");
    lines.push_back("  /exit, /quit    Exit");
    lines.push_back("  /reset          Reset session");
    lines.push_back("  /config         Show configuration");
    lines.push_back("  /settings       Open settings panel");
    lines.push_back("  /model <id>     Set model");
    lines.push_back("  /provider <n>   Set provider");
    lines.push_back("  /locale <code>  Set language (en, zh-CN, ja)");
    lines.push_back("  /set key <p> <k> Set API key for provider");
    lines.push_back("");
    lines.push_back(std::string(" ") + term::BOLD + "Keyboard:" + term::RESET);
    lines.push_back("  Ctrl+P     Settings panel");
    lines.push_back("  Ctrl+H     Help");
    lines.push_back("  Ctrl+C     Cancel generation");
    lines.push_back("  Ctrl+Q     Quit");
    lines.push_back("  PgUp/PgDn  Scroll chat");
    lines.push_back("  Tab        Autocomplete");
    lines.push_back("  Up/Down    Command history");
    lines.push_back("");
    lines.push_back(" " + term::fg(term::GRAY) + "Press Esc or q to close" + term::RESET);
    engine_.show_overlay(lines);
}

void TuiApp::show_config() {
    auto& cfg = config_->agent();
    std::vector<std::string> lines;
    lines.push_back(std::string(" ") + term::BOLD + "Configuration" + term::RESET);
    lines.push_back("  Model:    " + cfg.default_model);
    lines.push_back("  Provider: " + cfg.default_provider);
    lines.push_back("  Locale:   " + cfg.locale);
    lines.push_back("  Temp:     " + std::to_string(cfg.temperature));
    lines.push_back("  Max turns: " + std::to_string(cfg.max_turns));
    lines.push_back("");
    for (auto& [pn, pc] : cfg.providers) {
        std::string ks = pc.api_key.empty() ? "(no key)" : "(key set)";
        lines.push_back("  " + pn + ": " + pc.base_url + " " + ks);
    }
    lines.push_back("");
    lines.push_back("  Tokens in: " + std::to_string(tokens_in_) + "  out: " + std::to_string(tokens_out_));
    lines.push_back("  Tool calls: " + std::to_string(tool_call_count_));
    engine_.show_overlay(lines);
}

// ── Session persistence ──────────────────────────────────────────────────
void TuiApp::save_session() {
    std::string path = config_->config_dir().string() + "/session.json";
    std::ofstream ofs(path);
    if (!ofs) return;
    nlohmann::json j;
    j["chat"] = chat_markdown_.text();
    j["tokens_in"] = tokens_in_;
    j["tokens_out"] = tokens_out_;
    j["tool_calls"] = tool_call_count_;
    // Save message history (last 100 messages)
    nlohmann::json msgs = nlohmann::json::array();
    int n = 0;
    for (auto& msg : session_.messages()) {
        nlohmann::json mj;
        mj["role"] = (int)msg.role;
        mj["content"] = msg.content;
        msgs.push_back(mj);
        if (++n > 100) break;
    }
    j["messages"] = msgs;
    ofs << j.dump(2);
}

void TuiApp::load_session() {
    std::string path = config_->config_dir().string() + "/session.json";
    std::ifstream ifs(path);
    if (!ifs) return;
    try {
        auto j = nlohmann::json::parse(ifs);
        if (j.contains("chat") && j["chat"].is_string()) {
            chat_markdown_.set_text(j["chat"].get<std::string>());
        }
        if (j.contains("tokens_in")) tokens_in_ = j["tokens_in"];
        if (j.contains("tokens_out")) tokens_out_ = j["tokens_out"];
        if (j.contains("tool_calls")) tool_call_count_ = j["tool_calls"];
        if (j.contains("messages") && j["messages"].is_array()) {
            for (auto& mj : j["messages"]) {
                Role r = static_cast<Role>(mj.value("role", 0));
                session_.add_message(Message(r, mj.value("content", "")));
            }
        }
    } catch (...) {}
}

void TuiApp::update_token_display() {
    status_line_.set_tokens(tokens_in_, tokens_out_);
}

} // namespace pi
