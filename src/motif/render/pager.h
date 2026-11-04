#pragma once

namespace motif::render {

/// Records whether stdout is a terminal, and how wide.
///
/// Must be called before any pager is started, because once output is redirected into a
/// pager stdout is a pipe and the real terminal's properties are no longer visible.
void init_terminal();

/// True when stdout was a terminal at startup, regardless of any pager since.
[[nodiscard]] bool stdout_was_tty();

/// True when stdin was a terminal at startup, so a full-screen program can read keys.
[[nodiscard]] bool stdin_was_tty();

/// The terminal width captured at startup, or 0 when stdout is not a terminal.
[[nodiscard]] int captured_width();

/// Pipes stdout through a pager, as git does, when output is going to a terminal.
///
/// Honours MOTIF_PAGER, then PAGER; setting either to "cat" (or MOTIF_NO_PAGER to
/// anything) disables paging. Returns true when a pager was started.
bool start_pager();

/// Closes stdout and waits for the pager to exit. Safe to call when none was started.
///
/// Without this the shell prompt would return while the pager is still on screen.
void finish_pager();

}  // namespace motif::render
