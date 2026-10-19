# Working on MetalTyrian

Issues and pull requests are welcome. A few ground rules keep the port easy to
follow and easy to update from upstream.

- **Engine changes stay marked.** Anything changed in `Engine/` goes inside
  `#ifdef METALTYRIAN`, and larger pieces go in `mt_*.c` files of their own.
  Fixes that belong upstream are better sent to
  [OpenTyrian2000](https://github.com/KScl/opentyrian2000) too.
- **Keep the game's feel.** Looks and comfort can change; game mechanics
  should not, unless it is a setting that defaults to the original.
- **Check headless.** Run the Linux host (`Tests/linux/run.sh`, see
  `Documentation/ARCHITECTURE.md`) for engine and shim changes; build the app
  with `scripts/build-app.sh` on a Mac for anything in Swift or Metal.
- **Credit and licence.** New third-party code, fonts or art need a line in the
  README's Credits (which also feeds the About box) and their licence in
  `App/Licenses/`. Contributions are under the GPL version 2 or later.

Updating the engine from upstream:

```
git subtree pull --prefix=Engine https://github.com/KScl/opentyrian2000.git master
```

Cutting a release (maintainer, on the Mac with the Developer ID):

1. Update `CHANGELOG.md` and the README.
2. `scripts/tag-version.sh X.Y.Z`: writes `VERSION`, commits, tags `vX.Y.Z`.
3. `scripts/release.sh` (or `--check` for a dry run): signed build with the
   hardened runtime, checks, notarisation, stapling, and a zip verified the
   way a visitor gets it, in `~/Developer/Outputs/MetalTyrian-X.Y.Z.zip`.
4. Push `main` with tags and attach the zip to the GitHub release
   (`gh release create`, as the script prints). Attach it a second time
   named plain `MetalTyrian.zip`: the website's Download button links to
   `releases/latest/download/MetalTyrian.zip`.
