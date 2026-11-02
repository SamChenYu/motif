#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "motif/score/model.h"

namespace motif::score {

/// The fraction of a whole note a MuseScore `<durationType>` names.
///
/// Returns nullopt for "measure" (whose length depends on the enclosing measure's time
/// signature) and for any unrecognised type.
[[nodiscard]] std::optional<Fraction> duration_type_fraction(std::string_view type);

/// Duration in ticks of a `<durationType>` with `dots` augmentation dots applied.
///
/// Each dot adds half of what precedes it, so n dots multiply the base by
/// (2^(n+1) - 1) / 2^n.
[[nodiscard]] std::optional<int> duration_ticks(std::string_view type, int dots, int division);

/// Scales a duration by a tuplet ratio, e.g. normal 2 / actual 3 for a triplet.
[[nodiscard]] int apply_tuplet(int ticks, int normal_notes, int actual_notes);

/// Renders a tick count as the nearest note-value name, for human-readable output
/// ("quarter", "dotted eighth"). Falls back to a tick count when nothing matches.
[[nodiscard]] std::string describe_ticks(int ticks, int division);

}  // namespace motif::score
