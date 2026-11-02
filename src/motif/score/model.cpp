#include "motif/score/model.h"

namespace motif::score {

int Measure::note_count() const {
  int n = 0;
  for (const auto& voice : voices) {
    for (const auto& el : voice.elements) {
      n += static_cast<int>(el.notes.size());
    }
  }
  return n;
}

std::string Part::display_name() const {
  // "Violins, Violin I" -> "Violin I"; "Timpani, Timpani" -> "Timpani".
  const auto specific = [](const std::string& name) -> std::string {
    const auto comma = name.rfind(", ");
    if (comma == std::string::npos) return name;
    return name.substr(comma + 2);
  };

  if (!track_name.empty()) return specific(track_name);
  if (!long_name.empty()) return specific(long_name);
  if (!instrument_name.empty()) return instrument_name;
  if (!short_name.empty()) return short_name;
  if (!instrument_id.empty()) return instrument_id;
  return id.empty() ? "(unnamed part)" : "Part " + id;
}

const Staff* Score::staff_by_id(int id) const {
  for (const auto& s : staves) {
    if (s.id == id) return &s;
  }
  return nullptr;
}

const Part* Score::part_for_staff(int staff_id) const {
  const auto* s = staff_by_id(staff_id);
  if (s == nullptr || s->part_index < 0) return nullptr;
  if (static_cast<std::size_t>(s->part_index) >= parts.size()) return nullptr;
  return &parts[static_cast<std::size_t>(s->part_index)];
}

int Score::measure_count() const {
  return staves.empty() ? 0 : static_cast<int>(staves.front().measures.size());
}

int Score::note_count() const {
  int n = 0;
  for (const auto& staff : staves) {
    for (const auto& m : staff.measures) {
      n += m.note_count();
    }
  }
  return n;
}

std::optional<int> Score::key_signature(int measure_number) const {
  // Signatures carry forward, so take the last one declared at or before this measure.
  std::map<int, int> tally;
  for (const auto& staff : staves) {
    std::optional<int> in_force;
    for (const auto& m : staff.measures) {
      if (m.number > measure_number) break;
      // Skip transposing staves: their `concert` value cannot be trusted.
      if (m.key_sig && !m.key_sig->transposing()) in_force = m.key_sig->concert;
    }
    if (in_force) ++tally[*in_force];
  }

  if (tally.empty()) return std::nullopt;

  auto best = tally.begin();
  for (auto it = tally.begin(); it != tally.end(); ++it) {
    if (it->second > best->second) best = it;
  }
  return best->first;
}

std::string Score::title() const {
  for (const char* key : {"workTitle", "movementTitle"}) {
    const auto it = meta.find(key);
    if (it != meta.end() && !it->second.empty()) return it->second;
  }
  return {};
}

}  // namespace motif::score
