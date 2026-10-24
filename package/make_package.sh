#!/bin/sh
# Builds the .hpkg on Haiku. usage (in port/): package/make_package.sh [version]
# Writes build/gobe_productive_port-<version>-x86_64.hpkg
set -e
cd "$(dirname "$0")/.."
VERSION=${1:-0.1~preview-1}
make
STAGE=build/package
rm -rf "$STAGE"
mkdir -p "$STAGE/apps/GobeProductive" "$STAGE/data/deskbar/menu/Applications" \
	"$STAGE/data/licenses" "$STAGE/documentation/packages/gobe_productive_port"
cp build/GobeProductive "$STAGE/apps/GobeProductive/GobeProductive"
ln -s ../../../../apps/GobeProductive/GobeProductive \
	"$STAGE/data/deskbar/menu/Applications/Gobe Productive"
cp package/LICENSE "$STAGE/data/licenses/Gobe Productive port"
cp README.md "$STAGE/documentation/packages/gobe_productive_port/README.md"
sed -e "s/@VERSION@/$VERSION/g" \
	-e "s/@PACKAGER@/${PACKAGER:-Gobe Productive port}/" \
	-e "s/@VENDOR@/${VENDOR:-Gobe Productive port}/" \
	-e "s/@COPYRIGHT@/${COPYRIGHT:-the Gobe Productive port authors}/" \
	package/PackageInfo.in > "$STAGE/.PackageInfo"
OUT="build/gobe_productive_port-$VERSION-x86_64.hpkg"
rm -f "$OUT"
package create -C "$STAGE" "$OUT"
package list "$OUT" | head -20
echo "wrote $OUT"
