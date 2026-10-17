# MetalTyrian version 1 plan

Written 2026-10-17 for John's review. Nothing in this plan gets built until he
approves it. Research behind it: `RESEARCH.md`.

## Goal

A native Mac app that plays all of Tyrian 2000, all five episodes, with no
SDL. Swift app shell, Metal renderer, Apple frameworks for input and sound,
OpenTyrian2000's C game code underneath. Signed, notarised, free on GitHub.

## Decided so far

| Area | Decision |
|---|---|
| Game code | OpenTyrian2000 (commit aad5aca) plus two upstream fixes: the extra-ship crash and the data-cube memory corruption. Upstream's refactors are skipped. |
| Game data | Tyrian 2000, bundled in the app. Freeware: "Feel free to play it all you want and share it with friends." |
| Picture | No upscaler. Display modes: sharp pixels and authentic VGA, switchable. HDR glow overlay, off by default, strength 1.2. |
| Network play | Not in version 1. Two players on one Mac still works. |
| UI | The original pixel menus and shop. The native HD UI is version 1.5. |
| Licence | GPLv2 or later, as OpenTyrian. |
| Distribution | Developer ID signed and notarised, on GitHub releases. |

## The approach: replace SDL underneath, leave the game alone

OpenTyrian2000 calls about 70 different SDL functions. Most of the calls go
through a handful of choke points: one function pumps events
(`service_SDL_events`, called from 76 places), one draws the frame
(`scale_and_flip`), `SDL_Delay` and `SDL_GetTicks` handle timing, one audio
callback fills the sound buffer, and `SDL_Surface` is just a block of pixels.

So instead of editing the game, MetalTyrian supplies its own small
implementation of exactly the SDL functions the game uses, backed by Apple
frameworks. The game code compiles against it unchanged, apart from a few
lines at the choke points. This keeps the port small, keeps the game's
behaviour exactly as OpenTyrian2000 has it, and makes future fixes from
OpenTyrian2000 easy to bring in.

```
 Main thread (Swift, AppKit)              Game thread (C, OpenTyrian2000)
 ---------------------------              -------------------------------
 Window, menus, settings                  The game's own loop, unchanged
 Keyboard, mouse, GameController  ----->  Input state, read by service_SDL_events
 Metal renderer                   <-----  320x200 frame + palette from scale_and_flip
                                          SDL_Delay / SDL_GetTicks on Mach time
 Audio thread (AVAudioEngine)     <-----  The game's own audio callback
```

The game keeps its own loop on its own thread, as it expects. The main thread
stays free for the window and Metal. Frames pass through a small triple buffer,
so neither side waits on the other.

## Phases

Each phase ends with the game playable, so problems surface early.

### Phase 0: repository and build

- New repo with OpenTyrian2000 as its base, history kept, so later fork fixes merge normally. The two upstream fixes cherry-picked on top.
- App built with Swift Package Manager plus a script that assembles, signs and notarises the `.app`, the same way MetalPreview builds. No Xcode project to keep in sync, and the same script runs in GitHub Actions.
- Tyrian 2000 data copied into the app's resources at build time.

Done when: an empty MetalTyrian window opens from a one-command build.

### Phase 1: the game running in Metal

- The game loop on its own thread, frames handed to the Metal renderer from MetalPreview: sharp pixels, authentic VGA, HDR glow at 1.2.
- SDL still linked for input and sound in this phase only, so threading and frame pacing can be proved on their own.

Done when: Tyrian 2000 plays in the MetalTyrian window, start to finish of one level, at the right speed. This phase is the main test of the approach.

### Phase 2: input without SDL

- Keyboard and mouse from AppKit, mapped to the scancodes and buttons the game already uses.
- Gamepads through GameController: Xbox, PlayStation and Switch controllers, with sensible default buttons.
- Typing names for high scores and saves.

Done when: the game plays fully on keyboard, mouse and a gamepad, and SDL's input code is gone.

### Phase 3: sound without SDL

- AVAudioEngine calls the game's own mixing callback: the emulated AdLib music and the sound effects.
- Volume, mute when the app is in the background, and no crackle under load.

Done when: music and effects match OpenTyrian2000 and SDL is no longer linked at all.

### Phase 4: a proper Mac app

- Menu bar: display mode, HDR glow on or off, fullscreen, window size.
- Saves and settings in `~/Library/Application Support/MetalTyrian`.
- Sensible window sizing, true fullscreen, Retina, ProMotion pacing.
- About box with credits: Eclipse Software, Jason Emery, the OpenTyrian and OpenTyrian2000 teams, and "unofficial, not affiliated".

Done when: it behaves like a Mac app someone would expect to download.

### Phase 5: release

- Version numbering, signing and notarisation, following `~/Developer` rules: a new version for every distributed build, every step pasteable with its own verdict.
- GitHub Actions build that produces the signed `.dmg` on each tagged release.
- README, licences, credits, and the freeware terms for the game data.

Done when: a signed, notarised `.dmg` is on GitHub and opens on a clean Mac with no warnings.

## Testing

A play-through checklist, kept in `Documentation/`, ticked off before release:

- All five episodes, including the secret levels, Timed Battle and arcade modes
- Two players on one Mac
- Shop, data cubes, saving and loading, high scores
- The Destruct mini-game and the attract demos
- Each display mode and HDR glow, windowed and fullscreen, on the built-in XDR screen and an external SDR screen
- Keyboard only, mouse, and each gamepad type to hand

## Risks

| Risk | Plan |
|---|---|
| Game thread and main thread disagree about timing or shutdown | Proved in phase 1, before input and audio change |
| Audio crackle or drift | Ring buffer between the game's callback and AVAudioEngine; tested on long play sessions |
| A Tyrian 2000 level misbehaves | OpenTyrian2000 is the reference; any difference is checked against it on Linux, where it already builds and runs |
| Later fixes in OpenTyrian2000 | Repo keeps its history, so they merge normally |

## What needs John

- Approving this plan, and the open decisions below.
- Running each build on his Mac and playing it. Builds cannot be compiled from the sandbox.
- Signing and notarisation prompts, and the release itself.

## Open decisions

1. **Repository name and visibility.** Recommendation: `johnrbeckett/metaltyrian`, private until phase 5, then public.
2. **Apple Silicon only, or also Intel.** Recommendation: both (a universal build). It costs one build flag and widens who can play it.
3. **App name.** Recommendation: "MetalTyrian", described as an unofficial Tyrian 2000 port.
