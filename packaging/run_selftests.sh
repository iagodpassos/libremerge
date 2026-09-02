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
  # 3-way: one line, three versions (pane-relative merge flow)
  printf 'alpha\nversion = "A"\ncommon\n' > "$TMP/w3a.txt"
  printf 'alpha\nversion = "B"\ncommon\n' > "$TMP/w3b.txt"
  printf 'alpha\nversion = "C"\ncommon\n' > "$TMP/w3c.txt"
  # 64x48 white PNG vs the same with a red and a green box (2 differences)
  base64 -d > "$TMP/left.png" <<'PNG'
iVBORw0KGgoAAAANSUhEUgAAAEAAAAAwCAYAAAChS3wfAAAAdElEQVR4nO3QQREAAAiAMPuX1hh7yBJwzD43OkBrgA7QGqADtAboAK0BOkBrgA7QGqADtAboAK0BOkBrgA7QGqADtAboAK0BOkBrgA7QGqADtAboAK0BOkBrgA7QGqADtAboAK0BOkBrgA7QGqADtAboAO0AM+HSwnsOK34AAAAASUVORK5CYII=
PNG
  base64 -d > "$TMP/right.png" <<'PNG'
iVBORw0KGgoAAAANSUhEUgAAAEAAAAAwCAYAAAChS3wfAAAAiElEQVR4nO3QQQrDMBAEwX16fm6fc7INMkWibtBNLEPNsXmjB+gC0AN0AegBugD0AF0AeoDuPsDMs/cjBXD/ZwABBBBAAAEEEEAAmwL8aQHoAboA9ABdAHqALgA94Kr5zNdbfn/5xcUFEMDmAG8XgB6gC0AP0AWgB+gC0AN0AegBugD0AN32ACcCDIwDSDcRXgAAAABJRU5ErkJggg==
PNG
}

fails=0
run_one() { # $1 = selftest name, $2/$3[/$4] = files
  make_fixtures
  if "$BIN" "--$1" "${@:2}" > /dev/null 2>&1; then
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
run_one selftest-undo-rescan
run_one selftest-undo-ghosts
run_one selftest-open-enter
run_one selftest-table "$TMP/left.csv" "$TMP/right.csv"
run_one selftest-image "$TMP/left.png" "$TMP/right.png"
run_one selftest-merge3 "$TMP/w3a.txt" "$TMP/w3b.txt" "$TMP/w3c.txt"

echo "== $fails failure(s)"
exit "$((fails > 0 ? 1 : 0))"
