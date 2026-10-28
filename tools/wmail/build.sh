#!/bin/sh
# Build wmail-sync and its test suite. Plain cc + OpenSSL, no configure.
# (No Makefile: this repo's .gitignore ignores generated Makefiles.)
set -e
cd "$(dirname "$0")"
cc -O2 -Wall -Wextra -o wmail-sync wmail.c util.c json.c goa.c imap.c maildir.c -lssl -lcrypto
cc -O2 -Wall -Wextra -o tests/test tests/test.c util.c json.c goa.c imap.c maildir.c -lssl -lcrypto
./tests/test
