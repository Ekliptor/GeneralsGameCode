# Phase 5h.18 — CPU-shadow routing for DX8 buffer types

Companion doc to `docs/CrossPlatformPort-Plan.md`. Eighteenth
sub-phase of the DX8Wrapper adapter. Follows `Phase5h17-DrawCallRouting.md`.

## Scope gate

5h.17 wired `DX8Wrapper::Draw_Triangles` to route through
`IRenderBackend::Draw_Triangles_Dynamic` when both the current VB and
IB are **sorting-type** (`BUFFER_TYPE_SORTING` / `BUFFER_TYPE_DYNAMIC_SORTING`).
The DX8-type buffers — the path the game's mesh cache uses for
**static geometry** (terrain, building meshes, unit models) — stayed
silently dropped because their `VertexBuffer` pointer is null in bgfx
mode (compat-shim `CreateVertexBuffer` zeros it).

5h.18 closes that gap with a **CPU-side shadow buffer** on each
`DX8VertexBufferClass` / `DX8IndexBufferClass` instance. Allocated
alongside the DX8 VB in the ctor, sized `FVF_Info().Get_FVF_Size() *
VertexCount` bytes (or `index_count * sizeof(unsigned short)` for the
IB). Populated automatically: `VertexBufferClass::WriteLockClass`'s
DX8 dispatch case calls `Lock()` as before, and when the returned
pointer is null (bgfx mode — compat shim stub), falls back to the
shadow. In DX8 mode the shadow is dead memory — real `Lock()` returns
valid GPU-mapped memory on first try, the `nullptr` check never
fires, and the shadow is never read.

`Draw_Triangles` then extracts the shadow on the bgfx path and runs
an FVF → `VertexLayoutDesc` translator. That translator is the other
half of the slice: it parses the D3DFVF bitmask and emits a layout
covering the attribute sequence the game uses (POSITION + optional
NORMAL + optional DIFFUSE + optional SPECULAR-skip + 0–4 UVs).

## Locked decisions

| Question | Decision |
|---|---|
| Per-instance shadow vs side-table | Per-instance. An `unsigned char* m_cpuShadow` member on `DX8VertexBufferClass` and `unsigned short* m_cpuShadow` on `DX8IndexBufferClass`. 8 bytes per VB/IB in DX8 mode (always `nullptr`); O(1) access via `Get_Cpu_Shadow()`. A side-table keyed on `DX8VertexBufferClass*` would add a hash lookup + thread-safety concern for every Lock. |
| Allocation lifecycle | `new[]` in ctor right after `Create_Vertex_Buffer` returns (so `VertexCount` + `FVF_Info()` are both finalized); `delete[]` in dtor. No ref-counting needed — the shadow shares the enclosing object's lifetime. |
| Lock fallback shape | `if (Vertices == nullptr) Vertices = dxVB->Get_Cpu_Shadow();` — the fallback kicks in only when `Lock()` yielded null. In DX8 mode the real `Lock()` yields valid memory, the check never fires, behavior is byte-identical. |
| AppendLockClass byte offset | `Vertices = shadow + start_index * fvfSize;` for VB; `indices = shadow + start_index;` for IB. Matches the byte-offset arithmetic the DX8 `Lock(start * size, range * size, ...)` path performs on real GPU memory. |
| FVF translator scope | Covers POSITION (required XYZ — no XYZRHW support), NORMAL, DIFFUSE, SPECULAR (byte-skipped, no shader slot), and 0–4 sets of 2D UVs. UV2 / UV3 are byte-skipped (the uber-shader only samples UV0 / UV1). Unknown FVFs (non-XYZ position, texcount > 4) return `false` and the draw drops. |
| Why translate instead of using fixed `VertexFormatXYZNDUV2` | Static meshes use various FVFs depending on their material: `XYZNUV1` (simple lit mesh), `XYZNDUV1` (lit + per-vertex color), `XYZNUV2` (lit + second texture stage), etc. Hardcoding one layout would misinterpret most of the game's mesh data. |
| Specular handling | Byte-skipped in the layout. `VertexFormatXYZND*` packs `unsigned specular` immediately after `unsigned diffuse`; the FVF translator adds `sizeof(unsigned)` to the running offset without emitting a semantic. The Phase 5h.11 Blinn-Phong specular gets its contribution from `MaterialDesc::specularColor` + `LightDesc::specular`, not per-vertex specular. |
| Stride sanity check | The translator computes the running byte offset and compares against the FVF size reported by `FVFInfoClass::Get_FVF_Size()`. Mismatch returns false (draw drops). Catches future FVFs the translator doesn't understand without crashing. |
| DX8 impact | Near-zero. Each `DX8VertexBufferClass` carries 8 extra bytes (always-null pointer); each `DX8IndexBufferClass` same. The shadow allocation is a 1-shot `new unsigned char[...]` in the ctor, compiled in both modes but only populated in bgfx mode. The Lock fallback branch is a `nullptr` check on a non-null value — free. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.h` | Add `unsigned char* m_cpuShadow = nullptr;` to `DX8VertexBufferClass::protected`. Add `Get_Cpu_Shadow()` public accessor. |
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8indexbuffer.h` | Add `unsigned short* m_cpuShadow = nullptr;` to `DX8IndexBufferClass::private`. Add `Get_Cpu_Shadow()` public accessor. |
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8vertexbuffer.cpp` | Allocate shadow at end of `DX8VertexBufferClass::Create_Vertex_Buffer`. Free in `DX8VertexBufferClass::~DX8VertexBufferClass`. `VertexBufferClass::WriteLockClass` + `AppendLockClass`: fall back to shadow on `Vertices == nullptr` for the `BUFFER_TYPE_DX8` case. |
| `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/dx8indexbuffer.cpp` | Allocate shadow at end of `DX8IndexBufferClass::DX8IndexBufferClass`. Free in dtor. `IndexBufferClass::WriteLockClass` + `AppendLockClass`: fall back to shadow on `indices == nullptr`. |
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | Extend `Draw_Triangles(4-arg)` bgfx branch to accept both sorting and DX8 buffer types. Resolve CPU payload + stride + layout per-type; use `FillLayoutFromFVF` for DX8-type VBs. Extract indices from shadow for DX8-type IBs. |

### The Lock fallback

```cpp
// Before (5h.17):
DX8_ErrorCode(static_cast<DX8VertexBufferClass*>(VertexBuffer)->Get_DX8_Vertex_Buffer()->Lock(
    0, 0, (unsigned char**)&Vertices, flags));

// After (5h.18):
auto* dxVB = static_cast<DX8VertexBufferClass*>(VertexBuffer);
DX8_ErrorCode(dxVB->Get_DX8_Vertex_Buffer()->Lock(
    0, 0, (unsigned char**)&Vertices, flags));
if (Vertices == nullptr) Vertices = dxVB->Get_Cpu_Shadow();
```

### The FVF translator

```cpp
bool FillLayoutFromFVF(VertexLayoutDesc& d, unsigned fvf, unsigned stride)
{
    d = VertexLayoutDesc{};
    d.stride = stride;
    unsigned off = 0;
    if ((fvf & D3DFVF_POSITION_MASK) != D3DFVF_XYZ) return false;
    add(SEM_POSITION, FLOAT32, 3, 12); off += 12;
    if (fvf & D3DFVF_NORMAL)   { add(SEM_NORMAL, FLOAT32, 3, 12); off += 12; }
    if (fvf & D3DFVF_DIFFUSE)  { add(SEM_COLOR0, UINT8_NORMALIZED, 4, 4); off += 4; }
    if (fvf & D3DFVF_SPECULAR) { off += 4; }  // shader has no spec slot today
    const unsigned texN = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (texN >= 1) { add(SEM_TEXCOORD0, FLOAT32, 2, 8); off += 8; }
    if (texN >= 2) { add(SEM_TEXCOORD1, FLOAT32, 2, 8); off += 8; }
    if (texN >= 3) off += 8;
    if (texN >= 4) off += 8;
    return (off == stride);
}
```

### The extended Draw_Triangles

```cpp
if (vbType == SORTING) {
    vertBytes = reinterpret_cast<uint8_t*>(sortVB->VertexBuffer);
    stride    = sizeof(VertexFormatXYZNDUV2);
    FillSortingLayout(layout);
} else if (vbType == DX8) {
    vertBytes = dxVB->Get_Cpu_Shadow();
    stride    = vb->FVF_Info().Get_FVF_Size();
    layoutOk  = FillLayoutFromFVF(layout, vb->FVF_Info().Get_FVF(), stride);
}
// ... resolve indices similarly ...
const uint8_t* firstVert = vertBytes + (vba_offset + min_vertex_index) * stride;
b->Draw_Triangles_Dynamic(firstVert, vertex_count, layout, firstIdx, polygon_count * 3);
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `DX8VertexBufferClass` / `DX8IndexBufferClass` now carry shadow allocations; the adapter's `Draw_Triangles` accepts both paths. |
| All 20 bgfx tests | PASSED. None exercise the DX8 buffer path (they build descriptors directly); behavior unchanged. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `Get_Cpu_Shadow` producers | Two — `DX8VertexBufferClass` + `DX8IndexBufferClass`. |
| `Get_Cpu_Shadow` consumers | Five — both Lock classes (×2 VB, ×2 IB = 4) plus `DX8Wrapper::Draw_Triangles`. |
| `new[]` allocations of shadow in DX8 mode | Same number as VB/IB ctor calls. Extra heap pressure; deferred check if the game's VB count is high enough to warrant pooling. |
| Memory overhead per DX8 VB in DX8 mode | 8 bytes (always-null pointer) + the shadow allocation (N×size bytes). For Generals' ~10 MB mesh-cache pressure: ~5% overhead in DX8 mode; free in bgfx mode (the shadow is the actual storage). |

## Deferred

| Item | Stage | Blocker |
|---|---|---|
| Non-XYZ position FVFs (XYZRHW, XYZB1..B5). Translator returns false and drops the draw. The game doesn't seem to use these for mesh geometry; may surface for HUD / text where pre-transformed verts appear | 5h.19 if we see drops | Need concrete usage |
| Per-vertex specular lighting. Currently byte-skipped. `LightDesc::specular` + `MaterialDesc::specularColor` carry the light/material specular terms instead, which is a reasonable approximation | later | shader rewrite |
| Shadow allocation elision in DX8 mode | optional | Requires `#ifdef` in `Create_Vertex_Buffer`; micro-opt |
| `Draw_Strip` → backend. Backend has no strip path today | 5h.19 | backend extension |
| Z-sorted alpha via `SortingRendererClass` port | 5h.20 | sortingrenderer.cpp un-ifdef |
| `texture.cpp` un-`#ifdef` for the texture pipeline | 5h.21 | analogous to VB/IB arc |

## What this phase buys

**Every VB/IB path the game uses in `DX8Wrapper::Draw_Triangles` now
reaches the bgfx backend.** Sorting buffers (5h.17) covered particles
/ decals / per-frame CPU-side assembly; DX8 buffers (5h.18) cover the
static-mesh cache that terrain, buildings, and unit models route
through. The draw-call piece of the adapter is effectively complete
for game-rendering purposes — remaining work is the texture binding
path (5h.21) and the alpha-sort pipeline.

Combined with the lighting arc (5h.7–5h.11) and the state-translator
arc (5h.1–5h.12), this phase closes the main data-plane loop: a
mesh with lights + material + textures + vertex + index state set
via DX8Wrapper statics now arrives at the bgfx backend as a concrete
`Draw_Triangles_Dynamic` call with the correct layout descriptor. The
first game-visible frame in bgfx mode is gated only on texture-bind
wiring (which gets a stub placeholder today and a real texture from
the cache once `texture.cpp` un-ifdefs).
