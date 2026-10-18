#!/bin/bash
# MetalTyrian: build the release, check it, notarise, staple, zip, and verify
# the zip the way a visitor gets it. Same pattern, Developer ID and "orbitangel"
# notarytool profile as Night Watch (Night Watch/Mac Saver/scripts/release.sh).
#
#   scripts/release.sh            full release (a few minutes; notarisation waits for Apple)
#   scripts/release.sh --check    build and checks only, no notarisation (a dry run)
#
# Run scripts/tag-version.sh X.Y.Z first. Stops at the first failed check.
# Scratch goes under ~/Developer/Trash, the finished zip under ~/Developer/Outputs.
set -u
cd "$(dirname "$0")/.." || exit 1
ROOT="$PWD"
DEV="$HOME/Developer"
STAMP="$(date +%Y-%m-%d-%H%M%S)"
SCRATCH="$DEV/Trash/metaltyrian-release-$STAMP"
OUT="$DEV/Outputs"
IDENTITY="Developer ID Application: John Beckett (PDF9QMZ5G4)"
PROFILE="orbitangel"
VERSION="$(tr -d '[:space:]' < VERSION)"
BUILT="$HOME/Library/Caches/MetalTyrian-build/MetalTyrian.app"
APP="$OUT/MetalTyrian.app"
ZIP="$OUT/MetalTyrian-$VERSION.zip"
CHECK_ONLY=0; [ "${1:-}" = "--check" ] && CHECK_ONLY=1
fail() { echo; echo "STOP: $1"; exit 1; }
want() { # want "label" "expected" "actual"
  [ "$2" = "$3" ] && echo "ok   $1 = $3" || fail "$1 is '$3', expected '$2'"
}

mkdir -p "$SCRATCH" "$OUT" || fail "cannot make scratch/output folders"

echo "== 0. Source: clean tree, tagged v$VERSION"
[ -z "$(git status --porcelain)" ] && echo "ok   working tree clean" || fail "uncommitted changes (commit, or run with --check)"
TAGGED="$(git tag --points-at HEAD | grep -x "v$VERSION" || true)"
if [ -n "$TAGGED" ]; then echo "ok   HEAD is v$VERSION"
elif [ $CHECK_ONLY = 1 ]; then echo "note HEAD is not tagged v$VERSION (fine for a dry run)"
else fail "HEAD is not tagged v$VERSION: run scripts/tag-version.sh $VERSION"; fi

echo "== 1. Release build (Apple Silicon), signed with the Developer ID"
MT_SIGN_IDENTITY="$IDENTITY" scripts/build-app.sh > "$SCRATCH/build.log" 2>&1 \
  || { grep -m5 "error:" "$SCRATCH/build.log"; fail "build failed (log: $SCRATCH/build.log)"; }
ARCHS="$(lipo -archs "$BUILT/Contents/MacOS/MetalTyrian")"
[ "$ARCHS" = "arm64" ] && echo "ok   archs = $ARCHS" || fail "archs are '$ARCHS', expected arm64"

# ditto into a fresh place; the old copy moves to Trash first.
[ -d "$APP" ] && mv "$APP" "$DEV/Trash/outputs-metaltyrian-$STAMP.app"
ditto "$BUILT" "$APP" || fail "copy to Outputs failed"

echo "== 2. Checks before spending a notarisation round trip"
CS="$(codesign -dv --verbose=4 "$APP" 2>&1)"
echo "$CS" | grep -q "Authority=$IDENTITY" && echo "ok   signed with Developer ID" || fail "not signed with the Developer ID identity"
echo "$CS" | grep -q "flags=0x10000(runtime)" && echo "ok   hardened runtime" || fail "hardened runtime missing"
echo "$CS" | grep -q "^Timestamp=" && echo "ok   secure timestamp" || fail "no secure timestamp"
codesign --verify --strict --deep "$APP" && echo "ok   signature verifies" || fail "codesign --verify failed"
PL="$APP/Contents/Info.plist"
want "CFBundleShortVersionString" "$VERSION" "$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$PL")"
want "LSMinimumSystemVersion" "13.0" "$(/usr/libexec/PlistBuddy -c 'Print :LSMinimumSystemVersion' "$PL")"
for f in AppIcon.icns Credits.rtf Licenses/GPL-2.0.txt Licenses/LGPL-2.1.txt Licenses/MersenneTwister.txt Licenses/OFL-1.1-Orbitron.txt Licenses/Tyrian-freeware.txt; do
  [ -f "$APP/Contents/Resources/$f" ] && echo "ok   bundled $f" || fail "missing $f in Contents/Resources"
done
for d in Fonts Title Pictures GameData; do
  N="$(ls "$APP/Contents/Resources/$d" 2>/dev/null | wc -l | tr -d ' ')"
  [ "$N" -gt 0 ] && echo "ok   $d: $N files" || fail "Contents/Resources/$d is empty"
done

if [ $CHECK_ONLY = 1 ]; then echo; echo "Dry run passed. Not notarised. App: $APP"; exit 0; fi

echo "== 3. Notarise (waits for Apple), staple, verify"
ditto -c -k --keepParent "$APP" "$SCRATCH/MetalTyrian-submit.zip" || fail "submission zip failed"
xcrun notarytool submit "$SCRATCH/MetalTyrian-submit.zip" --keychain-profile "$PROFILE" --wait 2>&1 | tee "$SCRATCH/notarize.txt"
grep -q "status: Accepted" "$SCRATCH/notarize.txt" || {
  ID="$(awk '/^  id:/{print $2; exit}' "$SCRATCH/notarize.txt")"
  echo "Notarisation submission id: $ID"
  [ -n "$ID" ] && xcrun notarytool log "$ID" --keychain-profile "$PROFILE"
  fail "notarisation was not Accepted"
}
echo "Notarisation submission id: $(awk '/^  id:/{print $2; exit}' "$SCRATCH/notarize.txt")  (log kept in $SCRATCH/notarize.txt)"
xcrun stapler staple "$APP" || fail "staple failed"
xcrun stapler validate "$APP" || fail "staple validate failed"

echo "== 4. Distributable zip (made from the STAPLED app) + visitor check"
rm -f "$ZIP"
ditto -c -k --keepParent "$APP" "$ZIP" || fail "zip failed"
mkdir -p "$SCRATCH/gatekeeper-check" && ( cd "$SCRATCH/gatekeeper-check" && ditto -x -k "$ZIP" . ) || fail "could not extract the zip"
spctl -a -vv -t exec "$SCRATCH/gatekeeper-check/MetalTyrian.app" 2>&1 | tee "$SCRATCH/spctl.txt"
grep -q "source=Notarized Developer ID" "$SCRATCH/spctl.txt" || fail "spctl did not report Notarized Developer ID on the extracted zip"
xcrun stapler validate "$SCRATCH/gatekeeper-check/MetalTyrian.app" || fail "extracted zip is not stapled"

echo
echo "DONE. MetalTyrian $VERSION signed, notarised, stapled and verified from the zip."
stat -f "%N  %z bytes" "$ZIP"
echo "Next: push with tags, then attach $ZIP to the v$VERSION release on GitHub:"
echo "  gh release create v$VERSION \"$ZIP\" --title \"MetalTyrian $VERSION\" --notes-file CHANGELOG.md"
