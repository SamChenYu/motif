#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace motif::render {

/// How output should be dressed for the current terminal.
///
/// Detected once and threaded through the renderers, so that piping motif's output
/// into a file or a pager yields plain text with no escape sequences.
struct Style {
  bool color = false;
  bool unicode = true;
  int width = 80;

  /// Inspects stdout, TERM and NO_COLOR to decide. `force_no_color` and `force_ascii`
  /// come from the --no-color and --ascii flags and always win.
  [[nodiscard]] static Style detect(bool force_no_color = false, bool force_ascii = false);
};

// Text attributes. Each returns `text` unchanged when colour is disabled, so call sites
// never need to branch.
[[nodiscard]] std::string bold(const Style& style, std::string_view text);
[[nodiscard]] std::string dim(const Style& style, std::string_view text);
[[nodiscard]] std::string red(const Style& style, std::string_view text);
[[nodiscard]] std::string green(const Style& style, std::string_view text);
[[nodiscard]] std::string yellow(const Style& style, std::string_view text);
[[nodiscard]] std::string cyan(const Style& style, std::string_view text);

// Glyphs, with ASCII equivalents for terminals that cannot show them.
[[nodiscard]] std::string_view glyph_rule(const Style& style);        ///< horizontal line
[[nodiscard]] std::string_view glyph_arrow_down(const Style& style);  ///< a becomes b
[[nodiscard]] std::string_view glyph_arrow_change(const Style& style);
[[nodiscard]] std::string_view glyph_note(const Style& style);
[[nodiscard]] std::string_view glyph_dash(const Style& style);  ///< range separator

/// A full-width horizontal rule.
[[nodiscard]] std::string rule(const Style& style);

/// Formats a measure range as "m. 84" or "mm. 84-97".
[[nodiscard]] std::string measure_range(const Style& style, int first, int last);

/// The number of terminal columns `text` occupies.
///
/// Counts UTF-8 code points rather than bytes, so that multi-byte glyphs do not throw
/// off column alignment. Combining marks and wide East Asian characters are not
/// handled; motif's own output does not use them.
[[nodiscard]] std::size_t display_width(std::string_view text);

/// Pads `text` on the right to `width` display columns.
[[nodiscard]] std::string pad_right(std::string_view text, std::size_t width);

/// Wraps a comma-separated list of names into rows no wider than `limit`.
///
/// Long instrument lists are common -- a tutti key change touches every staff -- and
/// they must fold rather than run off the terminal.
[[nodiscard]] std::vector<std::string> wrap_list(const std::vector<std::string>& items,
                                                 std::size_t limit);

}  // namespace motif::render
