/* Session file format: write and parse. See sessionfmt.h.
 * Pure stdio/stdlib/string; no wily types, no X.
 */

#include <stdlib.h>
#include <string.h>

#include "sessionfmt.h"

/* Defensive caps against corrupt or hostile session files. Labels
 * in wily are Path-sized (a few KB at most); anything bigger is not
 * a real session.
 */
enum {
	MAXLABEL	= 65536,
	MAXCOL		= 256,
	MAXWIN		= 16384
};

Session *
session_alloc(void)
{
	return calloc(1, sizeof(Session));
}

void
session_free(Session *s)
{
	int i, j;

	if (!s)
		return;
	for (i = 0; i < s->ncol; i++) {
		for (j = 0; j < s->cols[i].nwin; j++) {
			free(s->cols[i].wins[j].label);
			free(s->cols[i].wins[j].fontname);
		}
		free(s->cols[i].wins);
	}
	free(s->cols);
	free(s);
}

int
session_add_col(Session *s, int width)
{
	SCol *c;

	if (!s || s->ncol >= MAXCOL)
		return -1;
	if (s->ncol == s->capcol) {
		int ncap = s->capcol ? s->capcol * 2 : 4;
		SCol *nc = realloc(s->cols, ncap * sizeof(SCol));
		if (!nc)
			return -1;
		s->cols = nc;
		s->capcol = ncap;
	}
	c = &s->cols[s->ncol++];
	memset(c, 0, sizeof *c);
	c->width = width;
	return s->ncol - 1;
}

int
session_add_win(Session *s, int col, const char *label,
		unsigned long dot, unsigned long origin,
		int height, int kind, int backup, const char *fontname)
{
	SCol *c;
	SWin *w;
	int total = 0, i;

	if (!s || col < 0 || col >= s->ncol || !label)
		return -1;
	for (i = 0; i < s->ncol; i++)
		total += s->cols[i].nwin;
	if (total >= MAXWIN)
		return -1;
	c = &s->cols[col];
	if (c->nwin == c->capwin) {
		int ncap = c->capwin ? c->capwin * 2 : 4;
		SWin *nw = realloc(c->wins, ncap * sizeof(SWin));
		if (!nw)
			return -1;
		c->wins = nw;
		c->capwin = ncap;
	}
	w = &c->wins[c->nwin++];
	memset(w, 0, sizeof *w);
	w->label = strdup(label);
	if (!w->label)
		return -1;
	w->dot = dot;
	w->origin = origin;
	w->height = height;
	w->kind = kind;
	w->backup = backup;
	w->fontname = fontname ? strdup(fontname) : NULL;
	return 0;
}

int
session_write(FILE *fp, Session *s)
{
	int i, j;

	if (!fp || !s)
		return -1;
	if (fprintf(fp, "wily-session 1\n") < 0)
		return -1;
	if (s->theme[0] &&
	    fprintf(fp, "theme %s\n", s->theme) < 0)
		return -1;
	for (i = 0; i < s->ncol; i++) {
		SCol *c = &s->cols[i];
		if (fprintf(fp, "col %d\n", c->width) < 0)
			return -1;
		for (j = 0; j < c->nwin; j++) {
			SWin *w = &c->wins[j];
			size_t len = strlen(w->label);
			if (fprintf(fp, "win %lu ", (unsigned long)len) < 0)
				return -1;
			if (len && fwrite(w->label, 1, len, fp) != len)
				return -1;
			if (fprintf(fp, " %lu %lu %d %d %d",
				w->dot, w->origin, w->height,
				w->kind, w->backup) < 0)
				return -1;
			/* Optional trailing font name (no spaces in an
			 * XLFD name, but the reader takes the whole rest
			 * of the line, so paths with spaces survive too).
			 * Old files have no trailing token: the reader
			 * treats that as "default font". */
			if (w->fontname &&
			    fprintf(fp, " %s", w->fontname) < 0)
				return -1;
			if (fputc('\n', fp) == EOF)
				return -1;
		}
	}
	return 0;
}

/* Read one "win" record. The file position must be just past the
 * "win" keyword. Returns 0 on success, -1 on malformed input.
 */
static int
read_win(FILE *fp, Session *s, int col)
{
	unsigned long len, dot, origin;
	int height, kind, backup;
	char *label, *fontname = NULL;
	char rest[1024];

	if (fscanf(fp, "%lu", &len) != 1 || len > MAXLABEL)
		return -1;
	/* Exactly one space separates the length from the label bytes;
	 * the label itself may start with whitespace, so consume the
	 * separator explicitly instead of letting fscanf skip it.
	 */
	if (fgetc(fp) != ' ')
		return -1;
	label = malloc(len + 1);
	if (!label)
		return -1;
	if (len && fread(label, 1, len, fp) != len) {
		free(label);
		return -1;
	}
	label[len] = '\0';
	if (fscanf(fp, "%lu %lu %d %d %d",
			&dot, &origin, &height, &kind, &backup) != 5) {
		free(label);
		return -1;
	}
	/* Optional trailing font name: the rest of the line, trimmed.
	 * Old files end the line here, giving an empty rest. */
	if (fgets(rest, sizeof rest, fp)) {
		char *p = rest, *end;
		while (*p == ' ' || *p == '\t')
			p++;
		end = p + strlen(p);
		while (end > p && (end[-1] == '\n' || end[-1] == '\r' ||
		    end[-1] == ' ' || end[-1] == '\t'))
			*--end = 0;
		if (*p)
			fontname = p;
	}
	if (session_add_win(s, col, label, dot, origin,
			height, kind, backup, fontname) != 0) {
		free(label);
		return -1;
	}
	free(label);
	return 0;
}

Session *
session_read(FILE *fp)
{
	char line[64], kw[32];
	Session *s;
	int col = -1;

	if (!fp)
		return 0;
	/* First line is the version stamp, on its own line. */
	if (!fgets(line, sizeof line, fp))
		return 0;
	if (strcmp(line, "wily-session 1\n") != 0 &&
	    strcmp(line, "wily-session 1") != 0)
		return 0;

	s = session_alloc();
	if (!s)
		return 0;
	for (;;) {
		if (fscanf(fp, "%31s", kw) != 1)
			break;	/* EOF: a clean end */
		if (strcmp(kw, "col") == 0) {
			int width;
			if (fscanf(fp, "%d", &width) != 1)
				goto bad;
			col = session_add_col(s, width);
			if (col < 0)
				goto bad;
		} else if (strcmp(kw, "theme") == 0) {
			/* Last one wins; allowed anywhere at top level. */
			if (fscanf(fp, "%31s", s->theme) != 1)
				goto bad;
		} else if (strcmp(kw, "win") == 0) {
			if (col < 0 || read_win(fp, s, col) != 0)
				goto bad;
		} else {
			goto bad;	/* unknown record */
		}
	}
	if (ferror(fp))
		goto bad;
	return s;

bad:
	session_free(s);
	return 0;
}
