/*******************************************
 *	Session save and restore
 *
 * A session is the set of visible columns and windows, in order,
 * with each window's file (stored as its resolved path, so a
 * session restores the same files from any directory), cursor
 * (dot), scroll origin, size, and — for windows with unsaved
 * edits — a copy of the buffer.
 *
 * The file itself lives at $WILYSESSION, else $HOME/.wily-session
 * (see session_default_path); unsaved buffers go in <file>.d/N.bak.
 * The record format is in sessionfmt.c; this file only converts
 * between tiles and those records.
 *
 * Save runs on demand (SaveSession) and on every clean exit, via
 * cleanup() in util.c — so quitting always preserves the session.
 * Restore runs at startup when no files and no -e command were
 * given, and on demand (RestoreSession).
 *******************************************/

#include "wily.h"
#include "tile.h"
#include "view.h"
#include "data.h"
#include "sessionfmt.h"
#include <dirent.h>
#include <sys/stat.h>

/* Set while restoring, so win_new() places windows in the column
 * being restored instead of its usual hash-chosen column.
 */
Tile	*session_restore_col = 0;

static char	*session_override = 0;

/* Defensive cap: a corrupted tile list must not loop forever. */
enum { MAXWALK = 10000 };

void
session_set_path(char *path)
{
	session_override = path;
}

/* Fill buf with the session file path. Returns 0 on success. */
static int
session_path(char *buf, int n)
{
	char *home;

	if (session_override) {
		snprintf(buf, n, "%s", session_override);
		return 0;
	}
	if ((home = getenv("WILYSESSION")) != 0) {
		snprintf(buf, n, "%s", home);
		return 0;
	}
	if ((home = getenv("HOME")) != 0) {
		snprintf(buf, n, "%s/.wily-session", home);
		return 0;
	}
	return -1;
}

int
session_default_exists(void)
{
	Path p;
	Stat st;

	if (session_path(p, sizeof p) != 0)
		return 0;
	return stat(p, &st) == 0;
}

/* "<session>.d" — the directory holding unsaved-buffer backups. */
static void
backup_dir(char *buf, int n, char *path)
{
	snprintf(buf, n, "%s.d", path);
}

/* Join dir + "/" + name into buf (size n). Returns 0, or -1 if it
 * does not fit. Built by hand, not snprintf: a path that does not
 * fit is an error here, never a silent shortening.
 */
static int
join_path(char *buf, int n, const char *dir, const char *name)
{
	size_t dl = strlen(dir), nl = strlen(name);

	if (dl + 1 + nl + 1 > (size_t)n)
		return -1;
	memcpy(buf, dir, dl);
	buf[dl] = '/';
	memcpy(buf + dl + 1, name, nl + 1);
	return 0;
}

/* "N.bak" for backup index N (N >= 0). */
static void
bak_name(char *buf, int idx)
{
	char rev[16];
	int r = 0, i = 0;

	if (idx == 0)
		rev[r++] = '0';
	else while (idx > 0) {
		rev[r++] = '0' + idx % 10;
		idx /= 10;
	}
	while (r > 0)
		buf[i++] = rev[--r];
	buf[i++] = '.';
	buf[i++] = 'b';
	buf[i++] = 'a';
	buf[i++] = 'k';
	buf[i] = '\0';
}

static int
ends_with(char *s, char *suffix)
{
	int ls = strlen(s), lf = strlen(suffix);

	return ls >= lf && strcmp(s + ls - lf, suffix) == 0;
}

/* Collect the current layout into records, writing backups for
 * dirty windows as we go. Returns 0 on success.
 */
static int
session_collect(Session *s, char *bakdir)
{
	Tile *c, *w;
	int ncol = 0, nwin = 0, bakidx = 0;
	int rootspan;

	if (!wily)
		return -1;
	snprintf(s->theme, sizeof s->theme, "%s", theme_current());
	rootspan = wily->cmax - wily->cmin;
	for (c = wily->down; c && ncol < MAXWALK; c = c->right) {
		int col, width;
		if (c->ishidden)
			continue;
		width = rootspan > 0 ?
			(c->max - c->min) * 1000 / rootspan : 0;
		col = session_add_col(s, width);
		if (col < 0)
			return -1;
		ncol++;
		for (w = c->down; w && nwin < MAXWALK; w = w->right) {
			Data *d;
			View *body;
			Path fpath;
			int height, kind, backup = -1;
			ulong dot, origin, len;
			int colspan;

			if (w->ishidden || !ISWIN(w))
				continue;
			body = w->body;
			d = view_data(body);
			if (!d || ends_with(d->label, "+Errors"))
				continue;	/* transient output, not a file */
			len = text_length(body->t);
			dot = body->sel.p0 <= len ? body->sel.p0 : len;
			origin = body->visible.p0 <= len ?
				body->visible.p0 : len;
			colspan = c->cmax - c->cmin;
			height = colspan > 0 ?
				(w->max - w->min) * 1000 / colspan : 0;
			kind = (d->has_stat || d->names) ? 0 : 1;
			/* Store the resolved path for real files, so a
			 * session saved in one directory restores the
			 * same files from another. A never-saved
			 * window has no path; its label is stored.
			 */
			if (kind == 0)
				label2path(fpath, d->label);
			else
				snprintf(fpath, sizeof fpath, "%s", d->label);
			if (data_isdirty(d)) {
				Path bak;
				char name[32];
				bak_name(name, bakidx);
				if (join_path(bak, sizeof bak, bakdir, name) == 0 &&
				    text_write(d->t, bak) == 0)
					backup = bakidx;
				else
					diag(0, "session: backup failed for %s",
						d->label);
				bakidx++;
			}
			{
				char *fontname = view_font_xlfd(body);
				int r = session_add_win(s, col, fpath,
						dot, origin, height, kind,
						backup, fontname);
				free(fontname);
				if (r != 0)
					return -1;
			}
			nwin++;
		}
	}
	return 0;
}

/* Delete backup files numbered >= keep (stale, from a longer
 * previous session). Other files in the directory are untouched.
 */
static void
backup_prune(char *bakdir, int keep)
{
	DIR *dirp;
	struct dirent *de;

	if ((dirp = opendir(bakdir)) == 0)
		return;
	while ((de = readdir(dirp)) != 0) {
		char *dot = strrchr(de->d_name, '.');
		int n;
		if (!dot || strcmp(dot, ".bak") != 0)
			continue;
		n = atoi(de->d_name);
		if (n >= keep) {
			Path p;
			if (join_path(p, sizeof p, bakdir, de->d_name) == 0)
				unlink(p);
		}
	}
	closedir(dirp);
}

int
session_save(char *path)
{
	Path tmp, bakdir;
	Session *s;
	FILE *fp;
	int bakcount = 0, i, j;

	if (!wily || !path)
		return -1;
	backup_dir(bakdir, sizeof bakdir, path);
	/* Best effort: a missing backup dir only costs dirty windows
	 * their content, not the session itself.
	 */
	mkdir(bakdir, 0700);

	s = session_alloc();
	if (!s)
		return -1;
	if (session_collect(s, bakdir) != 0) {
		session_free(s);
		return -1;
	}
	for (i = 0; i < s->ncol; i++)
		for (j = 0; j < s->cols[i].nwin; j++)
			if (s->cols[i].wins[j].backup >= bakcount)
				bakcount = s->cols[i].wins[j].backup + 1;

	/* Write via a temp file + rename, so a crash mid-save never
	 * leaves a truncated session behind.
	 */
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	if ((fp = fopen(tmp, "w")) == 0) {
		session_free(s);
		return -1;
	}
	if (session_write(fp, s) != 0 || fclose(fp) != 0) {
		fclose(fp);
		unlink(tmp);
		session_free(s);
		return -1;
	}
	if (rename(tmp, path) != 0) {
		unlink(tmp);
		session_free(s);
		return -1;
	}
	backup_prune(bakdir, bakcount);
	session_free(s);
	return 0;
}

void
session_save_default(void)
{
	Path p;

	if (!wily)
		return;
	if (session_path(p, sizeof p) == 0)
		session_save(p);	/* errors are silent on exit */
}

/* Reorder list's children to tiles[0..n-1] and give them sizes from
 * permille[0..n-1] of the list span. Returns 0 on success; on
 * failure the list is left untouched (caller keeps creation order).
 * Every tile must end up at least its base size, and the sizes must
 * tile the list span exactly.
 */
static int
reorder_resize(Tile *list, Tile **tiles, int *permille, int n)
{
	int span = list->cmax - list->cmin;
	int i, pos, size;

	if (n <= 0 || span <= 0)
		return -1;
	pos = list->cmin;
	for (i = 0; i < n; i++) {
		size = (i == n - 1) ? (list->cmax - pos) :
			span * permille[i] / 1000;
		if (size < tiles[i]->base)
			return -1;
		pos += size;
	}
	if (pos != list->cmax)
		return -1;
	list->down = tiles[0];
	for (i = 0; i < n; i++) {
		tiles[i]->left = (i > 0) ? tiles[i - 1] : 0;
		tiles[i]->right = (i < n - 1) ? tiles[i + 1] : 0;
	}
	pos = list->cmin;
	for (i = 0; i < n; i++) {
		size = (i == n - 1) ? (list->cmax - pos) :
			span * permille[i] / 1000;
		tiles[i]->min = pos;
		tiles[i]->max = pos + size;
		pos += size;
	}
	return 0;
}

/* Read a whole file into memory (for dirty-window backups).
 * Returns 0 on success, with *buf holding malloc'd bytes.
 */
static int
read_file(char *path, char **buf, long *len)
{
	FILE *fp;
	long n;

	if ((fp = fopen(path, "r")) == 0)
		return -1;
	fseek(fp, 0, SEEK_END);
	n = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	if (n < 0)
		n = 0;
	*buf = malloc(n + 1);
	if (!*buf) {
		fclose(fp);
		return -1;
	}
	if (n && fread(*buf, 1, n, fp) != (size_t)n) {
		free(*buf);
		fclose(fp);
		return -1;
	}
	(*buf)[n] = '\0';
	*len = n;
	fclose(fp);
	return 0;
}

int
session_restore(char *path)
{
	Path bakdir;
	FILE *fp;
	Session *s;
	int i, j;
	int had_columns;

	if (!wily || !path)
		return -1;
	if ((fp = fopen(path, "r")) == 0)
		return -1;
	s = session_read(fp);
	fclose(fp);
	if (!s)
		return -1;
	backup_dir(bakdir, sizeof bakdir, path);
	had_columns = (wily->down != 0);

	/* The theme is applied first, so windows are created
	 * already themed. An unknown name (hand-edited file, or
	 * a theme from a newer wily) must not fail the restore.
	 */
	if (s->theme[0] && theme_apply(s->theme) != 0)
		diag(0, "session: unknown theme %s", s->theme);

	for (i = 0; i < s->ncol; i++) {
		SCol *sc = &s->cols[i];
		Tile *col;
		Tile **tiles = 0;
		int *heights = 0;
		int ntiles = 0;

		col = col_new_tile(wily->tag);
		if (!col) {
			session_free(s);
			return -1;
		}
		session_restore_col = col;
		tiles = malloc(sizeof(Tile*) * (sc->nwin + 1));
		heights = malloc(sizeof(int) * (sc->nwin + 1));
		if (!tiles || !heights) {
			free(tiles);
			free(heights);
			session_restore_col = 0;
			session_free(s);
			return -1;
		}
		for (j = 0; j < sc->nwin; j++) {
			SWin *sw = &sc->wins[j];
			Path fpath;
			View *bv;
			ulong len;

			if (sw->kind == 0) {
				Stat st;
				label2path(fpath, sw->label);
				if (stat(fpath, &st) != 0) {
					diag(0, "session: skipping missing %s",
						sw->label);
					continue;
				}
			}
			bv = data_open(sw->label, true);
			if (!bv) {
				diag(0, "session: cannot open %s", sw->label);
				continue;
			}
			tiles[ntiles] = bv->tile;
			heights[ntiles] = sw->height;
			ntiles++;
			if (sw->backup >= 0) {
				Path bak;
				char name[32];
				char *content;
				long clen;
				bak_name(name, sw->backup);
				if (join_path(bak, sizeof bak, bakdir, name) != 0 ||
				    read_file(bak, &content, &clen) != 0) {
					diag(0, "session: backup %d unreadable",
						sw->backup);
				} else {
					text_replaceutf(bv->t,
						text_all(bv->t), content);
					free(content);
					/* The replace marks the window
					 * dirty (Put reappears) via the
					 * undo machinery, as if typed.
					 */
				}
			}
			len = text_length(bv->t);
			view_setorigin(bv, sw->origin <= len ?
				sw->origin : len);
			view_select(bv, range(sw->dot <= len ?
				sw->dot : len, sw->dot <= len ?
				sw->dot : len));
			/* A font the server no longer has must not fail
			 * the restore; the window just keeps its
			 * default font. */
			if (sw->fontname &&
			    view_font_set_xlfd(bv, sw->fontname) != 0)
				diag(0, "session: unknown font %s",
					sw->fontname);
		}
		session_restore_col = 0;
		/* New columns contain only restored windows, so
		 * reordering them to saved order is always safe.
		 */
		if (ntiles > 0 &&
		    reorder_resize(col, tiles, heights, ntiles) == 0)
			list_reshaped(col, 0);
		free(tiles);
		free(heights);
	}

	if (!had_columns && s->ncol > 0) {
		/* Startup restore: the root chain holds exactly the
		 * restored columns, so order + widths can be exact.
		 */
		Tile **cols = malloc(sizeof(Tile*) * s->ncol);
		int *widths = malloc(sizeof(int) * s->ncol);
		Tile *c;
		int n = 0;
		if (cols && widths) {
			for (c = wily->down; c && n < s->ncol;
					c = c->right)
				cols[n++] = c;
			for (i = 0; i < n; i++)
				widths[i] = s->cols[i].width;
			if (n == s->ncol &&
			    reorder_resize(wily, cols, widths, n) == 0)
				list_reshaped(wily, 0);
		}
		free(cols);
		free(widths);
	}
	session_free(s);
	return 0;
}

int
session_restore_default(void)
{
	Path p;

	if (session_path(p, sizeof p) != 0)
		return -1;
	return session_restore(p);
}

/* Builtins (see builtins.c). arg is an optional session path. */
void
session_save_cmd(View *v, char *arg)
{
	Path p;

	(void)v;
	if (arg && *arg) {
		if (session_save(arg) != 0)
			diag(0, "session: cannot save %s", arg);
		return;
	}
	if (session_path(p, sizeof p) == 0) {
		if (session_save(p) != 0)
			diag(0, "session: cannot save %s", p);
	}
}

void
session_restore_cmd(View *v, char *arg)
{
	Path p;

	(void)v;
	if (arg && *arg) {
		if (session_restore(arg) != 0)
			diag(0, "session: cannot restore %s", arg);
		return;
	}
	if (session_path(p, sizeof p) == 0) {
		if (session_restore(p) != 0)
			diag(0, "session: cannot restore %s", p);
	}
}
