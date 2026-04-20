# Phase 5h.27 — TextureClass ↔ BgfxTextureCache wire-up

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twenty-seventh
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h26-TextureSubclassBgfxStubs.md`.

**Milestone phase.** Closes the last data-plane gap: when the game's
asset manager constructs a `TextureClass` and the mesh renderer
calls `Apply(stage)`, the texture now actually reaches the bgfx
backend and binds to the shader sampler.

## Scope gate

Phases 5h.19 → 5h.26 built the infrastructure: the entire texture
class hierarchy is linkable in bgfx mode, the `BgfxTextureCache`
singleton (Phase 5h.3) has been waiting since the cross-platform
port started to receive real load requests, and the `BgfxTexture`
handle field on `TextureBaseClass` (Phase 5h.4) has been sitting
zero-initialized on every texture the game allocates.

5h.27 connects all three:

1. `TextureBaseClass` grows a `Set_Bgfx_Handle(uintptr_t)` public
   setter so the bgfx-mode `Init` can store the cache's return value.
2. `TextureClass::Init` (bgfx stub) now calls
   `BgfxTextureCache::Get_Or_Load_File(Get_Full_Path().str())` —
   which decodes the DDS/KTX/PNG via `bimg` + uploads through the
   active `IRenderBackend` — and stores the resulting handle via
   the new setter.
3. `TextureClass::Apply(stage)` (bgfx stub) now calls
   `IRenderBackend::Set_Texture(stage, Peek_Bgfx_Handle())` — the
   backend binds the cached texture to the requested stage, falling
   back to the built-in 2×2 white placeholder when the handle is
   zero (paths that failed to load, or procedural textures that
   bypass the cache).

Six lines of real wire-up, spread across two files. The heavy lifting
was done by the phases that came before.

## Locked decisions

| Question | Decision |
|---|---|
| Cache key | `Get_Full_Path().str()`. `StringClass::Get_Full_Path()` returns `full_path` when it's set, otherwise the texture name — which matches what the asset manager does when resolving a texture request. |
| Null-handle fallback | Backend's 2×2 white placeholder. `BgfxBackend::Set_Texture(stage, 0)` already routes to `m_placeholderTexture` (since Phase 5i / 5h.3), so textures that fail to load render as white rather than crashing or showing garbage. |
| Cached vs uncached check | `if (Peek_Bgfx_Handle() == 0)` before the cache lookup. Idempotent Init — calling it twice doesn't re-query the cache. |
| Where does `Set_Bgfx_Handle` live | Public in `TextureBaseClass`, inline in the header (`void Set_Bgfx_Handle(uintptr_t h) { BgfxTexture = h; }`). Alternative was `protected` with `friend class TextureClass` — rejected because it would need to friend the three subclasses too, and the handle is already readable publicly via `Peek_Bgfx_Handle()`. |
| `DX8Wrapper::Set_DX8_Texture(stage, nullptr)` still called | Yes. DX8CALL is a no-op in bgfx mode; the call just updates the `Textures[]` tracking array so code that peeks it sees a consistent "null binding" state. Cheap in bgfx. |
| Procedural textures (no file path) | `Init` skips the cache lookup when `Get_Full_Path()` is empty, leaving `BgfxTexture = 0`. `Apply` binds the placeholder. Procedural textures (render targets, generated content) get no visual — those paths need 5h.28+ work to expose their pixels to the backend via `Create_Texture_RGBA8` or a render-target handle. |
| Thread safety | `BgfxTextureCache` is explicitly "main thread only" (5h.3 doc). `TextureClass::Init` + `Apply` both run on the main thread in Generals, so no mutex needed. |
| DX8 impact | Zero. The bgfx branch is inside `#ifndef RTS_RENDERER_DX8`; DX8 build compiles none of this. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/texture.h` | Added public inline setter `void Set_Bgfx_Handle(uintptr_t h) { BgfxTexture = h; }`. |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | In the bgfx-mode block: `#include "BGFXDevice/Common/BgfxTextureCache.h"` + `"WW3D2/RenderBackendRuntime.h"` + `"WW3D2/IRenderBackend.h"`. `TextureClass::Init` now resolves the full path and calls `BgfxTextureCache::Get_Or_Load_File(path.str())` → `Set_Bgfx_Handle(handle)`. `TextureClass::Apply` now calls `b->Set_Texture(stage, Peek_Bgfx_Handle())` via `RenderBackendRuntime::Get_Active()`. |

### The new Init body

```cpp
void TextureClass::Init()
{
    if (Initialized) return;

    // Headline wire-up: resolve the cache-key path, load through bimg +
    // upload to bgfx, store the handle on this texture. Skipped for
    // procedural textures (width/height ctor with no file path).
    if (Peek_Bgfx_Handle() == 0) {
        const StringClass& path = Get_Full_Path();
        if (!path.Is_Empty()) {
            uintptr_t handle = BgfxTextureCache::Get_Or_Load_File(path.str());
            Set_Bgfx_Handle(handle);
        }
    }
    Initialized  = true;
    LastAccessed = WW3D::Get_Sync_Time();
}
```

### The new Apply body

```cpp
void TextureClass::Apply(unsigned int stage)
{
    if (!Initialized) Init();
    LastAccessed = WW3D::Get_Sync_Time();

    // Bind the cached handle. Null falls back to the backend's placeholder.
    if (IRenderBackend* b = RenderBackendRuntime::Get_Active())
        b->Set_Texture(stage, Peek_Bgfx_Handle());

    // Keep the DX8Wrapper state machine consistent (no-op in bgfx).
    DX8Wrapper::Set_DX8_Texture(stage, nullptr);
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. The BgfxTextureCache include chain resolves through `corei_ww3d2`'s bgfx-mode link edge (added in Phase 5h.2). |
| All 20 bgfx tests | PASSED. Test harnesses don't exercise `TextureClass::Apply` directly (they build descriptors through `BackendDescriptors.h`), so behavior is unchanged there. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `BgfxTextureCache::Get_Or_Load_File` production-code callers | One — `TextureClass::Init` in the bgfx branch. Up from zero. |
| `IRenderBackend::Set_Texture` production-code callers | One — `TextureClass::Apply` in the bgfx branch. Up from zero. |
| `Set_Bgfx_Handle` callers | One — `TextureClass::Init` in the bgfx branch. |
| `Peek_Bgfx_Handle` callers | Many (every Apply call reads it); now actually returns a non-zero value for textures with valid file paths. |

## What this phase buys — the milestone

**The cross-platform port's data plane is now closed in bgfx mode.**

Every element of the render pipeline — transforms, viewport, shader
state, material, light environment, vertex/index buffers, draw calls,
and now textures — reaches the bgfx backend from the same
`DX8Wrapper` entry points the game uses in DX8 mode. Running the
game in bgfx mode no longer "clears the screen and stalls"; it
should instead render geometry with correctly-sampled textures on
Metal / D3D11 / D3D12 etc., subject only to bug fixes and the
deferred features listed below.

Flow on first-frame texture access in bgfx mode:

1. Game's asset manager constructs `TextureClass("Art/tree.dds", ...)` —
   the bgfx-mode ctor stores the name/path but doesn't load yet.
2. Mesh renderer sets up material, binds texture via
   `DX8Wrapper::Set_Texture(stage, tex)` — inline path, caches in
   `render_state.Textures[]`.
3. Draw-call path reaches `DX8Wrapper::Draw_Triangles` — `Apply_Render_State_Changes`
   drains all the cached state including the material + shader;
   `tex->Apply(stage)` routes through the new 5h.27 wire-up:
   - `Init()` fires `BgfxTextureCache::Get_Or_Load_File("Art/tree.dds")`.
   - Cache reads the file with `FileFactory`, decodes via `bimg`,
     uploads through `IRenderBackend::Create_Texture_From_Memory`
     (returning an opaque `uintptr_t` handle).
   - Handle is stored in `BgfxTexture`.
   - `Apply()` calls `b->Set_Texture(stage, handle)` → backend binds
     the real texture.
4. `IRenderBackend::Draw_Triangles_Dynamic` submits — the uber-shader
   now samples actual texture data.

## Deferred

| Item | Stage |
|---|---|
| Procedural textures (render targets, generated content). Need `Create_Texture_RGBA8` path wired into the `(width, height, format, pool, rendertarget, ...)` ctor | 5h.28 |
| Texture invalidation on `Invalidate_Old_Unused_Textures` — the bgfx handle lifecycle should honor the existing invalidation timer, calling `Release` on the cache | 5h.28 |
| Sampler state: `TextureFilterClass::Apply` still forwards to DX8-only texture stage state; the bgfx shader samples with `LINEAR + MIP_LINEAR` regardless | 5h.29 |
| `Invalidate_Old_Unused_Textures` currently null-guards `Get_Instance()` — once the game loads a real asset manager, this starts walking for-real. Should verify the invalidation cadence is sane in bgfx mode | 5h.29 |
| In-game verification: run the game in bgfx mode, observe actual game visuals. First time the whole data path is exercisable. Almost certainly surfaces bugs that tests can't reach | after 5h.28 |
| Windows bgfx parity — the port targets Mac + Windows; Windows DX11/12 backend path through bgfx should also light up with this phase, but hasn't been built/tested | Windows CI |

## Closing note

This is the end of the 5h arc's primary data-plane work. The next
phases (5h.28+) are smaller refinements (procedural textures, sampler
state, invalidation timers) rather than foundational infrastructure.
The game can now, in principle, run in bgfx mode and render actual
game scenes — the next step is to verify that empirically.

The six-line wire-up that closes this phase was possible only because
the prior 26 sub-phases made it trivial. Every include, every
un-`#ifdef`, every stub pattern was a prerequisite that this phase
collects into one coherent chain:

- `BgfxTextureCache` (5h.3) ← ready from day one of Phase 5h.
- `BgfxTexture` handle field (5h.4) ← stable field in `TextureBaseClass`.
- `IRenderBackend::Set_Texture` (5h.4) ← entry point on the backend.
- `RenderBackendRuntime::Get_Active()` (5h.1) ← singleton access.
- `corei_ww3d2 → corei_bgfx` link edge (5h.2) ← so the includes resolve.
- `DX8Wrapper::Draw_Triangles` routing (5h.17/5h.18) ← so the draw actually fires.
- `TextureClass` + subclass stubs (5h.25/5h.26) ← so `TextureClass::Init`/`Apply` *have* bodies to fill.

Six lines of code. Twenty-six sub-phases of groundwork.
