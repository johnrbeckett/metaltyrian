# MetalTyrian

A native Mac version of Tyrian 2000: Swift app, Metal renderer, OpenTyrian2000's
C game code underneath. Public on GitHub from 1.0.0; signed and notarised
releases on the GitHub Releases page.

Read `Documentation/ARCHITECTURE.md` first, then `Documentation/HANDOFF.md`
(development notes) and `CONTRIBUTING.md`.

## Rules for this project

- Talk a piece of work through with John and get his yes before coding it.
- Fun over faithful in looks and comfort; game mechanics stay as they are.
  Remove dead options rather than leave them.
- Change `Engine/` as little as possible, always inside `#ifdef METALTYRIAN`;
  bigger additions go in `Engine/src/mt_*.c`.
- New third-party code, fonts or art: credit it in the README (the About box
  is made from it) and add its licence to `App/Licenses/`.
- Builds go to `~/Library/Caches/MetalTyrian-build/`, never into the repo.
  Swift and Metal only build on a Mac; check engine and shim with
  `Tests/linux/run.sh`.
- `Source/`, `Research/` and `Art/` sit beside the repo as reference material
  and are ignored by git.
