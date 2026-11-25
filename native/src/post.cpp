// See post.h.
#include "post.h"

#include "d3d12_device.h"
#include "log.h"
#include "resolve.h"

#include <rex/cvar.h>
#include <rex/logging.h>

#include <algorithm>
#include <string>

REXCVAR_DECLARE(std::string, graw_native_haze);
REXCVAR_DECLARE(int32_t, graw_native_ao);
REXCVAR_DECLARE(double, graw_native_sharpness);
REXCVAR_DEFINE_BOOL(graw_native_ao_debug, false, "GRAW native", "Show the ambient occlusion alone");

namespace graw::native {

namespace {

constexpr D3D12_RESOURCE_STATES kImageRead = D3D12_RESOURCE_STATE_COPY_SOURCE |
                                             D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                             D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES kWrite = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

// The blurred far picture: half the guest's resolution at any draw resolution.
constexpr uint32_t kFarWidth = 640, kFarHeight = 360;

struct Constants {
  float out_size[2], inv_out[2];
  float ao_size[2], axis[2];
  float wave[4];
  float time[2], shift[2];
  float range, ao, sharpness;
  uint32_t flags;  // 1: the haze, 2: ambient occlusion, 4: it alone
};
static_assert(sizeof(Constants) == 20 * 4);

const char kSource[] = R"(
cbuffer C : register(b0) {
  float2 out_size, inv_out;
  float2 ao_size, axis;
  float4 wave;
  float2 time, shift;
  float range, ao_strength, sharpness;
  uint flags;
};
Texture2D<float4> scene : register(t0);
Texture2D<float> depth : register(t1);
Texture2D<float4> blurred : register(t2);
Texture2D<float> ao : register(t3);
#if defined(FAR_DOWN) || defined(FAR_BLUR)
RWTexture2D<float4> dst : register(u0);
#else
RWTexture2D<unorm float4> dst : register(u0);
#endif
SamplerState lin : register(s0);
SamplerState pnt : register(s1);

// The depth is the near plane of both games' projections over the distance in meters.
float Dist(float d) { return 0.1 / max(d, 1e-7); }
float DistAt(float2 uv) { return Dist(depth.SampleLevel(pnt, uv, 0)); }
// The games' haze weight by depth.
float Far(float2 uv) {
  return saturate(1.0 / (depth.SampleLevel(pnt, uv, 0) + 1e-7) / range);
}

#if defined(FAR_DOWN)
// Where the haze is, averaged with its weight, so nothing near bleeds into what is far.
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  float4 sum = 0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x) {
      float2 uv = (id.xy + 0.5 + float2(x, y) / 3.0) * inv_out;
      float4 s = scene.SampleLevel(lin, uv, 0);
      float w = saturate(Far(uv) + s.a) + 1e-3;
      w *= w;
      sum += float4(s.rgb * w, w);
    }
  dst[id.xy] = sum / 9.0;
}
#elif defined(FAR_BLUR)
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  float4 sum = 0;
  float total = 0;
  for (int i = -7; i <= 7; ++i) {
    float g = exp(-0.5 * i * i / (2.5 * 2.5));
    sum += blurred.SampleLevel(lin, (id.xy + 0.5 + axis * i) * inv_out, 0) * g;
    total += g;
  }
  dst[id.xy] = sum / total;
}
#elif defined(AO)
static const float2 kTan = float2(0.7516, 0.4228);  // of half the usual field of view
float3 Pos(float2 uv) {
  float z = DistAt(uv);
  return float3((uv * 2.0 - 1.0) * kTan * z, z);
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  float2 uv = (id.xy + 0.5) * inv_out;
  float3 p = Pos(uv);
  if (p.z > 120.0) { dst[id.xy] = 1; return; }
  float2 ix = float2(inv_out.x, 0), iy = float2(0, inv_out.y);
  float3 l = p - Pos(uv - ix), r = Pos(uv + ix) - p, u = p - Pos(uv - iy), d = Pos(uv + iy) - p;
  float3 dx = abs(l.z) < abs(r.z) ? l : r, dy = abs(u.z) < abs(d.z) ? u : d;
  float3 n = normalize(cross(dy, dx));
  if (dot(n, p) > 0) n = -n;  // towards the viewer
  const float R = 0.9;
  float radius = R / (2.0 * kTan.y * p.z);  // of the screen height
  radius = min(radius, 0.12);
  if (radius * out_size.y < 1.5) { dst[id.xy] = 1; return; }
  float noise = frac(52.9829189 * frac(dot(float2(id.xy), float2(0.06711056, 0.00583715))));
  float sum = 0;
  const int DIRS = 8, STEPS = 4;
  float bias = 0.06 + 0.002 * p.z;
  for (int i = 0; i < DIRS; ++i) {
    float a = (i + noise) * (6.2831853 / DIRS);
    float2 dir = float2(cos(a) * out_size.y / out_size.x, sin(a)) * radius;
    float jitter = frac(noise * 7.0 + i * 0.618);
    float best = 0;
    for (int s = 0; s < STEPS; ++s) {
      float t = (s + jitter + 0.15) / (STEPS + 0.15);
      float2 q = uv + dir * t;
      if (all(q >= 0) && all(q <= 1)) {
        float3 v = Pos(q) - p;
        float len2 = dot(v, v);
        float e = dot(v, n) / sqrt(len2 + 1e-6) - bias;
        best = max(best, e * saturate(1.0 - len2 / (R * R)));
      }
    }
    sum += best;
  }
  float o = saturate(1.0 - 1.6 * sum / DIRS);
  dst[id.xy] = lerp(o, 1.0, smoothstep(50.0, 110.0, p.z));
}
#elif defined(AO_BLUR)
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  float2 uv = (id.xy + 0.5) * inv_out;
  float z = DistAt(uv);
  float sum = 0, total = 0;
  for (int i = -3; i <= 3; ++i) {
    float2 q = uv + axis * i * inv_out;
    float w = exp(-0.12 * i * i) * saturate(1.0 - abs(DistAt(q) - z) / (0.02 * z + 0.05));
    if (i == 0) w = 1;
    sum += ao.SampleLevel(pnt, q, 0) * w;
    total += w;
  }
  dst[id.xy] = sum / total;
}
#else
float Occlusion(float2 uv, float z) {
  // The four nearest, by how near in depth.
  float2 t = uv * ao_size - 0.5, b = floor(t), f = t - b;
  float sum = 0, total = 0;
  for (int i = 0; i < 4; ++i) {
    float2 o = float2(i & 1, i >> 1);
    float2 q = (b + o + 0.5) / ao_size;
    float2 m = lerp(1.0 - f, f, o);
    float w = (m.x * m.y + 0.01) / (0.02 + abs(DistAt(q) - z) / z);
    sum += ao.SampleLevel(pnt, q, 0) * w;
    total += w;
  }
  return sum / total;
}
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
  if (any(id.xy >= uint2(out_size))) return;
  float2 uv = (id.xy + 0.5) * inv_out;
  float4 s = scene.SampleLevel(pnt, uv, 0);
  float3 c = s.rgb;
  float z = DistAt(uv);
  if (sharpness > 0) {
    // Less where the neighbors already differ, as contrast adaptive sharpening does.
    float3 n = scene.SampleLevel(pnt, uv - float2(0, inv_out.y), 0).rgb;
    float3 e = scene.SampleLevel(pnt, uv + float2(inv_out.x, 0), 0).rgb;
    float3 o = scene.SampleLevel(pnt, uv + float2(0, inv_out.y), 0).rgb;
    float3 w = scene.SampleLevel(pnt, uv - float2(inv_out.x, 0), 0).rgb;
    float3 lo = min(c, min(min(n, e), min(o, w))), hi = max(c, max(max(n, e), max(o, w)));
    float3 amount = sqrt(saturate(min(lo, 1.0 - hi) / max(hi, 1e-4))) * -0.2 * sharpness;
    c = saturate((c + (n + e + o + w) * amount) / (1.0 + 4.0 * amount));
  }
  if (flags & 1u) {
    float w = Far(uv);
    float k = saturate(s.a + w);
    if (k > 0.002) {
      // The moving displacement, which the games work out per vertex.
      float sv = sin(wave.w * uv.y + time.y), su = sin(wave.w * uv.x + time.x);
      float a = wave.y * uv.y + time.y;
      float b = wave.x * uv.x + time.x - wave.z * (a + uv.x);
      float g = a - wave.z * uv.x * a;
      float blot = saturate(su * sv + su - sv - 0.5);
      float2 move = float2(sin(b), sin(g)) * shift * (0.5 * k * (1.0 + blot));
      // The games average the picture with its displaced copy, which doubles it: here it moves
      // half as far, softened as wide.
      float2 r = shift * (0.35 * k);
      float3 sum = 0;
      float total = 0;
      [unroll] for (int i = 0; i < 4; ++i) {
        float2 q = uv + move + r * float2(i & 1 ? 1 : -1, i & 2 ? 1 : -1);
        float4 t = scene.SampleLevel(lin, q, 0);
        float m = saturate((saturate(Far(q) + t.a) / k - 0.5) * 2.5);
        sum += t.rgb * m;
        total += m;
      }
      // Without a neighbor as far, the pixel itself.
      c = lerp(c, sum / max(total, 1e-4), saturate(total) * smoothstep(0.0, 0.35, k));
      float4 f = blurred.SampleLevel(lin, uv + move, 0);
      float share = 0.5 * w + k * blot * (1.0 - 0.5 * w);
      c = lerp(c, f.rgb / max(f.a, 1e-6), share * saturate(f.a / (0.05 * k * k + 1e-6)));
    }
  }
  if (flags & 2u) {
    float o = Occlusion(uv, z);
    float l = dot(c, float3(0.2, 0.6, 0.2));
    o = lerp(1.0, o, ao_strength * (1.0 - 0.6 * smoothstep(0.45, 1.0, l)));
    c = flags & 4u ? o.xxx : c * o;
  }
  // No haze left for the pass of the game to add.
  dst[id.xy] = float4(c, flags & 1u ? 0.0 : s.a);
}
#endif
)";

struct Target {
  ComPtr<ID3D12Resource> resource;
  DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  D3D12_RESOURCE_STATES state = kWrite;
};

}  // namespace

struct PostProcess::Impl {
  enum Pass { kFarDown, kFarBlur, kAo, kAoBlur, kComposite, kPasses };
  static constexpr uint32_t kViews = 5, kTablesPerFrame = 16, kFrames = 8;

  ID3D12Device* device = nullptr;
  bool failed = false;
  ComPtr<ID3D12RootSignature> root;
  ComPtr<ID3D12PipelineState> pso[kPasses];
  ComPtr<ID3D12DescriptorHeap> heap;
  uint32_t stride = 0, table = 0;
  uint64_t frame = ~0ull;
  uint32_t width = 0, height = 0, ao_width = 0, ao_height = 0;
  Target far_a, far_b, ao_a, ao_b, out;

  bool Create() {
    D3D12_DESCRIPTOR_RANGE ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 4;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].OffsetInDescriptorsFromTableStart = 4;
    D3D12_ROOT_PARAMETER params[2] = {};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.Num32BitValues = sizeof(Constants) / 4;
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 2;
    params[1].DescriptorTable.pDescriptorRanges = ranges;
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
    for (UINT i = 0; i < 2; ++i) {
      samplers[i].Filter =
          i ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
      samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW =
          D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
      samplers[i].ShaderRegister = i;
      samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
    }
    D3D12_ROOT_SIGNATURE_DESC rd = {2, params, 2, samplers};
    ComPtr<ID3DBlob> blob, error;
    if (Failed(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error),
               "post root signature") ||
        Failed(device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                           IID_PPV_ARGS(&root)),
               "post CreateRootSignature"))
      return false;
    const pD3DCompile compile = D3DCompiler();
    if (!compile) return false;
    const char* names[kPasses] = {"FAR_DOWN", "FAR_BLUR", "AO", "AO_BLUR", "COMPOSITE"};
    for (int i = 0; i < kPasses; ++i) {
      const D3D_SHADER_MACRO defines[] = {{names[i], "1"}, {nullptr, nullptr}};
      ComPtr<ID3DBlob> code;
      error.Reset();
      if (FAILED(compile(kSource, sizeof(kSource) - 1, "graw_post.hlsl", defines, nullptr, "main",
                         "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error))) {
        REXLOG_ERROR("GRAW native post: {}: {}", names[i],
                     error ? static_cast<const char*>(error->GetBufferPointer()) : "?");
        return false;
      }
      D3D12_COMPUTE_PIPELINE_STATE_DESC pd = {};
      pd.pRootSignature = root.Get();
      pd.CS = {code->GetBufferPointer(), code->GetBufferSize()};
      if (Failed(device->CreateComputePipelineState(&pd, IID_PPV_ARGS(&pso[i])), "post pipeline"))
        return false;
    }
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    hd.NumDescriptors = kViews * kTablesPerFrame * kFrames;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    if (Failed(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&heap)), "post descriptor heap"))
      return false;
    stride = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
  }

  bool Make(Target& t, uint32_t w, uint32_t h, DXGI_FORMAT format) {
    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d = {};
    d.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width = w;
    d.Height = h;
    d.DepthOrArraySize = 1;
    d.MipLevels = 1;
    d.Format = format;
    d.SampleDesc.Count = 1;
    d.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    t.format = format;
    t.state = kWrite;
    return !Failed(
        device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &d, kWrite, nullptr,
                                        IID_PPV_ARGS(t.resource.ReleaseAndGetAddressOf())),
        "post target");
  }

  static void To(ID3D12GraphicsCommandList* list, Target& t, D3D12_RESOURCE_STATES state) {
    if (t.state == state) return;
    const D3D12_RESOURCE_BARRIER b = TransitionBarrier(t.resource.Get(), t.state, state);
    list->ResourceBarrier(1, &b);
    t.state = state;
  }

  void Dispatch(ID3D12GraphicsCommandList* list, Pass pass, Constants& c, ID3D12Resource* scene,
                ID3D12Resource* depth, Target* far_in, Target* ao_in, Target& dst, uint32_t w,
                uint32_t h) {
    if (far_in) To(list, *far_in, kRead);
    if (ao_in) To(list, *ao_in, kRead);
    To(list, dst, kWrite);
    const uint32_t first = (uint32_t(frame % kFrames) * kTablesPerFrame + table++) * kViews;
    const auto cpu = heap->GetCPUDescriptorHandleForHeapStart();
    auto view = [&](uint32_t i) {
      return D3D12_CPU_DESCRIPTOR_HANDLE{cpu.ptr + size_t(first + i) * stride};
    };
    auto srv = [&](uint32_t i, ID3D12Resource* r, DXGI_FORMAT format) {
      D3D12_SHADER_RESOURCE_VIEW_DESC d = {};
      d.Format = format;
      d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      d.Texture2D.MipLevels = 1;
      device->CreateShaderResourceView(r, &d, view(i));
    };
    srv(0, scene, DXGI_FORMAT_R8G8B8A8_UNORM);
    srv(1, depth, DXGI_FORMAT_R32_FLOAT);
    srv(2, far_in ? far_in->resource.Get() : nullptr, DXGI_FORMAT_R16G16B16A16_FLOAT);
    srv(3, ao_in ? ao_in->resource.Get() : nullptr, DXGI_FORMAT_R8_UNORM);
    D3D12_UNORDERED_ACCESS_VIEW_DESC ud = {};
    ud.Format = dst.format;
    ud.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    device->CreateUnorderedAccessView(dst.resource.Get(), nullptr, &ud, view(4));
    c.out_size[0] = float(w);
    c.out_size[1] = float(h);
    c.inv_out[0] = 1.0f / float(w);
    c.inv_out[1] = 1.0f / float(h);
    list->SetPipelineState(pso[pass].Get());
    list->SetComputeRoot32BitConstants(0, sizeof(c) / 4, &c, 0);
    auto gpu = heap->GetGPUDescriptorHandleForHeapStart();
    gpu.ptr += uint64_t(first) * stride;
    list->SetComputeRootDescriptorTable(1, gpu);
    list->Dispatch((w + 7) / 8, (h + 7) / 8, 1);
  }
};

PostProcess::PostProcess() : impl_(std::make_unique<Impl>()) {}
PostProcess::~PostProcess() = default;

bool PostProcess::DrawsHaze() {
  static const bool draws = REXCVAR_GET(graw_native_haze) == "fixed";
  return draws;
}

bool PostProcess::Wanted() {
  static const bool wanted =
      DrawsHaze() || REXCVAR_GET(graw_native_ao) > 0 || REXCVAR_GET(graw_native_sharpness) > 0;
  return wanted;
}

bool PostProcess::Run(ID3D12Device* device, ID3D12GraphicsCommandList* list, uint64_t frame,
                      const ResolvedImage& scene_image, const ResolvedImage& depth_image,
                      const HazeConstants& haze) {
  Impl& m = *impl_;
  ID3D12Resource* const scene =
      scene_image.texture_scaled ? scene_image.texture_scaled : scene_image.texture;
  ID3D12Resource* const depth =
      depth_image.depth_r32f_scaled ? depth_image.depth_r32f_scaled : depth_image.depth_r32f;
  if (m.failed || !scene || !depth) return false;
  const D3D12_RESOURCE_DESC sd = scene->GetDesc();
  if (sd.Format != DXGI_FORMAT_R8G8B8A8_TYPELESS) return false;
  if (!m.device) {
    m.device = device;
    if (!m.Create()) {
      m.failed = true;
      return false;
    }
  }
  const uint32_t w = uint32_t(sd.Width), h = sd.Height;
  if (w != m.width || h != m.height) {
    if (m.width) return false;  // the scene is one size for the whole run
    const bool half = h > 1080;
    m.width = w;
    m.height = h;
    m.ao_width = half ? w / 2 : w;
    m.ao_height = half ? h / 2 : h;
    if (!m.Make(m.far_a, kFarWidth, kFarHeight, DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        !m.Make(m.far_b, kFarWidth, kFarHeight, DXGI_FORMAT_R16G16B16A16_FLOAT) ||
        !m.Make(m.ao_a, m.ao_width, m.ao_height, DXGI_FORMAT_R8_UNORM) ||
        !m.Make(m.ao_b, m.ao_width, m.ao_height, DXGI_FORMAT_R8_UNORM) ||
        !m.Make(m.out, w, h, DXGI_FORMAT_R8G8B8A8_UNORM)) {
      m.failed = true;
      return false;
    }
    REXLOG_INFO("GRAW native post: {}x{}, haze {}, ambient occlusion {}, sharpness {}", w, h,
                REXCVAR_GET(graw_native_haze), REXCVAR_GET(graw_native_ao),
                REXCVAR_GET(graw_native_sharpness));
  }
  if (m.frame != frame) {
    m.frame = frame;
    m.table = 0;
  }
  if (m.table + 8 > Impl::kTablesPerFrame) return false;
  static const int32_t ao = std::clamp(REXCVAR_GET(graw_native_ao), 0, 100);
  Constants c = {};
  std::copy_n(haze.wave, 4, c.wave);
  std::copy_n(haze.time, 2, c.time);
  std::copy_n(haze.shift, 2, c.shift);
  c.range = haze.range;
  c.ao = float(ao) / 100.0f;
  c.sharpness = float(std::clamp(REXCVAR_GET(graw_native_sharpness), 0.0, 1.0));
  c.ao_size[0] = float(m.ao_width);
  c.ao_size[1] = float(m.ao_height);
  c.flags = (DrawsHaze() ? 1u : 0u) | (ao ? 2u : 0u) |
            (ao && REXCVAR_GET(graw_native_ao_debug) ? 4u : 0u);
  ID3D12DescriptorHeap* heaps[] = {m.heap.Get()};
  list->SetDescriptorHeaps(1, heaps);
  list->SetComputeRootSignature(m.root.Get());
  auto pass = [&](Impl::Pass p, Target* far_in, Target* ao_in, Target& dst, uint32_t dw,
                  uint32_t dh, float ax = 0, float ay = 0) {
    c.axis[0] = ax;
    c.axis[1] = ay;
    m.Dispatch(list, p, c, scene, depth, far_in, ao_in, dst, dw, dh);
  };
  if (c.flags & 1) {
    pass(Impl::kFarDown, nullptr, nullptr, m.far_a, kFarWidth, kFarHeight);
    pass(Impl::kFarBlur, &m.far_a, nullptr, m.far_b, kFarWidth, kFarHeight, 1, 0);
    pass(Impl::kFarBlur, &m.far_b, nullptr, m.far_a, kFarWidth, kFarHeight, 0, 1);
  }
  if (c.flags & 2) {
    pass(Impl::kAo, nullptr, nullptr, m.ao_a, m.ao_width, m.ao_height);
    pass(Impl::kAoBlur, nullptr, &m.ao_a, m.ao_b, m.ao_width, m.ao_height, 1, 0);
    pass(Impl::kAoBlur, nullptr, &m.ao_b, m.ao_a, m.ao_width, m.ao_height, 0, 1);
  }
  pass(Impl::kComposite, &m.far_a, &m.ao_a, m.out, w, h);
  Impl::To(list, m.out, D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_RESOURCE_BARRIER b = TransitionBarrier(scene, kImageRead, D3D12_RESOURCE_STATE_COPY_DEST);
  list->ResourceBarrier(1, &b);
  list->CopyResource(scene, m.out.resource.Get());
  b = TransitionBarrier(scene, D3D12_RESOURCE_STATE_COPY_DEST, kImageRead);
  list->ResourceBarrier(1, &b);
  return true;
}

}  // namespace graw::native
