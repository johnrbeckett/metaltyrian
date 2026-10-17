# MetalTyrian handoff

## Status, 2026-10-17

| Item | State |
|---|---|
| Folder | Created at `~/Developer/MetalTyrian/` |
| Git repo | Local repo on `main`, created 2026-10-17. GitHub remote not yet set up. |
| Code | Phase 0 of `PLAN-V1.md` (approved 2026-10-17): app shell, build script, engine in `Engine/`. Not yet built on the Mac. |
| Research | `Documentation/RESEARCH.md` |
| OpenTyrian source | `Source/opentyrian/`, commit 624e2f2 (2026-10-09) |
| OpenTyrian2000 source | `Source/opentyrian2000/`, commit aad5aca (2026-02-22) |
| Game data | `GameData/tyrian21/`, `GameData/tyrian2000/`, original zips beside them |
| Art | `Art/Remastered/`, Daniel Cook's CC-BY 3.0 pack and its zip |
| Sprite decoder | `Research/tools/shp.py`, checked against the pack |
| Scaling mock-ups | `Research/scaling/index.html`, eight modes from a real demo frame. Picked: sharp pixels default, CRT option. |
| MetalPreview test app | `Research/metal-preview/`, Swift package: 6 upscalers x 2 display modes x HDR glow on 351 demo frames. Built and run by John 2026-10-17. Picks: no upscaler; sharp pixels and authentic VGA selectable; HDR glow optional, dialled back, default strength 1.2 (great with authentic VGA). C scalers verified on Linux (hq4x byte-identical to OpenTyrian). Build steps in its README. |

## Next

Phase 0: John runs `scripts/build-app.sh` and opens the app; the window title should list episodes 1 to 5. Then the GitHub repo (`johnrbeckett/metaltyrian`, private) and phase 1.

## Commits

| Commit | What | Pushed |
|---|---|---|
| 9924780 | Start MetalTyrian: research, plan and Tyrian 2000 data | No |
| 8bba68c | Add OpenTyrian2000 as Engine/ (git subtree, aad5aca) | No |
| d0460d4, 6629fbc | Upstream fixes: extra-ship crash, data-cube memory corruption | No |
| 17336f5 | Phase 0: app shell, build script, licence, README | No |

## Notes

- The two `Source/` clones were made in the VM's scratch space and copied in, because git cannot remove its own lock files on the mounted folder. A failed first clone went to `Trash/metaltyrian-failed-clone-2026-10-17/`. Treat both clones as reference copies; the project repo will vendor OpenTyrian's code in a planned way.
