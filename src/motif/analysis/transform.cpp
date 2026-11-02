#include "motif/analysis/transform.h"

#include <numeric>

namespace motif::analysis {
namespace {

using diff::Transform;
using score::Note;

/// True when both sequences have the same length, at least the minimum, and that length
/// is what the tick vectors describe.
bool comparable(const std::vector<Note>& before, const std::vector<int>& before_ticks,
                const std::vector<Note>& after, const std::vector<int>& after_ticks) {
  return before.size() == after.size() && before.size() >= kMinTransformNotes &&
         before_ticks.size() == before.size() && after_ticks.size() == after.size();
}

bool same_rhythm(const std::vector<int>& before, const std::vector<int>& after) {
  return before == after;
}

/// A constant non-zero pitch offset across every note, with the rhythm untouched.
///
/// The interval is measured in semitones from the MIDI numbers: that is what "up a
/// step" means to a player, and it stays correct across enharmonic respellings.
bool detect_transposition(const std::vector<Note>& before, const std::vector<int>& before_ticks,
                          const std::vector<Note>& after, const std::vector<int>& after_ticks,
                          Transform& out) {
  if (!same_rhythm(before_ticks, after_ticks)) return false;

  const int delta = after[0].pitch.midi - before[0].pitch.midi;
  if (delta == 0) return false;

  for (std::size_t i = 0; i < before.size(); ++i) {
    if (after[i].pitch.midi - before[i].pitch.midi != delta) return false;
  }

  out.kind = Transform::Kind::Transposition;
  out.semitones = delta;
  out.note_count = static_cast<int>(before.size());
  return true;
}

/// Every duration scaled by one ratio, with the pitches untouched.
///
/// The ratio is required to be exact for every note, so a phrase where only some values
/// doubled is not reported as an augmentation.
bool detect_scaling(const std::vector<Note>& before, const std::vector<int>& before_ticks,
                    const std::vector<Note>& after, const std::vector<int>& after_ticks,
                    Transform& out) {
  for (std::size_t i = 0; i < before.size(); ++i) {
    if (!(before[i].pitch == after[i].pitch)) return false;
  }
  if (same_rhythm(before_ticks, after_ticks)) return false;

  if (before_ticks[0] <= 0 || after_ticks[0] <= 0) return false;

  // Reduce the first pair to lowest terms and require every other pair to match it.
  const int divisor = std::gcd(after_ticks[0], before_ticks[0]);
  const int num = after_ticks[0] / divisor;
  const int den = before_ticks[0] / divisor;

  for (std::size_t i = 0; i < before.size(); ++i) {
    if (before_ticks[i] <= 0) return false;
    if (static_cast<long long>(before_ticks[i]) * num !=
        static_cast<long long>(after_ticks[i]) * den) {
      return false;
    }
  }

  if (num == den) return false;

  out.kind = num > den ? Transform::Kind::Augmentation : Transform::Kind::Diminution;
  out.ratio_num = num;
  out.ratio_den = den;
  out.note_count = static_cast<int>(before.size());
  return true;
}

/// The same pitches in reverse order, with the rhythm reversed to match.
bool detect_retrograde(const std::vector<Note>& before, const std::vector<int>& before_ticks,
                       const std::vector<Note>& after, const std::vector<int>& after_ticks,
                       Transform& out) {
  const std::size_t n = before.size();

  // A palindrome is not a retrograde of anything; require the order to actually differ.
  bool identical = true;
  for (std::size_t i = 0; i < n; ++i) {
    if (!(before[i].pitch == after[i].pitch)) {
      identical = false;
      break;
    }
  }
  if (identical) return false;

  for (std::size_t i = 0; i < n; ++i) {
    if (before[i].pitch.midi != after[n - 1 - i].pitch.midi) return false;
    if (before_ticks[i] != after_ticks[n - 1 - i]) return false;
  }

  out.kind = Transform::Kind::Retrograde;
  out.note_count = static_cast<int>(n);
  return true;
}

/// Intervals mirrored about the opening note, with the rhythm untouched.
bool detect_inversion(const std::vector<Note>& before, const std::vector<int>& before_ticks,
                      const std::vector<Note>& after, const std::vector<int>& after_ticks,
                      Transform& out) {
  if (!same_rhythm(before_ticks, after_ticks)) return false;
  if (before[0].pitch.midi != after[0].pitch.midi) return false;

  bool any_movement = false;
  for (std::size_t i = 0; i < before.size(); ++i) {
    const int original = before[i].pitch.midi - before[0].pitch.midi;
    const int mirrored = after[i].pitch.midi - after[0].pitch.midi;
    if (mirrored != -original) return false;
    if (original != 0) any_movement = true;
  }
  if (!any_movement) return false;

  out.kind = Transform::Kind::Inversion;
  out.note_count = static_cast<int>(before.size());
  return true;
}

}  // namespace

std::vector<Transform> detect(const std::vector<Note>& before,
                              const std::vector<int>& before_ticks,
                              const std::vector<Note>& after,
                              const std::vector<int>& after_ticks) {
  std::vector<Transform> found;
  if (!comparable(before, before_ticks, after, after_ticks)) return found;

  // Each test is tried independently; at most one can hold, since they make mutually
  // exclusive demands on the pitches and rhythms.
  Transform transform;
  if (detect_transposition(before, before_ticks, after, after_ticks, transform)) {
    found.push_back(transform);
    return found;
  }
  if (detect_scaling(before, before_ticks, after, after_ticks, transform)) {
    found.push_back(transform);
    return found;
  }
  if (detect_inversion(before, before_ticks, after, after_ticks, transform)) {
    found.push_back(transform);
    return found;
  }
  if (detect_retrograde(before, before_ticks, after, after_ticks, transform)) {
    found.push_back(transform);
    return found;
  }

  return found;
}

void annotate(diff::ScoreDiff& diff) {
  for (auto& change : diff.staves) {
    // A truncated run is only a prefix of the material, so a relationship holding over
    // the visible part says nothing about the rest. Only complete runs are tested.
    if (change.sequence_state != diff::StaffChange::SequenceState::Complete) continue;

    change.transforms = detect(change.before_seq, change.before_ticks, change.after_seq,
                               change.after_ticks);
  }
}

}  // namespace motif::analysis
