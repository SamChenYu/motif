#include "motif/score/ticks.h"

#include <fmt/format.h>

#include <array>
#include <utility>

namespace motif::score {
namespace {

/// MuseScore duration type names, as fractions of a whole note.
constexpr std::array<std::pair<std::string_view, Fraction>, 12> kDurations{{
    {"long", Fraction{4, 1}},
    {"breve", Fraction{2, 1}},
    {"whole", Fraction{1, 1}},
    {"half", Fraction{1, 2}},
    {"quarter", Fraction{1, 4}},
    {"eighth", Fraction{1, 8}},
    {"16th", Fraction{1, 16}},
    {"32nd", Fraction{1, 32}},
    {"64th", Fraction{1, 64}},
    {"128th", Fraction{1, 128}},
    {"256th", Fraction{1, 256}},
    {"512th", Fraction{1, 512}},
}};

}  // namespace

std::optional<Fraction> duration_type_fraction(std::string_view type) {
  for (const auto& [name, frac] : kDurations) {
    if (name == type) return frac;
  }
  return std::nullopt;
}

std::optional<int> duration_ticks(std::string_view type, int dots, int division) {
  const auto base = duration_type_fraction(type);
  if (!base) return std::nullopt;

  const long long base_ticks = base->to_ticks(division);
  if (dots <= 0) return static_cast<int>(base_ticks);

  // n dots multiply the base duration by (2^(n+1) - 1) / 2^n.
  const long long scale = (1LL << (dots + 1)) - 1;
  const long long divisor = 1LL << dots;
  return static_cast<int>((base_ticks * scale) / divisor);
}

int apply_tuplet(int ticks, int normal_notes, int actual_notes) {
  if (actual_notes <= 0 || normal_notes <= 0) return ticks;
  return static_cast<int>((static_cast<long long>(ticks) * normal_notes) / actual_notes);
}

std::string describe_ticks(int ticks, int division) {
  if (ticks <= 0 || division <= 0) return fmt::format("{} ticks", ticks);

  for (const auto& [name, frac] : kDurations) {
    const int base = frac.to_ticks(division);
    if (base <= 0) continue;
    if (ticks == base) return std::string(name);
    if (ticks * 2 == base * 3) return fmt::format("dotted {}", name);
    if (ticks * 4 == base * 7) return fmt::format("double-dotted {}", name);
  }
  return fmt::format("{} ticks", ticks);
}

}  // namespace motif::score
