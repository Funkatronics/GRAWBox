// What the host's hooks of the guest D3D report. The dispatch follows graw360-recomp's
// graw_d3d_hooks.cpp, after pgr4-native's (MIT): the original always runs first, the push-buffer
// tracker sees every call's entry and exit, and the outermost draw, clear, resolve or Swap reaches
// the renderer once the original returned.
#include "d3d_state.h"
#include "hook_args.h"
#include "renderer.h"

#include <rex/logging.h>

namespace {

namespace nat = graw::native;
using nat::HookFn;
using nat::TilingCall;

thread_local uint32_t t_native_depth = 0;  // active draw/clear/resolve/Swap calls
thread_local bool t_pending = false;       // a BeginVertices draw waits for EndVertices
thread_local nat::HookArgs t_pending_args;
thread_local TilingCall t_tiling = TilingCall::kNone;
// The push-buffer functions nest: only the outermost decodes at its entry. What lies between that
// and the inner one's cursor is the submit's own bookkeeping, not packets.
thread_local uint32_t t_jump_depth = 0;

nat::HookArgs EntryArgs(const GrawboxCall& c) {
  nat::HookArgs a;
  a.fn = HookFn(c.function);
  a.device = c.r[0];
  for (int i = 0; i < 8; ++i) a.r[i] = c.r[i];
  for (int i = 0; i < 4; ++i) a.f[i] = c.f[i];
  a.sp = c.sp;
  a.ret = 0;
  a.tiling = t_tiling;
  return a;
}

// A draw BeginVertices reserved that never reached EndVertices: its packets were not published.
void DropPending() {
  if (!t_pending) return;
  t_pending = false;
  static uint32_t dropped = 0;
  if (++dropped <= 8)
    REXLOG_WARN("GRAW native: BeginVertices draw at {:08X} without EndVertices; dropped",
                t_pending_args.device);
}

uint64_t Tracked(uint8_t* base, const GrawboxCall& c, bool outer, bool draw) {
  nat::GuestStateTracker& gs = nat::GuestState();
  gs.Sync(base, c.r[0], c.function, outer);
  if (draw) gs.BeginDrawCall();
  const uint64_t ret = c.original(c.context);
  gs.Sync(base, c.r[0], c.function);
  gs.AfterCall(base, c.r[0]);
  return ret;
}

}  // namespace

GRAWBOX_NATIVE_API bool grawbox_native_drawing() { return nat::NativeRenderer::Live() != nullptr; }

GRAWBOX_NATIVE_API void grawbox_native_call(uint8_t* base, const GrawboxCall* call) {
  namespace fn = nat::fn;
  const GrawboxCall& c = *call;
  nat::NativeRenderer* const r = nat::NativeRenderer::Live();
  if (!r) {
    t_pending = false;
    c.original(c.context);
    if (c.function == fn::kSwap)
      if (nat::NativeRenderer* suspended = nat::NativeRenderer::Get()) suspended->OnSwapSuspended();
    return;
  }
  switch (c.function) {
    case fn::kEndVertices:
      Tracked(base, c, t_native_depth == 0, true);
      if (t_pending && t_native_depth == 0) {
        t_pending = false;
        if (t_pending_args.device == c.r[0]) r->OnDraw(base, t_pending_args);
      }
      return;
    case fn::kBeginTiling:
    case fn::kEndTiling: {
      const TilingCall outer = t_tiling;
      t_tiling = c.function == fn::kBeginTiling ? TilingCall::kBegin : TilingCall::kEnd;
      Tracked(base, c, t_native_depth == 0, false);
      t_tiling = outer;
      return;
    }
    case fn::kMakeSpace:
    case fn::kWrapSegment:
    case fn::kRestoreSegment: {
      nat::GuestStateTracker& gs = nat::GuestState();
      if (t_jump_depth++ == 0) gs.Sync(base, c.r[0], c.function, t_native_depth == 0);
      c.original(c.context);
      --t_jump_depth;
      gs.Jump(base, c.r[0], c.function);
      return;
    }
  }
  const bool pending = c.function == fn::kBeginVertices || c.function == fn::kBeginIndexedVertices;
  const bool draw = pending || c.function == fn::kDrawVertices ||
                    c.function == fn::kDrawVerticesUP || c.function == fn::kDrawIndexedVertices;
  const bool outer = t_native_depth++ == 0;
  nat::HookArgs a;
  if (outer) {
    DropPending();
    a = EntryArgs(c);
  }
  const uint64_t ret = Tracked(base, c, outer, outer && draw);
  --t_native_depth;
  if (!outer) return;
  a.ret = ret;
  if (pending) {
    // BeginVertices returns the vertex pointer (0: failed), BeginIndexedVertices an HRESULT.
    if (c.function == fn::kBeginIndexedVertices ? uint32_t(ret) == 0 : ret != 0) {
      t_pending_args = a;
      t_pending = true;
    }
  } else if (draw) {
    r->OnDraw(base, a);
  } else if (c.function == fn::kResolve) {
    r->OnResolve(base, a);
  } else if (c.function == fn::kSwap) {
    r->OnSwap(base, a);
  } else {
    r->OnClear(base, a);
  }
}

GRAWBOX_NATIVE_API bool grawbox_native_start(const GrawboxHost* host, const GrawboxTitle* title) {
  if (host->size != sizeof(GrawboxHost) || title->size != sizeof(GrawboxTitle)) return false;
  graw::native::g_host = *host;
  graw::native::g_title = *title;
  return true;
}
