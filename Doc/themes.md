Themes
======

Wily has five built-in color themes. They change only the
foreground and background colors — text, selection (inverse
video), scrollbars, borders and cursors all follow from those two
— so a theme is a single pair of colors:

    default           black on white            #000000 on #ffffff
    tokyo             Tokyo Night (night)       #c0caf5 on #1a1b26
    tokyo-light       Tokyo Night Day (light)   #3760bf on #e1e2e7
    catppuccin        Catppuccin Mocha          #cdd6f4 on #1e1e2e
    catppuccin-light  Catppuccin Latte (light)  #4c4f69 on #eff1f5

Switching
---------

* Click `Theme` on the main tag: it cycles
  default → tokyo → tokyo-light → catppuccin → catppuccin-light
  → default.
* Run `Theme <name>` in any tag (middle-click it, as with any
  command): `Theme tokyo-light`, `Theme catppuccin-light`,
  `Theme default`. `tokyonight` is an alias for tokyo,
  `tokyonight-day` and `day` for tokyo-light, `mocha` and
  `catppuccin-mocha` for catppuccin, `latte` and
  `catppuccin-latte` for catppuccin-light. An unknown
  name is reported and the current theme is left alone.
* `wily -e "Theme tokyo-light"` starts themed.

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
