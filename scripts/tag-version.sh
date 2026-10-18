#!/bin/bash
# Set VERSION, commit, and tag vX.Y.Z on main (step one of a release;
# scripts/release.sh then builds, signs and notarises).
# Update the README's feature list and Documentation/HANDOFF.md first; the
# About box's credits come from the README at build time. Pushing (with
# tags) is a separate step: see CONTRIBUTING.md.
# Usage: scripts/tag-version.sh 1.0.0
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
V="${1:?usage: scripts/tag-version.sh X.Y.Z}"
[[ "$V" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version must be X.Y.Z"; exit 1; }
cd "$ROOT"
git rev-parse -q --verify "refs/tags/v$V" >/dev/null && { echo "v$V already exists"; exit 1; }
echo "$V" > VERSION
git add -A VERSION README.md Documentation App scripts
git -c user.name="John Beckett" -c user.email="johnreabeckett@gmail.com" commit -q -m "MetalTyrian $V" || true
git -c user.name="John Beckett" -c user.email="johnreabeckett@gmail.com" tag -a "v$V" -m "MetalTyrian $V"
echo "Tagged v$V at $(git rev-parse --short HEAD)"
