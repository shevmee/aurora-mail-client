#!/usr/bin/env bash
# measure-resources.sh — sample Aurora Mail resource consumption on macOS.
#
# Phase 1 (cold start):
#   * fork the binary, capture wall time until the process is visible in `ps`
#     and its resident set has stabilised (delta-RSS over a 1 s window stays
#     below STABLE_DELTA_MB megabytes for two consecutive samples)
#   * print: cold-start time (s), RSS at "ready" (MB), VSZ at "ready" (MB)
#
# Phase 2 (sampling loop):
#   * every SAMPLE_PERIOD_S seconds, append a CSV row:
#       epoch_s,elapsed_s,rss_mb,vsz_mb,cpu_pct,thread_count
#     where cpu_pct is sampled with `top -l 2 -pid <pid> -stats cpu` and the
#     reading from the SECOND iteration is used (the first reading from
#     `top -l 1` is always 0 by design).
#
# Stops on: SIGINT (Ctrl+C), SIGTERM, or when the target process exits.
# All output paths are inside the workspace; nothing is written to /tmp.
#
# Usage:
#   scripts/measure-resources.sh [--label LABEL] [--duration SECONDS]
# Examples:
#   scripts/measure-resources.sh --label cold-start --duration 30
#   scripts/measure-resources.sh --label idle-30min --duration 1800

set -u
set -o pipefail

# --- Config ---------------------------------------------------------------
APP_BIN="${APP_BIN:-build/macos-release/desktop/aurora-mail.app/Contents/MacOS/aurora-mail}"
SAMPLE_PERIOD_S="${SAMPLE_PERIOD_S:-2}"
STABLE_DELTA_MB="${STABLE_DELTA_MB:-2}"      # |RSS_t - RSS_{t-1}| <= this => stable
STABLE_WINDOW_S="${STABLE_WINDOW_S:-1}"
COLD_START_TIMEOUT_S="${COLD_START_TIMEOUT_S:-15}"
LABEL="run"
DURATION_S=0                                  # 0 = run until SIGINT or process exit

while [ $# -gt 0 ]; do
  case "$1" in
    --label)    LABEL="$2"; shift 2 ;;
    --duration) DURATION_S="$2"; shift 2 ;;
    --help|-h)
      grep -E '^# ' "$0" | sed 's/^# \{0,1\}//'
      exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 64 ;;
  esac
done

OUT_DIR="measurements"
mkdir -p "$OUT_DIR"
TS=$(date +%Y%m%d-%H%M%S)
CSV="$OUT_DIR/${LABEL}-${TS}.csv"
LOG="$OUT_DIR/${LABEL}-${TS}.log"

if [ ! -x "$APP_BIN" ]; then
  echo "ERROR: binary not found / not executable: $APP_BIN" | tee -a "$LOG"
  exit 1
fi

echo "=== Aurora Mail resource sampler ===" | tee -a "$LOG"
echo "binary       : $APP_BIN"             | tee -a "$LOG"
echo "label        : $LABEL"               | tee -a "$LOG"
echo "csv          : $CSV"                 | tee -a "$LOG"
echo "log          : $LOG"                 | tee -a "$LOG"
echo "sample period: ${SAMPLE_PERIOD_S}s"  | tee -a "$LOG"
echo "duration     : ${DURATION_S}s (0 = until exit/SIGINT)" | tee -a "$LOG"
echo                                       | tee -a "$LOG"

# --- Helpers --------------------------------------------------------------

# Read RSS (KB) and CPU% (over a 1 s top window) and thread count for a pid.
# Print "rss_kb cpu_pct thread_count" or empty if pid is gone.
#
# VSZ on macOS is the entire process address space (~400 GB once Qt frameworks
# are linked) and bears no relation to actual memory pressure — we deliberately
# do not report it.
sample_pid() {
  local pid="$1"
  if ! kill -0 "$pid" 2>/dev/null; then
    return 1
  fi
  local rss_kb
  rss_kb=$(ps -o rss= -p "$pid" 2>/dev/null | awk '{print $1+0}')
  if [ -z "$rss_kb" ] || [ "$rss_kb" = "0" ]; then
    return 1
  fi
  local thr
  thr=$(ps -M -p "$pid" 2>/dev/null | awk 'NR>1' | wc -l | awk '{print $1+0}')
  if [ -z "$thr" ] || [ "$thr" = "0" ]; then thr="?"; fi
  # `top -l 2` gives a real CPU%; iteration 1 is always 0.0 by design.
  local cpu
  cpu=$(top -l 2 -pid "$pid" -stats cpu -s 1 2>/dev/null \
        | awk '/^[0-9]+\.[0-9]+/ {v=$1} END{print v+0}')
  if [ -z "$cpu" ]; then cpu="0.0"; fi
  printf "%s %s %s\n" "$rss_kb" "$cpu" "$thr"
}

# Print disk size (in MB) of the encrypted message cache directory, or 0 if
# unreadable.
cache_size_mb() {
  local dir="$HOME/Library/Application Support/Aurora/Aurora Mail/messagecache"
  if [ ! -d "$dir" ]; then echo 0; return; fi
  du -sk "$dir" 2>/dev/null | awk '{printf "%.1f", $1/1024}'
}

# --- Phase 1: cold start --------------------------------------------------

START_NS=$(date +%s)
"$APP_BIN" >"$OUT_DIR/${LABEL}-${TS}.stdout" 2>"$OUT_DIR/${LABEL}-${TS}.stderr" &
PID=$!
trap 'echo; echo "stopping sampler (signal)..." | tee -a "$LOG"; kill "$PID" 2>/dev/null; exit 0' INT TERM

# Wait until process is visible in ps (usually <100ms after fork)
WAITED=0
while ! kill -0 "$PID" 2>/dev/null; do
  sleep 0.05
  WAITED=$((WAITED + 1))
  if [ "$WAITED" -gt 200 ]; then
    echo "ERROR: process did not start within 10s" | tee -a "$LOG"
    exit 1
  fi
done

CACHE_BEFORE_MB=$(cache_size_mb)
echo "[disk] cache size BEFORE run: ${CACHE_BEFORE_MB} MB"        | tee -a "$LOG"

LAST_RSS_KB=0
STABLE_HITS=0
COLD_READY_S=""
COLD_READY_RSS_MB=""

while :; do
  NOW=$(date +%s)
  ELAPSED=$((NOW - START_NS))
  if [ "$ELAPSED" -gt "$COLD_START_TIMEOUT_S" ]; then
    echo "WARN: cold-start did not stabilise within ${COLD_START_TIMEOUT_S}s — continuing anyway" | tee -a "$LOG"
    COLD_READY_S="$ELAPSED"
    break
  fi
  S=$(sample_pid "$PID") || { echo "process exited during cold-start phase"; exit 1; }
  RSS_KB=$(echo "$S" | awk '{print $1}')
  DELTA_KB=$(( RSS_KB > LAST_RSS_KB ? RSS_KB - LAST_RSS_KB : LAST_RSS_KB - RSS_KB ))
  DELTA_MB=$(( DELTA_KB / 1024 ))
  if [ "$LAST_RSS_KB" -gt 0 ] && [ "$DELTA_MB" -le "$STABLE_DELTA_MB" ]; then
    STABLE_HITS=$((STABLE_HITS + 1))
  else
    STABLE_HITS=0
  fi
  if [ "$STABLE_HITS" -ge 2 ]; then
    COLD_READY_S="$ELAPSED"
    COLD_READY_RSS_MB=$(( RSS_KB / 1024 ))
    break
  fi
  LAST_RSS_KB="$RSS_KB"
  sleep "$STABLE_WINDOW_S"
done

echo "[cold-start] ready in ${COLD_READY_S}s, RSS=${COLD_READY_RSS_MB} MB" | tee -a "$LOG"
echo "[cold-start] pid=${PID}"                                    | tee -a "$LOG"

# --- Phase 2: sampling loop ----------------------------------------------

echo "epoch_s,elapsed_s,rss_mb,cpu_pct,threads" > "$CSV"
PHASE2_START=$(date +%s)
echo "[sample] writing CSV every ${SAMPLE_PERIOD_S}s; press Ctrl+C to stop" | tee -a "$LOG"

while :; do
  if ! kill -0 "$PID" 2>/dev/null; then
    echo "[sample] target process exited; stopping sampler" | tee -a "$LOG"
    break
  fi
  NOW=$(date +%s)
  ELAPSED=$((NOW - PHASE2_START))
  if [ "$DURATION_S" -gt 0 ] && [ "$ELAPSED" -ge "$DURATION_S" ]; then
    echo "[sample] reached requested duration ${DURATION_S}s; stopping" | tee -a "$LOG"
    kill "$PID" 2>/dev/null
    wait "$PID" 2>/dev/null
    break
  fi
  S=$(sample_pid "$PID") || { echo "[sample] target gone"; break; }
  RSS_KB=$(echo "$S" | awk '{print $1}')
  CPU=$(echo "$S" | awk '{print $2}')
  THR=$(echo "$S" | awk '{print $3}')
  RSS_MB=$(( RSS_KB / 1024 ))
  printf "%s,%s,%s,%s,%s\n" "$NOW" "$ELAPSED" "$RSS_MB" "$CPU" "$THR" >> "$CSV"
  printf "  t=%4ds  RSS=%4d MB  CPU=%5s%%  threads=%s\n" \
    "$ELAPSED" "$RSS_MB" "$CPU" "$THR"
  sleep "$SAMPLE_PERIOD_S"
done

# --- Summary -------------------------------------------------------------

echo                                                              | tee -a "$LOG"
CACHE_AFTER_MB=$(cache_size_mb)
echo "=== Summary (${LABEL}) ==="                                 | tee -a "$LOG"
echo "cold-start ready time : ${COLD_READY_S} s"                  | tee -a "$LOG"
echo "RSS at ready          : ${COLD_READY_RSS_MB} MB"            | tee -a "$LOG"
echo "cache size before     : ${CACHE_BEFORE_MB} MB"              | tee -a "$LOG"
echo "cache size after      : ${CACHE_AFTER_MB} MB"               | tee -a "$LOG"
SAMPLES=$(($(wc -l <"$CSV") - 1))
echo "samples in phase 2    : ${SAMPLES}"                         | tee -a "$LOG"
if [ "$SAMPLES" -gt 0 ]; then
  awk -F, 'NR>1 {
    if (NR==2) { rss_min=rss_max=$3; cpu_max=$4 }
    if ($3<rss_min) rss_min=$3; if ($3>rss_max) rss_max=$3;
    rss_sum+=$3; cpu_sum+=$4; if ($4+0>cpu_max+0) cpu_max=$4;
    last_rss=$3;
  } END {
    if (NR>1) {
      printf "RSS min/avg/max/last  : %d / %.1f / %d / %d MB\n", rss_min, rss_sum/(NR-1), rss_max, last_rss;
      printf "CPU avg / max         : %.2f / %s %%\n", cpu_sum/(NR-1), cpu_max;
    }
  }' "$CSV" | tee -a "$LOG"
  # Steady-state slice (last 60s) — useful as a "post-sync" snapshot.
  awk -F, -v dur="$DURATION_S" 'NR>1 {
    rows[NR]=$0;
  } END {
    if (NR<=1) exit;
    cutoff = (dur>60 ? dur-60 : 0);
    cnt=0;
    for (i=2; i<=NR; i++) {
      split(rows[i], f, ",");
      if (f[2]+0 >= cutoff) {
        cnt++; rss_sum += f[3]; cpu_sum += f[4];
        if (cnt==1) { rss_min=rss_max=f[3] }
        if (f[3]<rss_min) rss_min=f[3];
        if (f[3]>rss_max) rss_max=f[3];
      }
    }
    if (cnt>0) {
      printf "[steady-state, last 60s]  RSS min/avg/max = %d / %.1f / %d MB ; CPU avg = %.2f %%\n",
             rss_min, rss_sum/cnt, rss_max, cpu_sum/cnt;
    }
  }' "$CSV" | tee -a "$LOG"
fi
echo "csv: $CSV"                                                  | tee -a "$LOG"
echo "log: $LOG"                                                  | tee -a "$LOG"
