/*
**	Phase 5h.3 smoke test: BgfxTextureCache path-keyed loader.
**
**	Hand-builds a minimal 16×16 A8R8G8B8 DDS on disk (same header layout
**	as tst_bgfx_bimg), then exercises the cache:
**	  1. Get_Or_Load_File on the path returns a non-zero handle; Size()==1.
**	  2. Second call for the same path returns the *identical* handle
**	     (dedup worked, no re-upload).
**	  3. Release drops the entry; Size()==0.
**	  4. Get_Or_Load_File again brings it back; handle non-zero.
**	  5. Clear_All sweeps the map; Size()==0.
**	  6. Bad path returns 0 and doesn't insert anything.
**
**	Temp file is written to SDL's prefer-path if available, else /tmp.
**	Cleaned up at exit regardless of pass/fail.
*/

#include <Utility/CppMacros.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>

#include "BGFXDevice/Common/BgfxBackend.h"
#include "BGFXDevice/Common/BgfxBootstrap.h"
#include "BGFXDevice/Common/BgfxTextureCache.h"
#include "SDLDevice/Common/SDLGlobals.h"

namespace
{

bool FailIf(bool cond, const char* msg)
{
	if (cond) { fprintf(stderr, "  %s\n", msg); return true; }
	return false;
}

void WriteU32(uint8_t* p, uint32_t v)
{
	p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); p[2] = uint8_t(v >> 16); p[3] = uint8_t(v >> 24);
}

// Hand-builds a 128-byte DDS header + 16×16 A8R8G8B8 pixels. Matches the
// layout documented in tst_bgfx_bimg / Phase 5j.
void BuildDDS(std::FILE* f)
{
	uint8_t header[128];
	std::memset(header, 0, sizeof(header));
	header[0]='D'; header[1]='D'; header[2]='S'; header[3]=' ';
	// Offsets are from file start; bytes 0-3 are the "DDS " magic. The 124-
	// byte DDS_HEADER runs [4..127], with DDS_PIXELFORMAT sitting at offset
	// 76 (after dwReserved1[11] ends at offset 75).
	WriteU32(header + 4,   124);                 // dwSize
	WriteU32(header + 8,   0x1007);              // dwFlags = CAPS|HEIGHT|WIDTH|PIXELFORMAT
	WriteU32(header + 12,  16);                  // dwHeight
	WriteU32(header + 16,  16);                  // dwWidth
	WriteU32(header + 20,  64);                  // dwPitchOrLinearSize = 16*4
	WriteU32(header + 76,  32);                  // DDS_PIXELFORMAT.dwSize
	WriteU32(header + 80,  0x41);                // flags = ALPHAPIXELS|RGB
	WriteU32(header + 88,  32);                  // dwRGBBitCount
	WriteU32(header + 92,  0x00FF0000);          // R mask
	WriteU32(header + 96,  0x0000FF00);          // G mask
	WriteU32(header + 100, 0x000000FF);          // B mask
	WriteU32(header + 104, 0xFF000000);          // A mask
	WriteU32(header + 108, 0x1000);              // dwCaps = TEXTURE
	std::fwrite(header, 1, sizeof(header), f);

	// 16×16 pixels of a magenta/cyan checker so the file has real content.
	for (int y = 0; y < 16; ++y)
	for (int x = 0; x < 16; ++x)
	{
		const bool m = ((x ^ y) & 1) == 0;
		const uint8_t R = m ? 0xFF : 0x00;
		const uint8_t G = m ? 0x00 : 0xFF;
		const uint8_t B = m ? 0xFF : 0xFF;
		const uint8_t A = 0xFF;
		// Memory order BGRA (little-endian 0xAARRGGBB dword).
		const uint8_t px[4] = { B, G, R, A };
		std::fwrite(px, 1, 4, f);
	}
}

std::string MakeTempPath()
{
	const char* prefs[] = { "/tmp", "."};
	for (const char* d : prefs)
	{
		std::string p = std::string(d) + "/tst_bgfx_texcache.dds";
		std::FILE* f = std::fopen(p.c_str(), "wb");
		if (f) { std::fclose(f); std::remove(p.c_str()); return p; }
	}
	return std::string("./tst_bgfx_texcache.dds");
}

} // namespace

int main(int /*argc*/, char* /*argv*/[])
{
	if (!SDL_Init(SDL_INIT_VIDEO))
	{
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return 1;
	}
	SDL_Window* window = SDL_CreateWindow("tst_bgfx_texcache", 320, 240, SDL_WINDOW_HIDDEN);
	if (!window) { fprintf(stderr, "SDL_CreateWindow failed\n"); SDL_Quit(); return 1; }
	SDLDevice::TheSDLWindow = window;
	void* nwh = SDLDevice::getNativeWindowHandle();
	if (!nwh) { SDL_DestroyWindow(window); SDL_Quit(); return 1; }

	if (!BgfxBootstrap::Ensure_Init(nwh, 320, 240, true))
	{
		fprintf(stderr, "Ensure_Init failed\n");
		SDL_DestroyWindow(window); SDL_Quit(); return 1;
	}

	const std::string tmp = MakeTempPath();
	{
		std::FILE* f = std::fopen(tmp.c_str(), "wb");
		if (!f)
		{
			fprintf(stderr, "failed to open %s for writing\n", tmp.c_str());
			BgfxBootstrap::Shutdown();
			SDL_DestroyWindow(window); SDL_Quit(); return 1;
		}
		BuildDDS(f);
		std::fclose(f);
	}

	bool ok = true;

	// Invariant 1 — first load succeeds, cache tracks it.
	const uintptr_t h1 = BgfxTextureCache::Get_Or_Load_File(tmp.c_str());
	ok &= !FailIf(h1 == 0, "invariant 1: first Get_Or_Load_File returned 0");
	ok &= !FailIf(BgfxTextureCache::Size() != 1, "invariant 1: Size() != 1 after first load");

	// Invariant 2 — second lookup is deduplicated.
	const uintptr_t h2 = BgfxTextureCache::Get_Or_Load_File(tmp.c_str());
	ok &= !FailIf(h2 != h1, "invariant 2: second load returned a different handle");
	ok &= !FailIf(BgfxTextureCache::Size() != 1, "invariant 2: Size() changed on repeat lookup");

	// Invariant 3 — Release drops the entry.
	BgfxTextureCache::Release(tmp.c_str());
	ok &= !FailIf(BgfxTextureCache::Size() != 0, "invariant 3: Size() != 0 after Release");

	// Invariant 4 — re-load after release still works.
	const uintptr_t h3 = BgfxTextureCache::Get_Or_Load_File(tmp.c_str());
	ok &= !FailIf(h3 == 0, "invariant 4: re-load returned 0");
	ok &= !FailIf(BgfxTextureCache::Size() != 1, "invariant 4: Size() != 1 after re-load");

	// Invariant 5 — Clear_All wipes everything.
	BgfxTextureCache::Clear_All();
	ok &= !FailIf(BgfxTextureCache::Size() != 0, "invariant 5: Size() != 0 after Clear_All");

	// Invariant 6 — bad path returns 0, doesn't pollute.
	const uintptr_t hBad = BgfxTextureCache::Get_Or_Load_File("/does/not/exist.dds");
	ok &= !FailIf(hBad != 0, "invariant 6: bad path returned non-zero");
	ok &= !FailIf(BgfxTextureCache::Size() != 0, "invariant 6: bad path polluted cache");

	// Cleanup.
	std::remove(tmp.c_str());
	BgfxBootstrap::Shutdown();
	SDLDevice::TheSDLWindow = nullptr;
	SDL_DestroyWindow(window);
	SDL_Quit();

	if (!ok) { fprintf(stderr, "tst_bgfx_texcache: FAILED\n"); return 1; }
	fprintf(stderr, "tst_bgfx_texcache: PASSED\n");
	return 0;
}
