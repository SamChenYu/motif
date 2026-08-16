# Motif 
A music-native git wrapper for MuseScore files 🎼

## `motif status`
```shell
On branch main

Changes not staged for commit:

    MODIFIED   Flutes            m. 3
    MODIFIED   Piano · staff 1   m. 1

Musical summary:
  ~ 2 notes changed
  + 4 notes added

Other changed files:
  hello.md
  .mscbackup/
```

## `motif diff`
```shell
Piano Concerto in G Minor
HEAD → working tree
────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────

FLUTES
  m. 3

  AFTER
  ♪ B4


PIANO · STAFF 1
  m. 1

  AFTER
  ♪ E3 E3+G3


────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────
2 notes changed
4 notes added
```


## `motif log -1`
```shell
commit e1cfadf4ff0495783290ff87be64af5b57e30723
Author: Sam Yu
Date:   2026-08-14

    Initial commit

  13 instruments · 102 measures · 5430 notes
```

## `motif inspect Piano\ Concerto\ in\ G\ Minor.mscz`
```shell
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