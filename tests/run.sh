#!/usr/bin/env bash
# Usage: [GANAK=/path/to/ganak] tests/run.sh [SOLVER...]   (default: $GANAK --mode 1 --coverdrop 0, GANAK defaults to ../ganak/build/ganak)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TOOL="$HERE/../build/openpsa-to-wmc"
GANAK="${GANAK:-$HERE/../../ganak/build/ganak}"
if [ $# -gt 0 ]; then SOLVER=("$@"); else SOLVER=("$GANAK" --mode 1 --coverdrop 0); fi
CNF="$(mktemp)"
trap 'rm -f "$CNF"' EXIT
fail=0
check() {
  local top=$1 want=$2
  shift 2
  "$TOOL" --top "$top" -o "$CNF" "${@/#/$HERE/}"
  got=$("${SOLVER[@]}" "$CNF" | sed -n 's/^c s exact arb [a-z]* //p')
  if [ "$got" = "$want" ]; then echo "ok   $1 P($top) = $got"; else echo "FAIL $1 P($top): got '$got', want $want"; fail=1; fi
}
check TOP 1099/100000 pump.xml
check BothPumps 1/100 pump.xml
check Damage 109/20000 loss-of-cooling.xml loss-of-cooling-data.xml
check OK 19891/20000 loss-of-cooling.xml loss-of-cooling-data.xml
check Cooling.Train.Fail 109/1000 loss-of-cooling.xml loss-of-cooling-data.xml
exit $fail
