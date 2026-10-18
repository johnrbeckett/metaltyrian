# How MetalTyrian fits together

MetalTyrian runs OpenTyrian2000's C game code nearly unchanged, on its own
thread, and replaces SDL underneath it with a small shim. A Swift app owns
the window, input, sound and a Metal renderer, and talks to the game only
through the shim's interface, `Sources/SDLShim/include/metaltyrian_host.h`.

```
Swift app (Sources/MetalTyrian)
  main.swift     app, menus, saved settings
  GameView       keyboard, mouse, pointer
  Gamepads       GameController
  Renderer       Metal passes (Shaders.swift)
  CrispText      Orbitron text overlay
  Audio          AVAudioEngine
        ^ frames, text, pictures, edges        | input, settings
        |                                      v
C shim (Sources/SDLShim): the SDL 2 calls the game uses + metaltyrian_host.h
        ^                                      |
        |                                      v
Game thread (Engine/src, OpenTyrian2000): 320x200, 8-bit palette
```

## The game thread

`Engine/` is OpenTyrian2000 (added with `git subtree`, history kept). Its
`main` is renamed `tyrian_main` and started on a thread of its own. It draws
into 320x200 8-bit surfaces exactly as before. Every MetalTyrian change in it
is inside `#ifdef METALTYRIAN`, so a plain build is still OpenTyrian2000.
Bigger additions live in files of their own:

| File | Does |
|---|---|
| `mt_text.c` | Finds the text the game draws (each font's glyphs, where and in what colour) so the app can draw it again in Orbitron, and masks the original letters |
| `mt_title.c` | Recognises which full-screen picture a frame shows, works out what the game drew over it, and passes the high-resolution art, the HUD's playfield hole, the floating overlays (for their shadow) and the edge-smoothing layers |
| `mt_opl.c`, `opl3.c` | Music through Nuked OPL3 at the chip's own 49716 Hz |
| `loudness.c` (`mt_mix`) | Stereo mixer: effects placed by where they happen on screen, limiter |
| `menus.c` (`mt_arcade_ship_select`) | The arcade ship picker |
| `opentyr.c` | The Setup menus' MetalTyrian settings |

Settings the app and game share (look, light, edges, speeds and so on) are
integers in the shim (`mt_setting` / `mt_set_setting`); the app saves them.

## The shim

`sdl_shim.c` implements just the SDL 2 calls the game makes: surfaces and
palettes, events, timing, audio callbacks, joysticks. When the game presents
a frame, the shim copies the indices and palette, plus whatever the game
side staged for that frame (text, picture, overlays, edges), under one lock.
The app copies the newest frame on its own schedule, so neither side waits
for the other. Input goes the other way: the app pushes keys, mouse motion,
buttons and gamepad state, which the shim turns into SDL events.

## The renderer

`Renderer.swift` turns each new frame to RGBA on the CPU and draws with Metal
(`Shaders.swift`, compiled at launch):

1. **Glow** (HDR screens): bright, saturated pixels blurred at two widths and
   added back, lifting lasers and fire into EDR.
2. **Dynamic light**: the playfield's hot pixels are blurred wide into a light
   map that brightens what is near them, shaped by the art's own shading.
3. **Display** (`display_frag`): sharp pixels or VGA scanlines; the
   high-resolution picture outside the playfield; inside it, edge smoothing,
   the floating overlays' soft shadow and the inset shadow under the HUD;
   the crisp text overlay drawn at three times the game's resolution.
4. **Ambient borders**: a heavily blurred copy spilling past the picture.

### Edge smoothing

`tyrian2.c` marks the end of each drawing layer of a level (background,
islands, ground enemies, sky enemies, shots, the ship, explosions...).
`mt_edge_mark` stamps every pixel a layer changed with the layer's number
and the colour it covered. In the shader, around each screen point the four
nearest game pixels' layers are blended bilinearly; where a layer's share
crosses one half is its smooth outline. Stair-steps become diagonals, the
real colour underneath shows past the outline, and inside a shape nothing
changes.

### High-resolution pictures

`mt_title.c` keeps a copy of every picture as the game loads it and, each
frame, recognises which one is on screen. The app draws `App/Pictures` (and
the title art in `App/Title`) in its place and lays the game's own pixels
that differ from the picture on top, with shaded areas passed as darkening so
the new art shows through.

## Sound

The game fills audio at 49716 Hz (the OPL chip's rate, so music needs no
resampling); effects are resampled once at load. `Audio.swift` plays the
stereo stream through AVAudioEngine, which converts to the device rate.

## Testing without a Mac

`Tests/linux/run.sh GameData/tyrian2000 OUT SECONDS` builds the engine and
shim with gcc and runs the game headless, with a scripted player:

- `MT_SCRIPT="w:Text;k:40;p:5;d"`: wait until text is on screen, press a key
  (scancode), pause, dump the frame.
- `MT_TEXT_LOG`, `MT_TEXT_LEAKS=DIR`: report crisp text and find original
  lettering that shows through.
- `MT_PIC_DUMP=DIR`: dump frames, picture masks and edge layers.
- `MT_EDGES=n`, `MT_WAV=file`, `MT_DUMP_DIR=DIR`: edge smoothing level,
  record the audio, dump raw frames.

Swift and Metal only build on a Mac (`scripts/build-app.sh`).
