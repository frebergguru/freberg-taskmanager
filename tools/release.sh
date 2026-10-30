#!/bin/bash
# Builds the release packages for this machine's architecture into dist/.
# Stops if anything that ships is unstripped.
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

cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr -DFG_BUILD_TESTS=OFF
cmake --build "$BUILD" -j"$JOBS"

rm -rf "$WORK" "$DIST"
mkdir -p "$WORK" "$DIST"

# The installed tree, which the .tar.gz and .deb both use.
STAGE=$WORK/stage
DESTDIR="$STAGE" cmake --install "$BUILD" --strip

tar -C "$STAGE/usr" --owner=0 --group=0 -czf "$DIST/$NAME.tar.gz" --transform "s,^\.,$NAME," .

# A .deb is an ar archive of three members, so it needs no Debian tools.
DEB=$WORK/deb
mkdir -p "$DEB/control"
cat >"$DEB/control/control" <<CONTROL
Package: freberg-taskmanager
Version: $VERSION
Architecture: $DEB_ARCH
Maintainer: Freberg.guru <noreply@freberg.guru>
Depends: libqt6widgets6 (>= 6.4), libqt6sql6 (>= 6.4), libqt6sql6-sqlite (>= 6.4), libqt6dbus6 (>= 6.4)
Recommends: policykit-1, smartmontools
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

# The AppImage carries its own Qt, for systems whose Qt is too old.
APPDIR=$WORK/AppDir
mkdir -p "$APPDIR/usr"
cp -a "$STAGE/usr/." "$APPDIR/usr/"
LIBS=$APPDIR/usr/lib
PLUGINS=$APPDIR/usr/plugins
mkdir -p "$LIBS" "$PLUGINS"
QT_PLUGINS=$(qtpaths6 --query QT_INSTALL_PLUGINS)
for group in platforms:libqoffscreen.so platforms:libqwayland.so platforms:libqxcb.so sqldrivers:libqsqlite.so imageformats:libqjpeg.so; do
    dir=${group%%:*}
    file=${group##*:}
    if [ -e "$QT_PLUGINS/$dir/$file" ]; then
        mkdir -p "$PLUGINS/$dir"
        cp -L "$QT_PLUGINS/$dir/$file" "$PLUGINS/$dir/"
    fi
done
[ -d "$QT_PLUGINS/wayland-shell-integration" ] && cp -rL "$QT_PLUGINS/wayland-shell-integration" "$PLUGINS/"
[ -d "$QT_PLUGINS/wayland-graphics-integration-client" ] && cp -rL "$QT_PLUGINS/wayland-graphics-integration-client" "$PLUGINS/"

# Every library those need, except the ones a system must supply itself.
SKIP='^(ld-linux|libc\.|libm\.|libdl\.|libpthread|librt\.|libresolv|libgcc_s|libstdc\+\+|libGL|libEGL|libGLX|libGLdispatch|libOpenGL|libdrm|libgbm|libX11|libxcb|libwayland|libdbus-1|libfontconfig|libfreetype|libz\.|libsystemd|libudev)'
copy_deps() {
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

cat >"$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
HERE=$(dirname "$(readlink -f "$0")")
export LD_LIBRARY_PATH="$HERE/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/usr/plugins"
exec "$HERE/usr/bin/freberg-taskmanager" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"
cp "$ROOT/packaging/guru.freberg.TaskManager.desktop" "$APPDIR/"
cp "$ROOT/packaging/guru.freberg.TaskManager.png" "$APPDIR/"
ln -sf guru.freberg.TaskManager.png "$APPDIR/.DirIcon"
ARCH="$ARCH" appimagetool --no-appstream "$APPDIR" "$DIST/$NAME.AppImage" >/dev/null

# Stops the release if anything that ships still carries symbols or debug info.
"$ROOT/tools/check-stripped.sh" "$STAGE" "$APPDIR" "$DIST/$NAME.tar.gz"

(cd "$DIST" && sha256sum ./* >SHA256SUMS)
ls -la "$DIST"
