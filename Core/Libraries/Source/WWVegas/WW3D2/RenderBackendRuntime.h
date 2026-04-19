/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#pragma once

// RenderBackendRuntime
// ====================
// Phase 5h.1 — the seam through which production DX8 call sites find the
// active bgfx `IRenderBackend` (if any). Header-only, inline singleton:
//
//   * A DX8-only build: nothing ever calls `Set_Active`, so `Get_Active`
//     always returns `nullptr`. Production code's `if (auto* b = Get_Active())`
//     branch is dead and the existing DX8 fast path runs as-is.
//   * A bgfx build: `BgfxBackend::Init` calls `Set_Active(this)` once bgfx
//     itself has come up; `Shutdown` clears it. Production call sites can
//     then route through the interface when the pointer is non-null.
//
// Kept header-only so it lives in whichever translation unit includes it
// without a dedicated .cpp / static-library target. The function-local
// static in `Instance()` is single-definition across TUs by C++ inline
// semantics.

class IRenderBackend;

namespace RenderBackendRuntime
{
	// Implementation detail — the shared slot. Don't call directly.
	inline IRenderBackend*& Instance()
	{
		static IRenderBackend* s_backend = nullptr;
		return s_backend;
	}

	// Returns the currently-installed backend, or nullptr if none.
	inline IRenderBackend* Get_Active()
	{
		return Instance();
	}

	// Installs (or clears, with nullptr) the active backend. Callers are
	// responsible for clearing on shutdown so subsequent `Get_Active`
	// calls don't observe a dangling pointer.
	inline void Set_Active(IRenderBackend* backend)
	{
		Instance() = backend;
	}
}
