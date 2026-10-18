# MetalTyrian handoff

## Status, 2026-10-17

| Item | State |
|---|---|
| Folder | Created at `~/Developer/MetalTyrian/` |
| Git repo | Local repo on `main`, created 2026-10-17. Local only for now (John, 2026-10-17). A private GitHub repo `johnrbeckett/metaltyrian` exists but the token has no access to it; nothing is pushed. |
| Code | `PLAN-V1.md` phases 0 to 3 written; no SDL at all (John's option 2): `Sources/SDLShim` implements the SDL calls the game uses. Phase 1 (game thread, Metal renderer, keyboard) built and played by John 2026-10-17. Phase 2: mouse, glow-strength menu, gamepads (one virtual "MetalTyrian Gamepad" fed from GameController); name entry needed no work. Phase 3: sound through AVAudioEngine pulling the game's own mixer via `mt_audio_render`, muted in the background. Phases 2 (gamepad part) and 3 not yet built on the Mac. |
| Research | `Documentation/RESEARCH.md` |
| OpenTyrian source | `Source/opentyrian/`, commit 624e2f2 (2026-10-09) |
| OpenTyrian2000 source | `Source/opentyrian2000/`, commit aad5aca (2026-02-22) |
| Game data | `GameData/tyrian21/`, `GameData/tyrian2000/`, original zips beside them |
| Art | `Art/Remastered/`, Daniel Cook's CC-BY 3.0 pack and its zip |
| Sprite decoder | `Research/tools/shp.py`, checked against the pack |
| Scaling mock-ups | `Research/scaling/index.html`, eight modes from a real demo frame. Picked: sharp pixels default, CRT option. |
| MetalPreview test app | `Research/metal-preview/`, Swift package: 6 upscalers x 2 display modes x HDR glow on 351 demo frames. Built and run by John 2026-10-17. Picks: no upscaler; sharp pixels and authentic VGA selectable; HDR glow optional, dialled back, default strength 1.2 (great with authentic VGA). C scalers verified on Linux (hq4x byte-identical to OpenTyrian). Build steps in its README. |

## Next

John to build and try sound and a gamepad. Then phase 4: Mac polish (window sizes, About box with credits, fullscreen behaviour, ProMotion pacing).

Sound check, 2026-10-17: stock OpenTyrian2000 (aad5aca, SDL 2.30.0 built from source, disk audio driver) and MetalTyrian's headless build (`MT_WAV=out.wav Tests/linux/run.sh ...`) produce bit-identical music for the first 25 seconds from the first note; they part only when the demo's sound effects start at slightly different moments. Effect resampling (11025 to 44100 Hz) matches SDL's own converter to 41 dB. Scratch build of SDL and stock OpenTyrian2000 is in `Research/audio-check/` (ignored by git).

Engine changes so far, all under `#ifdef METALTYRIAN`:

- `video.c`: frame handover at the top of `scale_and_flip`; `scaleWindowDistanceToScreen` passes mouse distances through unchanged (the app sends them in game pixels); `toggle_fullscreen` (Alt+Return) flips the shared full-screen setting.
- `opentyr.c`: the Setup → Graphics menu shows MetalTyrian's settings (Display: Window/Full Screen, Look: Sharp Pixels/Authentic VGA, HDR Glow: Off/1.0/1.2/1.5/2.0), shared with the Mac View menu through `mt_setting`/`mt_set_setting`. OpenTyrian's Scaler and Scaling Mode are gone from it.
- `config.c`: detail level fixed at 4 ("Pentium", the highest regular level) in `JE_initProcessorType`; John, 2026-10-17: not a setting.
- `mainint.c`: the in-flight options menu hides the Detail row; keys and the digital d-pad move by Keyboard Speed (demos keep the original 1); gamepads always use analog movement.
- `tyrian2.c`: `JE_starShowVGA` shows each frame as soon as it is drawn, then waits for the frame's time slot and reads input; OpenTyrian waited first and read input before the wait (2-3 frames of lag). Game speed checked unchanged on Linux (about 33 frames a second in the demo, before and after).
- `mainint.c` also: Momentum (Setup > Controls; Original, Light, Off). Only the drift from the player's own steering changes; level pushes (magnets, currents, hit knockback) keep the original velocity physics. Demos always use the original physics. `tyrian2.c` resets it at level start.
- `mainint.c` also: Mouse Mode Direct (default): an invisible pointer moves over the ship's reachable area (x 40-256, y 10-160) and the ship flies to it at the game's top mouse speed; keys or a gamepad hand control back; no steering drift while the mouse drives; reversed-controls levels still flip it. The app feeds Direct mode raw GCMouse deltas (no macOS acceleration), learning the raw y direction from AppKit's deltas.
- `menus.c`: the game-type menu hides Network (no network play).
- `game_menu.c`: the ship screen's Joystick page hides Analog, Sensitivity and Threshold (always analog, speed in Setup > Controls); the Joystick row picks Gamepad 1 or 2; button assignments, Reset and Done remain.
- Gamepads: the shim reports two virtual gamepads ("MetalTyrian Gamepad 1" and "2"); the app gives the first connected controller slot 1 and the next slot 2, sets controller player lights, and frees a slot on disconnect. Single player reads both; 2 Player Arcade picks per player on its device screen. John chose to keep 2 Player Arcade as couch co-op, 2026-10-18.
- Menu audit, 2026-10-17 (John: "remove the dead"): every other option checked live: title (New Game, Load, High Scores, Instructions, Setup, Demo, Quit), Setup (Graphics, Sound, Controls, Jukebox), ship Options (Load, Save, volumes, Joystick, Keyboard, Mouse buttons), in-flight menu (volumes, Game Speed, Return, Quit).
- `keyboard.c`: SDL_QUIT (pushed by the app on Command-Q or closing the window) runs `JE_tyrianHalt(0)`, which saves `opentyrian.cfg`, `tyrian.cfg` and `tyrian.sav` before exiting; checked on Linux. The app also quits when the game ends (title-screen Quit).
- `joystick.c`: `joystick_axis_reduce` scales the stick by Gamepad Speed instead of the joystick sensitivity.
- `opentyr.c` also: Setup > Controls (Keyboard Speed 1-5, Gamepad Speed 1-10, Mouse Speed 1-10); Graphics gained Aspect (Original, Square, Exact) and Borders (Black, Ambient).

John's direction, 2026-10-17: balance for "fun" over "faithful". Defaults so far: Retro look, glow 2.0, Square aspect, Black borders, Keyboard Speed 2, Gamepad Speed 5, Mouse Speed 5, Momentum Light, Mouse Mode Direct. App icon from `Research/ai-renders/r3.png` via `scripts/make-icon.py`.

## Commits

| Commit | What | Pushed |
|---|---|---|
| 9924780 | Start MetalTyrian: research, plan and Tyrian 2000 data | No |
| 8bba68c | Add OpenTyrian2000 as Engine/ (git subtree, aad5aca) | No |
| d0460d4, 6629fbc | Upstream fixes: extra-ship crash, data-cube memory corruption | No |
| 17336f5 | Phase 0: app shell, build script, licence, README | No |
| db6d9de | Phase 1: game runs natively on the SDL shim and Metal | No |
| 89fee1c | Phase 2: mouse control and glow strength menu | No |
| 1edccd5 | Phase 2: gamepads through GameController | No |

## Notes

- The two `Source/` clones were made in the VM's scratch space and copied in, because git cannot remove its own lock files on the mounted folder. A failed first clone went to `Trash/metaltyrian-failed-clone-2026-10-17/`. Treat both clones as reference copies; the project repo will vendor OpenTyrian's code in a planned way.
