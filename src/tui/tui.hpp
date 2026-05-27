#pragma once

#include "component.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <functional>

namespace pi::tui {

// ── Theme ─────────────────────────────────────────────────────────────────
struct Theme {
    uint8_t bg            = 0;    // black
    uint8_t fg            = 15;   // bright white
    uint8_t accent        = 14;   // bright cyan
    uint8_t accent_dim    = 6;    // cyan
    uint8_t success       = 10;   // bright green
    uint8_t error         = 9;    // bright red
    uint8_t warning       = 11;   // bright yellow
    uint8_t muted         = 8;    // gray
    uint8_t user_msg      = 10;   // green
    uint8_t assistant_msg = 14;   // cyan
    uint8_t tool_msg      = 11;   // yellow
    uint8_t code_bg       = 236;  // dark gray
    uint8_t code_fg       = 157;  // light green
    uint8_t line_num      = 242;  // medium gray
    uint8_t border        = 238;  // darker gray
    uint8_t highlight     = 4;    // blue
};

// ── Key codes ─────────────────────────────────────────────────────────────
enum class Key : int {
    None = 0,
    Up = 0x101, Down, Left, Right,
    PageUp, PageDown, Home, End,
    Delete, Backspace = 0x7F,
    Tab = 0x9, Enter = 0xD, Escape = 0x1B,
    CtrlA = 1, CtrlB, CtrlC, CtrlD, CtrlE, CtrlF, CtrlG,
    CtrlH = 8, CtrlI = 9, CtrlJ = 10, CtrlK = 11, CtrlL = 12,
    CtrlM = 13, CtrlN = 14, CtrlO = 15, CtrlP = 16, CtrlQ = 17,
    CtrlR = 18, CtrlS = 19, CtrlT = 20, CtrlU = 21, CtrlV = 22,
    CtrlW = 23, CtrlX = 24, CtrlY = 25, CtrlZ = 26,
    AltUp = 0x201, AltDown, AltLeft, AltRight,
};

// ── Input event ───────────────────────────────────────────────────────────
struct InputEvent {
    Key key = Key::None;
    char ch = 0;
    bool alt = false;
    bool ctrl = false;
};

// ── Parse input bytes into events ─────────────────────────────────────────
InputEvent parse_input(const char* buf, size_t len);

// ── Rate-limited differential rendering TUI ──────────────────────────────
class TUI {
public:
    TUI();
    ~TUI();

    void run(Container* root);
    void request_render(bool immediate = false);

    // Keyboard event dispatch — return true if handled
    std::function<bool(InputEvent)> on_key;

    void stop() { running_ = false; }
    int width() const { return width_; }
    int height() const { return height_; }
    Theme& theme() { return theme_; }

    // Scroll control
    int scroll_offset() const { return scroll_offset_; }
    void set_scroll(int offset) { scroll_offset_ = offset; request_render(); }
    int content_height() const { return content_height_; }

    // Show/hide a modal overlay
    void show_overlay(const std::vector<std::string>& lines);
    void hide_overlay();
    bool overlay_showing() const { return !overlay_lines_.empty(); }
    std::vector<std::string> overlay_lines_;

private:
    Container* root_ = nullptr;
    int width_ = 80, height_ = 24;
    bool running_ = false;
    Theme theme_;
    int scroll_offset_ = 0;
    int content_height_ = 0;

    std::vector<std::string> prev_lines_;
    int prev_width_ = 0;

    std::mutex mtx_;
    std::condition_variable cv_;
    bool render_pending_ = false;
    bool render_immediate_ = false;
    std::thread render_thread_;
    std::chrono::steady_clock::time_point last_render_;
    std::chrono::milliseconds rate_limit_{16};

    void render_loop();
    void do_render();
    void apply_diff(const std::vector<std::string>& new_lines);
};

} // namespace pi::tui
