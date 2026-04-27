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

#include <cstdarg>
#include <cstdint>

#include <bgfx/bgfx.h>

// Minimal bgfx::CallbackI implementation whose sole purpose is to receive
// backbuffer screenshots from `bgfx::requestScreenShot` and write them to
// disk as TGA. All other CallbackI duties (cache, profiler, video capture)
// are empty stubs; fatal/trace forward to stderr so bgfx errors remain
// visible.
//
// The callback instance is registered once in BgfxBackend::Init via
// `bgfx::Init::callback`. It holds no state between screenshots — each
// `screenShot` call is self-contained.
class BgfxScreenshotCallback final : public bgfx::CallbackI
{
public:
	BgfxScreenshotCallback() = default;
	~BgfxScreenshotCallback() override = default;

	void fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) override;
	void traceVargs(const char* filePath, uint16_t line, const char* format, va_list argList) override;

	void profilerBegin(const char* name, uint32_t abgr, const char* filePath, uint16_t line) override;
	void profilerBeginLiteral(const char* name, uint32_t abgr, const char* filePath, uint16_t line) override;
	void profilerEnd() override;

	uint32_t cacheReadSize(uint64_t id) override;
	bool cacheRead(uint64_t id, void* data, uint32_t size) override;
	void cacheWrite(uint64_t id, const void* data, uint32_t size) override;

	void screenShot(const char* filePath,
	                uint32_t width,
	                uint32_t height,
	                uint32_t pitch,
	                bgfx::TextureFormat::Enum format,
	                const void* data,
	                uint32_t size,
	                bool yflip) override;

	void captureBegin(uint32_t width, uint32_t height, uint32_t pitch,
	                  bgfx::TextureFormat::Enum format, bool yflip) override;
	void captureEnd() override;
	void captureFrame(const void* data, uint32_t size) override;
};
