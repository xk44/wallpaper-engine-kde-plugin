# Benchmark Infrastructure

Tools for measuring wallpaper plugin performance and tracking regressions.

## Files

- `scenarios.json` — Defines benchmark scenarios with expected thresholds
- `bench.sh` — Collects plasmashell CPU/RSS/GPU metrics while wallpaper is active

## Quick Start

```bash
# Measure current wallpaper for 30 seconds
./bench.sh

# Run for 60 seconds, save JSON results
./bench.sh --duration 60 --output results.json

# Tag a scenario
./bench.sh --scenario video-1080p30 --wallpaper /path/to/video.mp4
```

## Scenarios

| ID                   | Type  | Resolution | Target FPS | CPU Threshold |
| -------------------- | ----- | ---------- | ---------- | ------------- |
| video-1080p30        | video | 1920x1080  | 30         | 5%            |
| video-4k60           | video | 3840x2160  | 60         | 15%           |
| scene-light-2d       | scene | 1920x1080  | 30         | 10%           |
| scene-heavy-particle | scene | 1920x1080  | 30         | 25%           |
| web-static           | web   | 1920x1080  | 30         | 15%           |
| web-webgl            | web   | 1920x1080  | 30         | 25%           |
| idle-baseline        | none  | 1920x1080  | —          | 1%            |

## Debug HUD

Enable the on-screen debug HUD for real-time metrics:

- **Video (mpv):** Settings > Video Option > Show Mpv Stats
- **Scene:** Settings > Scene Option > Show Scene Stats

The HUD shows frame count, frame time, render path, and backend-specific details.

## Metrics Collected

- **CPU%** — plasmashell process CPU usage (via /proc)
- **RSS** — resident memory in MB
- **GPU%** — GPU utilization (nvidia-smi if available)

## Interpreting Results

Compare results against the thresholds in `scenarios.json`. A regression is any metric exceeding its threshold by >20%.

The `idle-baseline` scenario is the most important regression target — the plugin should add negligible CPU when no wallpaper is actively rendering.
