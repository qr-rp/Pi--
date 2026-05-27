#pragma once
#include "component.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

namespace pi::tui {

struct Theme {
    uint8_t bg=0, fg=15, accent=14, accent_dim=6;
    uint8_t success=10, error=9, warning=11, muted=8;
    uint8_t user_msg=10, assistant_msg=14, tool_msg=11;
    uint8_t code_bg=236, code_fg=157, line_num=242, border=238;
};

enum class Key : int {
    None=0, Up=257, Down, Left, Right,
    PageUp, PageDown, Home, End,
    Delete, Backspace=127,
    Tab=9, Enter=13, Escape=27, ShiftTab,
    CtrlA=1,CtrlB,CtrlC,CtrlD,CtrlE,CtrlF,CtrlG,
    CtrlH=8,CtrlI=9,CtrlJ=10,CtrlK=11,CtrlL=12,
    CtrlM=13,CtrlN=14,CtrlO=15,CtrlP=16,CtrlQ=17,
    CtrlR=18,CtrlS=19,CtrlT=20,CtrlU=21,CtrlV=22,
    CtrlW=23,CtrlX=24,CtrlY=25,CtrlZ=26,
};

struct InputEvent { Key key=Key::None; char ch=0; bool alt=false; bool ctrl=false; std::string text; };
InputEvent parse_input(const char* buf, size_t len);

class TUI {
public:
    TUI(); ~TUI();
    void run(Container* root);
    void request_render(bool immediate=false);
    void stop() { running_ = false; }
    int width() const { return width_; }
    int height() const { return height_; }
    Theme& theme() { return theme_; }
    int scroll_offset() const { return scroll_offset_; }
    void set_scroll(int offset);
    void scroll_to_bottom();
    int content_height() const { return content_height_; }
    bool auto_scroll() const { return auto_scroll_; }
    void show_overlay(const std::vector<std::string>& lines, int cursor=-1);
    void hide_overlay();
    bool overlay_showing() const { return !overlay_lines_.empty(); }
    int overlay_cursor() const { return overlay_cursor_; }
    void set_overlay_cursor(int c) { overlay_cursor_ = c; if(c>=0) request_render(); }
    std::function<void(int selected, int cursor)> on_overlay_act;
    std::vector<std::string> overlay_lines_;
    std::function<bool(InputEvent)> on_key;
private:
    Container* root_=nullptr; int width_=80, height_=24; bool running_=false;
    Theme theme_; int scroll_offset_=0, content_height_=0; bool auto_scroll_=true;
    int overlay_cursor_=-1;
    std::vector<std::string> prev_lines_; int prev_width_=0;
    std::mutex mtx_; std::condition_variable cv_;
    bool render_pending_=false, render_immediate_=false;
    std::thread render_thread_;
    std::chrono::steady_clock::time_point last_render_;
    std::chrono::milliseconds rate_limit_{16};
    void render_loop(); void do_render(); void apply_diff(const std::vector<std::string>&);
};

} // namespace pi::tui
