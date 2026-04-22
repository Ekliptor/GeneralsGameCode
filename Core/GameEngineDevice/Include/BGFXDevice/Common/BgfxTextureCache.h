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

#include <cstddef>
#include <cstdint>
#include <vector>

// BgfxTextureCache
// ================
// Phase 5h.3 — deduplicating lazy-loader for DDS / KTX / PNG textures.
//
// Game code holds logical textures by path (e.g. "Art/Textures/tree.dds").
// Repeatedly calling `IRenderBackend::Create_Texture_From_Memory` for the
// same file would re-upload on every request; the cache keyed by path
// returns the previously-uploaded handle instead.
//
// Lifetime:
//   * Handles are owned by the backend (via Phase 5j's `m_ownedTextures`).
//     The cache holds `uintptr_t`s that point into that tracking list.
//   * Phase 5h.33 — entries are refcounted. `Get_Or_Load_File`
//     increments the refcount; `Release` decrements it. The handle is
//     only `Destroy_Texture`-d and removed from the map when the
//     refcount hits zero. This lets multiple `TextureClass` instances
//     safely share a single file-backed handle.
//   * `Clear_All` drops everything regardless of refcount.
//     `BgfxBootstrap::Shutdown` calls this *before* the backend itself
//     goes down so `Destroy_Texture` still has a live backend.
//
// Thread safety: none. Texture loads happen on the main thread in
// Generals; if that ever changes, add a mutex.
//
// DX8 builds: don't include this header — there's no corei_bgfx for it
// to link against.

namespace BgfxTextureCache
{
	// File reader plugged in by the game engine (routes through
	// TheFileSystem so BIG archives work). Default reader uses stdio, which
	// is fine for standalone smoke tests but can't see .big content.
	using FileReaderFn = bool(*)(const char* path, std::vector<uint8_t>& out);
	void Set_File_Reader(FileReaderFn fn);

	// Loads `path` from disk if not already cached, decodes via bimg,
	// uploads through the active IRenderBackend. Returns 0 if no backend
	// is active, the file can't be opened, or bimg can't decode it.
	// Subsequent calls with the same path return the cached handle.
	uintptr_t Get_Or_Load_File(const char* path);

	// Decrements the path's refcount. When the refcount reaches zero
	// the underlying bgfx texture is `Destroy_Texture`-d and the entry
	// is removed from the map. No-op if the path isn't cached.
	void Release(const char* path);

	// Drops every entry + destroys every underlying bgfx texture,
	// regardless of refcount. Invoked by BgfxBootstrap::Shutdown
	// before the backend goes down.
	void Clear_All();

	// Diagnostic: number of currently-cached entries.
	std::size_t Size();

	// Diagnostic: current refcount for `path`. Returns 0 if the path
	// isn't cached (indistinguishable from "cached but refcount==0",
	// which shouldn't happen since a zero refcount triggers removal).
	unsigned Ref_Count(const char* path);
}
