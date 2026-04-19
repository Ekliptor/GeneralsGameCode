# Phase 5m — Transient VB/IB for per-frame streaming geometry

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twelfth stage of Phase 5
(Renderer bgfx backend). Follows `Phase5l-MultiLight.md`.

(Phase 5h — DX8Wrapper → IRenderBackend adapter — is still open; 5m slots
in before it so the adapter has a fire-and-forget streaming-geometry draw
path that doesn't reduce to "destroy + recreate a persistent bgfx VB every
frame" — which is what the Phase 5g persistent path collapses to if the
game's per-frame `DX8Wrapper::Lock_Vertex_Buffer` call sites are routed
straight through `Set_Vertex_Buffer`.)

## Objective

Add a single new `IRenderBackend` method —
`Draw_Triangles_Dynamic(verts, vcount, layout, indices, icount)` — that
absorbs the whole alloc + copy + bind + submit cycle in one call using
bgfx's transient buffers. The result:

- **zero caller-visible handle** — data lives for one frame, bgfx reclaims
  it at the next frame boundary;
- **no GPU-allocator churn** — transient buffers draw from a ring pool bgfx
  grows on demand, instead of pairing `createVertexBuffer` + `destroy`
  every frame;
- **same shader selection / uniforms / state / textures** as the persistent
  `Draw_Indexed` path — through a new shared `ApplyDrawState` helper that
  both paths now call.

The existing persistent path (`Set_Vertex_Buffer` → `Draw_Indexed`) stays
for long-lived meshes (cubes in tests; static scene geometry in
production). The two are orthogonal and can be mixed in the same frame.

## Locked decisions

| Question | Decision |
|---|---|
| API shape | One method: `Draw_Triangles_Dynamic(verts, vcount, layout, indices, icount)`. `indices == nullptr && icount == 0` → non-indexed draw of `vcount / 3` triangles; otherwise `icount % 3 == 0` required. Fire-and-forget: no handle returned, nothing to release. Matches the usage shape of DX8's dynamic-lock pattern (`Lock → write → Unlock → Draw`) collapsed to a single call. |
| Lock-and-fill vs. copy | Copy. Caller hands over a pointer + size, backend `memcpy`s into bgfx's transient storage. The alternative — hand the caller a bgfx-owned `TransientVertexBuffer.data` pointer to fill in place — would save a memcpy but leaks bgfx types into `IRenderBackend.h` and forces the caller into bgfx's frame-boundary contract. The copy version matches what DX8Wrapper does internally anyway (upload staging → device). |
| Transient vs. dynamic bgfx buffers | Transient (`allocTransient*`). Dynamic (`createDynamicVertexBuffer` + `update`) preserves the handle across frames, but the game pattern is one-shot-per-draw; dynamic would require handle bookkeeping we don't need. Transient buffers live in a per-frame ring pool that bgfx grows as needed. |
| Capacity check | `getAvailTransient*Buffer` checked *before* `allocTransient*`; undersized → silently drop the draw. Matches DX8's "failed lock drops the call" semantics. Production adapter will split oversize uploads on its side when it comes to that. |
| Where `ApplyDrawState` lives | As a private member of `BgfxBackend` (new, Phase 5m). Extracted from the two near-duplicate bodies in `Draw_Indexed` + `Draw` so `Draw_Triangles_Dynamic` reuses the same program / uniform / texture / state setup. The only variable input is the attribute mask — from `m_vbAttrMask` in the persistent path, from the caller's layout in the dynamic path. |
| Layout translation | Same `TranslateLayout(VertexLayoutDesc → bgfx::VertexLayout)` that the persistent path uses. Both paths agree that the caller's descriptor is the source of truth; the backend never caches a bgfx layout keyed on the descriptor (the cost of a per-call `layout.begin/.add/.end` is negligible relative to the GPU submit). |
| Smoke test geometry | 16×16 vertex grid (240 verts, 450 triangles) driven by a 2D sine + cosine wave. Chosen because (a) it visibly changes every frame, (b) has non-trivial geometry — enough indices to exercise the index path, (c) analytical normals are cheap (partial-derivative of the height function), and (d) lighting by the Phase 5l multi-lit program proves the state-apply refactor didn't regress the lit path. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | Add pure-virtual `Draw_Triangles_Dynamic`. Docstring spells out the `indices == nullptr` sentinel and the "fire-and-forget, one-frame lifetime" contract. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Declare the override. Add private `ApplyDrawState(attrMask) -> bgfx::ProgramHandle` helper. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Extract `ApplyDrawState` from the duplicated state-setup that lived in `Draw_Indexed` + `Draw`. Rewrite both to call it, then just set buffers and submit. Implement `Draw_Triangles_Dynamic`: bgfx layout translate → attr mask → `getAvailTransient*` capacity check → `allocTransient*` + `memcpy` → `ApplyDrawState` → set buffers → submit. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_dynamic)`. |

### New files

| Path | Purpose |
|---|---|
| `tests/tst_bgfx_dynamic/main.cpp` | 16×16 grid re-computed every frame; `h(x,z,t) = A(sin(kx+wt) + cos(kz+wt))`; analytical normal `normalize(-∂h/∂x, 1, -∂h/∂z)`; rendered via the multi-lit program with a gradient texture. Exits after ~3 s. |
| `tests/tst_bgfx_dynamic/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`. |
| `docs/Phase5m-DynamicVBIB.md` | This doc. |

### Implementation highlight

```cpp
void BgfxBackend::Draw_Triangles_Dynamic(const void* verts,
                                         unsigned vertexCount,
                                         const VertexLayoutDesc& layout,
                                         const uint16_t* indices,
                                         unsigned indexCount)
{
    if (!m_initialized || !verts || vertexCount == 0) return;
    InitPipelineResources();

    const bool indexed = (indices != nullptr && indexCount > 0);
    if (indexed && (indexCount % 3 != 0)) return;
    if (!indexed && (vertexCount % 3 != 0)) return;

    bgfx::VertexLayout bLayout;
    TranslateLayout(layout, bLayout);
    const uint32_t attrMask = AttrMaskFromLayout(layout);

    if (bgfx::getAvailTransientVertexBuffer(vertexCount, bLayout) < vertexCount) return;
    if (indexed && bgfx::getAvailTransientIndexBuffer(indexCount) < indexCount) return;

    bgfx::TransientVertexBuffer tvb;
    bgfx::allocTransientVertexBuffer(&tvb, vertexCount, bLayout);
    std::memcpy(tvb.data, verts, uint32_t(layout.stride) * vertexCount);

    bgfx::TransientIndexBuffer tib;
    if (indexed)
    {
        bgfx::allocTransientIndexBuffer(&tib, indexCount);
        std::memcpy(tib.data, indices, indexCount * sizeof(uint16_t));
    }

    const bgfx::ProgramHandle prog = ApplyDrawState(attrMask);
    bgfx::setVertexBuffer(0, &tvb);
    if (indexed) bgfx::setIndexBuffer(&tib);
    bgfx::submit(0, prog);
}
```

Post-refactor `Draw_Indexed`:

```cpp
void BgfxBackend::Draw_Indexed(unsigned, unsigned, unsigned startIndex, unsigned primitiveCount)
{
    if (!m_initialized || !m_pipelineInit
        || !bgfx::isValid(m_currentVB) || !bgfx::isValid(m_currentIB)) return;

    const bgfx::ProgramHandle prog = ApplyDrawState(m_vbAttrMask);
    bgfx::setVertexBuffer(0, m_currentVB);
    bgfx::setIndexBuffer(m_currentIB, startIndex, primitiveCount * 3);
    bgfx::submit(0, prog);
}
```

`Draw` is the non-indexed analogue. The two state-setup blocks (~60 lines
each) now reduce to a single `ApplyDrawState` call apiece — net deletion
of ~100 lines despite adding a whole new draw method.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK. |
| `cmake --build build_bgfx --target corei_bgfx` | OK — zero new warnings. |
| `cmake --build build_bgfx --target tst_bgfx_dynamic` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_{clear,triangle,uber,mesh,texture,bimg,multitex,multilight,dynamic}` (all 9) | OK. |
| `./tests/tst_bgfx_dynamic/…` | Waving sine/cosine grid, per-frame shape change visible, lit smoothly (normals animate with the surface); `tst_bgfx_dynamic: PASSED`. |
| All 8 prior bgfx tests | PASSED — the `ApplyDrawState` refactor didn't change visual output. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly — zero production call sites affected. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::` in production code | Zero (only doc mentions). |
| `#include.*<bgfx` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero. |
| `createVertexBuffer` / `createIndexBuffer` in a per-frame loop | Zero — both the existing persistent path and the new dynamic path are structurally single-shot (persistent: once per `Set_*_Buffer`; dynamic: zero — only transient allocs). |

## Deferred

| Item | Stage |
|---|---|
| Line-strip / triangle-strip / point primitives — `Draw_Triangles_Dynamic` is triangles-only by name and contract. A future `Draw_Lines_Dynamic` / `Draw_Points_Dynamic` can slot in next to it without breaking this one | later, when the adapter surfaces a caller |
| 32-bit index support — current signature hard-codes `const uint16_t*`. Particle systems with > 65k verts would need a separate overload. Generals doesn't hit that; leave until a caller forces it | later |
| Lock-style API (`Begin_Dynamic` → caller-writable pointer → `End_Dynamic`) that matches DX8's lock/unlock shape more literally, for DX8Wrapper call sites that currently do partial fills and then decide count at end-of-lock | 5h, if the adapter's cost of copy-once becomes measurable |
| Multi-view / layered-render submission — 5m submits to view 0 only, same as every other backend call so far | later |
| DX8Wrapper → IRenderBackend adapter (per-frame `DX8Wrapper::Draw_Strip` → `Draw_Triangles_Dynamic`; `DX8Wrapper::Lock_Dynamic_Vertex_Buffer` → staging buffer → `Draw_Triangles_Dynamic`) | 5h |
