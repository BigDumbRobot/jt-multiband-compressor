#!/bin/bash
# Builds the macOS installer: dist/JT-Multiband-Compressor-<version>.pkg
# Universal (Apple silicon + Intel) VST3 and AU, installed for all users into
#   /Library/Audio/Plug-Ins/VST3 and /Library/Audio/Plug-Ins/Components
# (the installer asks for an administrator password).
#
# Optional signing (needs an Apple Developer account):
#   DEV_ID_APP="Developer ID Application: Name (TEAMID)"        signs the plugins
#   DEV_ID_INSTALLER="Developer ID Installer: Name (TEAMID)"    signs the .pkg
#   NOTARY_PROFILE=<keychain profile from `xcrun notarytool store-credentials`>  notarizes and staples
# Without them the plugins are ad-hoc signed and the .pkg is unsigned (macOS warns on first open:
# right-click > Open, or System Settings > Privacy & Security > Open Anyway).
#
# Extra CMake arguments can be passed through CMAKE_ARGS (e.g. -DFETCHCONTENT_SOURCE_DIR_JUCE=...).
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
VERSION=$(sed -nE 's/^project\(JTMultibandCompressor VERSION ([0-9.]+).*/\1/p' CMakeLists.txt)
NAME="JT Multiband Compressor"
ID=com.bigdumbrobot.jtmultibandcompressor
BUILD="$ROOT/build-release"
WORK="$BUILD/pkg"
OUT="$ROOT/dist/JT-Multiband-Compressor-$VERSION.pkg"

echo "== Building $NAME $VERSION (universal)"
cmake -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBDR_AAX=OFF \
      -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" ${CMAKE_ARGS:-} >/dev/null
cmake --build "$BUILD" --target JTMultibandCompressor_VST3 JTMultibandCompressor_AU

ART="$BUILD/JTMultibandCompressor_artefacts/Release"
rm -rf "$WORK"; mkdir -p "$WORK/vst3" "$WORK/au" "$ROOT/dist"
ditto --norsrc --noextattr --noacl "$ART/VST3/$NAME.vst3" "$WORK/vst3/$NAME.vst3"
ditto --norsrc --noextattr --noacl "$ART/AU/$NAME.component" "$WORK/au/$NAME.component"

echo "== Signing plugins"
for b in "$WORK/vst3/$NAME.vst3" "$WORK/au/$NAME.component"; do
    if [ -n "${DEV_ID_APP:-}" ]; then
        codesign --force --timestamp --options runtime --sign "$DEV_ID_APP" "$b"
    else
        codesign --force --sign - "$b"
    fi
    xattr -cr "$b"   # no extended attributes in the payload (they'd install as ._ files)
    codesign --verify --strict "$b"
done
export COPYFILE_DISABLE=1

echo "== Packaging"
component () { # <root> <identifier> <location> [scripts]
    local plist="$WORK/$(basename "$1").plist"
    pkgbuild --analyze --root "$1" "$plist" >/dev/null
    plutil -replace 0.BundleIsRelocatable -bool NO "$plist" # always install to the system folder
    pkgbuild --root "$1" --component-plist "$plist" --identifier "$2" --version "$VERSION" \
             --install-location "$3" ${4:+--scripts "$4"} "$WORK/$(basename "$1").pkg" >/dev/null
}
component "$WORK/vst3" "$ID.vst3" "/Library/Audio/Plug-Ins/VST3"
component "$WORK/au" "$ID.au" "/Library/Audio/Plug-Ins/Components" "$ROOT/packaging/scripts/au"

mkdir -p "$WORK/resources"
cp packaging/resources/welcome.html "$WORK/resources/"
cp LICENSE "$WORK/resources/license.txt"
sed "s/@VERSION@/$VERSION/g" packaging/distribution.xml > "$WORK/distribution.xml"
productbuild --distribution "$WORK/distribution.xml" --resources "$WORK/resources" --package-path "$WORK" \
             ${DEV_ID_INSTALLER:+--sign "$DEV_ID_INSTALLER"} "$OUT"

if [ -n "${NOTARY_PROFILE:-}" ]; then
    echo "== Notarizing"
    xcrun notarytool submit "$OUT" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$OUT"
fi
echo "== Done: $OUT"
