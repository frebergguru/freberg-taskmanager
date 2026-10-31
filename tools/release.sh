#!/bin/bash
# Builds the release packages for this machine's architecture into dist/.
# Stops if anything that ships is unstripped, or if the GPL's source and notices are missing.
#
# Usage: tools/release.sh [jobs]
set -euo pipefail

JOBS=${1:-2}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
VERSION=$(sed -n 's/^project(freberg-taskmanager VERSION \([0-9.]*\).*/\1/p' "$ROOT/CMakeLists.txt")
ARCH=$(uname -m)
case "$ARCH" in
    x86_64) DEB_ARCH=amd64 ;;
    aarch64) DEB_ARCH=arm64 ;;
    *) echo "No package names for $ARCH" >&2; exit 1 ;;
esac
BUILD=$ROOT/build-release
DIST=$ROOT/dist
WORK=$BUILD/package # never /tmp: it is small and shared
NAME=freberg-taskmanager-$VERSION-linux-$ARCH

# A version number names one set of source. A tag for it on other code means this needs a new number.
if TAGGED=$(git -C "$ROOT" rev-parse -q --verify "refs/tags/v$VERSION^{commit}") && [ "$TAGGED" != "$(git -C "$ROOT" rev-parse HEAD)" ]; then
    echo "v$VERSION is already tagged on another commit. Raise the version in CMakeLists.txt first." >&2
    exit 1
fi

# The GPL needs the source of exactly what ships, so only committed code can be released.
if [ "$(git -C "$ROOT" status --porcelain)" ]; then
    echo "The source tree has uncommitted changes. Commit them first, so the source package matches." >&2
    exit 1
fi

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DFG_BUILD_TESTS=OFF
cmake --build "$BUILD" -j"$JOBS"

rm -rf "$WORK" "$DIST"
mkdir -p "$WORK" "$DIST"

# The complete source of this release, which goes next to the packages.
git -C "$ROOT" archive --format=tar.gz --prefix="freberg-taskmanager-$VERSION/" -o "$DIST/freberg-taskmanager-$VERSION-source.tar.gz" HEAD

# The installed tree, which the .tar.gz and .deb both use.
STAGE=$WORK/stage
DESTDIR="$STAGE" cmake --install "$BUILD" --strip

tar -C "$STAGE/usr" --owner=0 --group=0 -czf "$DIST/$NAME.tar.gz" --transform "s,^\.,$NAME," .

# The newest symbol version a binary needs from a library, such as 2.38 for GLIBC.
needs_version() {
    objdump -T "$STAGE/usr/bin/freberg-taskmanager" | grep -o "$1_[0-9.]*" | sed "s/$1_//" | sort -V | tail -n 1
}
GLIBC_NEEDED=$(needs_version GLIBC)
GLIBCXX_NEEDED=$(needs_version GLIBCXX)
if [ -z "$GLIBC_NEEDED" ] || [ -z "$GLIBCXX_NEEDED" ]; then
    echo "Could not read which glibc and libstdc++ versions the binary needs." >&2
    exit 1
fi
# Qt keeps old programs working on newer Qt, not the other way, so the build's own version is the floor.
QT_BUILT=$(qmake6 -query QT_VERSION)
echo "This build needs glibc $GLIBC_NEEDED or later, GLIBCXX $GLIBCXX_NEEDED or later and Qt $QT_BUILT or later."

# A .deb is an ar archive of three members, so it needs no Debian tools.
DEB=$WORK/deb
mkdir -p "$DEB/control"
cat >"$DEB/control/control" <<CONTROL
Package: freberg-taskmanager
Version: $VERSION
Architecture: $DEB_ARCH
Maintainer: Freberg.guru <37067463-Freberg.guru@users.noreply.gitlab.com>
Depends: libc6 (>= $GLIBC_NEEDED), libqt6widgets6 (>= $QT_BUILT), libqt6sql6 (>= $QT_BUILT), libqt6sql6-sqlite (>= $QT_BUILT), libqt6dbus6 (>= $QT_BUILT)
Recommends: qt6-wayland, pkexec | policykit-1, smartmontools
Section: utils
Priority: optional
Description: Small system monitor and task manager with a classic desktop look
 Shows processes, performance, services, connections, disk space and more,
 and records system activity for replay. Uses very little of the computer.
CONTROL
tar -C "$DEB/control" --owner=0 --group=0 -czf "$DEB/control.tar.gz" .
tar -C "$STAGE" --owner=0 --group=0 -czf "$DEB/data.tar.gz" .
printf '2.0\n' >"$DEB/debian-binary"
(cd "$DEB" && ar rcD "$DIST/freberg-taskmanager_${VERSION}_$DEB_ARCH.deb" debian-binary control.tar.gz data.tar.gz)

# The AppImage carries its own Qt, for systems whose Qt is too old. It still uses the system's
# C and C++ libraries, so the versions printed above are its floor too.
APPDIR=$WORK/AppDir
mkdir -p "$APPDIR/usr"
cp -a "$STAGE/usr/." "$APPDIR/usr/"
LIBS=$APPDIR/usr/lib
PLUGINS=$APPDIR/usr/plugins
mkdir -p "$LIBS" "$PLUGINS"
QT_PLUGINS=$(qmake6 -query QT_INSTALL_PLUGINS)
for group in platforms:libqoffscreen.so platforms:libqwayland.so platforms:libqxcb.so sqldrivers:libqsqlite.so imageformats:libqjpeg.so; do
    dir=${group%%:*}
    file=${group##*:}
    if [ -e "$QT_PLUGINS/$dir/$file" ]; then
        mkdir -p "$PLUGINS/$dir"
        cp -L "$QT_PLUGINS/$dir/$file" "$PLUGINS/$dir/"
    fi
done
# Without these the app can't open a window or keep its history, which must not go unnoticed.
for needed in platforms/libqxcb.so platforms/libqwayland.so sqldrivers/libqsqlite.so; do
    if [ ! -e "$PLUGINS/$needed" ]; then
        echo "The AppImage would lack the Qt plugin $needed." >&2
        exit 1
    fi
done
[ -d "$QT_PLUGINS/wayland-shell-integration" ] && cp -rL "$QT_PLUGINS/wayland-shell-integration" "$PLUGINS/"
[ -d "$QT_PLUGINS/wayland-graphics-integration-client" ] && cp -rL "$QT_PLUGINS/wayland-graphics-integration-client" "$PLUGINS/"

# Every library those need, except the ones a system must supply itself. The small helper
# libraries around libxcb are often not installed, and the X11 plugin needs them, so they are carried.
SKIP='^(ld-linux|libc\.|libm\.|libdl\.|libpthread|librt\.|libresolv|libgcc_s|libstdc\+\+|libGL|libEGL|libGLX|libGLdispatch|libOpenGL|libdrm|libgbm|libX11|libxcb(\.|-(dri|glx|present|randr|render\.|shape|shm|sync|xfixes|xkb|xinput|xinerama))|libwayland|libdbus-1|libfontconfig|libfreetype|libz\.|libsystemd|libudev)'
copy_deps() {
    if ldd "$1" 2>/dev/null | grep -q '=> not found'; then
        echo "A library that $1 needs is missing:" >&2
        ldd "$1" | grep '=> not found' >&2
        exit 1
    fi
    ldd "$1" 2>/dev/null | awk '/=> \//{print $3}' | while read -r lib; do
        base=$(basename "$lib")
        if ! [[ $base =~ $SKIP ]] && [ ! -e "$LIBS/$base" ]; then
            cp -L "$lib" "$LIBS/$base"
            copy_deps "$lib"
        fi
    done
}
copy_deps "$APPDIR/usr/bin/freberg-taskmanager"
while IFS= read -r plugin; do copy_deps "$plugin"; done < <(find "$PLUGINS" -name '*.so')
find "$APPDIR" -type f \( -name '*.so' -o -name '*.so.*' \) -exec strip --strip-unneeded {} +

"$ROOT/tools/third-party-notices.sh" "$APPDIR" "$QT_PLUGINS"

cat >"$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
# The app hands programs it starts the original value, so they don't load the libraries in here.
export FG_APPIMAGE=1
export FG_ORIGINAL_LD_LIBRARY_PATH="${LD_LIBRARY_PATH:-}"
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
exec "$HERE/usr/bin/freberg-taskmanager" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"
cp "$ROOT/packaging/guru.freberg.TaskManager.desktop" "$APPDIR/"
cp "$ROOT/packaging/guru.freberg.TaskManager.png" "$APPDIR/"
ln -sf guru.freberg.TaskManager.png "$APPDIR/.DirIcon"
ARCH="$ARCH" appimagetool --no-appstream "$APPDIR" "$DIST/$NAME.AppImage" >/dev/null

# Stops the release if anything that ships still carries symbols or debug info, and leaves nothing to publish.
if ! "$ROOT/tools/check-stripped.sh" "$STAGE" "$APPDIR" "$DIST/$NAME.tar.gz" "$DIST/freberg-taskmanager_${VERSION}_$DEB_ARCH.deb"; then
    rm -rf "$DIST"
    exit 1
fi

(cd "$DIST" && sha256sum ./* >SHA256SUMS)
ls -la "$DIST"
