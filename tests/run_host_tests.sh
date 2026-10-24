#!/bin/sh
# Builds and runs every host test on the sample and label documents and prints one
# PASS/FAIL line per test. usage: port/tests/run_host_tests.sh [test...]
# (from the project root or port/; default: all). Exit status 1 if any fail.
# A test FAILs if it exits nonzero, reports failures or differences, or
# reports that it checked nothing (a count of 0: wrong input files).
cd "$(dirname "$0")/.." || exit 1
OUT=${TMPDIR:-/tmp}/gobe-host-tests
mkdir -p "$OUT/core"
# the 14 samples and 30 label templates (tables), not their .attrs.json
APP=$(ls -d ../original/installed/*/"Gobe Productive 2.0" 2>/dev/null | head -1)
DOCS="$OUT/documents"
find "$APP/Samples" "$APP/Labels" -type f ! -name '*.json' 2>/dev/null | sort > "$DOCS"
if [ ! -s "$DOCS" ]; then
	echo "FAIL: no documents found in $APP/Samples and $APP/Labels"
	exit 1
fi

# the core once, as objects
for c in src/core/*.cpp; do
	o="$OUT/core/$(basename "$c" .cpp).o"
	if [ ! -e "$o" ] || [ "$c" -nt "$o" ] || [ -n "$(find src/core -name '*.h' -newer "$o")" ]; then
		c++ -std=c++17 -O1 -Wall -Isrc/core -c "$c" -o "$o" 2> "$OUT/build.log" \
			|| { echo "FAIL: $c does not compile:"; grep -E 'error' "$OUT/build.log" | head; exit 1; }
	fi
done

# tests that take no arguments; all others get every sample document
NO_ARGS="text_range text_url doc_measure doc_columns doc_notes doc_wordcount doc_fragment spell"
TOOLS="core_dump ss_dump"	# not tests

TESTS=${*:-$(ls tests/*.cpp | xargs -n1 basename | sed 's/\.cpp$//')}
status=0
for t in $TESTS; do
	case " $TOOLS " in *" $t "*) continue ;; esac
	if ! c++ -std=c++17 -O1 -Wall -Isrc/core "tests/$t.cpp" "$OUT"/core/*.o -lz \
			-o "$OUT/$t" 2> "$OUT/$t.build"; then
		echo "FAIL $t: does not compile: $(grep -m1 'error' "$OUT/$t.build")"
		status=1
		continue
	fi
	warnings=$(grep -c 'warning:' "$OUT/$t.build")
	case " $NO_ARGS " in
		*" $t "*) "$OUT/$t" > "$OUT/$t.out" 2>&1 ;;
		*) tr '\n' '\0' < "$DOCS" | xargs -0 "$OUT/$t" > "$OUT/$t.out" 2>&1 ;;
	esac
	code=$?
	last=$(grep -v '^$' "$OUT/$t.out" | tail -1)
	verdict=PASS
	[ $code -ne 0 ] && verdict=FAIL
	echo "$last" | grep -qE 'failed +[1-9]|[1-9][0-9]* failures|differ(ently)? [1-9]|errors [1-9]|FAIL' \
		&& verdict=FAIL
	# "formulas 0", "ok 0", "edits 0", ...: nothing was checked
	echo "$last" | grep -qE '(formulas|ok|edits|inputs|identically|parts|cells) 0([^0-9.]|$)' \
		&& verdict="FAIL (checked nothing)"
	extra=""
	[ "$warnings" -gt 0 ] && extra=" [$warnings warnings: $OUT/$t.build]"
	echo "$verdict $t: $last$extra"
	case "$verdict" in FAIL*) status=1; grep -v '^$' "$OUT/$t.out" | tail -6 | sed 's/^/    /' ;; esac
done
exit $status
