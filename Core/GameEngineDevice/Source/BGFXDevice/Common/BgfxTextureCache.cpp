/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#include <Utility/CppMacros.h>
#include "BGFXDevice/Common/BgfxTextureCache.h"

#include "WW3D2/IRenderBackend.h"
#include "WW3D2/RenderBackendRuntime.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <unordered_map>
#include <vector>

namespace BgfxTextureCache
{

namespace
{
	// Phase 5h.33 — entries track both the bgfx handle and a refcount.
	// Get_Or_Load_File bumps refCount; Release decrements; handle is
	// destroyed + entry erased when refCount drops to zero.
	struct Entry {
		uintptr_t handle;
		unsigned  refCount;
	};

	std::unordered_map<std::string, Entry>& Entries()
	{
		// Inline function-local static: single instance across TUs, no
		// static-init-order headaches.
		static std::unordered_map<std::string, Entry> s_map;
		return s_map;
	}

	bool Read_File(const char* path, std::vector<uint8_t>& out)
	{
		FILE* f = std::fopen(path, "rb");
		if (!f) return false;
		std::fseek(f, 0, SEEK_END);
		const long size = std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
		if (size <= 0) { std::fclose(f); return false; }
		out.resize(static_cast<std::size_t>(size));
		const std::size_t read = std::fread(out.data(), 1, out.size(), f);
		std::fclose(f);
		return read == out.size();
	}
}

uintptr_t Get_Or_Load_File(const char* path)
{
	if (path == nullptr || *path == '\0')
		return 0;

	auto& map = Entries();
	auto it = map.find(path);
	if (it != map.end())
	{
		++it->second.refCount;
		return it->second.handle;
	}

	IRenderBackend* backend = RenderBackendRuntime::Get_Active();
	if (backend == nullptr)
		return 0;

	std::vector<uint8_t> bytes;
	if (!Read_File(path, bytes))
	{
		std::fprintf(stderr, "BgfxTextureCache: can't read '%s'\n", path);
		return 0;
	}

	const uintptr_t handle = backend->Create_Texture_From_Memory(
		bytes.data(), static_cast<uint32_t>(bytes.size()));
	if (handle == 0)
	{
		std::fprintf(stderr, "BgfxTextureCache: bimg decode failed for '%s'\n", path);
		return 0;
	}

	map.emplace(path, Entry{handle, 1u});
	return handle;
}

void Release(const char* path)
{
	if (path == nullptr) return;
	auto& map = Entries();
	auto it = map.find(path);
	if (it == map.end()) return;

	if (it->second.refCount > 0)
		--it->second.refCount;

	if (it->second.refCount == 0)
	{
		IRenderBackend* backend = RenderBackendRuntime::Get_Active();
		if (backend != nullptr)
			backend->Destroy_Texture(it->second.handle);
		// If the backend has already shut down, the texture was already
		// destroyed by DestroyPipelineResources' sweep — we just drop the
		// map entry.
		map.erase(it);
	}
}

void Clear_All()
{
	auto& map = Entries();
	IRenderBackend* backend = RenderBackendRuntime::Get_Active();
	if (backend != nullptr)
	{
		for (const auto& kv : map)
			backend->Destroy_Texture(kv.second.handle);
	}
	map.clear();
}

std::size_t Size()
{
	return Entries().size();
}

unsigned Ref_Count(const char* path)
{
	if (path == nullptr) return 0;
	auto& map = Entries();
	auto it = map.find(path);
	return (it == map.end()) ? 0u : it->second.refCount;
}

} // namespace BgfxTextureCache
