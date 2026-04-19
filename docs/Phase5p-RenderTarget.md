# Phase 5p — Off-screen render targets

Companion doc to `docs/CrossPlatformPort-Plan.md`. Fifteenth stage of
Phase 5 (Renderer bgfx backend). Follows `Phase5o-Fog.md`.

(Phase 5h — DX8Wrapper → IRenderBackend adapter — is still open; 5p
closes the last **capability** gap that blocked the adapter. Production
Generals renders into multiple RTs per frame: shadow map, water
reflection, water refraction, screen-space effects, mini-map.
`IRenderBackend` can now express those passes.)

## Objective

Let `IRenderBackend` callers create off-screen render targets, bind them
as the current render destination, sample the result as a regular texture
in a subsequent pass, and tear them down cleanly. The bgfx backing is a
`FrameBuffer` with a BGRA8 color attachment and optional D24S8 depth,
sitting on its own unique view ID; the backbuffer keeps view 0.

The contract is intentionally small — four methods, one opaque handle
type, no new shader code. Every existing uber-program already samples
whatever is bound via `Set_Texture(0, h)`, which is all the quad-with-RT
pass needs.

## Locked decisions

| Question | Decision |
|---|---|
| Handle shape | Opaque `uintptr_t` pointing to a heap `BgfxRenderTarget`. Same shape as texture handles, but a distinct type family — passing an RT handle to `Destroy_Texture` or a texture handle to `Destroy_Render_Target` fails-closed (the lookup simply doesn't find it). The RT owns a heap-allocated `bgfx::TextureHandle*` wrapping `bgfx::getTexture(fb, 0)` so the color attachment can be bound via the existing `Set_Texture` path without any new sampler API. |
| Who owns the attached textures | bgfx, via `createFrameBuffer(..., destroyTextures = true)`. `bgfx::destroy(fb)` disposes the color + depth textures transitively; we only manage the heap wrapper we allocated around the color attachment for `Set_Texture`. |
| Color format | BGRA8. Matches the default backbuffer format bgfx picks on Metal/D3D and avoids format conversion when the RT's output ends up on screen. A future Generals shadow-map pass would want a 16-bit float depth-only attachment — that's a different `Create_Render_Target_Format` overload for later, not this phase. |
| Depth format | D24S8, optional. Shadow-map-only passes will eventually want depth-as-texture readback (`BGFX_TEXTURE_RT_WRITE_ONLY` vs. the read-sampled variant); 5p ships with `WRITE_ONLY` because the only caller right now is the picture-in-picture smoke test. |
| View ID allocation | Monotonic counter starting at 1. View 0 stays the backbuffer. bgfx supports 256 views; the counter is uint16_t with plenty of headroom. A freed RT's view ID is not recycled — again, 256 headroom; recycling is an optimization we don't need yet. |
| Submit ordering | `bgfx::setViewOrder(0, N+1, [rt1, rt2, …, rtN, 0])` rebuilt on every `Create_Render_Target` / `Destroy_Render_Target`. Default bgfx view ordering is by ascending ID, which would submit the backbuffer (view 0) first and any RTs after — exactly backwards for a "render into RT, then sample in backbuffer" flow. Explicit reorder puts every RT before the backbuffer so the backbuffer pass reads a live frame. |
| Where does `Set_Render_Target(0)` reset to | The backbuffer (view 0). `m_currentView = 0`; subsequent `Begin_Scene` / `Clear` / `Draw_*` hit view 0. The 5g-through-5o draw paths no longer hard-code view 0 — they submit to `m_currentView`, threaded end-to-end. |
| Transform-uniform invalidation across a view switch | Set `m_viewProjDirty = true` inside `Set_Render_Target`. Every view has its own `setViewTransform` state in bgfx, so the first draw into a newly-bound view needs a fresh view-transform upload; the existing `m_viewProjDirty` mechanism already handles "upload once per dirty cycle". |
| Smoke-test `Draw*` methods | Untouched. `DrawSmokeTriangle` / `DrawSmokeSolidQuad` / etc. still call `bgfx::setViewTransform(0, …)` + `bgfx::submit(0, …)` directly — they only ever render to the backbuffer, so pinning to view 0 is correct. Threading `m_currentView` through them would be dead weight. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | 4 new pure-virtuals: `Create_Render_Target`, `Destroy_Render_Target`, `Set_Render_Target`, `Get_Render_Target_Texture`. Contract spelled out in-place (handle 0 = backbuffer, texture lifetime owned by RT, etc.). |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Declare the overrides. Add nested `BgfxRenderTarget` POD (fb + texWrap + viewId + w + h), `std::vector<BgfxRenderTarget*>`, `m_currentView`, `m_nextViewId`, private `UpdateViewOrder()`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Thread `m_currentView` through `Begin_Scene`, `Clear`, and the three production draw paths (`Draw_Indexed`, `Draw`, `Draw_Triangles_Dynamic`) in place of the hard-coded `0`s. In `ApplyDrawState`, `setViewTransform` now targets `m_currentView`. Implement the four RT methods. `DestroyPipelineResources` sweeps any RTs the caller forgot. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_rendertarget)`. |

### New files

| Path | Purpose |
|---|---|
| `tests/tst_bgfx_rendertarget/main.cpp` | Two-pass picture-in-picture. Pass 1 renders a lit + checker-textured spinning cube into a 256×256 `FrameBuffer` (BGRA8 + D24S8) on view 1; pass 2 clears the backbuffer to a gray surround and draws a 1.6×1.2-unit quad filling ~60% of the 800×600 window, textured with the RT's color attachment. Destroy_Render_Target is called before Shutdown, so the teardown path is exercised. |
| `tests/tst_bgfx_rendertarget/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`. |
| `docs/Phase5p-RenderTarget.md` | This doc. |

### Implementation highlight — `Create_Render_Target`

```cpp
uintptr_t BgfxBackend::Create_Render_Target(uint16_t w, uint16_t h, bool hasDepth)
{
    bgfx::TextureHandle attachments[2];
    uint8_t count = 0;
    attachments[count++] = bgfx::createTexture2D(w, h, false, 1,
        bgfx::TextureFormat::BGRA8, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
    if (hasDepth)
        attachments[count++] = bgfx::createTexture2D(w, h, false, 1,
            bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT_WRITE_ONLY);

    bgfx::FrameBufferHandle fb = bgfx::createFrameBuffer(count, attachments, /*destroyTextures=*/true);
    auto* rt = new BgfxRenderTarget{ fb, new bgfx::TextureHandle(bgfx::getTexture(fb, 0)),
                                     m_nextViewId++, w, h };
    bgfx::setViewFrameBuffer(rt->viewId, fb);
    bgfx::setViewRect(rt->viewId, 0, 0, w, h);
    m_renderTargets.push_back(rt);
    UpdateViewOrder();
    return reinterpret_cast<uintptr_t>(rt);
}
```

### `UpdateViewOrder`

```cpp
std::vector<bgfx::ViewId> order;
for (auto* rt : m_renderTargets) order.push_back(rt->viewId);
order.push_back(0);                       // backbuffer last
bgfx::setViewOrder(0, uint16_t(order.size()), order.data());
```

Called once at every create / destroy. Cheap; the cost is bounded by the
number of RTs, which is small (Generals' worst frame has ≤5).

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK. |
| `cmake --build build_bgfx --target corei_bgfx` | OK — zero new warnings. |
| `cmake --build build_bgfx --target tst_bgfx_rendertarget` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_{clear,triangle,uber,mesh,texture,bimg,multitex,multilight,dynamic,alphatest,fog,rendertarget}` (all 12) | OK. |
| `./tests/tst_bgfx_rendertarget/…` | Backbuffer shows an 80% × 60% quad textured with the spinning cube rendered in the offscreen pass; gray surround around it; `tst_bgfx_rendertarget: PASSED`. |
| `./tests/tst_bgfx_{clear..fog}/…` (all 11 prior tests) | PASSED — the `m_currentView` thread-through left non-RT callers identical to 5o output. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly — zero production call sites affected. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::` in production code | Zero (only doc mentions). |
| `#include.*<bgfx` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero. |
| `bgfx::submit(0,` anywhere in the production draw paths | Zero — `Draw_Indexed`, `Draw`, `Draw_Triangles_Dynamic` all use `m_currentView` now. The smoke-test `DrawSmoke*` methods intentionally keep submitting to view 0. |
| `bgfx::createFrameBuffer` callers | One — `BgfxBackend::Create_Render_Target`. |

## Deferred

| Item | Stage |
|---|---|
| Depth-as-texture readback (shadow maps need to sample depth). `bgfx::getTexture(fb, 1)` + a depth-read format when created with `BGFX_TEXTURE_READ_BACK` instead of `RT_WRITE_ONLY`. One overload | 5h+ when a shadow-map caller materializes |
| Float / half-float color formats for HDR compositing | later |
| MRT (multiple color attachments) for G-buffer-style passes. `bgfx::createFrameBuffer` already supports N attachments; our API currently hard-codes one color + optional depth | later |
| Cube RTs for reflection probes | later |
| Mipmap generation on RT color (for glossy reflections) | later |
| `Capture_Frame` CPU readback — takes the current backbuffer or any RT and memcpys pixels into a caller buffer. Useful for golden-image testing the 5h adapter. `bgfx::readTexture` + frame-sync wait | 5q candidate |
| DX8Wrapper → IRenderBackend adapter — `IDirect3DSurface8::GetDesc` + the W3D "render surface" paths in `dx8wrapper.cpp` map 1:1 to `Create_Render_Target` + `Get_Render_Target_Texture` | 5h |

The bgfx backend's capability envelope now covers every renderstate,
every texture stage, every fixed-function light path, and every render
destination Generals day-to-day uses. Phase 5h is unblocked on all axes.
