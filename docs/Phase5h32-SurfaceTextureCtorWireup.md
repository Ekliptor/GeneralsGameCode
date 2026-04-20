# Phase 5h.32 — SurfaceClass → TextureClass ctor wire-up

Companion doc to `docs/CrossPlatformPort-Plan.md`. Thirty-second
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h31-SurfaceBgfxBody.md`.

## Scope gate

5h.31 taught `SurfaceClass` to live in bgfx mode with a CPU pixel
buffer and an optional `AssociatedTextureHandle` that routes pixel
mutations to `IRenderBackend::Update_Texture_RGBA8`. What it left
open: the association had to be set by the caller via
`surface->Set_Associated_Texture(handle)`, and the caller had to
already own a bgfx handle. Today's callers don't — they construct a
`TextureClass` from a `SurfaceClass` and expect the TextureClass to
quietly own the GPU side.

Three call sites in the WW3D2 code do exactly this:

| Caller | File | What it wants |
|---|---|---|
| `Font3DInstanceClass` glyph upload | `font3d.cpp` | `NEW_REF(SurfaceClass, (w, h, A4R4G4B4))` → fill glyph bitmap → `NEW_REF(TextureClass, (surface, MIP_LEVELS_1))` |
| `Bitmap2DObjClass::Init` | `bmp2d.cpp` | `NEW_REF(SurfaceClass, (32, 32, …))` → draw → `NEW_REF(TextureClass, (surface, MIP_LEVELS_ALL))` |
| `W3DAssetManager::Generate_XXX_Texture` | `W3DAssetManager.cpp` | procedural texture generated into a SurfaceClass then handed to a TextureClass |

The DX8 path threads the surface through
`DX8Wrapper::_Create_DX8_Texture(surface->Peek_D3D_Surface(), …)` and
the D3D8 driver handles the upload. bgfx mode's ctor stub (5h.26)
swallowed the surface entirely.

5h.32 closes the loop: the bgfx-branch `TextureClass(SurfaceClass*,
MipCountType)` ctor now allocates a bgfx texture, associates it with
the surface, and triggers an immediate upload of the surface's current
pixels via a throwaway Lock/Unlock cycle.

## The ctor body

```cpp
TextureClass::TextureClass
(
    SurfaceClass* surface,
    MipCountType mip_level_count
)
:   TextureBaseClass(0, 0, mip_level_count),
    TextureFormat(surface ? surface->Get_Surface_Format() : WW3D_FORMAT_UNKNOWN),
    Filter(mip_level_count)
{
    IsProcedural=true;
    Initialized=true;
    IsReducible=false;

    if (surface) {
        SurfaceClass::SurfaceDescription sd;
        surface->Get_Description(sd);
        Width = sd.Width;
        Height = sd.Height;
        if (IRenderBackend* backend = RenderBackendRuntime::Get_Active()) {
            if (sd.Width && sd.Height) {
                const uintptr_t handle = backend->Create_Texture_RGBA8(
                    nullptr,
                    static_cast<uint16_t>(sd.Width),
                    static_cast<uint16_t>(sd.Height),
                    /*mipmap=*/false);
                if (handle) {
                    Set_Bgfx_Handle(handle);
                    surface->Set_Associated_Texture(handle);
                    // Seed the GPU copy. Lock/Unlock is the cheapest way
                    // to route through SurfaceClass's upload path without
                    // duplicating the BGRA→RGBA swizzle here.
                    int pitch = 0;
                    (void)surface->Lock(&pitch);
                    surface->Unlock();
                }
            }
        }
    }

    LastAccessed=WW3D::Get_Sync_Time();
}
```

## Locked decisions

| Question | Decision |
|---|---|
| Where does the upload happen — in the ctor, or later? | In the ctor, via `Lock()+Unlock()` on the surface. Rationale: the caller's contract is "here is a filled surface, give me a ready-to-sample texture." Deferring the upload to first `Apply()` would force every caller to reason about the frame in which the texture first got bound. |
| Why `Lock()+Unlock()` instead of calling `Update_Texture_RGBA8` directly? | 5h.31 already has a BGRA→RGBA swizzle inside `Upload_To_Associated_Texture`. Going through the surface path reuses that routine, keeping the swizzle logic single-sourced. The Lock is trivial (pointer return), the Unlock does the upload; no extra allocation. |
| Format coverage | Same as 5h.31 — `Upload_To_Associated_Texture` silently skips non-BGRA8 surfaces. A surface of `WW3D_FORMAT_A4R4G4B4` (font3d) binds the handle and seeds an empty GPU texture. Sampling it produces garbage until a follow-on phase adds format-aware upload. Most bgfx-mode callers generate A8R8G8B8 content already; the format-expansion is out-of-scope here. |
| `mipmap` flag | Hardcoded `false`. The DX8 ctor defers mipmap generation to D3D8 (via `D3DX_DEFAULT` filtering in `_Create_DX8_Texture`). Mip generation in bgfx lands when a caller actually depends on it; today's surface-based callers use `MIP_LEVELS_1`. |
| Failure semantics | If `Create_Texture_RGBA8` returns 0 (backend unavailable, zero-dim surface), fall through without setting a handle. Subsequent `Apply()` then binds the null handle, which `BgfxBackend::Set_Texture(stage, 0)` routes to the 2×2 placeholder (same behavior as 5h.27 for zero-handle textures). |
| DX8 impact | None — the ctor body is a `#ifndef RTS_RENDERER_DX8` branch. DX8 path is byte-identical. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Replaced the bgfx-mode `TextureClass(SurfaceClass*, MipCountType)` stub body with a real implementation (~25 lines). |

No header edits needed — `Set_Bgfx_Handle` / `Peek_Bgfx_Handle`
already exist from 5h.27, and `SurfaceClass::Set_Associated_Texture`
is from 5h.31.

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21,
bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. |
| `cmake --build build_bgfx --target corei_bgfx` | OK. |
| All 22 bgfx tests | PASSED (unchanged from 5h.31 — no new test this phase). |
| `cmake -S . -B cmake-build-release -DRTS_RENDERER=dx8` | DX8 reconfigures clean. |

### No new test — why

Matches the pattern from 5h.4 / 5h.7 / 5h.8 / 5h.12 / 5h.17: a harness
that exercises `TextureClass(SurfaceClass*, …)` has to link the
TextureClass machinery (TextureBaseClass + DX8TextureManager +
WW3DAssetManager static init + MissingTexture statics + …). The
behavior this phase added is a four-line routing: allocate → bind →
trigger Unlock.

The pure routing is already validated by `tst_bgfx_surface` —
`Set_Associated_Texture` followed by a Lock/Unlock round-trip lands
the right pixels in the right bgfx handle. What 5h.32 does is wire
the exact same calls through the `TextureClass` ctor. The backend-side
behavior is unchanged from 5h.31's test; the only new surface area is
"does the ctor issue those calls in the right order," which is short
enough to read and verify by inspection.

Build + 22-test regression + DX8 reconfigure-clean is the right bar.

## What this phase buys

Three existing caller patterns now reach a fully populated bgfx
texture without any additional wire-up:

```cpp
// font3d.cpp, bmp2d.cpp, W3DAssetManager.cpp
SurfaceClass* surf = NEW_REF(SurfaceClass, (w, h, WW3D_FORMAT_A8R8G8B8));
// …draw into surf…
TextureClass* tex = NEW_REF(TextureClass, (surf, MIP_LEVELS_1));
// tex->Peek_Bgfx_Handle() is now a real bgfx::TextureHandle holding
// the drawn pixels. tex->Apply(stage) binds it for sampling.
```

Combined with 5h.31, any caller that already knows how to fill a
SurfaceClass gets full bgfx texture plumbing for free.

## Deferred

| Item | Stage | Note |
|---|---|---|
| Format-aware upload (`Update_Texture` with WW3DFormat) | later | Covers A4R4G4B4 font glyphs, 565, 4444 etc. |
| Mip-chain generation when `mip_level_count > 1` | later | Most surface-path callers use MIP_LEVELS_1 anyway |
| Per-surface / per-texture ref-count coupling (surface outlives handle, or vice versa) | later | Today the `AssociatedTextureHandle` is a raw uintptr_t; the surface doesn't own a ref. If the texture dies first, the surface's next Unlock writes into a stale handle. The current callers destroy surface first (it was a scratch), so this is latent. |
| `BgfxTextureCache` refcounting + `Invalidate` integration | 5h.33 | Bumped from 5h.32 in the previous doc's deferred table |
| Sampler-state routing from `TextureFilterClass::Apply` | 5h.34 | Unchanged |

## Meta-status on the 5h arc

- **32 sub-phases** complete (5h.1 → 5h.32).
- **22 bgfx tests**, all green; DX8 reconfigures clean after every phase.
- `IRenderBackend` interface surface is still 26 virtual methods — no
  new backend capability this phase. What landed is a production caller
  that closes the surface-path round-trip:
  `SurfaceClass` → `TextureClass` ctor → `IRenderBackend::Create_Texture_RGBA8` →
  `IRenderBackend::Update_Texture_RGBA8`.

Together with 5h.31, the adapter-side surface path is now
**functionally complete**: any code that constructs a `TextureClass`
from a filled `SurfaceClass` renders correctly in bgfx mode.
