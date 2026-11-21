#!/bin/bash
# cpu_per_frame.sh LOG SECONDS: CPU time per game frame of each busy thread of the running game (Linux).
#
# WHAT IT IS FOR
#   On a shared or small machine the frame rate of the same binary moves by
#   up to 20% between runs (findings/20 s.11). CPU time per game frame of
#   each thread (game thread, GS thread, window thread, OpenGL threads) is a
#   steadier measure and shows which thread limits the frame rate.
#
# HOW TO USE
#   Run the game with BEN10_FPS_LOG=1 and its stderr in LOG, bring it to the
#   scene to measure, then: tools/perf/cpu_per_frame.sh LOG 90
#   It reads /proc/<pid>/task/*/stat (utime + stime) at the start and the
#   end, counts the game frames logged by src/diagnostics/frame_rate.cpp in
#   between, and prints the threads that used more than one second of CPU.
#   The GS thread is the unnamed "ben10_poe" thread other than the first.

set -euo pipefail
log=$1; seconds=$2
pid=$(pgrep -x ben10_poe | head -1)
snapshot() {
  for t in /proc/"$pid"/task/*; do
    echo "$(basename "$t") $(tr ' ' _ < "$t"/comm) $(awk '{print $14 + $15}' "$t"/stat)"
  done | sort
}
lines0=$(grep -c "\[fps\] game frames" "$log" || true)
before=$(snapshot); t0=$(date +%s.%N)
sleep "$seconds"
lines1=$(grep -c "\[fps\] game frames" "$log" || true)
after=$(snapshot); t1=$(date +%s.%N)
frames=$(grep "\[fps\] game frames" "$log" | tail -n $((lines1 - lines0)) | awk '{n += $4} END {print n + 0}')
ticks=$(getconf CLK_TCK)
awk -v f="$frames" -v a="$t0" -v b="$t1" 'BEGIN {printf "game frames %d in %.1f s: %.2f per second\n", f, b - a, f / (b - a)}'
[ "$frames" -gt 0 ] || exit 0
join <(echo "$before") <(echo "$after") | awk -v f="$frames" -v tk="$ticks" \
  '{d = $5 - $3; if (d > tk) printf "%-8s %-16s %6.1f ms of CPU per game frame\n", $1, $2, d * 1000 / tk / f}'
