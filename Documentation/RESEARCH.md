# MetalTyrian research

Written 2026-10-17, from a chat with John that started on his phone. This
records what was checked, what was tested and what was decided. No code has
been written for the port yet.

## The idea

A native Mac version of Tyrian for Apple Silicon. Swift app shell, Metal
renderer, OpenTyrian's C game code kept as it is. Free on GitHub, signed and
notarized with John's Developer ID.

## What already exists

| Thing | Where | Notes |
|---|---|---|
| OpenTyrian | `Source/opentyrian/`, from github.com/opentyrian/opentyrian | C port on SDL2, GPLv2. Still maintained: last commit 624e2f2, 2026-10-09. Ships an official universal macOS build, not notarized. Has `make_macos.sh`, which bundles SDL2.framework and the 2.1 data. |
| OpenTyrian2000 | `Source/opentyrian2000/`, from github.com/KScl/opentyrian2000 | Fork that plays the Tyrian 2000 data (episode 5, extra ships). Last commit aad5aca, 2026-02-22. |
| Homebrew | `brew install open-tyrian` | Native arm64 build. SDL2 draws through Metal on macOS already. |
| Tyrian 2.1 data | `GameData/tyrian21/` and the zip | Freeware. Same mirror OpenTyrian uses: camanis.net/tyrian/tyrian21.zip. |
| Tyrian 2000 data | `GameData/tyrian2000/` and the zip | camanis.net/tyrian/tyrian2000.zip. Readme still carries the 1999 copyright notice. |
| Remastered Tyrian Graphics | `Art/Remastered/` | Daniel Cook's 2007 release of the game's art. CC-BY 3.0. |

Nobody has built a Metal-native front end, a modern Mac UI or an HD version.
That gap is the project.

## How OpenTyrian draws today

Checked against `Source/opentyrian/src/` at commit 624e2f2, 2026-10-17.

- **The game draws on the CPU.** `video.c` lines 70 to 74 create three 320x200, 8-bit software surfaces (`VGAScreen`, `VGAScreen2`, `game_screen`). Every sprite, background and line of text is written into these.
- **Scaling is on the CPU.** `video_scale.c` and `video_scale_hqNx.c` hold the scalers: None, 2x, 3x, 4x, Scale2x, Scale3x, hq2x, hq3x, hq4x. Each converts the 8-bit frame to 32-bit colour and writes it into a streaming texture with `SDL_LockTexture`. The biggest output is 4x, 1280x800.
- **SDL2 does the last step on the GPU.** `scale_and_flip()` (`video.c` around line 380) clears the window, `SDL_RenderCopy`s that texture into an aspect-correct rectangle and presents it. `SDL_CreateRenderer(main_window, -1, 0)` takes SDL's default driver, which is Metal on macOS. No render hints are set, so the stretch from 1280x800 to the window uses SDL's default nearest-neighbour filtering.

So Metal only shows a finished picture. The CPU does all the drawing and scaling.

**What this means for MetalTyrian.** `scale_and_flip()` is the one handover point: an 8-bit 320x200 frame plus a palette goes in, pixels on screen come out. Version 1 can replace that function and the scaler files with a Metal path (upload the 8-bit frame and palette as textures, do palette lookup, smoothing, CRT and glow in shaders) and leave the game code alone. Per-sprite rendering, needed for lighting or shader banking, would mean hooking the blit functions in `sprite.c` instead, a much bigger change.

## Which code base: trial merge, 2026-10-17

John's priority: every level, including Tyrian 2000's episode 5. That needs OpenTyrian2000: it is OpenTyrian plus 44 commits (about 1,200 lines in 38 files) for Tyrian 2000's level events, Timed Battle mode, text and item data, and fidelity fixes. Plain OpenTyrian with the 2000 level files would not play them correctly.

- **Full merge of upstream into the fork:** not clean. 28 upstream commits since the shared base (2026-02-06) include rewrites of config and saves, keyboard and mouse input, and file I/O. The merge stops with 41 conflict hunks in 16 source files, mostly in config, saves and the main loop.
- **Cherry-picking the real fixes instead:** two apply cleanly and matter: "Fix crash when selecting extra ship" and "Fix memory corruption due to too many data cubes". Saved in `Research/base-check/`. Four smaller ones conflict only because of the refactors (a `toupper` fix, a missing `math.h`, skipping sound when audio fails, logging); the first two are one-line fixes by hand, the last two are in code MetalTyrian replaces anyway.
- **Result checked:** OpenTyrian2000 plus the two fixes builds with no warnings (Linux, without SDL_net) and runs the Tyrian 2000 data headless for 100 seconds: XSIV Games logo, title, attract demo, no crash. `EPISODE_MAX` is 5 and episode 5 is detected from `levels5.dat`. Playing episode 5 itself is not yet tested.

Recommendation: base MetalTyrian on OpenTyrian2000 plus those cherry-picks and skip the upstream refactors, since the input, audio and video layers they rework are the ones MetalTyrian replaces.

## Licences

- **Code:** OpenTyrian is GPLv2, so MetalTyrian's code must be GPLv2 too. Simplest is GPLv2 for the whole repo, crediting OpenTyrian.
- **Art:** CC-BY 3.0. Credit line: "Remastered Tyrian Graphics" art by Daniel Cook (Lostgarden.com), linking his post at lostgarden.com/2007/04/05/free-game-graphics-tyrian-ships-and-tiles/. Keep it in `assets/LICENSE`, separate from the code licence. Mark any changes we make.
- **Game data:** freeware. Upstream OpenTyrian now carries `doc/tyrian-freeware-license.txt` (copy in `Research/base-check/`): Jason Emery released Tyrian as freeware in 2004 with the terms "Feel free to play it all you want and share it with friends." OpenTyrian2000's README says the Tyrian 2000 data is freeware too, and GOG gives Tyrian 2000 away free. Bundling the data in the app, as OpenTyrian's own Mac builds do, is on solid ground. The old Epic MegaGames notice in the 2.1 zip predates the freeware release.
- **Name:** no original logo or branding. Add a clear "unofficial, not affiliated" line.

## Finding: the "Remastered" pack is the game's own sprites

The pack's file names match the game's sprite files (`newsh*.shp`, `tyrian.shp` tables, `shapes*.dat`). A decoder for Tyrian's sprite format (`Research/tools/shp.py`) renders tyrian.shp tables 7 to 11, and they match the pack sheet for sheet and pixel for pixel. So:

- Every sprite maps 1:1 to what the engine draws. No matching job.
- All of it can ship under CC-BY.
- It is the original resolution (12x14 cells, ships 24x28). It is not HD art.

Sprite format, for the record: a table is a uint16 offset list, then each sprite is nibble run-length data. High nibble is a count of opaque pixels that follow, low nibble is transparent pixels to skip, a zero count ends a row, 0x0F ends the sprite. Cells are 12x14, 19 per row; a ship is a 2x2 block. Palette 0 in `palette.dat` is the game palette, in 6-bit VGA values.

## Tests run on the player ship, the USP Talon

All images are in `Research/images/` and `Research/ai-renders/`.

1. **Pixel-art smoothing (Scale4x).** `talon_test.png`. Cleans the jagged edges and still looks like Tyrian. Plain bilinear smoothing makes it muddy. Free, and easy to do in a Metal shader.
2. **AI redraws, Nano Banana Pro through Magnific.** `talon_ai_redraws.png`, renders `r1` to `r4`. All four kept the design and read well at real 4K game size. Option 4 was the pick. 300 credits.
3. **Sleeker version of option 4.** The first pass (`sleek-v1-*`) flipped the wings to sweep backwards. The corrected pass (`sleek-v2-*`) kept them swept forward. `sleek-v2-t3` became the base.
4. **Banking frames.** `talon_banking_test.png`, renders `bank-*`. Failed. The hard-bank frames turned the whole ship sideways instead of rolling it. The slight-bank frames barely changed. Image models draw each frame from scratch, so animation frames will not match. Better prompts will not fix this.

Total Magnific spend: 1,050 credits out of 240,000.

## Scaling mode mock-ups, 2026-10-17

`Research/scaling/index.html` compares eight ways to put the 320x200 frame on a Mac screen, using a real frame from the attract-mode demo. Open it in a browser; keys 1 to 8 switch modes.

The frame came from OpenTyrian built on Linux with a small patch (`Research/scaling/tools/frame-dump.patch`) that writes the 8-bit buffer and palette out from `scale_and_flip()`, plus OpenTyrian's own hq4x output. `tools/modes.py` renders the modes at 2400x1500, a 7.5x non-integer fit. Modes: nearest, bilinear, sharp pixels (integer prescale then bilinear), Scale4x, hq4x, CRT, and sharp pixels at the original 4:3 shape.

**Decided 2026-10-17, John's pick: sharp pixels as the default, CRT as the option.** Both are cheap fragment shaders. The 4:3 aspect mode was not picked; it stays out unless John asks for it.

### Round two, same day

Five more modes on the same page: xBRZ, authentic VGA, arcade CRT, HDR glow and CRT with phosphor afterglow, plus two short motion clips (CRT with and without afterglow) from 31 consecutive demo frames. The demo replays identically run to run, so frame numbers are stable. `tools/modes2.py` renders them; xBRZ came from the xbrz.py project's copy of Zenju's library, built locally.

- **xBRZ** is GPLv3. OpenTyrian's source headers say GPLv2 "or (at your option) any later version", so a GPLv3 build of MetalTyrian could include it. Check again when the repo licence is set.
- **HDR glow** in the mock-up is an SDR imitation. The real thing needs Metal's EDR output on an XDR or Liquid Retina screen.
- **Afterglow** only reads properly in motion, so judge it from the clips, not the still.

### Picked in MetalPreview, 2026-10-17

John built and ran `Research/metal-preview/`. His picks: **no upscaler** (plain pixels beat Scale4x, hq4x, xBRZ, xBR and MMPX), with **sharp pixels and authentic VGA as selectable display modes** and **HDR glow as an optional overlay**. The first HDR glow was too hot; it was dialled back the same day (smaller EDR lift, softer bloom, finer +/- steps). This replaces the earlier "sharp pixels plus CRT" pick: authentic VGA is the CRT-style option now. After the softer rebuild John settled on **HDR glow at strength 1.2**, and called it great with authentic VGA; that is the default strength now. The tuned shader in `Research/metal-preview/Sources/MetalPreview/Shaders.swift` is the reference for the real renderer.

## What would work for HD ships

- **3D model route:** build a 3D model of each ship from the HD art, then render all five banking frames from it. Consistent by construction. Fit for the dozen or so player ships.
- **Shader route:** ship one level HD sprite and a lighting map; Metal squashes and shades it as the ship rolls. No extra art, smooth banking. Flatter up close. Fit for enemies.

## Scope agreed so far

A full HD remaster means thousands of sprites. That is an art project, not a port. John's direction, 2026-10-17: upgrade the UI and leave the pixels.

1. **Version 1:** Swift app around OpenTyrian's C core. Metal renderer with pixel-art smoothing, optional CRT and glow. GameController support. Signed, notarized, on GitHub.
2. **Version 1.5, the UI:** HD HUD and fonts first, drawn sharp in Metal; a native settings window; then the upgrade shop and in-game menus, one screen at a time. On widescreen Macs the HUD can sit in the empty side bars, with the playfield centred.
3. **Later and optional:** HD player ships through the 3D route.

The known hard part of 1.5: OpenTyrian's menu code mixes drawing and logic, DOS style. Each screen has to be split into what it does and how it looks before it can be redrawn. The upgrade shop has the most logic.

## Open decisions for John

- Repo name, and public or private to start.
- Base on OpenTyrian (2.1 data) or OpenTyrian2000 (Tyrian 2000 data), or support both.
- Bundle game data or download on first launch.
- Final project name. `MetalTyrian` is the working folder name.
