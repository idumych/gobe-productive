#!/bin/sh
# haiku-typecheck.sh — type-checks the app's Haiku-API sources against
# Haiku's real system headers on a Linux host (g++ -fsyntax-only). Catches
# wrong API names, signatures and types in seconds, before a VM build; it
# does not link or run anything. Files and include dirs come from
# TYPECHECK_SRCS / TYPECHECK_INCLUDES in haiku-app.env.
#
#   sh tools/haiku-typecheck.sh [CACHE_DIR]     (default: ./.haiku-headers)
# Prints "ok  <file>" or the compiler errors; exit 1 on any error.
set -eu
ROOT=$(cd "$(dirname "$0")/.." && pwd)
. "$ROOT/haiku-app.env"
CACHE=$(mkdir -p "${1:-$ROOT/.haiku-headers}" && cd "${1:-$ROOT/.haiku-headers}" && pwd)

if [ ! -d "$CACHE/haiku/headers/os" ]; then
	git clone -q --depth 1 --filter=blob:none --sparse \
		https://github.com/haiku/haiku.git "$CACHE/haiku"
	git -C "$CACHE/haiku" sparse-checkout set headers/os headers/posix \
		headers/config headers/build/config_headers headers/libs
fi
H=$CACHE/haiku/headers

# Haiku's headers expect Haiku's libc; these few types are all that's missing
# when they sit on top of glibc.
cat > "$CACHE/shim.h" <<'EOF'
#include <config/types.h>
#include <stddef.h>
typedef unsigned long addr_t;
EOF

INC="-I$H"
for d in "$H"/os "$H"/os/*/; do INC="$INC -I$d"; done
for d in ${TYPECHECK_INCLUDES:-}; do INC="$INC -I$ROOT/$d"; done

[ -n "${TYPECHECK_SRCS:-}" ] || { echo "haiku-typecheck: TYPECHECK_SRCS is empty" >&2; exit 1; }
status=0 n=0
for f in $TYPECHECK_SRCS; do
	n=$((n + 1))
	out=$(g++ -std=c++17 -Wall -Wno-cpp -Wno-multichar -fsyntax-only $INC \
		-include "$CACHE/shim.h" "$ROOT/$f" 2>&1 \
		| grep -E "^[^ ].*: (error|warning): " | grep -v "^$H/" | grep -v "redefined" || true)
	if [ -n "$out" ]; then echo "$out"; status=1; else echo "ok  $f"; fi
done
echo "typecheck: $n files, $( [ $status = 0 ] && echo PASS || echo FAIL )"
exit $status
