# Font size: Bigger / Smaller

File windows' tags now carry `Bigger` and `Smaller` (after `Look`).
Clicking one steps the window's body font one size up or down;
`Bigger 3` jumps three sizes. The tag font is untouched, matching
what `Font` (toggle) already does.

## How it works

Wily's X-backed fonts are loaded by XLFD name. Bigger/Smaller asks
the X server for the current font's canonical XLFD name (via the
font's `FONT` property), then looks for the nearest larger/smaller
size of the same face, falling back to the same family at any width,
then to direct neighbouring sizes. A candidate that renders at the
same height as the current font is skipped: some servers answer a
missing size with a fallback font, and applying that would be a
silent no-op. When no real size exists in that direction, the
command reports `no larger/smaller font available` in `+Errors`
and leaves the font alone.

## Limits

- Only X-backed fonts resize. Windows using a Plan 9 font file
  (loaded via the `p9font`/`p9fixed` resources) report `this font
  cannot be resized`.
- The size is per window, like `Font`. Resizing one window does not
  resize the others.
- The size persists across restarts: the session file records each
  window's font name as an optional trailing field on its `win`
  line, and restore re-applies it (a font the server no longer has
  is reported and skipped, never fatal). Old session files without
  the field still load.
- Keyboard shortcuts are deliberately not bound: wily has no
  shortcut table for these, and grabbing keys silently would break
  muscle memory. If you want keys, say which ones.
