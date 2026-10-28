/* Standalone tests for markdown.c. Built manually, NOT part of
 * the wily binary:
 *   cc -o /tmp/mdtest wily/markdown.c wily/mdtest.c && /tmp/mdtest
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *markdown_render(const char *src);

static int fails;

static void
check(const char *name, const char *in, const char *want)
{
	char *got = markdown_render(in);
	if (!got) {
		printf("FAIL %s: render returned NULL\n", name);
		fails++;
		return;
	}
	if (strcmp(got, want) != 0) {
		printf("FAIL %s:\n--- got ---\n%s--- want ---\n%s---\n",
			name, got, want);
		fails++;
	}
	free(got);
}

int
main(void)
{
	check("atx-h1",
		"# Hello\n\nWorld\n",
		"Hello\n===\n\nWorld\n");
	check("atx-h2-h3",
		"## Sub\n\n### Deep\n",
		"Sub\n---\n\n### Deep\n");
	check("setext",
		"Title\n=====\n\nSub\n---\n",
		"Title\n===\n\nSub\n---\n");
	check("emphasis-code",
		"This is **bold** and *italic* and `code`.\n",
		"This is bold and italic and code.\n");
	check("underscore-intraword",
		"foo_bar baz and _em_ here\n",
		"foo_bar baz and em here\n");
	check("link-image",
		"See [wily](http://example.com) and ![alt](img.png).\n",
		"See wily (http://example.com) and alt (img.png).\n");
	check("ref-link",
		"[a][b]\n\n[b]: http://x.com\n",
		"a (http://x.com)\n");
	check("list",
		"- one\n- two\n  - nested\n1. first\n2. second\n",
		"- one\n- two\n  - nested\n1. first\n2. second\n");
	check("task-list",
		"- [ ] todo\n- [x] done\n",
		"- [ ] todo\n- [x] done\n");
	check("fenced-code",
		"```\nx = 1\n```\n",
		"    x = 1\n");
	check("quote",
		"> hello\n> world\n",
		"  hello world\n");
	check("hr",
		"a\n\n---\n\nb\n",
		"a\n\n---\n\nb\n");
	check("table",
		"| A | B |\n|---|---|\n| 1 | 2 |\n",
		"| A | B |\n|---|---|\n| 1 | 2 |\n");
	check("hard-break",
		"line one  \nline two\n",
		"line one\nline two\n");
	check("escape",
		"a \\* b\n",
		"a * b\n");
	check("empty", "", "");
	check("strikethrough",
		"a ~~gone~~ b\n",
		"a gone b\n");
	check("autolink",
		"visit <http://example.com> now\n",
		"visit http://example.com now\n");
	if (fails) {
		printf("%d FAILURES\n", fails);
		return 1;
	}
	printf("all markdown tests pass\n");
	return 0;
}
