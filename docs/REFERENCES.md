# References

Researched 2026-11-24 by reading docs and source. Nothing has been built or run here yet.
"Claims" means the project's own docs say so and we have not reproduced it.

## The games

| | GRAW 1 | GRAW 2 |
|---|---|---|
| Title ID | 555307D5 | 555307F0 |
| Front end | `default.xex` | `default.xex` |
| Campaign | `Yeti_Final_Xenon.exe` + `Yeti.big` | `Yeti_Final_Xenon.xex` + `Yeti.big` (role inferred from GRAW 1) |
| Multiplayer/co-op | `GR3Xenon_Release.exe` | `GRAW2Xbox 360_Release.xex` (role inferred) |
| Other programs | none | `AI_X360_F.xex` (AI module the campaign loads), `Xbox360AuthenticationSystem.xex` (role unknown) |
| Title update patches | all three (`default.xexp`, `GR3Xenon_Release.exep`, `Yeti_Final_Xenon.exep`) | all five (`.xexp`) |
| Xenia Canary tracker | #464: playable, finished by testers; drawing artifacts | #465: playable, finished by testers; artifacts, random crashes |

- Each game is several executables that start each other with `XamLoaderLaunchTitle`.
- The GRAW 1 campaign is Ubisoft Paris's Yeti engine; its front end and multiplayer are Red Storm's
  engine (graw360 findings). Co-op is therefore a separate program from the campaign.
- Xenia Canary is the visual comparison reference.

## Toolchain: ReXGlue SDK (BSD-3-Clause, includes Xenia code)

- v0.11.0 (2026-11-14) is 338 commits after `bd833a2` (0.10 nightly, 2026-10-01), which COGBox,
  graw360 and pgr4-native pin. The source layout was reorganised (about 2,500 files changed), so
  their SDK patches will not apply as they are.
- Has D3D12 and Vulkan GPU backends. Platforms: Windows, Linux, macOS. No Android platform upstream.
- Loads `<module path>p` as the title update patch, which covers both `.xexp` and `.exep`
  (`src/kernel/user_module.cpp`).
- `XamLoaderLaunchTitle` only stores `launch_path`; nothing in 0.11 `src` or `include` reads it, so we
  must implement the hand-off between programs.
- The jump-table bug graw360 worked around (tables on a 64 KB boundary) still has the same branch in
  0.11 (`src/codegen/function_scanner.cpp`). `vpkd3d128` was rewritten; aliasing fix not tested.

## Primary references

**graw360-recomp** (GRAW 1 only; MIT, plus GPL in `port-yeti-nx/`, BSD in SDK patches; 284 files)
- Claims the campaign plays (v0.6): tutorial and first missions, native D3D12 renderer ported from
  pgr4-native, 120 fps, Vulkan on Linux. Not played start to finish.
- Native renderer draws at 1280x720. Windows users build from disc on the command line.
- Ignores title updates. All its addresses, hooks and function lists are for the unpatched disc.
- Open campaign bugs include: #7 allies' weapons vanish, #19 squad member vanishes and blocks the
  rally point, #17/#18 logic and AI depend on frame rate (built for about 30 fps), #52 missing truck,
  #51 crash after mission 3, #21 shader stutter, #5/#6/#32 lighting, textures and sky.
- Best assets: FINDINGS (executables, function lists, jump tables), the D3D function map in
  NATIVE_RENDERER.md, root causes in CROSSCOM.md, TACMAP.md and BORDER.md, the hand-off code
  (`graw_loader`, `graw_process`), autopilot, `check_switch_tables.py`, `find_missed_functions.py`,
  resolve comparison tools.

**COGBox** (Gears 1-3 and Judgment; MIT, third-party notices per folder)
- UX reference. C++/Dear ImGui/SDL3 launcher, about 24,000 lines, much of it Gears-specific (online,
  mods, HDR). Built with MinGW on Linux.
- Reusable ideas: disc/STFS reader (`x360fs.cpp`), import (`install.cpp`), XEX fingerprinting
  (`games.*`), required title updates, per-game folders, problem reports.
- Own renderer (about 49,000 lines) and roughly 100 SDK patches.
- Ships prebuilt recompiled executables with no game data.

## Other references

| Repo | License | Use |
|---|---|---|
| pgr4-native | MIT | Origin of graw360's renderer. D3D12 only; 0 Vulkan files in `src/` |
| fh2-recompiled | GPL-3.0 | Android ReXGlue SDK fork (0.10), launcher, driver loading; claims low fps on phones with the emulated GPU |
| pgr4-android | MIT (builds on fh2's GPL SDK) | Claims 37-39 fps at 720p, Snapdragon 8 Gen 3 after heavy patching; good tiling and Vulkan notes |
| NFSMW-Recompiled | GPL-3.0 | Native Vulkan 1.1 renderer for PC and Android |
| Skate3-Mobile | no root license file; check before copying | ISO import, touch overlay, in-game menu ideas |
| fear360-recomp | MIT + GPL parts | Same author as graw360; settings inside the game's menu, two-game launcher |
| bioshock1-native-recomp | BSD-3 | Claims Windows and Android on SDK 0.11, GLES renderer |
| Black-Android, SCDA-XBERecomp-android | MIT | Original Xbox, not 360. Touch, gyro, options UI. SCDA compiles game code on the device, so its APK has no game code |
| uncharted-android | GPL-2.0-or-later | **Multi-title Android launcher reference** (ideas only): one APK, three games, see below |
| OpenRAC | launcher is GPL-3.0-or-later | **Launcher look reference** (ideas only): multi-title Tauri/Svelte app for desktop and Android. Disc launch animation: `launcher/src/components/DiscLaunch.svelte`, `launcher/src/lib/insertion.ts`. Engine not relevant (PS2) |
| Precursor | ISC | **Launcher look reference**, code reusable with notice: Android multi-title launcher in plain Java (`scripts/android-port/apk`). Engine not relevant (PS2) |
| fear-nx, nfsmw-nx | GPL-3.0 | Switch ports; background reading only |
| Xenia Canary | BSD-3 | Behaviour and visual reference; renderer fixes may be ported with its notice |


This project is permissive (DECISIONS.md): do not copy code from the GPL projects or GPL parts above.
pgr4-android's own code is MIT, but its SDK patches apply to fh2's GPL tree, so treat them as GPL.

## Multi-title Android pattern (uncharted-android, read in source)

- One APK. A `Launcher` activity holds one table of games (id, executable, subfolder) that drives
  checking, preparing, settings and launch arguments.
- The player picks one folder holding all three games. Missing files, untested versions and an
  update folder next to it are reported in plain words; `Play` stays disabled until ready.
- The launcher writes per-game argument and environment files, then starts `GameActivity` (SDL).
- Switching programs: a `Restart` activity in its own process waits for the old game process to die,
  then starts the game activity again. A possible model for GRAW's several executables per game.
- Also: GPU driver picker, controller setup over the game, icon resumes a running game, updater.
- Differs from our needs: one collection rather than separate games, no ISO import, and it needs
  all-files access. Plain native Android views.

## Renderer facts checked in source

- Canary has `force_depth_clamp` (tracker marks GRAW 1 as needing it). SDK 0.11 has no such
  setting in its D3D12 or Vulkan backend; likely a small port.
- Testers of GRAW 1 fixed flicker in older Canary builds with the fake occlusion lower threshold at
  -1. SDK 0.11 has `query_occlusion_fake_sample_count` instead.
