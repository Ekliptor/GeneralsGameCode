# BGFX Port — Open TODOs

_Date: 2026-05-06_

## Render correctness

- **Particle queue (Phase D)** — Shell map loads (per `GameLOD.cpp` gate removal on 2026-04-26), but the particle pipeline is still gated as Phase D. Full enable pending.
- **Selection-black-viewport bug** — Phase A capture in progress: refined logs for view3D-slot `Set_View_Transform` writes + `ApplyDrawState` PUSH log captured into `/tmp/zh-selectblack.stderr`. The G2 identity-view auto-route fix has landed (`Set_View_Transform` detects identity → `m_view3DPartMtx`, `ApplyDrawState` honors `m_lastNonUIViewIsIdentity`). Need to confirm whether any residual cases remain or close it out.

## Stubbed subsystems

(per `bgfx_build_subsystem_stubs.md`; see `ww3d2_bgfx_stubs.cpp` and `RTS_RENDERER_DX8` gates)

- **Fonts pipeline** — empty stubs.
- **OpenAL playback pipeline** — empty stubs. Main-menu hover SFX worked around with a direct `addAudioEvent("GUILogoMouseOver")` call in `MainMenuSystem`; broader script-hook audio still silent.

## Smaller TODOs / audit items

- **`Render2DClass` per-instance coord range** — Owners must call `Set_Coordinate_Range(Get_Screen_Resolution())` themselves before `Add_Quad`. Only `W3DDisplay`'s singleton `m_2DRender` is poked automatically. Audit any new 2D consumers.
- **ZH localized texture path probing** — Already in place (`data\english\art\textures\` probed before `Art\Textures\` to beat vanilla in `EnglishZH.big`). Any new texture loaders need the same prefix order.

## Recently closed (for reference)

- SDL3 dropped BUTTON_UP across macOS focus boundaries — fixed via per-frame `SDL_GetMouseState` reconcile in `serviceWindowsOS` (synthesizes `WM_*BUTTONUP` for stale `s_heldButtons` bits).
- BGFX identity-view routing (G2) — `Set_View_Transform` auto-routes identity 3D views to `kView3DPart`, fixing the building-selection black viewport regression for `LineGroup`/`SegLine`/`Streak`/`Snow`/`Smudge`/`Dazzle`.
