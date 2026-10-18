# MetalTyrian

An unofficial native Mac port of Tyrian 2000, the 1995 vertical-scrolling
shooter by Eclipse Software. All five episodes, drawn with Metal, with Apple
frameworks for input and sound. Not affiliated with the original authors.

Version 1.0.0.

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

## Build

Needs Xcode 15 or later.

```
"/Users/beckettj/Developer/MetalTyrian/scripts/build-app.sh"
```

The app lands in `~/Library/Caches/MetalTyrian-build/MetalTyrian.app`.

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

MetalTyrian is under the GNU GPL version 2 or later, as OpenTyrian (`LICENSE`). Each licence above is bundled in the app under Contents/Resources/Licenses (`App/Licenses` here). The Tyrian 2000 data in `GameData/` is freeware under the terms above.
