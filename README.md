# Motif: A Music-Native Git Wrapper For MuseScore Files 🎼

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![Build: CMake](https://img.shields.io/badge/build-CMake-064F8C.svg?logo=cmake&logoColor=white)](https://cmake.org/)
[![Requires: Git](https://img.shields.io/badge/requires-Git-F05032.svg?logo=git&logoColor=white)](https://git-scm.com/)
[![MuseScore](https://img.shields.io/badge/MuseScore-4-1A70DA.svg?logo=musescore&logoColor=white)](https://musescore.org/)

`.mscz` files are zipped files, with underlying XML representing musical staffs.
Even if you git diff'ed the raw XML it would look like this:
```
+          <Chord>
+            <eid>A+wG06iOjMN_7mhKj0SVR1K</eid>
+            <durationType>quarter</durationType>
+            <Note>
+              <eid>qfka9XoYzLN_nC47N0dUO8L</eid>
+              <pitch>71</pitch>
+              <tpc>19</tpc>
+              </Note>
+            </Chord>
```

## `motif` wraps git and calculates proper music diffs for supported git commands

### Commands

| Command | Description |
| --- | --- |
| `motif status` | Show changed files with a musical summary of what changed |
| `motif diff` | Show note-level changes between HEAD and the working tree |
| `motif log` | Show commit history with per-commit score stats |
| `motif inspect <file>` | Print a structural breakdown of a score (parts, staves, measures, notes) |

Any other command will be forwarded straight to git and returned without modification.

---

#### `motif status`
Show changed files, annotated with which instruments and measures changed.

```console
$ motif status
On branch main

Changes not staged for commit:

    MODIFIED   Flutes            m. 3
    MODIFIED   Piano · staff 1   m. 1

Musical summary:
  ~ 2 notes changed
  + 4 notes added

Other changed files:
  README.md
  .mscbackup/
```

#### `motif diff`
Show note-level changes between HEAD and the working tree.

In a terminal, each changed score opens engraved in [stave](https://github.com/SamChenYu/stave):
added notes in green, changed notes in yellow, and bars that lost notes in red. It opens on the
first change.

| Key | Does |
| --- | --- |
| `n` / `N` | next / previous change |
| ← → / h l | one bar |
| ↑ ↓ / k j | one staff |
| PgUp PgDn / Space | one screen |
| + - | zoom |
| q | next score, or quit |

Notes are drawn at concert pitch. stave draws pixels in terminals that show sixel images
(Windows Terminal 1.22+, WezTerm, foot, iTerm2), and falls back to braille characters elsewhere.
After you quit, motif prints a summary of the counts.

`--raw` prints the text view below instead. So do `--stat`, `--orchestration` and `--key`,
and any `motif diff` whose output isn't a terminal (piped, or redirected to a file):

```console
$ motif diff --raw
Piano Concerto in G Minor
HEAD → working tree
───────────────────

FLUTES
  m. 3

  AFTER
  ♪ B4


PIANO · STAFF 1
  m. 1

  AFTER
  ♪ E3 E3+G3


───────────────────
2 notes changed
4 notes added
```


#### `motif log -1`
Show commit history with per-commit score statistics.

```console
$ motif log -1
commit e1cfadf4ff0495783290ff87be64af5b57e30723
Author: Sam Yu
Date:   2026-08-14

    Initial commit

  13 instruments · 102 measures · 5430 notes
```

#### `motif inspect <file>`
Print a structural breakdown of a score — parts, staves, measures, and per-staff contents.

```console
$ motif inspect "Piano Concerto in G Minor.mscz"
Piano Concerto in G Minor.mscz
  division         480 ticks/quarter
  parts            13
  staves           14
  measures         102
  notes            5434
  measure eids     102/102 on staff 1
  key signature    1 sharp (E minor / G major)

PARTS
  [ 0] Flutes                   wind.flutes.flute          staves 1
  [ 1] Oboes                    wind.reed.oboe             staves 2
  [ 2] Clarinets in Bb          wind.reed.clarinet.bflat   staves 3
  [ 3] Bassoons                 wind.reed.bassoon          staves 4
  [ 4] Horns in F               brass.french-horn          staves 5
  [ 5] Trumpets in Bb           brass.trumpet.bflat        staves 6
  [ 6] Timpani                  drum.timpani               staves 7
  [ 7] Piano                    keyboard.piano             staves 8,9
  [ 8] Violin I                 strings.group              staves 10
  [ 9] Violin II                strings.group              staves 11
  [10] Viola                    strings.group              staves 12
  [11] Violoncello              strings.group              staves 13
  [12] Contrabass               strings.group              staves 14

(showing measures 1-8; pass --measures A:B for more)

STAFF 1 -- Flutes
  m.1    tick 0       1920 ticks  timesig 4/4  keysig 1 sharp
      voice 1: [G6 quarter articStaccatoAbove] [rest quarter] [F#6 quarter] [rest quarter]
  m.2    tick 1920    2400 ticks  timesig 5/4
      voice 1: [G6 dotted half] [rest half]
  m.3    tick 4320    1920 ticks  timesig 4/4
      voice 1: [B4 quarter] [rest quarter] [rest half]
  m.4    tick 6240    1920 ticks
      voice 1: [rest whole]
```

---

## Building

Dependencies are vendored under `external/`, some as git submodules (e.g. `stave`,
which itself nests other dependencies. Submodules must be initialized **recursively** or the build will fail.

```console
# When cloning fresh:
$ git submodule update --init --recursive
```

Requires CMake 3.24+ and a C++20 compiler (MSVC 2022, GCC 11+ or Clang 14+), on Windows, macOS
or Linux.

```console
$ cmake -S . -B build
$ cmake --build build --config Release
$ ctest --test-dir build -C Release --output-on-failure
```

The programs land together in `build/bin/` (`build/bin/Release/` with Visual Studio): `motif`,
`stave`, and `verovio-data/`, which holds stave's music fonts. Keep them together when
installing. motif looks for stave next to itself, then on `PATH`, or wherever `MOTIF_STAVE`
points.

stave pulls in Verovio, which takes a few minutes to compile the first time. Configure with
`-DMOTIF_WITH_STAVE=OFF` to build motif without it; `motif diff` then prints text.

Set `MOTIF_DUMP_NOTATION` to a folder to keep the MEI score and marks file that motif hands
stave. `stave score.mei --marks marks.json --png out.png` redraws them without a terminal.
