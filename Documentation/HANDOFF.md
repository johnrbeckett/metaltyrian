# MetalTyrian handoff

## Status, 2026-10-17

| Item | State |
|---|---|
| Folder | Created at `~/Developer/MetalTyrian/` |
| Git repo | Local repo on `main`, created 2026-10-17. Local only for now (John, 2026-10-17). A private GitHub repo `johnrbeckett/metaltyrian` exists but the token has no access to it; nothing is pushed. |
| Code | `PLAN-V1.md` phases 0 and 1 done, phase 2 written. No SDL at all (John's option 2): `Sources/SDLShim` implements the SDL calls the game uses. Phase 1 (game on its own thread, Metal renderer, keyboard) built and played by John 2026-10-17. Phase 2: mouse and glow-strength menu committed; gamepads (one virtual "MetalTyrian Gamepad" fed from GameController) written, not yet built on the Mac. Name entry needed no work: typed text already reaches the game. |
| Research | `Documentation/RESEARCH.md` |
| OpenTyrian source | `Source/opentyrian/`, commit 624e2f2 (2026-10-09) |
| OpenTyrian2000 source | `Source/opentyrian2000/`, commit aad5aca (2026-02-22) |
| Game data | `GameData/tyrian21/`, `GameData/tyrian2000/`, original zips beside them |
| Art | `Art/Remastered/`, Daniel Cook's CC-BY 3.0 pack and its zip |
| Sprite decoder | `Research/tools/shp.py`, checked against the pack |
| Scaling mock-ups | `Research/scaling/index.html`, eight modes from a real demo frame. Picked: sharp pixels default, CRT option. |
| MetalPreview test app | `Research/metal-preview/`, Swift package: 6 upscalers x 2 display modes x HDR glow on 351 demo frames. Built and run by John 2026-10-17. Picks: no upscaler; sharp pixels and authentic VGA selectable; HDR glow optional, dialled back, default strength 1.2 (great with authentic VGA). C scalers verified on Linux (hq4x byte-identical to OpenTyrian). Build steps in its README. |

## Next

John to build and try the mouse and a gamepad (he said "assume fine" for the mouse, 2026-10-17). Then phase 3: sound through AVAudioEngine, with a real `SDL_BuildAudioCVT`/`SDL_ConvertAudio` in the shim.

Engine changes so far, all under `#ifdef METALTYRIAN` in `Engine/src/video.c`: frame handover at the top of `scale_and_flip`, and `scaleWindowDistanceToScreen` passing mouse distances through unchanged (the app sends them in game pixels).

## Commits

| Commit | What | Pushed |
|---|---|---|
| 9924780 | Start MetalTyrian: research, plan and Tyrian 2000 data | No |
| 8bba68c | Add OpenTyrian2000 as Engine/ (git subtree, aad5aca) | No |
| d0460d4, 6629fbc | Upstream fixes: extra-ship crash, data-cube memory corruption | No |
| 17336f5 | Phase 0: app shell, build script, licence, README | No |
| db6d9de | Phase 1: game runs natively on the SDL shim and Metal | No |
| 89fee1c | Phase 2: mouse control and glow strength menu | No |

## Notes

- The two `Source/` clones were made in the VM's scratch space and copied in, because git cannot remove its own lock files on the mounted folder. A failed first clone went to `Trash/metaltyrian-failed-clone-2026-10-17/`. Treat both clones as reference copies; the project repo will vendor OpenTyrian's code in a planned way.
