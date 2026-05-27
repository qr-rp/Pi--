#pragma once

#include "core/config.hpp"
#include "core/types.hpp"
#include "agent/agent.hpp"
#include "session/session.hpp"
#include "provider/registry.hpp"
#include "tools/registry.hpp"

#include "tui/component.hpp"
#include "tui/text.hpp"
#include "tui/editor.hpp"
#include "tui/markdown.hpp"
#include "tui/status_line.hpp"
#include "tui/tui.hpp"

#include <string>
#include <atomic>
#include <thread>
#include <fstream>

namespace pi {

class TuiApp {
public:
    TuiApp(Config* config, ProviderRegistry* providers, ToolRegistry* tools);
    ~TuiApp();
    int run();

private:
    Config* config_;
    ProviderRegistry* providers_;
    ToolRegistry* tools_;

    Agent agent_;
    Session session_;

    tui::TUI engine_;
    tui::Container root_;

    tui::Container chat_container_;
    tui::Markdown chat_markdown_;
    tui::Text status_text_;
    tui::Editor editor_;
    tui::StatusLine status_line_;

    std::string streaming_buffer_;
    bool is_streaming_ = false;
    std::atomic<bool> agent_busy_{false};
    std::thread agent_thread_;
    int tool_call_count_ = 0;
    std::string current_tool_name_;
    int64_t tokens_in_ = 0, tokens_out_ = 0;

    void on_chunk(std::string_view chunk);
    void on_tool_start(std::string_view name);
    void on_tool_result(const ToolResult& result);
    void on_error(std::string_view err);
    void on_done();
    void start_agent(const std::string& input);
    void append_chat(std::string_view text);

    void on_editor_submit(const std::string& text);

    // Settings overlay
    void show_settings();
    void show_help();
    void show_config();

    // Session persistence
    void save_session();
    void load_session();

    // Autocomplete
    std::vector<std::string> autocomplete(std::string_view prefix);

    // Token tracking
    void update_token_display();
};

} // namespace pi
