# MetalTyrian

An unofficial native Mac port of Tyrian 2000, the 1995 vertical-scrolling
shooter by Eclipse Software. All five episodes, drawn with Metal, with Apple
frameworks for input and sound. Not affiliated with the original authors.

Work in progress. See `Documentation/PLAN-V1.md`.

## Build

Needs Xcode 15 or later.

```
"/Users/beckettj/Developer/MetalTyrian/scripts/build-app.sh"
```

The app lands in `~/Library/Caches/MetalTyrian-build/MetalTyrian.app`.

## Credits

- Tyrian: Eclipse Software, published by Epic MegaGames in 1995. Released as freeware by Jason Emery in 2004: "Feel free to play it all you want and share it with friends."
- Tyrian 2000: Eclipse Software and XSIV Games, 1999. Freeware.
- Game code: OpenTyrian (github.com/opentyrian/opentyrian) and OpenTyrian2000 (github.com/KScl/opentyrian2000), in `Engine/`.

## Licence

GPL version 2 or later, as OpenTyrian. See `LICENSE`. The Tyrian 2000 data in
`GameData/` is freeware under the terms above.
