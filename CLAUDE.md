# MetalTyrian

A native Apple Silicon Mac version of Tyrian: Swift app, Metal renderer,
OpenTyrian's C game code underneath. Free on GitHub, signed and notarized
with John's Developer ID (Team `PDF9QMZ5G4`, see `~/Developer/CLAUDE.md`).

Read `Documentation/RESEARCH.md` first, then `Documentation/HANDOFF.md`.
The `~/Developer/CLAUDE.md` rules apply here in full.

## Repo

`~/Developer/MetalTyrian/` is the git repo (branch `main`), one repo for the product per the `~/Developer` rule. `Engine/` is OpenTyrian2000 added with `git subtree` (history kept) plus two upstream OpenTyrian fixes; bring later fork fixes in with `git subtree pull --prefix=Engine https://github.com/KScl/opentyrian2000.git master`. `Source/`, `Research/`, `Art/`, the zips and `GameData/tyrian21/` sit beside the repo and are ignored. Build with `scripts/build-app.sh`; output goes to `~/Library/Caches/MetalTyrian-build/`, never into this folder. Version in `VERSION`.

## Rules for this project

- No code until John has approved a written plan for that piece of work.
- Keep the pixel art. The upgrade is the renderer and the UI, not the sprites.
- Code is GPLv2 (OpenTyrian). Art from `Art/Remastered/` is CC-BY 3.0 and must credit Daniel Cook (Lostgarden.com). Keep the two licences separate.
- `Source/opentyrian/` and `Source/opentyrian2000/` are reference clones. Do not edit them in place. The game code that ships is in `Engine/`.
- Change `Engine/` as little as possible: replace SDL underneath it, at the choke points named in `Documentation/PLAN-V1.md`.

## Layout

| Folder | Holds |
|---|---|
| `Documentation/` | Research, handoff, plans |
| `Engine/` | OpenTyrian2000 game code (C, GPLv2 or later) |
| `Sources/MetalTyrian/` | The Mac app (Swift) |
| `App/` | Info.plist template |
| `scripts/` | `build-app.sh` |
| `Source/` | Upstream reference clones |
| `GameData/` | Freeware Tyrian 2.1 and 2000 data, with original zips |
| `Art/Remastered/` | Daniel Cook's CC-BY sprite pack |
| `Research/` | Sprite decoder and the 2026-10-17 art tests |
