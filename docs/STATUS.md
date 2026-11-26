# Status

Updated 2026-11-25. "WARP" is the software GPU used for automated runs; "tester" is a person on a real GPU.

## Where it stands

- Both campaigns (GRAW 1 0.0.2.1, GRAW 2 0.0.4.1) start directly, without the front end, on ReXGlue SDK 0.11 and reach
  gameplay. Neither has been played through.
- The **native renderer** is the default; **F9** switches to the emulated one. See `docs/RENDERER.md`.
- Tester, at 4K: GRAW 2 runs locked at 60 fps with flat frame times. GRAW 1 looks good, with a few hitches.
- Cross-Com works in both. Shadows match the emulated renderer. GRAW 2's horizontal line and the ghosting are gone.
- Haze redone (`native.haze`), random drift as its motion. Ambient occlusion and sharpening work in both, off by default.
- Keyboard and mouse work in gameplay in both (tester), with key caps in the prompts (GRAW 1: partly).

## Open

- **GRAW 1: flicker and corrupted graphics while paused**, started by switching to another window; stays after returning and
  ends when unpausing (tester). Not reproduced on WARP: pausing, minimizing, restoring and changing focus, windowed and
  fullscreen, stayed clean. Not seen in GRAW 2. It stops while the snipping tool is open, so there is no capture. The
  tester's log shows the paused game drawing steadily at 60 fps, with the game thread's wait for the renderer dropping
  from 13 ms to under 0.3 ms partway through the pause. Suspects: a race between the threads, the post-processing, the
  presents; the `FLICKER-*` test scripts each rule one out.
  Now seen in GRAW 2 too, and it goes away while a capture is taken: timing, not content. GRAW 1's paused frames alternate
  between three kinds (shadow cascades refreshed in turn).
- **Pause flicker, details (tester):** fast, moving, polygonal flashes, mostly bright green, sometimes blue, against the
  sky; starts after a few seconds paused (alt-tab is not needed); native only. Not changed by: GPU serialized, one
  submission per frame, texture capture order off, haze off or original, 720p, general resolves, a vertex copy for
  every draw, one thread, post off, vsync off. Not seen on WARP (paused GRAW 1 training, 5 captured frames).
  A phone video (GRAW 1 training street, paused) shows single-frame vertex explosions from the screen centre: a
  yellow-green beam into the sky, a magenta polygon over the horizon. Not changed by hashing whole shaders (e48f150).
  GRAW 1 issues no LOAD_ALU_CONSTANT there. Parked until it can be tested at full speed on more systems.
- **Locked at 60 fps** in both games, also with the guest vblank at 120 Hz or 1 kHz (`--gpu.vsync=false`). The games sleep in
  1 to 10 ms steps on worker threads; the busiest untimed wait is at 0x82101798 (GRAW 2), about 60 a second. What it waits
  for is not known yet. `REX_TRACE_WAITS=1` (local SDK change, not committed) logs waits by caller. WARP is too slow to measure.
- **GRAW 1: shadow triangles in the air** by some barrels, seen in one run and not again; not on emulated then. Transient.
- **One GRAW 1 squadmate loses weapon, outline and marker** together. Not frame rate or occlusion queries. graw360 #7.
- **GRAW 1 world went mostly black** once, late in the first mission, on the emulated renderer. Cause not known.
- **GRAW 1: dark polygon in the sky** (helicopter scene), on the emulated renderer. Not checked on native.
- GRAW 1 hitches: need longer and repeated play.
- Keyboard and mouse: keys 1 and 2 are d-pad left and right (the tester's choice), 3 and 4 up and down, Q weapons, Tab map.
  Wanted: 1, 2 and G going straight to the primary weapon, secondary and grenades, and one key that cycles all Cross-Com
  assets; the games' lists do not wrap, so both need the games' own state. The mouse does nothing in menus.
  B held and the d-pad scroll the weapons; a tap goes back to the last. There is no sprint.
- GRAW 1 prompts still showing the pad: X, Y, triggers, BACK, in-text icons.
- Native renderer: no occlusion queries (a fixed count is reported); Vulkan not brought over; system dialogs not tested.
- **Field of view and ultrawide** (wanted): the games draw 1280x720 and are pillarboxed on 21:9. The projection is built each
  frame and kept nowhere but the GPU command buffer (GRAW 2, memory dump in gameplay): vertical field of view 0.8 rad in GRAW 2,
  0.75 in GRAW 1 (70 degrees across), 16:9. Scaling it in the renderer would break GRAW 2's screen-space shadows, which rebuild
  positions with the game's matrices, so the camera code has to be found. `<capture dir>\memory.request` dumps guest memory.
- Not tested: drone and squad feeds beyond the first mission, loading GRAW 1 saves, later missions, returning to the front end.

## Findings worth keeping

- GRAW 1 HUD blend jump table: override at 0x821B9164, graw360's labels moved by -0x78. Jump tables checked against the loaded
  images: GRAW 1 388, GRAW 2 440 and 820 in the AI module, 0 wrong.
- GRAW 2 loads `AI_X360_F.xex` as a module with guest path `ai_x360_f.xex`. setjmp/longjmp are named in its manifest, and 53
  functions the scanner missed, from tables in the loaded image; 14 uncertain ones are left out. GRAW 1 has the same
  setjmp pair, not declared; its identified callers are the shader compiler.
- The `.xexp`/`.exep` updates must sit beside their executables, for translation and at run time.
- `app.cache_root` is empty by default, which keeps no shader cache.
- `patches/rexglue-0.11.0-freed-page-resolve.patch` (graw360 issue #1) fixed GRAW 1's HUD noise.
- A driver module must call `rexglue_link_host_allocator`, or the program dies with heap corruption.
- Translate before CMake configures, or the sources are left out. A build caches the SDK's path: `tools/build.ps1` sets it.
- Buttons, GRAW 2 from its control screen: LT aim, RT fire, LB engagement mode, RB camera side and command view, BACK map,
  left stick press stance, right stick press scope, d-pad left/right speaker, up/down orders, A reload and fire mode, B weapons,
  X vision, Y action. GRAW 1: A, B (a tap swaps weapons) and Y the same by a review; the rest not checked.
- The haze pass mixes the picture, by 1 / depth / 1000, with a blurred 256x200 copy and a displaced copy. The displacement is
  worked out on a 32x16 grid with a frequency that grows with time, so that it is noise there.
- GRAW 2's shadow atlas is 1024x1024 `k_16_16` with three mip levels, each a resolve with exponent bias -5. Its shaders' c104 and
  c105 are loaded once per screen tile, predicated; the first tile's are used.
- GRAW 2's HUD clips with a depth mask that only color clears of the same EDRAM tiles reset (`RenderTargets::FillDepths`).
- On WARP the emulated renderer shows a black 3D world; native does not.

## Launcher

- `launcher/` (Rust, egui on Direct3D 12 through wgpu; chosen because this VM has no OpenGL 2): Play, Graphics, Controls,
  Game files. Imports a disc image or title update (recognised by its files; free space checked; the reader is a port of
  `tools/x360_extract.py` and lists the same files for both discs and updates), or uses a folder already extracted. Before
  starting a game it checks the SHA-1 of the executables and updates the port is made from. Writes `grawbox-keys.txt` and
  passes the settings as arguments. Layout: `programs/<game>/` (the port), `games/<game>/{game,user,cache}`, `settings.toml`.
- On WARP: the window, the tabs and Play (GRAW 2 started at 1440p with the launcher's keys). Not yet: importing through the
  window, key capture, a real GPU. How players get the programs is not settled.

## Toolchain and tests

- Portable in the ignored `.tools/`: SDK 0.11.0, LLVM 22.1.8 (22 or newer is required), CMake 3.31.8, Ninja 1.13.1. Also Python
  3.13, Visual Studio 2026 C++ tools, Windows SDK 26100, and Rust for the launcher.
- `tools/build.ps1 -Game graw1|graw2`; `tools/smoke.ps1` runs a build under WARP with a pad script and captures;
  `tools/package_test.ps1` makes a private test folder.
- Scripted presses are lost or late at low frame rates: hold them for seconds. A GRAW 2 save makes runs start in gameplay.
- `GRAWBOX_NATIVE_PROMPT_DUMP=<folder>` writes small textures, to find button prompts.

## Next

The tester's flicker and key results; the launcher on a real GPU.
