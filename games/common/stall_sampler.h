// Finds out what the game is doing when a frame takes long with the renderer idle (GitHub #34).
//
// The game thread calls Heartbeat() at each Swap. A sampler thread watches the time since the last
// one; once a frame is over 50 ms old it samples the thread that swaps every 2 ms. While it is
// suspended only its registers and a copy of its stack are read (nothing that takes a lock it might
// hold); after resuming, each sample is counted by where the thread was: a translated game function
// (by kPPCFuncMappings), or else its module with, from a scan of the copied stack for return
// addresses, the first of our modules and the first game function below it (approximate), and
// per such place the next two game functions seen most often (the callers of a wait wrapper). A
// frame of 100 ms or more gets one log line at the next Swap, at most 20 a minute:
//   GRAW stall: frame took 535 ms, 191 samples of the game thread after 50 ms:
//   ntdll.dll < rexbase.dll < sub_82406AD8 100% (via sub_X < sub_Y 85%), ...
#pragma once

namespace grawbox::stall {

// On the game thread, at each Swap.
void Heartbeat();

}  // namespace grawbox::stall
