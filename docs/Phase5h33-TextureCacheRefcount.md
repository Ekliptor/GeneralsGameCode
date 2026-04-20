# Phase 5h.33 — BgfxTextureCache refcounting + TextureBaseClass integration

Companion doc to `docs/CrossPlatformPort-Plan.md`. Thirty-third
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h32-SurfaceTextureCtorWireup.md`.

## Scope gate

`BgfxTextureCache` (Phase 5h.3) was a simple `path → handle` map.
`Get_Or_Load_File` deduplicated loads. `Release(path)` did a hard
destroy. That's insufficient for real game code:

- Multiple `TextureClass` instances can refer to the same file
  (e.g. a shared "dirt.dds" used by many drawables). With 5h.27
  each of those TextureClass instances calls `Get_Or_Load_File`
  and stashes the returned handle. But nobody was releasing.
- If the dtor of *any* TextureClass called `Release(path)`, it
  would immediately destroy the handle that other live
  TextureClass instances still have a copy of.
- `TextureBaseClass::Invalidate()` is called on cache-managed
  textures for asset-reload scenarios. In bgfx mode there was
  no handle-side unwind; the stale handle would just keep
  getting sampled.

5h.33 fixes both problems. The cache learns refcounts; the
TextureBaseClass dtor and `Invalidate()` learn to route file-
loaded handles through the cache while procedural handles go
straight to `Destroy_Texture`.

## Locked decisions

| Question | Decision |
|---|---|
| Refcount location | In the cache, not in `TextureBaseClass`. The cache is already the single point of truth for "who owns this handle"; having the refcount next to the handle in the same `Entry` struct keeps the invariant local. `TextureBaseClass` just knows it holds a copy of the handle and calls `Release(path)` when done. |
| `Get_Or_Load_File` semantics | Now implicitly acquires (refcount +=1). Callers who drop the returned handle without calling `Release` leak a refcount (entry stays in map forever). The convention is: every `Get_Or_Load_File` call must be matched by a `Release`, unless `Clear_All` is going to sweep. |
| `Release` semantics | Decrements refcount; destroys the bgfx texture + removes the map entry only when refcount reaches zero. Still a safe no-op on unknown paths. |
| `Clear_All` semantics | Unchanged — destroys everything regardless of refcount. This is the shutdown path; leaked refcounts from TextureClass instances that somehow survived past backend shutdown shouldn't keep the map alive. |
| New `Ref_Count(path)` diagnostic | Yes, for tests. Returns 0 for unknown paths (indistinguishable from "cached with refcount 0," which can't actually happen since zero-refcount triggers removal). |
| Distinguishing file-loaded vs procedural in the dtor | `!Get_Full_Path().Is_Empty()` means cache-loaded (`TextureClass::Init` only calls the cache when `Get_Full_Path()` is non-empty). Empty full-path → procedural → destroy directly. Render target handles are already handled separately (5h.29) via the `BgfxRenderTarget != 0` branch. |
| `Invalidate` on procedural textures | Still a no-op — the DX8 path has `if (IsProcedural) return;` and the bgfx path inherits that early-return. Procedural textures don't get re-loaded from a source; invalidating them would just lose the pixels. |
| `Invalidate` on cache-loaded textures | Decrements refcount, clears `BgfxTexture`, sets `Initialized = false`. Next `Apply()` calls `Init()` which calls `Get_Or_Load_File` again → refcount +1 → handle repopulated. If this was the last reference the first time, the texture gets destroyed and re-decoded on next use; if there were others, they stay in the cache and the same handle comes back. Cost is one map lookup either way. |
| `Invalidate` on RT textures | Unchanged — not touched. RTs don't have a `Full_Path` (they're procedural from the POV of the ctor) and they get destroyed at dtor time via the 5h.29 branch. |
| DX8 impact | None — the dtor/Invalidate edits are inside `#ifndef RTS_RENDERER_DX8`. The cache header/source live only in bgfx-mode code paths. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxTextureCache.h` | Updated doc comment; added `Ref_Count(path)` to the public API. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxTextureCache.cpp` | Changed `unordered_map<string, uintptr_t>` → `unordered_map<string, Entry>` where `Entry = {uintptr_t handle, unsigned refCount}`. `Get_Or_Load_File` increments on hit. `Release` decrements; destroys + erases on zero. `Clear_All` unchanged. Added `Ref_Count` body. |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Added `#include "BGFXDevice/Common/BgfxTextureCache.h"` to the early bgfx include block. `~TextureBaseClass`: added the cache/procedural routing branch. `TextureBaseClass::Invalidate()`: added a matching bgfx-mode branch that `Release`-es cache-owned handles. |
| `tests/tst_bgfx_texcache/main.cpp` | Updated invariants 1–3 + 5 to exercise refcount semantics; added `Ref_Count` assertions. |

### The routing branches

`~TextureBaseClass` — replaces "do nothing to `BgfxTexture`" with:

```cpp
else if (BgfxTexture != 0)
{
    const StringClass& path = Get_Full_Path();
    if (!path.Is_Empty())
        BgfxTextureCache::Release(path.str());          // cache-owned
    else if (IRenderBackend* b = RenderBackendRuntime::Get_Active())
        b->Destroy_Texture(BgfxTexture);                // procedural
    BgfxTexture = 0;
}
```

`TextureBaseClass::Invalidate()` — adds after the DX8 `D3DTexture` release:

```cpp
#ifndef RTS_RENDERER_DX8
if (BgfxTexture != 0 && BgfxRenderTarget == 0) {
    const StringClass& path = Get_Full_Path();
    if (!path.Is_Empty())
        BgfxTextureCache::Release(path.str());
    BgfxTexture = 0;
}
#endif
```

No call to `backend->Destroy_Texture` on the procedural branch here —
`Invalidate` is for cache-reload scenarios, and procedural textures
were already filtered out above by `if (IsProcedural) return;`.

### The cache `Entry` struct

```cpp
struct Entry {
    uintptr_t handle;
    unsigned  refCount;
};
```

Trivial POD. No move/copy concerns — `unordered_map<string, Entry>`
stores Entries by value; the lookup returns a reference; refcount
mutations happen through `it->second.refCount`.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21,
bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. `TextureBaseClass` dtor + `Invalidate` now call into `BgfxTextureCache::Release`. |
| `cmake --build build_bgfx --target corei_bgfx` | OK. `BgfxTextureCache` now owns an `Entry` struct per path. |
| `cmake --build build_bgfx --target tst_bgfx_texcache` | OK. |
| `tst_bgfx_texcache` | PASSED — all 9 invariants (6 original + 3 new refcount assertions). |
| All 22 bgfx tests | PASSED — no regressions. |
| `cmake -S . -B cmake-build-release -DRTS_RENDERER=dx8` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `BgfxTextureCache::Release` callers | Three — `BgfxBootstrap::Shutdown`'s `Clear_All` path (unchanged), `TextureBaseClass::~TextureBaseClass`, `TextureBaseClass::Invalidate`. |
| `BgfxTextureCache::Get_Or_Load_File` callers | Two — `TextureClass::Init` (production), `tst_bgfx_texcache` (test). |
| `Ref_Count` callers | One — `tst_bgfx_texcache`. |

## What this phase buys

The cache is now a real, balanced refcounted loader:

- Loading the same DDS file from two different `TextureClass`
  instances shares the GPU upload (handle dedup — unchanged from 5h.3)
  **and** keeps the handle alive as long as either TextureClass
  holds it (new in 5h.33).
- Destroying one TextureClass doesn't yank the handle out from
  under the other. When the last one goes, the handle is destroyed.
- `Invalidate()` on a cache-loaded texture drops its refcount and
  re-fetches via the cache on next `Apply()` — the normal
  asset-reload path now works end-to-end in bgfx mode.
- Procedural / surface-ctor'd textures (5h.28, 5h.32) and RT
  textures (5h.29) each have their own disposal paths that don't
  confuse with the cache.

## Deferred

| Item | Stage | Note |
|---|---|---|
| Sampler-state routing (`TextureFilterClass::Apply` → bgfx sampler flags) | 5h.34 | Mipmap filter, wrap mode, anisotropy currently ignored in bgfx mode; texture binds use bgfx's default sampler |
| Format-aware `Update_Texture` (non-RGBA8 surface paths) | later | Still pending since 5h.31 |
| Mip-chain generation for cache loads that come with pre-baked mips | later | bimg can decode DDS mip chains; the backend wires them through `Create_Texture_From_Memory` already. No action needed until a caller actually depends on this. |
| Thread-safe cache | later | Texture loads are main-thread in Generals today; still need to revisit when/if the loader thread (5h.22's deferred) comes back |

## Meta-status on the 5h arc

- **33 sub-phases** complete (5h.1 → 5h.33).
- **22 bgfx tests**, all green. `tst_bgfx_texcache` now carries
  refcount-specific invariants on top of the original deduplication
  invariants.
- `IRenderBackend` still 26 virtual methods — no new backend
  capability this phase.
- Adapter-side texture lifetime is now balanced across three
  ownership paths: file-loaded via cache (refcounted), procedural
  via `Create_Texture_RGBA8` (owned 1:1 by the `TextureClass`),
  render target via `Create_Render_Target` (also 1:1).

The remaining 5h phases target sampler-state + format coverage +
integration verification rather than new capabilities.
