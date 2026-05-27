#include "tui.hpp"
#include "term.hpp"

#include <iostream>
#include <algorithm>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>

namespace pi::tui {

static termios original_termios;
static bool termios_saved = false;

static void enable_raw_mode() {
    if (!isatty(STDIN_FILENO)) return;
    tcgetattr(STDIN_FILENO, &original_termios);
    termios_saved = true;
    termios raw = original_termios;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void disable_raw_mode() {
    if (termios_saved) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_termios);
        termios_saved = false;
    }
}

static void get_term_size(int& w, int& h) {
    winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        w = ws.ws_col; h = ws.ws_row;
    } else {
        w = 80; h = 24;
    }
}

static void write_out(std::string_view s) {
    std::cout.write(s.data(), s.size());
    std::cout.flush();
}

TUI::TUI() { get_term_size(width_, height_); }

TUI::~TUI() {
    stop();
    if (render_thread_.joinable()) render_thread_.join();
    disable_raw_mode();
    write_out(term::SHOW_CURSOR);
    write_out(term::EXIT_ALT_SCREEN);
    std::cout.flush();
}

void TUI::run(Container* root) {
    root_ = root;
    running_ = true;

    enable_raw_mode();
    write_out(term::ALT_SCREEN);
    write_out(term::HIDE_CURSOR);

    render_thread_ = std::thread([this] { render_loop(); });

    // Read input in main thread
    while (running_) {
        char buf[32];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) continue;
        std::string_view data(buf, n);

        if (root_) root_->handle_input(data);

        // Ctrl+Q or Ctrl+C to quit
        if (n == 1 && (buf[0] == 17 || buf[0] == 3)) { // Ctrl+Q or Ctrl+C
            running_ = false;
            break;
        }
        request_render();
    }

    if (render_thread_.joinable()) render_thread_.join();
}

void TUI::request_render(bool immediate) {
    {
        std::lock_guard lk(mtx_);
        render_pending_ = true;
        if (immediate) render_immediate_ = true;
    }
    cv_.notify_one();
}

void TUI::render_loop() {
    last_render_ = std::chrono::steady_clock::now();
    while (running_) {
        std::unique_lock lk(mtx_);
        cv_.wait_for(lk, rate_limit_, [this] { return render_pending_ || !running_; });
        if (!running_) break;

        bool imm = render_immediate_;
        render_pending_ = false;
        render_immediate_ = false;
        lk.unlock();

        auto now = std::chrono::steady_clock::now();
        if (!imm && (now - last_render_) < rate_limit_)
            std::this_thread::sleep_for(rate_limit_ - (now - last_render_));

        do_render();
        last_render_ = std::chrono::steady_clock::now();
    }
}

void TUI::do_render() {
    if (!root_) return;
    get_term_size(width_, height_);
    auto lines = root_->render(width_);
    apply_diff(lines);
    prev_lines_ = lines;
    prev_width_ = width_;
}

void TUI::apply_diff(const std::vector<std::string>& new_lines) {
    if (prev_lines_.empty() || prev_width_ != width_) {
        write_out(term::CURSOR_HOME);
        write_out(term::CLEAR_SCREEN);
        for (size_t i = 0; i < new_lines.size() && i < (size_t)height_; ++i) {
            if (i > 0) write_out("\n");
            write_out(new_lines[i]);
        }
        for (size_t i = new_lines.size(); i < (size_t)height_; ++i) {
            write_out("\n");
            write_out(term::CLEAR_EL);
        }
        return;
    }

    int first_diff = -1, last_diff = -1;
    size_t min_lines = std::min(prev_lines_.size(), new_lines.size());

    for (size_t i = 0; i < min_lines; ++i) {
        if (prev_lines_[i] != new_lines[i]) {
            if (first_diff == -1) first_diff = i;
            last_diff = i;
        }
    }
    if (new_lines.size() > prev_lines_.size()) {
        if (first_diff == -1) first_diff = prev_lines_.size();
        last_diff = new_lines.size() - 1;
    }
    if (new_lines.size() < prev_lines_.size()) {
        if (first_diff == -1) first_diff = new_lines.size();
        last_diff = prev_lines_.size() - 1;
    }
    if (first_diff == -1) return;

    first_diff = std::min(first_diff, height_ - 1);
    last_diff = std::min(last_diff, height_ - 1);

    for (int i = first_diff; i <= last_diff; ++i) {
        write_out(term::move_to(i + 1, 1));
        if (i < (int)new_lines.size())
            write_out(new_lines[i]);
        else
            write_out(term::CLEAR_EL);
    }
}

} // namespace pi::tui
