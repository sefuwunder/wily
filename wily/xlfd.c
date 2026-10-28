/* xlfd.c - pure XLFD font-name manipulation for font sizing.
 *
 * An XLFD name has 14 dash-separated fields:
 *   -foundry-family-weight-slant-setwidth-addstyle-pixelsize-pointsize-
 *    resx-resy-spacing-avgwidth-registry-encoding
 * Pixel size is field 7, point size (decipoints) field 8.
 * No X calls here, so this file is testable standalone (see
 * fontsizetest.c). Non-XLFD names (aliases like "fixed", Plan 9 font
 * file paths) are rejected: there is no size field to step.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Split an XLFD name into its 14 fields. fields[i] points into a copy
 * owned by the caller (see xlfd_free_fields). Returns 0, or -1 when
 * the name is not a 14-field XLFD name. */
int
xlfd_split(const char *xlfd, char *fields[14], char **copy_out)
{
	char *copy, *p, *dash;
	int i;

	if (!xlfd || xlfd[0] != '-')
		return -1;
	copy = strdup(xlfd);
	if (!copy)
		return -1;
	/* Field 0 (before the first dash) must be empty. */
	p = copy + 1;
	for (i = 0; i < 14; i++) {
		fields[i] = p;
		dash = strchr(p, '-');
		if (i < 13) {
			if (!dash) {
				free(copy);
				return -1;
			}
			*dash = 0;
			p = dash + 1;
		} else {
			/* Last field runs to the end; it must not contain
			 * another dash (that would be a 15th field). */
			if (dash) {
				free(copy);
				return -1;
			}
		}
	}
	*copy_out = copy;
	return 0;
}

void
xlfd_free_fields(char *copy)
{
	free(copy);
}

static int
num_field(const char *s, int *out)
{
	/* "*" and "" mean "unspecified" in scalable names. */
	if (!s || !*s || strcmp(s, "*") == 0) {
		*out = 0;
		return 0;
	}
	for (const char *p = s; *p; p++)
		if (!isdigit((unsigned char)*p))
			return -1;
	*out = atoi(s);
	return 0;
}

/* Extract pixel and point sizes. Returns 0, or -1 for non-XLFD or
 * non-numeric size fields. */
int
xlfd_sizes(const char *xlfd, int *pixel, int *point)
{
	char *fields[14], *copy;
	int r;

	if (xlfd_split(xlfd, fields, &copy) < 0)
		return -1;
	r = num_field(fields[6], pixel) < 0 ||
	    num_field(fields[7], point) < 0 ? -1 : 0;
	free(copy);
	return r;
}

/* Replace one field (0-based) of an XLFD name with a new value.
 * Returns a malloc'd name, or NULL. */
char *
xlfd_replace_field(const char *xlfd, int idx, const char *value)
{
	char *fields[14], *copy, *out;
	size_t len;
	int i;

	if (idx < 0 || idx > 13)
		return NULL;
	if (xlfd_split(xlfd, fields, &copy) < 0)
		return NULL;
	len = strlen(xlfd) + strlen(value) + 1;
	out = malloc(len);
	if (!out) {
		free(copy);
		return NULL;
	}
	{
		char *f[14];
		for (i = 0; i < 14; i++)
			f[i] = strdup(fields[i]);
		free(copy);
		free(f[idx]);
		f[idx] = strdup(value);
		snprintf(out, len, "-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s-%s",
		    f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7],
		    f[8], f[9], f[10], f[11], f[12], f[13]);
		for (i = 0; i < 14; i++)
			free(f[i]);
	}
	return out;
}

/* Rebuild an XLFD name with a new pixel size. The point size scales
 * proportionally for bitmap fonts (14px/140 -> 18px/180); for scalable
 * names (pixel 0, or point "*"/0) the point field is left alone
 * unless the old pixel was 0, in which case point becomes 0 (the
 * standard scalable form). Returns a malloc'd name, or NULL. */
char *
xlfd_with_pixel(const char *xlfd, int new_pixel)
{
	int old_pixel, old_point;
	char pixbuf[16], ptbuf[16], *out, *tmp;

	if (xlfd_sizes(xlfd, &old_pixel, &old_point) < 0)
		return NULL;
	snprintf(pixbuf, sizeof pixbuf, "%d", new_pixel);
	if (old_pixel > 0 && old_point > 0) {
		/* Bitmap face: scale the point size with the pixels. */
		snprintf(ptbuf, sizeof ptbuf, "%d",
		    old_point * new_pixel / old_pixel);
	} else if (old_pixel == 0) {
		/* Scalable name: point becomes 0 (standard form). */
		snprintf(ptbuf, sizeof ptbuf, "%d", 0);
	} else {
		/* old_point is 0 or "*": keep the written token. */
		char *fields[14], *copy;
		if (xlfd_split(xlfd, fields, &copy) < 0)
			return NULL;
		snprintf(ptbuf, sizeof ptbuf, "%s", fields[7]);
		free(copy);
	}
	tmp = xlfd_replace_field(xlfd, 6, pixbuf);
	if (!tmp)
		return NULL;
	out = xlfd_replace_field(tmp, 7, ptbuf);
	free(tmp);
	return out;
}
