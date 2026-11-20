#!/usr/bin/env bash
# Usage: tests/run.sh [SOLVER...]   (default: ../ganak/build/ganak --mode 1)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
TOOL="$HERE/../build/openpsa-to-wmc"
if [ $# -gt 0 ]; then SOLVER=("$@"); else SOLVER=("$HERE/../../ganak/build/ganak" --mode 1); fi
CNF="$(mktemp)"
trap 'rm -f "$CNF"' EXIT
fail=0
check() {
  "$TOOL" --top "$2" -o "$CNF" "$HERE/$1"
  got=$("${SOLVER[@]}" "$CNF" | sed -n 's/^c s exact arb [a-z]* //p')
  if [ "$got" = "$3" ]; then echo "ok   $1 P($2) = $got"; else echo "FAIL $1 P($2): got '$got', want $3"; fail=1; fi
}
check pump.xml TOP 1099/100000
check pump.xml BothPumps 1/100
exit $fail
