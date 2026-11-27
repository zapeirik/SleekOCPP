#!/bin/sh
# Builds a self-contained SleekOCPP.app (Qt libraries and plugins copied inside) and a .dmg.
# Usage: packaging/macos.sh [--install]   (run from anywhere; needs Qt from Homebrew or qt.io on PATH)
#   --install  also copies the app to /Applications (replacing an older copy)
set -e

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$ROOT/build-macos"

qt-cmake -S "$ROOT" -B "$BUILD" -DCMAKE_BUILD_TYPE=Release \
    -DSLEEKOCPP_MACOS_BUNDLE=ON -DSLEEKOCPP_BUILD_TESTS=OFF
cmake --build "$BUILD" -j8

APP="$BUILD/src/SleekOCPP.app"
macdeployqt "$APP" 2>&1 | grep -v "Cannot resolve rpath\|using QList" || true

# Plugins the app does not need. Homebrew's split Qt cannot resolve their dependencies,
# and they make the bundle larger.
rm -rf "$APP/Contents/PlugIns/platforminputcontexts" \
       "$APP/Contents/PlugIns/iconengines" \
       "$APP/Contents/PlugIns/imageformats/libqpdf.dylib"

# Removing files invalidates macdeployqt's signature, and macOS then refuses to start the app.
# Ad-hoc signing is enough to run on this Mac; distributing to others needs a Developer ID.
codesign --force --deep --sign - "$APP"

rm -f "$BUILD/src/SleekOCPP.dmg"
hdiutil create -volname SleekOCPP -srcfolder "$APP" -ov -format UDZO "$BUILD/src/SleekOCPP.dmg" >/dev/null

if [ "$1" = "--install" ]; then
    rm -rf /Applications/SleekOCPP.app
    cp -R "$APP" /Applications/SleekOCPP.app
    echo "Installed /Applications/SleekOCPP.app"
fi

echo
echo "Done: $APP"
echo "      $BUILD/src/SleekOCPP.dmg"
