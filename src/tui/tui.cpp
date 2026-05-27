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
    raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
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
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) { w = ws.ws_col; h = ws.ws_row; }
    else { w = 80; h = 24; }
}

static void write_out(std::string_view s) {
    std::cout.write(s.data(), s.size());
    std::cout.flush();
}

InputEvent parse_input(const char* buf, size_t len) {
    InputEvent ev;
    if (len == 0) return ev;
    if (buf[0] == '\x1b') {
        if (len == 1) { ev.key = Key::Escape; return ev; }
        if (buf[1] == '[' && len >= 3) {
            switch (buf[2]) {
                case 'A': ev.key = Key::Up; break;
                case 'B': ev.key = Key::Down; break;
                case 'C': ev.key = Key::Right; break;
                case 'D': ev.key = Key::Left; break;
                case 'H': ev.key = Key::Home; break;
                case 'F': ev.key = Key::End; break;
                case '5': if (len > 3 && buf[3] == '~') ev.key = Key::PageUp; break;
                case '6': if (len > 3 && buf[3] == '~') ev.key = Key::PageDown; break;
                case '3': if (len > 3 && buf[3] == '~') ev.key = Key::Delete; break;
            }
            if (ev.key != Key::None) return ev;
        }
        if (len >= 2) { ev.alt = true; ev.ch = buf[1]; }
        return ev;
    }
    if (buf[0] < 0x20) {
        ev.ctrl = true;
        if (buf[0] == 0x7F) ev.key = Key::Backspace;
        else if (buf[0] == 0x09) ev.key = Key::Tab;
        else if (buf[0] == 0x0D) ev.key = Key::Enter;
        else ev.key = static_cast<Key>(buf[0]);
        return ev;
    }
    ev.ch = buf[0];
    return ev;
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

void TUI::set_scroll(int offset) {
    auto_scroll_ = false;
    scroll_offset_ = std::clamp(offset, 0, std::max(0, content_height_ - height_ + 2));
    request_render();
}

void TUI::scroll_to_bottom() {
    auto_scroll_ = true;
    scroll_offset_ = 0;
    request_render();
}

void TUI::run(Container* root) {
    root_ = root;
    running_ = true;
    enable_raw_mode();
    write_out("\x1b[?2026h");
    write_out(term::ALT_SCREEN);
    write_out(term::HIDE_CURSOR);

    render_thread_ = std::thread([this] { render_loop(); });

    while (running_) {
        char buf[32];
        ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
        if (n <= 0) continue;

        InputEvent ev = parse_input(buf, n);

        // Global keys
        if (ev.key == Key::PageUp) {
            set_scroll(scroll_offset_ + std::max(1, height_ / 3));
            continue;
        }
        if (ev.key == Key::PageDown) {
            if (scroll_offset_ > 0)
                set_scroll(scroll_offset_ - std::max(1, height_ / 3));
            else
                scroll_to_bottom();
            continue;
        }
        if (ev.key == Key::CtrlC || ev.key == Key::CtrlQ) break;
        if (ev.key == Key::Escape) { hide_overlay(); continue; }

        // Overlay input
        if (!overlay_lines_.empty()) continue;

        // Dispatch to root
        if (root_)
            root_->handle_input(ev);

        request_render();
    }

    running_ = false;
    if (render_thread_.joinable()) render_thread_.join();
    write_out("\x1b[?2026l");
}

void TUI::request_render(bool immediate) {
    { std::lock_guard lk(mtx_); render_pending_ = true; if (immediate) render_immediate_ = true; }
    cv_.notify_one();
}

void TUI::show_overlay(const std::vector<std::string>& lines) {
    overlay_lines_ = lines;
    request_render(true);
}

void TUI::hide_overlay() {
    overlay_lines_.clear();
    request_render(true);
}

void TUI::render_loop() {
    last_render_ = std::chrono::steady_clock::now();
    while (running_) {
        std::unique_lock lk(mtx_);
        cv_.wait_for(lk, rate_limit_, [this] { return render_pending_ || !running_; });
        if (!running_) break;
        bool imm = render_immediate_;
        render_pending_ = render_immediate_ = false;
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
    content_height_ = (int)lines.size();

    int viewport_lines = height_ - 2;
    int scroll_max = std::max(0, content_height_ - viewport_lines);

    // Auto-scroll: when content grows and we're at the bottom, follow
    if (auto_scroll_) scroll_offset_ = scroll_max;
    else scroll_offset_ = std::clamp(scroll_offset_, 0, scroll_max);

    // Build viewport
    std::vector<std::string> viewport;
    int start_line = scroll_offset_;
    int end_line = std::min((int)lines.size(), start_line + viewport_lines);

    for (int i = start_line; i < end_line; ++i)
        viewport.push_back(lines[i]);

    // Pad if short
    while ((int)viewport.size() < viewport_lines)
        viewport.push_back(std::string(width_, ' '));

    // Add scroll indicator if not at bottom
    if (scroll_offset_ > 0 && viewport_lines > 0) {
        viewport[0] = term::scroll_indicator(content_height_ - scroll_offset_, content_height_, width_);
    }

    // Composite overlay
    if (!overlay_lines_.empty()) {
        int ov_w = std::min(width_ - 4, 60);
        int ov_h = (int)overlay_lines_.size();
        int ov_x = (width_ - ov_w) / 2;
        int ov_y = (viewport_lines - ov_h) / 2;
        if (ov_y < 0) ov_y = 0;

        auto ov_line = [&](int row, const std::string& content) {
            if (row < 0 || row >= (int)viewport.size()) return;
            std::string& tgt = viewport[row];
            std::string l = term::fg(theme_.accent) + term::box_vert() + term::RESET
                          + " " + content
                          + std::string(std::max(0, ov_w - (int)content.size()), ' ')
                          + " " + term::fg(theme_.accent) + term::box_vert() + term::RESET;
            if (ov_x + (int)l.size() <= (int)tgt.size())
                tgt.replace(ov_x, l.size(), l);
        };

        // Top border
        if (ov_y < (int)viewport.size()) {
            std::string top = term::fg(theme_.accent) + term::box_tl() + term::box_horiz(ov_w) + term::box_tr() + term::RESET;
            if (ov_x + (int)top.size() <= (int)viewport[ov_y].size())
                viewport[ov_y].replace(ov_x, top.size(), top);
        }

        for (int i = 0; i < ov_h; ++i)
            ov_line(ov_y + 1 + i, overlay_lines_[i]);

        // Bottom border
        int bot_row = ov_y + 1 + ov_h;
        if (bot_row < (int)viewport.size()) {
            std::string bot = term::fg(theme_.accent) + term::box_bl() + term::box_horiz(ov_w) + term::box_br() + term::RESET;
            if (ov_x + (int)bot.size() <= (int)viewport[bot_row].size())
                viewport[bot_row].replace(ov_x, bot.size(), bot);
        }
    }

    apply_diff(viewport);
    prev_lines_ = viewport;
    prev_width_ = width_;
}

void TUI::apply_diff(const std::vector<std::string>& new_lines) {
    if (prev_lines_.empty() || prev_width_ != width_) {
        write_out(term::cursor_home());
        write_out(term::CLEAR_SCREEN);
        for (size_t i = 0; i < new_lines.size(); ++i) {
            if (i > 0) write_out("\n");
            write_out(new_lines[i]);
        }
        return;
    }

    int first_diff = -1, last_diff = -1;
    size_t n = std::min(prev_lines_.size(), new_lines.size());

    for (size_t i = 0; i < n; ++i) {
        if (prev_lines_[i] != new_lines[i]) {
            if (first_diff == -1) first_diff = i;
            last_diff = i;
        }
    }
    if (new_lines.size() > prev_lines_.size()) {
        first_diff = first_diff == -1 ? prev_lines_.size() : first_diff;
        last_diff = new_lines.size() - 1;
    }
    if (new_lines.size() < prev_lines_.size()) {
        first_diff = first_diff == -1 ? new_lines.size() : first_diff;
        last_diff = prev_lines_.size() - 1;
    }
    if (first_diff == -1) return;

    first_diff = std::min(first_diff, (int)new_lines.size() - 1);
    last_diff = std::min(last_diff, (int)new_lines.size() - 1);

    for (int i = first_diff; i <= last_diff; ++i) {
        write_out(term::move_to(i + 1, 1));
        write_out(i < (int)new_lines.size() ? new_lines[i] : term::CLEAR_EL);
    }
}

} // namespace pi::tui
