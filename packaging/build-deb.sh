#!/bin/sh
# build-deb.sh — builds <cmd>_<version>-<revision>_<arch>.deb for VitruvianOS.
# Generic beos-app-platforms template: everything app-specific comes from haiku-app.env.
#
#   . <sdk>/env.sh && sh packaging/build-deb.sh     (from the project root)
#
# Needs VitruvianOS headers and libraries (pkg-config vos, /system/develop/
# headers, or a link-time SDK's env.sh), rc, xres, resattr, tar with xattr
# support and dpkg-deb.
set -eu
cd "$(dirname "$0")/.."
ROOT=$(pwd)
. ./haiku-app.env

ARCH=$(dpkg --print-architecture 2>/dev/null || echo amd64)
# Built against VitruvianOS 0.6.x. The Haiku API is C++ and V\OS is pre-1.0,
# so class layouts may change in 0.7: require a rebuild then.
VOS_DEPENDS="vos (>= 0.6.2), vos (<< 0.7~)"
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct 2>/dev/null || date +%s)}"

BUILD="$ROOT/obj/deb"
PKG="$BUILD/${APP_CMD}_${VERSION}-${REVISION}_${ARCH}"
OUT="$ROOT/${APP_CMD}_${VERSION}-${REVISION}_${ARCH}.deb"
RSRC="$ROOT/obj/$APP_NAME.rsrc"     # the Makefile's compiled .rdef

for tool in rc xres resattr dpkg-deb tar; do
	command -v "$tool" >/dev/null 2>&1 || {
		echo "build-deb: '$tool' not found in PATH (source the SDK's env.sh?)" >&2; exit 1; }
done

# 1. Build. Strip at link time: xres appends the resources after the ELF
#    data, so stripping afterwards would cut them off.
make clean >/dev/null
make CXXFLAGS="${CXXFLAGS:--O2 -g -fstack-protector-strong -D_FORTIFY_SOURCE=2}" \
	LDFLAGS="${LDFLAGS:-} -s -Wl,--as-needed -Wl,-z,relro -Wl,-z,now"
[ -s "$RSRC" ] || { echo "build-deb: $RSRC missing; the Makefile must compile the .rdef there" >&2; exit 1; }

# 2. Package tree
rm -rf "$BUILD"
mkdir -p "$PKG/DEBIAN" "$PKG/system/apps" "$PKG/system/data/deskbar/menu/Applications" \
	"$PKG/usr/bin" "$PKG/usr/share/$APP_CMD" "$PKG/usr/share/doc/$APP_CMD"
install -m 0755 "$APP_NAME" "$PKG/system/apps/$APP_NAME"
ln -s "/system/apps/$APP_NAME" "$PKG/system/data/deskbar/menu/Applications/$APP_NAME"
ln -s "/system/apps/$APP_NAME" "$PKG/usr/bin/$APP_CMD"

# 3. Attributes. dpkg drops xattrs, so they ship on an EMPTY placeholder in a
#    tarball, and postinst copies the attributes (not the file!) onto the
#    real binary.
STAGE="$BUILD/attrs"
mkdir -p "$STAGE/system/apps"
: > "$STAGE/system/apps/$APP_NAME"
resattr -O -o "$STAGE/system/apps/$APP_NAME" "$RSRC"
# BEOS:TYPE as mimeset would set it: 'MIMS' (little-endian) + NUL-terminated value
TYPE_HEX=534d494d$(printf '%s' application/x-vnd.Be-elfexecutable | od -A n -t x1 | tr -d ' \n')00
python3 -c 'import os,sys; os.setxattr(sys.argv[1], "user.beos.BEOS:TYPE", bytes.fromhex(sys.argv[2]))' \
	"$STAGE/system/apps/$APP_NAME" "$TYPE_HEX"
tar --xattrs --xattrs-include='user.*' --owner=0 --group=0 --numeric-owner \
	--mtime="@$SOURCE_DATE_EPOCH" -cf "$PKG/usr/share/$APP_CMD/apps_attrs.tar" \
	-C "$STAGE" "./system/apps/$APP_NAME"

# 4. Docs
cp LICENSE "$PKG/usr/share/doc/$APP_CMD/copyright"
gzip -9n -c packaging/debian/changelog > "$PKG/usr/share/doc/$APP_CMD/changelog.Debian.gz"
[ ! -f README.md ] || gzip -9n -c README.md > "$PKG/usr/share/doc/$APP_CMD/README.md.gz"

# 5. Control files
sed -e "s|@APP_NAME@|$APP_NAME|g" -e "s|@APP_CMD@|$APP_CMD|g" \
	packaging/debian/postinst.in > "$PKG/DEBIAN/postinst"
chmod 0755 "$PKG/DEBIAN/postinst"
SIZE=$(du -sk --apparent-size --exclude=DEBIAN "$PKG" | cut -f1)
# Debian long description: one leading space per line, " ." for blank lines
LONGDESC=$(sed -e 's/^$/./' -e 's/^/ /' "$DESCRIPTION_FILE")
cat > "$PKG/DEBIAN/control" <<EOF
Package: $APP_CMD
Version: ${VERSION}-${REVISION}
Architecture: ${ARCH}
Maintainer: ${PACKAGER}
Installed-Size: ${SIZE}
Depends: ${VOS_DEPENDS}, libc6 (>= 2.38), libstdc++6 (>= 11), libgcc-s1 (>= 3.0), attr${DEB_DEPENDS_EXTRA:+, $DEB_DEPENDS_EXTRA}
Section: ${DEB_SECTION}
Priority: optional
Homepage: ${HOMEPAGE}
Description: ${SUMMARY}
${LONGDESC}
EOF
( cd "$PKG" && find . -type f ! -path './DEBIAN/*' -printf '%P\0' \
	| sort -z | xargs -0 md5sum ) > "$PKG/DEBIAN/md5sums"
find "$PKG" -exec touch -h -d "@$SOURCE_DATE_EPOCH" {} +
chmod -R u+rwX,go+rX,go-w "$PKG"

dpkg-deb --root-owner-group -Zxz --build "$PKG" "$OUT"
echo "Built $OUT"
