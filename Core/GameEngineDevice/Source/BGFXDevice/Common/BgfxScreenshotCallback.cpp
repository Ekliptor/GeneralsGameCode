/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
*/

#include "BGFXDevice/Common/BgfxScreenshotCallback.h"

#include "TARGA.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// --- diagnostics --------------------------------------------------------

void BgfxScreenshotCallback::fatal(const char* filePath, uint16_t line,
                                   bgfx::Fatal::Enum /*code*/, const char* str)
{
	std::fprintf(stderr, "[bgfx:fatal] %s:%u: %s\n",
	             filePath ? filePath : "<null>",
	             static_cast<unsigned>(line),
	             str ? str : "<null>");
	std::fflush(stderr);
}

void BgfxScreenshotCallback::traceVargs(const char* /*filePath*/, uint16_t /*line*/,
                                        const char* format, va_list argList)
{
	if (format)
	{
		std::vfprintf(stderr, format, argList);
		std::fflush(stderr);
	}
}

// --- profiler (unused) --------------------------------------------------

void BgfxScreenshotCallback::profilerBegin(const char* /*name*/, uint32_t /*abgr*/,
                                           const char* /*filePath*/, uint16_t /*line*/) {}
void BgfxScreenshotCallback::profilerBeginLiteral(const char* /*name*/, uint32_t /*abgr*/,
                                                  const char* /*filePath*/, uint16_t /*line*/) {}
void BgfxScreenshotCallback::profilerEnd() {}

// --- shader cache (unused — fall back to bgfx's in-memory defaults) -----

uint32_t BgfxScreenshotCallback::cacheReadSize(uint64_t /*id*/) { return 0; }
bool BgfxScreenshotCallback::cacheRead(uint64_t /*id*/, void* /*data*/, uint32_t /*size*/) { return false; }
void BgfxScreenshotCallback::cacheWrite(uint64_t /*id*/, const void* /*data*/, uint32_t /*size*/) {}

// --- screenshot ---------------------------------------------------------

void BgfxScreenshotCallback::screenShot(const char* filePath,
                                        uint32_t width,
                                        uint32_t height,
                                        uint32_t pitch,
                                        bgfx::TextureFormat::Enum /*format*/,
                                        const void* data,
                                        uint32_t /*size*/,
                                        bool yflip)
{
	if (!filePath || !*filePath || !data || width == 0 || height == 0)
	{
		std::fprintf(stderr, "[bgfx:screenShot] ignored (missing path or data)\n");
		return;
	}

	// bgfx gives us 4-byte BGRA with `pitch` bytes per row (≥ width*4).
	// Targa wants a tightly-packed 24-bit RGB buffer (the TGA spec is BGR
	// on disk, but WWLib's Targa::Save writes the bytes we hand it verbatim
	// to the pixel area — and other sites in this tree that emit 24-bit
	// TGA via this class feed it BGR-ordered bytes, see
	// W3DDisplay::takeScreenShot's CAPTURE_TO_TARGA branch which writes
	// (B, G, R) per pixel before calling Save).
	const uint32_t pixelCount = width * height;
	char* image = static_cast<char*>(std::malloc(static_cast<size_t>(pixelCount) * 3));
	if (!image)
	{
		std::fprintf(stderr, "[bgfx:screenShot] malloc failed for %ux%u TGA\n", width, height);
		return;
	}

	const uint8_t* srcBase = static_cast<const uint8_t*>(data);
	for (uint32_t y = 0; y < height; ++y)
	{
		const uint8_t* srcRow = srcBase + static_cast<size_t>(y) * pitch;
		char* dstRow = image + static_cast<size_t>(y) * width * 3;
		for (uint32_t x = 0; x < width; ++x)
		{
			const uint8_t* px = srcRow + x * 4; // BGRA
			char* out = dstRow + x * 3;
			out[0] = static_cast<char>(px[0]); // B
			out[1] = static_cast<char>(px[1]); // G
			out[2] = static_cast<char>(px[2]); // R
		}
	}

	Targa targ;
	std::memset(&targ.Header, 0, sizeof(targ.Header));
	targ.Header.Width = static_cast<short>(width);
	targ.Header.Height = static_cast<short>(height);
	targ.Header.PixelDepth = 24;
	targ.Header.ImageType = TGA_TRUECOLOR;
	targ.SetImage(image);

	// TGA origin convention: TGAF_IMAGE + `Header.ImageType=TGA_TRUECOLOR`
	// writes pixels with bottom-left origin. bgfx's `_yflip=true` means the
	// source buffer already has bottom-left origin, so we leave it. When
	// `_yflip=false` the source is top-down and needs a flip to match TGA.
	if (!yflip)
		targ.YFlip();

	const long rc = targ.Save(filePath, TGAF_IMAGE, false);
	if (rc != 0)
	{
		std::fprintf(stderr, "[bgfx:screenShot] Targa::Save(%s) failed: rc=%ld\n", filePath, rc);
	}

	// Targa keeps the pointer we handed it; avoid freeing it before Save
	// returns. After Save we can drop the buffer — clear the pointer so the
	// destructor doesn't double-free.
	targ.SetImage(nullptr);
	std::free(image);
}

// --- video capture (unused) --------------------------------------------

void BgfxScreenshotCallback::captureBegin(uint32_t /*width*/, uint32_t /*height*/,
                                          uint32_t /*pitch*/,
                                          bgfx::TextureFormat::Enum /*format*/,
                                          bool /*yflip*/) {}
void BgfxScreenshotCallback::captureEnd() {}
void BgfxScreenshotCallback::captureFrame(const void* /*data*/, uint32_t /*size*/) {}
