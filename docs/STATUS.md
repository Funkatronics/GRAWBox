# Status

Updated 2026-11-24.

- **The updated GRAW 1 campaign (0.0.2.1) boots to its Campaign menu** on SDK 0.11 under WARP
  (emulated GPU, 1280x720): background, text and logo draw. Started directly, without the front end
  (`title.entry=Yeti_Final_Xenon.exe`). One run of 85 s: no crash, then stopped by the test. Only a
  refused `D:\Dummy` write appeared in the log. Sound plays in the menu.
- The prebuilt SDK package is enough to build; no SDK source build was needed so far.
- `tools/x360_extract.py` reads disc images and title updates (update blocks hash-checked).
- Codegen: no manifest hints needed, no validation errors, 178 files. The `.exep` must sit beside
  the executable for codegen and at run time.

## Toolchain

- Portable in the ignored `.tools/`: SDK 0.11.0, LLVM 22.1.8, CMake 3.31.8, Ninja 1.13.1.
  Also needed: Python 3.13, Visual Studio 2026 C++ tools, Windows SDK 26100.
- Clang 22 or newer is required by this Visual Studio's C++ library. 22.1.8 builds the game with no
  override and it boots to the menu.

## Known work

- HUD blend jump table (graw360: cause of the black Cross-Com): override added at 0x821B9164, graw360's
  labels moved by -0x78. `check_switch_tables.py` against the loaded image: 388 switches, 0 wrong.
  Cross-Com itself not seen yet.
- **The first mission (tutorial) loads under WARP** from New game: loading screen, HUD, HUD border, text
  and the narration video panel draw correctly, and `GRAW.sav` is written. **The 3D scene is black**,
  cause not known: `allow_invalid_fetch_constants` changed nothing, and the
  ROV path gave a fully black window. Frame rate about 1.4 fps by log timing. One fetch constant is reported "invalid".
- `tools/autopilot/` is a scripted controller driver; `tools/smoke.ps1` runs it under WARP with
  captures. Some presses land late or are lost under WARP. The campaign menu does not wrap, so five
  held Up presses reach New game. Use `--kernel.headless=true` to answer system dialogs.
- A driver module must call `rexglue_link_host_allocator`, or the program dies with heap corruption.
- `games/graw1-campaign/` and `tools/build.ps1` are the tracked GRAW 1 campaign project: they build and
  boot to the menu. Translate before CMake configures, or the sources are left out.
- A private test folder (Release build, launch script, checklist) is ready for a play-test on a real
  GPU. Results pending.

## GRAW 2 campaign

- Disc (1,811 files) and title update extracted; update blocks hash-checked.
- `Yeti_Final_Xenon.xex` with the update (0.0.4.1) translates without errors and compiles. Its first
  boot crashed: it loads `AI_X360_F.xex` as a separate module (`Run(885879A8)` is that module's entry).
- **Boots to its Single player menu under WARP** (one 60 s run, no crash) after two fixes:
  `AI_X360_F.xex` as a `[[modules]]` entry with guest path `ai_x360_f.xex`, and setjmp/longjmp named:
  main program 0x827927A0 / 0x82792550, AI module 0x8858FFD0 / 0x88587620. Both `.xexp` files must sit
  beside their executables. Started directly, without the front end. No input sent, no mission tried.
- An "invalid" fetch constant repeats in the log, as in GRAW 1 (a different constant).
- Jump tables not audited yet. GRAW 1 also has a setjmp-style function (`sub_825AFBD0`), not named yet.
- `sub_823A56C0` is about 1 MB of generated code. Cause not checked.
- Its project is still only in `private/`.

## Next

GRAW 2: tracked project, jump table audit, start a mission. GRAW 1:

Find out why the 3D scene is black, on a real GPU. Then check Cross-Com and the tactical map.

## Not verified

- Other projects' README claims, including Cross-Com in graw360.
- GRAW 2 program roles other than the campaign and its AI module. What the updates change.
- Saves, and returning from the campaign to the front end.