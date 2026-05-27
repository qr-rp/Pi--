#pragma once

#include "component.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace pi::tui {

class TUI {
public:
    TUI();
    ~TUI();
    void run(Container* root);
    void request_render(bool immediate = false);
    void stop() { running_ = false; request_render(true); }
    int width() const { return width_; }

private:
    Container* root_ = nullptr;
    int width_ = 80;
    int height_ = 24;
    bool running_ = false;

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
    void read_input();
};

} // namespace pi::tui
