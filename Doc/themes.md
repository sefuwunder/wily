Themes
======

Wily has eleven built-in color themes. They change only the
foreground and background colors — text, selection (inverse
video), scrollbars, borders and cursors all follow from those two
— so a theme is a single pair of colors:

    default           black on white            #000000 on #ffffff
    tokyo             Tokyo Night (night)       #c0caf5 on #1a1b26
    tokyo-light       Tokyo Night Day (light)   #3760bf on #e1e2e7
    catppuccin        Catppuccin Mocha          #cdd6f4 on #1e1e2e
    catppuccin-light  Catppuccin Latte (light)  #4c4f69 on #eff1f5
    solarized         Solarized (dark)          #839496 on #002b36
    solarized-light   Solarized (light)         #657b83 on #fdf6e3
    nord              Nord (dark)               #d8dee9 on #2e3440
    nord-light        Nord (light)              #2e3440 on #eceff4
    rose-pine         Rosé Pine (main)          #e0def4 on #191724
    rose-pine-dawn    Rosé Pine Dawn (light)    #575279 on #faf4ed

Rosé Pine's light variant is officially called Dawn, hence
`rose-pine-dawn` (with `rose-pine-light` as an alias). Nord ships
no official light variant: `nord-light` is the community
inversion of the Nord palette (nord0 text on nord6), the same
pair other nord-light ports use.

Switching
---------

* Click `Theme` on the main tag: it cycles
  default → tokyo → tokyo-light → catppuccin → catppuccin-light
  → solarized → solarized-light → nord → nord-light
  → rose-pine → rose-pine-dawn → default.
* Run `Theme <name>` in any tag (middle-click it, as with any
  command): `Theme tokyo-light`, `Theme catppuccin-light`,
  `Theme default`. `tokyonight` is an alias for tokyo,
  `tokyonight-day` and `day` for tokyo-light, `mocha` and
  `catppuccin-mocha` for catppuccin, `latte` and
  `catppuccin-latte` for catppuccin-light, `solarized-dark`
  for solarized, `nord-dark` for nord, `rose-pine-main` for
  rose-pine, and `rose-pine-light` and `dawn` for
  rose-pine-dawn. An unknown name is reported and the current
  theme is left alone.
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
