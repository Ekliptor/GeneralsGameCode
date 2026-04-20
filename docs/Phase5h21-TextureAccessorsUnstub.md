# Phase 5h.21 — TextureBaseClass D3D accessors + priority

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twenty-first
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h20-TextureBaseInvalidateUnstub.md`.

## Scope gate

5h.20 brought three `TextureBaseClass` methods out of the guard
(`Set_Texture_Name`, `Set_HSV_Shift`, `Invalidate`) plus six POD WW3D
statics that those methods transitively depend on. 5h.21 takes the
next slice: four more `TextureBaseClass` methods — the D3D texture
accessor pair (`Peek_D3D_Base_Texture`, `Set_D3D_Base_Texture`) and
the priority pair (`Get_Priority`, `Set_Priority`). All four need
either `WW3D::Get_Sync_Time()` (now available post-5h.20) or methods
on `IDirect3DBaseTexture8` (`AddRef`/`Release` available in the
compat shim since Phase 5d; `GetPriority`/`SetPriority` added in
this phase).

Deferred for subsequent phases because each has a dedicated blocker
of its own:

- `Is_Missing_Texture`: `MissingTexture::_Get_Missing_Texture()` is
  defined inside `missingtexture.cpp`'s outer `#ifdef RTS_RENDERER_DX8`.
- `Load_Locked_Surface`: `TextureLoader::Request_Thumbnail(this)` —
  same situation in `textureloader.cpp`.
- `_Get_Total_*_Size/Count` + `Invalidate_Old_Unused_Textures`:
  `WW3DAssetManager::Get_Instance()->Texture_Hash()` — `assetmgr.cpp`
  has its own set of `#ifdef RTS_RENDERER_DX8` guards around the
  texture-registry machinery.
- `Apply_Null`: routes through `DX8Wrapper::Set_DX8_Texture(stage,
  nullptr)` which is a DX8-only static.
- `Get_Reduction`: reads `WW3D::Get_Texture_Reduction()` — this one
  *is* an inline accessor, but it reads `_TextureReduction` which is
  a **non-static** file-local that lives inside ww3d.cpp's guard,
  not a WW3D:: member. Unlike 5h.20's static members, file-locals
  can't be moved out without bringing the setters with them.

## Locked decisions

| Question | Decision |
|---|---|
| Add priority stubs to compat shim | `DWORD IDirect3DBaseTexture8::GetPriority() { return 0; }` + `DWORD SetPriority(DWORD) { return 0; }`. Fixed return of 0 — bgfx has its own cache eviction logic, DX8 priorities have no meaning there. Deterministic output means callers that read the priority after setting it get the default they'd get if the texture had never been assigned one. |
| `Peek_` vs `Set_` bodies | Copied verbatim from the DX8 path. Both touch `LastAccessed = WW3D::Get_Sync_Time()` (now linkable); `Set_` calls `AddRef`/`Release` on `D3DTexture` which resolves through the compat-shim stubs. In bgfx mode `D3DTexture` is always nullptr until the texture-load path is un-guarded (5h.22+), so `Peek_` returns nullptr and `Set_` is a no-op beyond the `LastAccessed` update. |
| `Get_Priority` / `Set_Priority` null check | Preserved. The DX8 body's `WWASSERT_PRINT(0, "…: D3DTexture is null!")` fires in bgfx mode whenever the game tries to read/write priority before a texture's been loaded. That's the correct diagnostic: the D3DTexture *is* null, and the caller shouldn't be touching priority there. |
| Duplicate removal | Removed the original copies from inside the guard (replaced with marker comments). DX8 build now sees each of these methods defined exactly once, same as the un-guarded version. |
| DX8 impact | Zero. Same source lines, different `#ifdef` region. Compiler emits byte-identical DX8 code. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/compat/d3d8.h` | Add `DWORD IDirect3DBaseTexture8::GetPriority()` + `DWORD SetPriority(DWORD)` stubs. Both return 0. |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Moved `TextureBaseClass::Peek_D3D_Base_Texture`, `Set_D3D_Base_Texture`, `Get_Priority`, `Set_Priority` above the `#ifdef RTS_RENDERER_DX8` guard. Replaced original in-guard copies with marker comments. |

### Method bodies (unchanged from DX8)

```cpp
IDirect3DBaseTexture8 * TextureBaseClass::Peek_D3D_Base_Texture() const
{
    LastAccessed = WW3D::Get_Sync_Time();
    return D3DTexture;
}

void TextureBaseClass::Set_D3D_Base_Texture(IDirect3DBaseTexture8* tex)
{
    LastAccessed = WW3D::Get_Sync_Time();
    if (D3DTexture != nullptr) D3DTexture->Release();
    D3DTexture = tex;
    if (D3DTexture != nullptr) D3DTexture->AddRef();
}

unsigned int TextureBaseClass::Get_Priority()
{
    if (!D3DTexture) { WWASSERT_PRINT(0, "Get_Priority: D3DTexture is null!"); return 0; }
    return D3DTexture->GetPriority();
}

unsigned int TextureBaseClass::Set_Priority(unsigned int priority)
{
    if (!D3DTexture) { WWASSERT_PRINT(0, "Set_Priority: D3DTexture is null!"); return 0; }
    return D3DTexture->SetPriority(priority);
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. Four more `TextureBaseClass` symbols link in bgfx mode. |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `Peek_D3D_Base_Texture` / `Set_D3D_Base_Texture` linkable in bgfx | Yes. |
| `Get_Priority` / `Set_Priority` linkable in bgfx | Yes. |
| `IDirect3DBaseTexture8::GetPriority/SetPriority` callable in bgfx | Yes — compat-shim stubs. |
| Duplicate definitions across the TU | Zero. |

Cumulative progress on the `TextureBaseClass` surface: 9 of ~18
public methods now link in bgfx mode (2 in 5h.19, 3 in 5h.20, 4 in
5h.21). Roughly half the class is un-guarded.

## Deferred

| Item | Stage | Blocker |
|---|---|---|
| `Is_Missing_Texture` | 5h.22 | `missingtexture.cpp` wholly guarded |
| `Load_Locked_Surface` | 5h.22 | `textureloader.cpp` wholly guarded |
| `_Get_Total_*_Size/Count` (×8) + `Invalidate_Old_Unused_Textures` | 5h.23 | `assetmgr.cpp` has RTS_RENDERER_DX8 guards; `WW3DAssetManager::Get_Instance()` + `Texture_Hash()` need a no-op stub or partial un-guard |
| `Apply_Null` | 5h.24 | `DX8Wrapper::Set_DX8_Texture` is DX8-only; needs forwarding to `IRenderBackend::Set_Texture(stage, 0)` |
| `Get_Reduction` | 5h.24 | `WW3D::Get_Texture_Reduction()` + `Is_Large_Texture_Extra_Reduction_Enabled()` are non-inline; need ww3d.cpp un-guard for their bodies |
| `TextureClass` ctors + `Apply` + `Init` | 5h.25+ | Substantial `TextureLoader` un-guard; connects to `BgfxTextureCache` for the first game-loaded DDS render |

Five sub-phases away from a full `TextureBaseClass` un-guard; then
another two or three for the derived `TextureClass` + `TextureLoader`
+ asset-manager plumbing.

## What this phase buys

Mechanical progress on the texture arc. The D3D accessor pair plus
priority accessor pair are the most-used `TextureBaseClass` methods
outside of `Apply` / `Init`; downstream code holding a
`TextureBaseClass*` (lots of it — the material system, rendobj
system, scene graph) now compiles + links whenever it calls one of
these four methods, which is pretty much everywhere a texture is
touched per-frame.

Runtime behavior in bgfx mode stays the same as 5h.20 — no
`TextureClass` instance exists yet, so all four accessors act on
nullptr `D3DTexture` and do nothing useful. The value is
link-readiness: the blast radius of further texture un-ifdef'ing
won't hit new compile errors on these four methods.
