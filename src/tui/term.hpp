#include <cstdint>
#include <format>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <memory>

// ── ANSI escape sequence helpers ──────────────────────────────────────────
namespace pi::term {

// SGR (Select Graphic Rendition) codes
inline constexpr auto RESET     = "\x1b[0m";
inline constexpr auto BOLD      = "\x1b[1m";
inline constexpr auto DIM       = "\x1b[2m";
inline constexpr auto ITALIC    = "\x1b[3m";
inline constexpr auto UNDERLINE = "\x1b[4m";
inline constexpr auto REVERSE   = "\x1b[7m";

// Foreground colors (8-bit)
inline std::string fg(uint8_t code) { return std::format("\x1b[38;5;{}m", code); }
inline std::string bg(uint8_t code) { return std::format("\x1b[48;5;{}m", code); }

// Named 256-color indices (approximate)
inline constexpr uint8_t BLACK     = 0;
inline constexpr uint8_t RED       = 1;
inline constexpr uint8_t GREEN     = 2;
inline constexpr uint8_t YELLOW    = 3;
inline constexpr uint8_t BLUE      = 4;
inline constexpr uint8_t MAGENTA   = 5;
inline constexpr uint8_t CYAN      = 6;
inline constexpr uint8_t WHITE     = 7;
inline constexpr uint8_t GRAY      = 8;
inline constexpr uint8_t BRIGHT_RED    = 9;
inline constexpr uint8_t BRIGHT_GREEN  = 10;
inline constexpr uint8_t BRIGHT_YELLOW = 11;
inline constexpr uint8_t BRIGHT_BLUE   = 12;
inline constexpr uint8_t BRIGHT_MAGENTA= 13;
inline constexpr uint8_t BRIGHT_CYAN   = 14;
inline constexpr uint8_t BRIGHT_WHITE  = 15;

// Cursor movement
inline std::string cursor_up(int n)    { return n > 0 ? std::format("\x1b[{}A", n) : ""; }
inline std::string cursor_down(int n)  { return n > 0 ? std::format("\x1b[{}B", n) : ""; }
inline std::string cursor_right(int n) { return n > 0 ? std::format("\x1b[{}C", n) : ""; }
inline std::string cursor_left(int n)  { return n > 0 ? std::format("\x1b[{}D", n) : ""; }
inline std::string cursor_col(int n)   { return std::format("\x1b[{}G", n); }
inline std::string cursor_pos(int row, int col) { return std::format("\x1b[{};{}H", row, col); }

// Screen
inline constexpr auto CLEAR_SCREEN      = "\x1b[2J";
inline constexpr auto CLEAR_LINE        = "\x1b[2K";
inline constexpr auto CLEAR_EL          = "\x1b[0K";
inline constexpr auto CURSOR_HOME       = "\x1b[H";
inline constexpr auto CURSOR_SAVE       = "\x1b[s";
inline constexpr auto CURSOR_RESTORE    = "\x1b[u";
inline constexpr auto HIDE_CURSOR       = "\x1b[?25l";
inline constexpr auto SHOW_CURSOR       = "\x1b[?25h";
inline constexpr auto ALT_SCREEN        = "\x1b[?1049h";
inline constexpr auto EXIT_ALT_SCREEN   = "\x1b[?1049l";

// Combined
inline std::string move_to(int row, int col) {
    return std::format("\x1b[{};{}H", row, col);
}

// Wrap an SGR sequence
inline std::string styled(std::string_view text, std::string_view style) {
    return std::string(style) + std::string(text) + RESET;
}

// Visible width of a string (strips ANSI)
int visible_width(std::string_view s);

// Strip ANSI escape codes
std::string strip_ansi(std::string_view s);

} // namespace pi::term
