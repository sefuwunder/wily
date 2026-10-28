/* maildir.c - maildir storage + sync state. */
#include "maildir.h"
#include "util.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int
mkdir_p(const char *path, char **err)
{
	char *tmp, *p;
	struct stat st;
	if (stat(path, &st) == 0) {
		if (S_ISDIR(st.st_mode))
			return 0;
		*err = xstrfmt("%s is not a directory", path);
		return -1;
	}
	tmp = xstrdup(path);
	for (p = tmp + 1; *p; p++) {
		if (*p == '/') {
			*p = 0;
			if (mkdir(tmp, 0777) < 0 && errno != EEXIST) {
				*err = xstrfmt("mkdir %s: %s", tmp,
				    strerror(errno));
				free(tmp);
				return -1;
			}
			*p = '/';
		}
	}
	if (mkdir(tmp, 0777) < 0 && errno != EEXIST) {
		*err = xstrfmt("mkdir %s: %s", tmp, strerror(errno));
		free(tmp);
		return -1;
	}
	free(tmp);
	return 0;
}

int
maildir_init(const char *dir, char **err)
{
	char *sub;
	int r = 0;
	sub = xstrfmt("%s/tmp", dir);
	if (mkdir_p(sub, err) < 0)
		r = -1;
	free(sub);
	if (r == 0) {
		sub = xstrfmt("%s/new", dir);
		if (mkdir_p(sub, err) < 0)
			r = -1;
		free(sub);
	}
	if (r == 0) {
		sub = xstrfmt("%s/cur", dir);
		if (mkdir_p(sub, err) < 0)
			r = -1;
		free(sub);
	}
	return r;
}

/* Unique maildir base name: <sec>.<usec>_<pid>.<host>[_<counter>] */
static char *
unique_base(void)
{
	static unsigned counter = 0;
	struct timespec ts;
	char host[256];
	clock_gettime(CLOCK_REALTIME, &ts);
	if (gethostname(host, sizeof host) < 0)
		strcpy(host, "localhost");
	host[sizeof host - 1] = 0;
	return xstrfmt("%ld.%06ld_%d.%s_%u", (long)ts.tv_sec,
	    (long)(ts.tv_nsec / 1000), (int)getpid(), host, counter++);
}

static int
has_flag(const char *flags, char c)
{
	return flags && strchr(flags, c) != NULL;
}

int
maildir_store(const char *dir, const char *body, size_t len,
    const char *flags, char **name_out, char **err)
{
	char *base = unique_base();
	char *tmpname, *tmppath, *finalname, *finalpath;
	FILE *f;

	tmpname = xstrfmt("%s", base);
	tmppath = xstrfmt("%s/tmp/%s", dir, tmpname);
	f = fopen(tmppath, "wb");
	if (!f) {
		*err = xstrfmt("cannot create %s: %s", tmppath,
		    strerror(errno));
		free(base);
		free(tmpname);
		free(tmppath);
		return -1;
	}
	if (len > 0 && fwrite(body, 1, len, f) != len) {
		*err = xstrfmt("cannot write %s: %s", tmppath,
		    strerror(errno));
		fclose(f);
		unlink(tmppath);
		free(base);
		free(tmpname);
		free(tmppath);
		return -1;
	}
	if (fclose(f) != 0) {
		*err = xstrfmt("cannot close %s: %s", tmppath,
		    strerror(errno));
		unlink(tmppath);
		free(base);
		free(tmpname);
		free(tmppath);
		return -1;
	}
	if (has_flag(flags, 'S'))
		finalname = xstrfmt("%s:2,%s", base, flags);
	else
		finalname = xstrdup(base);
	finalpath = xstrfmt("%s/%s/%s", dir,
	    has_flag(flags, 'S') ? "cur" : "new", finalname);
	if (link(tmppath, finalpath) < 0) {
		*err = xstrfmt("cannot link %s -> %s: %s", tmppath,
		    finalpath, strerror(errno));
		unlink(tmppath);
		free(base);
		free(tmpname);
		free(tmppath);
		free(finalname);
		free(finalpath);
		return -1;
	}
	unlink(tmppath);
	free(base);
	free(tmpname);
	free(tmppath);
	free(finalpath);
	*name_out = finalname;
	return 0;
}

/* Split "base:2,FS" into base and flags; flags "" when no suffix. */
static void
split_name(const char *name, char **base_out, char **flags_out)
{
	const char *colon = strstr(name, ":2,");
	if (colon) {
		*base_out = xstrndup(name, (size_t)(colon - name));
		*flags_out = xstrdup(colon + 3);
	} else {
		*base_out = xstrdup(name);
		*flags_out = xstrdup("");
	}
}

int
maildir_reflag(const char *dir, const char *oldname, const char *flags,
    char **newname_out, char **err)
{
	char *base, *oldflags, *newname, *oldpath, *newpath;
	const char *oldsub, *newsub;
	struct stat st;
	int same;

	split_name(oldname, &base, &oldflags);
	/* Locate the file for real: other maildir tools may keep a
	 * cur/ file without the Seen flag. */
	{
		char *p = xstrfmt("%s/cur/%s", dir, oldname);
		if (stat(p, &st) == 0)
			oldsub = "cur";
		else
			oldsub = "new";
		free(p);
	}
	newsub = has_flag(flags, 'S') ? "cur" : "new";
	if (has_flag(flags, 'S'))
		newname = xstrfmt("%s:2,%s", base, flags);
	else
		newname = xstrdup(base);
	same = strcmp(oldname, newname) == 0 && strcmp(oldsub, newsub) == 0;
	oldpath = xstrfmt("%s/%s/%s", dir, oldsub, oldname);
	newpath = xstrfmt("%s/%s/%s", dir, newsub, newname);
	if (!same && rename(oldpath, newpath) < 0) {
		*err = xstrfmt("cannot rename %s -> %s: %s", oldpath,
		    newpath, strerror(errno));
		free(base);
		free(oldflags);
		free(newname);
		free(oldpath);
		free(newpath);
		return -1;
	}
	free(base);
	free(oldflags);
	free(oldpath);
	free(newpath);
	*newname_out = newname;
	return 0;
}

void
imap_flags_to_maildir(const char *imap_flags, char *out, size_t outsz)
{
	size_t o = 0;
	/* Canonical maildir flag order: D F P R S T. */
	if (imap_flags && strcasestr_(imap_flags, "\\Draft"))
		out[o++] = 'D';
	if (imap_flags && strcasestr_(imap_flags, "\\Flagged"))
		out[o++] = 'F';
	if (imap_flags && strcasestr_(imap_flags, "\\Answered"))
		out[o++] = 'R';
	if (imap_flags && strcasestr_(imap_flags, "\\Seen"))
		out[o++] = 'S';
	if (imap_flags && strcasestr_(imap_flags, "\\Deleted"))
		out[o++] = 'T';
	if (o >= outsz)
		o = outsz - 1;
	out[o] = 0;
}

int
state_load(const char *path, MailState *st)
{
	FILE *f = fopen(path, "r");
	char line[1024];

	memset(st, 0, sizeof *st);
	if (!f)
		return 0; /* no state yet: fresh sync */
	if (!fgets(line, sizeof line, f)) {
		fclose(f);
		return 0;
	}
	{
		unsigned long v, n;
		if (sscanf(line, "v1 %lu %lu", &v, &n) == 2) {
			st->uidvalidity = v;
			st->uidnext = n;
		}
	}
	while (fgets(line, sizeof line, f)) {
		unsigned long uid;
		char name[512], flags[64];
		int got = sscanf(line, "%lu %511s %63s", &uid, name, flags);
		if (got == 2)
			state_put(st, uid, name, "");
		else if (got == 3)
			state_put(st, uid, name, flags);
	}
	fclose(f);
	return 0;
}

int
state_save(const char *path, MailState *st)
{
	char *tmp = xstrfmt("%s.tmp", path);
	FILE *f = fopen(tmp, "w");
	int i;
	if (!f) {
		free(tmp);
		return -1;
	}
	fprintf(f, "v1 %lu %lu\n", st->uidvalidity, st->uidnext);
	for (i = 0; i < st->n; i++)
		fprintf(f, "%lu %s %s\n", st->ents[i].uid,
		    st->ents[i].name, st->ents[i].flags);
	if (fclose(f) != 0) {
		unlink(tmp);
		free(tmp);
		return -1;
	}
	if (rename(tmp, path) < 0) {
		unlink(tmp);
		free(tmp);
		return -1;
	}
	free(tmp);
	return 0;
}

StateEnt *
state_find(MailState *st, unsigned long uid)
{
	int i;
	for (i = 0; i < st->n; i++)
		if (st->ents[i].uid == uid)
			return &st->ents[i];
	return NULL;
}

void
state_put(MailState *st, unsigned long uid, const char *name,
    const char *flags)
{
	StateEnt *e = state_find(st, uid);
	if (e) {
		free(e->name);
		free(e->flags);
		e->name = xstrdup(name);
		e->flags = xstrdup(flags);
		return;
	}
	if (st->n == st->cap) {
		st->cap = st->cap ? st->cap * 2 : 64;
		st->ents = xrealloc(st->ents, sizeof(StateEnt) * (size_t)st->cap);
	}
	e = &st->ents[st->n++];
	e->uid = uid;
	e->name = xstrdup(name);
	e->flags = xstrdup(flags);
}

void
state_free(MailState *st)
{
	int i;
	for (i = 0; i < st->n; i++) {
		free(st->ents[i].name);
		free(st->ents[i].flags);
	}
	free(st->ents);
	memset(st, 0, sizeof *st);
}
