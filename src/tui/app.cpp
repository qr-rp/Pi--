#include "tui/app.hpp"
#include "tui/term.hpp"
#include <iostream>
#include <sstream>
#include <fstream>

namespace pi {

TuiApp::TuiApp(Config* config, ProviderRegistry* providers, ToolRegistry* tools)
    : config_(config), providers_(providers), tools_(tools)
    , agent_(config, providers, tools)
    , chat_markdown_("", 1, 1), status_text_(" Ready")
{
    session_.set_system_prompt(
        "You are pi-coding-agent, a coding agent with tools: read/write/edit/bash/search.");

    editor_.on_submit = [this](const std::string& t) { on_editor_submit(t); };
    editor_.on_tab = [this](std::string_view p) { return autocomplete(p); };

    // ── Global keybindings (matching omp exactly) ─────────────────────
    engine_.on_key = [this](tui::InputEvent ev) -> bool {
        // omp: app.clear = ctrl+c (cancel/interrupt)
        if (ev.key == tui::Key::CtrlC) {
            if (agent_busy_) { agent_.cancel(); agent_busy_ = false; append_chat("[Cancelled]"); }
            else { /* clear screen - terminal cleared by TUI */ }
            return true;
        }
        // omp: app.exit = ctrl+d
        if (ev.key == tui::Key::CtrlD) { save_session(); engine_.stop(); return true; }
        // omp: app.suspend = ctrl+z
        if (ev.key == tui::Key::CtrlZ && !engine_.overlay_showing()) {
            if (chat_markdown_.tool_call_count() > 0) {
                chat_markdown_.toggle_fold(chat_markdown_.active_fold());
            }
            return true;
        }
        // omp: app.interrupt = escape (when not in overlay)
        if (ev.key == tui::Key::Escape && !engine_.overlay_showing()) {
            if (agent_busy_) { agent_.cancel(); agent_busy_ = false; append_chat("[Cancelled]"); return true; }
            return false;
        }
        // omp: app.model.cycleForward = ctrl+p
        if (ev.key == tui::Key::CtrlP && !engine_.overlay_showing()) {
            static const std::vector<std::string> ms = {"gpt-4o","gpt-4o-mini","deepseek-chat","deepseek-reasoner","claude-sonnet-4-20250514","gemini-2.5-pro"};
            auto it = std::find(ms.begin(), ms.end(), config_->agent().default_model);
            int idx = it != ms.end() ? (it - ms.begin() + 1) % ms.size() : 0;
            config_->agent().default_model = ms[idx]; (void)config_->save();
            status_line_.set_model(ms[idx]); append_chat("Model: " + ms[idx]); return true;
        }
        // omp: app.model.select = ctrl+l
        if (ev.key == tui::Key::CtrlL && !engine_.overlay_showing()) {
            show_settings(); return true;
        }
        // omp: app.thinking.toggle = ctrl+t  (stub)
        if (ev.key == tui::Key::CtrlT && !engine_.overlay_showing()) { return true; }
        // omp: app.editor.external = ctrl+g (stub)
        if (ev.key == tui::Key::CtrlG && !engine_.overlay_showing()) { return true; }
        // omp: app.tools.expand = ctrl+o (stub)
        if (ev.key == tui::Key::CtrlO && !engine_.overlay_showing()) { return true; }
        // omp: app.history.search = ctrl+r (stub)
        if (ev.key == tui::Key::CtrlR && !engine_.overlay_showing()) { return true; }
        // omp: app.thinking.cycle = shift+tab
        if (ev.key == tui::Key::ShiftTab && !engine_.overlay_showing()) { return true; }
        // omp: app.message.followUp = ctrl+enter (pass through to editor)
        // omp: app.message.dequeue = alt+up (pass through to editor)
        return false;
    };

    // ── Layout (matching omp structure) ─────────────────────────────
    //   chat → editor (with integrated status border) → status_line (bottom bar)
    chat_container_.add(&chat_markdown_);
    root_.add(&chat_container_);
    root_.add(&editor_);
    root_.add(&status_line_);

    editor_.focus(true);

    auto& cfg = config_->agent();
    editor_.set_caption(cfg.default_model + " | " + cfg.default_provider);
    status_line_.set_model(cfg.default_model);
    status_line_.set_provider(cfg.default_provider);
    status_line_.set_locale(cfg.locale);
    status_line_.set_hint("Ctrl+P:Next model  Ctrl+L:Select model  Ctrl+D:Quit");

    load_session();
    if (chat_markdown_.text().empty()) {
        append_chat("## pi-coding-agent");
        append_chat("C++23 coding agent with LLM support.");
        append_chat("  - Type a message at the prompt and press Enter");
        append_chat("  - Ctrl+P: Next model    Ctrl+L: Model selector");
        append_chat("  - Ctrl+C: Cancel        Ctrl+D: Quit");
        append_chat("  - Ctrl+Z: Fold tool calls");
    }
}

TuiApp::~TuiApp() { if (agent_thread_.joinable()) agent_thread_.join(); save_session(); }
int TuiApp::run() { engine_.run(&root_); return 0; }

void TuiApp::append_chat(std::string_view text) {
    std::string ex = chat_markdown_.text();
    if (!ex.empty()) ex += '\n';
    ex += text;
    chat_markdown_.set_text(ex);
    engine_.request_render();
}

void TuiApp::on_editor_submit(const std::string& text) {
    if (agent_busy_) return;
    if (text == "/exit" || text == "/quit") { save_session(); engine_.stop(); return; }
    if (text == "/help") { show_help(); return; }
    if (text == "/reset") { session_.reset(); chat_markdown_.set_text(""); append_chat("Session reset."); return; }
    if (text == "/config") { show_config(); return; }
    if (text == "/settings") { show_settings(); return; }
    if (text.starts_with("/model ")) {
        config_->agent().default_model = text.substr(7); (void)config_->save();
        status_line_.set_model(config_->agent().default_model);
        append_chat("Model set to: " + text.substr(7)); return;
    }
    if (text.starts_with("/provider ")) {
        std::string p = text.substr(10);
        if (config_->find_provider(p)) { config_->agent().default_provider = p; (void)config_->save(); status_line_.set_provider(p); append_chat("Provider: " + p); }
        else append_chat("Unknown: " + p);
        return;
    }
    if (text.starts_with("/locale ")) {
        config_->agent().locale = text.substr(8); (void)config_->save();
        status_line_.set_locale(config_->agent().locale); append_chat("Locale: " + text.substr(8)); return;
    }
    if (text.starts_with("/set key ")) {
        auto r = text.substr(9); auto sp = r.find(' ');
        if (sp != std::string::npos) {
            auto pn = r.substr(0, sp), key = r.substr(sp+1);
            if (auto* pc = config_->find_provider(pn)) { pc->api_key = key; (void)config_->save(); append_chat("Key saved for " + pn); }
            else { ProviderConfig npc; npc.api_key = key; npc.base_url = "https://api."+pn+".com"; npc.api_type = "openai-completions"; config_->agent().providers[pn] = npc; (void)config_->save(); append_chat("Created " + pn); }
        }
        return;
    }
    start_agent(text);
}

void TuiApp::start_agent(const std::string& input) {
    if (agent_busy_) return;
    agent_busy_ = true; is_streaming_ = true; streaming_buffer_.clear(); tool_call_count_ = 0;
    append_chat("**" + input + "**");
    status_text_.set_text(" Working..."); status_line_.set_status(" Working...");
    engine_.request_render();

    agent_thread_ = std::thread([this, input] {
        AgentCallbacks cb{
            .on_chunk = [this](std::string_view c) { on_chunk(c); },
            .on_tool_start = [this](std::string_view n) { on_tool_start(n); },
            .on_tool_result = [this](const ToolResult& r) { on_tool_result(r); },
            .on_error = [this](std::string_view e) { on_error(e); },
            .on_turn_complete = []{},
        };
        auto r = agent_.run_conversation(session_, input, cb);
        if (!r) on_error(r.error().message);
        on_done();
    });
    agent_thread_.detach();
}

void TuiApp::on_chunk(std::string_view chunk) {
    streaming_buffer_ += chunk; tokens_out_ += chunk.size()/4;
    status_line_.set_tokens(tokens_in_, tokens_out_);
    engine_.request_render();
}

void TuiApp::on_tool_start(std::string_view name) {
    if (!streaming_buffer_.empty()) { append_chat(streaming_buffer_); streaming_buffer_.clear(); }
    is_streaming_ = false; current_tool_name_ = name; tool_call_count_++;
    append_chat(std::format("[Tool: {}]", name));
    status_text_.set_text(" Running: " + std::string(name) + "...");
    status_line_.set_tool_count(tool_call_count_);
    engine_.request_render();
}

void TuiApp::on_tool_result(const ToolResult& r) {
    if (!r.success) append_chat("  Error: " + r.error_message);
    tokens_in_ += r.output.size()/4; status_line_.set_tokens(tokens_in_, tokens_out_);
}

void TuiApp::on_error(std::string_view err) { append_chat("[Error] " + std::string(err)); is_streaming_ = false; engine_.request_render(); }
void TuiApp::on_done() {
    if (!streaming_buffer_.empty()) { append_chat(streaming_buffer_); streaming_buffer_.clear(); }
    is_streaming_ = false; agent_busy_ = false;
    status_text_.set_text(" Ready"); status_line_.set_status(" Ready");
    engine_.request_render(); save_session();
}

std::vector<std::string> TuiApp::autocomplete(std::string_view prefix) {
    static const std::vector<std::string> cmds = {"/help","/exit","/quit","/reset","/config","/settings","/model ","/provider ","/locale ","/set key "};
    std::vector<std::string> r;
    for (auto& c : cmds) if (c.starts_with(prefix)) r.push_back(c);
    return r;
}

// ── Overlays ──────────────────────────────────────────────────────────────
void TuiApp::show_settings() {
    auto& cfg = config_->agent();
    static const std::vector<std::string> kModels = {
        "gpt-4o","gpt-4o-mini","deepseek-chat","deepseek-reasoner",
        "claude-sonnet-4-20250514","gemini-2.5-pro"
    };
    std::vector<std::string> providers;
    for (auto& [pn,_] : cfg.providers) providers.push_back(pn);

    std::vector<std::string> lines;
    lines.push_back(std::string(" ") + term::BOLD + "Settings (\u2191\u2193 select, Enter=choose, Esc=close)" + term::RESET);
    lines.push_back("");
    lines.push_back(std::string("  ") + term::BOLD + "Model:" + term::RESET);
    for (auto& m : kModels) {
        std::string mk = (m == cfg.default_model) ? term::fg(term::BRIGHT_CYAN)+term::BOLD+"\u25C9 "+term::RESET : "  ";
        lines.push_back("    " + mk + m);
    }
    lines.push_back("");
    lines.push_back(std::string("  ") + term::BOLD + "Provider:" + term::RESET);
    for (auto& p : providers) {
        std::string mk = (p == cfg.default_provider) ? term::fg(term::BRIGHT_CYAN)+term::BOLD+"\u25C9 "+term::RESET : "  ";
        lines.push_back("    " + mk + p);
    }
    lines.push_back("");
    lines.push_back("  Locale: " + cfg.locale);
    lines.push_back("");
    lines.push_back(std::string(" ") + term::fg(term::GRAY) + "Or use Ctrl+N/M to cycle" + term::RESET);

    int ms = 2, me = ms + (int)kModels.size();
    int ps = me + 1, pe = ps + (int)providers.size();

    engine_.show_overlay(lines, 2);
    engine_.on_overlay_act = [this, providers, ms, me, ps, pe](int sel, int) {
        if (sel >= ms && sel < me) {
            int idx = sel - ms;
            config_->agent().default_model = kModels[idx]; (void)config_->save();
            status_line_.set_model(kModels[idx]); this->engine_.hide_overlay(); append_chat("Model: " + kModels[idx]);
        } else if (sel >= ps && sel < pe) {
            int idx = sel - ps;
            config_->agent().default_provider = providers[idx]; (void)config_->save();
            status_line_.set_provider(providers[idx]); this->engine_.hide_overlay(); append_chat("Provider: " + providers[idx]);
        }
    };
}

void TuiApp::show_help() {
    std::vector<std::string> lines;
    lines.push_back(std::string(" ") + term::BOLD + "Help" + term::RESET);
    lines.push_back("");
    lines.push_back(std::string("  ") + term::BOLD + "Commands:" + term::RESET);
    lines.push_back("    /help, /exit, /quit, /reset, /config, /settings");
    lines.push_back("    /model <id>, /provider <n>, /locale <c>, /set key <p> <k>");
    lines.push_back("");
    lines.push_back(std::string("  ") + term::BOLD + "Keyboard:" + term::RESET);
    lines.push_back("    Ctrl+P  Settings     Ctrl+H  Help       Ctrl+Z  Fold tool call");
    lines.push_back("    Ctrl+N  Next model   Ctrl+M  Next provider");
    lines.push_back("    Ctrl+C  Cancel       Ctrl+Q  Quit");
    lines.push_back("    PgUp/PgDn  Scroll   Tab  Autocomplete   Up/Down  History");
    lines.push_back("    Ctrl+A/E  Line start/end  Ctrl+F/B  Forward/back  Ctrl+K/U  Kill");
    lines.push_back("");
    lines.push_back(" " + term::fg(term::GRAY) + "Press Esc to close" + term::RESET);
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
    for (auto& [pn, pc] : cfg.providers)
        lines.push_back("  " + pn + ": " + pc.base_url + " " + (pc.api_key.empty() ? "(no key)" : "(key set)"));
    lines.push_back("");
    lines.push_back("  Tokens in: " + std::to_string(tokens_in_) + "  out: " + std::to_string(tokens_out_));
    lines.push_back("  Tool calls: " + std::to_string(tool_call_count_));
    engine_.show_overlay(lines);
}

// ── Session persistence ──────────────────────────────────────────────────
void TuiApp::save_session() {
    std::ofstream ofs(config_->config_dir().string() + "/session.json");
    if (!ofs) return;
    nlohmann::json j;
    j["chat"] = chat_markdown_.text();
    j["tokens_in"] = tokens_in_; j["tokens_out"] = tokens_out_; j["tool_calls"] = tool_call_count_;
    nlohmann::json msgs = nlohmann::json::array();
    int n = 0;
    for (auto& msg : session_.messages()) {
        nlohmann::json mj; mj["role"] = (int)msg.role; mj["content"] = msg.content; msgs.push_back(mj);
        if (++n > 100) break;
    }
    j["messages"] = msgs;
    ofs << j.dump(2);
}

void TuiApp::load_session() {
    std::ifstream ifs(config_->config_dir().string() + "/session.json");
    if (!ifs) return;
    try {
        auto j = nlohmann::json::parse(ifs);
        if (j.contains("chat")) chat_markdown_.set_text(j["chat"].get<std::string>());
        if (j.contains("tokens_in")) tokens_in_ = j["tokens_in"];
        if (j.contains("tokens_out")) tokens_out_ = j["tokens_out"];
        if (j.contains("tool_calls")) tool_call_count_ = j["tool_calls"];
        if (j.contains("messages")) {
            for (auto& mj : j["messages"])
                session_.add_message(Message(static_cast<Role>(mj.value("role",0)), mj.value("content","")));
        }
    } catch (...) {}
}

void TuiApp::update_token_display() { status_line_.set_tokens(tokens_in_, tokens_out_); }

} // namespace pi
