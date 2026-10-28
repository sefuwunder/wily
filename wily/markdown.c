/*******************************************
 *	Markdown rendering to plain text
 *
 * Wily's frames carry a single font, so rich per-run styling
 * (bold, heading sizes) is not expressible. Instead, Markdown
 * renders to typographic plain text, pandoc-plain style:
 *
 *   headings    -> text with === (h1) or --- (h2) underline,
 *                  or a ### prefix for h3-h6
 *   emphasis    -> markers stripped, text kept
 *   code spans  -> markers stripped, text kept
 *   code blocks -> indented 4 spaces
 *   links       -> text (url)
 *   images      -> alt (url)
 *   lists       -> - / N. markers, 2-space nesting
 *   blockquotes -> 2-space indent per level
 *   rules       -> ---
 *   tables      -> passed through as-is
 *
 * Reference links ([text][id] + [id]: url) are resolved.
 * The result is plain text for a derived, read-only window;
 * it is not a full CommonMark implementation (same-type nested
 * emphasis and a few other corners are approximated).
 *
 * Pure string in -> string out; no wily types, no X.
 *******************************************/

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

typedef struct {
	char	*buf;
	size_t	len, cap;
} Str;

static int
sb_reserve(Str *s, size_t need)
{
	char *nb;

	if (s->len + need + 1 <= s->cap)
		return 0;
	need = (s->len + need + 1) * 2;
	nb = realloc(s->buf, need);
	if (!nb)
		return -1;
	s->buf = nb;
	s->cap = need;
	return 0;
}

static int
sb_putn(Str *s, const char *p, size_t n)
{
	if (sb_reserve(s, n) != 0)
		return -1;
	memcpy(s->buf + s->len, p, n);
	s->len += n;
	s->buf[s->len] = '\0';
	return 0;
}

static int
sb_puts(Str *s, const char *p)
{
	return sb_putn(s, p, strlen(p));
}

static int
sb_putc(Str *s, char c)
{
	return sb_putn(s, &c, 1);
}

/* Is 'label' a markdown file? Used to offer the Markdown command
 * in the tag of markdown windows.
 */
int
markdown_is_filename(const char *label)
{
	const char *dot, *ext;
	size_t n;

	if (!label)
		return 0;
	dot = strrchr(label, '.');
	if (!dot)
		return 0;
	ext = dot + 1;
	n = strlen(ext);
	if (n == 2 && (ext[0] == 'm' || ext[0] == 'M') &&
	    (ext[1] == 'd' || ext[1] == 'D'))
		return 1;
	if (n == 8 && strncasecmp(ext, "markdown", 8) == 0)
		return 1;
	return 0;
}

/* ---------- lines ---------- */

typedef struct {
	const char	*p;
	int		len;
} Line;

/* Split text into lines (no copies; lines point into src).
 * Returns the line array, or 0 on failure.
 */
static Line *
split_lines(const char *src, int *nlines)
{
	int count = 1, i = 0, start = 0, len = strlen(src);
	Line *lines;
	const char *p;

	for (p = src; *p; p++)
		if (*p == '\n')
			count++;
	lines = malloc(sizeof(Line) * count);
	if (!lines)
		return 0;
	for (i = 0; i < len; i++) {
		if (src[i] == '\n') {
			int l = i - start;
			if (l > 0 && src[i - 1] == '\r')
				l--;
			lines[*nlines].p = src + start;
			lines[*nlines].len = l;
			(*nlines)++;
			start = i + 1;
		}
	}
	if (start < len || len == 0) {
		int l = len - start;
		if (l > 0 && src[len - 1] == '\r')
			l--;
		lines[*nlines].p = src + start;
		lines[*nlines].len = l;
		(*nlines)++;
	}
	return lines;
}

/* Line helpers: leading spaces, trimmed view, blank test. */
static int
lindent(Line *l)
{
	int i = 0;
	while (i < l->len && (l->p[i] == ' ' || l->p[i] == '\t'))
		i++;
	return i;
}

static int
lblank(Line *l)
{
	return lindent(l) == l->len;
}

/* Trim leading/trailing spaces (not tabs) into a pointer/length pair. */
static void
ltrim(Line *l, const char **p, int *len)
{
	int a = 0, b = l->len;
	while (a < b && l->p[a] == ' ')
		a++;
	while (b > a && (l->p[b - 1] == ' ' || l->p[b - 1] == '\t'))
		b--;
	*p = l->p + a;
	*len = b - a;
}

/* ---------- link references ---------- */

typedef struct {
	char	*id;	/* lowercased */
	char	*url;
} Ref;

static Ref *refs;
static int nrefs, caprefs;

static void
refs_free(void)
{
	int i;
	for (i = 0; i < nrefs; i++) {
		free(refs[i].id);
		free(refs[i].url);
	}
	free(refs);
	refs = 0;
	nrefs = caprefs = 0;
}

static void
lower(char *s)
{
	for (; *s; s++)
		*s = tolower((unsigned char)*s);
}

static char *
ref_lookup(const char *id, int len)
{
	char *tmp;
	int i;

	tmp = malloc(len + 1);
	if (!tmp)
		return 0;
	memcpy(tmp, id, len);
	tmp[len] = '\0';
	lower(tmp);
	for (i = 0; i < nrefs; i++) {
		if (strcmp(refs[i].id, tmp) == 0) {
			free(tmp);
			return refs[i].url;
		}
	}
	free(tmp);
	return 0;
}

/* Try to parse a link reference definition line. Returns 1 if the
 * line is a definition (caller skips it), 0 otherwise.
 */
static int
ref_define(Line *l)
{
	int i = lindent(l), j, k;
	const char *id, *url;
	int idlen, urllen;
	char *u;

	if (i > 3 || i >= l->len || l->p[i] != '[')
		return 0;
	j = i + 1;
	while (j < l->len && l->p[j] != ']')
		j++;
	if (j >= l->len || j == i + 1)
		return 0;
	if (j + 1 >= l->len || l->p[j + 1] != ':')
		return 0;
	id = l->p + i + 1;
	idlen = j - (i + 1);
	k = j + 2;
	while (k < l->len && (l->p[k] == ' ' || l->p[k] == '\t'))
		k++;
	if (k >= l->len)
		return 0;
	if (l->p[k] == '<') {
		int e = k + 1;
		while (e < l->len && l->p[e] != '>')
			e++;
		if (e >= l->len)
			return 0;
		url = l->p + k + 1;
		urllen = e - (k + 1);
	} else {
		int e = k;
		while (e < l->len && l->p[e] != ' ' && l->p[e] != '\t')
			e++;
		url = l->p + k;
		urllen = e - k;
	}
	if (urllen == 0)
		return 0;
	if (nrefs == caprefs) {
		int nc = caprefs ? caprefs * 2 : 8;
		Ref *nr = realloc(refs, nc * sizeof(Ref));
		if (!nr)
			return 0;
		refs = nr;
		caprefs = nc;
	}
	u = malloc(urllen + 1);
	if (!u)
		return 0;
	memcpy(u, url, urllen);
	u[urllen] = '\0';
	refs[nrefs].id = malloc(idlen + 1);
	if (!refs[nrefs].id) {
		free(u);
		return 0;
	}
	memcpy(refs[nrefs].id, id, idlen);
	refs[nrefs].id[idlen] = '\0';
	lower(refs[nrefs].id);
	refs[nrefs].url = u;
	nrefs++;
	return 1;
}

/* ---------- inline rendering ---------- */

static int render_inline(const char *s, int len, Str *out);

/* Find the closing run of exactly n ticks starting at or after i.
 * Returns the index of the run, or -1.
 */
static int
find_ticks(const char *s, int len, int i, int n)
{
	int j;
	for (j = i; j < len; j++) {
		if (s[j] == '`') {
			int k = j, cnt = 0;
			while (k < len && s[k] == '`') {
				k++;
				cnt++;
			}
			if (cnt == n)
				return j;
			j = k - 1;
		}
	}
	return -1;
}

/* Find the closing marker, skipping over code spans. */
static int
find_marker(const char *s, int len, int i, const char *m, int mlen)
{
	int j;
	for (j = i; j < len; j++) {
		if (s[j] == '`') {
			int k = j, cnt = 0;
			while (k < len && s[k] == '`') {
				k++;
				cnt++;
			}
			k = find_ticks(s, len, k, cnt);
			if (k < 0)
				return -1;
			j = k + cnt - 1;
			continue;
		}
		if (j + mlen <= len && memcmp(s + j, m, mlen) == 0)
			return j;
	}
	return -1;
}

/* Parse "[text](url)" or "[text][id]" / "[text][]" / "[text]" at s.
 * On success returns the index just past the construct and fills
 * the text span and url (malloc'd, or 0 for a shortcut link that
 * did not resolve). On failure returns -1.
 */
static int
parse_link(const char *s, int len, const char **text, int *tlen,
		char **url)
{
	int depth = 0, j, close = -1;

	*url = 0;
	for (j = 1; j < len; j++) {
		if (s[j] == '\\' && j + 1 < len) {
			j++;
			continue;
		}
		if (s[j] == '[')
			depth++;
		if (s[j] == ']') {
			if (depth == 0) {
				close = j;
				break;
			}
			depth--;
		}
	}
	if (close < 0)
		return -1;
	*text = s + 1;
	*tlen = close - 1;
	if (close + 1 < len && s[close + 1] == '(') {
		/* Inline link: [text](url) or [text](<url>) */
		int k = close + 2, e;
		while (k < len && (s[k] == ' ' || s[k] == '\t'))
			k++;
		if (k < len && s[k] == '<') {
			e = k + 1;
			while (e < len && s[e] != '>')
				e++;
			if (e >= len)
				return -1;
			*url = malloc(e - (k + 1) + 1);
			if (!*url)
				return -1;
			memcpy(*url, s + k + 1, e - (k + 1));
			(*url)[e - (k + 1)] = '\0';
			e++;
		} else {
			int depth2 = 0;
			e = k;
			while (e < len) {
				if (s[e] == '(')
					depth2++;
				if (s[e] == ')') {
					if (depth2 == 0)
						break;
					depth2--;
				}
				if (s[e] == ' ' && depth2 == 0)
					break;	/* title follows; stop */
				e++;
			}
			if (e >= len)
				return -1;
			*url = malloc(e - k + 1);
			if (!*url)
				return -1;
			memcpy(*url, s + k, e - k);
			(*url)[e - k] = '\0';
		}
		while (e < len && s[e] != ')')
			e++;
		if (e >= len)
			return -1;
		return e + 1;
	}
	if (close + 1 < len && s[close + 1] == '[') {
		int k = close + 2, e = k;
		while (e < len && s[e] != ']')
			e++;
		if (e >= len)
			return -1;
		if (e == k) {
			/* Collapsed: [text][] -> id is text */
			*url = ref_lookup(*text, *tlen);
			if (*url) {
				char *u = strdup(*url);
				*url = u;
			}
		} else {
			char *r = ref_lookup(s + k, e - k);
			if (r)
				*url = strdup(r);
		}
		return e + 1;
	}
	/* Shortcut: [text] -> id is text */
	{
		char *r = ref_lookup(*text, *tlen);
		if (r)
			*url = strdup(r);
	}
	return close + 1;
}

static int
render_inline(const char *s, int len, Str *out)
{
	int i = 0;

	while (i < len) {
		char c = s[i];
		if (c == '\\' && i + 1 < len &&
		    strchr("`\\*_{}[]()#+-.!<>", s[i + 1])) {
			if (sb_putc(out, s[i + 1]) != 0)
				return -1;
			i += 2;
			continue;
		}
		if (c == '`') {
			int j = i, cnt = 0, close;
			while (j < len && s[j] == '`') {
				j++;
				cnt++;
			}
			close = find_ticks(s, len, j, cnt);
			if (close < 0) {
				if (sb_putc(out, '`') != 0)
					return -1;
				i++;
				continue;
			}
			/* Strip one surrounding space, CommonMark style. */
			{
				int a = j, b = close;
				if (b - a > 1 && s[a] == ' ' && s[b - 1] == ' ')
					a++, b--;
				if (sb_putn(out, s + a, b - a) != 0)
					return -1;
			}
			i = close + cnt;
			continue;
		}
		if (c == '!' && i + 1 < len && s[i + 1] == '[') {
			const char *text;
			int tlen;
			char *url = 0;
			int next = parse_link(s + i + 1, len - (i + 1),
				&text, &tlen, &url);
			if (next > 0) {
				if (render_inline(text, tlen, out) != 0)
					return -1;
				if (url) {
					if (sb_puts(out, " (") != 0 ||
					    sb_puts(out, url) != 0 ||
					    sb_putc(out, ')') != 0)
						return -1;
					free(url);
				}
				i += 1 + next;
				continue;
			}
			if (sb_putc(out, '!') != 0)
				return -1;
			i++;
			continue;
		}
		if (c == '[') {
			const char *text;
			int tlen;
			char *url = 0;
			int next = parse_link(s + i, len - i,
				&text, &tlen, &url);
			if (next > 0) {
				if (render_inline(text, tlen, out) != 0)
					return -1;
				if (url) {
					if (sb_puts(out, " (") != 0 ||
					    sb_puts(out, url) != 0 ||
					    sb_putc(out, ')') != 0)
						return -1;
					free(url);
				}
				i += next;
				continue;
			}
			if (sb_putc(out, '[') != 0)
				return -1;
			i++;
			continue;
		}
		if (c == '<') {
			/* Autolink <scheme:...> / <mail@host>, else drop
			 * an HTML tag, else a literal '<'.
			 */
			int e = i + 1;
			while (e < len && s[e] != '>' && s[e] != ' ')
				e++;
			if (e < len && s[e] == '>' && e > i + 1) {
				int inner = e - (i + 1);
				if (memchr(s + i + 1, ':', inner) ||
				    memchr(s + i + 1, '@', inner)) {
					if (sb_putn(out, s + i + 1, inner) != 0)
						return -1;
					i = e + 1;
					continue;
				}
				if (isalpha((unsigned char)s[i + 1]) ||
				    s[i + 1] == '/' || s[i + 1] == '!') {
					/* HTML tag or comment: drop it. */
					int cmt = (i + 3 < len &&
						memcmp(s + i, "<!--", 4) == 0);
					if (cmt) {
						int k = i + 4;
						while (k + 2 < len &&
						    memcmp(s + k, "-->", 3) != 0)
							k++;
						i = (k + 2 < len) ? k + 3 : len;
					} else {
						i = e + 1;
					}
					continue;
				}
			}
			if (sb_putc(out, '<') != 0)
				return -1;
			i++;
			continue;
		}
		if ((c == '*' || c == '~') && i + 1 < len) {
			int mlen = 1;
			const char *m = s + i;
			char mbuf[4];
			int close;
			if (s[i + 1] == c)
				mlen = 2;
			if (mlen == 2 && i + 2 < len && s[i + 2] == c &&
			    c == '*')
				mlen = 3;
			memcpy(mbuf, m, mlen);
			mbuf[mlen] = '\0';
			close = find_marker(s, len, i + mlen, mbuf, mlen);
			if (close >= 0) {
				if (render_inline(s + i + mlen,
						close - (i + mlen), out) != 0)
					return -1;
				i = close + mlen;
				continue;
			}
			if (sb_putc(out, c) != 0)
				return -1;
			i++;
			continue;
		}
		if (c == '_' && i + 1 < len) {
			/* Intraword underscores are literal: _ only
			 * marks emphasis at a word boundary.
			 */
			int prev_ok = (i == 0) ||
				!(isalnum((unsigned char)s[i - 1]));
			int next_ok = isalnum((unsigned char)s[i + 1]);
			int mlen = 1, close;
			if (!prev_ok || !next_ok) {
				if (sb_putc(out, '_') != 0)
					return -1;
				i++;
				continue;
			}
			if (s[i + 1] == '_')
				mlen = 2;
			/* Closing _ needs a following boundary. */
			close = -1;
			{
				int j;
				for (j = i + mlen; j < len; j++) {
					if (s[j] == '`') {
						int k = j, cnt = 0;
						while (k < len && s[k] == '`') {
							k++;
							cnt++;
						}
						k = find_ticks(s, len, k, cnt);
						if (k < 0)
							break;
						j = k + cnt - 1;
						continue;
					}
					if (j + mlen <= len &&
					    memcmp(s + j,
						mlen == 1 ? "_" : "__",
						mlen) == 0) {
						int after = j + mlen;
						if (after >= len ||
						    !isalnum((unsigned char)s[after])) {
							close = j;
							break;
						}
					}
				}
			}
			if (close >= 0) {
				if (render_inline(s + i + mlen,
						close - (i + mlen), out) != 0)
					return -1;
				i = close + mlen;
				continue;
			}
			if (sb_putc(out, '_') != 0)
				return -1;
			i++;
			continue;
		}
		if (sb_putc(out, c) != 0)
			return -1;
		i++;
	}
	return 0;
}

/* ---------- block rendering ---------- */

static int render_blocks(Line *lines, int n, Str *out);
static int render_blocks_from_text(const char *text, Str *out);

static int
emit_blank(Str *out, int *need_blank)
{
	if (*need_blank) {
		if (sb_putc(out, '\n') != 0)
			return -1;
		*need_blank = 0;
	}
	return 0;
}

/* A line of 3+ identical -, *, _ (spaces allowed) is a rule. */
static int
is_hr(Line *l)
{
	const char *p;
	int len, i, cnt = 0;
	char m = 0;

	ltrim(l, &p, &len);
	if (len < 3)
		return 0;
	for (i = 0; i < len; i++) {
		if (p[i] == ' ' || p[i] == '\t')
			continue;
		if (p[i] != '-' && p[i] != '*' && p[i] != '_')
			return 0;
		if (!m)
			m = p[i];
		if (p[i] != m)
			return 0;
		cnt++;
	}
	return cnt >= 3;
}

/* ATX heading: returns level 0 if not a heading. Fills text span. */
static int
atx_heading(Line *l, const char **text, int *tlen)
{
	int ind = lindent(l), i, level = 0;
	const char *p;
	int len;

	if (ind > 3)
		return 0;
	i = ind;
	while (i < l->len && l->p[i] == '#') {
		level++;
		i++;
	}
	if (level == 0 || level > 6)
		return 0;
	if (i >= l->len || (l->p[i] != ' ' && l->p[i] != '\t'))
		return 0;
	while (i < l->len && (l->p[i] == ' ' || l->p[i] == '\t'))
		i++;
	p = l->p + i;
	len = l->len - i;
	while (len > 0 && (p[len - 1] == ' ' || p[len - 1] == '\t'))
		len--;
	/* Closing sequence of #'s. */
	while (len > 1 && p[len - 1] == '#') {
		int k = len - 1;
		while (k > 0 && p[k - 1] == '#')
			k--;
		if (k > 0 && (p[k - 1] == ' ' || p[k - 1] == '\t')) {
			len = k;
			while (len > 0 &&
			    (p[len - 1] == ' ' || p[len - 1] == '\t'))
				len--;
		}
		break;
	}
	*text = p;
	*tlen = len;
	return level;
}

/* Setext underline: 1 for ===, 2 for ---, 0 otherwise. */
static int
setext_line(Line *l)
{
	const char *p;
	int len, i, cnt = 0;
	char m = 0;

	ltrim(l, &p, &len);
	if (len < 1)
		return 0;
	for (i = 0; i < len; i++) {
		if (p[i] == ' ' || p[i] == '\t')
			continue;
		if (p[i] != '=' && p[i] != '-')
			return 0;
		if (!m)
			m = p[i];
		if (p[i] != m)
			return 0;
		cnt++;
	}
	if (cnt < 1)
		return 0;
	return m == '=' ? 1 : 2;
}

/* Fence opener: returns fence char (` or ~) and length, else 0. */
static int
fence_open(Line *l, char *ch, int *flen)
{
	int ind = lindent(l), i, cnt = 0;
	char c;

	if (ind > 3)
		return 0;
	i = ind;
	c = l->p[i];
	if (c != '`' && c != '~')
		return 0;
	while (i < l->len && l->p[i] == c) {
		i++;
		cnt++;
	}
	if (cnt < 3)
		return 0;
	*ch = c;
	*flen = cnt;
	return 1;
}

static int
fence_close(Line *l, char ch, int flen)
{
	int ind = lindent(l), i, cnt = 0;

	if (ind > 3)
		return 0;
	i = ind;
	while (i < l->len && l->p[i] == ch) {
		i++;
		cnt++;
	}
	if (cnt < flen)
		return 0;
	while (i < l->len && (l->p[i] == ' ' || l->p[i] == '\t'))
		i++;
	return i == l->len;
}

/* Table delimiter row: | --- | :---: | etc. */
static int
is_delim_row(Line *l)
{
	const char *p;
	int len, i, dashes = 0;

	ltrim(l, &p, &len);
	for (i = 0; i < len; i++) {
		char c = p[i];
		if (c == '|' || c == ':' || c == ' ' || c == '\t')
			continue;
		if (c == '-') {
			dashes++;
			continue;
		}
		return 0;
	}
	return dashes > 0;
}

/* Quote marker: returns depth (0 if not a quote line) and strips
 * markers into *inner (a Line view of the remainder).
 */
static int
quote_depth(Line *l, Line *inner)
{
	int i = 0, depth = 0;

	for (;;) {
		while (i < l->len && l->p[i] == ' ')
			i++;
		if (i < l->len && l->p[i] == '>') {
			depth++;
			i++;
			if (i < l->len && l->p[i] == ' ')
				i++;
			continue;
		}
		break;
	}
	inner->p = l->p + i;
	inner->len = l->len - i;
	return depth;
}

/* List marker: returns 1 and fills indent, ordered, number, content. */
static int
list_marker(Line *l, int *indent, int *ordered, int *number,
		const char **content, int *clen)
{
	int ind = lindent(l), i = ind;
	const char *p;
	int len;

	if (ind > 3 && 0)
		return 0;
	p = l->p;
	len = l->len;
	if (i < len && (p[i] == '-' || p[i] == '*' || p[i] == '+') &&
	    i + 1 < len && (p[i + 1] == ' ' || p[i + 1] == '\t')) {
		i++;
		while (i < len && (p[i] == ' ' || p[i] == '\t'))
			i++;
		*indent = ind;
		*ordered = 0;
		*number = 0;
		*content = p + i;
		*clen = len - i;
		return 1;
	}
	if (i < len && isdigit((unsigned char)p[i])) {
		int num = 0, j = i;
		while (j < len && isdigit((unsigned char)p[j])) {
			num = num * 10 + (p[j] - '0');
			j++;
		}
		if (j < len && (p[j] == '.' || p[j] == ')') &&
		    j + 1 < len && (p[j + 1] == ' ' || p[j + 1] == '\t')) {
			j++;
			while (j < len && (p[j] == ' ' || p[j] == '\t'))
				j++;
			*indent = ind;
			*ordered = 1;
			*number = num;
			*content = p + j;
			*clen = len - j;
			return 1;
		}
	}
	return 0;
}

static int
render_blocks(Line *lines, int n, Str *out)
{
	int i = 0, need_blank = 0;
	int in_fence = 0, fence_len = 0;
	char fence_ch = 0;
	int in_code = 0;

	while (i < n) {
		Line *l = &lines[i];

		if (in_fence) {
			if (fence_close(l, fence_ch, fence_len)) {
				in_fence = 0;
				need_blank = 1;
			} else {
				if (emit_blank(out, &need_blank) != 0)
					return -1;
				if (sb_puts(out, "    ") != 0 ||
				    sb_putn(out, l->p, l->len) != 0 ||
				    sb_putc(out, '\n') != 0)
					return -1;
			}
			i++;
			continue;
		}
		if (lblank(l)) {
			/* A blank line ends a paragraph and an
			 * indented code block's run of text, but
			 * indented code resumes after blanks only
			 * if the next line is indented too.
			 */
			need_blank = 1;
			in_code = 0;
			i++;
			continue;
		}
		{
			char ch;
			int flen;
			if (fence_open(l, &ch, &flen)) {
				in_fence = 1;
				fence_ch = ch;
				fence_len = flen;
				i++;
				continue;
			}
		}
		if (in_code || (lindent(l) >= 4 &&
		    (i == 0 || lblank(&lines[i - 1])))) {
			/* Indented code block. */
			in_code = 1;
			if (emit_blank(out, &need_blank) != 0)
				return -1;
			if (sb_putn(out, l->p, l->len) != 0 ||
			    sb_putc(out, '\n') != 0)
				return -1;
			i++;
			continue;
		}
		in_code = 0;
		{
			const char *text;
			int tlen, level = atx_heading(l, &text, &tlen);
			if (level > 0) {
				if (emit_blank(out, &need_blank) != 0)
					return -1;
				if (level <= 2) {
					if (render_inline(text, tlen, out) != 0)
						return -1;
					if (sb_putc(out, '\n') != 0)
						return -1;
					if (sb_puts(out, level == 1 ?
						"===\n" : "---\n") != 0)
						return -1;
				} else {
					int k;
					for (k = 0; k < level; k++)
						if (sb_putc(out, '#') != 0)
							return -1;
					if (sb_putc(out, ' ') != 0 ||
					    render_inline(text, tlen, out) != 0 ||
					    sb_putc(out, '\n') != 0)
						return -1;
				}
				need_blank = 1;
				i++;
				continue;
			}
		}
		{
			/* Blockquote: gather the run, render the
			 * stripped interior, indent by 2 per level.
			 */
			Line inner;
			int depth = quote_depth(l, &inner);
			if (depth > 0) {
				int j = i;
				Str sub = {0}, joined = {0};
				/* Collect consecutive quote lines. */
				while (j < n) {
					Line in2;
					int d2 = quote_depth(&lines[j], &in2);
					if (d2 == 0 && !lblank(&lines[j]))
						break;
					if (d2 > 0) {
						if (sb_putn(&joined, in2.p,
								in2.len) != 0 ||
						    sb_putc(&joined, '\n') != 0)
							return -1;
					} else {
						/* Blank line inside a quote. */
						if (sb_putc(&joined, '\n') != 0)
							return -1;
					}
					j++;
				}
				if (render_blocks_from_text(joined.buf ?
						joined.buf : "", &sub) != 0) {
					free(joined.buf);
					free(sub.buf);
					return -1;
				}
				free(joined.buf);
				if (emit_blank(out, &need_blank) != 0) {
					free(sub.buf);
					return -1;
				}
				/* Indent every output line by depth. */
				{
					char *p = sub.buf;
					int ind;
					for (ind = 0; ind < depth * 2; ind++)
						if (sb_putc(out, ' ') != 0) {
							free(sub.buf);
							return -1;
						}
					while (p && *p) {
						char *nl = strchr(p, '\n');
						if (!nl) {
							if (sb_puts(out, p) != 0) {
								free(sub.buf);
								return -1;
							}
							break;
						}
						if (sb_putn(out, p, nl - p + 1) != 0) {
							free(sub.buf);
							return -1;
						}
						p = nl + 1;
						if (*p) {
							for (ind = 0;
							     ind < depth * 2; ind++)
								if (sb_putc(out, ' ') != 0) {
									free(sub.buf);
									return -1;
								}
						}
					}
				}
				free(sub.buf);
				need_blank = 1;
				i = j;
				continue;
			}
		}
		{
			/* List: gather the block, render items. */
			int indent, ordered, number;
			const char *content;
			int clen;
			if (list_marker(l, &indent, &ordered, &number,
					&content, &clen)) {
				int j = i;
				int stack_ind[32], stack_cnt[32], sp = 0;
				int loose = 0, first = 1;
				/* Items are rendered as encountered. */
				while (j < n) {
					Line *lj = &lines[j];
					int ind2, ord2, num2;
					const char *c2;
					int cl2;
					if (lblank(lj)) {
						/* Loose list if a marker
						 * follows the blank.
						 */
						int k = j + 1;
						while (k < n && lblank(&lines[k]))
							k++;
						if (k < n && list_marker(&lines[k],
								&ind2, &ord2, &num2,
								&c2, &cl2)) {
							loose = 1;
							j = k;
							continue;
						}
						break;
					}
					if (!list_marker(lj, &ind2, &ord2, &num2,
							&c2, &cl2)) {
						/* Continuation: append to the
						 * previous item's line.
						 */
						const char *tp;
						int tl;
						ltrim(lj, &tp, &tl);
						if (sb_putc(out, ' ') != 0 ||
						    render_inline(tp, tl, out) != 0)
							return -1;
						j++;
						continue;
					}
					while (sp > 0 && stack_ind[sp - 1] > ind2)
						sp--;
					if (sp == 0 || stack_ind[sp - 1] < ind2) {
						if (sp < 32) {
							stack_ind[sp] = ind2;
							stack_cnt[sp] = 0;
							sp++;
						}
					}
					if (loose) {
						if (sb_putc(out, '\n') != 0)
							return -1;
						loose = 0;
					}
					/* Items share one block: end the
					 * previous item's line, except
					 * before the first item, where
					 * emit_blank separates blocks.
					 */
					if (!first) {
						if (sb_putc(out, '\n') != 0)
							return -1;
					}
					first = 0;
					if (emit_blank(out, &need_blank) != 0)
						return -1;
					{
						int depth = sp - 1, d;
						char mbuf[32];
						const char *ct = c2;
						int ctl = cl2;
						for (d = 0; d < depth * 2; d++)
							if (sb_putc(out, ' ') != 0)
								return -1;
						if (ord2) {
							stack_cnt[sp - 1]++;
							snprintf(mbuf, sizeof mbuf,
								"%d. ",
								stack_cnt[sp - 1]);
						} else {
							snprintf(mbuf, sizeof mbuf,
								"- ");
						}
						/* Task list checkboxes. */
						if (!ord2 && ctl >= 4 &&
						    ct[0] == '[' &&
						    (ct[1] == ' ' || ct[1] == 'x' ||
						     ct[1] == 'X') &&
						    ct[2] == ']' && ct[3] == ' ') {
							snprintf(mbuf, sizeof mbuf,
								"- [%c] ",
								ct[1] == ' ' ? ' ' : 'x');
							ct += 4;
							ctl -= 4;
						}
						if (sb_puts(out, mbuf) != 0 ||
						    render_inline(ct, ctl, out) != 0)
							return -1;
					}
					j++;
				}
				/* A list item's trailing newline. */
				if (sb_putc(out, '\n') != 0)
					return -1;
				need_blank = 1;
				i = j;
				continue;
			}
		}
		{
			/* Table: header + delimiter row, then rows. */
			const char *tp;
			int tl;
			ltrim(l, &tp, &tl);
			if (memchr(tp, '|', tl) && i + 1 < n &&
			    is_delim_row(&lines[i + 1])) {
				int j = i;
				if (emit_blank(out, &need_blank) != 0)
					return -1;
				while (j < n) {
					const char *rp;
					int rl;
					if (j > i + 1) {
						ltrim(&lines[j], &rp, &rl);
						if (!memchr(rp, '|', rl))
							break;
					}
					ltrim(&lines[j], &rp, &rl);
					if (render_inline(rp, rl, out) != 0 ||
					    sb_putc(out, '\n') != 0)
						return -1;
					j++;
					if (j > i + 1 && lblank(&lines[j - 1]))
						break;
				}
				need_blank = 1;
				i = j;
				continue;
			}
		}
		/* Paragraph: gather lines until a blank or a block
		 * start, joining with spaces (or newlines on hard
		 * breaks: a line ending with two spaces).
		 */
		{
			Str para = {0};
			int j = i, prev_hard = 0;
			while (j < n) {
				Line *lj = &lines[j];
				const char *tp;
				int tl;
				if (lblank(lj))
					break;
				if (j > i) {
					const char *tx;
					int txl;
					char fch;
					int flen, in2, o2, nu2;
					const char *c2;
					int cl2;
					Line qin;
					const char *hp;
					int hl;
					/* A block start ends the paragraph. */
					if (setext_line(lj) ||
					    is_delim_row(lj) ||
					    atx_heading(lj, &tx, &txl) ||
					    fence_open(lj, &fch, &flen) ||
					    is_hr(lj) ||
					    quote_depth(lj, &qin) > 0 ||
					    list_marker(lj, &in2, &o2, &nu2,
						&c2, &cl2))
						break;
					/* A table header line starts a table. */
					ltrim(lj, &hp, &hl);
					if (memchr(hp, '|', hl) && j + 1 < n &&
					    is_delim_row(&lines[j + 1]))
						break;
				}
				ltrim(lj, &tp, &tl);
				if (j > i) {
					if (sb_putc(&para,
						prev_hard ? '\n' : ' ') != 0) {
						free(para.buf);
						return -1;
					}
				}
				if (render_inline(tp, tl, &para) != 0) {
					free(para.buf);
					return -1;
				}
				prev_hard = (lj->len >= 2 &&
				    lj->p[lj->len - 1] == ' ' &&
				    lj->p[lj->len - 2] == ' ');
				j++;
			}
			if (emit_blank(out, &need_blank) != 0) {
				free(para.buf);
				return -1;
			}
			if (para.buf) {
				if (sb_puts(out, para.buf) != 0 ||
				    sb_putc(out, '\n') != 0) {
					free(para.buf);
					return -1;
				}
			}
			free(para.buf);
			/* Setext underline directly after a paragraph. */
			if (j < n && setext_line(&lines[j])) {
				int lvl = setext_line(&lines[j]);
				if (sb_puts(out, lvl == 1 ? "===\n" : "---\n") != 0)
					return -1;
				j++;
			}
			need_blank = 1;
			i = j;
			continue;
		}
	}
	return 0;
}

/* Render text (used for quote interiors). */
static int
render_blocks_from_text(const char *text, Str *out)
{
	int n = 0;
	Line *lines = split_lines(text, &n);
	int r;

	if (!lines)
		return -1;
	r = render_blocks(lines, n, out);
	free(lines);
	return r;
}

char *
markdown_render(const char *src)
{
	int n = 0, i, w = 0;
	Line *lines, *kept;
	Str out = {0};
	char *ret;

	if (!src)
		return strdup("");
	lines = split_lines(src, &n);
	if (!lines)
		return 0;
	/* First pass: link reference definitions are consumed. */
	kept = malloc(sizeof(Line) * (n + 1));
	if (!kept) {
		free(lines);
		return 0;
	}
	for (i = 0; i < n; i++) {
		if (!ref_define(&lines[i]))
			kept[w++] = lines[i];
	}
	if (render_blocks(kept, w, &out) != 0) {
		free(lines);
		free(kept);
		free(out.buf);
		refs_free();
		return 0;
	}
	free(lines);
	free(kept);
	refs_free();
	/* Trim trailing blank lines; end with exactly one newline. */
	while (out.len > 0 && out.buf[out.len - 1] == '\n')
		out.len--;
	if (out.len > 0) {
		if (sb_putc(&out, '\n') != 0) {
			free(out.buf);
			return 0;
		}
	}
	ret = out.buf ? out.buf : strdup("");
	return ret;
}
