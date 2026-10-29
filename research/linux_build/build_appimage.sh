#!/usr/bin/env bash
# Builds a single-file OVITO AppImage from an already compiled build directory.
#
#   bash research/linux_build/build_appimage.sh <build dir> <output dir>
#
# Run from the repository root. Needs patchelf. cmake --install lays out usr/{bin,lib/ovito,share/ovito} with
# RUNPATHs relative to $ORIGIN, so OVITO's own libraries keep finding each other; linuxdeploy then copies the system
# libraries to usr/lib and the Qt plugins to usr/plugins. The plugin .so files are loaded with dlopen, so they must
# be passed with --deploy-deps-only or their dependencies would not be bundled.
#
# The AppImage runs on distributions whose glibc is at least as new as the build machine's (Ubuntu 24.04: 2.39).
set -euo pipefail

BUILD_DIR=$(realpath "$1")
OUT_DIR=$(realpath -m "$2")
HERE=$(dirname "$(realpath "$0")")
REPO=$(realpath "$HERE/../..")
TOOLS="${LINUXDEPLOY_TOOLS:-$OUT_DIR/tools}"
APPDIR="$OUT_DIR/AppDir"

mkdir -p "$OUT_DIR" "$TOOLS"
rm -rf "$APPDIR"

for tool in linuxdeploy linuxdeploy-plugin-qt linuxdeploy-plugin-appimage; do
    if [ ! -x "$TOOLS/$tool-x86_64.AppImage" ]; then
        curl -fsSL -o "$TOOLS/$tool-x86_64.AppImage" \
            "https://github.com/linuxdeploy/$tool/releases/download/continuous/$tool-x86_64.AppImage"
        chmod +x "$TOOLS/$tool-x86_64.AppImage"
    fi
done
export PATH="$TOOLS:$PATH"

cmake --install "$BUILD_DIR" --prefix "$APPDIR/usr"
cp "$HERE/package/LEEME.txt" "$APPDIR/usr/share/ovito/"
echo "commit $(git -C "$REPO" rev-parse HEAD)" > "$APPDIR/usr/share/ovito/VERSION.txt"

# The runners have no FUSE; the system strip of Ubuntu 24.04 is newer than the one inside linuxdeploy, which
# fails on the .relr.dyn sections of the system libraries.
export APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1
export QMAKE="${QMAKE:-$(command -v qmake6 || echo /usr/lib/qt6/bin/qmake6)}"
# The offscreen platform serves headless runs (tests, --nogui scripts that render).
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"
export LDAI_OUTPUT="$OUT_DIR/OVITO-x86_64.AppImage"

# linuxdeploy replaces the RUNPATH of every ELF file it touches with one that points to usr/lib only, which would
# cut OVITO's plugins off from each other. Record the $ORIGIN-relative entries and put them back in front afterwards.
# Absolute entries (e.g. the HDF5 directory of the build machine) are dropped so that the bundled copy wins.
declare -A RUNPATHS
while IFS= read -r -d '' elf; do
    rp=$(patchelf --print-rpath "$elf" 2>/dev/null | tr ':' '\n' | grep '^\$ORIGIN' | paste -sd: -) || true
    if [ -n "$rp" ]; then RUNPATHS["$elf"]=$rp; fi
done < <(find "$APPDIR/usr/bin" "$APPDIR/usr/lib/ovito" -type f -print0)

# Some of OVITO's libraries (libovito_netcdf.so) link to Core.so without a RUNPATH to it; at run time it is already
# loaded, but linuxdeploy resolves every file on its own.
export LD_LIBRARY_PATH="$APPDIR/usr/lib/ovito:$APPDIR/usr/lib/ovito/plugins${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

# All of OVITO's theme icons are SVG files, and Qt's theme icon loader only considers SVG files when the SVG icon
# engine plugin is present. linuxdeploy-plugin-qt deploys the SVG image format plugin but not the icon engine, which
# left every icon-only button (command panel tabs, viewport maximize) blank.
QT_PLUGIN_DIR=$("$QMAKE" -query QT_INSTALL_PLUGINS)
mkdir -p "$APPDIR/usr/plugins/iconengines"
cp "$QT_PLUGIN_DIR/iconengines/libqsvgicon.so" "$APPDIR/usr/plugins/iconengines/"

cd "$OUT_DIR"
"$TOOLS/linuxdeploy-x86_64.AppImage" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/ovito" \
    --deploy-deps-only "$APPDIR/usr/lib/ovito" \
    --deploy-deps-only "$APPDIR/usr/lib/ovito/plugins" \
    --deploy-deps-only "$APPDIR/usr/plugins/iconengines" \
    --desktop-file "$HERE/package/ovito.desktop" \
    --icon-file "$REPO/doc/manual/images/team/ovito_logo_256.png" \
    --icon-filename ovito \
    --plugin qt

for elf in "${!RUNPATHS[@]}"; do
    current=$(patchelf --print-rpath "$elf")
    case ":$current:" in *":${RUNPATHS[$elf]}:"*) continue ;; esac
    patchelf --set-rpath "${RUNPATHS[$elf]}${current:+:$current}" "$elf"
done
echo "RUNPATH of usr/bin/ovito: $(patchelf --print-rpath "$APPDIR/usr/bin/ovito")"
echo "RUNPATH of Core.so:       $(patchelf --print-rpath "$APPDIR/usr/lib/ovito/plugins/Core.so")"

for plugin in iconengines/libqsvgicon.so imageformats/libqsvg.so platforms/libqxcb.so; do
    [ -f "$APPDIR/usr/plugins/$plugin" ] || { echo "ERROR: Qt plugin $plugin is missing from the AppDir"; exit 1; }
done

# Packaging step only: unlike linuxdeploy --output appimage, it does not run the ELF deployment again.
"$TOOLS/linuxdeploy-plugin-appimage-x86_64.AppImage" --appdir "$APPDIR"
ls -la "$LDAI_OUTPUT"
