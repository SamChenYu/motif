#include "motif/diff/note_diff.h"

#include <algorithm>
#include <map>
#include <unordered_map>

namespace motif::diff {
namespace {

using score::Element;
using score::Measure;
using score::Note;
using score::Voice;

/// A pairing of before/after elements within one voice. -1 means "no counterpart".
struct ElementPair {
  int before = -1;
  int after = -1;
};

/// Pairs up the elements of one voice across two versions.
///
/// Identity first: a chord keeps its eid through an edit, so matching on eid recognises
/// a changed chord as the same chord. Whatever is left over is matched by onset, which
/// is what catches material that MuseScore rewrote from scratch.
std::vector<ElementPair> pair_elements(const std::vector<Element>& before,
                                       const std::vector<Element>& after) {
  std::vector<ElementPair> pairs;
  std::vector<bool> before_used(before.size(), false);
  std::vector<bool> after_used(after.size(), false);

  std::unordered_map<std::string, std::size_t> after_by_eid;
  for (std::size_t j = 0; j < after.size(); ++j) {
    if (!after[j].eid.empty()) after_by_eid.emplace(after[j].eid, j);
  }

  for (std::size_t i = 0; i < before.size(); ++i) {
    if (before[i].eid.empty()) continue;
    const auto it = after_by_eid.find(before[i].eid);
    if (it == after_by_eid.end() || after_used[it->second]) continue;

    before_used[i] = true;
    after_used[it->second] = true;
    pairs.push_back({static_cast<int>(i), static_cast<int>(it->second)});
  }

  // Remaining elements: match on where they sit in the bar, preferring an exact
  // duration match so a split note is not mistaken for a shortened one.
  for (std::size_t i = 0; i < before.size(); ++i) {
    if (before_used[i]) continue;

    int best = -1;
    bool best_exact = false;
    for (std::size_t j = 0; j < after.size(); ++j) {
      if (after_used[j] || after[j].onset != before[i].onset) continue;
      const bool exact = after[j].ticks == before[i].ticks;
      if (best < 0 || (exact && !best_exact)) {
        best = static_cast<int>(j);
        best_exact = exact;
      }
      if (exact) break;
    }

    if (best >= 0) {
      before_used[i] = true;
      after_used[static_cast<std::size_t>(best)] = true;
      pairs.push_back({static_cast<int>(i), best});
    } else {
      pairs.push_back({static_cast<int>(i), -1});
    }
  }

  for (std::size_t j = 0; j < after.size(); ++j) {
    if (!after_used[j]) pairs.push_back({-1, static_cast<int>(j)});
  }

  std::sort(pairs.begin(), pairs.end(), [&](const ElementPair& l, const ElementPair& r) {
    const int l_onset = l.after >= 0 ? after[static_cast<std::size_t>(l.after)].onset
                                     : before[static_cast<std::size_t>(l.before)].onset;
    const int r_onset = r.after >= 0 ? after[static_cast<std::size_t>(r.after)].onset
                                     : before[static_cast<std::size_t>(r.before)].onset;
    return l_onset < r_onset;
  });

  return pairs;
}

/// Compares the noteheads of two chords sounding at the same place.
void diff_notes(const Element& before, const Element& after, int measure_number, int voice,
                std::vector<NoteChange>& out) {
  std::vector<bool> before_used(before.notes.size(), false);
  std::vector<bool> after_used(after.notes.size(), false);

  const auto emit = [&](NoteChange::Kind kind, const Note* b, const Note* a) {
    NoteChange change;
    change.kind = kind;
    change.measure = measure_number;
    change.voice = voice;
    change.onset = after.onset;
    if (b != nullptr) change.before = b->pitch;
    if (a != nullptr) change.after = a->pitch;
    change.before_ticks = before.ticks;
    change.after_ticks = after.ticks;
    out.push_back(change);
  };

  // Identity, then identical pitch, then position -- three passes, each only claiming
  // notes the earlier passes left unmatched.
  for (std::size_t i = 0; i < before.notes.size(); ++i) {
    if (before.notes[i].eid.empty()) continue;
    for (std::size_t j = 0; j < after.notes.size(); ++j) {
      if (after_used[j] || after.notes[j].eid != before.notes[i].eid) continue;
      before_used[i] = true;
      after_used[j] = true;
      if (!(before.notes[i].pitch == after.notes[j].pitch)) {
        emit(NoteChange::Kind::PitchChanged, &before.notes[i], &after.notes[j]);
      }
      break;
    }
  }

  for (std::size_t i = 0; i < before.notes.size(); ++i) {
    if (before_used[i]) continue;
    for (std::size_t j = 0; j < after.notes.size(); ++j) {
      if (after_used[j] || !(after.notes[j].pitch == before.notes[i].pitch)) continue;
      before_used[i] = true;
      after_used[j] = true;
      break;
    }
  }

  for (std::size_t i = 0; i < before.notes.size(); ++i) {
    if (before_used[i]) continue;
    for (std::size_t j = 0; j < after.notes.size(); ++j) {
      if (after_used[j]) continue;
      before_used[i] = true;
      after_used[j] = true;
      emit(NoteChange::Kind::PitchChanged, &before.notes[i], &after.notes[j]);
      break;
    }
  }

  for (std::size_t i = 0; i < before.notes.size(); ++i) {
    if (!before_used[i]) emit(NoteChange::Kind::Removed, &before.notes[i], nullptr);
  }
  for (std::size_t j = 0; j < after.notes.size(); ++j) {
    if (!after_used[j]) emit(NoteChange::Kind::Added, nullptr, &after.notes[j]);
  }

  // A chord whose pitches all survived but whose length changed is a rhythmic edit. A
  // rest that became a chord is not: its notes are simply added.
  if (before.ticks != after.ticks && !before.notes.empty() && !after.notes.empty()) {
    NoteChange change;
    change.kind = NoteChange::Kind::DurationChanged;
    change.measure = measure_number;
    change.voice = voice;
    change.onset = after.onset;
    change.before = before.notes.empty() ? std::optional<score::Pitch>{} : before.notes[0].pitch;
    change.after = after.notes[0].pitch;
    change.before_ticks = before.ticks;
    change.after_ticks = after.ticks;
    out.push_back(change);
  }

  if (before.articulations != after.articulations) {
    NoteChange change;
    change.kind = NoteChange::Kind::ArticulationChanged;
    change.measure = measure_number;
    change.voice = voice;
    change.onset = after.onset;
    if (!after.notes.empty()) change.after = after.notes[0].pitch;
    if (!before.notes.empty()) change.before = before.notes[0].pitch;
    out.push_back(change);
  }
}

/// Emits every note of one element as wholly added or removed.
void emit_element(const Element& el, int measure_number, int voice, NoteChange::Kind kind,
                  std::vector<NoteChange>& out) {
  for (const auto& note : el.notes) {
    NoteChange change;
    change.kind = kind;
    change.measure = measure_number;
    change.voice = voice;
    change.onset = el.onset;
    if (kind == NoteChange::Kind::Removed) {
      change.before = note.pitch;
      change.before_ticks = el.ticks;
    } else {
      change.after = note.pitch;
      change.after_ticks = el.ticks;
    }
    out.push_back(change);
  }
}

const Voice* find_voice(const Measure& measure, int index) {
  for (const auto& voice : measure.voices) {
    if (voice.index == index) return &voice;
  }
  return nullptr;
}

}  // namespace

std::vector<NoteChange> diff_measure(const Measure& before, const Measure& after,
                                     int measure_number) {
  std::vector<NoteChange> out;

  // Voices are compared by their index, so material staying in voice 2 is compared with
  // voice 2 even when a voice is added above or below it.
  std::vector<int> indices;
  for (const auto& v : before.voices) indices.push_back(v.index);
  for (const auto& v : after.voices) indices.push_back(v.index);
  std::sort(indices.begin(), indices.end());
  indices.erase(std::unique(indices.begin(), indices.end()), indices.end());

  for (const int index : indices) {
    const Voice* before_voice = find_voice(before, index);
    const Voice* after_voice = find_voice(after, index);

    if (before_voice == nullptr && after_voice == nullptr) continue;

    if (before_voice == nullptr) {
      for (const auto& el : after_voice->elements) {
        emit_element(el, measure_number, index, NoteChange::Kind::Added, out);
      }
      continue;
    }
    if (after_voice == nullptr) {
      for (const auto& el : before_voice->elements) {
        emit_element(el, measure_number, index, NoteChange::Kind::Removed, out);
      }
      continue;
    }

    for (const auto& pair : pair_elements(before_voice->elements, after_voice->elements)) {
      if (pair.before >= 0 && pair.after >= 0) {
        diff_notes(before_voice->elements[static_cast<std::size_t>(pair.before)],
                   after_voice->elements[static_cast<std::size_t>(pair.after)], measure_number,
                   index, out);
      } else if (pair.before >= 0) {
        emit_element(before_voice->elements[static_cast<std::size_t>(pair.before)],
                     measure_number, index, NoteChange::Kind::Removed, out);
      } else if (pair.after >= 0) {
        emit_element(after_voice->elements[static_cast<std::size_t>(pair.after)], measure_number,
                     index, NoteChange::Kind::Added, out);
      }
    }
  }

  std::stable_sort(out.begin(), out.end(),
                   [](const NoteChange& a, const NoteChange& b) { return a.onset < b.onset; });
  return out;
}

std::vector<NoteChange> whole_measure_change(const Measure& measure, int measure_number,
                                             NoteChange::Kind kind) {
  std::vector<NoteChange> out;
  for (const auto& voice : measure.voices) {
    for (const auto& el : voice.elements) {
      emit_element(el, measure_number, voice.index, kind, out);
    }
  }
  for (auto& change : out) change.whole_measure = true;
  return out;
}

}  // namespace motif::diff
