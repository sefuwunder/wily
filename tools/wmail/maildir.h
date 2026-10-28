/* maildir.h - maildir storage + sync state for wmail-sync. */
#ifndef WMAIL_MAILDIR_H
#define WMAIL_MAILDIR_H

#include <stddef.h>

/* Create dir/{tmp,new,cur} if missing. 0 = ok, -1 with *err set. */
int maildir_init(const char *dir, char **err);

/* Store a message body. flags are maildir flag letters (e.g. "FS").
 * If flags contains 'S' the file lands in cur/, else new/.
 * On success *name_out is the bare filename (no directory). */
int maildir_store(const char *dir, const char *body, size_t len,
    const char *flags, char **name_out, char **err);

/* Rename a stored file to a new flag set, moving between new/ and cur/
 * as the Seen flag dictates. oldname is the bare filename.
 * On success *newname_out is the new bare filename. */
int maildir_reflag(const char *dir, const char *oldname, const char *flags,
    char **newname_out, char **err);

/* Map an IMAP flag string ("\\Seen \\Flagged") to maildir letters. */
void imap_flags_to_maildir(const char *imap_flags, char *out, size_t outsz);

typedef struct {
	unsigned long uid;
	char *name;	/* bare filename */
	char *flags;	/* maildir letters */
} StateEnt;

typedef struct {
	unsigned long uidvalidity, uidnext;
	StateEnt *ents;
	int n, cap;
} MailState;

int state_load(const char *path, MailState *st);
int state_save(const char *path, MailState *st);
StateEnt *state_find(MailState *st, unsigned long uid);
void state_put(MailState *st, unsigned long uid, const char *name,
    const char *flags);
void state_free(MailState *st);

#endif
