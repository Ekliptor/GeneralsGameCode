# BGFX Port — Open TODOs

_Date: 2026-05-06_

## Status: COMPLETE

All TODOs tracked in this document are closed or formally deferred as of 2026-05-06. No further implementation work is outstanding under this scope. See "Recently closed" below for the resolution of each item.

## Render correctness

_(none open — verified 2026-05-06)_

## Stubbed subsystems

(per `bgfx_build_subsystem_stubs.md`; see `ww3d2_bgfx_stubs.cpp` and `RTS_RENDERER_DX8` gates)

- **Fonts pipeline (3D billboards only) — DEFERRED (no shipping caller).** UI/menu fonts fully working (FNT atlas via stbtt in `render2dsentence_bgfx.cpp`). `Font3DInstanceClass` / `Font3DDataClass` (`ww3d2_bgfx_stubs.cpp:405,499-504`) have no shipping game-code caller (audited 2026-05-06: no `Get_Font3DInstance`/`Render2DTextClass`/`TextDrawClass` references in Generals/GeneralsMD `GameEngine`/`GameEngineDevice`/`Main`). Re-open only if a real consumer surfaces.

## Smaller TODOs / audit items

_(none open — audited 2026-05-06)_

## Recently closed (for reference)

- SDL3 dropped BUTTON_UP across macOS focus boundaries — fixed via per-frame `SDL_GetMouseState` reconcile in `serviceWindowsOS` (synthesizes `WM_*BUTTONUP` for stale `s_heldButtons` bits).
- BGFX identity-view routing (G2) — `Set_View_Transform` auto-routes identity 3D views to `kView3DPart`, fixing the building-selection black viewport regression for `LineGroup`/`SegLine`/`Streak`/`Snow`/`Smudge`/`Dazzle`.
- Particle queue (Phase D) — fully wired through `PointGroupClass` (`kSrcParticle`) → `BgfxBackend` → `kView3DPart`; `SortingRendererClass` re-asserts tag at flush; `W3DParticleSystemManager` instantiated with no `RTS_RENDERER_DX8` gate.
- Selection-black-viewport bug — closed by G2 identity-view auto-route; no debug instrumentation left in tree, all known identity-VIEW callers covered.
- OpenAL playback pipeline — Phase 1+2 fully implemented in `OpenALAudioManager.cpp` (1212 lines: ALC lifecycle, request queue, source pool, three-tier volume, music tracks, polyphony culling). All five previously-empty overrides closed (`audioDebugDisplay`, `notifyOfAudioCompletion`, `removeAllDisabledAudio`, `has3DSensitiveStreamsPlaying`, `closeAnySamplesUsingFile`). Remaining header-level ToDos (3D distance attenuation polish, AT_Streaming speech, EFX reverb, surround) are separate larger features.
- `Render2DClass` per-instance coord-range audit — all owners compliant (`W3DDisplay` auto-poked, `GUIEdit/EditWindow.cpp:421+1501`, `WorldBuilder/DrawObject.cpp:2069+2077`).
- ZH localized texture path probing audit — centralised through `W3DDisplay::readViaFS` (`Data\{lang}\Art\Textures\` → `Art\Textures\`, `.dds` twin before `.tga`); registered globally via `Set_File_Reader`, no bypass paths.
