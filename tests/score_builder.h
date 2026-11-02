#pragma once

#include <fmt/format.h>

#include <optional>
#include <string>
#include <vector>

/// Builders for small MuseScore documents, so diff tests can state a musical situation
/// directly instead of carrying fixture files around.
namespace motif::testing {

struct TestNote {
  int midi = 60;
  int tpc = 14;
};

/// One measure: a list of quarter-note chords, plus the eid that identifies the measure
/// across versions. Giving two versions the same eid is how a test says "this is the
/// same bar, edited".
struct TestMeasure {
  std::string eid;
  std::vector<TestNote> notes;
  std::optional<int> key_sig;
  std::optional<std::pair<int, int>> time_sig;
  /// Set to reuse element ids from another version, marking the notes as the same notes.
  std::string element_prefix;
};

struct TestPart {
  std::string name;          ///< the part's trackName, e.g. "Violins, Violin I"
  std::string instrument_id; ///< e.g. "strings.violin"
  std::vector<TestMeasure> measures;
};

/// Renders parts into a MuseScore XML document, one staff per part.
inline std::string build_score(const std::vector<TestPart>& parts) {
  std::string xml =
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
      "<museScore version=\"4.70\"><Score><Division>480</Division>"
      "<metaTag name=\"workTitle\">Test</metaTag>";

  for (std::size_t p = 0; p < parts.size(); ++p) {
    xml += fmt::format(
        "<Part id=\"{}\"><Staff/><trackName>{}</trackName>"
        "<Instrument id=\"i{}\"><trackName>{}</trackName>"
        "<instrumentId>{}</instrumentId></Instrument></Part>",
        p + 1, parts[p].name, p + 1, parts[p].name, parts[p].instrument_id);
  }

  for (std::size_t p = 0; p < parts.size(); ++p) {
    xml += fmt::format("<Staff id=\"{}\">", p + 1);

    for (std::size_t m = 0; m < parts[p].measures.size(); ++m) {
      const auto& measure = parts[p].measures[m];
      xml += "<Measure>";
      if (!measure.eid.empty()) xml += fmt::format("<eid>{}</eid>", measure.eid);
      xml += "<voice>";

      if (measure.key_sig) {
        xml += fmt::format("<KeySig><concertKey>{}</concertKey></KeySig>", *measure.key_sig);
      }
      if (measure.time_sig) {
        xml += fmt::format("<TimeSig><sigN>{}</sigN><sigD>{}</sigD></TimeSig>",
                           measure.time_sig->first, measure.time_sig->second);
      }

      const std::string prefix =
          measure.element_prefix.empty() ? fmt::format("s{}m{}", p, m) : measure.element_prefix;

      if (measure.notes.empty()) {
        xml += fmt::format(
            "<Rest><eid>{}r</eid><durationType>measure</durationType><duration>4/4</duration></Rest>",
            prefix);
      } else {
        for (std::size_t n = 0; n < measure.notes.size(); ++n) {
          xml += fmt::format(
              "<Chord><eid>{0}c{1}</eid><durationType>quarter</durationType>"
              "<Note><eid>{0}n{1}</eid><pitch>{2}</pitch><tpc>{3}</tpc></Note></Chord>",
              prefix, n, measure.notes[n].midi, measure.notes[n].tpc);
        }
      }

      xml += "</voice></Measure>";
    }
    xml += "</Staff>";
  }

  xml += "</Score></museScore>";
  return xml;
}

/// Four quarter notes forming an ascending run from `start`, spelled naturally.
inline std::vector<TestNote> scale_run(int start_midi, int start_tpc) {
  return {
      TestNote{start_midi, start_tpc},
      TestNote{start_midi + 2, start_tpc + 2},
      TestNote{start_midi + 4, start_tpc + 4},
      TestNote{start_midi + 5, start_tpc - 1},
  };
}

/// The same run shifted by `semitones`, which on the line of fifths is the same shift.
inline std::vector<TestNote> transposed(const std::vector<TestNote>& notes, int semitones) {
  std::vector<TestNote> out;
  out.reserve(notes.size());
  for (const auto& n : notes) {
    out.push_back(TestNote{n.midi + semitones, n.tpc + semitones});
  }
  return out;
}

}  // namespace motif::testing
