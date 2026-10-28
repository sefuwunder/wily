# LLM: ask loom from wily, without blocking wily

`LLM [question]` is a wily builtin that hands a question to loom and
delivers the answer into a `+LLM` window when it is ready. The minutes
of thinking happen in loom's daemon and its ephemeral worker
processes; wily's event loop is never held hostage — you keep editing
while the answer is being thought, and it lands in `+LLM` when done.

## Setup

1. Put the watcher script on your path (it is the only new moving
   part on wily's side):

   ```
   ln -s $PWD/tools/wily-llm/wily-llm.sh ~/.local/bin/wily-llm
   ```

   Or set `WILEY_LLM` to the script's full path. Rebuild wily
   (`cd wily && make`) and restart it.
2. Have a loom daemon running with a thinker, e.g.:

   ```
   LOOM_THINKER="sh examples/thinker-model.sh" loom work
   ```

   The shim never starts a daemon, never configures a thinker, and
   never downloads a model. If no worker picks the job up within a
   minute, the `+LLM` window says how to start one, and keeps waiting
   — a worker that appears later still runs the job. Killing wily
   does not kill the thought: it stays in loom's queue (`loom board`,
   `loom status <id>`).

3. Tell the shim where loom lives, if it cannot be found on your
   path: `LOOM_DIR` (a loom checkout) or `LOOM_DATA_DIR` (its
   database dir). The defaults mirror sherry's loom wiring.

## Use

Type the command in any tag and middle-click it:

```
LLM why does this loop terminate
```

- With a question and a body selection, the selection is context
  (clipped to 4000 chars, and the clip is noted in the prompt so the
  thinker knows it is not seeing everything).
- Select text and chord-click `LLM` (middle button, then left while
  holding it): the selection arrives as the argument, is recognised
  as context rather than a question, and the question defaults to
  explaining it.
- `LLM` with no question and no selection summarises the window
  (clipped the same way).
- The window's file path is named in full in the prompt, so the
  thinker can read the file itself with loom's tools. Note the
  trust that implies: loom's thinker can already run commands, so a
  question about a file hands that file — and that directory — to
  the agent, exactly as a loom job would.

Answers accumulate in the `+LLM` window beside the file they were
asked from, newest last. Each answer arrives selected, ready to
Snarf; typing in `+LLM` replaces the selection, as with any piped
output. The watcher script shows in the main tag's running list
while it waits, so `Kill wily-llm` stops the wait (the loom job
itself keeps running unless you also `loom cancel` it).

## Bounds

- One job per invocation, capped at 20 steps (`WILEY_LLM_MAX_STEPS`
  overrides; loom's own default is 200). The daemon's wall-clock
  budget still applies.
- Memory: the shim itself is a shell script plus a few loom CLI
  calls. The model lives in loom's worker — about 3.6GB at most with
  the bundled 2B model file — and exits when the job is done. Wily
  holds no model, no thread, and no timer for this; the watcher is
  an ordinary wily child whose stdout is piped to `+LLM`.
- After the answer is shown, the shim marks only that job's loom
  outbox notices delivered, so other outbox readers (e.g. sherry)
  do not announce the same answer again.

## Customisation

`LLM` is deliberately not in the default file tag. To put it there,
set `WFILETAG`, e.g. `WFILETAG="Del Look Bigger Smaller LLM ."`,
before starting wily. That is also how the lab test invokes it.

## How it is built

- `wily/builtins.c`: the `LLM` builtin (prompt assembly, `+LLM`
  window, first-output-replaces-selection parked at end-of-text so
  answers append to the log).
- `wily/exec.c` / `wily/proto.h`: `run_to_view()`, which runs a
  command with stdout piped to a given view and stderr to `+Errors`,
  like a `|` pipe command but without touching the selection.
- `tools/wily-llm/wily-llm.sh`: submit, print a thinking line at
  once, wait (`loom wait`), print the result, mark the outbox.
