#pragma once

#include <string>
#include <vector>

namespace motif::cli {

/// Entry point. `args` is argv without the program name.
///
/// Subcommands motif re-renders musically are handled here; everything else is handed
/// to git by replacing this process, so motif is a drop-in replacement for git.
int dispatch(std::vector<std::string> args);

/// True when `name` is a subcommand motif implements itself rather than forwarding.
bool is_motif_command(std::string_view name);

}  // namespace motif::cli
