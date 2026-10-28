# wmail-sync — GOA mail to maildir, in C

`tools/wmail/wmail-sync` syncs GNOME Online Accounts mail to local
maildirs, one way (IMAP -> disk), incrementally. It is deliberately a
plain C tool in wily's `tools/` tree: no wily core changes, no Bun,
no new runtime beyond OpenSSL.

## What it does

- Discovers mail-enabled GOA accounts with `busctl --json` (the same
  calls outpost's `goa.ts` uses): `GetManagedObjects`, then
  `EnsureCredentials` + `GetAccessToken` (OAuth2) or
  `GetPassword("imap-password")` (password accounts). Credentials are
  fetched on demand and never written to disk by this tool.
- Connects IMAP with TLS: port 993 when the account says `ImapUseSsl`,
  port 143 + STARTTLS when it says `ImapUseTls`. Plaintext IMAP to a
  non-local host is refused. Certificates are verified against the
  system store.
- `SELECT`s each folder, compares `UIDVALIDITY`/`UIDNEXT` against
  `dir/.wmail-state`, downloads new messages verbatim (full RFC822)
  into `tmp/` -> `new/` (unseen) or `cur/` (seen), and renames files
  when flags change. Flag mapping: Seen -> S, Flagged -> F,
  Answered -> R, Deleted -> T (Deleted messages are skipped, not
  downloaded).
- On a `UIDVALIDITY` change it warns and re-downloads (old files are
  left alone; duplicates are possible in that rare case).

## Usage

```
wmail-sync [-a account] [-f folder]... [-d root] [-l] [-v]
```

- `-a` matches a GOA account by email substring (default: first).
- `-f` repeats for folders (default: `INBOX`). `a/b` becomes `a.b`
  on disk.
- `-d` sets the maildir root (default: `$WMAIL_DIR` or `~/Mail`).
  Layout: `root/<email>/<folder>/{tmp,new,cur}` plus
  `root/<email>/<folder>/.wmail-state`.
- `-l` lists GOA mail accounts and exits.

Build: `./build.sh` in `tools/wmail` (needs `libssl-dev`; it builds
`wmail-sync` and the test binary and runs all 45 checks). There is
deliberately no Makefile — this repo's `.gitignore` ignores generated
Makefiles, so the build lives in `build.sh`. Install by copying
`wmail-sync` to a directory on your `PATH`.

## Using it from wily (no core changes)

Sync from a shell, then open the maildir in a wily directory window
and click messages — they are ordinary files:

```
wmail-sync
wily ~/Mail/alice@example.com/INBOX
```

To get a `SyncMail` tag on mail windows, add to `~/.wilytools`
(regexp, tab, tools) and put a wrapper on your `PATH`:

```
^Mail/.*	SyncMail
```

```sh
#!/bin/sh
# SyncMail - put on PATH; B2 on it in any mail window.
wmail-sync && echo synced
```

## Limitations (v1, honest)

- One-way only: no expunge propagation (deleting in wily does not
  delete server-side; a later sync does not resurrect it either, since
  state only tracks what it downloaded — but expunged server messages
  are not removed locally).
- No MIME part extraction: messages are stored raw. HTML mail shows
  as source until a text-rendering step is added.
- No sending yet. `Send`/`Reply` from a wily tag is phase 2: a
  `wmail-send` reusing the same GOA credential flow with SMTP
  (OAuth2 XOAUTH2 or password, per the account's SMTP flags).
- Live verification against a real GOA account + IMAP server has not
  run yet (no GNOME session in this environment); the suite covers
  the GOA parser, the IMAP exchange, and maildir/state. Run
  `wmail-sync -l` first on a real machine, then a sync with `-v`.
