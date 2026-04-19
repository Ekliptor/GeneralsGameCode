# Phase 5q — CPU frame capture via `bgfx::readTexture`

Companion doc to `docs/CrossPlatformPort-Plan.md`. Sixteenth stage of
Phase 5 (Renderer bgfx backend). Follows `Phase5p-RenderTarget.md`.

(Phase 5h — DX8Wrapper → IRenderBackend adapter — is still open; 5q lands
the infrastructure that lets us **golden-image-regression-test** the
adapter when it finally routes production meshes through the backend.
It's also the obvious path for an in-game screenshot feature.)

## Objective

Let `IRenderBackend` callers pull the contents of a render target's color
attachment back to a CPU buffer, synchronously. The first test in the
suite that asserts pixel values (rather than just "runs without
crashing") — proof that the pipeline is deterministic, and a wedge we can
use to regression-test 5h when production textures / meshes / shaders
start flowing through.

## Locked decisions

| Question | Decision |
|---|---|
| Source for capture | Render-target color attachment only. The default backbuffer's swap-chain image isn't directly readable on any modern backend; Generals-style screenshot code that wants a backbuffer snapshot should render into an RT and capture that. Keeps the API small; no hidden blit-from-backbuffer path. |
| Sync model | Synchronous from the caller's POV. Internally we pump `bgfx::frame()` up to 8 times (typical GPU→CPU latency is 1-2 frames on Metal / D3D); any more frames than that and we treat the backend as stuck and return `false`. Hot-path renderers shouldn't use this — it advances the bgfx frame counter, which stalls normal rendering. That's fine: capture is for testing and screenshots, not scene composition. |
| Pixel format | BGRA8 little-endian, matching the RT color attachment's native format. Byte layout `[B, G, R, A]` per pixel. Documented in-place on `Capture_Render_Target`. |
| Buffer ownership | Caller-provided. We take a `void*` + byte capacity, validate `byteCapacity >= width * height * 4`, and `memcpy` via bgfx's readback machinery. No allocation inside the backend, no hidden buffer lifetime to reason about. |
| Metal's RT+READ_BACK restriction | On Metal (and some D3D paths) a texture can't carry both `BGFX_TEXTURE_RT` and `BGFX_TEXTURE_READ_BACK`. We discovered this the direct way — `createFrameBuffer` just returned an invalid handle without a helpful error. Workaround: each RT lazily allocates a sibling `BGFX_TEXTURE_BLIT_DST \| BGFX_TEXTURE_READ_BACK` texture of the same size/format on first capture; capture blits RT→staging on the RT's view, then `bgfx::readTexture` reads the staging copy. |
| Lazy staging allocation | First `Capture_Render_Target` call per RT allocates its `captureTex`. RTs that never get captured don't pay the extra texture's memory (which at 64²×4B is trivial, but at 2K² is 16 MB — worth gating). `Destroy_Render_Target` and `DestroyPipelineResources` release both the staging texture and the RT's framebuffer. |
| Blit destination view | The RT's own view ID. bgfx::blit executes at that view's submit time; since the RT has been rendered to already (caller's responsibility), scheduling the blit in the same view gets it ordered correctly. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/IRenderBackend.h` | Add pure-virtual `Capture_Render_Target(handle, pixels, byteCapacity) -> bool`. Contract: synchronous, BGRA8, returns false on handle 0 / undersized buffer / backend failure. |
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxBackend.h` | Declare the override. Extend `BgfxRenderTarget` POD with `captureTex` (invalid until first capture). |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp` | Add the capture method: lazy-alloc staging texture, `bgfx::blit(rt view, staging, rt color)`, `bgfx::readTexture(staging, pixels)`, pump `bgfx::frame()` up to 8 times. `DestroyPipelineResources` and `Destroy_Render_Target` both release `captureTex` if set. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_capture)`. |

### New files

| Path | Purpose |
|---|---|
| `tests/tst_bgfx_capture/main.cpp` | Hidden-window test (`SDL_WINDOW_HIDDEN`) — we don't need a visible surface, just a valid native window handle for bgfx init. Creates a 64×64 RT, clears to `(R=0.2, G=0.8, B=0.4)` = BGRA `(102, 204, 51, 255)`, captures, asserts the four corner pixels + the center match within ±2 LSB (float→uint8 quantization slack). |
| `tests/tst_bgfx_capture/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`. |
| `docs/Phase5q-FrameCapture.md` | This doc. |

### Implementation

```cpp
bool BgfxBackend::Capture_Render_Target(uintptr_t handle, void* pixels, uint32_t byteCapacity)
{
    auto* rt = reinterpret_cast<BgfxRenderTarget*>(handle);
    // ...validate handle, texWrap, byteCapacity...

    if (!bgfx::isValid(rt->captureTex))
    {
        rt->captureTex = bgfx::createTexture2D(rt->width, rt->height, false, 1,
            bgfx::TextureFormat::BGRA8,
            BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK);
    }

    bgfx::blit(rt->viewId, rt->captureTex, 0, 0, *rt->texWrap, 0, 0, rt->width, rt->height);
    const uint32_t targetFrame = bgfx::readTexture(rt->captureTex, pixels);
    for (uint32_t i = 0; i < 8; ++i)
        if (bgfx::frame() >= targetFrame) return true;
    return false;
}
```

### The RT+READ_BACK rabbit-hole

Initial attempt added `BGFX_TEXTURE_READ_BACK` to the RT color attachment
directly:

```cpp
const uint64_t colorFlags = BGFX_TEXTURE_RT | BGFX_TEXTURE_READ_BACK | ...;
```

`createFrameBuffer` returned an invalid handle, silently. `bgfx::isValid`
is the only signal. On Metal the two flags are mutually exclusive (the
texture's backing storage mode can't satisfy both "transient attachment"
and "CPU-mapped readable" at once). The documented workaround — blit to
a separate `BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK` texture — is
what 5q does. It also happens to be what bgfx's own `10-font` example
does for screenshot capture.

The swap was a two-line diff in `Create_Render_Target` (drop READ_BACK
from the RT flags) plus the blit-then-read dance in `Capture_Render_Target`.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK. |
| `cmake --build build_bgfx --target corei_bgfx` | OK — zero new warnings. |
| `cmake --build build_bgfx --target tst_bgfx_capture` | OK. |
| `cmake --build build_bgfx --target tst_bgfx_{clear,triangle,uber,mesh,texture,bimg,multitex,multilight,dynamic,alphatest,fog,rendertarget,capture}` (all 13) | OK. |
| `./tests/tst_bgfx_capture/…` | 5 pixel samples (4 corners + center) all match clear color `(102, 204, 51, 255)` within ±2; `tst_bgfx_capture: PASSED`. |
| All 12 prior bgfx tests | PASSED — the RT color-flag change (READ_BACK removed) leaves sampling behavior identical to 5p. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly — zero production call sites affected. |

Static sweep:

| Pattern | Result |
|---|---|
| `IRenderBackend::` in production code | Zero (only doc mentions). |
| `#include.*<bgfx` outside `BGFXDevice/` and `tests/tst_bgfx_*` | Zero. |
| `bgfx::readTexture` callers | One — `BgfxBackend::Capture_Render_Target`. |
| Asserting tests in the bgfx suite (pre-5q) | Zero. (post-5q) One — `tst_bgfx_capture`, the wedge for pixel-level regression tests. |

## Deferred

| Item | Stage |
|---|---|
| Backbuffer capture — render-to-intermediate-RT, blit, readback. Straightforward wrapper around Create / Set_Render_Target / Capture, no new bgfx API needed | 5q.1 when a caller surfaces |
| Asynchronous capture — callback / promise API so hot-path screenshot calls don't stall frames. The current 8-frame pump is fine for tests and deliberate one-shot screenshots; real-time streaming (e.g. Twitch-style encode feed) would need a separate API | later |
| Non-BGRA8 readback (float HDR captures, depth captures for shadow-map debugging) | 5p.HDR |
| PNG / JPEG encoding of captured pixels — strictly a consumer concern, but a thin `Save_Capture_To_PNG` helper using bimg's encoder would be nice. Out of scope for 5q | later |
| Golden-image comparator — "load reference PNG, capture current frame, compare pixel-by-pixel within tolerance" would let 5h adapter changes be regression-tested automatically. Now unblocked by this phase | 5h.test scaffolding |
| DX8Wrapper → IRenderBackend adapter | 5h |

The bgfx backend's capability + tooling envelope is now complete. Phase
5h can proceed with both capability coverage *and* pixel-level regression
testing as needed.
