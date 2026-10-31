#!/bin/sh
# build-hpkg-arm64.sh — builds <cmd>-<version>-<revision>-arm64.hpkg on Linux, with Haiku's
# cross toolchain and a sysroot of Haiku's arm64 packages. Generic beos-app-platforms
# template: everything app-specific comes from haiku-app.env.
#
#   sh tools/haiku-arm64/make-sysroot.sh ~/.cache/haiku-arm64     (once, ~20 min)
#   . ~/.cache/haiku-arm64/env.sh && sh packaging/build-hpkg-arm64.sh
#
# Only for apps that need nothing beyond Haiku's own libraries (libbe, libtracker,
# libtranslation, libnetwork...): HaikuPorts has no arm64 repository yet.
# Same package as build-hpkg.sh, except: Haiku's host `package` tool builds it, and the
# app gets no mimeset (Haiku's host mimeset can't identify an arm64 binary and would set
# BEOS:TYPE to application/octet-stream). Haiku reads the signature from the resources.
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
. ./haiku-app.env

ARCH=arm64
OUT="$ROOT/${APP_CMD}-${VERSION}-${REVISION}-${ARCH}.hpkg"

[ -n "${HAIKU_CROSS_CFLAGS:-}" ] || {
	echo "build-hpkg-arm64: source the env.sh from tools/haiku-arm64/make-sysroot.sh first" >&2; exit 1; }
for tool in aarch64-unknown-haiku-g++ rc xres package; do
	command -v "$tool" >/dev/null 2>&1 || { echo "build-hpkg-arm64: '$tool' not found" >&2; exit 1; }
done
# (Gobe Productive) HPKG_REQUIRES_ARM64: what the arm64 package requires beyond haiku, from
# what the arm64 system itself has (the nightly's bootstrap zlib provides lib:libz)
if [ -n "${HPKG_REQUIRES_EXTRA:-}" ] && [ -z "${HPKG_REQUIRES_ARM64:-}" ]; then
	echo "build-hpkg-arm64: HPKG_REQUIRES_EXTRA is set ($HPKG_REQUIRES_EXTRA); HaikuPorts has no arm64 packages" >&2
	exit 1
fi

# 1. Build, stripped at link time (xres appends the resources after the ELF data)
make clean >/dev/null
make HAIKU_CROSS=1 LDFLAGS="${LDFLAGS:-} -s"
case "$(od -An -tx1 -j18 -N1 "$APP_NAME" | tr -d ' ')" in
	b7) ;;
	*) echo "build-hpkg-arm64: $APP_NAME is not an aarch64 binary" >&2; exit 1 ;;
esac

# 2. Package tree, relative to the install location (/boot/system)
PKG="$ROOT/obj/hpkg-$ARCH"
rm -rf "$PKG"
mkdir -p "$PKG/apps" "$PKG/bin" "$PKG/data/deskbar/menu/Applications" \
	"$PKG/documentation/packages/$APP_CMD"
cp "$APP_NAME" "$PKG/apps/$APP_NAME"
ln -s "../apps/$APP_NAME" "$PKG/bin/$APP_CMD"
ln -s "../../../../apps/$APP_NAME" "$PKG/data/deskbar/menu/Applications/$APP_NAME"
cp README.md LICENSE "$PKG/documentation/packages/$APP_CMD/" 2>/dev/null || :

# 3. .PackageInfo: haiku >= r1~beta6 is satisfied by the arm64 nightlies
#    (r1~beta6_hrevNNNNN) and keeps the file the same as the x86_64 one
DESC=$(sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e '2,$s/^/\t/' "$DESCRIPTION_FILE")
cat > "$PKG/.PackageInfo" <<INFO
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
	haiku >= r1~beta6
$(for r in ${HPKG_REQUIRES_ARM64:-}; do printf '\t%s\n' "$r"; done)
}
INFO

# 4. Package
rm -f "$OUT"
package create -C "$PKG" "$OUT"
package list -i "$OUT" >/dev/null
echo "Built $OUT"
