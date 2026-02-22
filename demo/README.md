# VHS Demo Recordings for `await`

This directory contains VHS tape files and generated MP4 terminal recordings demonstrating all key features of the `await` CLI tool.

## Generated Demos

All 8 demos successfully generated and copied to Remotion project:

| Demo File | Size | Description |
|-----------|------|-------------|
| `demo-faang-failure.mp4` | 72K | Monitoring multiple FAANG services with `--fail` flag |
| `demo-website-monitoring.mp4` | 545K | Forever monitoring with change detection and notifications |
| `demo-concurrent-watch.mp4` | 189K | Parallel process watching (stylus + pug compilation) |
| `demo-placeholder.mp4` | 61K | Placeholder substitution with `\1`, `\2`, `\3` |
| `demo-iphone-daemon.mp4` | 235K | Daemon mode with API polling and notifications |
| `demo-redis-restart.mp4` | 100K | Service restart + connection verification workflow |
| `demo-auto-compile.mp4` | 89K | Auto-recompile on C file changes |
| `demo-timeout.mp4` | 68K | **NEW**: Timeout feature demonstration |

**Total Size:** ~1.4MB

## VHS Tape Files

Each `.tape` file defines a terminal recording session:

- **1.tape** → demo-faang-failure.mp4
- **2.tape** → demo-website-monitoring.mp4
- **3.tape** → demo-concurrent-watch.mp4
- **4.tape** → demo-placeholder.mp4
- **5.tape** → demo-iphone-daemon.mp4
- **6.tape** → demo-redis-restart.mp4
- **7.tape** → demo-auto-compile.mp4
- **8-timeout.tape** → demo-timeout.mp4
- **test.tape** → test.mp4 (simple test demo)

## Regenerating Demos

To regenerate all demos:

```bash
cd /Users/slava/work/await/demo
./generate-vhs.sh
```

To regenerate a specific demo:

```bash
vhs 1.tape  # Generates demo-faang-failure.mp4
```

## VHS Gotchas & Solutions

### 1. Filename Parsing
❌ **Don't use**: `Output 1-demo.mp4` (VHS parser fails)
✅ **Use**: `Output demo-feature.mp4`

### 2. Quote Escaping
❌ **Don't use**: `Type "await \"command\" --flag"`
✅ **Use**: `Type "await 'command' --flag"`

VHS parser interprets escaped quotes and special shell characters (`&`, `>`, `|`) as VHS commands rather than literal text.

### 3. Working Pattern
```tape
Output demo-name.mp4
Set FontSize 15
Set Width 1322
Set Height 700
Set Theme "Dracula"
Set TypingSpeed 100ms

Type "await 'simple command' --flag"
Enter
Sleep 2s
```

## Next Steps

The generated MP4 files are now in:
- **Source**: `/Users/slava/work/await/demo/demo-*.mp4`
- **Remotion**: `/Users/slava/work/await/await-video/public/demos/demo-*.mp4`

Ready for Remotion video scene creation!
