Session save and restore
=========================

Wily can save its window layout on exit and restore it on the next
start, so a session survives quitting (and a crash, up to the last
clean exit).

The session file
----------------

By default the session lives in `$HOME/.wily-session`. Two things
change that:

* the `WILYSESSION` environment variable, and
* the `-s path` command-line option, which wins over both.

Unsaved buffers are backed up next to the session file, in a
directory named `<session>.d`, as `N.bak` files. Backups for windows
that no longer exist are pruned on the next save.

What is saved
------------

For each visible column, in order: its width share, and for each
window in it: the file (stored as its resolved path, so a session
saved in one directory restores the same files from another), the
cursor position, the scroll origin, and the height share. Windows
holding files that no longer exist on disk are skipped on restore
(with a diagnostic); windows that were never saved are recreated
empty. A window with unsaved edits is restored with its backed-up
contents, and comes back dirty (Put reappears), exactly as if the
edits had just been typed. The disk file is never touched by a
restore.

The save is written to a temporary file and renamed over the old
session, so a crash mid-save cannot corrupt the previous one.

The current color theme is saved with the session, as a
`theme <name>` line (see `themes.md`); a session file without one
restores with whatever theme is current.

When it happens
--------------

* On every exit — normal quit, or SIGHUP/SIGINT/SIGTERM — the
  session is saved automatically before the usual backups run.
* On startup, a session is restored automatically, but only when
  wily is started with no file arguments and no `-e` command. File
  arguments or `-e` always win; the session is left alone.
* The `SaveSession` and `RestoreSession` commands (also on the main
  tag) save or restore on demand. Both take an optional path
  argument; without one they use the current session file.
  Restoring mid-session adds the saved columns' windows to the
  existing layout; it never closes what is already open.

Format
------

The file is plain text, one record per line (see `sessionfmt.c`):

    wily-session 1
    col <width-permille>
    win <label-length> <label> <dot> <origin> <height-permille>
        <kind> <backup-index-or--1>

`kind` is 0 for a file on disk, 1 for a never-saved buffer. Labels
are length-prefixed, so spaces and other characters in file names
are safe. The format is deliberately boring: it is parsed by a
small standalone reader (`sessionfmt.c`, no wily dependencies) with
defensive limits, and malformed input is rejected rather than
half-restored.

Known limitations
-----------------

* A window created with Split is two views of one file in wily;
  the session stores them as two independent windows, which restore
  as two independent windows (same content, no longer synced).
* A session lost to SIGKILL (or a machine crash) is only as recent
  as the last clean exit or SaveSession; there is no periodic
  autosave.
