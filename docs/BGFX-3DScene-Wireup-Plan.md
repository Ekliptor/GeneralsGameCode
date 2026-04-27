# Wire up the 3D scene path on the BGFX backend

## Context

In skirmish gameplay (Generals + Zero Hour, BGFX/macOS) the 3D world is
completely black. Only the 2D HUD, 2D text, the minimap (rendered via a
2D-quad path), and floating overlays like unit health bars are drawn —
the construction yard, terrain, units, decals, and particles never reach
the screen. "Unable to set rally point" stacks in the upper-left because
the player keeps clicking trying to find what's there. Screenshots:
`~/Desktop/generals-game.png` (vanilla), `~/Desktop/generals-zh-game.png`
(Zero Hour) — both show the same symptom.

The BGFX port has driven Phases 5a–5q + 6a–6g closed: window, input,
audio, textures, dynamic + DX8 buffers, multi-light, fog, alpha test,
render targets, frame capture. `DX8Wrapper::Draw_Triangles` already
routes through `IRenderBackend::Draw_Triangles_Dynamic`
(Phase 5h.17/.18). What's left is **the scene-level wireup that turns
all that plumbing on for the actual game frame**.

The scene-level entry points are still hard-stubbed in BGFX mode:

- `WW3D::Render(SceneClass*, CameraClass*, …)` returns immediately
  (`Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp:2208`).
- `WW3D::Flush` and `Render_And_Clear_Static_Sort_Lists` are empty
  (`ww3d.cpp:2207`, `:2235`).
- `DX8MeshRendererClass::Flush` is empty
  (`Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d2_bgfx_stubs.cpp:470`).
- `RenderObjClass::Add` / `Remove` / `Get_Scene` / `Prepare_LOD` /
  `Get_Screen_Size` are no-op stubs (`ww3d2_bgfx_stubs.cpp:95–142`) —
  meaning every drawable that gameplay code adds to a scene at runtime
  is silently dropped from the scene graph.
- `dx8renderer.cpp` (mesh batcher) is fully `#ifdef RTS_RENDERER_DX8`
  gated, so `MeshClass::Render` queues batches into a no-op
  `DX8TextureCategoryClass::Add_Render_Task`.

`scene.cpp`, `mesh.cpp`, `hlod.cpp` are **already un-gated** — they
compile in BGFX mode today, they just never fire because the entry
point is stubbed and the terminal mesh-renderer sink is empty.

Goal: get terrain, buildings, units, particles, decals, water,
shadows visible. Phased delivery, with a screenshot check after
each phase. Both build targets (Generals + Zero Hour) share most of
the affected code.

## Phase 0 — Un-stub the scene-graph plumbing (must precede A's payoff)

`RenderObjClass::Add` is the elephant. Its DX8 body
(`Core/Libraries/Source/WWVegas/WW3D2/rendobj.cpp:792-798`) does
`Scene = scene; scene->Add_Render_Object(this);`. The BGFX stub is `{}`,
so every call like `obj->Add(scene)` from gameplay code silently drops
the object from the render list. Until this is fixed, every later
phase fails silently because the scene's `RenderList` stays empty.

Tighten the outer `#ifdef RTS_RENDERER_DX8` in `rendobj.cpp` so the
device-independent methods (`Add`, `Remove`, `Get_Scene`,
`Prepare_LOD`, `Get_Screen_Size`, `Update_Sub_Object_Transforms`,
`Update_Cached_Bounding_Volumes`, etc.) compile in both modes; only
truly DX8-coupled bodies (`Cast_Ray`, paths that touch `D3DRS_*`) stay
gated. Then delete the corresponding `{}` stubs from
`ww3d2_bgfx_stubs.cpp`. Cleaner than copying bodies — kills duplication.

**Expected visible result after Phase 0 alone:** nothing yet (entry
point still stubbed). The scene graph is now actually populated, which
the next phase needs.

## Phase A — Un-stub the WW3D scene entry points

Replace stubs at `ww3d.cpp:2207–2235` with the real DX8 bodies copied
from `:948–1078`:

- `WW3D::Render(SceneClass*, CameraClass*, …)` —
  `cam->On_Frame_Update()`, build `RenderInfoClass`, `cam->Apply()`,
  `DX8Wrapper::Clear`, `Set_DX8_Render_State(D3DRS_FILLMODE, …)`,
  `Set_Ambient`, `TheDX8MeshRenderer.Set_Camera`, `scene->Render(rinfo)`,
  `Flush(rinfo)`.
- `WW3D::Flush(RenderInfoClass&)` — call `TheDX8MeshRenderer.Flush()`,
  `Render_And_Clear_Static_Sort_Lists(rinfo)`,
  `SortingRendererClass::Flush()`, `Clear_Pending_Delete_Lists()`.
- `WW3D::Render(RenderObjClass&, RenderInfoClass&)` — body from `:1013–1050`.
- `Add_To_Static_Sort_List` — un-stub to call
  `CurrentStaticSortLists->Add_To_List(robj, level)`.
- `Render_And_Clear_Static_Sort_Lists` — body comes in Phase D
  (water/foliage); leave a single `{}` for now.

`DX8Wrapper::Clear`, `Set_Ambient`, `Set_DX8_Render_State`, and
`CameraClass::Apply` are already wired through `IRenderBackend`
(Phases 5h.5/.6/.8). Cleanest implementation: same as Phase 0 — change
the outer `#ifdef RTS_RENDERER_DX8` at `ww3d.cpp:103/2124` to compile
in both modes; only individual DX8-coupled methods (`Toggle_Movie_Capture`)
get an inner gate.

**Expected visible result:** `HeightMapRenderObjClass::Render`
(`Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp:1854`)
runs once per frame via `RTS3DScene::Customized_Render`'s explicit
terrain branch. Terrain submits geometry through the
`Set_*_Buffer` + `Draw_Triangles` path that Phase 5h.17/.18 wired.
Terrain mesh likely appears with base diffuse texture, possibly
mis-textured on cloud/light-map passes. HLODs still invisible (queued
into the no-op `Add_Render_Task`).

## Phase B — Terrain refinement

`HeightMap.cpp:1854–2080` is already structurally device-independent.
Address whatever Phase A reveals as broken, in priority order:

- **Multipass shaders.** `W3DShaderManager::setShader` /
  `getShaderPasses` (`Core/GameEngineDevice/Source/W3DDevice/Common/W3DShaderManager.cpp`)
  has DX8 pixel-shader paths. Stub `getShaderPasses → 1` and
  `setShader → no-op` for BGFX mode initially so the single-pass
  fallback at `HeightMap.cpp:2005` runs. Cloud and light-map passes
  follow once the underlying multi-stage shader plumbing is added.
- **Software-T&L fallback** (`m_xformedVertexBuffer`, `:2017`) — unused
  in BGFX (HW T&L always available); leave gated.
- **Tree buffer** (`m_treeBuffer->setIsTerrain()`, `:1888`) — depends
  on `W3DTreeBuffer` which already has BGFX-side null guards added in
  ZH round 5. Should be a no-op pass.

**Expected visible result:** terrain visible with diffuse texture.
Camera/projection from Phase 5h.5 should give the correct view.
Construction yard and units still invisible.

## Phase C — Static + skinned meshes (the hardest phase)

`MeshClass::Render` (`Generals/Code/Libraries/Source/WWVegas/WW3D2/mesh.cpp:657`)
doesn't draw — at line 725 it calls
`polygon_renderer->Get_Texture_Category()->Add_Render_Task(polygon_renderer, this)`,
which is a no-op stub at `ww3d2_bgfx_stubs.cpp:491`. Drawing only
happens when `TheDX8MeshRenderer.Flush()` runs, and that's empty.

Two routes:

**Route C1 (RECOMMENDED for first cut) — bypass the batcher.**
Add a BGFX-mode branch in `MeshClass::Render` that skips the
`Add_Render_Task` queuing path and instead immediately walks
`Model->PolygonRendererList`, calling per polygon-renderer:
`DX8Wrapper::Set_Vertex_Buffer` / `Set_Index_Buffer` /
`Set_Texture` / `Set_Material` / `Set_Shader` / `Draw_Triangles`.
Then leave `TheDX8MeshRenderer.Flush()` empty (nothing was queued).
Skinned meshes: route through CPU vertex skinning via
`MeshGeometryClass::Get_Deformed_Vertices` (`meshgeometry.cpp:491`,
already device-independent) into the dynamic-VB path that already
works for sorted geometry.

Risk to address: `MeshModelClass::Register_For_Rendering` lives in
`dx8renderer.cpp` and is gated. `Model->PolygonRendererList` won't
be populated without it. Either (a) extract the device-independent
record-building portion into a small always-compile helper that
walks W3D mesh subsets and emits
`(start_index, polygon_count, texture, material, shader)` tuples,
or (b) build the list lazily on first `Render` from the W3D model
data. Option (a) is cleaner.

Pros: avoids un-gating ~2200 lines of DX8 batching code. Loses
sort-by-state efficiency, but BGFX has its own state-sort.

**Route C2 — un-gate `dx8renderer.cpp` wholesale.** Lift the
`#ifdef RTS_RENDERER_DX8` at `dx8renderer.cpp:42`. Pulls in
`dx8polygonrenderer.cpp`, `dx8fvf.cpp`, `dx8caps.cpp`. Most uses of
`D3DRS_*` flow through `DX8Wrapper::Set_DX8_Render_State` which is
already a tolerable no-op-ish on BGFX, so this *might* compile with
surgical fixes (~50–100 sites). Higher cost; preserves batching.

**Recommendation: C1.** Faster path to "units visible." Iterate to
C2 if profiling shows the unbatched path is too slow.

**Expected visible result:** buildings (`MeshClass`) and units
(`HLodClass` → `MeshClass` per LOD) draw. Animations work because
`Animatable3DObjClass` lives in `animobj.cpp` and is device-independent.
LOD selection works because `Prepare_LOD` was un-stubbed in Phase 0.

## Phase D — Particles, water, shadows, decals

Ranked by visual importance × ROI:

1. **Particles** (`part_buf.cpp`, `part_emt.cpp`) — emit into the
   sorting renderer. `SortingRendererClass::Flush`
   (`sortingrenderer.cpp`) is **not** DX8-gated. Sorting VB/IB
   already route through `Draw_Triangles_Dynamic` (Phase 5h.17).
   Should "just work" once `WW3D::Flush` calls
   `SortingRendererClass::Flush()` (Phase A). Verify
   `ParticleEmitterClass::Render` is being called.
2. **Water** — drawn via `Render_And_Clear_Static_Sort_Lists`. Walk
   `CurrentStaticSortLists`, sort by camera distance, call each
   `obj->Render(rinfo)`. Mirror the DX8 body.
3. **Shadows** — `TheW3DShadowManager->queueShadows(TRUE)`
   (`W3DScene.cpp:1153`). Stencil shadows need stencil RT (Phase 5p
   provides it). Project-shadow path needs render-to-texture. Defer
   to a sub-phase after units render so we can A/B compare.
4. **Decals** — `DecalMeshClass::Render` invoked by the mesh-renderer
   decal pass. Trivially follows once Phase C polygon-renderer
   hookup lands.

## Critical files

| Phase | File | Lines | Action |
|---|---|---|---|
| 0 | `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d2_bgfx_stubs.cpp` | 95–142 | Delete `Add` / `Remove` / `Get_Scene` / `Prepare_LOD` / `Get_Screen_Size` no-op stubs |
| 0 | `Core/Libraries/Source/WWVegas/WW3D2/rendobj.cpp` | 77, end-of-file | Tighten `#ifdef RTS_RENDERER_DX8` so device-independent methods always compile |
| A | `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d.cpp` | 103, 2207–2235 | Either replace stubs with bodies from `:948–1078`, or convert outer gate to compile both modes with inner per-method guards |
| A | `Generals/Code/Libraries/Source/WWVegas/WW3D2/ww3d2_bgfx_stubs.cpp` | 470 | Keep `DX8MeshRendererClass::Flush()` empty for Route C1; delete for Route C2 |
| B | `Core/GameEngineDevice/Source/W3DDevice/GameClient/HeightMap.cpp` | 1854–2080 | Verify multipass fallback, default to single-texture path |
| B | `Core/GameEngineDevice/Source/W3DDevice/Common/W3DShaderManager.cpp` | (whole) | Stub `getShaderPasses → 1`, `setShader → no-op` for BGFX |
| C | `Generals/Code/Libraries/Source/WWVegas/WW3D2/mesh.cpp` | 657–769, 725 | Add BGFX-mode branch that walks `PolygonRendererList` and submits via `DX8Wrapper::Draw_Triangles` |
| C | `Generals/Code/Libraries/Source/WWVegas/WW3D2/meshmdl.cpp` | search `Register_For_Rendering` | Extract device-independent polygon-renderer record builder |
| C | `Generals/Code/Libraries/Source/WWVegas/WW3D2/dx8renderer.cpp` | 42 | Route C2 only: lift outer gate, fix `D3DRS_*` direct uses |
| D | `Generals/Code/Libraries/Source/WWVegas/WW3D2/sortingrenderer.cpp` | (whole) | Verify device-independent; fix any direct `D3DRS_*` uses |
| D | `Generals/Code/Libraries/Source/WWVegas/WW3D2/part_buf.cpp`, `part_emt.cpp` | (whole) | Verify particle render enters sorting renderer cleanly |
| D | `Core/GameEngineDevice/Source/W3DDevice/GameClient/W3DShadowManager.cpp` | TBD | Phase 5p RT integration |

GeneralsMD (Zero Hour) sibling files mirror the same paths — apply the
identical changes there. The target-duplicated WW3D2 trees were aligned
in Generals/ZH rounds 7–11 per `docs/Vanilla-Generals-Status.md`.

## Risks

1. **`MeshModelClass::Register_For_Rendering` is in gated code.** Without
   it, `PolygonRendererList` is always empty and Route C1 has no state
   to walk. Must extract or reimplement first.
2. **Z-sorted alpha is broken** (Phase 5h.17 note). Translucent
   geometry will draw out of order until per-triangle CPU sort
   actually fires inside the dynamic-VB path. Visible as halos around
   smoke, dust, wreck plumes.
3. **Light environment** — Phase 5h.8 only routed ambient + diffuse
   subset. Skinned meshes that read the 4-light environment may render
   flat/dark. Acceptable for Phase C v1.
4. **Shroud / fog-of-war material pass** (`m_shroudMaterialPass`,
   `W3DScene.cpp:1101`) uses `MaterialPassClass`. Likely renders
   nothing in BGFX until material-pass plumbing lands. Terrain may
   appear unshrouded in unexplored areas.
5. **Camera/viewport state drift.** Render2D path leaves the BGFX view
   transform set to identity (Phase 5h.2D). Phase A's `cam->Apply()`
   must repopulate it before any 3D submit, or the 3D scene draws
   into NDC-0,0,0,0 and disappears. Sanity check: log
   `BgfxBackend::m_viewProjDirty` flow per frame.
6. **`RenderObjClass::Get_Container_Inline` chain.** Sub-objects on
   bones need `Update_Sub_Object_Transforms` (stub at
   `ww3d2_bgfx_stubs.cpp:111`). HLOD sub-meshes won't track bones
   until this is real. Include in Phase 0's un-gating sweep.

## Verification

For each phase, capture a screenshot from the same skirmish/map (use
the `-screenshot /tmp/<phase>.tga` arg per `CLAUDE.md`) and compare
against the DX8 reference. Skirmish reproducer:

```bash
scripts/run-game.sh --target generalszh -- -win
# in-game: start skirmish vs Easy on a small map
# OR for the menu shell-map test (faster iteration):
scripts/run-game.sh --target generalszh -- -win -screenshot /tmp/zh-shell.tga
sips -s format png /tmp/zh-shell.tga --out /tmp/zh-shell.png && open /tmp/zh-shell.png
```

Per-phase checks:

- **Phase 0:** add a one-shot counter in `RenderObjClass::Add` —
  expect hundreds of adds during match start.
- **Phase A:** terrain hull appears (possibly mis-textured). Frame no
  longer pure-black outside the HUD. The `RTS3DScene::Customized_Render`
  terrain branch should fire — log a single frame's `Render` count.
- **Phase B:** terrain texture matches DX8 reference within
  color/lighting tolerance. Ground type transitions visible.
- **Phase C:** capture frame at known camera pose, compare unique-pixel
  count and SSIM against DX8 reference. Construction yard, refinery,
  units all draw. Run vs-Easy match for ~30s and verify no null-deref
  via `lldb`.
- **Phase D:** unit muzzle flashes / explosions appear; alpha-stacked
  translucent props render in correct order. Water surface visible.

If a frame hangs, `sample <pid> 5` to grab thread stacks.

## Out of scope

- DX8 movie capture (`Toggle_Movie_Capture`)
- `CubeTextureClass` / `VolumeTextureClass` (cubemap reflections)
- N-patches gap filling
- Software-T&L pre-transform path
- `AggregateLoaderClass::Load_W3D` (multi-model containers; not used by
  core gameplay)
- Save-game persist factories (`PersistFactory` stubs return null —
  fine for skirmish, breaks campaign saves)
- Audio (already addressed elsewhere)
- Per-phase doc files (`docs/Phase5r-*.md` etc.) — write once each
  phase actually lands