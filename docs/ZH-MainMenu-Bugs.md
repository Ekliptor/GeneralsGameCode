# Zero Hour Main Menu — Persistent Bugs (macOS BGFX build)

**Status:** Open, unresolved. All symptoms persist after multiple fix attempts.
**Build:** `build_bgfx/GeneralsMD/generalszh.app` (Metal / BGFX / SDL3 / OpenAL / FFmpeg).
**Date recorded:** 2026-04-23.
**Evidence:** `~/Desktop/generals8.png`, `~/Desktop/generals10.png` (identical md5, same state).

## Observed symptoms

On the Zero Hour main menu:

1. **Wrong background / logo.** The visible backdrop is the vanilla Generals "tank battle at dusk" scene with the `COMMAND & CONQUER / GENERALS` logo top-right. Zero Hour's main menu should show the ZH-branded backdrop and logo.
2. **Empty buttons.** Six outlined rectangles on the right side. No text inside any of them.
3. **Buttons are inert.** No hover highlight, no click response, no tooltip.
4. **No audio.** No shell music, no UI hover/click sounds.

All four symptoms appear together. Game process runs stably (~30 s observed) and does not crash in the current code state.

## Files and paths relevant to diagnosis

- BIG loader used on macOS: `Core/GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp`
  - `Win32BIGFileSystem::init()` (lines 54–73) — selects which archives are loaded and in what order.
  - `Win32BIGFileSystem::loadBigFilesFromDirectory()` (lines ~211–242) — enumerates BIG files in a directory and calls `loadIntoDirectoryTree`.
- Archive insertion / lookup: `Core/GameEngine/Source/Common/System/ArchiveFileSystem.cpp::loadIntoDirectoryTree` (lines 118–211) and `ArchiveFileSystem::getArchiveFile` (line 320).
- Local file enumeration on macOS: `Core/GameEngineDevice/Source/StdDevice/Common/StdLocalFileSystem.cpp::getFileListInDirectory` (lines 211–282) — **contains a recursion bug: the recursive call passes only the subdir leaf name, not the full path from `cwd`, so recursion effectively stops after one level**. This is unrelated but relevant context (prevents the `Data\INI\INIZH.big` @bugfix skip from ever triggering on macOS).
- Factory: `SDLGameEngine` extends `Win32GameEngine` and inherits `createArchiveFileSystem() → NEW Win32BIGFileSystem`, and `createLocalFileSystem()` — `TheLocalFileSystem` is actually `StdLocalFileSystem` on macOS (the Win32 one is `#ifdef _WIN32` out).
- Audio backend: `Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALAudioManager.cpp` — `openDevice()` is called from `init()`. This is expected to be correct.
- Startup + staging: `GeneralsMD/Code/Main/AppMain.cpp:113–119` (`SDL_GetBasePath()` + `chdir`), `scripts/run-game.sh` (stages assets into `<app>/Contents/Resources/` via symlinks).

## Environment at runtime

- `cwd` (after `chdir(SDL_GetBasePath())`) = `build_bgfx/GeneralsMD/generalszh.app/Contents/Resources/`.
- `Resources/` contains 16 ZH BIG symlinks at top level (`AudioEnglishZH.big`, `AudioZH.big`, `EnglishZH.big`, `gensecZH.big`, `INIZH.big`, `MapsZH.big`, `Music.big` (ZH's copy of vanilla Music.big), `MusicZH.big`, `ShadersZH.big`, `SpeechEnglishZH.big`, `SpeechZH.big`, `TerrainZH.big`, `TexturesZH.big`, `W3DEnglishZH.big`, `W3DZH.big`, `WindowZH.big`).
- `Resources/` also contains stray symlinked subdirectories from an earlier `--assets` invocation: `Install/` (→ parent `Install/` containing both `Generals/` and `ZeroHour/` subtrees), `Zero Hour/`, `Command_and_Conquer_Generals_CD1`/`CD2` (+`.cue`/`.bin`), `Data/`, `support/`, `MSS/`. These are **not** picked up by the engine due to the `StdLocalFileSystem` recursion bug (see above), so in practice only the 16 top-level BIGs are loaded via the cwd pass. Even so, staging should be narrowed.
- Registry: `~/Library/Application Support/EA/Generals/RegistrySettings.ini` has `InstallPath=/Users/daniel/Library/Application Support/Mountain Duck/.../Game/Install/Generals` — this resolves and contains 15 vanilla BIGs.

## Archive multimap semantics (verified)

The directory tree stores files in `std::multimap<AsciiString, ArchiveFile*>`. `loadIntoDirectoryTree(archive, overwrite)`:

- `overwrite=TRUE`: insert hint = `find(key)` → new entry lands at the FRONT of equal-key range.
- `overwrite=FALSE`: insert hint = `end()` → new entry lands at the BACK of equal-key range.

`ArchiveFileSystem::getArchiveFile(name, instance=0)` returns `equal_range(name).first->second`. So:

- `overwrite=TRUE` → **LAST-loaded archive wins** single-file lookup.
- `overwrite=FALSE` → **FIRST-loaded archive wins** single-file lookup.

Validated with a minimal `<map>` reproducer under Apple libc++.

## Attempts and outcomes

### Attempt 1 — swap load order only

Change: in `Win32BIGFileSystem::init()`, move the `InstallPath` (vanilla) pass before the cwd (ZH) pass. Both still default to `overwrite=FALSE`.

Result: the game **crashed** during INI load with:

```
Reason Error parsing INI file 'Data\INI\Weapon.ini' (Line: 'Weapon CINE_USAPathfinderSniperRifle ')
```

That error is thrown by `WeaponStore::parseWeaponTemplateDefinition` (`GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Weapon.cpp:1773–1791`) when a weapon name is redefined without `OVERRIDE`. With `overwrite=FALSE` in both passes and vanilla loaded first, `openFile("data\\ini\\weapon.ini")` returned vanilla's copy; why the ZH-only weapon `CINE_USAPathfinderSniperRifle` then appeared as a duplicate is not explained by single-file lookup alone. Possibly the parser is re-reading a second `Weapon.ini` through some other path (includes, or a scan of all archives containing the file), but the mechanism was **not identified**.

Reverted.

### Attempt 2 — swap load order + `overwrite=TRUE` on the ZH pass

Change (current code state):

```cpp
#if RTS_ZEROHOUR
    AsciiString installPath;
    GetStringFromGeneralsRegistry("", "InstallPath", installPath);
    if (!installPath.isEmpty())
        loadBigFilesFromDirectory(installPath, "*.big");   // vanilla, overwrite=FALSE

    loadBigFilesFromDirectory("", "*.big", TRUE);          // ZH, overwrite=TRUE
#else
    loadBigFilesFromDirectory("", "*.big");
#endif
```

Verified at runtime via stderr-instrumentation that the load order is exactly:

1. 14 vanilla BIGs from `Install/Generals/*.big` with `overwrite=0`.
2. 16 ZH BIGs from `./*.big` (Resources/) with `overwrite=1`.

The game runs past all INI loading and reaches the main menu without crashing. **But the user reports the four symptoms above still present** — no visual/audible change from the vanilla-menu state. Instrumentation has been removed; fix is still in place.

## Hypothesis space (open questions)

With the current fix in place, `getArchiveFile("data\\window\\menus\\mainmenu.wnd")` **must** return `WindowZH.big`'s entry (overwrite=TRUE puts it at front of the equal-key range). If the menu still renders vanilla, one of the following is wrong:

1. **Lookup is not instance=0.** Some caller passes `instance=1` to get the "other" archive. Nothing obvious in grep, but worth re-examining `W3DGameWindowManager::parseMenu` / `GameWindowManager::winCreateFromScript`-style code paths that load `.wnd` files.
2. **The .wnd file is read from a non-archive path first.** `FileSystem::openFile` tries local FS, then archive FS (need to verify order in `GeneralsMD/Code/GameEngine/Source/Common/System/FileSystem.cpp`). If there's a local `data/window/menus/mainmenu.wnd` that shadows the archive, vanilla-ish content could come from there. There is a symlinked `Data/` in `Resources/` — worth checking whether any loose `.wnd`/`.csf`/`.ini` files are reachable.
3. **MainMenu.wnd is identical in vanilla and ZH for the reported keys.** Unlikely — both BIGs contain a `mainmenu.wnd` of different sizes (208 336 vs 208 561 bytes). But the ZH one may still reference vanilla asset names for the backdrop image (e.g., `MainMenuBG_01.tga`). The image loader would then pull the texture from whichever `Textures*.big` wins, and with the fix that's `TexturesZH.big`.
4. **CSF / labels.** `TheGameText` loads `data\<lang>\generals.csf`. With the fix, `EnglishZH.big` wins. If buttons are empty it means button label lookup fails — could be a stale `NameKeyGenerator` key mismatch between the ZH .wnd and ZH CSF, or a font-rendering bug unrelated to archive load order.
5. **Audio.** `OpenALAudioManager` opens the ALC device in `init()`. Silent main menu could be a separate issue (music cue lookup failure, AudioEnglishZH.big mismatch, or audio-callback path not wired). The shell map `Maps\ShellMap1\ShellMap1.map` drives main-menu music through `Audio.ini` — if the script engine is failing for any reason, no music even though the device is open.
6. **Something runs before `TheArchiveFileSystem::init()`** and caches a decision (unlikely but not ruled out).

## What was not tried

- Reading back from the multimap after `init()` and asserting that `getArchiveFile("data\\window\\menus\\mainmenu.wnd")` returns `WindowZH.big`. This is the single most valuable next diagnostic — if it returns `Window.big`, the insertion semantics model is wrong in this codebase. If it returns `WindowZH.big`, the bug is downstream of archive lookup.
- Instrumenting `FileSystem::openFile` to print which source (local vs archive, which archive) served `mainmenu.wnd`, `generals.csf`, `audio.ini`.
- Cleaning up the `Resources/` staging to contain ONLY the 16 ZH BIGs + the `Data/` subdir actually needed, removing the stray `Install/`, `Zero Hour/`, `Command_*/`, `MSS/`, `support/` symlinks.
- Fixing the recursion bug in `StdLocalFileSystem::getFileListInDirectory` (independent issue; worth fixing but would not, on its own, change the menu symptoms because the macOS recursion currently yields *fewer* archives than the Windows version, not more).
- Verifying that the `ShadersZH.big` at 996 bytes (suspiciously tiny) is the real ZH archive and not a truncated symlink target.

## Current file state

- `Core/GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp` — modified (Attempt 2 fix in place). Ordering: vanilla first, ZH second with `overwrite=TRUE`. Attempt 2 did not visibly fix the menu but also did not regress (no crash).
- `Core/Libraries/Source/WWVegas/WWMath/vector3.h` — pre-existing `an .env` corruption at line 907 was removed during this session (blocking build).

## Recommended next diagnostic step

Add targeted logging to `ArchiveFileSystem::getArchiveFile` for the three filenames that decide the main menu: `data\window\menus\mainmenu.wnd`, `data\english\generals.csf`, `data\ini\audio.ini`. Print the winning archive's `getName()`. Run once, capture output. That answers hypothesis 1/2 unambiguously and narrows the search to either "archive lookup is wrong" or "archive lookup is right but downstream is broken".

## Resolution — 2026-04-23

**Root cause:** `CPUDetectClass::Init_Memory()` was a stub on non-Win32 platforms (`Core/Libraries/Source/WWVegas/WWLib/cpudetect.cpp:894`), leaving `TotalPhysicalMemory` at zero. That failed `GameLODManager::init`'s 256 MB memory check (`GameLOD.cpp:325`) and, via `m_memPassed=FALSE`, caused `GameLOD.cpp:621` to force `m_shellMapOn=FALSE`. With the shell map disabled, `TheShell->showShellMap(TRUE)` took the else branch and layered `BlankWindow.wnd` behind `MainMenu.wnd` instead of running the Zero Hour shell map 3D scene. ZH's `MainMenu.wnd` then rendered against its static fallback backdrop image `MainMenuBackdropUserInterface.tga`, which only exists in vanilla `Textures.big` (not `TexturesZH.big`) — producing the "vanilla tank battle + Generals logo, no ZERO HOUR branding" visual.

**Hypothesis audit:**
- Archive multimap semantics (hypothesis 1) — correct. `overwrite=TRUE` on the ZH pass with `std::multimap::insert(find(key), …)` reliably puts ZH entries at the front of each equal-key range under Apple libc++, and `getArchiveFile` returns ZH for every collision (confirmed by a one-shot probe in `Win32BIGFileSystem::init()`).
- Local FS shadowing (hypothesis 2) — false. No loose `.wnd`/`.csf`/`.ini` files in the staged `Resources/Data/` tree.
- Downstream CSF / font / audio issues (4 / 5) — symptoms, not causes. The empty buttons were empty because the shell-map scene wasn't running behind them, and the no-audio symptom is a separate OpenAL issue still open.
- `ShadersZH.big` at 996 bytes (noted in the doc) — the real ZH shader config file ships at that size; not a truncation.

**Fixes applied:**
- `Core/Libraries/Source/WWVegas/WWLib/cpudetect.cpp` — implemented `Init_Memory()` for macOS/FreeBSD (`sysctlbyname("hw.memsize", …)`) and Linux (`sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGE_SIZE)`).
- `Core/GameEngineDevice/Source/StdDevice/Common/StdLocalFileSystem.cpp:275` — recursion bug: the recursive call now builds the full relative path (`currentDirectory + '/' + leaf`) instead of just the leaf name. Independent of the menu fix but needed for correct subdirectory scans (and for `MapCache` to find nested maps).
- `Core/GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp` — Attempt 2 ordering kept as-is (vanilla first with `overwrite=FALSE`, ZH second with `overwrite=TRUE`). Still correct; still required so ZH archives win for name-collision files.
- `GeneralsMD/Code/GameEngine/Source/GameClient/GameClient.cpp` — added a dev-only `ZH_SKIP_INTRO=1` env-var branch that skips EA logo + Sizzle so the shell reaches the main menu immediately during testing.

**Unresolved:** the "no audio" symptom. `audio.ini` is not present at `data\ini\audio.ini`, `data\ini\default\audio.ini`, `ini\audio.ini`, `ini\default\audio.ini`, or `audio.ini` in any loaded archive. `INIZH.big` only contains `data\ini\miscaudio.ini`. This is likely a separate investigation — possibly OpenAL device never finishes opening on macOS, or the audio config lives under a different path this tree doesn't grep for.

## Resolution — round 2 (2026-04-23)

Round 1's cpudetect memory fix unblocked `m_memPassed` but the visible symptoms persisted. A consolidated diagnostic probe at the end of `GameEngine::init()` on `__APPLE__` revealed the actual state:

```
m_shellMapOn=0, m_shellMapName='Maps\ShellMapMD\ShellMapMD.map'
LOD.didMemPass()=1, LOD.isReallyLowMHz()=1, LOD.currentStaticLOD=0 (STATIC_GAME_LOD_LOW)
CPU ProcessorSpeed=0 MHz, TotalPhysicalMemory=128 GiB
MapCache.find('maps\shellmapmd\shellmapmd.map')=HIT
```

**Root cause (Outcome C from the plan):** On Apple Silicon, `CPUDetectClass::Init_Processor_Speed()` predicates on `Has_RDTSC_Instruction()`, which is FALSE on ARM → `ProcessorSpeed=0` → `m_cpuFreq=0` in `GameLODManager` → `isReallyLowMHz() = 0 < 400 = TRUE`. `applyStaticLODLevel(STATIC_GAME_LOD_LOW)` ran (the memory gate did not block it), hit the `!m_memPassed || isReallyLowMHz()` check at `GameLOD.cpp:621`, and force-disabled `m_shellMapOn`. The MapCache gate was NOT firing — the map was found correctly (confirming the StdLocalFileSystem recursion fix + archive enumeration both work).

**Fix applied:** One-line guard at `GameLOD.cpp:621` (and the vanilla sibling at `Generals/Code/…/GameLOD.cpp:615`) — treat `m_cpuFreq==0` as "unknown → do not gate":

```cpp
if (!m_memPassed || (m_cpuFreq > 0 && isReallyLowMHz())) {
    TheWritableGlobalData->m_shellMapOn = false;
}
```

**Result (partial):** The vanilla "tank battle" backdrop is gone; the menu now renders ZH's `MainMenu.wnd` frame with a **black** backdrop area (screenshot `~/Desktop/generals12.png`). The 3D shell map scene is being queued (`GAME_SHELL` path in `Shell::showShellMap`), the map is in MapCache, but the W3D/BGFX backend isn't producing visible output for it.

**Still unresolved (downstream of the shell-map fix):**
- 3D shell-map scene renders black. Probably BGFX-backend-specific — the scene never reaches `W3DScene::customRender` or similar. Needs a separate investigation focused on the W3D→BGFX render path for the shell-map `GameClient` update loop.
- Six button outlines are still empty. `MainMenu.wnd` layout loads; the text/label fetch is failing. Could be a `MappedImage` or CSF lookup miss against `EnglishZH.big`/`WindowZH.big`, or a font rasterizer issue unrelated to the shell map.
- Audio — still open from round 1. Likely a third, independent issue.

**Follow-ups (not this PR):**
- Proper CPU frequency detection on ARM macOS (sysctl `hw.perflevel0.freq_hz` on Sonoma+) so `m_cpuFreq` carries real data — lets the LOD preset matcher choose a more appropriate level instead of defaulting to LOW.
- Investigate W3D→BGFX render path for the shell-map 3D scene.
- Investigate MainMenu button label lookup path (CSF vs font atlas vs `MappedImages`).

## Resolution — round 3 (2026-04-23)

Three further root causes landed in this round. None were in the shell-map gate; all were orthogonal issues in the BGFX port's content pipeline.

### 3.1 Vanilla logo / vanilla UI chrome instead of ZH assets — FIXED

**Root cause.** `readViaFS()` in `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp` is the file reader registered with `BgfxTextureCache`. For bare texture filenames it only prepended `Art\Textures\`. Zero Hour's localized UI textures (`SCSmShellUserInterface512_001.tga`, etc., which hold the ZH branded logo) live under `data\english\art\textures\` in `EnglishZH.big`; the vanilla sibling lives under `art\textures\` in `Textures.big`. Requesting `SCSmShellUserInterface512_001.tga` via the existing prefix resolved to vanilla's archive and produced the vanilla logo. `GameFileClass::Set_Name` in `W3DFileSystem.cpp` already does the `Data/<Language>/Art/Textures/` fallback for the DX8 render path, but the BGFX path did not.

**Fix.** `readViaFS()` now probes `Data\<RegistryLanguage>\Art\Textures\<filename>` first, then `Art\Textures\<filename>`, then the bare path. Applied to both `GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3DDisplay.cpp` and the Generals sibling.

**Verified.** Screenshot `/tmp/zh-menu.tga` after the fix shows the ZH "GENERALS ZERO:HOUR" gold-tinted logo (from the localized `data\english\art\textures\scsmshelluserinterface512_001.tga`) instead of vanilla's "GENERALS" logo.

### 3.2 Button labels still empty / CSF parse desync on macOS — FIXED (CSF side)

**Root cause.** `GameTextManager::parseCSF` in `Core/GameEngine/Source/GameClient/GameText.cpp` read each CSF string with `file->read(m_tbuffer, len*sizeof(WideChar))`. `WideChar` is a typedef for `wchar_t`, which is **2 bytes on Windows** but **4 bytes on macOS/Linux**. The CSF on-disk format stores UCS-2 (2 bytes/char), so the read consumed 2× too many bytes. The first LABEL parsed correctly; by the second LABEL the file position was offset into string data, the next 4-byte id read as `0x72657961` (`"ayer"` from "SinglePlayer"'s tail) instead of `CSF_LABEL` (`0x4c424c20`), `parseCSF` bailed via the `goto quit`, `init()` deinit'd, `TheGameText` ended up unusable, every `fetch("GUI:...")` fell back to the `MISSING:` path, and buttons drew with no text.

**Fix.** `parseCSF` now reads into a fixed `uint16_t` staging buffer sized to the disk format, then widens each 16-bit value into `m_tbuffer` (and applies the XOR inversion on the 16-bit raw value). This is platform-agnostic and does not change behaviour on Windows where `sizeof(WideChar)==2`.

**Verified.** Instrumented dump after the fix:

```
[GT] CSF loaded: format=1 textCount=6364 csfFile='data\english\Generals.csf' lang='english'
[GT] [0] label='GUI:GameOptions' text='GAME OPTIONS'
[GT] [1] label='GUI:SinglePlayer' text='SOLO PLAY'
[GT] [2] label='GUI:Network' text='NETWORK'
```

All 6364 labels now load with the correct widened Unicode text.

### 3.3 Buttons STILL render blank after the CSF fix — NOT FIXED, root-caused

With the CSF fix in place, `TheGameText->fetch("GUI:Skirmish")` returns the correct `UnicodeString`, and `GadgetButtonSetText` propagates it to the window. But the rendered screen still shows empty button outlines.

**Root cause (downstream of the CSF fix).** The font pipeline is not wired for the BGFX build:

- `Core/Libraries/Source/WWVegas/WW3D2/render2dsentence.cpp` — the whole file is gated on `#ifdef RTS_RENDERER_DX8`. `FontCharsClass::Create_GDI_Font` (Win32 GDI `CreateFont` + `CreateDIBSection`) and `Render2DSentenceClass::Build_Sentence` only compile on the DX8 build.
- `GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/ww3d2_bgfx_stubs.cpp` — the BGFX build's stand-ins. `WW3DAssetManager::Get_FontChars` returns `nullptr`. `Render2DSentenceClass::Build_Sentence` is a no-op. `FontCharsClass::Get_Char_Spacing` returns 0.

Net effect: `W3DFontLibrary::loadFontData` calls `Get_FontChars(name, size, bold)`, gets `nullptr`, returns `FALSE`. The gadgets hold their text but have no font data to rasterize, so nothing draws. This affects every piece of 2D text the UI tries to render — not just main-menu buttons.

**Scope note.** A proper fix is a platform-agnostic font rasterizer (likely SDL_ttf or FreeType) plus a BGFX-backed glyph atlas + vertex stream for `Render2DSentenceClass`. That is a self-contained subsystem port, not a one-line tweak. Deferred to a dedicated PR.

### 3.4 No audio — NOT FIXED, root-caused

`Core/GameEngineDevice/Source/OpenALAudioDevice/OpenALAudioManager.cpp` currently only opens the ALC device + context, enumerates providers, and exposes a video-audio stream for the FFmpeg intro player. Almost every playback-facing override in `Core/GameEngineDevice/Include/OpenALAudioDevice/OpenALAudioManager.h` is an empty `{}` — `stopAudio`, `pauseAudio`, `pauseAmbient`, `killAudioEventImmediately`, `nextMusicTrack`, `prevMusicTrack`, `isMusicPlaying`, `hasMusicTrackCompleted`, `getMusicTrackName`, `notifyOfAudioCompletion`. The base `AudioManager::addAudioEvent` queues events, but `OpenALAudioManager` never consumes them, never builds `ALuint` sources/buffers, never kicks off streaming for music. Shell music, UI hover/click sounds, and ambient audio therefore cannot play, regardless of whether the device opened successfully.

**Scope note.** Porting the Miles-style event queue → OpenAL source/buffer pipeline (including looping streamed music, 2D/3D positioning, and Miles .bik wave integration for shell sounds) is a separate subsystem port.

### Summary of what shipped vs what remains

| Issue | Status | Fix location |
|---|---|---|
| Vanilla logo / wrong UI textures | **Fixed** | `readViaFS` in `W3DDisplay.cpp` (ZH + Generals) — localized `Data\<Lang>\Art\Textures\` probe. |
| CSF text decode corrupt on macOS | **Fixed** | `parseCSF` in `Core/GameEngine/Source/GameClient/GameText.cpp` — 16-bit staging read. |
| Buttons render no text | **Open** | Needs BGFX font rasterizer + `Render2DSentenceClass` backend. |
| Shell-map 3D scene renders black | **Open** | Needs W3D→BGFX scene render path debugging. |
| 2D menu backdrop renders black (vanilla + ZH) | **Open, diagnosed** | See round 4 below — full-screen textured quad reaches `bgfx::submit` with valid texture handle, but UV data in CPU shadow is all zero. |
| All audio silent | **Open** | Needs OpenAL event/source/buffer pipeline port. |

## Resolution — round 4 (2026-04-24): 2D backdrop still black, diagnosed

Instrumented `Render2DClass::Render`, `BgfxBackend::Draw_Triangles_Dynamic`, and `BgfxBackend::Clear` on the vanilla build (`g_generals` target, `build_bgfx/Generals/generalsv.app`) to trace the 2D pipeline end-to-end. Diagnostic removed after capture; raw output summary:

- `Render2DClass::Render` fires ~30+ times per frame (letterbox fills, menu gadgets, logo, backdrop) — pipeline is reachable from the game loop.
- `BgfxBackend::Draw_Triangles_Dynamic` receives matching draws (V=4 I=6 for quads, V=16 I=24 for multi-quad fills) with valid vertex layout (`attrs=5 stride=48` for `dynamic_fvf_type = D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_DIFFUSE|D3DFVF_TEX2`). Position offsets 0/12/24 and UV0 at 28 all match `FillLayoutFromFVF`.
- Texture handles stored in `m_stageTexture[stage]` are non-null and `bgfx::isValid(*owned)` returns true for real textures — placeholder fallback only kicks in for untextured fills.
- Vertex positions are already in NDC space (Render2D's `Update_Bias_And_Projection` pre-transforms to clip space; `m_projMtx/m_viewMtx/m_worldMtx` are all identity). Example for full-screen backdrop: `v0=(-1,1,0) v1=(-1,-1,0)`.
- Shader state: `cw=1 dw=0 dc=7 (DEPTH_ALWAYS) sb=4 (SRC_ALPHA) db=5 (INV_SRC_ALPHA)`. State mask `0x100000006565_08f` — write RGBA, depth always, alpha blend. No culling (phase-debug disabled in `BuildStateMask`).
- Clear color stays `(0,0,0,1)` across frames — expected for a menu with a full-screen backdrop quad that should overwrite it.

**Actual defect (smoking gun):** The raw bytes of the submitted vertex for the full-screen backdrop quad (draw #2 in the trace) are:

```
raw0[0..48]: 000080bf 0000803f 00000000 00000000 00000000 00000000 ffffffff 00000000 00000000 00000000 00000000 00000000
              ^pos.x  ^pos.y  ^pos.z  ^--normal.xyz--^  ^diffuse  ^--UV0.u,UV0.v--^  ^--UV1.u,UV1.v--^  ^pad
              -1.0     1.0     0.0    0/0/0            white     0.0 / 0.0          0.0 / 0.0
```

All UV bytes are zero → every fragment of the backdrop quad samples texel (0,0) of the bound texture. If (0,0) in that texture is a transparent or black pixel (it is, for many menu backdrops with matte-transparent corners), the whole quad renders black through the SRC_ALPHA/INV_SRC_ALPHA blend.

Smaller gadget draws (#3+) have proper UVs — e.g. `uv0=(0,0.303) uv1=(0.921,0)` — so `Internal_Add_Quad_UVs` / `Render2DClass::Set_Texture` work for the normal Image-based draw path.

**Root-cause hypothesis (not yet confirmed):** `W3DDisplay::drawImage` builds the UV rect from `image->getUV()` (`W3DDisplay.cpp:2590`). For the full-screen main-menu backdrop Image object, `getUV()` appears to return a degenerate rect (`(0,0,0,0)`), so all four vertex UVs get written as `(0,0)`. The UV loader for that specific Image object is the next diagnostic target. Candidates: `GameWindowManagerScript::load` or the `MappedImage` INI parser — whichever populates `Image::m_UV` for backdrop-class images. Verify by printing `uv_rect` inside `drawImage` for the `image->getName()` that matches the main menu backdrop.

**Why the `-screenshot /tmp/...tga` output is entirely black but the running window shows a small white shape:** the `-screenshot` capture path requests the next `bgfx::frame` after screenshot arming, but the main-menu-ready frames (where real Image draws happen) come later. The capture ends up grabbing an earlier frame where only the black clear + a couple of non-textured (placeholder-white) letterbox/fill draws were submitted — hence "white shape on black" in the running window and pure black in the screenshot.

**Not fixed in this session.** Fixing requires tracing `Image::m_UV` initialization for backdrop images and/or making `drawImage` substitute `(0,0,1,1)` when `getUV()` returns a degenerate rect. That's a self-contained edit but needs careful handling because atlased images rely on real UV rects.

**Diagnostic code removed from BgfxBackend.cpp + render2d.cpp** before end of session. No code changes landed as a result of this round — purely investigation.
