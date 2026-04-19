# Phase 5h.3 — `BgfxTextureCache`: path-keyed DDS loader

Companion doc to `docs/CrossPlatformPort-Plan.md`. Third sub-phase of
the DX8Wrapper adapter. Follows `Phase5h2-DeviceLifetime.md`.

## Objective

Give production texture code a single entry point that:

1. takes a texture path,
2. streams the bytes off disk,
3. decodes through bimg (Phase 5j),
4. uploads via the active `IRenderBackend` (Phase 5h.1 runtime),
5. caches the resulting handle so the same path isn't re-uploaded,
6. cleans up at shutdown before the backend dies.

Generals' `TextureClass` is called from dozens of sites — terrain, units,
decals, HUD — and the same texture is often requested multiple times
per frame. Without caching, every lookup would re-read from disk, re-
decode, and re-upload to GPU. Phase 5h.4+ will route `TextureClass`'s
constructor through this cache so those dozens of call sites automatically
share handles.

5h.3 doesn't touch `TextureClass` yet. It just lands the cache + wires
its teardown into `BgfxBootstrap::Shutdown` so nothing leaks. 5h.4 is the
bridge.

## Locked decisions

| Question | Decision |
|---|---|
| Key type | `const char*` path, copied into a `std::unordered_map<std::string, uintptr_t>`. Game texture names are typically short; map-copy cost is in the noise vs. decode + upload. Normalization (lowercase / slash direction) is left to the caller — production Generals already canonicalizes paths before hitting the loader. |
| Storage | Singleton `unordered_map` behind an inline `Entries()` accessor. No static-init-order fragility: first access constructs. Exposed via four free functions inside `namespace BgfxTextureCache`. |
| Disk I/O | Stock `<cstdio>` — `fopen` / `fseek` / `ftell` / `fread`. No Generals file-system abstraction yet; that lives in `chunkio` / `FileClass` and pulling those into BGFXDevice would be a circular dep. 5h.4 can swap in the abstraction when `TextureClass` is routed through. |
| Decode / upload | Straight call into `RenderBackendRuntime::Get_Active()->Create_Texture_From_Memory(bytes, size)`. All format handling (DDS / KTX / PNG / TGA, DXT compressed variants) rides on bimg from Phase 5j — no new decode code. |
| `Release(path)` | Drops the single entry and `Destroy_Texture`s the handle. No refcounting — callers that need multiple logical references should manage their own. Simpler; matches `TextureClass`'s own "one path = one GPU resource" model. |
| Teardown order | `BgfxBootstrap::Shutdown` calls `BgfxTextureCache::Clear_All` **before** `s_instance->Shutdown()`. If the cache dropped to nullptr-backend first, `Destroy_Texture` would be a no-op (safe) but the backend's own sweep would destroy those textures anyway. Doing it explicitly keeps the ownership story honest. |
| DX8 build behavior | The header isn't includable without corei_bgfx on the link line, so DX8 builds simply don't see it. Production DX8 texture code stays unchanged. |

## Source-level changes

### New files

| Path | Purpose |
|---|---|
| `Core/GameEngineDevice/Include/BGFXDevice/Common/BgfxTextureCache.h` | 4-function API: `Get_Or_Load_File`, `Release`, `Clear_All`, `Size`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxTextureCache.cpp` | Map + disk-read helper + calls through `RenderBackendRuntime`. |
| `tests/tst_bgfx_texcache/main.cpp` | Hand-builds a 1,152-byte DDS to a temp file (`/tmp/tst_bgfx_texcache.dds`), then exercises six invariants on the cache. Includes a gotcha: DDS_PIXELFORMAT starts at **offset 76**, not 72 — the dwReserved1[11] array ends at 75. Got this wrong the first time and bimg refused to parse. |
| `tests/tst_bgfx_texcache/CMakeLists.txt` | Gated on `RTS_RENDERER=bgfx` + `RTS_BUILD_CORE_EXTRAS`. |
| `docs/Phase5h3-TextureCache.md` | This doc. |

### Edited files

| File | Change |
|---|---|
| `Core/GameEngineDevice/Source/BGFXDevice/CMakeLists.txt` | Add `BgfxTextureCache.cpp` + header to `corei_bgfx`. |
| `Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBootstrap.cpp` | Include `BgfxTextureCache.h`; `Shutdown` calls `Clear_All()` before `s_instance->Shutdown()`. |
| `tests/CMakeLists.txt` | `add_subdirectory(tst_bgfx_texcache)`. |

### API

```cpp
namespace BgfxTextureCache
{
    uintptr_t Get_Or_Load_File(const char* path);   // 0 on failure, cached on success
    void      Release(const char* path);            // drops a single path
    void      Clear_All();                          // sweeps; BgfxBootstrap calls this
    std::size_t Size();                             // diagnostic
}
```

### Cache algorithm (the entire hot path)

```cpp
auto it = map.find(path);
if (it != map.end()) return it->second;              // hit

IRenderBackend* backend = RenderBackendRuntime::Get_Active();
if (!backend) return 0;                              // no-op in DX8-only runs

std::vector<uint8_t> bytes;
if (!Read_File(path, bytes)) return 0;               // missing / unreadable

uintptr_t h = backend->Create_Texture_From_Memory(bytes.data(), bytes.size());
if (h == 0) return 0;                                // bimg refused

map.emplace(path, h);
return h;
```

## Verification

Ran on macOS 15 (Apple Silicon), CMake 4.3, AppleClang 21, bgfx v1.143.9216-529.

| Command | Outcome |
|---|---|
| `cmake -S . -B build_bgfx` | OK. |
| `cmake --build build_bgfx --target corei_bgfx` | OK — `BgfxTextureCache.cpp` compiles; `BgfxBootstrap` picks up the new `Clear_All` call at the right teardown ordering. |
| `cmake --build build_bgfx --target z_ww3d2` | OK — per-game WW3D2 (which pulls in corei_bgfx via 5h.2's link edge) still builds. |
| `cmake --build build_bgfx --target tst_bgfx_texcache` | OK. |
| `./tests/tst_bgfx_texcache/…` | Six invariants: first-load, dedup, release, re-load, clear-all, bad-path-doesn't-pollute. `tst_bgfx_texcache: PASSED`. |
| All 15 prior bgfx tests | PASSED. |
| `cmake -S . -B cmake-build-release` | DX8 path reconfigures cleanly. |

### Pitfall caught

The hand-rolled DDS file in the test originally wrote DDS_PIXELFORMAT
starting at offset 72 instead of 76. `bimg::imageParse` returned nullptr
and the BgfxBackend logged "bimg decode failed". Fixed the offsets; the
docstring now calls it out. `tst_bgfx_bimg`'s DDS writer (Phase 5j) has
the layout right because it appends sequentially (no offset math). Noted
for any future hand-built DDS test.

Static sweep:

| Pattern | Result |
|---|---|
| `BgfxTextureCache::` callers outside the test + `BgfxBootstrap::Shutdown` | Zero — ready for 5h.4's `TextureClass` wiring. |
| `Create_Texture_From_Memory` callers in production code | Zero still; the test calls it through the cache. |
| Path-keyed lookups of any other texture form (cube maps, 3D textures) | None supported yet — 2D RGBA / DXT only, matching what Generals ships. |

## Deferred

| Item | Stage |
|---|---|
| `TextureClass::`Init`_From_File` → `BgfxTextureCache::Get_Or_Load_File` routing. Once this lands, every Generals `TextureClass` construction transparently acquires a bgfx handle. Expected shape: an opt-in `m_bgfxHandle` field populated lazily, DX8 path remains authoritative for anything not routed through `IRenderBackend` yet | 5h.4 |
| Path-normalization — lowercase + forward-slash. Keep in the cache layer or keep in the caller — TBD in 5h.4 once we see how `TextureClass` canonicalizes | 5h.4 |
| Async / background decode — the cache is synchronous today. Generals loads thousands of textures at map-load time; if hitches become a problem, a worker-thread decode path is the right fix | later |
| Reference counting — current `Release` is unconditional. `TextureClass` ref-counts; the cache would need to honor that or call sites need to hold refs. 5h.4 decides | 5h.4 |
| Multi-backend support (e.g. DX8 and bgfx cooperating on the same texture). The current design assumes one active backend, which matches 5h.1's seam. If dual-backend ever lands, the cache key would need to include backend identity | later |
| Use `FileClass` / chunkio instead of `<cstdio>` once BGFXDevice can sensibly depend on the file-system abstraction | 5h.4 or later |

The path → bgfx-handle bridge is live. Phase 5h.4 is the wire: `TextureClass`
calls this cache instead of (or alongside) its DX8 upload on construction.
