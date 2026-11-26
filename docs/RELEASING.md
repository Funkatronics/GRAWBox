# Releasing

Releases are built on the developer's machine with `tools/release.ps1` and uploaded to GitHub with `gh`. CI cannot
make them: the programs are built from the owner's game files, which never leave that machine. CI
(`.github/workflows/launcher.yml`) only builds and tests the launcher.

## What a release holds

`GRAWBox-vX.Y.Z-windows-x64.zip` and its `.sha256`, a portable folder:

```
GRAWBox/
  GRAWBox.exe                      the launcher
  programs/graw1/, programs/graw2/ the built game programs, the native renderer, the ReXGlue runtime,
                                   the Visual C++ runtime, the default key files
  LICENSE, README.md, OFL-Rajdhani.txt
```

Never game files: no executables or title updates of the games, no archives, no disc images, no keys. The script
refuses to zip anything else (step 4). The ReXGlue runtime DLLs come from `.tools/rexglue-grawbox/bin`, never from
the build folders, so a library copied there by hand for a test does not ship.

Asset names keep this pattern so update tools (Obtainium) can match them: `GRAWBox-v<version>-windows-x64.zip`.

## Before

1. `prod` is up to date with `main` through a pull request, it is checked out, and the tree is clean and pushed
   (the script checks the last two).
2. The owner's files are extracted to `private/graw1/` and `private/graw2/` (`disc/`, `update/`), as for
   `tools/build.ps1`.
3. `gh auth status` shows a login that can create releases on the repository.
4. Release notes in `private/release/notes-vX.Y.Z.md`: what changed since the last release, known issues
   (docs/STATUS.md), and how to start (unzip, run GRAWBox.exe, add your own disc image and title update).

## Build, check, publish

```powershell
# Build and zip only: private/release/vX.Y.Z/
tools/release.ps1 -Version X.Y.Z -SdkSource <rexglue-sdk clone>
# Smoke test the zip: unzip into a new folder, run GRAWBox.exe, set up both games, start each.
# Then tag, push the tag and upload (the build is reused):
tools/release.ps1 -Version X.Y.Z -SkipBuild -Publish -Notes private/release/notes-vX.Y.Z.md
```

`-Prerelease` marks it a pre-release; Obtainium skips those unless told otherwise, so normal releases leave it off.

The `<rexglue-sdk clone>` is ReXGlue v0.11.0 with `patches/` applied; the native renderer builds against its
`thirdparty/`. The programs link against `.tools/rexglue-grawbox`, the same source installed.

## Versions

`MAJOR.MINOR.PATCH`, tags `vX.Y.Z`. 0.x while the campaigns are not fully playable. PATCH for fixes, MINOR for
features. A tag is never moved: a broken release gets a new version.

## After

Add the release to docs/STATUS.md (version, date, commit).
