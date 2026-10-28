/* Standalone tests for sessionfmt.c. Built manually, NOT part of
 * the wily binary:
 *   cc -o /tmp/sessiontest wily/sessionfmt.c wily/sessiontest.c
 *   /tmp/sessiontest
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sessionfmt.h"

static int fails;

#define CHECK(c, msg) do { \
	if (!(c)) { printf("FAIL %s\n", msg); fails++; } \
} while (0)

int
main(void)
{
	Session *s, *r;
	FILE *fp;

	s = session_alloc();
	CHECK(s != 0, "alloc");
	CHECK(session_add_col(s, 600) == 0, "col0");
	CHECK(session_add_col(s, 400) == 1, "col1");
	CHECK(session_add_win(s, 0, "my file.md", 10, 5, 500, 0, -1) == 0,
		"win space label");
	CHECK(session_add_win(s, 0, "tab\tname", 0, 0, 500, 0, -1) == 0,
		"win tab label");
	CHECK(session_add_win(s, 1, "nl\nname", 42, 7, 1000, 1, 3) == 0,
		"win newline label");
	CHECK(session_add_win(s, 1, "", 0, 0, 0, 1, -1) == 0,
		"win empty label");

	fp = tmpfile();
	CHECK(fp != 0, "tmpfile");
	CHECK(session_write(fp, s) == 0, "write");
	rewind(fp);
	r = session_read(fp);
	CHECK(r != 0, "read back");
	if (r) {
		CHECK(r->ncol == 2, "ncol");
		CHECK(r->cols[0].width == 600, "width0");
		CHECK(r->cols[1].width == 400, "width1");
		CHECK(r->cols[0].nwin == 2, "nwin0");
		CHECK(r->cols[1].nwin == 2, "nwin1");
		CHECK(strcmp(r->cols[0].wins[0].label, "my file.md") == 0,
			"label space");
		CHECK(r->cols[0].wins[0].dot == 10, "dot");
		CHECK(r->cols[0].wins[0].origin == 5, "origin");
		CHECK(strcmp(r->cols[0].wins[1].label, "tab\tname") == 0,
			"label tab");
		CHECK(strcmp(r->cols[1].wins[0].label, "nl\nname") == 0,
			"label newline");
		CHECK(r->cols[1].wins[0].dot == 42, "dot2");
		CHECK(r->cols[1].wins[0].kind == 1, "kind");
		CHECK(r->cols[1].wins[0].backup == 3, "backup");
		CHECK(strcmp(r->cols[1].wins[1].label, "") == 0,
			"label empty");
	}
	fclose(fp);
	session_free(s);
	session_free(r);

	/* Bad version must be rejected. */
	fp = tmpfile();
	fputs("wily-session 2\ncol 100\n", fp);
	rewind(fp);
	CHECK(session_read(fp) == 0, "bad version rejected");
	fclose(fp);

	/* Truncated record must be rejected. */
	fp = tmpfile();
	fputs("wily-session 1\ncol 100\nwin 10 short\n", fp);
	rewind(fp);
	CHECK(session_read(fp) == 0, "truncated rejected");
	fclose(fp);

	/* Theme line round-trips, and a file without one reads
	 * back with an empty theme (old sessions stay valid).
	 */
	s = session_alloc();
	snprintf(s->theme, sizeof s->theme, "%s", "tokyo");
	CHECK(session_add_col(s, 1000) == 0, "theme col");
	fp = tmpfile();
	CHECK(session_write(fp, s) == 0, "theme write");
	rewind(fp);
	r = session_read(fp);
	CHECK(r != 0, "theme read back");
	if (r)
		CHECK(strcmp(r->theme, "tokyo") == 0, "theme value");
	fclose(fp);
	session_free(s);
	session_free(r);

	fp = tmpfile();
	fputs("wily-session 1\ncol 1000\n", fp);
	rewind(fp);
	r = session_read(fp);
	CHECK(r != 0, "no-theme read back");
	if (r)
		CHECK(r->theme[0] == '\0', "no-theme empty");
	fclose(fp);
	session_free(r);

	/* A theme line after the columns is still accepted. */
	fp = tmpfile();
	fputs("wily-session 1\ncol 1000\ntheme catppuccin\n", fp);
	rewind(fp);
	r = session_read(fp);
	CHECK(r != 0, "late theme read back");
	if (r)
		CHECK(strcmp(r->theme, "catppuccin") == 0, "late theme value");
	fclose(fp);
	session_free(r);

	if (fails) {
		printf("%d FAILURES\n", fails);
		return 1;
	}
	printf("all session format tests pass\n");
	return 0;
}
