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
- **Play-test on a real GPU (Release build): the tutorial runs and the 3D world renders.** So the black
  scene was specific to WARP; do not judge 3D on WARP. Performance is poor (emulated GPU, not measured).
- **HUD noise fixed, confirmed on a real GPU.** `patches/rexglue-0.11.0-freed-page-resolve.patch` (graw360
  issue #1: a resolve into freed memory left HUD textures stale) cleared the border, icons and minimap
  box. The video tile where speakers appear also shows correctly. No visual issues seen in the tutorial.
- Runs at 60 fps in the tutorial (the console ran at 30). Sharp frame time spikes without a shader cache:
  `app.cache_root` is empty by default, which keeps none. With it set, the tester reports much smoother play (not measured).
- graw360 reports game logic problems above 30 fps. The weapon problem is not one of them (see below).
- The tactical map works in the tutorial: map, legend and movement.
### GRAW 1 open issues (real GPU, emulated renderer)

- **World went mostly black** once, near the end of the first mission, in combat, depending on view direction. Seen at
  30 Hz after about 34 minutes in one session, occlusion queries on. Not seen in a short fresh run at 30 Hz with
  `--gpu.xenos.occlusion_query_enable=false`, nor at 60 fps with queries on at 2x and 3x resolution (same spot,
  different actions). Cause not known; occlusion queries are one suspect. Nothing in the log at that point.
  Reading the SDK 0.11 code: each finished query ends the submission and waits for the GPU, and a single
  active query is dropped when a submission ends or an address does not match. Canary's replacement
  (`fbd620c22`, `3d233a5b2`, `692cd59cf`) is asynchronous, but is thousands of lines in D3D12, Vulkan and the shader translators.
- **Frame pacing is poor at the 30 Hz setting** (unstable, stutters, spikes through the whole first mission). 60 fps at 2x
  runs much better, and 60 fps at 3x (4K) runs well, both with queries on. Tester reports; not measured.
- **One squadmate loses weapon, outline and marker** together. Seen at 60 Hz and at 30 Hz, so it is not
  frame rate, and with occlusion queries off. Squad orders still work. graw360 #7, open there. Also
  appeared at 3x (4K), 60 fps while the game was left idle, with no explosion. Needs more testing. Cause not known.
- **Ghosting around some geometry**, most visible in motion; noticed on a palm tree against the sky in an outdoor
  mission. Seen at 3x (4K), 60 fps, fullscreen, and at 1x. Also clear as
  a soft halo around the player's head and shoulders against the sky. Cause not known.
- **Dark polygon in the sky** (helicopter scene): a large straight-edged darker area that moves with the view.
  Looks like part of the sky clipped away. Untested idea: the missing `force_depth_clamp` setting, which Xenia Canary's
  tracker marks as required for this game. graw360 #32 is similar. Minor.
- **Stutter in new areas** with the shader cache on. Whether it is shader building is not confirmed.
## GRAW 2 campaign

- Disc (1,811 files) and title update extracted; update blocks hash-checked.
- `Yeti_Final_Xenon.xex` with the update (0.0.4.1) translates without errors and compiles. Its first
  boot crashed: it loads `AI_X360_F.xex` as a separate module (`Run(885879A8)` is that module's entry).
- **Boots to its Single player menu under WARP** (one 60 s run, no crash) after two fixes:
  `AI_X360_F.xex` as a `[[modules]]` entry with guest path `ai_x360_f.xex`, and setjmp/longjmp named:
  main program 0x827927A0 / 0x82792550, AI module 0x8858FFD0 / 0x88587620. Both `.xexp` files must sit
  beside their executables. Started directly, without the front end. No input sent, no mission tried.
- An "invalid" fetch constant repeats in the log, as in GRAW 1 (a different constant).
- Jump tables checked against the loaded updated image: 440 in the campaign and 820 in the AI module, 0 wrong.
- GRAW 1 has the same setjmp pair (`sub_825AFBD0`, `sub_825AFAB0`), not declared; its identified callers are the shader compiler.
- First real GPU run crashed seconds after start on a missed function (`0x82110F28`). The manifest now lists 53
  missed functions, from tables in the loaded image. Two candidates split functions and were dropped; 14 uncertain ones
  left out. Under WARP it now goes from New Campaign to the mission loading screen, no crash in 120 s; the
  mission itself had not loaded by then.
- Runs on a real GPU with the fix (tester report). **Ghosting** looks the same as in GRAW 1 and is more noticeable.
  Tester observation: it seems worse where there is more smoke or dust, and pulses in time with the smoke moving
  across the screen. After more play the tester is certain it comes from the haze effect, which seems to be always
  present, so no haze-free area has been checked. Ghosting and blur increase with distance. Either the haze effect or
  something downstream of it. Code cause not known.
  Other visual issues seen, details not recorded yet.
- **Thin horizontal line** across the screen, roughly 30% of the height from the bottom, over the character and terrain
  but not the sky. Not black: the tester describes a row of pixels where shadows seem not to be drawn, with its
  color following what is behind it. In one capture it sits at row 512 of 720, which would be a boundary between
  256-row tiles. Seen at 2x and 3x, gone at 1x, so it comes with resolution scaling;
  a tile seam is the likely cause. No change with `resolve_resolution_scale_fill_half_pixel_offset=false` or the ROV path;
  `half_pixel_offset=false` makes it at least twice as thick. Canary's viewport fix (`6579cc2a4`) did not help and was undone.
- `sub_823A56C0` is about 1 MB of generated code. Cause not checked.
- Tracked in `games/graw2-campaign/`; `tools/build.ps1 -Game graw2` builds it on the patched SDK.

## Native renderer

See `docs/RENDERER.md`. Everything below is from WARP only; nothing has run on a real GPU yet.

- **GRAW 1**: menu at 1x, 2x and 3x, and the first mission at 1x, draw on it: world, shadows, characters and HUD. The
  emulated renderer gives a black world on WARP, so WARP can now be used to look at 3D.
- **GRAW 2**: menu and the opening of the first mission at 1x and 2x: world, characters, video panel. No horizontal
  line at row 512 in the 2x captures (the emulated renderer's bug). Its HUD clips itself with a depth mask that only color
  clears of the same EDRAM tiles reset: done in `RenderTargets::FillDepths`.
- **Ghosting explained**: the last pass of both games (`CFC1BD3557C8B735`, `F17045F9B4E36E54`) mixes the picture, by
  1 / depth / 1000, with a blurred 256x200 copy and a copy displaced by sines of time (about 1.3 pixels at 720p). Same
  constants in both games, so it is the games' own haze, drawn as they ask. `native.haze=off` removes it: in the same GRAW 2
  frame the distant towers and signs are then sharp. Whether the console looked the same was not compared.
- **F9** switches both ways in the GRAW 1 menu on WARP.
- **Cross-Com may be broken on it in GRAW 1** (tester report, watching a WARP run). Not settled: at seconds per frame the
  emulated renderer's panel is empty too, the clip running in real time, and one native run did draw a 160x120 video image.
- GRAW 1's mission also draws at 2x; about 17 minutes of scripted play (cover, shooting, decals, HUD) without an error.
  Resize, minimize, restore and close work in both games.
- **Keyboard and mouse**: `input/keyboard_driver.cpp` (`--hid.drivers=keyboard,sdl3 --hid.shared_pads=true`), bindings in
  `grawbox-keys.txt`. Keys tested in the GRAW 1 menu with synthetic input; the mouse and gameplay are untested.
- **Button prompts** show the bound keys on the native renderer (`native.prompts`): GRAW 2's font and HUD atlas, and GRAW 1's
  menu A and B and HUD stick icons. GRAW 1's other buttons (X, Y, triggers, BACK, in-text icons) are still the pad's:
  `GRAWBOX_NATIVE_PROMPT_DUMP=<folder>` writes small textures to find them.
- The GRAW 1 build had cached the stock SDK's path, so it was not using the patched SDK; `tools/build.ps1` now sets it.

### First real GPU test of the native renderer (tester reports)

- F9 works in both games. Cross-Com works in both. The GRAW 2 horizontal line is gone. GRAW 1 looks good.
- **GRAW 2: no shadows** from trees, walls or buildings on native. Cause: its variance shadow map resolves `k_16_16` with
  exponent bias -5 to unsigned texels; the resolve stored signed bits without the format's factor of 32. Fixed in
  `resolve.cpp`; then the world's shaders read the
  shadow mask at the wrong place: the game loads c104/c105 once per screen tile (predicated), and the last tile's was kept. The
  tracker now takes the first tile's. Tree, building and prop shadows look right on WARP at 1x and 2x; not yet seen on a real GPU.
- **Textures less sharp** than emulated, mostly at angles and distance, both games. `native.anisotropy` now defaults to 16
  (was 1); not compared yet. Wanted: filtering levels as a player setting.
- **GRAW 1 stutters a lot**, seemingly on both renderers. Needs longer play.
- **Haze**: with it off the ghosting is gone but the world looks too flat and clean; `soft` looked no different (removed).

### Second real GPU test, GRAW 2 only (tester reports)

- Textures look much closer; emulated still a hair sharper. Cause not known. `native.sharpness` and `native.mip_bias` are new.
- **Shadows** present and mostly correct, but with more jagged edges and very unstable against emulated. Cause: the shadow
  atlas (1024x1024, three mip levels, each a resolve) was sampled from its 1x copies at any scale. Now at the draw
  resolution, confirmed in the log at 2x; not yet seen on a real GPU.
- **Haze** redone as `native.haze=fixed`, the new default: see `docs/RENDERER.md`. On WARP, GRAW 2 mission at 1x and 2x: distance
  haze without the double image, no halo around the player. Not yet seen on a real GPU.
- **Ambient occlusion** (`native.ambient_occlusion`, off by default) works on WARP in GRAW 2. Cost on a real GPU not measured.

### Third real GPU test, both games (tester reports)

- GRAW 2: locked 60 fps, flat frame times; shadows match emulated.
- First `fixed` haze: no doubling and good nearby, but jagged and wavy edges at a distance, a clock-like pulse, and white edges
  against a bright sky. Cause of the waves and pulse: the games work the displacement out on a 32x16 grid, with a frequency
  that grows with mission time, so it is noise there; it was done per pixel. Redone on the grid, as a smear instead of a second
  copy. Edges: the haze weight is now filtered over neighbors, and the sky no longer weighs more than what is in front.
- Ambient occlusion liked, but vertical stripes on walls (half resolution samples fell on pixel borders). Fixed.
- All checked on WARP at 2x in GRAW 2 (same view as the report, no stripes); pulsing not judged in motion. GRAW 1 not rerun.
- A GRAW 2 save makes scripted runs start in the playable part (Load Campaign).

### Fourth real GPU test (tester reports)

- Ambient occlusion and sharpening look great, no issues seen, both games. GRAW 2 performance solid. GRAW 1 looked good in the
  previous pass, with a few hitches; needs longer and repeated play, not a priority.
- GRAW 2 haze: better than the original in every way, playable. Left: still too rhythmic, a fast frequency stands out; one
  tree in the middle distance still doubled a little. `native.haze_tuning=true` makes F8 cycle six variants (slower, and noise
  without a rhythm) to choose from; the smear is tapered for the doubling.
- **GRAW 1: wrong shadow by some barrels**: part of a barrel's shadow is drawn in the air as small translucent black
  triangles. Cause not known. Not compared with the emulated renderer.
- **GRAW 1: flickering while paused and in the background** (window not focused). Cause not known. Low priority.

## Next

GRAW 2: details of the visual issues. GRAW 1:

the squadmate problem, occlusion queries, squad and drone Cross-Com feeds, loading a save, later missions.

## Not verified

- Other projects' README claims, including Cross-Com in graw360.
- GRAW 2 program roles other than the campaign and its AI module. What the updates change.
- Saves, and returning from the campaign to the front end.