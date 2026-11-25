# Decisions

- **2026-11-24, distribution:** follow COGBox. Releases ship prebuilt recompiled programs with no
  game data; players supply their own disc copy and title update. Generated code and game files
  are never committed. (Prebuilt programs do contain translated game code; accepted by the owner.)
- **2026-11-24, license:** MIT. No GPL code is
  copied or linked. GPL projects are not sources for code; check every dependency before use.
- **2026-11-24, testing:** automated checks on a software GPU (WARP) first; a real GPU for
  renderer work; full play-tests by hand on real hardware.
- **2026-11-24, base (preference, not a hard rule):** target the title-updated executables and start
  from ReXGlue SDK 0.11. Change either if evidence shows a better route; record why.
- **2026-11-24, hard rules:** only the end goal and legal compliance on game files are fixed.
- **2026-11-24, no fork:** clean repository, not a fork of graw360-recomp. Copy its MIT parts with
  attribution; leave its GPL directory out; bring over upstream fixes by hand.
- **2026-11-24, priority (guide; code complexity decides):** GRAW 1 campaign on Windows, GRAW 2
  campaign on Windows, both campaigns on Android, then co-op and the rest.
- **2026-11-24, first milestone:** boot the GRAW 1 campaign on SDK 0.11 with the emulated GPU, then
  estimate porting graw360's native renderer; fall back to the older SDK build if too costly.
- **2026-11-24, repository:** public, named GRAWBox, all of `docs/` published. Short one-line commit messages. Local
  commits at milestones; pushes need the owner's approval. `tools/check_commit.py` runs on each commit.
- **2026-11-24, launcher language:** the owner prefers Rust. To be settled when the Windows launcher starts.
- **2026-11-24, issues:** bugs go in GitHub issues. Screenshots showing only the game, with no desktop, are allowed
  there. No images in the repository itself.
- **2026-11-24, feature order:** first get everything working, including visual bugs that are always present (the
  horizontal line, ghosting). Then mouse and keyboard and/or button remapping, with correct in-game button prompts.
- **2026-11-24, native renderer:** its own library with a C interface, built against the SDK version it was written for,
  instead of porting it to 0.11 or moving the games back. The emulated renderer stays, and stays the default until the
  native one has been tested on a real GPU.
- **2026-11-25, branches:** work may go to a branch and be pushed there without asking; `main` still needs the owner's approval.
- **2026-11-25, haze:** kept on by default as the games ask for it, with a setting to turn it off; the owner chooses after seeing it.
