Themes
======

Wily has three built-in color themes. They change only the
foreground and background colors — text, selection (inverse
video), scrollbars, borders and cursors all follow from those two
— so a theme is a single pair of colors:

    default     black on white            #000000 on #ffffff
    tokyo       Tokyo Night (night)       #c0caf5 on #1a1b26
    catppuccin  Catppuccin Mocha          #cdd6f4 on #1e1e2e

Switching
---------

* Click `Theme` on the main tag: it cycles
  default → tokyo → catppuccin → default.
* Run `Theme <name>` in any tag (middle-click it, as with any
  command): `Theme tokyo`, `Theme catppuccin`, `Theme default`.
  `tokyonight` and `mocha` are accepted as aliases. An unknown
  name is reported and the current theme is left alone.
* `wily -e "Theme tokyo"` starts themed.

The switch repaints every window in place: scroll positions,
selections and layout are untouched.

Persistence
-----------

The current theme is saved in the session file (`theme <name>`,
see `session.md`) and restored with it. A session file without a
theme line restores with whatever theme is current.

How it works
------------

`theme.c` owns the theme table and the `Theme` command. The
colors themselves are applied by `xg_setcolors` (libXg), which
allocates both colors before committing either, so a failure
changes nothing. No drawing code needed changes: libXg reads the
foreground/background pixels fresh on every draw call, and the
mouse cursors — whose colors are baked in at creation — are
dropped and recreated on switch.
