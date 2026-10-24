#!/bin/sh
# build-appimage.sh — the app for any x86_64 or aarch64 Linux desktop, as an AppImage,
# using Hokku (https://github.com/idumych/hokku, a fork of Cosmoe), the Haiku API as a
# library on X11 and Wayland. Generic beos-app-platforms template: app-specific values
# come from haiku-app.env.
#
#   sh packaging/appimage/build-appimage.sh
#
# Run it in CI (.github/workflows/appimage.yml, Ubuntu 24.04), not on a
# rolling-release host: the AppImage needs the oldest glibc it should run on.
# It builds for the machine it runs on (uname -m): CI runs it on ubuntu-24.04 and
# ubuntu-24.04-arm.
# Builds Hokku into .cosmoe/ (unless COSMOE_STAGE points
# at a DESTDIR install of it made with --prefix=/usr --libdir=lib), builds
# the app against it, bundles both with linuxdeploy, packs with appimagetool.
set -eu
cd "$(dirname "$0")/../.."
ROOT=$(pwd)
. ./haiku-app.env

# Which Cosmoe to build: Hokku, the fork with fixes for freestanding Haiku/Renku app ports
# (pointer events, tooltips, font styles). haiku-app.env may override COSMOE_REPO (owner/name
# on GitHub) and COSMOE_TAG (a tag or branch), e.g. to try upstream haydentech/cosmoe.
COSMOE_REPO=${COSMOE_REPO:-idumych/hokku}
COSMOE_TAG=${COSMOE_TAG:-hokku}
LINUXDEPLOY_TAG=1-alpha-20251107-1
APPIMAGETOOL_TAG=1.9.1
case "$(uname -m)" in
	x86_64) ARCH=x86_64 ;;
	aarch64|arm64) ARCH=aarch64 ;;
	*) echo "build-appimage: unsupported architecture $(uname -m)" >&2; exit 1 ;;
esac

WORK="$ROOT/.cosmoe"
OUT="$ROOT/${APP_NAME}-${VERSION}-${ARCH}.AppImage"
mkdir -p "$WORK/tools"

# 1. Cosmoe
if [ -z "${COSMOE_STAGE:-}" ]; then
	COSMOE_STAGE="$WORK/stage"
	if [ ! -f "$COSMOE_STAGE/usr/lib/libbe.so" ]; then
		[ -d "$WORK/src" ] || git clone --depth 1 --branch "$COSMOE_TAG" \
			"https://github.com/$COSMOE_REPO.git" "$WORK/src"
		meson setup "$WORK/build" "$WORK/src" --prefix=/usr --libdir=lib \
			--buildtype=release -Denable_unit_test_compile=false
		ninja -C "$WORK/build"
		DESTDIR="$COSMOE_STAGE" meson install -C "$WORK/build" --no-rebuild
	fi
fi
CROOT="$COSMOE_STAGE/usr"

# 2. The app. Strip at link time; Cosmoe resources live in an allocated
#    .cosmoe_rsrc section, which stripping keeps.
make clean >/dev/null
make COSMOE_ROOT="$CROOT" LDFLAGS="${LDFLAGS:-} -s"
[ -z "${TEST_CMD:-}" ] || sh -c "$TEST_CMD"

# 3. AppDir
APPDIR="$ROOT/obj/AppDir"
DOC="$APPDIR/usr/share/doc/$APP_CMD"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/cosmoe" \
	"$APPDIR/usr/share/fonts/noto" "$APPDIR/etc/fonts" "$DOC"
install -m 0755 "$APP_NAME" "$APPDIR/usr/bin/$APP_NAME"
for lib in libbe.so libtracker.so libcosmoe-x11.so libcosmoe-wayland.so; do
	install -m 0755 "$CROOT/lib/$lib" "$APPDIR/usr/lib/$lib"
done
cp -a "$CROOT/lib/addons" "$APPDIR/usr/lib/"
for d in mime_db artwork locale; do
	cp -a "$CROOT/share/cosmoe/$d" "$APPDIR/usr/share/cosmoe/"
done
cp -a "$CROOT"/share/cosmoe/*.png "$APPDIR/usr/share/cosmoe/" 2>/dev/null || :
for f in NotoSans-Regular NotoSans-Bold NotoSans-Italic NotoSans-BoldItalic \
		NotoSansMono-Regular NotoSansMono-Bold; do
	find "$CROOT/share/fonts" -name "$f.ttf" -exec cp {} "$APPDIR/usr/share/fonts/noto/" \;
done
# Fonts the app asks for by name (haiku-app.env APPIMAGE_FONTS_EXTRA), from Cosmoe's fonts or
# the build host's (e.g. Ubuntu's fonts-noto-core via APPIMAGE_APT_EXTRA). Missing = error.
for f in ${APPIMAGE_FONTS_EXTRA:-}; do
	src=$(find "$CROOT/share/fonts" /usr/share/fonts -name "$f.ttf" 2>/dev/null | head -1)
	[ -n "$src" ] || { echo "build-appimage: font $f.ttf not found" >&2; exit 1; }
	cp "$src" "$APPDIR/usr/share/fonts/noto/"
done
cp packaging/appimage/fonts.conf "$APPDIR/etc/fonts/fonts.conf"
cp LICENSE "$DOC/LICENSE"
cp "$WORK/src/License.md" "$DOC/COSMOE-LICENSE.md" 2>/dev/null \
	|| curl -fsSL "https://raw.githubusercontent.com/$COSMOE_REPO/$COSMOE_TAG/License.md" \
		-o "$DOC/COSMOE-LICENSE.md"
find "$APPDIR/usr/lib" -name '*.so' -exec strip --strip-unneeded {} \;

# desktop entry, icon and AppRun from haiku-app.env
GEN="$ROOT/obj/appimage-gen"
rm -rf "$GEN" && mkdir -p "$GEN"
cat > "$GEN/$APP_CMD.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=$APP_NAME
Comment=$SUMMARY
Exec=$APP_NAME %F
Icon=$APP_CMD
Terminal=false
Categories=$DESKTOP_CATEGORIES
StartupWMClass=$APP_NAME
EOF
cp packaging/appimage/icon.png "$GEN/$APP_CMD.png"
sed "s|@APP_NAME@|$APP_NAME|g" packaging/appimage/AppRun.in > "$GEN/AppRun"
chmod +x "$GEN/AppRun"

# 4. Bundle the libraries Cosmoe and the app need (ICU, pango, cairo, image
#    codecs, the app's own -l libraries), leaving out the ones every desktop
#    has (glibc, X11, fontconfig, GL...). NO_STRIP: linuxdeploy's strip is
#    older than some distros' libraries.
fetch() {
	[ -x "$WORK/tools/$1" ] || { curl -fsSL "$2" -o "$WORK/tools/$1"; chmod +x "$WORK/tools/$1"; }
}
fetch linuxdeploy "https://github.com/linuxdeploy/linuxdeploy/releases/download/$LINUXDEPLOY_TAG/linuxdeploy-$ARCH.AppImage"
fetch appimagetool "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_TAG/appimagetool-$ARCH.AppImage"
export APPIMAGE_EXTRACT_AND_RUN=1	# no FUSE needed (CI containers)

# linuxdeploy appends $ORIGIN to a RUNPATH but keeps what is there, and
# Cosmoe bakes in /usr/lib, which would make the AppImage prefer the host's
# ICU, pango and cairo. Reset them first with linuxdeploy's own patchelf.
LDROOT="$WORK/tools/linuxdeploy-root"
if [ ! -x "$LDROOT/AppRun" ]; then
	( cd "$WORK/tools" && rm -rf squashfs-root && ./linuxdeploy --appimage-extract >/dev/null \
		&& mv squashfs-root linuxdeploy-root )
fi
PATCHELF=$(find "$LDROOT" -name patchelf -type f | head -1)
for so in "$APPDIR"/usr/lib/*.so; do "$PATCHELF" --set-rpath '$ORIGIN' "$so"; done
for so in $(find "$APPDIR/usr/lib/addons" -name '*.so'); do
	"$PATCHELF" --set-rpath '$ORIGIN/../..' "$so"
done
DEPS_ONLY=""
for so in $(find "$APPDIR/usr/lib/addons" -name '*.so') \
		"$APPDIR/usr/lib/libcosmoe-x11.so" "$APPDIR/usr/lib/libcosmoe-wayland.so"; do
	DEPS_ONLY="$DEPS_ONLY --deploy-deps-only=$so"
done
# shellcheck disable=SC2086
NO_STRIP=1 LD_LIBRARY_PATH="$APPDIR/usr/lib" "$LDROOT/AppRun" --appdir "$APPDIR" \
	--executable "$APPDIR/usr/bin/$APP_NAME" $DEPS_ONLY \
	--desktop-file "$GEN/$APP_CMD.desktop" \
	--icon-file "$GEN/$APP_CMD.png" \
	--custom-apprun "$GEN/AppRun"

# 5. Pack
rm -f "$OUT"
ARCH=$ARCH "$WORK/tools/appimagetool" --no-appstream "$APPDIR" "$OUT"
echo "Built $OUT"
