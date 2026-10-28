Markdown rendering
==================

Wily's text frames have a single font, so rich-text Markdown is not
possible. Instead, the `Markdown` command renders a Markdown file
as typographic plain text — the way `pandoc -t plain` looks — in a
new window titled `<file> +Rendered`.

Markdown files (names ending in `.md` or `.markdown`) get a
`Markdown` button in their tag. Clicking it renders the window's
current text; clicking again re-renders into the same window, unless
that window has edits of its own, in which case wily asks you to Del
it first rather than silently discarding your edits. The rendered
window is clean (no Put): it is a view, not a file, and quitting
with it open stores it in the session as an unsaved buffer.

What the renderer does
----------------------

* Headings: `#` becomes an underline (`===` for level 1, `---` for
  level 2); levels 3–6 keep their `#` prefix.
* Emphasis, strong emphasis, code spans and strikethrough markers
  are stripped, leaving the text.
* Links become `text (url)` — inline, reference, shortcut and
  autolinks alike; images become `alt (url)`.
* Fenced and indented code blocks are indented four spaces.
* Lists are normalized (`- ` bullets, renumbered `N. ` items,
  two-space nesting, task checkboxes kept); blockquotes are
  indented two spaces per level.
* Horizontal rules become `---`; tables pass through unchanged;
  HTML tags are dropped; backslash escapes are honored; hard line
  breaks (two trailing spaces) stay breaks, soft breaks join with
  a space.

The renderer is a standalone function (`markdown_render` in
`markdown.c`, no wily dependencies) with its own test program
(`mdtest.c`). One known approximation: same-type nested emphasis
(such as `*a *b* c*`) may mis-pair its markers.
