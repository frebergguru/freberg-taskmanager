#!/bin/bash
# Writes the notices for every library an AppDir carries, and copies their licenses.
# GPL and LGPL libraries also need directions to their source, so those are written too.
# Stops if a bundled file can't be traced to a package with a license text.
#
# Usage: tools/third-party-notices.sh <AppDir> <Qt plugin folder>
set -euo pipefail

APPDIR=${1:?usage: third-party-notices.sh <AppDir> <Qt plugin folder>}
QT_PLUGINS=${2:?usage: third-party-notices.sh <AppDir> <Qt plugin folder>}
OUT=$APPDIR/usr/share/licenses/third-party
NOTICES=$OUT/NOTICES.txt
RUNTIME_URL=https://github.com/AppImage/type2-runtime
export LC_ALL=C

command -v pacman >/dev/null || { echo "Tracing bundled libraries needs pacman." >&2; exit 1; }
mkdir -p "$OUT"

# Which package each bundled file came from.
declare -A FILES_OF=()
while IFS= read -r file; do
    relative=${file#"$APPDIR/usr/plugins/"}
    if [ "$relative" != "$file" ]; then
        origin=$QT_PLUGINS/$relative
    else
        origin=/usr/lib/$(basename "$file")
    fi
    package=$(pacman -Qqo "$origin" 2>/dev/null) || { echo "No package owns $origin, so its license is unknown." >&2; exit 1; }
    FILES_OF[$package]+=" $(basename "$file")"
done < <(find "$APPDIR/usr/lib" "$APPDIR/usr/plugins" -type f | sort)

{
    cat <<'HEADER'
Third-party software in this AppImage
=====================================

Freberg.guru - Task manager is under the GNU General Public License, version 3 or
later. See usr/share/licenses/freberg-taskmanager/LICENSE. Its own source code is
released next to this AppImage as freberg-taskmanager-<version>-source.tar.gz.

This AppImage also carries the libraries below, as built by Arch Linux, with only their
symbols stripped. Each keeps its own license, copied into the folder with its package's
name beside this file.

Source code: the Upstream line is where a library's project publishes it, and the
Build recipe line holds the distribution's build files and patches for that exact
version.

To use your own build of a library, run this AppImage with --appimage-extract, replace
the file in squashfs-root/usr/lib, and start squashfs-root/AppRun.

HEADER
} >"$NOTICES"

for package in $(printf '%s\n' "${!FILES_OF[@]}" | sort); do
    info=$(pacman -Qi "$package")
    field() { sed -n "s/^$1 *: //p" <<<"$info"; }
    version=$(field Version)
    licenses=$(field Licenses)
    # Packages built together share one recipe, under the name of their base.
    base=$(awk '/^%BASE%$/ { getline; print; exit }' "/var/lib/pacman/local/$package-$version/desc")
    tag=${version//:/-}

    target=$OUT/$package
    mkdir -p "$target"
    if [ -d "/usr/share/licenses/$package" ]; then
        cp -L -r "/usr/share/licenses/$package/." "$target/"
    fi
    # Some packages only name a common license, so take its text from the shared set.
    for licence in $(tr -c 'A-Za-z0-9.+-' ' ' <<<"$licenses"); do
        [ -f "/usr/share/licenses/spdx/$licence.txt" ] && cp -L "/usr/share/licenses/spdx/$licence.txt" "$target/"
    done
    if [ -z "$(find "$target" -type f -print -quit)" ]; then
        echo "No license text found for $package ($licenses)." >&2
        exit 1
    fi

    cat >>"$NOTICES" <<ENTRY
$package $version
  License:      $licenses
  Files:       ${FILES_OF[$package]}
  Upstream:     $(field URL)
  Build recipe: https://gitlab.archlinux.org/archlinux/packaging/packages/$base/-/tree/$tag

ENTRY
done

cat >>"$NOTICES" <<ENTRY
AppImage runtime
  The small program at the start of this file that mounts it. It includes libfuse,
  squashfuse and zstd.
  License:      MIT, with LGPL-2.1 and BSD parts
  Upstream:     $RUNTIME_URL
ENTRY
