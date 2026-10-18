# MetalTyrian

An unofficial native Mac port of Tyrian 2000, the vertical-scrolling shooter
by Eclipse Software (1995, Tyrian 2000 in 1999). All five episodes, drawn with
Metal, with Apple frameworks for input and sound. Not affiliated with the
original authors.

Version 1.0.0. Built on the game code of [OpenTyrian](https://github.com/opentyrian/opentyrian)
and [OpenTyrian2000](https://github.com/KScl/opentyrian2000).

## Download

Get the signed app from [Releases](https://github.com/johnrbeckett/metaltyrian/releases),
unzip it and drag MetalTyrian to Applications. It runs on macOS 13 Ventura or
later, on Apple Silicon and Intel Macs. The Tyrian 2000 game data (freeware)
is inside the app; there is nothing else to install.

## What's new on the Mac

- **Metal display:** sharp square pixels or an old VGA monitor's scanlines, HDR glow on shots and explosions, 4:3 or square aspect, black or ambient borders.
- **High-resolution art:** a new title screen and repainted menu, shop and HUD pictures, with the game's own pixels on top.
- **Crisp text:** Orbitron lettering in place of Tyrian's pixel font (or the original, if you prefer).
- **Edge smoothing:** smooth outlines on ships, shots and scenery, with the pixel art inside left crisp.
- **Dynamic light:** shots, engines and explosions light up what is near them.
- **Soft shadows:** the playfield sits inset in the HUD, and the score, lives and special float over it.
- **Sound:** the music on Nuked OPL3, effects in stereo where they happen on screen.
- **Controls:** keyboard, mouse (fly to where you point) and up to two gamepads for co-op.
- **Arcade ship picker:** fly any of the game's twelve ship looks in 1 Player Arcade.

## Playing

| | Keyboard | Mouse | Gamepad |
|---|---|---|---|
| Move | Arrow keys | Move the mouse | Left stick or D-pad |
| Fire | Space | Left button | A |
| Change rear weapon mode | Return | Scroll wheel, a trackpad swipe, or the middle button | B |
| Sidekicks | Left Control (left), Left Option (right) | Right button (both) | LB, RB |
| Superbomb, if you carry any | Either sidekick button | Right button | LB or RB |
| Game menu | Esc | | Menu |
| Pause | P | | Options |

Hold Command to free the mouse pointer during play (for the menu bar).
Keys can be changed in the game's own keyboard setup.

### Settings

In the game: **Setup > Graphics** (Display, Look, Glow, Aspect, Borders,
Text, Light, Edges), **Setup > Sound** (volumes, Stereo or Mono) and
**Setup > Controls** (keyboard, gamepad and mouse speed, momentum, mouse mode).
The View menu has shortcuts for the look (Command-1 Sharp, Command-2 Retro),
Glow (Command-G), Crisp Text (Command-T) and full screen (Control-Command-F).
Settings are saved between launches.

## Building from source

Needs a Mac with Xcode 15 or later.

```
git clone https://github.com/johnrbeckett/metaltyrian.git
cd metaltyrian
scripts/build-app.sh
```

The app lands in `~/Library/Caches/MetalTyrian-build/MetalTyrian.app` (ad hoc
signed, for running on your own Mac). See
[Documentation/ARCHITECTURE.md](Documentation/ARCHITECTURE.md) for how the
pieces fit, [CONTRIBUTING.md](CONTRIBUTING.md) for working on it, and
[CHANGELOG.md](CHANGELOG.md) for what changed.

## Repository layout

| Folder | Holds |
|---|---|
| `Sources/MetalTyrian/` | The Mac app in Swift: window, Metal renderer and shaders, input, audio, crisp text |
| `Sources/SDLShim/` | A small C stand-in for the parts of SDL 2 the game uses, and the interface the app talks to (`include/metaltyrian_host.h`) |
| `Engine/` | OpenTyrian2000's game code (C), with MetalTyrian's changes marked `#ifdef METALTYRIAN` |
| `App/` | Info.plist template, icon, fonts, high-resolution pictures and title art, bundled licences |
| `GameData/tyrian2000/` | The freeware Tyrian 2000 data files |
| `scripts/` | Build, release, credits, icon and picture preparation |
| `Tests/linux/` | A headless Linux host for checking the engine and shim without a Mac |
| `Documentation/` | Architecture, original plan, research and development notes |

## Credits

- MetalTyrian: John Beckett, built with Claude (Anthropic).
- Tyrian: Eclipse Software, published by Epic MegaGames in 1995. Released as freeware by Jason Emery in 2004: "Feel free to play it all you want and share it with friends."
- Tyrian 2000: Eclipse Software and XSIV Games, 1999. Freeware.
- Game code: OpenTyrian (github.com/opentyrian/opentyrian) and OpenTyrian2000 (github.com/KScl/opentyrian2000), in `Engine/`, under the GNU GPL version 2 or later. Includes hq2x, hq3x and hq4x by Maxim Stepin.
- Music chip emulation: Nuked OPL3 by Nuke.YKT (`Engine/src/opl3.c`), under the GNU LGPL 2.1 or later.
- Random numbers: Mersenne Twister by Makoto Matsumoto, Takuji Nishimura and Eric Landry (`Engine/src/mtrand.c`), under a BSD licence.
- Crisp text: Orbitron by Matt McInerney and the Orbitron Project Authors, under the SIL Open Font License 1.1.
- HUD smoothing: the Scale3x method by Andrea Mazzoleni.
- Art: title screen by John Beckett; menu, shop and HUD pictures redrawn at high resolution from the original Tyrian art, AI-assisted, prepared by John Beckett.

## Licence

MetalTyrian is free software under the GNU GPL version 2 or later, as
OpenTyrian (`LICENSE`). Each licence above is bundled in the app under
Contents/Resources/Licenses (`App/Licenses` here). The Tyrian 2000 data in
`GameData/` is freeware under the terms above. Tyrian is a trademark of its
owners; this project is unofficial.
