#pragma once

#include <string>
#include <vector>

namespace motif::cli {

/// Each command receives its own argv slice, with the subcommand name at index 0
/// (matching the convention CLI11 and getopt expect), and returns a process exit code.

int cmd_status(const std::vector<std::string>& args);
int cmd_diff(const std::vector<std::string>& args);
int cmd_log(const std::vector<std::string>& args);
int cmd_inspect(const std::vector<std::string>& args);

}  // namespace motif::cli
