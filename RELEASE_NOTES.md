# Release Notes

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
