#pragma once

#include <string>

#include "motif/diff/change_model.h"
#include "motif/render/terminal.h"

namespace motif::render {

struct DiffViewOptions {
  bool show_orchestration = true;
  bool show_key = true;
  bool show_staves = true;
  bool stat_only = false;  ///< counts only, no per-instrument detail
};

/// Renders a full musical diff: a header, one block per changed instrument, then
/// orchestration and key-signature blocks and a closing summary.
///
/// `subtitle` names what is being compared, e.g. "working tree vs HEAD".
void render_diff(const diff::ScoreDiff& diff, const Style& style, const std::string& subtitle,
                 const DiffViewOptions& options = {});

}  // namespace motif::render
