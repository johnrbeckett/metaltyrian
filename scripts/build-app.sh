#!/bin/bash
# Build MetalTyrian.app: a universal (Apple Silicon and Intel) release build,
# assembled into an app bundle with the Tyrian 2000 data inside, ad hoc signed
# for running on this Mac. scripts/release.sh makes the signed, notarised build.
#
# Usage: scripts/build-app.sh
# Output: ~/Library/Caches/MetalTyrian-build/MetalTyrian.app
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SCRATCH="$HOME/Library/Caches/MetalTyrian-build"
APP="$SCRATCH/MetalTyrian.app"
VERSION="$(tr -d '[:space:]' < "$ROOT/VERSION")"
BUILD="$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo 1)"

echo "MetalTyrian $VERSION (build $BUILD)"

ARCHS=(--arch arm64 --arch x86_64)
xcrun swift build -c release "${ARCHS[@]}" --package-path "$ROOT" --scratch-path "$SCRATCH/spm"
BIN="$(xcrun swift build -c release "${ARCHS[@]}" --package-path "$ROOT" --scratch-path "$SCRATCH/spm" --show-bin-path)"

rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources/GameData"
cp "$BIN/MetalTyrian" "$APP/Contents/MacOS/MetalTyrian"
cp "$ROOT/App/AppIcon.icns" "$APP/Contents/Resources/AppIcon.icns"
# Orbitron for crisp text (SIL Open Font License, see Fonts/OFL.txt).
mkdir -p "$APP/Contents/Resources/Fonts"
cp "$ROOT/App/Fonts/"* "$APP/Contents/Resources/Fonts/"
# John's high-resolution title screen art.
mkdir -p "$APP/Contents/Resources/Title"
cp "$ROOT/App/Title/"*.png "$APP/Contents/Resources/Title/"
mkdir -p "$APP/Contents/Resources/Pictures"
cp "$ROOT/App/Pictures/"*.png "$APP/Contents/Resources/Pictures/"
sed -e "s/__VERSION__/$VERSION/" -e "s/__BUILD__/$BUILD/" "$ROOT/App/Info.plist" > "$APP/Contents/Info.plist"
# The About box's credits, made from the README's Credits and Licence
# sections so they are kept in one place.
"$ROOT/scripts/make-credits.sh" "$ROOT/README.md" > "$APP/Contents/Resources/Credits.rtf"
mkdir -p "$APP/Contents/Resources/Licenses"
cp "$ROOT/App/Licenses/"* "$APP/Contents/Resources/Licenses/"

# Game data: the Tyrian 2000 data files only, not the DOS programs.
find "$ROOT/GameData/tyrian2000" -maxdepth 1 -type f \
    ! -iname '*.exe' ! -iname '*.pif' ! -iname '*.ovl' ! -iname '*.ico' \
    -exec cp {} "$APP/Contents/Resources/GameData/" \;

# Ad hoc signed for this Mac, unless scripts/release.sh asks for the
# Developer ID (hardened runtime and secure timestamp, as notarisation needs).
if [ -n "${MT_SIGN_IDENTITY:-}" ]; then
    codesign --force --options runtime --timestamp --sign "$MT_SIGN_IDENTITY" "$APP"
else
    codesign --force --sign - "$APP"
fi

echo "Architectures: $(lipo -archs "$APP/Contents/MacOS/MetalTyrian")"
echo "Data files: $(ls "$APP/Contents/Resources/GameData" | wc -l | tr -d ' ')"
echo "Built $APP"
