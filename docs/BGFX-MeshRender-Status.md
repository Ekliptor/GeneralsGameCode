# BGFX Mesh Render — Phase D Status

**Build target:** `build_bgfx/{Generals,GeneralsMD}/{generalsv,generalszh}.app` on macOS (Metal / BGFX / SDL3 / OpenAL).
**Last verified:** 2026-05-04 (D13b heightmap edge-blend probe → mip-chain fix)
**Phases covered:** D7 → D13b (post-Phase5h texture stack, building on the
direct-submit mesh path that bypasses the stubbed DX8 polygon-renderer
batching system).

## Current state

| Symptom (BGFX builds, main menu) | D6 | D7 | D8 | D9 | D10 | D11 | D13a | D13b |
|---|---|---|---|---|---|---|---|---|
| Vanilla menu reaches main loop | yes | yes | yes | yes | yes | yes | yes | yes |
| Vanilla menu — terrain/props rendered | partial (white props) | partial | yes | yes | yes | yes | yes | **clean (cobble + grass + water puddles)** |
| ZH menu reaches main loop | crash in `read_texture_ids` | crash | yes | yes | yes | yes | yes | yes |
| ZH menu — demo-battle shellmap renders | n/a | n/a | yes (with skin gaps) | yes (with skin gaps) | yes | yes | yes | **initial frame clean, progressive flicker (D13c)** |
| Large white rectangles on ZH terrain | yes | yes | yes | yes | yes | yes | yes | **gone (mip-chain fix)** |
| `BgfxTextureCache: can't read '*.tga'` floods | many | many | many | 0 | 0 | 0 | 0 | 0 |
| `bimg decode failed` errors | n/a | n/a | n/a | 0 | 0 | 0 | 0 | 0 |
| `[PhaseD7-warn] direct-submit fell through` | n/a | yes (latched once) | yes | yes | **gone** | replaced by `[PhaseD11:fallthrough]` (0 fires) | 0 fires | 0 fires |
| `[SurfaceClass:bgfx] Copy(surface) format mismatch` stub note | yes | yes | yes | yes | yes (informational) | yes (informational) | yes (informational) | yes (informational) |
| `[PhaseD11:*]` mesh inventory | n/a | n/a | n/a | n/a | n/a | **89 unique meshes catalogued (ZH)** | 89 | 89 |
| `[PhaseD12:slowpath]` array/multi-pass meshes routed | n/a | n/a | n/a | n/a | n/a | **4 meshes (3 arr + 1 multi-pass)** | 4 | 4 |
| `[PhaseD13a:*]` non-MeshClass probe | n/a | n/a | n/a | n/a | n/a | n/a | **5 / 13 fire** | 5 / 13 fire |
| `[PhaseD13b:*]` heightmap atlas probe | n/a | n/a | n/a | n/a | n/a | n/a | n/a | **atlas healthy; mip 1+ uninitialized → fixed** |

Last stderr observed (ZH, post-D10):

```
BgfxBackend::Init: Metal (1600x1200, windowed)
[SurfaceClass:bgfx] Copy(surface->surface) format mismatch is a stub — no bgfx callers expected yet
```

Vanilla stderr is identical minus the SurfaceClass line.

## Phases — what each one did

### Phase D7 — direct-submit BGFX path for rigid meshes

- New: `Core/Libraries/Source/WWVegas/WW3D2/MeshDirectRender.{h,cpp}`
  exporting `WW3D2::Render_Mesh_Direct_Bgfx(MeshClass&, RenderInfoClass&)`.
- Bypasses `TheDX8MeshRenderer.Register_Mesh_Type` /
  `DX8TextureCategoryClass::Add_Render_Task` (all stubs in
  `ww3d2_bgfx_stubs.cpp`) by reading vertex / index / material data
  directly off `MeshGeometryClass` / `MeshModelClass` / `MeshMatDescClass`
  and pushing it through `DX8Wrapper` → `IRenderBackend::Draw_Triangles_Dynamic`.
- `mesh.cpp` Render() calls it before falling through to the legacy
  `[PhaseD7-warn]` warning.
- Limitation D7: rigid-only — bailed on `Get_Flag(SKIN)`.

### Phase D8 — LP64 fix for W3D on-disk structs

- Root: `uint32`/`sint32` typedef'd as `unsigned long`/`signed long`
  in `bittype.h`; on LP64 (macOS / Linux) those are 8 bytes, doubling
  every on-disk W3D struct that used them. `cload.Read(&s, sizeof(s))`
  then over-reads, misaligning the rest of the chunk and crashing
  inside `MeshModelClass::read_texture_ids` on first asset load.
- Surgical fix: keep the typedef as `unsigned long` (preserves all
  legacy non-binary code), but switch every on-disk struct's *member*
  to fixed-width `uint32_t` / `int32_t`.
- Files patched: `chunkio.h`, `w3d_file.h` (47 structs), `w3d_obsolete.h`
  (15 structs), `meshmdlio.cpp` (six `sizeof(uint32)` → `sizeof(uint32_t)`
  call sites for per-vertex array reads). `persistfactory.h` defensive
  `(uint32)(uintptr_t)obj` cast retained.
- Memory: `uint32_unsigned_long_8bytes_macos.md`.

### Phase D9 — BGFX texture cache `.dds` twin probe

- Vanilla `Textures.big` ships ~96% of art textures as DXT-compressed
  `.dds`; INI/W3D references are authored against the original `.tga`
  source-asset names. The DX8 path silently rewrites `.tga`→`.dds` in
  `DDSFileClass::DDSFileClass(name, …)` (`ddsfile.cpp:51-56`,
  3 chars in place). The BGFX texture cache sat below that pipeline
  and only probed the literal `.tga` path — every vanilla unit /
  terrain / prop texture failed and stderr flooded with
  `BgfxTextureCache: can't read 'avbattlesh.tga'` etc.
- Fix: `readViaFS` hook in
  `{Generals,GeneralsMD}/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp`
  now derives the `.dds` twin (case-insensitive 3-char swap) and probes
  it first across the same prefix chain
  (`Data\<lang>\Art\Textures\…` → `Art\Textures\…` → literal). Only
  after the `.dds` 404s does it fall back to `.tga`. `bimg::imageParse`
  auto-detects DDS magic, so the backend's `Create_Texture_From_Memory`
  is unchanged. Cache key stays the original `.tga` path so two callers
  asking for the same texture still de-dupe via `Entries()`.
- ZH UI textures (`SCShellUserInterface512_001.tga`) ship as actual
  TGAs in `EnglishZH.big` — the `.dds` probe misses, the `.tga`
  fallback hits. Strictly additive.
- Memory: `bgfx_texture_dds_twin_probe.md`.

### Phase D10 — CPU-skinned mesh path

- D7's direct-submit explicitly bailed on `Get_Flag(SKIN)`, so every
  skinned mesh in a frame hit the
  `[PhaseD7-warn] direct-submit fell through (skin / no model / empty geometry)`
  warning (latched once on `static bool s_warnedEmpty`) and rendered
  as nothing.
- Fix: `Render_Mesh_Direct_Bgfx` now CPU-skins. Pulls
  `RenderObjClass* container = mesh.Get_Container()` and
  `const HTreeClass* htree = container->Get_HTree()`. If both present:
  scratch-allocates `std::vector<Vector3>` for deformed positions and
  normals, calls `mesh.Get_Deformed_Vertices(skinPos, skinNorm)`
  (public method that walks `VertexBoneLink` and applies each bone's
  HTree transform), and submits with
  `Set_Transform(D3DTS_WORLD, Matrix3D::Identity)` because HTree bones
  are already in world space — matching the VisRasterizer precedent
  at `mesh.cpp:1083`.
- Diagnostic verification: the `static bool s_warnedEmpty` would have
  flipped if any single skinned mesh fell through. The warning's
  absence proves all skinned meshes route through the new path.
- Memory: `bgfx_skin_mesh_cpu_skinning.md`.

### Phase D11 — Untextured-mesh diagnosis (instrumentation)

Pure diagnostic phase. Adds four one-shot stderr loggers, mirrored on
`Surface_Warn_Once` (`surfaceclass.cpp:1006-1014`):

- **`[PhaseD11:directsubmit]`** in
  `Core/Libraries/Source/WWVegas/WW3D2/MeshDirectRender.cpp` —
  one line per unique `model->Get_Name()` reaching
  `Render_Mesh_Direct_Bgfx`, with pass count, sort level, flag set,
  and `Peek_Single_Texture(p,s)` / `Has_Texture_Array(p,s)` for every
  pass × stage in `[0..MAX_PASSES) × [0..MAX_TEX_STAGES)`.
- **`[PhaseD11:staticsort-defer]`** in `mesh.cpp` (Generals + GeneralsMD)
  — fires inside the `WW3D::Are_Static_Sort_Lists_Enabled() &&
  sort_level != SORT_LEVEL_NONE` branch, just before
  `WW3D::Add_To_Static_Sort_List`. Increments
  `g_PhaseD11_StaticSortDeferCount` (declared in `MeshDirectRender.h`).
- **`[PhaseD11:staticsort-null]`** in `ww3d.cpp` (Generals + GeneralsMD)
  — fires once when `Render_And_Clear_Static_Sort_Lists` enters the
  BGFX `CurrentStaticSortLists==nullptr` early-out, reporting the tally
  from logger 2.
- **`[PhaseD11:fallthrough]`** in `mesh.cpp` — replaces the latched
  `[PhaseD7-warn]`. `Render_Mesh_Direct_Bgfx` now returns a
  `DirectSubmitStatus` enum (`kDirectSubmit_Submitted` /
  `kDirectSubmit_NoBackend` / `..._NoModel` / `..._SkinNoContainer` /
  `..._SkinNoHTree` / `..._EmptyGeometry` / `..._NoVertsOrTris`) so the
  caller logs per-mesh fall-through reasons instead of a single latched
  warning. The DX8 build keeps the original `[PhaseD7-warn]`.

#### Findings — ZH demo-battle shellmap (post-D11 run, 40 s capture)

Inventory totals (single capture, `~/Desktop/d11-zh.png`):

| Bucket | Count |
|---|---|
| `[PhaseD11:directsubmit]` (unique mesh names submitted to backend) | 89 |
| `[PhaseD11:staticsort-defer]` (deferred to BGFX-null static sort) | **0** |
| `[PhaseD11:staticsort-null]` (silently dropped) count | **0** |
| `[PhaseD11:fallthrough]` (direct-submit bailed) | **0** |

**Static-sort defer is NOT the cause.** No mesh in the ZH menu reached
the `WW3D::Are_Static_Sort_Lists_Enabled() && sort_level != SORT_LEVEL_NONE`
branch — `sort_level` is 0 for every mesh in this scene, so the
`Add_To_Static_Sort_List` BGFX null-guard does nothing. D13 (static-sort
integration) is **not needed for the menu**; defer to in-game scenes
where sort-level-tagged meshes (alpha foliage, water, etc.) appear.

**Direct-submit fall-through is empty.** Every mesh that reached
`Render_Mesh_Direct_Bgfx` submitted successfully. D10's CPU-skin path
covers every skinned mesh in the menu.

**Identified white-mesh causes inside the inventory:**

1. **Texture arrays (3 meshes)** — `p0s0=ARR` means
   `Has_Texture_Array(0, 0)` is true and the `Texture[0][0]` single slot
   is null; direct-submit only consults `Peek_Single_Texture(0,0)` so
   these submit with a null texture and render white:
   - `PMWALLCHN3` (perimeter chain-link wall)
   - `UIRGRD_SKN.MUZZLEFX01` (Ranger muzzle flash, skinned)
   - `UITUNF_SKN.MUZZLEFX01` (Tunnel-network muzzle flash, skinned)
2. **Multi-pass material (1 mesh)** — `CVJUNK_D.WINDOWS` declares
   `passes=2`; pass 0 is `lakedusk.tga` (env reflection), pass 1 is
   `cvjunk_d.tga` (the actual diffuse). Direct-submit only renders pass
   0, so junk-pile windows render with the env-map and never composite
   the diffuse.
3. **Vertex-color FX, no diffuse (6 meshes)** — likely intentional
   additive sprites: `AVAMPHIB.MIST03/04`, `AVAMPHIB.TREADFX02`,
   `UVCOMBIKEG.SUICIDEBOMB3/SMOKE/EXPLOSION`. These ship without a
   pass-0 texture by design (vertex-color particle path); not bugs.

**Out-of-MeshClass white rectangles.** The visible large white
rectangles in `~/Desktop/d11-zh.png` exceed the size profile of any
mesh in the inventory. They must originate on a **non-MeshClass**
render path — the most plausible candidates are `Shadow*` shadow
projectors (large flat planes projected on the ground) and
`DecalMeshClass` (terrain decals). Both route through the legacy DX8
batching system whose hooks (`DX8MeshRendererClass::Add_To_Render_List`
for decals, `Shadow*` paths) are stubbed in `ww3d2_bgfx_stubs.cpp`.
This is a separate investigation from D12 multi-pass and warrants its
own probe phase.

#### Recommended D12+ ordering after D11

1. **D12 — Multi-pass + texture-array support in `Render_Mesh_Direct_Bgfx`** (high payoff, narrow scope).
   - Loop `for (p = 0; p < model->Get_Pass_Count(); ++p)` and rebind material/shader/texture per pass before each `Draw_Triangles_Dynamic`.
   - When `Has_Texture_Array(p, s)` is true, resolve per-polygon textures via `Peek_Texture(pidx, p, s)` (`MeshMatDescClass::Peek_Texture` at `meshmatdesc.h:165`) and either batch by texture or split draws.
   - Fixes the 4 explicitly-identified white meshes from the inventory above.
2. **D13a — Shadow-plane / decal probe** (replaces "D13 static-sort"). Investigate which class produces the large white rectangles dominating the screenshot. Likely `Shadow*` projector geometry routed through stubs in `ww3d2_bgfx_stubs.cpp`. Add similar `[PhaseD13a:*]` instrumentation, then port the path.
3. **D13b — Static-sort list flush** (deferred — not needed for shellmap; revisit when in-game scenes with sort-level-tagged geometry are loadable).
4. **D14 — Decal mesh support.** May be subsumed by D13a if shadows turn out to be decal-mesh-routed.

Memory: none (instrumentation only; remove or gate behind a build define after D12 lands).

### Phase D12 — Multi-pass + texture-array submit

Closes the four in-MeshClass white meshes catalogued by D11.
Implementation in `Core/Libraries/Source/WWVegas/WW3D2/MeshDirectRender.cpp`
inside `Render_Mesh_Direct_Bgfx`.

- **Fast path** (single pass + no texture arrays): bit-for-bit
  identical to the D10 code (state-setter order
  `Set_Material → Set_Texture(0) → Set_Shader → Set_Transform →
  Set_Light_Environment → Apply → Draw`). Engaged by the 85
  inventory meshes that already worked. Reordering this even with
  equivalent operations regressed the entire 3D scene during D12
  bring-up — the bgfx pipeline state machine appears to depend on
  the original ordering, so the fast path is preserved verbatim.
- **Slow path** (multi-pass OR `Has_Texture_Array(0,*)` true): outer
  `for (pass = 0; pass < passCount && pass < MAX_PASSES; ++pass)` with
  per-pass `Peek_Material(0, pass)` + `Get_Single_Shader(pass)`.
  - When neither stage carries an array: one `Draw_Triangles_Dynamic`
    per pass with `Peek_Single_Texture(pass, s)` for s ∈ {0,1}.
  - When stage 0 or stage 1 carries an array: scan triangles, batch
    runs of equal `(Peek_Texture(pidx, pass, 0), Peek_Texture(pidx,
    pass, 1))`, submit each run as a sub-IB draw using
    `&g_idxScratch[runStart*3]` + `runLen*3`. Stage 1 is only sampled
    via the array helper when `arrayTex1` is true; otherwise the
    single-stage value is reused across the whole pass to avoid the
    Set_Texture(1) regression seen during bring-up.
  - `Set_Transform(D3DTS_WORLD, ...)` and `Set_Light_Environment` are
    re-issued on every submit (cheap; ensures the state matches what
    the fast path emits one-shot).

`Render_Mesh_Direct_Bgfx`'s return type stays `DirectSubmitStatus`
(D11 enum) so fall-through detail logging keeps working.

A diagnostic `[PhaseD12:slowpath]` one-shot fires on first
encounter with each unique mesh that takes the slow path. It
prints `passes`, `arr00`, `arr01` to confirm the right meshes are
routed there. Scheduled for removal once D12 is verified
in-game beyond the menu.

#### Verification (D12 capture, ZH menu, 40 s)

```
[PhaseD12:slowpath] name=PMWALLCHN3            passes=1 arr00=1 arr01=0
[PhaseD12:slowpath] name=CVJUNK_D.WINDOWS      passes=2 arr00=0 arr01=0
[PhaseD12:slowpath] name=UIRGRD_SKN.MUZZLEFX01 passes=1 arr00=1 arr01=0
[PhaseD12:slowpath] name=UITUNF_SKN.MUZZLEFX01 passes=1 arr00=1 arr01=0
```

Exactly the four meshes D11 predicted. `[PhaseD11:fallthrough]`
remains 0; `[PhaseD11:directsubmit]` count remains 89; vanilla
remains unaffected.

Memory: `bgfx_multipass_and_texture_arrays.md`.

### Phase D13a — Out-of-MeshClass white-rectangle probe

Pure diagnostic phase, mirroring D11. Adds 13 one-shot `[PhaseD13a:*]`
loggers across non-MeshClass render paths to identify which renderer
produces the still-visible large white rectangles in the ZH demo-battle
shellmap (since D11 ruled out MeshClass). All loggers `#ifndef
RTS_RENDERER_DX8` gated.

| Tag | Site | ZH | Vanilla |
|---|---|---|---|
| `[PhaseD13a:bibs]` | `W3DBibBuffer::renderBibs` | — | — |
| `[PhaseD13a:roads]` | `W3DRoadBuffer::drawRoads` (entry) | fires (`maxRoadTypes=100`, no cloud/noise) | fires (`maxRoadTypes=35`) |
| `[PhaseD13a:terrainbg]` | `W3DTerrainBackground::drawVisiblePolys:777` | — | — |
| `[PhaseD13a:terrain]` | `HeightMapRenderObjClass::Render` | fires (4×4 tiles, 2048 polys/tile) | fires (4×4 tiles) |
| `[PhaseD13a:shadowdecal]` | `W3DProjectedShadowManager::flushDecals` | — | — |
| `[PhaseD13a:shadowstencil]` | `W3DVolumetricShadowManager::renderShadows` | fires (`body=stubbed` MD / `devNull=1` Generals) | fires (`devNull=1`) |
| `[PhaseD13a:stencilquad]` | `renderStenciledPlayerColor` | — | — |
| `[PhaseD13a:water]` | `WaterRenderObjClass::Render` (entry) | fires (`mapLoaded=1`) | fires (`mapLoaded=1`) |
| `[PhaseD13a:shroud]` | `W3DShroud::render` (entry) | fires (`srcTex=null` → bails) | fires (`srcTex=null` → bails) |
| `[PhaseD13a:stub-decal]` | `DX8MeshRendererClass::Add_To_Render_List` stub | — | — |
| `[PhaseD13a:stub-rendertask]` | `DX8TextureCategoryClass::Add_Render_Task` stub | — | — |
| `[PhaseD13a:stub-matpass]` | `DX8FVFCategoryContainer::Add_Visible_Material_Pass` stub | — | — |
| `[PhaseD13a:stub-skin]` | `DX8SkinFVFCategoryContainer::Add_Visible_Skin` stub | — | — |

#### Findings — ZH demo-battle, post-D13a (40 s capture)

**Bibs / decals / shadows / shroud / stubs all silent.**

- `[PhaseD13a:bibs]` 0 fires → no building bibs in shellmap.
- `[PhaseD13a:shadowdecal]` 0 fires → projected-shadow decal path never
  reached (`flushDecals` not called even once; the `if(!m_pDev) return`
  bail is downstream of empty work).
- `[PhaseD13a:stencilquad]` 0 fires → player-color stencil overlay not
  invoked.
- `[PhaseD13a:terrainbg]` 0 fires → `W3DTerrainBackground` is only used
  by `FlatHeightMap`; the demo-battle uses `HeightMapRenderObjClass`.
- All four `[PhaseD13a:stub-*]` loggers: 0 fires. The DX8 batched
  submission paths (`Add_Render_List`, `Add_Render_Task`,
  `Add_Visible_Material_Pass`, `Add_Visible_Skin`) are **never reached**.
  Combined with D11's "0 fall-throughs", every mesh in the menu goes
  through `Render_Mesh_Direct_Bgfx`. The DX8 polygon-renderer / sorting
  / category-container subsystem is dead code on this scene.

**Five paths fire but none draw the white rectangles:**

- `[PhaseD13a:water]` — fires but the dispatched `renderWater()` /
  `renderWaterMesh()` immediately no-op: `renderWater()` iterates
  `PolygonTrigger` water areas (none in the demo-battle map);
  `renderWaterMesh()` early-bails on `if (!m_doWaterGrid) return`
  (the demo-battle shellmap does not enable the water grid).
- `[PhaseD13a:roads]` — fires; `m_maxRoadTypes` is the array capacity
  (35 / 100), not the actual count of roads in the map. Each
  `m_roadTypes[i].getNumIndices()==0` check inside the loop bails per
  road type. Demo battle shellmap has no roads.
- `[PhaseD13a:shroud]` — fires with `srcTex=null` and immediately
  early-bails. Shroud is also off in vanilla.
- `[PhaseD13a:shadowstencil]` — fires; on GeneralsMD the body is
  `#ifdef RTS_RENDERER_DX8` gated (whole function is empty on BGFX); on
  Generals the body falls through to `if(!m_pDev) return` with
  `devNull=1`. Either way no draw.
- `[PhaseD13a:terrain]` — fires; `HeightMapRenderObjClass::Render`
  iterates 4×4 vertex-buffer tiles, 2048 polys/4096 verts each, and
  draws the heightmap. **This is the only non-MeshClass path that
  actually submits geometry to BGFX in the menu scene.**

#### Root cause: heightmap render with ZH-specific data

Vanilla menu fires the **same five probes** with **identical no-draw
behavior** for the four early-bailing paths, yet renders perfectly
(cobblestone + grass courtyard, no white rectangles in
`~/Desktop/d13a-vv.png`). ZH renders the visible large white rectangles
on top of correctly-rendered rocky terrain (`~/Desktop/d13a-zh.png`).

Therefore the white rectangles originate **inside
`HeightMapRenderObjClass::Render` itself**, on the same `Draw_Triangles`
that produces the correctly-rendered tiles — the difference is per-tile
texture-atlas / vertex-color / UV data specific to the ZH demo-battle
map.

This is consistent with the prior memory note
`terrain_vertex_alpha_is_edge_mask`: tiles where `v_color0.a == 1.0` ask
the shader to fully blend the alpha-edge atlas region over the base
sample. If the ZH atlas has white pixels in the edge region OR if stage
1's UV indexes white padding outside the atlas, those tiles render
solid white. The current `fs_terrain.sc`/`vs_terrain.sc` implements the
documented `mix(base, edge, edge.a * v_color0.a)` formula, but the
inputs (atlas texels, UV1, vertex alpha) are not yet verified to match
the DX8 path's expectations on ZH content.

#### Recommended next phase: D13b — Heightmap edge-blend / atlas probe

Replaces the deferred D13b (static-sort flush). Drop `[PhaseD13b:*]`
instrumentation inside `HeightMapRenderObjClass::Render`:

1. On first call, log the actual texture pointers and names for stages
   0/1/2/3 (`m_stageZeroTexture`, `m_stageTwoTexture`,
   `m_stageThreeTexture`).
2. Sample one vertex per tile: log `(v_color0.r,g,b,a, uv0.uv,
   uv1.uv)`. Compare `v_color0.a` distribution between vanilla
   (cobble/grass — works) and ZH (rocky + white squares).
3. If alpha distribution is similar → the issue is texture content
   (probe atlas-texture region pixels at the UV1 sample point).
4. If alpha distribution is dissimilar → the issue is vertex-color
   computation (`BaseHeightMapRenderObjClass::doTheLight`).

Once D13b identifies the exact mismatch, the fix is either an atlas
load swap, a UV remap, or a shader tweak — narrow scope and bounded.

Memory: none yet (instrumentation only; remove or gate behind a build
define after the heightmap fix lands).

### Phase D13b — Heightmap edge-blend / atlas probe + mip-chain fix

Probe inside `HeightMapRenderObjClass::Render` and
`TerrainTextureClass::update`:

- `[PhaseD13b:texstate]` — names + dims of the bound stage 0/2/3
  textures + selected `ShaderTypes` + device pass count.
- `[PhaseD13b:tile]` — per VB-tile (4×4 grid), first vertex's
  position/diffuse/uv0/uv1 plus an alpha histogram across all
  4096 verts.
- `[PhaseD13b:edge]` — per VB-tile, the first vertex with high
  alpha (≥205) — i.e. an edge-blend vertex — to capture UV1 in the
  region where edge blending fires.
- `[PhaseD13b:atlas-base]` — atlas size, placed-tile count,
  pixel-color histogram (zero / white / grey / other).
- `[PhaseD13b:atlas-sample]` — direct CPU sample of atlas texels at
  the same UV coordinates the heightmap probes printed.

#### Findings — atlas content is intact, mip chain was uninitialized

The probes ruled out the obvious failure modes:

- **Stage 0 texture name is empty in BOTH vanilla and ZH** — that's
  expected; the atlas is built procedurally by `TerrainTextureClass`
  and never gets a name. The atlas pixel histograms are healthy in
  both targets (5791 / 614 white pixels out of >10⁶ — mostly genuine
  road-line / cobble texels). Direct atlas samples at vertex UVs
  return real terrain colors (not white).
- **Vertex alpha distribution is healthy** in both targets. Most
  verts have alpha=0 (no edge blend); a tail (~5–15%) have alpha≥205
  (full edge blend). UV1 at high-alpha verts points inside the atlas.
- **Same probes fire on vanilla, which renders correctly** — so the
  shader algebra and the terrain pass are right.

Tracking what was different: the texture is created with
`MIP_LEVELS_3` (terrain atlas) → `bgfx::createTexture2D(... hasMips=true ...)`
allocates a full mip chain, but `BgfxBackend::Update_Texture_RGBA8`
only uploaded mip 0 (`/*mip=*/0`). Mip levels 1–N stay uninitialized
GPU memory until written. Distance-based mip selection in the bgfx
sampler picks higher mips for distant terrain → **garbage / white**
texels for the upper portion of the screen, while the foreground
(mip 0) renders correctly. ZH atlas is 2048×1024 (more mip levels
in play) vs vanilla 2048×512, so ZH is dramatically more affected.

#### Fix — generate full mip chain on Update_Texture_RGBA8

`Core/GameEngineDevice/Source/BGFXDevice/Common/BgfxBackend.cpp`:
after uploading mip 0, walk the recorded mip count
(`m_textureMipCounts`) and for each subsequent level produce a 2×2
box-filter downscale on CPU, then `bgfx::updateTexture2D` it at the
correct mip level. The downscale uses straight (s+2)>>2 averaging to
match the way most GPU mip generators behave for unfiltered RGBA8.

```cpp
// per Update_Texture_RGBA8, after mip-0 upload
auto mipIt = m_textureMipCounts.find(handle);
if (mipIt == m_textureMipCounts.end() || mipIt->second <= 1) return;
const uint8_t totalMips = mipIt->second;
// box-filter src → dst, upload each level, src = dst, repeat
```

#### Verification

`~/Desktop/d13b-fix-vv.png`: vanilla cobblestone + grass with proper
edge blending and water puddles — the cleanest vanilla menu render
to date.

`~/Desktop/d13b-fix-zh.png`: ZH demo-battle terrain restored (the
huge white rectangles are gone — replaced by the actual sand /
turquoise water / concrete pad textures). The reference for what an
"early frame" should look like is `~/Desktop/generals-zh.png`.

Note: a separate progressive bug remains — see Phase D13c. The mip
fix is independent of that and is a strict improvement.

Memory: TBD after D13c lands.

- Per-vertex material arrays (`Has_Material_Array(pass)`) and
  per-polygon shader arrays (`Has_Shader_Array(pass)`) are not yet
  honored — slow path uses `Peek_Material(0, pass)` + the single
  per-pass shader. No menu mesh exercises this; revisit when
  in-game scenes need it.
- Stage-1 UV: `MeshMatDescClass::UVSource[pass][1]` may map stage 1
  to UV array 1, but the vertex layout still carries only UV0.
  When the slow path renders multi-stage materials whose stage 1
  uses UV1, sampling will use UV0 instead. None of the four D12
  meshes trip this in the menu, but it's a known incorrectness.

## Known issues still open

### Visible white rectangles in the ZH demo-battle shellmap — **fixed (D13b)**

Closed by `BgfxBackend::Update_Texture_RGBA8` mip-chain generation.
The terrain atlas was created with `MIP_LEVELS_3` but only mip 0 was
ever uploaded; distance-based sampling pulled garbage from
uninitialized mips 1–N, painting white where the camera selected
higher mip levels (the upper / distant portion of the screen).

### Progressive "silver triangle" flicker on ZH demo battle  *(open — D13c)*

`~/Desktop/d13b-fix-zh.png` and `~/Desktop/generals-zh.png` show the
post-D13b clean initial render. After several seconds of demo-battle
combat, axis-aligned silver/grey flat triangles accumulate over the
terrain until they dominate the screen. Vanilla does **not** show
this (no combat → no FX). Tracks contribute at most 11 edges (~22
triangles) — too small to be the source. See Phase D13c probe plan.

### Decal meshes / shadow projectors

Confirmed stubbed and **silent** on the demo-battle shellmap (D13a):
`flushDecals()`, `renderShadows()` (volumetric), and the four DX8
batched-submission stubs in `ww3d2_bgfx_stubs.cpp` are never called.
These will become relevant in in-game scenes (decals from impacts,
shadow projection from units). Defer until a gameplay scene is
loadable.

### UI fonts and OpenAL playback pipeline

Pre-existing — see `bgfx_build_subsystem_stubs.md`. Fonts +
playback pipeline are empty stubs on the BGFX build; UI buttons in
ZH menu show no labels and the menu has no shell music or click
sounds. Tracked in `docs/ZH-MainMenu-Bugs.md`.

### `[SurfaceClass:bgfx] Copy(surface->surface) format mismatch` stub

Informational. Fires once at startup. No callers expected yet on the
BGFX path; will become real when render-target round-trip code
(off-screen passes, screenshot frame buffers) needs surface-to-surface
copies.

## Next steps (suggested phase order)

D11 (instrumentation), D12 (multi-pass + texture-array), and D13a
(non-MeshClass probe) are **complete** — see the per-phase sections
above. The phase order below is what remains.

### Phase D13c — Progressive growth source  *(landed — narrowed to MeshClass)*

After D13b's mip fix, the ZH demo-battle initial frame renders
correctly (terrain texturing restored — see `~/Desktop/d13b-fix-zh.png`
and `~/Desktop/generals-zh.png`), but **silver flat triangles
accumulate over time** until they dominate the screen. Vanilla menu
does **not** exhibit this — same probes fire identically (terrain,
water, roads, shroud, shadowstencil), but vanilla has no demo-battle
combat to spawn the accumulating geometry.

D13b probes ruled out the obvious accumulators on the menu scene:

- **`[PhaseD13b:tracks]`**: tank tread `m_edgesToFlush` peaks at 11
  edges across the whole demo battle (≈22 triangles total) — too small
  to dominate the screen.
- **`[PhaseD13b:extrablend]`**: 88 3-way blend tiles, but the count is
  static (set at heightmap init), not progressive.
- **`[PhaseD13b:shoreline]`**: `numShoreLineTiles=0` → silent, ruled out.

#### D13c probe (Phase D13c.1)

Two new diagnostics added to `BgfxBackend`:

1. **Per-source draw-call accounting** — every Draw_Triangles_Dynamic
   call is binned by a `Set_Source_Tag(IRenderBackend::DrawSourceTag)`
   set just before the draw. Tags wired into:
   `MeshDirectRender.cpp` (`kSrcMeshDirect`), `HeightMap.cpp` (`kSrcTerrain`
   + `kSrcExtraBlend`), `pointgr.cpp` (`kSrcParticle`),
   `W3DTerrainTracks.cpp` (`kSrcTracks`), `W3DRoadBuffer.cpp` (`kSrcRoads`,
   both targets), `W3DBibBuffer.cpp` (`kSrcBibs`, both targets).
   `BgfxBackend::End_Scene` snapshots
   `[PhaseD13c:framedraw]` at frames 1, 30, 60, 120, 300, 600, 1200, 1800,
   2400, 3000.
2. **Per-mesh-name accumulation** — `MeshDirectRender.cpp` aggregates
   `mesh.Get_Name() → (calls, tris)` per frame; `Phase_D13c_Mesh_Frame_End`
   dumps the top-12 contributors at the same logarithmic frame indices
   (`[PhaseD13c:meshtop]`) and clears the map.

##### D13c findings

ZH 60-second capture (matched stderr `/tmp/zh-d13c.stderr`,
screenshot `~/Desktop/d13c-zh.png`):

```
frame=60   drawCalls=114 tris=78759 |mesh=76/5915  terrain=32/65536 roads=5/7262 …
frame=120  drawCalls=128 tris=79723 |mesh=84/6781  terrain=32/65536 roads=5/7258 …
frame=300  drawCalls=210 tris=85610 |mesh=161/12010 terrain=32/65536 roads=6/7476 …
```

- **`mesh` is the only category that grows over time.** Calls go
  76→84→161 (≈+85 calls in 240 frames). Tris go 5915→12010
  (≈+5887). Avg tris/call stays at ~75 — same kind of meshes,
  just more of them per frame.
- **`terrain`, `roads`, `extraBlend` are stable.** Locked at 32 / 5–6 / 1
  calls per frame. Not progressive.
- **`particle`, `bibs` never fire** in the demo-battle shellmap.
- **`tracks` grows from 0 → 10/496 tris** but stays below 1% of
  the frame total — not the visual culprit.

Per-mesh top-12 at frame 300 (sorted by tris):

```
UIRGRD_SKN.REPGRD_SKN  13/2041   ← skin: USA Ranger     (was 3/471 @ f60)
UITRST_SKN.UITRST_SKIN  9/1530   ← skin: USA Terrorist  (was 1/170 @ f60)
UITUNF_SKN.UITUNF_SKN   7/1246   ← skin: new since f120
AVBATTLESH.CHASSIS      2/1132   ← static (2 battleships, no growth)
NBWALL.BOX01            8/992    ← static-mesh wall segment, 6→8 calls
PRG02 / PRG14           2/722 each
AVAMPHIB.CHASSIS        1/414
UVCOMBIKEG.HANDLEBAR    3/294
AVCHINOOKAG.CHASSIS     1/184
```

The growth is dominated by **infantry skin meshes**
(`UIRGRD_SKN`, `UITRST_SKN`, `UITUNF_SKN`) with secondary contribution
from instanced static meshes (`NBWALL.BOX01` wall segments).

##### Status

- **Progressive growth is real.** `mesh` source draws +112% calls
  (76→161) and +103% tris (5915→12010) between f60 and f300.
- **Source narrowed.** Confined to `Render_Mesh_Direct_Bgfx`. Not
  terrain, particles, tracks, bibs, roads, extra-blend.
- **Visual evidence** in `~/Desktop/d13c-zh.png`: the gray rectangles
  scattered across the water + far terrain are flat, axis-aligned, and
  don't have the silhouette of correctly-rendered units (the bottom-
  right tank renders properly with detail). Hypothesis: skin-mesh CPU
  skinning or LOD selection drops vertices to a degenerate
  configuration on a subset of submissions, making them collapse to
  flat ground-plane rectangles.

### Phase D13c.2 — Mesh bbox + bone diagnostic *(landed — root cause found)*

Added a one-shot per-mesh-name probe inside `Render_Mesh_Direct_Bgfx`:
model-space vertex bbox + skin-mesh `htree->Num_Pivots()` +
`MAX(bonelink[])` + count of out-of-range bone-link entries.

**Findings (`/tmp/zh-d13c2b.stderr`):**

```
UIRGRD_SKN.REPGRD_SKN  htreePivots=1 maxBone=16 oobBonelinks=178/178
UITRST_SKN.UITRST_SKIN htreePivots=1 maxBone=26 oobBonelinks=207/207
UITUNF_SKN.UITUNF_SKN  htreePivots=1 maxBone=25 oobBonelinks=173/173
```

**Every** skin mesh has `htreePivots=1` and **100% of its bone-link
entries are out of range**. CPU skinning calls
`htree->Get_Transform(bonelink[vi])` which (in release builds) skips
the bounds `assert` and reads OOB memory inside the `Pivot[]` array,
producing junk matrices and garbage vertex positions in the 1e+19
range. The skinned verts then submit as flat collapsed quads — exactly
the gray rectangles visible in `~/Desktop/d13c-zh.png`.

**Root cause:** `WW3DAssetManager::Get_HTree` is stubbed to `nullptr`
in `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d2_bgfx_stubs.cpp:211`.
`Animatable3DObjClass` (the HLOD's base) calls it in its ctor; on
nullptr it falls back to `HTreeClass::Init_Default()` which allocates
**1 pivot** (just the root). Skinned units expect a full skeleton
(16-26 bones); the bone-link array is built against the real skeleton
in the .w3d file but the runtime HTree has only 1 pivot, so every
weighted vertex looks up an OOB bone.

The full pipeline that's stubbed in BGFX:

- `WW3DAssetManager::Get_HTree(name)` → returns `nullptr`
- `WW3DAssetManager::Load_3D_Assets(...)` → returns `false`
- `WW3DAssetManager::Find_Prototype(name)` → returns `nullptr`
- `WW3DAssetManager::Add_Prototype(...)` → no-op
- `AggregateLoaderClass::Load_W3D` → returns `nullptr`
- All `MotionChannelClass`/`BitChannelClass`/`TimeCodedMotionChannelClass`
  `::Load_W3D` → return `false`

Texture loading + non-skin mesh data work because `TextureClass::Init`
has its own `BgfxTextureCache::Get_Or_Load_File` path that bypasses
the asset manager. Skinned meshes don't have that escape hatch — they
need the HTree.

### Phase D13c.3 — Workaround: drop bone-OOR skin submissions  *(landed)*

In `Render_Mesh_Direct_Bgfx`, before `Get_Deformed_Vertices`, walk the
mesh's bone-link array and check `blinks[i] < htree->Num_Pivots()`.
If any entry is out of range, return a new
`kDirectSubmit_SkinBoneOutOfRange` status and skip the draw. Logs once
per unique mesh name as `[PhaseD13c3:skipBoneOOR] name=… htreePivots=1
badBoneIdx=N`. ZH demo battle now skips `UIRGRD_SKN`, `UITRST_SKN`,
`UITUNF_SKN` (3 mesh names, ≈29 instances/frame at frame 300) instead
of submitting 1e+19-extent garbage geometry. The collapsed silver
infantry-rectangles are gone; remaining gray polygons in
`~/Desktop/d13c3-zh.png` are static props (`PRG02..17 → prgrey.tga`,
`NBWALL.BOX01 → nbwall.tga`) rendering correctly.

This is a **bandage, not a fix.** Real fix is Phase D14 — wire
`WW3DAssetManager::Get_HTree` to actually load the `.w3d`
hierarchy chunks via `HTreeManager`.

### Phase D14 — Wire HTree loading in BGFX  *(landed 2026-05-04)*

Wired the WW3DAssetManager loader pipeline end-to-end on BGFX.

**Implementation** (both `Generals/` and `GeneralsMD/` `ww3d2_bgfx_stubs.cpp`):

1. ✅ `Get_HTree(name)` — mirrors `assetmgr.cpp:1019`: looks up via
   `HTreeManager.Get_Tree`, falls back to `Load_3D_Assets("<name>.w3d")`
   when load-on-demand is enabled (set in `W3DDisplay.cpp:737`).
2. ✅ `Load_3D_Assets(FileClass&)` — replaced the chunk-skipping stub
   with the real switch dispatcher: `W3D_CHUNK_HIERARCHY → HTreeManager.Load_Tree`,
   `W3D_CHUNK_ANIMATION{,_COMPRESSED,_MORPH} → HAnimManager.Load_Anim`,
   default → `Load_Prototype(cload)` (existing chunk-loader registry).
3. ✅ `Get_HAnim(name)` — mirrors `assetmgr.cpp:965` so future
   animated-pose work doesn't need another wireup.
4. ✅ Vanilla Generals stub brought up to GeneralsMD parity:
   `Find_Prototype` / `Add_Prototype` / `Register_Prototype_Loader` /
   `Find_Prototype_Loader` / `Load_Prototype` / `Create_Render_Obj` /
   `Render_Obj_Exists` are all real implementations now.
5. ✅ All built-in prototype loaders registered in the BGFX ctor:
   `_MeshLoader, _HModelLoader, _CollectionLoader, _BoxLoader,
    _HLodLoader, _DistLODLoader, _AggregateLoader, _NullLoader,
    _DazzleLoader, _RingLoader, _SphereLoader`.
6. ✅ `Free_Assets` walks the prototype list and clears HTree/HAnim
   managers so the singleton can shut down cleanly.

**Verification** (`/tmp/zh-d14.stderr` + `~/Desktop/d14-zh.png`):

| skin mesh | pre-D14 | post-D14 |
|-----------|---------|----------|
| UIRGRD_SKN.REPGRD_SKN | htreePivots=**1** maxBone=16 (OOR-skipped) | htreePivots=**19** maxBone=16 oobBonelinks=0/178 |
| UITRST_SKN.UITRST_SKIN | htreePivots=**1** maxBone=26 (OOR-skipped) | htreePivots=**27** maxBone=26 oobBonelinks=0/207 |
| UITUNF_SKN.UITUNF_SKN | htreePivots=**1** maxBone=25 (OOR-skipped) | htreePivots=**32** maxBone=25 oobBonelinks=0/173 |

`[PhaseD13c3:skipBoneOOR]` no longer fires. Per-mesh frame-300 top
contributors now include all three skin meshes (UIRGRD: 13/2041,
UITRST: 9/1530, UITUNF: 7/1246). Skinned units appear as small
white silhouettes upper-left of the demo-battle screenshot
(positions vary per frame); pre-D14 they were silently dropped.

**Out of scope (still observed):** Large gray rectangles on the demo
battle terrain are static rocks (`PRG02-17`) and walls (`NBWALL.BOX01`);
their `[PhaseD13c2:bbox]` extents are sane, so they're not D14-related.
Likely texture/material binding issues — track separately as D15+.

**D13c.3 bandage retained:** the OOR skip in `Render_Mesh_Direct_Bgfx`
still acts as a safety net for any skin mesh whose HTree fails to load.
Costs nothing when HTrees are available (the loop short-circuits on
the first in-range bonelink).

### Phase D15 — Un-stub motion channel loaders  *(landed 2026-05-04)*

D14 wired `Get_HAnim` and `Load_Anim`, but skinned units stayed in bind
pose because the motion channel `Load_W3D` calls were silently linking
against stub bodies in `ww3d2_bgfx_stubs.cpp` (returning `false`). The
real implementations in `motchan.cpp` are pure math + chunk reading
(no DX8 calls), so they compile cleanly under BGFX once the gate is
lifted.

**Implementation:**

1. **GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/motchan.cpp** —
   removed the `#ifdef RTS_RENDERER_DX8 ... #endif` wrapper around
   the file body. (Vanilla Generals' motchan.cpp was already un-gated.)
2. **{Generals,GeneralsMD}/Code/Libraries/Source/WWVegas/WW3D2/ww3d2_bgfx_stubs.cpp** —
   removed the duplicate stub bodies for `MotionChannelClass`,
   `BitChannelClass`, `TimeCodedMotionChannelClass`,
   `AdaptiveDeltaMotionChannelClass`, and `TimeCodedBitChannelClass`
   (constructors, destructors, `Load_W3D`, `Get_Vector`, `Get_QuatVector`,
   `Get_Bit`).

**Verification** (`/tmp/zh-d15.stderr` + `~/Desktop/d15-zh.png`):

Skin-mesh `[PhaseD13c2:bbox]` extents now vary per frame (animation-
driven deformation):

| Mesh | Pre-D15 ext | Post-D15 ext (frame N) |
|------|-------------|------------------------|
| UIRGRD_SKN.REPGRD_SKN | 15.50 × 16.64 × 14.91 | **8.75 × 7.71 × 14.73** |
| UITRST_SKN.UITRST_SKIN | 12.08 × 11.02 × 14.74 | **6.10 × 7.45 × 13.71** |
| UITUNF_SKN.UITUNF_SKN | 12.12 × 11.70 × 14.88 | **9.38 × 9.45 × 14.71** |

Tightening of the bbox confirms HAnim is moving the bones. Mesh
draw-call count grew (40 → 79 at frame 60, 100 → 124 at frame 300)
because animated unit instances are now driving more visibility/LOD
state changes.

### Phase D16 — Un-stub particle emitter loaders  *(landed 2026-05-04)*

Same pattern as D15 (gate-lifted real impl beats nullptr stubs).
`.w3d`-defined `ParticleEmitterDefClass` / `ParticleEmitterClass`
prototypes now load on the BGFX path. Engine-side particle effects
(INI-driven `ParticleSystemManager`) are a separate path; this phase
only covers the W3D-asset side.

**Implementation:**

1. **GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/part_ldr.cpp** —
   removed `#ifdef RTS_RENDERER_DX8` wrapper. Pure chunk-walk +
   setter calls; no DX8 references.
2. **GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/part_emt.cpp** —
   same gate lift. No DX8 calls (pointgr.cpp on the BGFX path
   submits the actual triangles).
3. **{Generals,GeneralsMD}/Code/Libraries/Source/WWVegas/WW3D2/ww3d2_bgfx_stubs.cpp** —
   removed `ParticleEmitterClass::Reset()` and
   `ParticleEmitterLoaderClass::Load_W3D` / global stubs. Kept the
   `AggregateLoader` stub (composes multiple render objects per .w3d;
   not on the menu path).

**Verification:**

`nm libww3d2.a` confirms `ParticleEmitterLoaderClass::Load_W3D` now
resolves to the real `part_ldr.cpp` body (not the nullptr stub).
The ZH demo-battle menu's `[PhaseD13c:framedraw]` particle counter
still reads 0 because the menu shellmap's effects are driven by the
engine's INI-based `ParticleSystemManager`, which is a parallel
pipeline through `W3DParticleSys.cpp` / `pointgr.cpp` and was not
the bottleneck. D16 is a structural prerequisite for in-game scenes
that bake `.w3d` particle emitters into props (smokestacks, fountains,
sparks).

### Phase D17 — Wire INI-driven particle pipeline  *(landed 2026-05-04)*

D16 brought `.w3d` emitter loaders online but the menu particle counter
stayed at `particle=0/0` because the demo-battle shellmap drives effects
through `ParticleSystemManager` (INI), not `.w3d`-baked emitters. Two
gates kept that pipeline dead on the BGFX build:

1. **`W3DScene.cpp` queueParticleRender** — the
   `TheParticleSystemManager->queueParticleRender()` call inside
   `RTS3DScene::render` was wrapped in `#ifdef RTS_RENDERER_DX8`, so
   `m_readyToRender` never flipped and `doParticles` early-returned.
2. **`PointGroupClass::_Init` skipped** —
   `DX8Wrapper::Do_Onetime_Device_Dependent_Inits` (BGFX branch in
   `dx8wrapper.cpp:4554`) explicitly skipped the call. Without it the
   static `_TriVertexUVFrameTable` / `_QuadVertexUVFrameTable` arrays
   stayed `nullptr`, the orientation tables stayed zero-filled, and the
   shared `Tris`/`Quads` index buffers were never created. First
   `Update_Arrays(quad_size_orient)` call segfaults reading the UV
   table.

**Then a second-order bug:** even with both fixed, lifting the gate
blanks the entire 3D scene (ZH and vanilla). Root cause:
`PointGroupClass::Render` writes `D3DTS_VIEW=identity` (its vertices
are CPU-pretransformed to view space). `BgfxBackend::Set_View_Transform`
funnels that into `m_view3DMtx`, and at `frame()` time bgfx applies the
last `setViewTransform(kView3D, ...)` to *all* prior submits to that
view — meshes/terrain rendered earlier in the frame retroactively use
identity view, putting them outside the frustum.

**Implementation:**

1. **`Core/.../BgfxBackend.h`** — added `kView3DPart = 2` constant,
   `m_view3DPartMtx[16]` / `m_proj3DPartMtx[16]` slots, and
   `m_view3DPartDirty`. Bumped `m_nextViewId = 3` so RT views still
   start above the reserved range.
2. **`Core/.../BgfxBackend.cpp`** —
   - `Init`: configure `kView3DPart` viewport / clear / mode and call
     `setViewOrder(0, 3, [kView3D, kView3DPart, kView2D])` so particles
     draw above terrain/meshes but below the HUD.
   - `Resize`: re-issue `setViewRect` for `kView3DPart`.
   - `Begin_Scene`: `bgfx::touch(kView3DPart)` so empty-particle frames
     don't break the order.
   - `Set_View_Transform`: when `m_phaseD13cSourceTag == kSrcParticle`
     and not 2D, route into `m_view3DPartMtx` instead of `m_view3DMtx`.
     The kView3D camera matrix is left intact.
   - `Set_Projection_Transform`: mirror every 3D projection write into
     `m_proj3DPartMtx` so particles share the camera projection at
     frame() time. Particle-tag projection writes go into the particle
     slot only.
   - `ApplyDrawState`: dispatch to `kView3DPart` when the source tag
     is `kSrcParticle`.
   - `UpdateViewOrder`: include `kView3DPart` in the explicit RT-first
     order list.
3. **`Generals/+GeneralsMD/Code/.../W3DScene.cpp`** — removed the
   `#ifdef RTS_RENDERER_DX8` wrapper around `queueParticleRender`.
4. **`Core/.../dx8wrapper.cpp`** — `Do_Onetime_Device_Dependent_Inits`
   on the BGFX branch now calls `PointGroupClass::_Init()` after
   `VertexMaterialClass::Init()`. The shared Tris/Quads index buffers
   write into a CPU shadow on the BGFX path — no DX device required.

**Verification** (`/tmp/zh-d17.stderr` + `~/Desktop/d17-{zh,vv}.png`):

| target | pre-D17 particle counter | post-D17 particle counter |
|--------|--------------------------|---------------------------|
| ZH (frame 60)  | 0/0 | **21 / 140** |
| ZH (frame 120) | 0/0 | **30 / 350** |
| ZH (frame 300) | 0/0 | **57 / 676** |
| Vanilla (frame 60)  | 0/0 | **8 / 120** |
| Vanilla (frame 120) | 0/0 | **10 / 388** |

3D scene is intact in both screenshots (terrain, meshes, infantry,
vehicles all visible). Particles render but appear as dark blobs —
likely a separate texture/shader-binding issue tracked as a D17
follow-up (ALPHA / ADDITIVE / ALPHA_TEST / MULTIPLY shader presets
need a BGFX-side review now that they're actually exercised). Vanilla
menu now shows a full demo scene (palm trees, building with domes,
vehicles, soldiers) where it previously rendered a static cobblestone
patch — a major secondary win.

Memory: `bgfx_view3dpart_for_particles.md`.

### Phase D17b — Shader presets bit-pattern fix  *(landed 2026-05-05)*

D17 lit up the particle pipeline but every emitter rendered as opaque
black quads — explosion / smoke textures have black backgrounds with
alpha for the actual shape, so opaque blending paints the background
verbatim. The `[PhaseD17:particle]` probe (added to
`PointGroupClass::Render`) showed every preset reporting
`srcBlend=1 dstBlend=0` — i.e. `SRCBLEND_ONE / DSTBLEND_ZERO` (opaque)
instead of the additive / alpha modes the particle systems requested.

Root cause: in `{Generals,GeneralsMD}/Code/Libraries/Source/WWVegas/WW3D2/shader.cpp`
the `#ifndef RTS_RENDERER_DX8` block default-initialised every preset
(`_PresetAdditiveSpriteShader = ShaderClass();` etc.) with an old
comment claiming "the exact preset values are not load-bearing because
nothing in bgfx mode dispatches off them — we just need the symbols to
exist." That was true before D17. After D17, `PointGroupClass::Render`
reads the preset blend modes verbatim via `Shader.Get_Src_Blend_Func()` /
`Shader.Get_Dst_Blend_Func()`.

**Implementation:**

Replaced the zero-init block in both `Generals/` and `GeneralsMD/`
copies with the same `SHADE_CNST` bit patterns the DX8 branch above
uses — `_PresetAdditiveSpriteShader(SC_ADD_SPRITE)` →
`SRCBLEND_ONE / DSTBLEND_ONE / DEPTH_WRITE_DISABLE`,
`_PresetAlphaSpriteShader(SC_ALPHA_SPRITE)` →
`SRCBLEND_SRC_ALPHA / DSTBLEND_ONE_MINUS_SRC_ALPHA / DEPTH_WRITE_DISABLE`,
and the rest of the preset table. All 22 presets now match the DX8
path bit-for-bit.

**Verification** (`/tmp/zh-d17c.stderr` + `~/Desktop/d17c-{zh,vv}.png`):

| target | pre-D17b particle render | post-D17b particle render |
|--------|--------------------------|----------------------------|
| ZH (`exshockwav.tga`) | `srcBlend=1 dstBlend=0` (opaque) | `srcBlend=1 dstBlend=1` (additive) |
| ZH (`excloud01.tga`)  | `srcBlend=1 dstBlend=0` (opaque) | `srcBlend=2 dstBlend=5` (alpha) |
| ZH (`depthMask`)      | `1` (write Z) | `0` (no Z write — correct for translucent) |

Visual: the giant grey rectangles that haunted ZH demo-battle since
D13 are gone. They were never rocks — they were sprite/effect quads
(from explosions, scorch marks, decals routed through PointGroupClass
or a similar preset-driven path) painting their texture's black
background opaquely. With proper additive/alpha presets, those
sprites composite over the terrain correctly. Smoke clouds, infantry
silhouettes, ZH logo and the 3D rock formations all render as
expected. Vanilla menu shows the demo scene cleanly without the
opaque-black smudges that D17 introduced.

Memory: `bgfx_shader_presets_bgfx_path.md`.

### Phase D17c — Sort renderer un-stub + source-tag preservation  *(landed 2026-05-05)*

D17 + D17b got particle data flowing and gave the presets correct blend
modes, but two follow-up issues remained:

1. **GeneralsMD's `sortingrenderer.cpp` was fully stubbed on BGFX.** The
   entire body sat under `#ifdef RTS_RENDERER_DX8` and the BGFX path
   provided empty `Flush()` / `Insert_Triangles` overloads. Any
   translucent particle (additive / alpha — almost everything after
   D17b's preset fix) hit `SortingRendererClass::Insert_Triangles` and
   was dropped on the floor. Vanilla Generals' copy was already
   un-gated; only ZH was affected.
2. **Sort flush didn't re-assert the source tag.** Even on vanilla,
   sort-flushed particles drew through `DX8Wrapper::Draw_Triangles`
   tagged as whatever `Set_Source_Tag` was last called with — usually
   `kSrcMeshDirect`. That kept `[PhaseD13c:framedraw]` reporting
   `particle=0/0` *and* routed those draws through `kView3D` instead of
   the `kView3DPart` slot D17 set up for particles, leaving them at the
   mercy of whatever camera matrix kView3D had latched.
3. **`Apply_Render_State` helper used `_Set_DX8_Transform`.** That
   expands to `DX8CALL(...)` which is a no-op on BGFX, so the per-node
   world+view captured at insert time never reached the backend. For
   particles the slot already held identity (PointGroupClass writes it
   before the insert), so visually OK; for any future non-particle
   sort items the captured camera transform would silently drop.

**Implementation:**

1. **`GeneralsMD/.../sortingrenderer.cpp`** — removed the
   `#ifdef RTS_RENDERER_DX8` wrapper around the file body and the BGFX-
   stub block at the end. Now mirrors vanilla's un-gated copy.
2. **`Core/.../IRenderBackend.h`** — added a `virtual unsigned
   Get_Source_Tag() const { return kSrcUnknown; }` companion to
   `Set_Source_Tag` so deferred submitters can capture the active tag
   at insert time.
3. **`Core/.../BgfxBackend.h`** — overrode `Get_Source_Tag` to return
   `m_phaseD13cSourceTag`.
4. **Both `sortingrenderer.cpp` copies** — added a `unsigned source_tag`
   field to `SortingNodeStruct`. `Insert_Triangles` and
   `Insert_VolumeParticle` capture the active backend tag into the
   node. Both `Flush()` (non-pool branch) and `Flush_Sorting_Pool()`
   (pre-batched draws) re-assert `state->source_tag` via
   `IRenderBackend::Set_Source_Tag(...)` immediately before
   `DX8Wrapper::Draw_Triangles(...)`.
5. **Both `sortingrenderer.cpp` copies** — replaced
   `_Set_DX8_Transform(...)` in the `Apply_Render_State` helper with
   `DX8Wrapper::Set_Transform(D3DTS_{WORLD,VIEW}, ...)`. The inline
   wrapper sets dirty bits + caches into `render_state`; the next
   `Apply_Render_State_Changes` (run from inside `Draw_Triangles`)
   drains them to the backend. DX8 path is functionally unchanged.
6. **`Core/.../pointgr.cpp`** — hoisted the `Set_Source_Tag(kSrcParticle)`
   call out of the immediate-draw branch so both routes (sort insert
   and direct draw) tag before submitting.

**Verification** (`/tmp/{zh,vv}-d17c.stderr` + `~/Desktop/d17c2-{zh,vv}.png`):

| target | pre-D17c counter | post-D17c counter |
|--------|------------------|-------------------|
| ZH (frame 60)  | `particle=0/0` | `particle=31/140` |
| ZH (frame 120) | `particle=0/0` | `particle=144/350` |
| ZH (frame 300) | `particle=0/0` | `particle=425/1046` |
| Vanilla (frame 60) | `particle=8/120` | `particle=8/120` (already routed) |
| Vanilla (frame 120) | `particle=11/388` | `particle=11/388` (already routed) |

ZH now sees translucent particles flow through the sort path; the f300
counter jumped from 0 to 425 calls / 1046 tris because explosions /
smoke clouds accumulate over the demo-battle timeline. Vanilla's count
is unchanged because its menu effects are mostly alpha-tested
(immediate draw) — the sort coverage is still active, it's just rare on
that scene.

Visually both targets render cleanly: ZH demo-battle keeps the smoke
clouds composing over terrain (and the dark explosion patches near the
helicopter group are now genuinely translucent rather than the
opaque-black D17b artefacts), and vanilla shows the full demo scene
with palm trees / domed building / vehicles intact. No regressions in
mesh / terrain / road rendering.

### Phase D18 — Skin-mesh white-silhouette fix (skip texture recolor on BGFX)  *(landed 2026-05-05)*

After D17c the ZH demo-battle still showed infantry as small white
silhouettes near the top of the screen (`UIRGRD_SKN`, `UITRST_SKN`,
`UITUNF_SKN`). `[PhaseD11:directsubmit]` reported their texture stage
as `#-16711936#zhca_uirguard.tga` (and similar for the other two skin
units) — the W3D house-color recolor pipeline had run on the parent
HLOD and written a per-instance recoloured `TextureClass` into the
material.

Root cause:

1. `W3DAssetManager::Recolor_Asset` → `Recolor_Mesh` → `Recolor_Texture`
   → `Recolor_Texture_One_Time` (Generals + GeneralsMD copies).
2. `Recolor_Texture_One_Time` calls `texture->Get_Surface_Level(0)` to
   pull the original ZHCA pixels for palette remapping.
3. `TextureClass::Get_Surface_Level` requires a non-null
   `Peek_D3D_Texture()` (calls `GetSurfaceLevel(level, &d3d_surface)`).
   On the BGFX path that pointer is permanently nullptr — texture data
   lives in `BgfxTextureCache` handles, not in a `IDirect3DTexture8`.
4. `Get_Surface_Level` returns nullptr; `desc` from `Get_Level_Description`
   is left zero-init; `newsurf = NEW_REF(SurfaceClass, (0, 0, fmt))` is
   a 0×0 surface; `NEW_REF(TextureClass, (newsurf, ...))` skips the
   `Width && Height` guard in the surface-ctor (`texture.cpp:618`) and
   never calls `Create_Texture_RGBA8` → final `TextureClass` has bgfx
   handle = 0 → bound stage falls back to the placeholder white texture
   in the BGFX backend.

Implementation (both `Generals/` and `GeneralsMD/`
`W3DDevice/GameClient/W3DAssetManager.cpp`): early-return `nullptr` on
the BGFX branch from both overloads of `Recolor_Texture_One_Time`
(team-color `int` and `Vector3 hsv_shift`). `Recolor_Mesh`'s `if
(newtex)` guard then leaves the original texture bound on the mesh,
so skin units render their bare ZHCA base texture. They lose the
team-color tint until a proper BGFX recolor pipeline lands, but visible
content beats white silhouettes.

**Verification** (`/tmp/{zh,vv}-d18.stderr` + `~/Desktop/d18-{zh,vv}.png`):

| skin mesh | pre-D18 stage0 | post-D18 stage0 |
|-----------|----------------|------------------|
| UIRGRD_SKN.REPGRD_SKN | `#-16711936#zhca_uirguard.tga` (no bgfx handle → white) | `zhca_uirguard.tga` (loaded via BgfxTextureCache) |
| UITRST_SKN.UITRST_SKIN | `#-16711936#zhca_uiter.tga` | `zhca_uiter.tga` |
| UITUNF_SKN.UITUNF_SKN | `#-16711936#zhca_uirtunfan.tga` | `zhca_uirtunfan.tga` |
| UITUNF_SKN.WARHEAD | `#-16711936#zhca_uirtunfan.tga` | `zhca_uirtunfan.tga` |

`[PhaseD13c:framedraw]` and `[PhaseD13c:meshtop]` numbers are unchanged
post-D18 (mesh=120/11636 @ f300, particle=330/676 @ f300, top-12
unchanged). The skin units are now visible as small textured infantry
shapes near the top of `~/Desktop/d18-zh.png` instead of the pure
white blobs in `~/Desktop/d17c2-zh.png`. Vanilla menu screenshot is
visually identical to `~/Desktop/d17c2-vv.png` (no `Recolor_Asset` is
ever called for the cobblestone-courtyard scene).

`AVBATTLESH.HOUSECOLOR01` etc. are *not* affected by this issue —
their texture stage was already `housecolor.tga` / `housecolor2.tga`
(no `#NNNN#` prefix) because they go through `Recolor_Vertex_Material`
(modifies the vertex material's diffuse color, no texture replacement)
rather than `Recolor_Texture`.

Memory: `bgfx_recolor_texture_one_time_d18.md`.

### Phase D13d — Static-sort list flush  *(deferred indefinitely)*

D11 showed **0 meshes deferred** in the ZH menu — every mesh in the
demo-battle shellmap has `sort_level=0`. Static-sort wiring is not
blocking shellmap rendering. Revisit when an in-game scene with
sort-level-tagged geometry (alpha foliage, water, etc.) is loadable.
Original prescription: wire a BGFX-side flush in
`StaticSortListClass::Render_And_Clear` that calls
`Render_Mesh_Direct_Bgfx` per mesh, and initialize
`DefaultStaticSortLists` in BGFX boot.

### Phase D14 — Decal mesh / shadow port  *(deferred — not menu-blocking)*

Confirmed silent on the menu via D13a. Visible-impact items: terrain
decals (build markers, weapon impact craters, footprints) and
projected unit shadows. `DecalMeshClass` and `W3DProjectedShadow`
both wire through stubbed paths in `ww3d2_bgfx_stubs.cpp` /
DX8-Wrapper. Defer until gameplay scenarios are loadable.

### Phase D15 — UI fonts + OpenAL playback

Outside mesh-render scope. Distinct effort; track via
`docs/ZH-MainMenu-Bugs.md` and `bgfx_build_subsystem_stubs.md`.
Without these the ZH menu remains unusable as a gameplay entry
point even with all mesh issues resolved.

## Verification commands

```bash
# Build both targets.
scripts/build-osx.sh --target both

# Screenshot ZH and vanilla menus (skips intro videos via -screenshot).
rm -f /tmp/zh.tga /tmp/zh.stderr
scripts/run-game.sh --target zh -- -win -screenshot /tmp/zh.tga 2>/tmp/zh.stderr &
sleep 40 && kill %1 2>/dev/null && wait
sips -s format png /tmp/zh.tga --out ~/Desktop/zh.png

rm -f /tmp/vv.tga /tmp/vv.stderr
scripts/run-game.sh --target generals -- -win -screenshot /tmp/vv.tga 2>/tmp/vv.stderr &
sleep 40 && kill %1 2>/dev/null && wait
sips -s format png /tmp/vv.tga --out ~/Desktop/vv.png

# D11 inventory analysis
grep '\[PhaseD11:directsubmit\]' /tmp/zh.stderr | sort -u
grep '\[PhaseD11:staticsort-defer\]' /tmp/zh.stderr | sort -u
grep '\[PhaseD11:staticsort-null\]' /tmp/zh.stderr
grep '\[PhaseD11:fallthrough\]' /tmp/zh.stderr | sort -u

# D12 slow-path tally
grep '\[PhaseD12:slowpath\]' /tmp/zh.stderr | sort -u

# D13a non-MeshClass probe
grep '\[PhaseD13a:' /tmp/zh.stderr | sort -u
grep '\[PhaseD13a:' /tmp/vv.stderr | sort -u
```

Pass criteria for the current state (post-D13a):

- Both targets build cleanly with `--target both`.
- ZH stderr contains the `[PhaseD11:*]` mesh inventory:
  ~89 unique `directsubmit` lines, **0** `staticsort-defer` entries,
  **0** `fallthrough` entries, plus a single `staticsort-null` line
  reporting count=0.
- ZH stderr contains 4 `[PhaseD12:slowpath]` lines for the texture-array
  / multi-pass meshes.
- ZH stderr contains exactly 5 `[PhaseD13a:*]` lines (terrain, water,
  roads, shroud, shadowstencil); the other 8 probes are silent.
- Vanilla stderr contains the same 5 `[PhaseD13a:*]` lines (no
  `directsubmit` because vanilla menu has no 3D MeshClass content
  visible).
- ZH menu screenshot shows demo-battle shellmap (terrain + rocks +
  aircraft + ZH logo at top-right). White rectangles still visible —
  D13b will localize them inside the heightmap render.
- Vanilla menu screenshot shows cobblestone/grass courtyard scene
  with vanilla Generals logo at top-right (no white rectangles).
