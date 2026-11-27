// Playtest reports (GitHub #31): F10 in a game writes a folder with what is needed to look into a
// problem, next to the log: <log folder>\reports\<date-time>\
//   log-tail.txt      the end of the log (2 MB)
//   frames-tail.csv   the end of the per-frame stats, when GRAWBOX_NATIVE_CSV is set
//   screenshot.png    the next presented frame (native renderer)
//   info.txt          build, game, Windows, GPU and driver, CPU, memory, the program's arguments
//   grawbox-keys*.txt the key files beside the program
// The user's profile folder, user name and computer name are replaced in every text file. The
// launcher's "Report a problem" adds settings and saves and packs it into one zip. A beep confirms.
#pragma once

#include <string>

namespace grawbox::report {

// Writes a report now (on the game thread, at a Swap). `screenshot`, if set, asks the renderer to
// write the next frame to the given path (UTF-8).
void Write(void (*screenshot)(const char* path));

// Replaces the user's profile folder, user name and computer name in `text`.
std::string Scrub(std::string text);

}  // namespace grawbox::report
