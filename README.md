# GRAWBox

Native ports of the Xbox 360 versions of Ghost Recon Advanced Warfighter 1 and 2, made by static
recompilation. Windows first, Android later.

**Early development, no releases.** Both campaigns reach gameplay on Windows; neither has been played
through. See [docs/STATUS.md](docs/STATUS.md).

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

## License

[MIT](LICENSE).
