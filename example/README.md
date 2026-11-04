# Example: Beethoven, String Quartet op. 18 no. 1

Two versions of one score, to try motif on:

| File | What it is |
| --- | --- |
| `beethoven-original.mscz` | The score as transcribed by OpenScore |
| `beethoven-modified.mscz` | The same score with a few edits |

The edits in `beethoven-modified.mscz`:

| Instrument | Bar | Edit | In stave |
| --- | --- | --- | --- |
| Violin 1 | 19 | two notes changed | yellow on both sides |
| Violin 2 | 18 | staccatos added | yellow on both sides |
| Violin 2 | 21–22 | the answer to the opening figure moved up a whole step | yellow on both sides |
| Viola | 21 | joins the opening figure, where it rested | green, on the right |
| Violoncello | 18 | the repeated B flats removed | red, on the left |
| Violin 1 | 318 | one note raised, in the second movement (Adagio) | yellow on both sides |

## Trying it

motif works on a git repository, so make one with the original in it, commit it, and then
copy the modified version over it. `example/demo/` is ignored by motif's own repository, so
it's a good place for it.

In PowerShell, from motif's folder:

```powershell
mkdir example\demo
cd example\demo
git init
copy ..\beethoven-original.mscz beethoven-op18no1.mscz
git add beethoven-op18no1.mscz
git commit -m "add Beethoven op. 18 no. 1"
copy ..\beethoven-modified.mscz beethoven-op18no1.mscz
..\..\build\bin\Release\motif.exe diff
```

In a POSIX shell (macOS, Linux, Git Bash):

```sh
mkdir example/demo
cd example/demo
git init
cp ../beethoven-original.mscz beethoven-op18no1.mscz
git add beethoven-op18no1.mscz
git commit -m "add Beethoven op. 18 no. 1"
cp ../beethoven-modified.mscz beethoven-op18no1.mscz
../../build/bin/motif diff
```

The path to motif depends on how it was built: `build/bin/Release/` with Visual Studio,
`build/bin/` otherwise.

`motif diff` opens the score in stave at the first change, with the committed version on the
left and the modified one on the right, scrolling together. `n` and `N` jump between changes,
the arrow keys move by bar and staff, `+` and `-` zoom, and `q` quits. Run it in a terminal that
shows sixel images (Windows Terminal 1.22+, WezTerm, iTerm2) for proper notation.

Then try:

- `motif status`: which instruments changed, in which bars
- `motif diff --raw`: the same diff as text
- `git commit -am "edit"`, then `motif log`: the history, summarised musically

To start again, delete `example/demo/`.

## Source

From [OpenScore String Quartets](https://github.com/OpenScore/StringQuartets/tree/main/scores/Beethoven%2C_Ludwig_van/String_Quartet_No.1%2C_Op.18_No.1)
(`sq8071278.mscx`), transcribed by contributors to OpenScore String Quartets and released under
CC0. Both copies have the separate instrument parts that the original file embeds removed
(motif reads only the score), and are packed as `.mscz`, MuseScore's compressed format.

It's a MuseScore 3 file, so it has no element ids; motif matches its bars by content.
