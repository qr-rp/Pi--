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

// ── Input parser ─────────────────────────────────────────────────────────
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
    if ((unsigned char)buf[0] < 0x20 || buf[0] == 0x7F) {
        ev.ctrl = true;
        if (buf[0] == 0x7F) ev.key = Key::Backspace;
        else if (buf[0] == 0x09) ev.key = Key::Tab;
        else if (buf[0] == 0x0D) ev.key = Key::Enter;
        else ev.key = static_cast<Key>(buf[0]);
        return ev;
    }
    // Printable (ASCII or multi-byte UTF-8)
    ev.ch = buf[0];
    ev.text = std::string(buf, len);
    return ev;
}

// ── TUI ───────────────────────────────────────────────────────────────────
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

        if (ev.key == Key::PageUp) { set_scroll(scroll_offset_ + std::max(1, height_ / 3)); continue; }
        if (ev.key == Key::PageDown) {
            if (scroll_offset_ > 0) set_scroll(scroll_offset_ - std::max(1, height_ / 3));
            else scroll_to_bottom();
            continue;
        }
        if (ev.key == Key::CtrlC || ev.key == Key::CtrlQ) break;
        if (ev.key == Key::Escape) { hide_overlay(); continue; }

        // Overlay input
        if (!overlay_lines_.empty()) {
            if (ev.key == Key::Up && overlay_cursor_ >= 0) { set_overlay_cursor(std::max(0, overlay_cursor_ - 1)); continue; }
            if (ev.key == Key::Down && overlay_cursor_ >= 0) { set_overlay_cursor(std::min((int)overlay_lines_.size()-1, overlay_cursor_ + 1)); continue; }
            if (ev.key == Key::Enter && on_overlay_act && overlay_cursor_ >= 0) { on_overlay_act(overlay_cursor_, overlay_cursor_); continue; }
            continue;
        }

        // Dispatch to root + key handler
        bool handled = false;
        if (on_key) handled = on_key(ev);
        if (!handled && root_) root_->handle_input(ev);
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

void TUI::show_overlay(const std::vector<std::string>& lines, int cursor) {
    overlay_lines_ = lines; overlay_cursor_ = cursor; request_render(true);
}

void TUI::hide_overlay() {
    overlay_lines_.clear(); overlay_cursor_ = -1; request_render(true);
}

void TUI::render_loop() {
    last_render_ = std::chrono::steady_clock::now();
    while (running_) {
        std::unique_lock lk(mtx_);
        cv_.wait_for(lk, rate_limit_, [this] { return render_pending_ || !running_; });
        if (!running_) break;
        bool imm = render_immediate_; render_pending_ = render_immediate_ = false;
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

    int vp_lines = height_ - 2;
    int scroll_max = std::max(0, content_height_ - vp_lines);
    if (auto_scroll_) scroll_offset_ = scroll_max;
    else scroll_offset_ = std::clamp(scroll_offset_, 0, scroll_max);

    std::vector<std::string> vp;
    int start = scroll_offset_;
    int end = std::min((int)lines.size(), start + vp_lines);
    for (int i = start; i < end; ++i) vp.push_back(lines[i]);
    while ((int)vp.size() < vp_lines) vp.push_back(std::string(width_, ' '));

    if (scroll_offset_ > 0 && vp_lines > 0)
        vp[0] = term::scroll_indicator(content_height_ - scroll_offset_, content_height_, width_);

    // Overlay
    if (!overlay_lines_.empty()) {
        int ow = std::min(width_ - 4, 60);
        int oh = (int)overlay_lines_.size();
        int ox = (width_ - ow) / 2;
        int oy = (vp_lines - oh) / 2;
        if (oy < 0) oy = 0;

        auto draw_line = [&](int row, const std::string& s) {
            if (row < 0 || row >= (int)vp.size()) return;
            std::string l = term::fg(theme_.accent) + term::box_vert() + term::RESET
                            + " " + s + std::string(std::max(0, ow - (int)s.size()), ' ')
                            + " " + term::fg(theme_.accent) + term::box_vert() + term::RESET;
            if (ox + (int)l.size() <= (int)vp[row].size())
                vp[row].replace(ox, l.size(), l);
        };

        if (oy < (int)vp.size()) {
            std::string t = term::fg(theme_.accent) + term::box_tl() + term::box_horiz(ow) + term::box_tr() + term::RESET;
            if (ox + (int)t.size() <= (int)vp[oy].size()) vp[oy].replace(ox, t.size(), t);
        }
        for (int i = 0; i < oh; ++i) {
            std::string content = overlay_lines_[i];
            if (i == overlay_cursor_ && overlay_cursor_ >= 0)
                content = term::REVERSE + content + term::RESET;
            draw_line(oy + 1 + i, content);
        }
        int br = oy + 1 + oh;
        if (br < (int)vp.size()) {
            std::string b = term::fg(theme_.accent) + term::box_bl() + term::box_horiz(ow) + term::box_br() + term::RESET;
            if (ox + (int)b.size() <= (int)vp[br].size()) vp[br].replace(ox, b.size(), b);
        }
    }

    apply_diff(vp);
    prev_lines_ = vp;
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

    int fd = -1, ld = -1;
    size_t n = std::min(prev_lines_.size(), new_lines.size());
    for (size_t i = 0; i < n; ++i)
        if (prev_lines_[i] != new_lines[i]) { if (fd == -1) fd = i; ld = i; }
    if (new_lines.size() > prev_lines_.size()) { if (fd == -1) fd = prev_lines_.size(); ld = new_lines.size() - 1; }
    if (new_lines.size() < prev_lines_.size()) { if (fd == -1) fd = new_lines.size(); ld = prev_lines_.size() - 1; }
    if (fd == -1) return;

    fd = std::min(fd, (int)new_lines.size()-1);
    ld = std::min(ld, (int)new_lines.size()-1);
    for (int i = fd; i <= ld; ++i) {
        write_out(term::move_to(i+1, 1));
        write_out(i < (int)new_lines.size() ? new_lines[i] : term::CLEAR_EL);
    }
}

} // namespace pi::tui
