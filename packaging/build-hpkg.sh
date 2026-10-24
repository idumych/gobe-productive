#!/bin/sh
# build-hpkg.sh — builds <cmd>-<version>-<revision>-<arch>.hpkg on Haiku.
# Generic beos-app-platforms template: everything app-specific comes from haiku-app.env.
#
#   sh packaging/build-hpkg.sh        (from the project root, on Haiku R1/beta6)
#
# Build it on Haiku R1/beta6: the package requires haiku >= r1~beta6, and a
# binary built against newer system libraries may not load on older ones.
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
. ./haiku-app.env

ARCH=$(getarch)
OUT="$ROOT/${APP_CMD}-${VERSION}-${REVISION}-${ARCH}.hpkg"

for tool in rc xres mimeset package getarch; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "build-hpkg: '$tool' not found (pkgman install haiku_devel)" >&2; exit 1; }
done

# 1. Build. Strip at link time: xres appends the resources after the ELF
#    data, so stripping afterwards would cut them off.
make clean >/dev/null
make LDFLAGS="${LDFLAGS:-} -s"
[ -z "${TEST_CMD:-}" ] || sh -c "$TEST_CMD"

# 2. Package tree, relative to the install location (/boot/system)
PKG="$ROOT/obj/hpkg"
rm -rf "$PKG"
mkdir -p "$PKG/apps" "$PKG/bin" "$PKG/data/deskbar/menu/Applications" \
	"$PKG/documentation/packages/$APP_CMD"
cp "$APP_NAME" "$PKG/apps/$APP_NAME"
mimeset -f "$PKG/apps/$APP_NAME"
ln -s "../apps/$APP_NAME" "$PKG/bin/$APP_CMD"
ln -s "../../../../apps/$APP_NAME" "$PKG/data/deskbar/menu/Applications/$APP_NAME"
cp README.md LICENSE "$PKG/documentation/packages/$APP_CMD/" 2>/dev/null || :

# 3. .PackageInfo
DESC=$(sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e '2,$s/^/\t/' "$DESCRIPTION_FILE")
REQ=""
for r in ${HPKG_REQUIRES_EXTRA:-}; do REQ="$REQ
	$r"; done
cat > "$PKG/.PackageInfo" <<EOF
name			$APP_CMD
version			$VERSION-$REVISION
architecture	$ARCH
summary			"$SUMMARY"
description		"$DESC"
packager		"$PACKAGER"
vendor			"$APP_NAME"
copyrights		{
	"$COPYRIGHT"
}
licenses		{
	"$LICENSE_NAME"
}
urls			{
	"$HOMEPAGE"
}
provides		{
	$APP_CMD = $VERSION
	app:$APP_NAME = $VERSION
	cmd:$APP_CMD
}
requires		{
	haiku >= r1~beta6$REQ
}
EOF

# 4. Package. The licence must be one Haiku knows or one shipped in the
#    package; `package create` says which when it isn't.
rm -f "$OUT"
package create -C "$PKG" "$OUT"
package list -i "$OUT" >/dev/null
echo "Built $OUT"
