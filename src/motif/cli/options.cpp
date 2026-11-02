#include "motif/cli/options.h"

#include <fmt/format.h>

#include <charconv>

namespace motif::cli {
namespace {

/// Parses "84:104" or "84" into an inclusive measure range.
bool parse_measure_range(std::string_view text, std::pair<int, int>& out) {
  const auto parse = [](std::string_view s, int& value) {
    if (s.empty()) return false;
    return std::from_chars(s.data(), s.data() + s.size(), value).ec == std::errc{};
  };

  const auto colon = text.find(':');
  if (colon == std::string_view::npos) {
    int single = 0;
    if (!parse(text, single)) return false;
    out = {single, single};
    return true;
  }

  int first = 0;
  int last = 0;
  if (!parse(text.substr(0, colon), first)) return false;
  if (!parse(text.substr(colon + 1), last)) return false;
  if (first > last) return false;

  out = {first, last};
  return true;
}

/// Reads the value of a flag written either as `--flag value` or `--flag=value`.
bool take_value(const std::vector<std::string>& args, std::size_t& i, std::string_view name,
                std::string& out) {
  const std::string& arg = args[i];
  if (arg.size() > name.size() && arg[name.size()] == '=') {
    out = arg.substr(name.size() + 1);
    return !out.empty();
  }
  if (i + 1 < args.size()) {
    out = args[++i];
    return true;
  }
  return false;
}

bool matches(const std::string& arg, std::string_view name) {
  return arg == name || (arg.size() > name.size() && arg.compare(0, name.size(), name) == 0 &&
                         arg[name.size()] == '=');
}

}  // namespace

const char* common_flag_help() {
  return
      "  --instrument <name>   only this instrument (substring, case-insensitive)\n"
      "  --measures A:B        only measures A to B\n"
      "  --staff <n>           only this staff\n"
      "  --orchestration       only which instruments were added or removed\n"
      "  --key                 only key and time signature changes\n"
      "  --stat                counts only, no per-instrument detail\n"
      "  --detail              log: expand every listed commit\n"
      "  --ascii               plain ASCII instead of box-drawing characters\n"
      "  --no-color            no ANSI colour\n";
}

Options parse_options(const std::vector<std::string>& args, bool accepts_revisions) {
  Options options;
  bool after_separator = false;

  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string& arg = args[i];

    if (after_separator) {
      options.paths.push_back(arg);
      continue;
    }
    if (arg == "--") {
      after_separator = true;
      continue;
    }

    if (arg == "-h" || arg == "--help") {
      options.help = true;
      continue;
    }
    if (arg == "--ascii") {
      options.ascii = true;
      continue;
    }
    if (arg == "--no-color" || arg == "--no-colour") {
      options.no_color = true;
      continue;
    }
    if (arg == "--stat") {
      options.stat = true;
      continue;
    }
    if (arg == "--detail") {
      options.detail = true;
      continue;
    }
    if (arg == "--orchestration") {
      options.only_orchestration = true;
      continue;
    }
    if (arg == "--key" || arg == "--harmony") {
      options.only_key = true;
      continue;
    }

    if (matches(arg, "--instrument")) {
      std::string value;
      if (!take_value(args, i, "--instrument", value)) {
        options.error = "--instrument needs an instrument name";
        return options;
      }
      options.instrument = value;
      continue;
    }

    if (matches(arg, "--measures")) {
      std::string value;
      if (!take_value(args, i, "--measures", value)) {
        options.error = "--measures needs a range like 84:104";
        return options;
      }
      std::pair<int, int> range;
      if (!parse_measure_range(value, range)) {
        options.error = fmt::format("'{}' is not a measure range like 84:104", value);
        return options;
      }
      options.measures = range;
      continue;
    }

    if (matches(arg, "--staff")) {
      std::string value;
      int staff = 0;
      if (!take_value(args, i, "--staff", value) ||
          std::from_chars(value.data(), value.data() + value.size(), staff).ec != std::errc{}) {
        options.error = "--staff needs a staff number";
        return options;
      }
      options.staff = staff;
      continue;
    }

    if (arg == "-n" || matches(arg, "--max-count")) {
      std::string value;
      int limit = 0;
      const std::string_view name = arg == "-n" ? "-n" : "--max-count";
      if (!take_value(args, i, name, value) ||
          std::from_chars(value.data(), value.data() + value.size(), limit).ec != std::errc{}) {
        options.error = fmt::format("{} needs a count", name);
        return options;
      }
      options.limit = limit;
      continue;
    }

    // `log -1` and friends: a bare negative number is a commit count.
    if (arg.size() > 1 && arg[0] == '-' && std::isdigit(static_cast<unsigned char>(arg[1]))) {
      int limit = 0;
      if (std::from_chars(arg.data() + 1, arg.data() + arg.size(), limit).ec == std::errc{}) {
        options.limit = limit;
        continue;
      }
    }

    if (arg.starts_with("-")) {
      options.error = fmt::format("unknown option '{}'", arg);
      return options;
    }

    if (!accepts_revisions) {
      options.error = fmt::format("unexpected argument '{}'", arg);
      return options;
    }
    options.positional.push_back(arg);
  }

  return options;
}

}  // namespace motif::cli
