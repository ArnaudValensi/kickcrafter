# Diagrams

`synthesis.svg` is the synthesis block diagram shown in [architecture.md](../architecture.md). It is
generated, not drawn by hand: the source is `tools/diagrams/synthesis_diagram.py` (plain Python 3,
no dependency). Never edit the SVG directly; edit the script, regenerate, check, commit both.

## Regenerate

```sh
python3 tools/diagrams/synthesis_diagram.py            # writes docs/diagrams/synthesis.svg
rsvg-convert -w 1400 docs/diagrams/synthesis.svg -o /tmp/synthesis.png   # render to look at it
```

`rsvg-convert` comes with librsvg (package `librsvg` on Arch, `librsvg2-bin` on Debian/Ubuntu); any
SVG viewer or a browser does as well. Always look at the render before committing: text widths depend
on the fonts, so a longer label can overflow its box or collide with an arrow. The script is
deterministic, so an unchanged script reproduces the committed file byte for byte (`git diff` stays
empty).

## The style, so that changes keep the same look

- **Layout.** Two rows of rounded blocks (`rx="8"`, stroke 1.6 px). Top row `y = 100`, height 100:
  the audio path from left to right, solid 2 px arrows between blocks, `OUT` at the far right.
  Bottom row `y = 300`, height 100: the modulators, each under the module it controls, with a dashed
  arrow going straight up into that module and an italic one-word label (`pitch`, `level`). `MIDI IN`
  sits at the left of the bottom row; its lines are routed with right angles, the lower one along
  `y = 430` below the row.
- **Blocks.** Title in capitals, 15 px semi-bold; one subtitle line in 12 px secondary ink listing the
  parameters or the operation; optionally a small glyph drawn with 1.5 px strokes (waveforms, envelope
  shapes) in the lower part of the block. Widths are chosen so the longest title fits with ~20 px
  of margin on each side in Helvetica/Arial; a module and the modulator under it share the same x
  and width so the control arrow is vertical.
- **Ink.** Black `#222222` for audio and titles, grey `#666666` for control lines, subtitles and
  labels, opaque white background card (`rx="12"`) so the image reads identically on light and dark
  pages. No colours: solid line = audio, dashed (`6 4`) = control. One version of the file only.
- **Text.** `font-family="Inter, Helvetica, Arial, sans-serif"`; the viewer's fallback decides the
  metrics, hence the generous margins. Legend top right, one explanatory sentence top left.
- **Canvas.** `1260 × 470`; widen it rather than squeezing blocks when adding a module.

## Adding or changing a module

Add a `block(x, y, w, h, TITLE, subtitle, glyph)` call on the right row, an `audio(x1, y, x2, y)`
arrow to its neighbours, and for a modulator a `ctrl([...points...], label, lx, ly)` dashed path up
into its module. Shift the blocks to its right by the added width, extend `W`, re-run, look at the
render, and update the module table in `architecture.md` in the same commit.
