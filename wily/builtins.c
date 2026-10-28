/*******************************************
 *	Builtin functions
 *******************************************/

#include "wily.h"
#include "view.h"
#include "tile.h"
#include "data.h"
#include <errno.h>
#include <ctype.h>

/* Each of these functions takes a View used as the context
 * of the command, and an argument.  The argument may be null.
 */
/*
 * If given arguments, Kill all the external processes matching
 * the arguments.
 *
 * If given no arguments, generate a list of possible 'Kill' commands.
 */
static void
builtin_kill(View*v, char*s) {
	if (s)
		kill_all(s);
	else
		kill_list();
}

/*****************************************************
	Dotfiles, Font, Indent
	Toggle operations.  These probably _should_
	all act similarly, but right now they don't.  todo.
*****************************************************/

static void
dotfiles(View *v, char *arg) {
	show_dot_files = !show_dot_files;
}

static void
builtin_font(View *v, char *arg) {
	tile_setfont(view_tile(v), arg);
}

/* Bigger [n] / Smaller [n]: step the window's font size up or
 * down n sizes (default 1). Only X-backed fonts resize; Plan 9
 * font-file fonts report back instead of breaking. */
static void
font_step_cmd(View *v, char *arg, int dir, const char *name)
{
	int steps = 1, r;

	if (arg && *arg) {
		steps = atoi(arg);
		if (steps <= 0)
			steps = 1;
		if (steps > 16)
			steps = 16;
	}
	r = tile_font_step(view_tile(v), dir, steps);
	if (r == -1)
		diag(0, "%s: this font cannot be resized", name);
	else if (r == -2)
		diag(0, "%s: no larger/smaller font available", name);
}

void
bigger(View *v, char *arg) {
	font_step_cmd(v, arg, 1, "Bigger");
}

void
smaller(View *v, char *arg) {
	font_step_cmd(v, arg, -1, "Smaller");
}

/* LLM context is clipped: loom's thinker has a bounded context budget,
 * and a whole file is rarely the question. The clip is noted in the
 * prompt so the thinker knows it is not seeing everything.
 */
#define LLM_CTX_MAX 4000

/*
 * LLM [question]: ask loom about the current window (see
 * Doc/wily-llm.md). The question is the argument; the selection in a
 * body view, if any, is context; the window's file path is named so
 * the thinker can read the file itself with loom's tools.
 *
 * Nothing slow happens here. This builtin only writes a prompt file
 * and starts tools/wily-llm/wily-llm.sh as a child, whose stdout is
 * piped to the +LLM window as it arrives. The thinking itself runs in
 * loom's daemon and ephemeral worker processes, so wily's event loop
 * is never held hostage — the answer lands minutes later in +LLM
 * while the user keeps editing.
 *
 * Idioms:
 *   LLM <question>		question, plus the body selection as
 *				context if there is one.
 *   select text, chord-click LLM
 *				the selection arrives as the argument;
 *				it is context, and the question
 *				defaults to explaining it.
 *   LLM with no question and no selection
 *				summarize the window (clipped).
 */
static void
builtin_llm(View *v, char *arg)
{
	View *ls = last_selection;
	View *body, *vllm;
	Text *t;
	Path label, path, llmlabel, promptpath;
	char *question, *context = 0, *cmdline, *script;
	char *tmpdir;
	ulong len;
	int clipped = 0;
	static int counter = 0;
	FILE *f;

	/* Question: the argument, with leading blanks skipped. */
	while (arg && (*arg == ' ' || *arg == '\t'))
		arg++;
	if (arg && !*arg)
		arg = 0;
	question = arg;

	/* Context: the selection in a body view, if any. */
	if (ISBODY(ls) && RLEN(ls->sel) > 0)
		context = text_duputf(ls->t, ls->sel);

	/* A chord-click passes the selection as the argument; that is
	 * context, not a question.
	 */
	if (question && context && strcmp(question, context) == 0)
		question = 0;
	if (!question && context)
		question = "Explain this selection.";
	if (!question) {
		/* No question and no selection: summarize the window. */
		if (!view_win(v))
			v = ls;
		body = v ? view_body(v) : 0;
		if (!body || !view_win(body)) {
			diag(0, "LLM: type a question, e.g. LLM what does this do");
			free(context);
			return;
		}
		t = view_text(body);
		len = text_length(t);
		context = text_duputf(t, range(0, len));
		question = "Summarize this file.";
	}
	if (context && strlen(context) > LLM_CTX_MAX) {
		context[LLM_CTX_MAX] = '\0';
		clipped = 1;
	}

	/* The window's file path, for the thinker to read. Labels are
	 * stored contracted ($PWD/...), which would resolve against the
	 * loom daemon's directory, not wily's — expand to a full path.
	 */
	label[0] = '\0';
	if (view_win(v))
		data_getlabel(view_data(v), label);
	else if (ls && view_win(ls))
		data_getlabel(view_data(ls), label);
	if (label[0])
		label2path(path, label);
	else
		path[0] = '\0';

	/* Prompt file: unique per invocation; the watcher script removes
	 * it once the job is submitted.
	 */
	tmpdir = getenv("TMPDIR");
	snprintf(promptpath, sizeof promptpath, "%s/wily-llm-%d-%d.txt",
		tmpdir ? tmpdir : "/tmp", (int)getpid(), counter++);
	f = fopen(promptpath, "w");
	if (!f) {
		diag(0, "LLM: cannot write prompt file");
		free(context);
		return;
	}
	fprintf(f, "Question: %s\n", question);
	fprintf(f, "File: %s\n", path[0] ? path : "(none)");
	fprintf(f, "Context:\n");
	if (context) {
		fputs(context, f);
		if (clipped)
			fputs("\n[context clipped to 4000 chars]", f);
	}
	fputc('\n', f);
	fclose(f);
	free(context);

	/* The +LLM window lives beside the file it was asked from. */
	if (path[0]) {
		dirnametrunc(path);
	} else {
		strcpy(path, wilydir);
	}
	if (!path[0])
		strcpy(path, wilydir);
	snprintf(llmlabel, sizeof llmlabel, "%s/+LLM", path);
	vllm = openlabel(llmlabel, true);
	if (!vllm) {
		diag(0, "LLM: cannot open +LLM window");
		unlink(promptpath);
		return;
	}
	body = view_body(vllm);
	/* Park the selection, empty, at the end: the watcher's first
	 * output replaces the current selection, so this makes it an
	 * append to the Q&A log instead of clobbering old answers.
	 */
	t = view_text(body);
	len = text_length(t);
	view_select(body, range(len, len));

	script = getenv("WILEY_LLM");
	cmdline = salloc(strlen(script ? script : "wily-llm") +
		strlen(promptpath) + 2);
	sprintf(cmdline, "%s %s", script ? script : "wily-llm", promptpath);
	run_to_view(v, cmdline, body);
	free(cmdline);
}

static void
builtin_autoindent(View *v, char *arg) {
	View *body;
	
	if ((body = view_body(v))) {
		body->autoindent = ! body->autoindent;
	} else {
		autoindent_enabled = !autoindent_enabled;
	}
}

/*****************************************************
	Markdown. Render the window's text as plain-text
	Markdown (see markdown.c) in a derived window titled
	"<label> +Rendered". Re-running re-renders into the
	same window, unless it has edits of its own.
*****************************************************/
static void
markdown(View *v, char *arg)
{
	View *body, *rv;
	Data *d;
	char *src, *rendered;
	Path tmp, rlabel;

	(void)arg;
	if (!(body = view_body(v)))
		return;
	d = view_data(body);
	if (!d || d->names)
		return;	/* directories have no markdown to render */
	src = text_duputf(body->t, text_all(body->t));
	rendered = markdown_render(src);
	free(src);
	if (!rendered)
		return;
	/* Contract the label the way data_open will store it,
	 * so re-running finds the same window. The suffix is
	 * appended by hand (not snprintf) so a maximal label is
	 * shortened to fit, rather than truncating the suffix.
	 */
	{
		size_t ll = strlen(d->label);
		if (ll + 10 >= sizeof tmp)
			ll = sizeof tmp - 11;
		memcpy(tmp, d->label, ll);
		strcpy(tmp + ll, " +Rendered");
	}
	pathcontract(rlabel, tmp);
	if ((rv = data_find(rlabel)) != 0) {
		Data *rd = view_data(rv);
		if (rd && data_isdirty(rd)) {
			diag(0, "markdown: rendered view has edits; "
				"Del it first to re-render");
			free(rendered);
			return;
		}
		text_replaceutf(rv->t, text_all(rv->t), rendered);
		undo_mark(rv->t);
		tag_rmtool(view_text(view_tile(rv)->tag), "Put");
		view_setorigin(rv, 0);
		view_select(rv, range(0, 0));
	} else {
		rv = data_open(rlabel, true);
		if (!rv) {
			diag(0, "markdown: cannot open %s", rlabel);
			free(rendered);
			return;
		}
		text_replaceutf(rv->t, text_all(rv->t), rendered);
		undo_mark(rv->t);
		tag_rmtool(view_text(view_tile(rv)->tag), "Put");
		view_setorigin(rv, 0);
		view_select(rv, range(0, 0));
	}
	free(rendered);
}

/*****************************************************
	Undo, redo.  If called with an argument, they
	each go 'all the way'. 
*****************************************************/
static void
undo_ops(View *v, char*arg, Range (*undofn)(Text*, Bool))
{
	Range	r;

	if(!(v = view_body(v)))
		return;
	r = (*undofn)(v->t, arg!=0);
	if ROK(r) {
		view_show(v, r);
		view_select(v, r);
	}
}

static void undo(View *v, char *arg){undo_ops(v, arg, undo_undo);}
static void redo(View *v, char *arg) {undo_ops(v, arg, undo_redo);}

/*****************************************************
	Get, Put.  If called with an argument, they
	each go 'all the way'.
*****************************************************/
/* Read or write the window associated with 'v', possibly using 'arg' */
static void
getorput(View*v, char*arg, int(*fn)(Data*,char*)) {
	Data *d;
	Path	dest;
	char	*s;

	if (!(d = view_data(v)))
		return;

	if (arg && strlen(arg)) {
		data_addcontext(d, dest, arg);
		s = dest;
	} else {
		s = 0;
	}
	(*fn)(d, s);
}

static void	put(View *v, char *arg)	{getorput(v, arg, data_put);}
static void	get(View *v, char *arg)	{getorput(v, arg, data_get);}

/*****************************************************
	Quit/Putall ignore the context.
*****************************************************/
static void
putall(View *v, char *arg)
{
	data_putall();
}

static void
quit(View *v, char *arg) {
	cleanup_and_die(0);
}

/*****************************************************
	Del, Delcol delete the window or column 
	'v' belongs to.
*****************************************************/
static void
del(View *v, char *arg) {
	win_del(view_win(v));
}

static void
delcol(View*v, char *arg) {
	col_del(tile_col(v->tile));
}

/*****************************************************
	Cut/Snarf/Paste all act on last_selection
*****************************************************/
static void
cut(View *v, char *arg) {
	if((v=last_selection))
		view_cut(v, v->sel);
}

static void
builtin_snarf(View *v, char *arg) {
	if((v=last_selection))
		snarf(v->t, v->sel);
}

static void
builtin_paste(View *v, char *arg) {
	if((v=last_selection))
		view_paste(v);
}

/*****************************************************
	Anchor, Split act on either the window of the current
	context, or the last selection.
*****************************************************/
static void
anchor(View *v, char *arg) {
	Tile	*win;

	if ( (win=view_win(v)) || (win=view_win(last_selection)) ) {
		win_anchor(win, arg);
	}
}

/* If 'v' or 'last_selection' are in a window, split that window */
static void
split(View *v, char *arg)
{
	Tile	*win;

	if( (win = view_win(v)) || (win = view_win(last_selection)) ) {
		win_clone(win);
	}
}

/*****************************************************
	Clear, Look act on the body of the current
	context.
*****************************************************/
static void
clear(View *v, char *arg) {
	View	*body;

	if ((body = view_body(v))) 
		text_replace(body->t, text_all(body->t), rstring(0,0));
}

/* Look for 'arg' or the current selection in the body of 'v',
 * select and show the found thing if you find it.
 */
static void
look(View *v, char *arg) {
	if((v=view_body(v)))
		view_look(v, arg);
}

/*****************************************************
	New window, possibly called 'arg'.
	Try to use a window for context.
*****************************************************/
static void
new(View *v, char *arg) {
	Path	label;

	if(!arg)
		arg = "New";
		
	/* If 'v' isn't part of a window, maybe last_selection is */
	if(!view_win(v))
		v = last_selection;
	data_addcontext(view_data(v), label, arg);
	if(!data_find(label))
		data_open(label, true);
}

typedef struct Cmd Cmd;
struct Cmd {
	char	*name;
	void	(*cmd)(View *, char *);
};

/* _Must_ be kept in sorted order, we use bsearch on it */
static Cmd builtins[] = {
	{"Anchor", anchor},
	{"Bigger", bigger},
	{"Clear", clear},
	{"Cut", cut},
	{"Del", del},
	{"Delcol", delcol},
	{"Dotfiles", dotfiles},
	{"Font", builtin_font},
	{"Get", get},
	{"Indent", builtin_autoindent},
	{"Kill", builtin_kill},
	{"LLM", builtin_llm},
	{"Look", look},
	{"Markdown", markdown},
	{"New", new},
	{"Newcol", col_new},
	{"Paste", builtin_paste},
	{"Put", put},
	{"Putall", putall},
	{"Quit", quit},
	{"Redo", redo},
	{"RestoreSession", session_restore_cmd},
	{"SaveSession", session_save_cmd},
	{"Smaller", smaller},
	{"Snarf", builtin_snarf},
	{"Split", split},
	{"Theme", theme_cmd},
	{"Undo", undo},
};

static int
cmd_compare(Cmd *a,Cmd *b)
{
	return strcmp(a->name, b->name);
}

/*
 * PRE: 'v' is the context, 'cmd' the command.
 * POST: return true if we recognise the builtin, false otherwise.
 */
Bool
builtin(View *v, char *cmd, char *arg)
{
	Cmd	key, *c;

	assert(v);
	assert(cmd[0] && !isspace(cmd[0]));

	key.name = cmd;
	c = bsearch( &key, builtins, sizeof(builtins)/sizeof(Cmd),
				 sizeof(Cmd), 
			(int	(*)(const void *,const void*))cmd_compare);

	if (c) 
		(*c->cmd)(v, arg);
	return c != 0;
}
