#!/bin/sh
# make-sysroot.sh — builds what an app needs to be compiled on Linux for Haiku arm64:
# Haiku's cross toolchain (gcc 13 for aarch64-unknown-haiku), Haiku's host tools (package,
# rc, xres, bfs_shell) and a sysroot made from the official arm64 packages.
#
#   sh tools/haiku-arm64/make-sysroot.sh [WORK_DIR]        (default ./.haiku-arm64)
#   . WORK_DIR/env.sh && make HAIKU_CROSS=1 && sh packaging/build-hpkg-arm64.sh
#
# Runs on an x86_64 Linux host with a C/C++ toolchain, git, curl, unzip, python3, flex,
# bison, texinfo (makeinfo), zlib and zstd headers. Takes about 20 minutes; the result is
# about 3 GB and can be cached (CI) or kept between releases.
#
# Where the pieces come from:
#  - Haiku's arm64 package repository (master) has haiku and haiku_devel: libroot, libbe,
#    libtracker, libtranslation, the CRT objects and the headers. Its hrev picks the Haiku
#    source revision for the toolchain, so both match.
#  - libbe needs libstdc++, libgcc_s, ICU and zlib. On arm64 they only exist as the
#    bootstrap packages inside the nightly image of the same hrev; bfs_shell copies them out.
#  - HaikuPorts has no arm64 repository, so an app that needs HaikuPorts libraries (sqlite,
#    openssl, ffmpeg, ...) can't be built this way yet.
# None of this is sent anywhere: only public downloads.
set -eu

WORK=$(mkdir -p "${1:-.haiku-arm64}" && cd "${1:-.haiku-arm64}" && pwd)
JOBS=$(nproc 2>/dev/null || echo 4)
REPO=https://eu.hpkg.haiku-os.org/haiku/master/arm64
NIGHTLY=https://haiku-nightly.cdn.haiku-os.org/arm64
say() { printf '\n== %s\n' "$*"; }

# 1. Which hrev the arm64 repository is at ("current" redirects to a versioned path)
say "Haiku arm64 repository"
ROUTE=$(curl -fsSL "$REPO/current/")
HREV=$(echo "$ROUTE" | grep -oE 'hrev[0-9]+' | head -1)
VERSION=$(echo "$ROUTE" | grep -oE 'r1~[a-z0-9]+_hrev[0-9]+' | head -1)
[ -n "$HREV" ] && [ -n "$VERSION" ] || { echo "make-sysroot: can't read $REPO/current/: $ROUTE" >&2; exit 1; }
echo "$VERSION"
if [ -f "$WORK/sysroot/.version" ] && [ "$(cat "$WORK/sysroot/.version")" = "$VERSION" ]; then
	echo "sysroot for $VERSION is already there"; exit 0
fi

# 2. Sources: Haiku at that hrev (Haiku's own git has the hrev tags), buildtools
say "Sources"
[ -d "$WORK/haiku/.git" ] || git clone -q --depth 1 --branch "$HREV" \
	https://review.haiku-os.org/haiku "$WORK/haiku"
[ -d "$WORK/buildtools/.git" ] || git clone -q --depth 1 \
	https://review.haiku-os.org/buildtools "$WORK/buildtools"

# 3. Cross toolchain. configure insists on an ELF-capable nasm even for arm64, where it is
#    never used; a stand-in answers its format query.
mkdir -p "$WORK/bin" "$WORK/generated"
cat > "$WORK/bin/nasm-stub" <<'EOF'
#!/bin/sh
# configure only asks nasm for its ELF formats; arm64 builds never run it
printf '  elf32 \n  elf64 \n'
EOF
chmod +x "$WORK/bin/nasm-stub"
CROSS="$WORK/generated/cross-tools-arm64/bin/aarch64-unknown-haiku-g++"
if [ ! -x "$CROSS" ]; then
	say "Cross toolchain (about 15 minutes)"
	( cd "$WORK/generated" && HAIKU_NASM="$WORK/bin/nasm-stub" ../haiku/configure \
		--cross-tools-source ../buildtools --build-cross-tools arm64 -j"$JOBS" )
fi

# 4. jam, then Haiku's host tools
say "Host tools"
JAM=$(ls "$WORK"/buildtools/jam/bin.*/jam 2>/dev/null | head -1)
if [ -z "$JAM" ]; then
	make -C "$WORK/buildtools/jam" -j"$JOBS" > /dev/null
	JAM=$(ls "$WORK"/buildtools/jam/bin.*/jam | head -1)
fi
( cd "$WORK/generated" && "$JAM" -q -j"$JOBS" '<build>package' '<build>rc' '<build>xres' \
	'<build>bfs_shell' > "$WORK/host-tools.log" 2>&1 ) \
	|| { tail -20 "$WORK/host-tools.log"; exit 1; }
T=$(find "$WORK/generated/objects" -type d -path '*/release/tools' | head -1)
# Only these go on PATH: Haiku's host mimeset doesn't recognise arm64 binaries (it would set
# BEOS:TYPE to application/octet-stream), so it is left out
for t in package/package rc/rc xres bfs_shell/bfs_shell; do
	ln -sf "$T/$t" "$WORK/bin/$(basename "$t")"
done

# 5. Packages: haiku and haiku_devel from the repository, the bootstrap libraries from the
#    nightly image of the same hrev
say "Packages"
P="$WORK/packages"
mkdir -p "$P"
for pkg in haiku haiku_devel; do
	f="$pkg-$VERSION-1-arm64.hpkg"
	[ -s "$P/$f" ] || curl -fsSL -o "$P/$f" "$REPO/$VERSION/packages/$f"
done
IMG="haiku-master-$HREV-arm64-mmc"
if [ ! -s "$P/$IMG.image" ]; then
	curl -fsSL -o "$P/$IMG.zip" "$NIGHTLY/$IMG.zip" \
		|| { echo "make-sysroot: no nightly image $IMG.zip (the repository moved on?)" >&2; exit 1; }
	curl -fsSL -o "$P/$IMG.zip.sha256" "$NIGHTLY/$IMG.zip.sha256"
	( cd "$P" && sha256sum -c "$IMG.zip.sha256" && unzip -o -q "$IMG.zip" "$IMG.image" )
	rm -f "$P/$IMG.zip"
fi
# The BFS partition is the image's second partition
OFFSET=$(( $(python3 -c 'import struct,sys
d=open(sys.argv[1],"rb").read(512)
print(struct.unpack_from("<I", d, 446 + 16 + 8)[0])' "$P/$IMG.image") * 512 ))
LIST=$(printf 'ls /myfs/system/packages\nexit\n' | "$WORK/bin/bfs_shell" --start-offset "$OFFSET" \
	"$P/$IMG.image" 2>/dev/null | grep -oE '(gcc_syslibs|icu[0-9]*|zlib)-[^ ]*bootstrap[^ ]*\.hpkg' | sort -u)
[ -n "$LIST" ] || { echo "make-sysroot: no bootstrap packages found in $IMG.image" >&2; exit 1; }
CMDS=""
for f in $LIST; do CMDS="${CMDS}cp /myfs/system/packages/$f :$P/$f
"; done
printf '%sexit\n' "$CMDS" | "$WORK/bin/bfs_shell" --start-offset "$OFFSET" "$P/$IMG.image" > /dev/null 2>&1
echo "$LIST"

# 6. Sysroot: the packages extracted as they'd be under /boot/system
say "Sysroot"
rm -rf "$WORK/sysroot"
mkdir -p "$WORK/sysroot"
for f in "$P"/haiku-"$VERSION"-1-arm64.hpkg "$P"/haiku_devel-"$VERSION"-1-arm64.hpkg; do
	"$WORK/bin/package" extract -C "$WORK/sysroot" "$f"
done
for f in $LIST; do "$WORK/bin/package" extract -C "$WORK/sysroot" "$P/$f"; done
# (Gobe Productive) the bootstrap zlib has only the library: its headers (the same API, from
# buildtools' zlib) and a link-time libz.so, for -lz
cp "$WORK/buildtools/gcc/zlib/zlib.h" "$WORK/buildtools/gcc/zlib/zconf.h" \
	"$WORK/sysroot/develop/headers/"
ln -sf ../../lib/libz.so.1 "$WORK/sysroot/develop/lib/libz.so"
echo "$VERSION" > "$WORK/sysroot/.version"

# 7. env.sh. Kit headers go first (-I); the C library headers come after the compiler's own
#    C++ headers (-idirafter), as Haiku's native compiler orders them, or libstdc++'s
#    #include_next <stdlib.h> fails.
cat > "$WORK/env.sh" <<EOF
# source me: . $WORK/env.sh   — Haiku arm64 cross build ($VERSION)
HAIKU_ARM64="$WORK"
export PATH="\$HAIKU_ARM64/generated/cross-tools-arm64/bin:\$HAIKU_ARM64/bin:\$PATH"
export HAIKU_ARM64_SYSROOT="\$HAIKU_ARM64/sysroot"
export HAIKU_ARM64_VERSION="$VERSION"
_s=\$HAIKU_ARM64_SYSROOT/develop/headers
_inc=""
for _d in \$(find \$_s/os -mindepth 1 -type d) \$_s/os \$_s/be_apps/Tracker; do _inc="\$_inc -I\$_d"; done
for _d in \$_s/posix \$_s/gnu \$_s/bsd \$_s; do _inc="\$_inc -idirafter \$_d"; done
export CC=aarch64-unknown-haiku-gcc CXX=aarch64-unknown-haiku-g++
export HAIKU_CROSS_CFLAGS="--sysroot=\$HAIKU_ARM64_SYSROOT\$_inc"
export HAIKU_CROSS_LDFLAGS="--sysroot=\$HAIKU_ARM64_SYSROOT -B\$HAIKU_ARM64_SYSROOT/develop/lib -L\$HAIKU_ARM64_SYSROOT/develop/lib -L\$HAIKU_ARM64_SYSROOT/lib -Wl,-rpath-link=\$HAIKU_ARM64_SYSROOT/lib"
unset _s _inc _d
EOF
say "done: $VERSION"
echo "Use it with:   . $WORK/env.sh && make HAIKU_CROSS=1 && sh packaging/build-hpkg-arm64.sh"
