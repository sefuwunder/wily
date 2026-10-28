/* wmail-sync - sync GOA mail accounts to local maildirs.
 *
 * v1: one-way IMAP -> maildir, incremental via a state file.
 * Credentials come from GNOME Online Accounts on demand and are never
 * stored. No expunge propagation, no MIME part extraction, no sending
 * (phase 2) yet. See Doc/wmail.md. */
#include "goa.h"
#include "imap.h"
#include "maildir.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int verbose = 0;

static void
usage(void)
{
	fprintf(stderr,
	    "usage: wmail-sync [-a account] [-f folder]... [-d root] [-l] [-v]\n"
	    "  -a account  match a GOA account by email substring (default: first)\n"
	    "  -f folder   IMAP folder to sync (default: INBOX; repeatable)\n"
	    "  -d root     maildir root (default: $WMAIL_DIR or ~/Mail)\n"
	    "  -l          list GOA mail accounts and exit\n"
	    "  -v          verbose\n");
	exit(2);
}

typedef struct {
	const char *dir;
	MailState *st;
	int nnew;
	char *err;
} FetchCtx;

static void
fetch_cb(void *ctx, unsigned long uid, const char *flags, const char *body,
    size_t len)
{
	FetchCtx *f = ctx;
	char mdflags[16], *name = NULL;
	char *err = NULL;

	imap_flags_to_maildir(flags, mdflags, sizeof mdflags);
	if (maildir_store(f->dir, body, len, mdflags, &name, &err) < 0) {
		if (!f->err)
			f->err = err;
		else
			free(err);
		return;
	}
	state_put(f->st, uid, name, mdflags);
	free(name);
	f->nnew++;
	if (verbose)
		fprintf(stderr, "  new uid %lu (%s)\n", uid, mdflags);
}

static char *
sanitize_folder(const char *folder)
{
	char *s = xstrdup(folder), *p;
	for (p = s; *p; p++)
		if (*p == '/')
			*p = '.';
	return s;
}

static int
sync_folder(GoaAccount *a, GoaCreds *creds, const char *folder,
    const char *root)
{
	char *san = sanitize_folder(folder);
	char *dir = xstrfmt("%s/%s/%s", root, a->email, san);
	char *statepath = xstrfmt("%s/.wmail-state", dir);
	char *err = NULL;
	MailState st;
	Imap *im;
	unsigned long uidvalidity = 0, uidnext = 0;
	unsigned long *uids = NULL;
	char **flags = NULL;
	int n = 0, i, nnew = 0, nreflag = 0;
	int port, rc = 1;

	free(san);
	if (maildir_init(dir, &err) < 0) {
		fprintf(stderr, "wmail-sync: %s\n", err);
		free(err);
		goto out;
	}
	state_load(statepath, &st);

	if (a->imap_ssl)
		port = 993;
	else if (a->imap_tls)
		port = 143;
	else {
		fprintf(stderr,
		    "wmail-sync: account %s offers neither IMAP SSL nor STARTTLS; refusing plaintext\n",
		    a->email);
		state_free(&st);
		goto out;
	}
	if (verbose)
		fprintf(stderr, "connecting to %s:%d...\n", a->imap_host, port);
	im = imap_connect(a->imap_host, port, a->imap_ssl, a->imap_tls, &err);
	if (!im) {
		fprintf(stderr, "wmail-sync: %s\n", err);
		free(err);
		state_free(&st);
		goto out;
	}
	if (creds->oauth2) {
		if (imap_oauth2(im, creds->user, creds->token, &err) < 0) {
			fprintf(stderr, "wmail-sync: XOAUTH2 failed: %s\n",
			    err);
			free(err);
			imap_free(im);
			state_free(&st);
			goto out;
		}
	} else {
		if (imap_login(im, creds->user, creds->imap_password,
			&err) < 0) {
			fprintf(stderr, "wmail-sync: LOGIN failed: %s\n",
			    err);
			free(err);
			imap_free(im);
			state_free(&st);
			goto out;
		}
	}
	if (imap_select(im, folder, &uidvalidity, &uidnext, &err) < 0) {
		fprintf(stderr, "wmail-sync: SELECT %s failed: %s\n", folder,
		    err);
		free(err);
		imap_free(im);
		state_free(&st);
		goto out;
	}
	if (verbose)
		fprintf(stderr,
		    "  %s: uidvalidity %lu uidnext %lu\n", folder,
		    uidvalidity, uidnext);
	if (st.uidvalidity != 0 && st.uidvalidity != uidvalidity) {
		fprintf(stderr,
		    "wmail-sync: %s: UIDVALIDITY changed (%lu -> %lu); re-downloading\n",
		    folder, st.uidvalidity, uidvalidity);
		state_free(&st);
		memset(&st, 0, sizeof st);
	}
	if (imap_uid_flags(im, &uids, &flags, &n, &err) < 0) {
		fprintf(stderr, "wmail-sync: UID FETCH failed: %s\n", err);
		free(err);
		imap_free(im);
		state_free(&st);
		goto out;
	}
	/* New messages (skip \Deleted). */
	{
		unsigned long *newuids = xmalloc(sizeof(unsigned long) *
		    (size_t)(n > 0 ? n : 1));
		int nnewuids = 0;
		FetchCtx fctx;
		for (i = 0; i < n; i++) {
			if (state_find(&st, uids[i]))
				continue;
			if (strcasestr_(flags[i], "\\Deleted"))
				continue;
			newuids[nnewuids++] = uids[i];
		}
		fctx.dir = dir;
		fctx.st = &st;
		fctx.nnew = 0;
		fctx.err = NULL;
		if (nnewuids > 0) {
			if (verbose)
				fprintf(stderr, "  fetching %d new\n",
				    nnewuids);
			if (imap_uid_fetch(im, newuids, nnewuids, fetch_cb,
				&fctx, &err) < 0) {
				fprintf(stderr,
				    "wmail-sync: fetch failed: %s\n", err);
				free(err);
				free(newuids);
				if (fctx.err)
					free(fctx.err);
				imap_free(im);
				state_free(&st);
				goto out_free;
			}
		}
		free(newuids);
		nnew = fctx.nnew;
		if (fctx.err) {
			fprintf(stderr, "wmail-sync: %s\n", fctx.err);
			free(fctx.err);
			imap_free(im);
			state_free(&st);
			goto out_free;
		}
	}
	/* Flag changes on messages we already have. */
	for (i = 0; i < n; i++) {
		StateEnt *e = state_find(&st, uids[i]);
		char mdflags[16], *newname = NULL;
		if (!e)
			continue;
		imap_flags_to_maildir(flags[i], mdflags, sizeof mdflags);
		if (strcmp(mdflags, e->flags) == 0)
			continue;
		if (maildir_reflag(dir, e->name, mdflags, &newname,
			&err) < 0) {
			fprintf(stderr, "wmail-sync: reflag failed: %s\n",
			    err);
			free(err);
			err = NULL;
			continue;
		}
		state_put(&st, uids[i], newname, mdflags);
		free(newname);
		nreflag++;
	}
	st.uidvalidity = uidvalidity;
	st.uidnext = uidnext;
	if (state_save(statepath, &st) < 0)
		fprintf(stderr, "wmail-sync: warning: cannot save %s\n",
		    statepath);
	printf("%s: %d new, %d reflagged\n", folder, nnew, nreflag);
	imap_logout(im);
	imap_free(im);
	state_free(&st);
	rc = 0;
out_free:
	for (i = 0; i < n; i++)
		free(flags[i]);
	free(flags);
	free(uids);
out:
	free(dir);
	free(statepath);
	return rc;
}

int
main(int argc, char **argv)
{
	GoaAccount *accs = NULL;
	int naccs = 0, i, opt;
	char *err = NULL;
	const char *account_match = NULL, *root = NULL;
	const char **folders = NULL;
	int nfolders = 0, list_only = 0;
	GoaAccount *acc = NULL;
	GoaCreds creds;
	int rc = 0;

	while ((opt = getopt(argc, argv, "a:f:d:lv")) != -1) {
		switch (opt) {
		case 'a':
			account_match = optarg;
			break;
		case 'f':
			folders = xrealloc(folders,
			    sizeof(char *) * (size_t)(nfolders + 1));
			folders[nfolders++] = optarg;
			break;
		case 'd':
			root = optarg;
			break;
		case 'l':
			list_only = 1;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}

	if (goa_accounts(&accs, &naccs, &err) < 0) {
		fprintf(stderr, "wmail-sync: %s\n", err);
		return 1;
	}
	if (naccs == 0) {
		fprintf(stderr, "wmail-sync: no mail-enabled GOA accounts\n");
		return 1;
	}
	if (list_only) {
		for (i = 0; i < naccs; i++)
			printf("%s\t%s\t%s (%s)\n", accs[i].email,
			    accs[i].provider_name, accs[i].imap_host,
			    accs[i].oauth2 ? "oauth2" : "password");
		goa_free_accounts(accs, naccs);
		return 0;
	}
	if (account_match) {
		for (i = 0; i < naccs; i++) {
			if (strcasestr_(accs[i].email, account_match)) {
				acc = &accs[i];
				break;
			}
		}
		if (!acc) {
			fprintf(stderr,
			    "wmail-sync: no account matching '%s'; available:\n",
			    account_match);
			for (i = 0; i < naccs; i++)
				fprintf(stderr, "  %s\n", accs[i].email);
			goa_free_accounts(accs, naccs);
			return 1;
		}
	} else {
		acc = &accs[0];
	}
	if (!root) {
		root = getenv("WMAIL_DIR");
		if (!root || !root[0]) {
			const char *home = getenv("HOME");
			if (!home) {
				fprintf(stderr,
				    "wmail-sync: no -d, $WMAIL_DIR or $HOME\n");
				goa_free_accounts(accs, naccs);
				return 1;
			}
			root = xstrfmt("%s/Mail", home);
		}
	}
	if (goa_credentials(acc, &creds, &err) < 0) {
		fprintf(stderr, "wmail-sync: %s\n", err);
		goa_free_accounts(accs, naccs);
		return 1;
	}
	if (nfolders == 0) {
		rc = sync_folder(acc, &creds, "INBOX", root);
	} else {
		for (i = 0; i < nfolders; i++) {
			if (sync_folder(acc, &creds, folders[i], root) != 0)
				rc = 1;
		}
	}
	goa_free_creds(&creds);
	goa_free_accounts(accs, naccs);
	free(folders);
	return rc;
}
