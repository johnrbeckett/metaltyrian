# What MetalTyrian changes from OpenTyrian

MetalTyrian plays Tyrian 2000 the way OpenTyrian2000 does: the same levels,
ships, weapons, shops and story, with the game's rules left alone. What
changes is everything around them: how the game looks, sounds and feels on a
Mac. The aim is a nice native Mac version that plays like a casual shooter,
lightly updated and still true to the original pixel art.

The game code lives in `Engine/`, and every change to it is marked
`#ifdef METALTYRIAN` (166 blocks across 28 files), so OpenTyrian2000's own
behaviour is always one `#else` away.

![Title screen](Images/title.png)

## A native Mac app

- Runs natively on Apple Silicon, on macOS 13 Ventura or later.
- Swift and Metal in place of SDL: a small C stand-in covers the parts of SDL 2 the game uses, and the Mac app supplies the window, drawing, input and sound.
- Signed and notarised; unzip, drag to Applications, play. The freeware Tyrian 2000 data is inside the app.
- Full screen by default for new players, with Command-F to switch.
- A Mac menu bar with Graphics, Sound and Controls menus that mirror the game's Setup and stay in step with it.
- Settings are saved between launches.
- An About box with credits, and every licence bundled in the app.

## How it looks

- **Metal display.** Two looks: Sharp (square pixels) or Retro (an old VGA monitor's scanlines). Aspect 4:3 or square, black or ambient borders.
- **HDR glow** on shots, engines and explosions, with no ceiling on how bright it gets.
- **Dynamic light.** Shots, engines and explosions light up the ships and scenery near them (Off, Subtle, Medium, Strong).
- **Edge smoothing.** Smooth outlines on ships, shots and scenery while the pixel art inside stays crisp (Off, Soft, Smooth).
- **Soft shadows.** The playfield sits inset in the HUD, and the score, lives and special weapon float over it.
- **A touch brighter** overall, with Retro no longer darker than Sharp. The effects stay as bright as they were.

## New art

- A new high-resolution title screen.
- The menu, shop and HUD pictures redrawn at high resolution from the original art, with the game's own pixels drawn on top.
- The HUD panels smoothed with the Scale3x method.

## Crisp text

- Orbitron lettering in place of Tyrian's pixel font, drawn sharp at full screen resolution throughout.
- The original font is one setting away.

## Sound

- The music on Nuked OPL3, an accurate model of the Sound Blaster's music chip.
- Sound effects in stereo, placed by where they happen on screen (or Mono, if you prefer).
- A limiter, so loud moments stay clean.

## Controls

- One layout for either hand. Right-handed: Space, Z, X, C with the arrows. Left-handed: Space, /, . and , with W A S D. Both work at once.
- Every key can be changed in Setup > Controls > Keys.
- Superbombs on their own key (C or comma, gamepad Y).
- Autofire for the main guns and for the sidekicks, each with its own key (1 and 2, or 0 and 9) and a setting in Setup > Controls.
- A mouse or trackpad works alongside the keys: the original mouse control, or fly to where you point. Adjustable speeds and momentum.
- Gamepads through Apple's Game Controller framework.
- Hold Command to free the mouse pointer during play.
- Quick special weapons (ready again within about a second) keep firing while Fire is held, and with autofire. Slow ones still need a tap, so autofire never spends them.
- A controls panel when a new game starts (the first level waits until you press fire) and whenever the game is paused.

## Easier menus

- Menu rows respond to the mouse across their whole width and height, so they're easy to hit.
- In the game and shop menus the highlight follows the mouse, and one click acts, buying included.
- A Keys screen and an Autofire setting added to Setup > Controls.
- Setup trimmed to what applies on a Mac; dead options removed.

## Single player throughout

- The start menu is Mode: Full Game, Arcade and Time Attack (Timed Battle renamed).
- Two-player modes, help pages and labels are left out: one list of saved games, one high score table, no "Player 1" label.
- An Arcade ship picker: fly any of the game's twelve ship looks.

## Fixes

- With main-gun autofire on, the special weapon fired once and never again. It now fires on each fresh tap of Fire, and the controls panel explains it.
- A typo in the ship picker corrected.

## Left alone

The levels, enemies, weapons, prices, scoring and story are OpenTyrian2000's,
unchanged. If something plays differently from Tyrian 2000, that's a bug.

![In the game](Images/gameplay.png)

See [CHANGELOG.md](../CHANGELOG.md) for the changes release by release.
