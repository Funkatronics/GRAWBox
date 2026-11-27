// Finds out what the game is doing when a frame takes long with the renderer idle (GitHub #34).
//
// The game thread calls Heartbeat() at each Swap. A sampler thread watches the time since the last
// one; once a frame is over 50 ms old it samples the game thread every 2 ms (suspend, read the
// instruction pointer, resume) and counts where it was: a translated game function (by
// kPPCFuncMappings, the same mapping used to find the camera code) or else the module it is in, such
// as ntdll.dll for a system call or wait. At the next Swap a frame of 100 ms or more gets one log
// line, "GRAW stall: ... ms, sampled: ...", with the most frequent places. At most 20 lines a minute.
#pragma once

namespace grawbox::stall {

// On the game thread, at each Swap.
void Heartbeat();

}  // namespace grawbox::stall
