// Finds out what the game is doing when a frame takes long with the renderer idle (GitHub #34).
//
// The game thread calls Heartbeat() at each Swap. A sampler thread watches the time since the last
// one; once a frame is over 50 ms old it samples the thread that swaps every 2 ms. While it is
// suspended only its registers and a copy of its stack are read (nothing that takes a lock it might
// hold); after resuming, each sample is counted by where the thread was: a translated game function
// (by kPPCFuncMappings), or else its module with, from a scan of the copied stack for return
// addresses, the first of our modules and up to three game functions below it (approximate). A frame
// of 100 ms or more gets one log line at the next Swap, at most 20 a minute:
//   GRAW stall: frame took 412 ms, 132 samples of the game thread after 50 ms:
//   ntdll.dll < grawbox_native.dll < sub_82113AB8 99%, ...
#pragma once

namespace grawbox::stall {

// On the game thread, at each Swap.
void Heartbeat();

}  // namespace grawbox::stall
