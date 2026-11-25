# Native renderer

Draws the games' Direct3D calls with Direct3D 12, as a library of its own (`grawbox_native.dll`) behind
the C interface in `include/grawbox_native.h`. `games/common/native_host.cpp` loads it and the games'
hooks report the calls.

`src/` is graw360-recomp's renderer (MIT), which ports pgr4-native's (MIT).
Drawing above 1x comes from [F.E.A.R. 360](https://github.com/Juansolo457/fear360-recomp)'s copy of it (MIT): the
`fear` remarks in comments are about that game. Changed here: the title's
addresses and device layout come from the host, and `host.cpp`, `calls.cpp` and `exports.cpp` replace what
it used of the SDK. It is written against ReXGlue SDK `bd833a2`, so that version's Xenos headers and DXBC
shader translator (BSD-3-Clause, from Xenia) are compiled in; `compat/` stands in for the rest of that SDK.
`fetch.ps1` gets them and applies `patches/` (from graw360-recomp and pgr4-native). Notices: `LICENSE.*.txt`, contact addresses left out.

Any of the renderer's options can be set with `GRAWBOX_<NAME>` in the environment, without the `graw_`
prefix, such as `GRAWBOX_NATIVE_RENDER_THREAD=false`.
