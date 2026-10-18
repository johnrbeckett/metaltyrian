#!/bin/bash
# Build MetalTyrian.app: a universal (Apple Silicon and Intel) release build,
# assembled into an app bundle with the Tyrian 2000 data inside, ad hoc signed
# for running on this Mac. Developer ID signing and notarisation come later.
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
sed -e "s/__VERSION__/$VERSION/" -e "s/__BUILD__/$BUILD/" "$ROOT/App/Info.plist" > "$APP/Contents/Info.plist"

# Game data: the Tyrian 2000 data files only, not the DOS programs.
find "$ROOT/GameData/tyrian2000" -maxdepth 1 -type f \
    ! -iname '*.exe' ! -iname '*.pif' ! -iname '*.ovl' ! -iname '*.ico' \
    -exec cp {} "$APP/Contents/Resources/GameData/" \;

codesign --force --sign - "$APP"

echo "Architectures: $(lipo -archs "$APP/Contents/MacOS/MetalTyrian")"
echo "Data files: $(ls "$APP/Contents/Resources/GameData" | wc -l | tr -d ' ')"
echo "Built $APP"
