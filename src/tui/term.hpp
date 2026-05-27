#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <format>
#include <algorithm>

// ── ANSI escape sequence helpers ──────────────────────────────────────────
namespace pi::term {

// SGR codes
inline constexpr auto RESET     = "\x1b[0m";
inline constexpr auto BOLD      = "\x1b[1m";
inline constexpr auto DIM       = "\x1b[2m";
inline constexpr auto ITALIC    = "\x1b[3m";
inline constexpr auto UNDERLINE = "\x1b[4m";
inline constexpr auto REVERSE   = "\x1b[7m";

inline std::string fg(uint8_t code) { return std::format("\x1b[38;5;{}m", code); }
inline std::string bg(uint8_t code) { return std::format("\x1b[48;5;{}m", code); }

inline constexpr uint8_t BLACK        = 0;
inline constexpr uint8_t RED          = 1;
inline constexpr uint8_t GREEN        = 2;
inline constexpr uint8_t YELLOW       = 3;
inline constexpr uint8_t BLUE         = 4;
inline constexpr uint8_t MAGENTA      = 5;
inline constexpr uint8_t CYAN         = 6;
inline constexpr uint8_t WHITE        = 7;
inline constexpr uint8_t GRAY         = 8;
inline constexpr uint8_t BRIGHT_RED   = 9;
inline constexpr uint8_t BRIGHT_GREEN = 10;
inline constexpr uint8_t BRIGHT_YELLOW= 11;
inline constexpr uint8_t BRIGHT_BLUE  = 12;
inline constexpr uint8_t BRIGHT_MAGENTA=13;
inline constexpr uint8_t BRIGHT_CYAN  = 14;
inline constexpr uint8_t BRIGHT_WHITE = 15;

// Cursor
inline std::string cursor_up(int n)    { return n > 0 ? std::format("\x1b[{}A", n) : ""; }
inline std::string cursor_down(int n)  { return n > 0 ? std::format("\x1b[{}B", n) : ""; }
inline std::string cursor_right(int n) { return n > 0 ? std::format("\x1b[{}C", n) : ""; }
inline std::string cursor_left(int n)  { return n > 0 ? std::format("\x1b[{}D", n) : ""; }
inline std::string cursor_home()       { return "\x1b[H"; }
inline std::string cursor_col(int n)   { return std::format("\x1b[{}G", n); }
inline std::string move_to(int row, int col) { return std::format("\x1b[{};{}H", row, col); }

// Screen
inline constexpr auto CLEAR_SCREEN     = "\x1b[2J";
inline constexpr auto CLEAR_LINE       = "\x1b[2K";
inline constexpr auto CLEAR_EL         = "\x1b[0K";
inline constexpr auto HIDE_CURSOR      = "\x1b[?25l";
inline constexpr auto SHOW_CURSOR      = "\x1b[?25h";
inline constexpr auto ALT_SCREEN       = "\x1b[?1049h";
inline constexpr auto EXIT_ALT_SCREEN  = "\x1b[?1049l";

inline std::string styled(std::string_view text, std::string_view style) {
    return std::string(style) + std::string(text) + RESET;
}

// ── UTF-8 string repeat ─────────────────────────────────────────────────
// Build a string consisting of a UTF-8 codepoint repeated n times.
// The codepoint is passed as a std::string containing the UTF-8 bytes.
inline std::string utf8_repeat(std::string_view utf8_char, int n) {
    std::string result;
    result.reserve(utf8_char.size() * n);
    for (int i = 0; i < n; ++i) result += utf8_char;
    return result;
}

// Box-drawing character helpers
inline std::string box_horiz(int n)  { return n > 0 ? utf8_repeat("\u2500", n) : ""; }
inline std::string box_vert()        { return "\u2502"; }
inline std::string box_tl()          { return "\u250C"; }
inline std::string box_tr()          { return "\u2510"; }
inline std::string box_bl()          { return "\u2514"; }
inline std::string box_br()          { return "\u2518"; }

// Styled text in a fixed-width field
int visible_width(std::string_view s);
std::string strip_ansi(std::string_view s);
std::string scroll_indicator(int shown, int total, int width);

} // namespace pi::term
