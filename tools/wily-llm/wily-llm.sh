#!/bin/sh
# wily-llm — wily-side shim for loom.
#
# Usage: wily-llm <promptfile>
#   The prompt file's first line is "Question: ..."; the rest is the
#   file path and context the wily LLM builtin assembled. This script
#   submits it as a loom job, prints a thinking line at once, waits
#   for the job to finish, then prints the result.
#
# Nothing here thinks: the model runs in loom's daemon and its
# ephemeral worker processes, and wily stays responsive throughout
# because this script is an ordinary wily child whose stdout is piped
# to the +LLM window as it arrives. Memory cost here is a shell plus
# a few loom CLI calls; the model itself lives in loom's worker and
# exits when the job is done.
#
# This script never starts a daemon, never configures a thinker, and
# never downloads a model. If no loom worker is running, the job waits
# in loom's queue and this script says how to start a worker.
#
# Env:
#   LOOM_DIR              loom checkout (else `loom` on PATH, else
#                         ~/workspace/your_files/loom)
#   LOOM_DATA_DIR         loom database dir (forwarded to the loom CLI)
#   WILEY_LLM_MAX_STEPS   job step cap (default 20; loom's own
#                         default is 200, far more than a Q&A needs)
set -u

PROMPTFILE=${1:-}
if [ -z "$PROMPTFILE" ] || [ ! -f "$PROMPTFILE" ]; then
  echo "usage: wily-llm <promptfile>"
  exit 1
fi

# Resolve the loom CLI and, for outbox marking, the loom root.
ROOT=""
if [ -n "${LOOM_DIR:-}" ] && [ -x "$LOOM_DIR/bin/loom" ]; then
  LOOM="$LOOM_DIR/bin/loom"
  ROOT="$LOOM_DIR"
elif command -v loom >/dev/null 2>&1; then
  LOOM=loom
  LPATH=$(readlink -f "$(command -v loom)" 2>/dev/null || command -v loom)
  case "$LPATH" in
    */bin/loom) ROOT=$(dirname "$(dirname "$LPATH")") ;;
  esac
elif [ -x "$HOME/workspace/your_files/loom/bin/loom" ]; then
  LOOM="$HOME/workspace/your_files/loom/bin/loom"
  ROOT="$HOME/workspace/your_files/loom"
else
  echo "wily-llm: no loom found. Set LOOM_DIR to a loom checkout or put loom on PATH."
  exit 1
fi

if [ -n "${LOOM_DATA_DIR:-}" ]; then
  DB="$LOOM_DATA_DIR/loom.db"
elif [ -n "$ROOT" ]; then
  DB="$ROOT/data/loom.db"
else
  DB=""
fi

NAME=$(sed -n '1p' "$PROMPTFILE" | sed 's/^Question: //' | cut -c1-50)
ID=$("$LOOM" submit "$(cat "$PROMPTFILE")" \
  --name "wily: $NAME" --max-steps "${WILEY_LLM_MAX_STEPS:-20}" 2>/dev/null) || {
  echo "wily-llm: loom submit failed (is bun installed? is LOOM_DIR right?)"
  rm -f "$PROMPTFILE"
  exit 1
}
rm -f "$PROMPTFILE"

echo "LLM thinking… job $ID"
echo "(loom's daemon does the thinking; wily stays responsive)"

# If no worker picks the job up within a minute, say so once and keep
# waiting — a worker may still appear later.
ST="pending"
i=0
while [ "$i" -lt 12 ]; do
  ST=$("$LOOM" status "$ID" 2>/dev/null | sed -n 's/^status: \([a-z]*\).*/\1/p')
  case "$ST" in
    active|done|failed|blocked) break ;;
  esac
  sleep 5
  i=$((i + 1))
done
case "$ST" in
  pending|"")
    echo "LLM: no loom worker has picked job $ID up yet. Start one with:"
    echo "  LOOM_THINKER=\"sh examples/thinker-model.sh\" loom work"
    echo "(waiting anyway — the job runs when a worker appears; killing"
    echo " wily does not kill the thought, it stays in loom's queue)"
    ;;
esac

OUT=$("$LOOM" wait "$ID" 2>&1)
RC=$?
echo "$OUT"

# The result has now been shown in wily, so this job's outbox notices
# are delivered — mark only this job's rows, never anyone else's.
if [ -n "$DB" ] && [ -f "$DB" ] && command -v sqlite3 >/dev/null 2>&1; then
  sqlite3 "$DB" "UPDATE outbox SET delivered=1 WHERE job_id='$ID' AND delivered=0;" 2>/dev/null || true
fi
exit "$RC"
