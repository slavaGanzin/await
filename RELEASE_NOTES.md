# Release Notes

## 2.9.0

### New Features

- **Update notifier.** In an interactive terminal, await checks for a newer release in the background and, when one exists, prints a one-line notice on stderr. The check never delays await: it runs as a detached process and its result is cached for a day in `~/.cache/await/latest-version` (or `$XDG_CACHE_HOME/await`), including failed checks, so offline machines aren't retried on every run. Nothing happens in scripts or CI (stderr not a terminal), and `AWAIT_NO_UPDATE_CHECK=1` turns it off. `--silent` hides the notice.

---

## 2.8.0

### Fixes

- **`\1`, `\2` and `\name` substitution works.** Placeholders were shifted onto a hidden command, so they always came out empty. A command that uses `\N` now waits for command N's first output, and trailing newlines are trimmed like shell `$(...)`.
- **Substituted output can't run as code.** Output is passed to the shell as a variable (`$AWAIT_1`, ...), never spliced into the command text, so `$(...)`, backticks or `;` in a monitored command's output are treated as text, however the placeholder is quoted or nested.
- **`--exec` runs once per trigger.** With `--forever` it was restarted every tick (`--change --forever --exec` ran the action 42 times in 3.5s); now it runs once per change, never overlapping. Its output is shown instead of swallowed.
- **`--change` no longer fires on startup.** The first read is the baseline, not a change.
- **Fractional seconds work** for `-i` / `-T` (`-i 0.5` used to become a busy loop).
- **`--cmd-timeout` / `-t` works.** It only killed the `sh` wrapper, so `await -t 1 'sleep 5'` still waited 5s. Now the command and everything it started are killed, and the run reports status 124, like `timeout(1)`.
- **`--retry` / `-r` counts attempts.** It counted 0.2s display ticks, so `-r 3` gave up after about 0.6s whether or not any command had finished. It now gives up after each command has run N times.
- **No more crashes** on large output (>10KB with `-o`), long commands, or 10+ / 100+ commands. Many commands also start much faster (1000 commands: 22s → 1.5s).
- **`--service` writes a working systemd unit**: all flags are kept (`--name`, `--json`, `--lap` were dropped) and arguments with quotes, `$` or `%` are escaped.
- **`--json` is always valid JSON**, and `elapsed_ms` is measured even without `--timeout`.
- **Shell completions** cover every flag in bash, zsh and fish; fish no longer stops suggesting flags after the first one.

### Improvements

- `NO_COLOR` is respected, and `--help` has no color codes when piped.

### Behavior changes

- With `--exec`, await exits with the exec command's status (it used to always exit 0). With `--json`, exec output goes to stderr so stdout stays one JSON document.
- With `--change` and several commands, all of them must change (or any, with `--any`); before, only the last one counted.
- A substituted value is always one argument: `kill \1` with several PIDs in the output no longer splits them.

---

## 2.7.0

### New Features

- **`--lap` / `-l`** — Show each command's last run duration in the spinner, colored green when faster than the previous run and red when slower. Handy for spotting performance regressions while watching a command loop.

### Fixes

- Passing a bare URL as a command (e.g. `await --timeout 60 http://example.com`) now prints a clear hint to wrap it in `curl` instead of failing with a cryptic `sh: ...: No such file or directory`.
- A command that exits with status 127 (not found) now gets a one-time note suggesting a typo check or missing PATH entry, instead of only the raw shell error.
- On timeout, await now prints each command's last exit status (including friendly labels for "command not found" and "permission denied") so you know exactly what was still failing when it gave up.

---

## 2.6.0

### New Features

- **`--name` / `-n`** — Label a command with a short name. The label appears in the spinner instead of the full command string, and can be used as `\name` in `--exec` for named output substitution (e.g. `await --name db 'pg_isready' --exec 'echo \db is ready'`).
- **`--json` / `-j`** — Output a JSON result on exit containing `success`, `elapsed_ms`, and a `commands` array with each command's name, status, and output. Useful for AI agent workflows and scripted pipelines that need to parse await results.

---

## 2.5.0

### Improvements

- `-i` / `--interval` now takes **seconds** instead of milliseconds (e.g. `--interval 1` = 1 second). More intuitive for human use.
- `-T` / `--timeout` now takes **seconds** instead of milliseconds.
- Fixed concurrent command handling on macOS (reduced thread race conditions).

---

## Earlier versions

See git tags for history prior to 2.5.0.
