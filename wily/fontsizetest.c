/* fontsizetest.c - standalone tests for xlfd.c. Built manually, NOT
 * part of the wily binary:
 *   cc -o /tmp/fontsizetest wily/xlfd.c wily/fontsizetest.c
 *   /tmp/fontsizetest
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int xlfd_split(const char *xlfd, char *fields[14], char **copy_out);
void xlfd_free_fields(char *copy);
int xlfd_sizes(const char *xlfd, int *pixel, int *point);
char *xlfd_with_pixel(const char *xlfd, int new_pixel);
char *xlfd_replace_field(const char *xlfd, int idx, const char *value);

static int fails;

#define CHECK(c, msg) do { \
	if (!(c)) { printf("FAIL %s\n", msg); fails++; } \
} while (0)

int
main(void)
{
	char *fields[14], *copy, *out;
	int pixel, point;

	/* Standard bitmap XLFD. */
	CHECK(xlfd_sizes("-misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1",
	    &pixel, &point) == 0, "bitmap sizes parse");
	CHECK(pixel == 14 && point == 130, "bitmap sizes values");

	/* Pixel step scales the point size proportionally. */
	out = xlfd_with_pixel("-misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1", 18);
	CHECK(out != NULL, "step up builds");
	if (out) {
		CHECK(strcmp(out, "-misc-fixed-medium-r-normal--18-167-75-75-c-70-iso8859-1") == 0,
		    "step up scales point");
		CHECK(xlfd_sizes(out, &pixel, &point) == 0 &&
		    pixel == 18, "stepped name re-parses");
		free(out);
	}
	out = xlfd_with_pixel("-misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1", 10);
	CHECK(out != NULL &&
	    strcmp(out, "-misc-fixed-medium-r-normal--10-92-75-75-c-70-iso8859-1") == 0,
	    "step down scales point");
	free(out);

	/* Empty addstyle field (double dash) is fine. */
	CHECK(xlfd_sizes("-misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1",
	    &pixel, &point) == 0, "empty field ok");

	/* Replace one field. */
	out = xlfd_replace_field("-misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1", 6, "*");
	CHECK(out != NULL &&
	    strcmp(out, "-misc-fixed-medium-r-normal--*-130-75-75-c-70-iso8859-1") == 0,
	    "replace pixel with star");
	free(out);
	out = xlfd_replace_field("-misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1", 13, "2");
	CHECK(out != NULL &&
	    strcmp(out, "-misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-2") == 0,
	    "replace encoding");
	free(out);
	CHECK(xlfd_replace_field("fixed", 6, "*") == NULL,
	    "replace on alias rejected");
	CHECK(xlfd_replace_field("-misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1", 14, "x") == NULL,
	    "replace out of range rejected");
	CHECK(xlfd_sizes("fixed", &pixel, &point) < 0, "alias rejected");
	CHECK(xlfd_sizes("variable", &pixel, &point) < 0, "variable rejected");
	CHECK(xlfd_sizes("/lib/font/bit/lucida/unicode.10.font",
	    &pixel, &point) < 0, "plan9 path rejected");
	CHECK(xlfd_sizes("-misc-fixed-medium-r-normal--14-130-75-75-c-70",
	    &pixel, &point) < 0, "13 fields rejected");
	CHECK(xlfd_sizes("-misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1-x",
	    &pixel, &point) < 0, "15 fields rejected");
	CHECK(xlfd_sizes("-misc-fixed-medium-r-normal--ab-130-75-75-c-70-iso8859-1",
	    &pixel, &point) < 0, "non-numeric pixel rejected");
	CHECK(xlfd_with_pixel("fixed", 18) == NULL, "alias step rejected");

	/* Scalable name: pixel 0 -> point becomes 0. */
	out = xlfd_with_pixel("-*-helvetica-medium-r-normal-*-0-0-0-0-p-0-iso8859-1", 14);
	CHECK(out != NULL &&
	    strcmp(out, "-*-helvetica-medium-r-normal-*-14-0-0-0-p-0-iso8859-1") == 0,
	    "scalable step");
	free(out);

	/* Split exposes all 14 fields. */
	CHECK(xlfd_split("-misc-fixed-medium-r-normal--14-130-75-75-c-70-iso8859-1",
	    fields, &copy) == 0, "split ok");
	if (copy) {
		CHECK(strcmp(fields[0], "misc") == 0, "field foundry");
		CHECK(strcmp(fields[1], "fixed") == 0, "field family");
		CHECK(strcmp(fields[5], "") == 0, "field addstyle empty");
		CHECK(strcmp(fields[6], "14") == 0, "field pixel");
		CHECK(strcmp(fields[12], "iso8859") == 0, "field registry");
		CHECK(strcmp(fields[13], "1") == 0, "field encoding");
		xlfd_free_fields(copy);
	}

	if (fails) {
		printf("%d FAILURES\n", fails);
		return 1;
	}
	printf("all xlfd tests pass\n");
	return 0;
}
