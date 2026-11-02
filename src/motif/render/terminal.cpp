#include "motif/render/terminal.h"

#include <fmt/format.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>

#include "motif/render/pager.h"

namespace motif::render {
namespace {

constexpr int kDefaultWidth = 80;
constexpr int kMinWidth = 40;

std::string wrap(const Style& style, std::string_view code, std::string_view text) {
  if (!style.color) return std::string(text);
  return fmt::format("\x1b[{}m{}\x1b[0m", code, text);
}

bool env_set(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr && value[0] != '\0';
}

}  // namespace

Style Style::detect(bool force_no_color, bool force_ascii) {
  Style style;

  // Read the terminal state captured at startup: once output is piped into a pager,
  // stdout is no longer a tty, and colour would switch itself off exactly when the user
  // is looking at a terminal.
  const bool tty = stdout_was_tty();
  const char* term = std::getenv("TERM");
  const bool dumb_terminal = term != nullptr && std::strcmp(term, "dumb") == 0;

  // NO_COLOR is honoured per https://no-color.org: any non-empty value disables colour.
  style.color = tty && !dumb_terminal && !env_set("NO_COLOR") && !force_no_color;

  // Unicode is assumed unless asked otherwise, or when the locale looks non-UTF-8.
  const char* lang = std::getenv("LC_ALL");
  if (lang == nullptr || lang[0] == '\0') lang = std::getenv("LC_CTYPE");
  if (lang == nullptr || lang[0] == '\0') lang = std::getenv("LANG");
  const bool utf8_locale =
      lang == nullptr || lang[0] == '\0' || std::strstr(lang, "UTF-8") != nullptr ||
      std::strstr(lang, "utf8") != nullptr;
  style.unicode = !force_ascii && !dumb_terminal && utf8_locale;

  style.width = captured_width() > 0 ? captured_width() : kDefaultWidth;
  if (style.width < kMinWidth) style.width = kMinWidth;

  return style;
}

std::string bold(const Style& style, std::string_view text) { return wrap(style, "1", text); }
std::string dim(const Style& style, std::string_view text) { return wrap(style, "2", text); }
std::string red(const Style& style, std::string_view text) { return wrap(style, "31", text); }
std::string green(const Style& style, std::string_view text) { return wrap(style, "32", text); }
std::string yellow(const Style& style, std::string_view text) { return wrap(style, "33", text); }
std::string cyan(const Style& style, std::string_view text) { return wrap(style, "36", text); }

std::string_view glyph_rule(const Style& style) { return style.unicode ? "─" : "-"; }
std::string_view glyph_arrow_down(const Style& style) { return style.unicode ? "↓" : "v"; }
std::string_view glyph_arrow_change(const Style& style) { return style.unicode ? "↗" : "->"; }
std::string_view glyph_note(const Style& style) { return style.unicode ? "♪" : ""; }
std::string_view glyph_dash(const Style& style) { return style.unicode ? "–" : "-"; }

std::string rule(const Style& style) {
  const std::string_view unit = glyph_rule(style);
  std::string out;
  out.reserve(unit.size() * static_cast<std::size_t>(style.width));
  for (int i = 0; i < style.width; ++i) out.append(unit);
  return out;
}

std::string measure_range(const Style& style, int first, int last) {
  if (first == last) return fmt::format("m. {}", first);
  return fmt::format("mm. {}{}{}", first, glyph_dash(style), last);
}

std::size_t display_width(std::string_view text) {
  std::size_t width = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    // Count only UTF-8 lead bytes; continuation bytes are 10xxxxxx.
    if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++width;
  }
  return width;
}

std::string pad_right(std::string_view text, std::size_t width) {
  std::string out(text);
  const std::size_t current = display_width(text);
  if (current < width) out.append(width - current, ' ');
  return out;
}

std::vector<std::string> wrap_list(const std::vector<std::string>& items, std::size_t limit) {
  std::vector<std::string> rows;
  std::string row;

  for (std::size_t i = 0; i < items.size(); ++i) {
    std::string token = items[i];
    if (i + 1 < items.size()) token += ',';

    if (!row.empty() && display_width(row) + 1 + display_width(token) > limit) {
      rows.push_back(row);
      row.clear();
    }
    if (!row.empty()) row += ' ';
    row += token;
  }

  if (!row.empty()) rows.push_back(row);
  return rows;
}

}  // namespace motif::render
