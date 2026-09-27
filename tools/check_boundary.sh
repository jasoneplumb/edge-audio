#!/bin/sh
# Boundary and purity checker — T-13 (proves R-06, I-006) plus the plan's §6 rules:
# no wall-clock reads outside adapters/ (R-07, E-03), no logging in the core (R-10),
# and the single loss funnel: overflow/episode accounting only in src/queue.c (K-04).
# Usage: check_boundary.sh <repo-root>
set -u
root="$1"
fail=0

# Hardware, wall-clock, and logging symbols are forbidden in the core (src/, include/).
core_pats='esp_|freertos|driver/|i2s|I2S|ES8311|es8311|gettimeofday|clock_gettime|localtime|time\(|printf|fputs|fprintf|syslog|NSLog'
hits=$(grep -RInE "$core_pats" "$root/src" "$root/include" 2>/dev/null | grep -v 'speccheck:ignore')
if [ -n "$hits" ]; then
  echo "boundary violations (hardware/clock/logging in core):"
  echo "$hits"
  fail=1
fi

# Loss funnel: drop and episode accounting live only in src/queue.c.
funnel=$(grep -RInE 'frames_dropped|episode' "$root/src" 2>/dev/null \
          | grep -v '^.*src/queue\.c' | grep -v 'speccheck:ignore')
if [ -n "$funnel" ]; then
  echo "loss-funnel violations (overflow accounting outside queue.c):"
  echo "$funnel"
  fail=1
fi

exit $fail
