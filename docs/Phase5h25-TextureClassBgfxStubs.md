# Phase 5h.25 — TextureClass bgfx-mode stubs

Companion doc to `docs/CrossPlatformPort-Plan.md`. Twenty-fifth
sub-phase of the DX8Wrapper adapter. Follows
`Phase5h24-TextureBaseComplete.md`.

## Scope gate

`TextureBaseClass` is fully linkable in bgfx mode post-5h.24. The
next layer up — the concrete `TextureClass` — remained guarded: five
ctor overloads, `Init`, `Apply`, `Apply_New_Surface`, and
`Get_Texture_Memory_Usage`. Each DX8 implementation is substantial,
pulling in `DX8Wrapper::_Create_DX8_Texture` + `TextureLoader`
thread queues + `D3DSURFACE_DESC` queries + surface-level walking.
Un-`#ifdef`'ing the DX8 bodies in-place is not tractable as a single
slice.

5h.25 instead provides **minimal bgfx-mode implementations** in a
dedicated `#ifndef RTS_RENDERER_DX8` block at the top of
`texture.cpp`. These implementations are intentionally dumb:

- All five ctors do the minimum member init (call base ctor, init
  `Filter`, stamp `LastAccessed`, set some flags).
- `Init()` just marks the texture initialized.
- `Apply(stage)` forwards to `DX8Wrapper::Set_DX8_Texture(stage, nullptr)`
  — a no-op in bgfx mode via the `DX8CALL` macro stub. No real
  texture gets bound yet.
- `Apply_New_Surface` updates state flags but skips surface queries.
- `Get_Texture_Memory_Usage` returns 0.

This phase is **not** where real texture binding lands. It's where
`TextureClass` becomes constructible + linkable so the rest of the
game code (material system, mesh renderer, asset manager) can hold
references to `TextureClass*` objects without linker errors. 5h.27
will replace `Init` + `Apply` with the real
`BgfxTextureCache::Get_Or_Load_File` + `IRenderBackend::Set_Texture`
wire-up.

Two prerequisite un-guards also land in this phase:

- `TextureFilterClass::TextureFilterClass(MipCountType)` — pure
  member-init ctor, moved above the `texturefilter.cpp` guard. The
  `TextureClass::Filter` member needs it.
- `TextureLoader::Request_Foreground_Loading` + `Request_Background_Loading`
  + `Is_DX8_Thread` — no-op stubs added to `textureloader.cpp`'s
  `#ifndef RTS_RENDERER_DX8` block (matching the 5h.22 pattern for
  `Request_Thumbnail`).

## Locked decisions

| Question | Decision |
|---|---|
| Stubs vs un-guarded DX8 bodies | Stubs. The DX8 bodies span ~300 lines and call into 8+ DX8-bound subsystems (texture loader, texture manager, thumbnail manager, caps query, surface create). Writing minimal bgfx stubs is ~80 lines total. |
| Stub file location | Inline in `texture.cpp` above the `#ifdef RTS_RENDERER_DX8` guard, wrapped in `#ifndef RTS_RENDERER_DX8`. Keeps all `TextureClass` implementations in one TU; the two branches (DX8 vs bgfx) are explicit and side-by-side. |
| Which ctors to stub | All five — the asset manager's canonical path uses `(name, full_path, …)`, but the other four are referenced from various helpers (`Load_Texture`, surface-to-texture conversion, direct-D3D pointer wrap). A missed ctor is a linker error. |
| `IsProcedural` / `IsReducible` / `Initialized` defaults | Mirror the DX8 ctor's behavior for each overload. The `(name, path)` ctor starts uninitialized (the DX8 version also defers init); the `(width, height, format, ...)` ctor — used for render-target textures — immediately marks initialized. |
| `DEFAULT_INACTIVATION_TIME` | Duplicated in the bgfx block (the DX8 const is inside the guard). Chose 20000ms to match. |
| `TextureFilterClass` member | Requires an explicit ctor call in each `TextureClass` ctor's initializer list since `Filter` has no default ctor. The un-guarded `TextureFilterClass::TextureFilterClass(MipCountType)` satisfies that. |
| `Set_Texture_Name` / `Set_Full_Path` in `(name, path)` ctor | Call both if their arguments are non-null. Lightmap-flag detection (`+` prefix in the texture name) is kept — some materials branch on `IsLightmap()` and the flag needs to survive. |
| `Apply(stage)` behavior | Routes to `DX8Wrapper::Set_DX8_Texture(stage, nullptr)` — which updates the internal `Textures[]` array and is a no-op on the D3D device. In bgfx mode the backend binds its built-in 2×2 white placeholder until 5h.27 wires the real texture path. |
| `Apply_New_Surface` | Accepts the call, toggles `Initialized` / `InactivationTime` flags, but doesn't touch the D3D texture pointer. Runtime-cold — no callers in bgfx mode today. |
| DX8 impact | Zero. The bgfx block is inside `#ifndef RTS_RENDERER_DX8`; DX8 builds skip it entirely. |

## Source-level changes

### Edited files

| File | Change |
|---|---|
| `Core/Libraries/Source/WWVegas/WW3D2/texturefilter.cpp` | Moved `TextureFilterClass::TextureFilterClass(MipCountType)` above the `#ifdef RTS_RENDERER_DX8` guard. |
| `Core/Libraries/Source/WWVegas/WW3D2/textureloader.cpp` | Added `Request_Foreground_Loading` + `Request_Background_Loading` + `Is_DX8_Thread` no-op stubs to the existing `#ifndef RTS_RENDERER_DX8` block. |
| `Core/Libraries/Source/WWVegas/WW3D2/texture.cpp` | Added a `#ifndef RTS_RENDERER_DX8` block above the main DX8 guard with minimal bodies for all five `TextureClass` ctors + `Init` + `Apply` + `Apply_New_Surface` + `Get_Texture_Memory_Usage`. |

### The `(name, path)` ctor — the most-used overload

```cpp
TextureClass::TextureClass(const char *name, const char *full_path,
                            MipCountType mip_level_count, WW3DFormat texture_format,
                            bool allow_compression, bool allow_reduction)
:   TextureBaseClass(0, 0, mip_level_count),
    TextureFormat(texture_format),
    Filter(mip_level_count)
{
    IsCompressionAllowed = allow_compression;
    InactivationTime     = DEFAULT_INACTIVATION_TIME_BGFX;
    IsReducible          = allow_reduction;

    if (name) {
        for (const char* p = name; *p; ++p)
            if (*p == '+') { IsLightmap = true; break; }   // lightmap marker
        Set_Texture_Name(name);
    }
    if (full_path) Set_Full_Path(full_path);

    LastAccessed = WW3D::Get_Sync_Time();
    // Initialized stays false — BgfxTextureCache bridge (5h.27) flips it.
}
```

### `Apply` stub

```cpp
void TextureClass::Apply(unsigned int stage)
{
    if (!Initialized) Init();
    LastAccessed = WW3D::Get_Sync_Time();
    // Phase 5h.27 will read this->Peek_Bgfx_Handle() and forward through
    // IRenderBackend::Set_Texture. For now the adapter's draw path uses the
    // backend's 2×2 placeholder via Set_DX8_Texture(stage, nullptr) → no-op
    // in bgfx mode.
    DX8Wrapper::Set_DX8_Texture(stage, nullptr);
}
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake --build build_bgfx --target z_ww3d2` | OK. All five `TextureClass` ctors + `Init` + `Apply` + `Apply_New_Surface` + `Get_Texture_Memory_Usage` link in bgfx mode. |
| All 20 bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 reconfigures clean. |

Static sweep:

| Pattern | Result |
|---|---|
| `TextureClass::TextureClass` (5 ctor overloads) linkable in bgfx mode | Yes. |
| `TextureClass::Init` / `Apply` / `Apply_New_Surface` / `Get_Texture_Memory_Usage` linkable | Yes. |
| `TextureFilterClass::TextureFilterClass(MipCountType)` linkable | Yes. |
| `TextureLoader::Request_Foreground_Loading` / `Request_Background_Loading` / `Is_DX8_Thread` linkable | Yes (no-op stubs). |
| Runtime behavior in bgfx mode | `TextureClass` instances are constructible but all their methods are no-ops; no texture data is actually loaded or bound. The backend still uses its 2×2 placeholder for any textured draw. |

## Deferred

| Item | Stage | Blocker |
|---|---|---|
| `ZTextureClass` / `CubeTextureClass` / `VolumeTextureClass` ctors + `Apply` + `Init` + `Get_Texture_Memory_Usage` | 5h.26 | Same pattern, smaller scope — these classes are less commonly used |
| `SurfaceClass` un-guard (for the `TextureClass(SurfaceClass*)` ctor to read width/height from the surface) | 5h.26 | surface.cpp is wholly guarded; this ctor's bgfx stub currently ignores the surface argument |
| `BgfxTextureCache` wire-up in `TextureClass::Init` + `Apply`: load file → upload to bgfx → store handle in `BgfxTexture` field → bind on Apply | 5h.27 | None — all prerequisites in place |
| First game-loaded DDS renders in bgfx mode | after 5h.27 | End-to-end texture path |

## What this phase buys

**`TextureClass` is constructible in bgfx mode.** The asset manager
can now create `TextureClass` instances from file paths, pass them
through the material system, hold them in caches. Every call site in
the game code that does `new TextureClass(...)` or holds a
`TextureClass*` links and runs — the textures just don't render
(backend always uses the placeholder).

This closes the gap between "texture metadata exists" and "texture
pixels bind to shaders." 5h.26 does the same for the three derived
classes (Z / Cube / Volume). 5h.27 swaps the placeholder for a real
`BgfxTextureCache` wire-up — that's the "first visible frame with
real textures" milestone.

The pattern (bgfx-only stub block above the main DX8 guard) is the
same shape 5h.26 will follow for the three remaining subclasses.
Should be a shorter phase since each subclass only has 1–2 ctors
and the stub bodies are structurally identical to `TextureClass`'s.
