# GRAWBox

Native ports of the Xbox 360 versions of Ghost Recon Advanced Warfighter 1 and 2, made by static
recompilation. Windows first, Android later.

**Early development.** Both campaigns reach gameplay on Windows; neither has been played through.
Known issues are in [docs/STATUS.md](docs/STATUS.md).

## Playing

1. Download `GRAWBox-vX.Y.Z-windows-x64.zip` from [Releases](../../releases) and unzip it anywhere.
2. Run `GRAWBox.exe`.
3. In **Game files**, drop your disc image (.iso) of each game, then its title update.
4. Play from **Campaign**.

Needs 64-bit Windows and a graphics card with Direct3D 12.

Something wrong? Press **F10** in the game right when it happens: it saves a report (the end of the
log, a screenshot, your PC's Windows and graphics versions; your user name and folders are removed).
Then **Report a problem** on the game's card in the launcher packs it with your settings and saves
and opens a new GitHub issue, where you drag in the zip.

## No game files

This repository contains no game executables, assets, title updates, artwork, keys or translated
game code. You need your own legally obtained copy of each game and its title update.

Unofficial fan project, not affiliated with or endorsed by Ubisoft, Red Storm Entertainment or
Microsoft. Trademarks belong to their owners.

## Credits

- [pgr4-native](https://github.com/shaunsingh/pgr4-native) (MIT): the native Direct3D 12 renderer in
  `native/`, through graw360-recomp's port of it to GRAW.
- [F.E.A.R. 360](https://github.com/Juansolo457/fear360-recomp) (MIT): drawing above 1x in that renderer.
- [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (BSD-3-Clause): the recompiler and runtime
  the ports are built with. It builds on [Xenia](https://xenia.jp) (BSD-3-Clause).
- [graw360-recomp](https://github.com/Juansolo457/graw360-recomp) (MIT, juansolo457 and
  contributors): its port of the native renderer and the GRAW Direct3D
  research behind it, and the cause of, and labels for, the GRAW 1 HUD blend jump table fix.
- [COGBox](https://github.com/Shagg88/COGBox) (MIT, Shagg): how compiled shaders are laid out in
  newer Xbox 360 shader containers, and the approach of preparing shaders from recorded recipes and
  the player's own game files.
- [Rajdhani](https://github.com/google/fonts/tree/main/ofl/rajdhani) (SIL Open Font License 1.1, Indian Type Foundry): the
  launcher's font, downloaded at build time.

## License

[MIT](LICENSE).
