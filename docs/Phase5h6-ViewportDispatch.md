# Phase 5h.6 — Viewport dispatch

Companion doc to `docs/CrossPlatformPort-Plan.md`. Sixth sub-phase of
the DX8Wrapper adapter. Follows `Phase5h5-ProjectionDispatch.md`.

## Scope gate

5h.1 through 5h.5 routed the frame lifecycle (Begin / End / Clear), the
render-backend runtime singleton, the texture cache, the bootstrap, and
all three transform matrices (world / view / projection) through the
`IRenderBackend` seam. The one other per-frame stateful knob the game
touches in the adapter's surface area is `Set_Viewport` — D3D8's way of
restricting draws to a sub-rect of the current render target. Common
uses: the minimap viewport, the shroud RT pass, letterboxed cutscenes,
and any tool that renders into a subregion.

5h.6 adds `IRenderBackend::Set_Viewport`, implements it in `BgfxBackend`,
wires `DX8Wrapper::Set_Viewport(D3DVIEWPORT8*)` to translate to it, and
proves the scissor semantics via a capture-based pixel assertion.

Draw-call routing, material / shader / light translators, and texture
bridging are still deferred (they depend on classes not compiled in
bgfx mode — `VertexBufferClass`, `IndexBufferClass`, `ShaderClass`,
`VertexMaterialClass`, `TextureClass`).

## Locked decisions

| Question | Decision |
|---|---|
| Interface signature | `Set_Viewport(int16_t x, int16_t y, uint16_t width, uint16_t height)`. Matches bgfx's `setViewRect` parameter types (`uint16_t` extents, signed `int16_t` origin to express negative overhang). Not `uint32_t` like D3DVIEWPORT8 — production RTs fit comfortably under 16 bits; clamping on the adapter side is cheap and avoids widening the abstraction. |
| Origin convention | Top-left of the current render target in pixels. Matches both D3D8 and bgfx conventions; no flip needed. |
| `width == 0 \|\| height == 0` sentinel | Resets the viewport to cover the full current target. Useful when the game's `Set_Viewport(nullptr)` convention fires — DX8's `IDirect3DDevice8::SetViewport(nullptr)` has the same "fill RT" semantic. The bgfx backend looks at `m_currentView`: if it's 0 it uses the cached backbuffer dims, otherwise it finds the matching `BgfxRenderTarget` in `m_renderTargets`. |
| Negative x / y | Clamped to 0 at the backend's `bgfx::setViewRect` boundary. `int16_t` carries the sign so the adapter can pass a small negative origin (some UI paths emit `(-1, -1)` as a "fudge inward" gesture); clamping avoids `uint16_t` wraparound that would otherwise send the viewport offscreen. |
| Per-view state | bgfx tracks viewports per view, so the draw lands on whichever RT `m_currentView` currently points at. `Set_Viewport` inside an RT pass clips that RT; after `Set_Render_Target(0)` (backbuffer), a subsequent `Set_Viewport` clips the backbuffer. No extra bookkeeping needed — bgfx's per-view state already matches D3D8's "viewport belongs to the current RT" semantic. |
| Why not use `DX8CALL(SetViewport)` in DX8 builds | The DX8 production path keeps its `DX8CALL(SetViewport(pViewport))` (in `DX8Wrapper::Set_Viewport` at line 1853) untouched. The bgfx-mode stub is a separate function body entirely — no `#ifdef` inside the DX8 implementation. |
| DX8 build impact | Zero. The DX8 branch of `Set_Viewport` is byte-identical to pre-5h.6; the new `IRenderBackend::Set_Viewport` is only referenced inside the `#else` branch. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | New pure-virtual `Set_Viewport(int16_t x, int16_t y, uint16_t w, uint16_t h)`. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Override declaration for `Set_Viewport`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Implementation: clamp negative origins to 0, handle `0×0 → full current target`, dispatch to `bgfx::setViewRect(m_currentView, …)`. |
| `Core/Libraries/Source/WWVegas/WW3D2/dx8wrapper.cpp` | Replace the bgfx-stub `Set_Viewport(CONST D3DVIEWPORT8*) {}` with a real translator: null → `0×0 reset`, otherwise forward `X / Y / Width / Height` to `backend->Set_Viewport`. |

### New files

| File | Purpose |
|---|---|
| `tests/tst_bgfx_viewport/{main.cpp, CMakeLists.txt}` | Frame-1 fills a 64×64 RT with red; frame-2 restricts the viewport to `(16,16,32,32)` and draws a full-NDC blue quad with no clear. Captures back the RT and asserts the sub-rect is blue while the surround stays red. First test in the suite that exercises per-view scissor state. |

### The backend body

```cpp
void BgfxBackend::Set_Viewport(int16_t x, int16_t y, uint16_t width, uint16_t height)
{
    if (!m_initialized) return;

    if (width == 0 || height == 0) {
        uint16_t w = static_cast<uint16_t>(m_width);
        uint16_t h = static_cast<uint16_t>(m_height);
        if (m_currentView != 0) {
            for (auto* rt : m_renderTargets)
                if (rt && rt->viewId == m_currentView) { w = rt->width; h = rt->height; break; }
        }
        bgfx::setViewRect(m_currentView, 0, 0, w, h);
        return;
    }

    uint16_t ox = (x < 0) ? 0 : static_cast<uint16_t>(x);
    uint16_t oy = (y < 0) ? 0 : static_cast<uint16_t>(y);
    bgfx::setViewRect(m_currentView, ox, oy, width, height);
}
```

### The adapter body

```cpp
void DX8Wrapper::Set_Viewport(CONST D3DVIEWPORT8* pViewport)
{
    IRenderBackend* b = RenderBackendRuntime::Get_Active();
    if (b == nullptr) return;
    if (pViewport == nullptr) { b->Set_Viewport(0, 0, 0, 0); return; }
    b->Set_Viewport(
        static_cast<int16_t>(pViewport->X),
        static_cast<int16_t>(pViewport->Y),
        static_cast<uint16_t>(pViewport->Width),
        static_cast<uint16_t>(pViewport->Height));
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK — `tests/CMakeLists.txt` picks up the new subdir, `shaderc` cache reused. |
| `cmake --build build_bgfx --target corei_bgfx` | OK. |
| `cmake --build build_bgfx --target z_ww3d2` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_viewport` | OK. |
| `./build_bgfx/tests/tst_bgfx_viewport/tst_bgfx_viewport` | `tst_bgfx_viewport: PASSED` — 11 sampled pixels (6 outside sub-rect assert red, 5 inside assert blue) all match within ±3 LSB. |
| All 17 bgfx tests (the 16 prior + the new one) | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly. DX8 full compile is Windows-only — `d3d8types.h` absent on macOS SDK — so reconfigure-clean is the usual sanity signal. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::Set_Viewport` callers in production code | One — `DX8Wrapper::Set_Viewport` bgfx branch. |
| `bgfx::setViewRect` call sites | Four — backend `Init`, backend `Reset`, `Create_Render_Target`, and the new `Set_Viewport`. The first three set the per-view rect on creation; the fourth updates it per-frame as the game drives. |
| DX8 production `Set_Viewport` | Unchanged — line 1853 still runs `DX8CALL(SetViewport(pViewport))`. |

### What the test proves

The viewport test is the second pixel-assertion test in the suite
(after `tst_bgfx_capture`) and the first to exercise a per-view state
change between frames:

1. **Retained per-view clear works as expected.** Frame 1 sets
   `setViewClear(RED)` + touch + `frame()`, landing on the full RT.
2. **Set_Viewport's scissor survives into the next bgfx::frame().**
   Frame 2 doesn't clear; the earlier red remains outside the scissor.
3. **Scissor region matches the declared sub-rect.** The blue draw
   lands exactly inside `(16,16)..(47,47)`; pixels one row outside
   remain red.

That triple is the complete proof that `bgfx::setViewRect` semantics
match the D3D8 viewport model the game is written against — the
minimap pass, shroud RT, letterboxed cutscenes, and any future sub-RT
work will all land correctly without per-call-site special-casing.

## Deferred

Same five items as 5h.5; 5h.6 is orthogonal to them.

| Item | Stage |
|---|---|
| Draw-call routing: `Draw_Triangles` / `Draw_Strip` / `Draw` → `IRenderBackend::Draw_Indexed`. Needs `VertexBufferClass` / `IndexBufferClass` compiled in bgfx mode first | 5h.7 |
| `Set_Shader(ShaderClass)` → `ShaderStateDesc` translator | 5h.8 |
| `Set_Material(VertexMaterialClass)` → `MaterialDesc` translator | 5h.8 |
| `Set_Light(LightClass)` + `Set_Light_Environment(LightEnvironmentClass*)` → `IRenderBackend::Set_Light`. LightClass is in WW3D2 core and likely the most tractable of the remaining translators | 5h.7 |
| `TextureClass::Init` → `BgfxTextureCache::Get_Or_Load_File`. Blocked on un-commenting `texture.cpp` in bgfx per-game builds | 5h.9 |

After this phase, the DX8Wrapper's set-state surface area that lives in
the adapter (transforms + viewport + lifecycle) is entirely routed
through the seam. Everything that's still a no-op stub depends on
non-compiled-in-bgfx-mode classes — the remaining adapter work is
unblocking those compile units, not writing more wiring.
