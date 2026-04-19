#!/usr/bin/env bash
# bench.sh — Wallpaper Engine KDE Plugin benchmark runner
#
# Collects system-level metrics (CPU, RSS, GPU) while a wallpaper is active.
# Designed to run on a live Plasma session with the plugin installed.
#
# Usage:
#   ./bench.sh [--duration SEC] [--output FILE] [--scenario ID] [--wallpaper PATH]
#
# Examples:
#   # Run for 30s, measure current wallpaper
#   ./bench.sh
#
#   # Run specific scenario with a wallpaper file
#   ./bench.sh --scenario video-1080p30 --wallpaper /path/to/video.mp4 --duration 60
#
#   # Output JSON results to file
#   ./bench.sh --output results.json
#
# Requirements:
#   - plasmashell running with the plugin
#   - Optional: nvidia-smi (for GPU metrics), intel_gpu_top, or radeontop

set -euo pipefail

DURATION=30
OUTPUT=""
SCENARIO=""
WALLPAPER=""
SAMPLE_INTERVAL=0.5

while [[ $# -gt 0 ]]; do
    case "$1" in
        --duration)   DURATION="$2"; shift 2 ;;
        --output)     OUTPUT="$2"; shift 2 ;;
        --scenario)   SCENARIO="$2"; shift 2 ;;
        --wallpaper)  WALLPAPER="$2"; shift 2 ;;
        -h|--help)
            sed -n '2,/^$/s/^# \?//p' "$0"
            exit 0
            ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

# ── Helpers ──────────────────────────────────────────────────────────────────

find_plasmashell_pid() {
    pgrep -x plasmashell | head -1
}

# Get CPU% for a PID over a sample window
sample_cpu() {
    local pid=$1
    # Use /proc/stat + /proc/$pid/stat for accurate measurement
    if [[ ! -d "/proc/$pid" ]]; then
        echo "0"
        return
    fi

    local clk_tck
    clk_tck=$(getconf CLK_TCK)

    # Read initial values
    local stat1 uptime1
    read -r _ stat1 < <(awk '{print NR, $14+$15}' "/proc/$pid/stat")
    uptime1=$(awk '{print $1}' /proc/uptime)

    sleep "$SAMPLE_INTERVAL"

    if [[ ! -d "/proc/$pid" ]]; then
        echo "0"
        return
    fi

    local stat2 uptime2
    read -r _ stat2 < <(awk '{print NR, $14+$15}' "/proc/$pid/stat")
    uptime2=$(awk '{print $1}' /proc/uptime)

    # CPU% = delta_ticks / (delta_uptime * CLK_TCK) * 100
    awk "BEGIN { dt = ($stat2 - $stat1); du = ($uptime2 - $uptime1) * $clk_tck; if(du>0) printf \"%.2f\", dt/du*100; else print 0 }"
}

# Get RSS in MB for a PID
sample_rss_mb() {
    local pid=$1
    if [[ -f "/proc/$pid/status" ]]; then
        awk '/VmRSS/ {printf "%.1f", $2/1024}' "/proc/$pid/status"
    else
        echo "0"
    fi
}

# Get GPU utilization if available
sample_gpu() {
    if command -v nvidia-smi &>/dev/null; then
        nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null | head -1 || echo ""
    else
        echo ""
    fi
}

# ── Main ─────────────────────────────────────────────────────────────────────

PLASMA_PID=$(find_plasmashell_pid)
if [[ -z "$PLASMA_PID" ]]; then
    echo "ERROR: plasmashell not running" >&2
    exit 1
fi

echo "=== Wallpaper Engine KDE Plugin Benchmark ==="
echo "  plasmashell PID: $PLASMA_PID"
echo "  duration: ${DURATION}s"
echo "  sample interval: ${SAMPLE_INTERVAL}s"
[[ -n "$SCENARIO" ]] && echo "  scenario: $SCENARIO"
[[ -n "$WALLPAPER" ]] && echo "  wallpaper: $WALLPAPER"
echo ""

# Collect samples
declare -a cpu_samples=()
declare -a rss_samples=()
declare -a gpu_samples=()

NUM_SAMPLES=$(awk "BEGIN { printf \"%d\", $DURATION / $SAMPLE_INTERVAL }")
echo "Collecting $NUM_SAMPLES samples..."

for ((i = 0; i < NUM_SAMPLES; i++)); do
    cpu=$(sample_cpu "$PLASMA_PID")
    rss=$(sample_rss_mb "$PLASMA_PID")
    gpu=$(sample_gpu)

    cpu_samples+=("$cpu")
    rss_samples+=("$rss")
    [[ -n "$gpu" ]] && gpu_samples+=("$gpu")

    # Progress indicator every 10 samples
    if (( (i + 1) % 10 == 0 )); then
        echo "  [$((i + 1))/$NUM_SAMPLES] cpu=${cpu}% rss=${rss}MB${gpu:+ gpu=${gpu}%}"
    fi
done

echo ""

# ── Compute stats ────────────────────────────────────────────────────────────

compute_stats() {
    local -n arr=$1
    local n=${#arr[@]}
    if (( n == 0 )); then
        echo "0 0 0 0"
        return
    fi
    printf '%s\n' "${arr[@]}" | awk '
    BEGIN { min=999999; max=0; sum=0; n=0 }
    {
        v = $1 + 0
        sum += v; n++
        if (v < min) min = v
        if (v > max) max = v
    }
    END {
        avg = (n > 0) ? sum/n : 0
        printf "%.2f %.2f %.2f %d", avg, min, max, n
    }'
}

read -r cpu_avg cpu_min cpu_max cpu_n <<< "$(compute_stats cpu_samples)"
read -r rss_avg rss_min rss_max rss_n <<< "$(compute_stats rss_samples)"

echo "=== Results ==="
echo "  CPU%:  avg=$cpu_avg  min=$cpu_min  max=$cpu_max  (n=$cpu_n)"
echo "  RSS:   avg=${rss_avg}MB  min=${rss_min}MB  max=${rss_max}MB"

if (( ${#gpu_samples[@]} > 0 )); then
    read -r gpu_avg gpu_min gpu_max gpu_n <<< "$(compute_stats gpu_samples)"
    echo "  GPU%:  avg=$gpu_avg  min=$gpu_min  max=$gpu_max  (n=$gpu_n)"
fi

# ── JSON output ──────────────────────────────────────────────────────────────

json_result() {
    local gpu_section=""
    if (( ${#gpu_samples[@]} > 0 )); then
        read -r gpu_avg gpu_min gpu_max gpu_n <<< "$(compute_stats gpu_samples)"
        gpu_section=$(cat <<GPUEOF
    "gpu_percent": { "avg": $gpu_avg, "min": $gpu_min, "max": $gpu_max, "samples": $gpu_n },
GPUEOF
)
    fi

    cat <<EOF
{
  "timestamp": "$(date -Iseconds)",
  "scenario": "${SCENARIO:-manual}",
  "wallpaper": "${WALLPAPER:-current}",
  "duration_s": $DURATION,
  "plasmashell_pid": $PLASMA_PID,
  "cpu_percent": { "avg": $cpu_avg, "min": $cpu_min, "max": $cpu_max, "samples": $cpu_n },
  "rss_mb": { "avg": $rss_avg, "min": $rss_min, "max": $rss_max, "samples": $rss_n },
$gpu_section
  "host": {
    "kernel": "$(uname -r)",
    "hostname": "$(hostname)"
  }
}
EOF
}

if [[ -n "$OUTPUT" ]]; then
    json_result > "$OUTPUT"
    echo ""
    echo "Results written to: $OUTPUT"
else
    echo ""
    echo "--- JSON ---"
    json_result
fi
