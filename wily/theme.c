/*******************************************
 *	Themes: named foreground/background pairs,
 *	switchable at runtime.
 *
 *	Why this works without touching the drawing code:
 *	libXg reads _fgpixel/_bgpixel fresh on every draw call
 *	(gcs.c), and selection is inverse video, so it adapts
 *	on its own. Switching a theme is therefore: allocate the
 *	new colors, repaint the whole tile tree (scroll positions
 *	are preserved — view_reshaped only resets them when the
 *	width changes), and recreate the mouse cursors, whose
 *	colors are baked in at creation time.
 *******************************************/

#include "wily.h"
#include "tile.h"
#include "data.h"

typedef struct Theme {
	char	*name;
	char	*fg;
	char	*bg;
} Theme;

static Theme themes[] = {
	/* default: wily's original black on white */
	{ "default",	"#000000", "#ffffff" },
	/* Tokyo Night (night variant) */
	{ "tokyo",	"#c0caf5", "#1a1b26" },
	/* Tokyo Night Day (light variant) */
	{ "tokyo-light",	"#3760bf", "#e1e2e7" },
	/* Catppuccin Mocha */
	{ "catppuccin",	"#cdd6f4", "#1e1e2e" },
	/* Catppuccin Latte (light variant) */
	{ "catppuccin-light", "#4c4f69", "#eff1f5" },
};
static int nthemes = sizeof themes / sizeof themes[0];
static int curtheme = 0;

const char *
theme_current(void)
{
	return themes[curtheme].name;
}

/* Find a theme by name, accepting a couple of obvious aliases.
 * Returns its index, or -1.
 */
static int
theme_lookup(const char *name)
{
	int i;

	if (!name)
		return -1;
	for (i = 0; i < nthemes; i++)
		if (strcmp(name, themes[i].name) == 0)
			return i;
	/* aliases */
	if (strcmp(name, "tokyonight") == 0)
		return 1;
	if (strcmp(name, "tokyonight-day") == 0 ||
	    strcmp(name, "day") == 0)
		return 2;
	if (strcmp(name, "mocha") == 0 ||
	    strcmp(name, "catppuccin-mocha") == 0)
		return 3;
	if (strcmp(name, "latte") == 0 ||
	    strcmp(name, "catppuccin-latte") == 0)
		return 4;
	return -1;
}

int
theme_apply(const char *name)
{
	int idx = theme_lookup(name);
	Theme *t;

	if (idx < 0)
		return -1;
	t = &themes[idx];
	/* Atomic at this layer: xg_setcolors allocates both colors
	 * before committing either, so a failure changes nothing.
	 */
	if (xg_setcolors(t->fg, t->bg) != 0)
		return -1;
	curtheme = idx;
	/* Mouse cursors bake their colors in at creation; drop the
	 * cached ones so they are recreated in the new colors.
	 * The arrow cursor is never created by wily (only fatarrow
	 * and boxcursor are switched to), so these two suffice.
	 */
	boxcursor.id = 0;
	cursor->id = 0;
	cursorswitch(cursor);
	if (wily)
		tile_reshaped(wily);
	return 0;
}

/*****************************************************
	Theme. With an argument, switch to that theme
	("Theme tokyo"); without one, cycle to the next.
	The Theme button on the main tag cycles.
*****************************************************/
void
theme_cmd(View *v, char *arg)
{
	char name[32];
	int i;

	(void)v;
	if (arg) {
		/* Take one word: skip leading blanks, cut at the
		 * first blank, so "Theme tokyo " still works.
		 */
		while (*arg == ' ' || *arg == '\t')
			arg++;
		for (i = 0; i < (int)sizeof name - 1 &&
				arg[i] && arg[i] != ' ' && arg[i] != '\t'; i++)
			name[i] = arg[i];
		name[i] = '\0';
		if (theme_apply(name) != 0)
			diag(0, "theme: unknown theme %s", name);
	} else {
		theme_apply(themes[(curtheme + 1) % nthemes].name);
	}
}
