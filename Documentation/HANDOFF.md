# MetalTyrian handoff

## Status, 2026-10-18 (end of session)

| Item | State |
|---|---|
| Folder | `~/Developer/MetalTyrian/` |
| Git | `main`, pushed to the private GitHub repo `johnrbeckett/metaltyrian` (remote `origin`, 2026-10-18; John asked for the push). Push with the keyserver key `GITHUB_TOKEN_ORBIT_ANGEL` (edit, account johnrbeckett) materialised into a registered scratch folder (e.g. `~/Developer/.claude-tmp-env-mt-1018`), sent as an `http.extraHeader`, so no token is stored in `.git/config`; clear the `.env` afterwards. Commit author `John Beckett <johnreabeckett@gmail.com>`. |
| Version | `VERSION` 0.1.0; the title screen shows "Metal 0.1.0" (from the bundle's short version). |
| Build | `scripts/build-app.sh` builds to `~/Library/Caches/MetalTyrian-build/MetalTyrian.app`; Swift can only be compiled on the Mac. Engine and shim are checked headless on Linux with `Tests/linux/run.sh` (see Testing). |
| Seen working on the Mac by John | Everything up to d0dca16 (2026-10-18): game, menus, mouse, gamepads, crisp text, HD title and pictures, new HUD with Orbitron labels, stereo audio, dynamic light (Light: Medium too), title version "Metal 0.1.0", menu titles at 90%, the shaded equipped-item bar, the "!" fix. |
| Research | `Documentation/RESEARCH.md`, `Research/` (gitignored): pictures, sprite sheets, crisp checks, screen checks, light preview. |
| Art inputs | `Art/Incoming/` (gitignored): title art originals, ChatGPT-regenerated screens (`screens/`), Magnific tests. |

## Where things stand

- Fun over faithful; game mechanics matter. Remove dead options. Talk features through and get John's yes before coding.
- Graphics: Sharp/VGA look, HDR glow, Aspect, Borders, Text (Crisp/Original), Light (Off/Subtle/Medium/Strong, default Medium). Crisp Orbitron text everywhere, with a leak finder. High-resolution art for the title (John's) and every other tyrian.pic picture (ChatGPT, prepared by `scripts/build-pictures.py`, needs `Research/pics` and `Research/screens-check/live_mask.npy`).
- Sound: Nuked OPL3 music at 49716 Hz, stereo effects placed by screen position (70/30), limiter; Sound > Output Stereo/Mono.
- Controls: Keyboard 5, Gamepad 5, Mouse Direct at 7, Momentum Off by default; two gamepads for couch co-op.

## Next (offered, waiting for John's choice)

1. Sprite art upscaling. Magnific tests: the big ship-screen silver ship (OPTION_SHAPES 32, 93x110; earlier mislabelled the Talon, which is 31, the gold one) came out faithful with creative mode, VideoGameAssets, creativity 3, resemblance 6, then mirrored and cut to the original outline. Offered: the other four big ships (27, 31 the Talon, 44, 45), about 720 credits (balance about 236,700). In-game sprites need a replacement pipeline (talked about: start with pickups); John wants exact shapes, no invented parts.
2. Small leaks: text shaded over by a dark menu box stays as old pixel font (the faint "Player" under the in-game menu); two coincidental hits from the leak test (a jukebox 'e' on the title, P/S on the Next Level map); level-start glow text and credits untested.
3. Crisp text: the large font's descenders overhang title boxes a little; the 90% shrink is the current answer (per-letter descender squeezing looked worse and was reverted).
4. Lighting: tune strength or reach once John has played more; per-sprite normal maps only if the light spill proves itself.
5. Phase 4 Mac polish from `PLAN-V1.md`: window sizes, About box with credits (OpenTyrian, Nuked OPL3, Orbitron, art), full-screen behaviour, ProMotion pacing, signing for release.
6. Two-player HUD art (picture 6) has had its labels removed and Orbitron labels added but hasn't been looked at closely on the Mac.

## Testing (Linux, headless)

`Tests/linux/run.sh GameData/tyrian2000 OUT SECONDS` builds engine and shim with gcc and runs the game. Environment: `MT_SCRIPT` steps (`w:text` wait, `k:scancode` key, `p:n` pause, `d` dump, `s` show "Secret Level!"), `MT_TEXT_LOG`, `MT_TEXT_LEAKS=DIR` (leak finder), `MT_TEXT_FIND=x` (where a letter is drawn), `MT_PIC_DUMP=DIR` (frame plus picture foreground), `MT_PIC_EVERY=n`, `MT_WAV=file` (stereo recording), `MT_DUMP_DIR`. On the device VM, `$HOME/runsec.sh NAME SCRIPT SECONDS` wraps a leak run and makes a contact sheet. The host pushes window focus at start (the game pauses itself otherwise).

## History

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

- Crisp text, 2026-10-18 (John: "go"): `mt_text.c` records every letter the three sprite fonts draw (`sprite.c` hooks) and the shadows under them, and HUD labels painted into the art (`tyrian2.c`, one-player panel) and sidekick icon words (`varz.c`). Each shown frame (`video.c`) it checks which letters are still in the picture and hands them, plus a mask of their pixels and shadows, to the app (`mt_set_frame_text`). The app (`CrispText.swift`) erases the masked pixels, joins letters into words, and draws Orbitron fitted to the original ink box on a 960x600 overlay (3 sub-pixels per game pixel, hard edges, palette gradient, lower/right stroke edges darkened, shadows); `display_frag` composites it. Setting: Graphics > Text (Crisp, Original) and View > Crisp Text (Command-T), default Crisp. Font bundled from Google Fonts unmodified (OFL Reserved Font Name). Since then: letters remember several looks (plain/highlighted); shadows and outlines of every kind are erased and redrawn crisp (round-brush contour for all-round outlines, the title menu's in its own dark colour); words fitted one by one; the mouse pointer is lifted out of the frame and drawn over the text (solid, not see-through); partly covered or box-clipped letters stay crisp, cut to the part still shown; text drawn over text replaces it; a full screen change clears leftovers; game_screen letters noted at both positions (ship specs). Erased pixels are filled from their neighbours inwards. Leak finder: `MT_TEXT_LEAKS=DIR` reports original-font pixels that would reach the screen; `Tests/linux/linux_host.c` also takes `MT_SCRIPT` (wait for on-screen text, press keys, dump frames), `MT_KEYS`, `MT_MOUSE`, `MT_TEXT_DUMP`. Leak test 2026-10-18 clean over title, Setup menus, High Scores, Instructions, new-game menus, shop screens, data cubes, ship specs, pause menu, HUD and 100 s of demo. Not yet: two-player panel labels; level-start glow text and credits untested.

- High-resolution pictures, 2026-10-18 (John: "build it"; seen on the Mac, committed c7ce4ae). Title: John's art in `App/Title` (planet, TYRIAN, 2027 with the TM removed), always shown. Other tyrian.pic pictures: ChatGPT-regenerated art (`Art/Incoming/screens`, gitignored) prepared by `scripts/build-pictures.py` into `App/Pictures/picNN.png` (all but 4). `mt_title.c` snapshots every picture as `JE_loadPic` loads it, finds which one each frame shows (enough pixels as loaded, counting its plain background apart; the HUD's playfield excluded), and hands the app the picture number, its palette fade, the playfield hole and a mask of pixels drawn on top; `display_frag` draws the art, the overlay carries the game's drawing on top (HUD: Scale3x-smoothed, with the art showing round it). HUD: painted labels removed from the art, crisp Orbitron labels on both panels (two-player labels added), gun-power slots keep the original recess, message strip (OPTION_SHAPES 36) blitted at level start and counted as picture. Crisp text off: the HUD labels come from the frame. Pictures 11 (bars fitted) and 13 (grid warped) adjusted. Linux checks 2026-10-18: pictures 10, 12, title, 2, 1, 3, 6 recognised with fades; leak test clean on one- and two-player HUD.

- Audio, 2026-10-18 (John: "all three, probably no reverb"; John: "I like the stereo"; committed b0c665b): music on Nuked OPL3 1.8 (`opl3.c`/`opl3.h` verbatim, `mt_opl.c`; DOSBox's `opl.c` compiled out), the game's audio running at the chip's 49716 Hz so the music needs no resampling (effects sinc-resampled 11025 to 49716 at load, macOS converts to the device). Stereo mixer (`loudness.c` `mt_mix`): effects placed by where they happen (`soundQueueX` set beside `soundQueue` at 32 sites: enemy shots, launches, hits, explosions, the player's guns, pickups, damage), pan law 70/30 at the edges, music centred; limiter at -1 dB (instant attack, 120 ms release) instead of hard clipping. Sound > Output: Stereo/Mono. Levels checked against the old emulator on 60 s of the attract demo: within a few per cent.

- Dynamic light, 2026-10-18 (John: "yes"; John: "pretty nice"; committed 21a40c1): Graphics > Light: Off / Subtle / Medium / Strong (default Medium, John 2026-10-18). In play only (the HUD's playfield hole), `emit_frag` takes the glow's hot pixels, blurred wide (sigma 11 game pixels) into `lightTex`; `display_frag` adds that light to each playfield pixel in its colour, scaled by the surface's colour and brightness, with a shape term from the art's own shading against the light's direction. Gains 22 / 35 / 50, tuned on demo frames with `Research/screens-check/light_preview.py`. Graphics menu items 18 px apart when there are eight.

- Fixes, 2026-10-18: large-font crisp words (menu titles, GAME OVER) drawn at 90%, centred and hung from the top, so descenders stay inside title boxes (John; squeezing descenders per letter was tried and reverted). Letters whose solid body can't be seen no longer count as shown (stale "!" after "Secret Level!"). Over picture art, pixels the game only shades darker (the equipped-item bar on the upgrade screens) darken the art instead of replacing it. Default controls: Keyboard 5, Gamepad 5, Mouse Direct at 7, Momentum Off. Linux host: focus pushed at start, `s` script step shows the "Secret Level!" banner, `MT_TEXT_FIND`.

- Arcade ship choice, 2026-10-18 (John: "any one of the ships... why not unlock them"): 1 Player Arcade (and Timed Battle) asks for a ship after the difficulty screen (`mt_arcade_ship_select` in `menus.c`, called from `newGame` in `tyrian2.c`): all 18 ships in the data (Talon, SuperCarrot, Phoenix, Maelstrom, Stalkers, Fang, U-Ship, Silver Ship, Nort Ship, The Stalker 21.126, Storm, Red Dragon, Gencore II, PeteZoomer, Rum Bottle), shown on the upgrade screen's picture with the big ship picture, name and armour; starts on the Stalker (the original's ship), remembers the last pick; Esc goes back to the title. Ships differ only in armour and looks (`spd` in the data is unused by the game); weapons come from pickups as before. 2 Player Arcade unchanged. Ships' big pictures: bigshipgraphic 28/32/33/45/46 = OPTION_SHAPES 27/31 (gold Talon)/32 (silver, the Stalker family; the app icon)/44/45.

John's direction, 2026-10-17: balance for "fun" over "faithful". Defaults so far: Retro look, glow 2.0, Square aspect, Black borders, Keyboard Speed 5, Gamepad Speed 5, Mouse Speed 7, Momentum Off, Mouse Mode Direct (John's picks, 2026-10-18). App icon: the silver ship from the ship screen (OPTION_SHAPES 32, the Stalker family's picture, original pixel art at 6x) in the gold tile, `scripts/make-icon.py Research/fonts/raw/opt_32.ppm App/AppIcon-1024.png --pixel` (John, 2026-10-18; replaced the r4 render).

## Commits

| Commit | What | Pushed |
|---|---|---|
| 9924780 | Start MetalTyrian: research, plan and Tyrian 2000 data | Yes |
| 8bba68c | Add OpenTyrian2000 as Engine/ (git subtree, aad5aca) | Yes |
| d0460d4, 6629fbc | Upstream fixes: extra-ship crash, data-cube memory corruption | Yes |
| 17336f5 | Phase 0: app shell, build script, licence, README | Yes |
| db6d9de | Phase 1: game runs natively on the SDL shim and Metal | Yes |
| 89fee1c | Phase 2: mouse control and glow strength menu | Yes |
| 1edccd5 | Phase 2: gamepads through GameController | Yes |
| 790ee02 ... 3e2a226 | Momentum, Mouse Direct, menu clean-up, two gamepads, app icon | Yes |
| ec49133 | Crisp text: Orbitron over Tyrian's lettering, with leak finder | Yes |
| c7ce4ae | High-resolution title and pictures over the original art | Yes |
| b0c665b | Audio: Nuked OPL3 music, stereo effects placed by position, limiter | Yes |
| 21a40c1 | Dynamic light, smaller menu titles, shading over new art, fixes | Yes |
| 9695afd | Title screen shows "Metal" and the app's version | Yes |

Everything up to 9695afd was pushed to `origin/main` on 2026-10-18.

## Notes

- The two `Source/` clones were made in the VM's scratch space and copied in, because git cannot remove its own lock files on the mounted folder. A failed first clone went to `Trash/metaltyrian-failed-clone-2026-10-17/`. Treat both clones as reference copies; the project repo will vendor OpenTyrian's code in a planned way.
