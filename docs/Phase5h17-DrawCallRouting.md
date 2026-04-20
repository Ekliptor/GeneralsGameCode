# Phase 5h.17 — Draw-call routing

Companion doc to `docs/CrossPlatformPort-Plan.md`. Seventeenth
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h16-DynamicBufferAccessUnstub.md`.

## Scope gate

5h.13 → 5h.16 un-guarded the entire `dx8vertexbuffer.cpp` /
`dx8indexbuffer.cpp` compile-unit surface. `VertexBufferClass*` /
`IndexBufferClass*` can now be constructed, held, locked, and
extracted in bgfx-mode code. The draw-path stubs in the bgfx branch
of `dx8wrapper.cpp` (lines 4596-4599 for the Set_ methods, 4976-4978
for the Draw_ methods) stayed as no-ops: they accepted arguments and
did nothing.

5h.17 makes them real. `Set_Vertex_Buffer` / `Set_Index_Buffer` (all
four overloads) now track state in `render_state.vertex_buffers[]` /
`render_state.index_buffer` + refcounts + dirty bits. `Draw_Triangles`
(both overloads) drains `Apply_Render_State_Changes` and routes the
draw through `IRenderBackend::Draw_Triangles_Dynamic` when both the
current VB and IB are **sorting-type buffers** (the only type that
carries a CPU-accessible payload in bgfx mode). The DX8-only path
(VBs with an `IDirect3DVertexBuffer8*` holding GPU-side storage)
stays silently dropped for now — routing those needs a CPU-side
payload extension on `DX8VertexBufferClass`, a separate slice.

This is the phase where **a game-driven draw call reaches the bgfx
backend for the first time**. Everything earlier was state setup;
this closes the loop.

## Locked decisions

| Question | Decision |
|---|---|
| Which VB/IB types route | Only `BUFFER_TYPE_SORTING` and `BUFFER_TYPE_DYNAMIC_SORTING`. Their `VertexBuffer` / `index_buffer` members point at CPU-side arrays (`VertexFormatXYZNDUV2*` / `unsigned short*`) — `reinterpret_cast` + `Draw_Triangles_Dynamic` handles the rest. `BUFFER_TYPE_DX8` / `BUFFER_TYPE_DYNAMIC_DX8` have `IDirect3DVertexBuffer8* VertexBuffer = nullptr` in bgfx mode (compat-shim `CreateVertexBuffer` zeros it), so there's nothing to extract. Silent drop with a commented TODO. |
| Vertex layout | Always `VertexFormatXYZNDUV2` for sorting VBs — the sorting pool is hard-wired to `dynamic_fvf_type` (see `SortingVertexBufferClass::SortingVertexBufferClass` at `dx8vertexbuffer.cpp:270`). Matches the Phase 5m `Draw_Triangles_Dynamic` layout used by `tst_bgfx_dynamic`: position + normal + color (UINT8_NORMALIZED ABGR) + UV0 + UV1. |
| Where does `Apply_Render_State_Changes` fire | At the top of `Draw_Triangles`'s 4-arg overload, before extracting the VB/IB payload. This mirrors the DX8 body's `_Apply_Render_State_Changes()` call (invoked inside `Draw(D3DPT_TRIANGLELIST, ...)` which `Draw_Triangles` forwards to). Ensures transform / shader / material state reaches the backend before the draw is submitted. |
| `Draw_Strip` routing | Stays a no-op. The main game draw path is `Draw_Triangles`; strips show up only in a few particle passes. Adding strip support is mechanical once needed — `Draw_Triangles_Dynamic` doesn't accept strips today, so the backend would need a `Draw_TriangleStrip_Dynamic` sibling first. Deferred. |
| `Set_*_Buffer` state tracking | Cloned verbatim from the DX8 implementations at `dx8wrapper.cpp:1867` / `1893` / `1917` / `1941` — those bodies are DX8-API-free, just refcount + dirty-bit management. Same semantics in both modes. |
| `vba_offset` / `iba_offset` / `index_base_offset` / `min_vertex_index` | Applied at extraction time: `firstIdx = inds + iba_offset + start_index`, `firstVert = verts + vba_offset + min_vertex_index`. Matches the DX8 offset chain that `_Apply_Render_State_Changes` + `Draw` compose at `SetStreamSource` / `SetIndices` / `DrawIndexedPrimitive` time. |
| `buffer_type` arg on the 5-arg overload | Ignored in bgfx; the VB/IB's own `Type()` already tells us whether to route (sorting) or drop (DX8). The DX8 body dispatches to `SortingRendererClass::Insert_Triangles` for sorting — that's an alpha-sort queue that eventually replays via the sorting renderer, which isn't compiled in bgfx mode. Skipping the sort step means translucent geometry will render out of order; Z-sorting parity is a follow-up. |
| Why no new test | Same pattern as 5h.4 / 5h.7 / 5h.8 / 5h.12: a harness would need to link `z_ww3d2` + statically initialize `DX8Wrapper` + navigate the `W3DMPO` + `RefCountClass` construction chain for `SortingVertexBufferClass`. The draw-path behavior on the backend side is already covered by `tst_bgfx_dynamic` (uses the same `Draw_Triangles_Dynamic` entry point). Build-verification + 20-test regression + DX8 reconfigure-clean is the right bar. |
| DX8 impact | Zero. Every edit was inside the `#else` branch of `RTS_RENDERER_DX8`; the DX8 body at lines 1867 / 1893 / 1917 / 1941 / 2188 / 2209 / 2224 is byte-identical. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | bgfx branch: `#include "dx8vertexbuffer.h"` + `"dx8indexbuffer.h"` + `"dx8fvf.h"`. Replace the no-op `Set_Vertex_Buffer` / `Set_Index_Buffer` stubs (both `VertexBufferClass*` + `DynamicVBAccessClass&` / IB equivalents) with verbatim clones of the DX8 state-tracking bodies. Replace the no-op 5-arg `Draw_Triangles` with a forward to the 4-arg overload. Replace the no-op 4-arg `Draw_Triangles` with: `Apply_Render_State_Changes` → sorting-type check → CPU-payload extraction → `backend->Draw_Triangles_Dynamic`. `Draw_Strip` stays a no-op with a comment. |

### The draw body

```cpp
void DX8Wrapper::Draw_Triangles(unsigned short start_index,
                                 unsigned short polygon_count,
                                 unsigned short min_vertex_index,
                                 unsigned short vertex_count)
{
    IRenderBackend* b = RenderBackendRuntime::Get_Active();
    if (b == nullptr) return;

    Apply_Render_State_Changes();

    VertexBufferClass* vb = render_state.vertex_buffers[0];
    IndexBufferClass*  ib = render_state.index_buffer;
    if (vb == nullptr || ib == nullptr) return;

    const bool vbSortingLike = (vb->Type() == BUFFER_TYPE_SORTING || vb->Type() == BUFFER_TYPE_DYNAMIC_SORTING);
    const bool ibSortingLike = (ib->Type() == BUFFER_TYPE_SORTING || ib->Type() == BUFFER_TYPE_DYNAMIC_SORTING);
    if (!vbSortingLike || !ibSortingLike) return;

    auto* sortVB = static_cast<SortingVertexBufferClass*>(vb);
    auto* sortIB = static_cast<SortingIndexBufferClass*>(ib);
    const VertexFormatXYZNDUV2* verts = sortVB->VertexBuffer;
    const unsigned short*       inds  = sortIB->index_buffer;
    if (verts == nullptr || inds == nullptr) return;

    const unsigned short*       firstIdx  = inds  + render_state.iba_offset + start_index;
    const VertexFormatXYZNDUV2* firstVert = verts + render_state.vba_offset + min_vertex_index;

    VertexLayoutDesc layout;
    FillSortingLayout(layout);  // position + normal + color + uv0 + uv1

    b->Draw_Triangles_Dynamic(firstVert, vertex_count, layout, firstIdx, polygon_count * 3);
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `DX8Wrapper::Draw_Triangles` + `Draw_Strip` + all `Set_*_Buffer` overloads now contribute real symbols (previously no-op stubs). |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::Draw_Triangles_Dynamic` call sites in production code | One — `DX8Wrapper::Draw_Triangles(4-arg)` bgfx branch. Up from zero. |
| DX8 production Draw_Triangles body | Unchanged. |
| Outer `#ifdef RTS_RENDERER_DX8` in dx8wrapper.cpp | Still wraps the whole DX8 body (lines 235–4477). The bgfx branch's `#else` now covers every DX8Wrapper entry point the game calls during scene setup + the draw path itself. |

## Deferred

| Item | Stage | Blocker |
|---|---|---|
| `BUFFER_TYPE_DX8` / `BUFFER_TYPE_DYNAMIC_DX8` draw path. Needs a CPU-side payload on `DX8VertexBufferClass` (e.g., a `std::vector<uint8_t>` that shadows the GPU VB and gets filled by WriteLockClass's SORTING-style pass). The game's mesh-cache uses DX8 buffers for static geometry | 5h.18 | None structural — just a CPU-side shadow buffer design |
| Non-dynamic_fvf_type vertex layouts. Today the sorting pool is hard-wired to `dynamic_fvf_type`; callers that set a VB with a different FVF via `DX8VertexBufferClass` would need FVF→layout translation | 5h.18 | Bundles with above |
| `Draw_Strip` → `backend->Draw_Triangle_Strip_Dynamic`. Backend currently lacks the strip variant | 5h.19 | Backend extension |
| Z-sorted alpha (the `SortingRendererClass::Insert_Triangles` queue the DX8 path uses for BUFFER_TYPE_SORTING). Today bgfx routes sorting draws inline, which means translucent faces render out of order | 5h.19 | Port sortingrenderer.cpp |
| `texture.cpp` un-`#ifdef` — still gates `TextureClass::Init` | 5h.20 | Analogous to VB/IB arc |
| First game-visible render in bgfx mode | after 5h.18 or 5h.20 | depends on which paths the first game scene exercises |

## What this phase buys

**The first draw call from production game code can now reach the
bgfx backend.** Any DX8Wrapper caller that sets a `SortingVertexBufferClass`
+ `SortingIndexBufferClass` pair and invokes `Draw_Triangles` will
land geometry on screen in bgfx mode — same entry-point the game
uses for translucent geometry, particles, decals, and any path that
wants per-frame CPU-side vertex assembly.

The final gap for "first game frame renders something" is:
1. Route DX8-type VBs (5h.18) — the main mesh-cache path.
2. Port `TextureClass::Init` → `BgfxTextureCache` (5h.20).

Either of those (plus what's already landed) is enough to get
*something* rendering. Both are needed for full visual coverage.
