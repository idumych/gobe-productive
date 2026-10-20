#!/bin/sh
# Build the portable core on the host and compare its parse of every original
# binary with tools/gobersrc.py. usage: port/tests/check_core.sh (from project root)
set -eu
OUT=${TMPDIR:-/tmp}/gobe-core-check
mkdir -p "$OUT"
c++ -std=c++17 -Wall -Wextra -O1 -Iport/src/core port/tests/core_dump.cpp \
	port/src/core/*.cpp -o "$OUT/core_dump"
A="original/installed/Gobe Productive 2.0b2/Gobe Productive 2.0"
status=0
for f in "$A/Gobe Productive" "$A/Parts/"*; do
	case "$f" in *.json) continue ;; esac
	n=$(basename "$f")
	"$OUT/core_dump" "$f" > "$OUT/$n.cpp.txt" 2>/dev/null || true
	python3 -I tools/gobersrc.py "original/resources/$n" > "$OUT/$n.json"
	python3 -I port/tests/expected_from_python.py "$OUT/$n.json" > "$OUT/$n.py.txt"
	if diff -q "$OUT/$n.py.txt" "$OUT/$n.cpp.txt" > /dev/null; then
		echo "ok    $n ($(wc -l < "$OUT/$n.cpp.txt") lines)"
	else
		echo "DIFF  $n"; diff "$OUT/$n.py.txt" "$OUT/$n.cpp.txt" | head -10
		status=1
	fi
done
exit $status
