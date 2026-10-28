/* fontsize.c - increase/decrease a view's font size (Bigger/Smaller).
 *
 * How it works: wily's X-backed fonts are built by mkfont(), which
 * names them "<synthetic>"; Plan 9 font-file fonts keep their file
 * path as the name. Only "<synthetic>" fonts are resizable: their X
 * font id (Subfont.id) is fed to XQueryFont, whose FONT property
 * yields the canonical XLFD name, and xlfd.c steps its pixel field
 * until XLoadQueryFont accepts a size. Stepped fonts are cached by
 * name and never unloaded (a handful of fonts per session at most).
 *
 * The XFontStruct returned by XQueryFont is deliberately not freed:
 * freeing it risks unloading the live font; the leak is one small
 * struct per resize step.
 */
#include "wily.h"
#include "view.h"
/* Same trick as libXg/libgint.h: X11's Cursor/Font/Event collide
 * with wily's structs. Rename them for the X headers only. */
#define Cursor xCursor
#define Font xFont
#define Event xEvent
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#undef Cursor
#undef Font
#undef Event

extern Display *_dpy;

int xlfd_sizes(const char *xlfd, int *pixel, int *point);
char *xlfd_with_pixel(const char *xlfd, int new_pixel);
char *xlfd_replace_field(const char *xlfd, int idx, const char *value);

#define FONTCACHE 64
static struct {
	char *name;
	Font *font;
} fontcache[FONTCACHE];
static int nfontcache = 0;

/* The view's current font as an XLFD name, or NULL when it is not
 * an X font (Plan 9 font file) or has no server-side name. A font
 * applied through font_from_xlfd answers with the name it was
 * requested by: the server's FONT property can lie about aliased
 * loads (a missing size may report the size it fell back to), and
 * the requested name is what the next step must count from. */
char *
view_font_xlfd(View *v)
{
	Font *f;
	XFontStruct *fs;
	unsigned long val;
	char *name;
	int i;

	if (!v || !v->f.font)
		return NULL;
	f = v->f.font;
	for (i = 0; i < nfontcache; i++)
		if (fontcache[i].font == f)
			return strdup(fontcache[i].name);
	if (!f->name || strcmp(f->name, "<synthetic>") != 0)
		return NULL;
	if (f->nsubf < 1 || !f->subf || !f->subf[0].f)
		return NULL;
	fs = XQueryFont(_dpy, (XID)f->subf[0].f->id);
	if (!fs)
		return NULL;
	if (!XGetFontProperty(fs, XA_FONT, &val))
		return NULL;
	name = XGetAtomName(_dpy, (Atom)val);
	if (!name)
		return NULL;
	{
		char *copy = strdup(name);
		XFree(name);
		return copy;
	}
}

/* Load (or reuse) a Font by XLFD name. NULL on failure. */
static Font *
font_from_xlfd(const char *name)
{
	int i;
	Subfont *sub;

	for (i = 0; i < nfontcache; i++)
		if (strcmp(fontcache[i].name, name) == 0)
			return fontcache[i].font;
	sub = getsubfont((char *)name);
	if (!sub)
		return NULL;
	{
		Font *f = mkfont(sub);
		if (!f)
			return NULL;
		if (nfontcache < FONTCACHE) {
			fontcache[nfontcache].name = strdup(name);
			fontcache[nfontcache].font = f;
			nfontcache++;
		}
		return f;
	}
}

/* Apply an XLFD-named font to a view. 0 = ok, -1 = cannot load. */
int
view_font_set_xlfd(View *v, const char *name)
{
	Font *f;

	if (!v || !name)
		return -1;
	f = font_from_xlfd(name);
	if (!f)
		return -1;
	frfont(&v->f, f);
	if (ISVISIBLE(v))
		view_reshaped(v, v->r);
	return 0;
}

/* Apply f to v only when it really changes the rendered size: a
 * server may answer a missing size with a fallback font (same
 * metrics, different fid), and applying that would be a silent
 * no-op that corrupts the next step's size. Returns 1 if applied. */
static int
try_apply(View *v, Font *f)
{
	Font *cur = v->f.font;

	if (!f || !cur)
		return 0;
	if (f->height == cur->height && f->ascent == cur->ascent)
		return 0;
	frfont(&v->f, f);
	if (ISVISIBLE(v))
		view_reshaped(v, v->r);
	return 1;
}

/* Step a view's font size. dir > 0 grows, dir < 0 shrinks, repeating
 * steps times (so "Bigger 3" jumps three sizes). Returns 0, or -1
 * when the font cannot be resized at all, or -2 when it is already
 * at the largest/smallest available size in that direction.
 *
 * Each step tries, in order:
 *  1. the same face (all fields but size) at the nearest larger /
 *     smaller pixel size, via XListFonts with the size wildcarded;
 *  2. the same family/weight/slant at any width, same way;
 *  3. direct loads at neighbouring pixel sizes (covers servers
 *     that synthesise sizes, e.g. scalable faces).
 * Bitmap faces have sparse sizes, so guessing pixel+1, pixel+2, ...
 * alone (stage 3) misses the next real size; the list stages find
 * it. Candidates whose metrics match the current font are skipped:
 * they are server fallbacks, not new sizes. Scalable names (pixel
 * 0) use the rendered height as the current-size estimate.
 */
int
view_font_step(View *v, int dir, int steps)
{
	int s;

	if (!v || dir == 0 || steps <= 0)
		return -1;
	for (s = 0; s < steps; s++) {
		char *cur = view_font_xlfd(v);
		int pixel, point, done = 0;

		if (!cur)
			return -1;
		if (xlfd_sizes(cur, &pixel, &point) < 0) {
			free(cur);
			return -1;
		}
		if (pixel <= 0)
			pixel = v->f.font->height;
		/* Stages 1 and 2: list-based picks, nearest first. */
		{
			int stage;
			for (stage = 0; stage < 2 && !done; stage++) {
				char *pat = xlfd_replace_field(cur, 6, "*");
				char *pat2;
				char **names;
				int count = 0, i, j;

				if (!pat)
					break;
				pat2 = xlfd_replace_field(pat, 7, "*");
				free(pat);
				if (!pat2)
					break;
				pat = pat2;
				if (stage == 1) {
					/* Relax width: setwidth, avgwidth. */
					pat2 = xlfd_replace_field(pat, 4, "*");
					free(pat);
					if (!pat2)
						break;
					pat = pat2;
					pat2 = xlfd_replace_field(pat, 11, "*");
					free(pat);
					if (!pat2)
						break;
					pat = pat2;
				}
				names = XListFonts(_dpy, pat, 1000, &count);
				free(pat);
				if (!names)
					continue;
				/* Selection order: nearest size in the
				 * stepping direction first. */
				for (i = 0; i < count && !done; i++) {
					int best = -1, besti = -1;
					for (j = 0; j < count; j++) {
						int p, q;
						if (!names[j])
							continue;
						if (xlfd_sizes(names[j], &p, &q) < 0 ||
						    p <= 0)
							continue;
						if (dir > 0 && p > pixel &&
						    (best < 0 || p < best)) {
							best = p;
							besti = j;
						}
						if (dir < 0 && p < pixel &&
						    (best < 0 || p > best)) {
							best = p;
							besti = j;
						}
					}
					if (besti < 0)
						break;
					{
						Font *f =
						    font_from_xlfd(names[besti]);
						if (f && try_apply(v, f))
							done = 1;
					}
					names[besti] = NULL; /* tried */
				}
				XFreeFontNames(names);
			}
		}
		/* Stage 3: direct neighbouring sizes, scaled point
		 * first, then point 0 (scalable form). */
		if (!done) {
			int t;
			for (t = 1; t <= 32 && !done; t++) {
				int cand_pixel = pixel + dir * t;
				char *cand;
				Font *f;

				if (cand_pixel < 1)
					break;
				cand = xlfd_with_pixel(cur, cand_pixel);
				if (!cand)
					break;
				f = font_from_xlfd(cand);
				if (!f) {
					char *zero;
					free(cand);
					cand = xlfd_with_pixel(cur, cand_pixel);
					if (!cand)
						break;
					zero = xlfd_replace_field(cand, 7, "0");
					free(cand);
					if (!zero)
						break;
					f = font_from_xlfd(zero);
					free(zero);
				} else {
					free(cand);
				}
				if (f && try_apply(v, f))
					done = 1;
			}
		}
		free(cur);
		if (!done)
			return -2;
	}
	return 0;
}
