# Wily vs real Acme — gap list (shelved 2026-10-28)

Comparison of Acme's command table (plan9port `src/cmd/acme/exec.c`
`exectab`) against wily's builtins table (`wily/builtins.c`), verified in
both sources 2026-10-28. Decision shelved by the user; do not build
without a fresh pick.

## Already present in both (no work)

Cut, Paste, Snarf, Del, Delcol, New, Newcol, Get, Put, Putall, Indent,
Undo, Redo, Look (partial — see below), Font (takes an argument),
Zerox = Split, Exit = Quit, Dump/Load = SaveSession/RestoreSession.

## Not gaps (verified)

- **Local**: wily already chdirs to the window's directory before running
  every command (`wily/exec.c` ex_child). Acme's Local is wily's default.
- **Font with argument**: `tile_setfont` already takes an argument.
- **Dump/Load**: covered by SaveSession/RestoreSession (own format).

## Gaps, ranked

1. **Edit** — the sam command language (addresses + a/c/d/i/p/x/y/g/v/s
   and friends) on the body. Large. Wily has sam's regexp engine only
   (`wily/sam.c` is regexp stubs); no address parser, no command
   interpreter.
2. **Incl** — per-window include path for filename resolution; bare
   Incl lists it. Small-medium. Wily's `<file>` click is one-shot only.
3. **Tab** — per-window tabstop (`Tab 8`, bare Tab prints current).
   Small-medium. Wily's tabsize is global (`-a` only).
4. **Sort** — sorts a column's windows alphabetically by label (Acme's
   `colsort` in `cols.c`). Small: reorder + relayout. Not text sorting.
5. **Look across windows** — Acme's Look searches all open windows;
   wily's Look searches only the current window (`view_look` on `v->t`).
   Small-medium. Button-3 already covers the file:addr half.
6. **Send** — cut selection, paste at end of body, ensure trailing
   newline. Small as a text op; caveat: in Acme it feeds a running
   program, and wily has no interactive-command model, so here it is
   text-shuffling only.
7. **ID** — print the window's id for scripting. Tiny. (Acme prints
   `/mnt/acme/N/`; wily's version would print its message-socket id.)
8. **/mnt/acme** — Acme's 9P interface (addr, body, ctl, data, event,
   tag, xdata). Extra-large; mostly Plan 9 tool compatibility — wily's
   message socket and tools already cover most of the same uses.
9. **Plumbing** — a plumb-port listener (open file:addr, show text).
   Medium-large; inert without a plumber daemon.
10. **Acme Dump-format compatibility** — medium. Session save/restore
    exists; this is only format interop.

`Delete` as an alias for Del is trivial and rides free with any batch.

## Divisive / needs its own decision

- B2B1 chord-context difference (1996 doc: Acme executes a B2B1 command
  in the context of the command, wily in the context of the argument).
  Touches muscle memory.
- Label contraction via environment variables (`$h/src/`).
- Right column skinnier (Acme layout quirk).

## Wily-only (Acme lacks)

Markdown rendering, Theme, Anchor, Clear, Dotfiles, message-socket
tools, `.wilytools` regexps.
