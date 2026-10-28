/* Session file format for wily.
 *
 * This file is deliberately free of wily types and X dependencies so
 * it can be unit-tested standalone (see sessiontest.c). The wily
 * side (session.c) converts between tiles and these structs.
 *
 * Format, version 1. Labels are length-prefixed, so filenames with
 * spaces, tabs or newlines round-trip exactly:
 *
 *   wily-session 1
 *   col <width-permille>
 *   win <labellen> <label-bytes> <dot> <origin> <height-permille> <kind> <backup>
 *   ...
 *
 * One "col" line per column, followed by its "win" lines in order.
 * <kind> is 0 for a file/directory that exists on disk, 1 for a
 * window that was never saved (a "New" buffer). <backup> is the
 * index of the window's saved content in the session backup
 * directory, or -1 when the window is clean.
 */

#ifndef SESSIONFMT_H
#define SESSIONFMT_H

#include <stdio.h>

typedef struct SWin {
	char	*label;		/* window label, or the resolved path
				 * for a file on disk (see session.c) */
	unsigned long dot;	/* cursor, in runes */
	unsigned long origin;	/* first visible rune */
	int	height;		/* permille of column height */
	int	kind;		/* 0 = on disk, 1 = never saved */
	int	backup;		/* backup index, or -1 */
} SWin;

typedef struct SCol {
	int	width;		/* permille of total width */
	SWin	*wins;
	int	nwin, capwin;
} SCol;

typedef struct Session {
	SCol	*cols;
	int	ncol, capcol;
	char	theme[32];	/* "" when the file has no theme line */
} Session;

Session *session_alloc(void);
void	session_free(Session *s);
int	session_add_col(Session *s, int width);
int	session_add_win(Session *s, int col, const char *label,
			unsigned long dot, unsigned long origin,
			int height, int kind, int backup);
int	session_write(FILE *fp, Session *s);
Session *session_read(FILE *fp);

#endif /* SESSIONFMT_H */
