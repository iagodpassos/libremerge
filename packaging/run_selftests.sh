#!/bin/bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Run the app's headless selftest suite against a given binary.
# Usage: run_selftests.sh <libremerge-binary>
# Fixtures are regenerated before every test (selftest-save mutates them).
set -u
BIN="${1:?usage: run_selftests.sh <libremerge-binary>}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

make_fixtures() {
  awk 'BEGIN{for(i=1;i<=800;i++) print "line " i " common text here"}' \
    > "$TMP/left.txt"
  awk 'BEGIN{for(i=1;i<=800;i++){
    if(i%100==0) print "line " i " CHANGED on right";
    else if(i==250){print "line 250 common text here"; print "extra right line"}
    else print "line " i " common text here"}}' > "$TMP/right.txt"
  printf 'id,name,price\n1,apple,10\n2,banana,20\n3,cherry,30\n4,date,40\n' \
    > "$TMP/left.csv"
  printf 'id,name,price\n1,apple,11\n2,banana,20\n3,cereja,30\n5,elder,50\n' \
    > "$TMP/right.csv"
}

fails=0
run_one() { # $1 = selftest name, $2/$3 = files
  make_fixtures
  if "$BIN" "--$1" "$2" "$3" > /dev/null 2>&1; then
    echo "ok: $1"
  else
    echo "FAIL: $1 (exit $?)"
    fails=$((fails + 1))
  fi
}

for t in selftest-merge selftest-save selftest-undo selftest-undo-scroll \
         selftest-nav selftest-copy; do
  run_one "$t" "$TMP/left.txt" "$TMP/right.txt"
done
run_one selftest-table "$TMP/left.csv" "$TMP/right.csv"

echo "== $fails failure(s)"
exit "$((fails > 0 ? 1 : 0))"
