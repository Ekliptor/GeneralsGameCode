/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Cross-platform implementation of FontCharsClass and Render2DSentenceClass
// for the BGFX renderer. The DX8 version in render2dsentence.cpp uses
// Win32 GDI; on the BGFX build that file is entirely #ifdef'd out. This
// one plugs the gap by rasterizing glyphs with stb_truetype into a
// per-font texture atlas and submitting textured quads through the
// existing Render2DClass pipeline.
//
// Per-instance backend state lives in a side-table keyed by object
// pointer so the public headers need no new fields.

#include "render2dsentence.h"

#ifndef RTS_RENDERER_DX8

#include "render2d.h"
#include "surfaceclass.h"
#include "texture.h"
#include "assetmgr.h"
#include "ww3dformat.h"
#include "vector2.h"
#include "vector2i.h"
#include "rect.h"
#include "wwstring.h"
#include "always.h"

#include "IRenderBackend.h"
#include "RenderBackendRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "ThirdParty/stb_truetype.h"

namespace {

// Atlas dimensions. 1024x1024 × A8R8G8B8 = 4 MiB per font variant — plenty
// for ASCII + extended Latin at menu sizes, well under the BGFX backend's
// texture budget. At HiDPI (rasterScale > ~1.5, e.g. macOS Retina in
// fullscreen) we double to 2048×2048 per font so the denser glyph bitmaps
// fit.
constexpr unsigned kAtlasW = 1024;
constexpr unsigned kAtlasH = 1024;
constexpr unsigned kAtlasPadding = 1;

// Query the active render backend for the display-pixel / UI-logical
// ratio so we can rasterize glyphs at the real pixel density. Returns 1.0
// when the backend isn't up yet (pre-device-init callers will seed at 1.0
// and the first rasterizeGlyph call re-queries; see FontImpl::finalizeRasterScale).
float currentFontRasterScale()
{
	if (IRenderBackend* be = RenderBackendRuntime::Get_Active()) {
		int pixW = 0, pixH = 0, logW = 0, logH = 0;
		be->Get_Back_Buffer_Size(pixW, pixH);
		be->Get_Logical_Resolution(logW, logH);
		if (logW > 0 && logH > 0 && pixW > 0 && pixH > 0) {
			const float sx = static_cast<float>(pixW) / static_cast<float>(logW);
			const float sy = static_cast<float>(pixH) / static_cast<float>(logH);
			return std::max(sx, sy);
		}
	}
	return 1.0f;
}

struct GlyphInfo {
	int bitmapW = 0;
	int bitmapH = 0;
	int offsetX = 0;     // pen origin → bitmap top-left, x
	int offsetY = 0;     // pen origin → bitmap top-left, y (top-of-line relative)
	int advance = 0;     // pen advance in pixels
	float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
	bool rasterized = false;
	bool missing = false;
};

// A single TTF blob, shared by any FontImpl that maps to this font family.
struct TTFBlob {
	std::vector<uint8_t> bytes;
	stbtt_fontinfo info{};
	bool ok = false;
};

std::vector<uint8_t> slurpFile(const char* path)
{
	std::vector<uint8_t> out;
	FILE* fp = std::fopen(path, "rb");
	if (!fp) return out;
	std::fseek(fp, 0, SEEK_END);
	long sz = std::ftell(fp);
	std::fseek(fp, 0, SEEK_SET);
	if (sz > 0) {
		out.resize(static_cast<size_t>(sz));
		if (std::fread(out.data(), 1, static_cast<size_t>(sz), fp) != static_cast<size_t>(sz)) {
			out.clear();
		}
	}
	std::fclose(fp);
	return out;
}

// Map a (font family name, bold) request to a TTF path. Fall back to a
// short list of widely-available fonts. macOS paths first; Linux/Win paths
// listed defensively for future cross-platform builds.
const char* const kFallbackFonts[] = {
#if defined(__APPLE__)
	"/System/Library/Fonts/Supplemental/Arial.ttf",
	"/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
	"/Library/Fonts/Arial.ttf",
	"/System/Library/Fonts/Geneva.ttf",
	"/System/Library/Fonts/Apple Symbols.ttf",
#elif defined(__linux__)
	"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
	"/usr/share/fonts/TTF/DejaVuSans.ttf",
	"/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
#else
	"C:/Windows/Fonts/arial.ttf",
	"C:/Windows/Fonts/segoeui.ttf",
#endif
	nullptr
};

const char* const kFallbackFontsBold[] = {
#if defined(__APPLE__)
	"/System/Library/Fonts/Supplemental/Arial Bold.ttf",
	"/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
	"/Library/Fonts/Arial Bold.ttf",
#elif defined(__linux__)
	"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
	"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
	"/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
#else
	"C:/Windows/Fonts/arialbd.ttf",
#endif
	nullptr
};

std::unordered_map<std::string, std::shared_ptr<TTFBlob>>& blobCache()
{
	static std::unordered_map<std::string, std::shared_ptr<TTFBlob>> c;
	return c;
}

std::shared_ptr<TTFBlob> loadTTF(const char* familyName, bool bold)
{
	// We don't try hard to honor familyName — the game requests specific
	// Windows font names (Arial, Tahoma, Times New Roman) that aren't all
	// present on other platforms. Just pick a serviceable fallback per
	// bold-vs-not and cache.
	(void)familyName;
	const char* const* list = bold ? kFallbackFontsBold : kFallbackFonts;
	for (const char* const* p = list; *p; ++p) {
		const std::string key(*p);
		auto& cache = blobCache();
		auto it = cache.find(key);
		if (it != cache.end()) return it->second;
		auto bytes = slurpFile(*p);
		if (bytes.empty()) continue;
		auto blob = std::make_shared<TTFBlob>();
		blob->bytes = std::move(bytes);
		if (stbtt_InitFont(&blob->info, blob->bytes.data(),
		                   stbtt_GetFontOffsetForIndex(blob->bytes.data(), 0))) {
			blob->ok = true;
			cache.emplace(key, blob);
			return blob;
		}
	}
	return nullptr;
}

// Per-FontCharsClass backend state.
struct FontImpl {
	std::shared_ptr<TTFBlob> blob;
	float scale = 1.0f;           // stb scale → logical pixels (for advance/ascent)
	float rasterStbScale = 1.0f;  // stb scale → atlas (dense) pixels
	float rasterScale = 1.0f;     // pixel/logical ratio; 1.0 == no HiDPI
	bool  rasterScaleFinalized = false; // true once queried with an active backend
	int pointSize = 12;
	bool bold = false;

	int ascentPx = 0;
	int descentPx = 0;
	int lineGapPx = 0;
	int lineHeightPx = 0;

	// Atlas dimensions grow with rasterScale so the denser glyphs fit.
	unsigned atlasW = kAtlasW;
	unsigned atlasH = kAtlasH;

	SurfaceClass* atlasSurface = nullptr;
	TextureClass* atlasTexture = nullptr;

	int penX = static_cast<int>(kAtlasPadding);
	int penY = static_cast<int>(kAtlasPadding);
	int rowH = 0;
	bool atlasFull = false;
	bool dirty = false;

	std::unordered_map<uint32_t, GlyphInfo> glyphs;

	~FontImpl()
	{
		if (atlasTexture) atlasTexture->Release_Ref();
		if (atlasSurface) atlasSurface->Release_Ref();
	}
};

// Pick an atlas edge length large enough for the requested raster density.
// 1× → 1024, anything above 1.5× → 2048. Above 2.5× the glyph count risks
// eviction but the pen wraps gracefully (atlasFull flag), so no clamp.
unsigned atlasEdgeForScale(float rasterScale)
{
	return (rasterScale > 1.5f) ? (kAtlasW * 2u) : kAtlasW;
}

std::unordered_map<const FontCharsClass*, FontImpl*>& fontImplTable()
{
	static std::unordered_map<const FontCharsClass*, FontImpl*> t;
	return t;
}

FontImpl* getImpl(const FontCharsClass* owner)
{
	if (!owner) return nullptr;
	auto& t = fontImplTable();
	auto it = t.find(owner);
	return it == t.end() ? nullptr : it->second;
}

// Per-Render2DSentenceClass backend state.
struct SentenceQuad {
	RectClass local;
	RectClass uv;
};

struct SentenceImpl {
	std::vector<SentenceQuad> quads;
	int hotKeyGlyphIndex = -1;
	float hotKeyX = 0;
	float hotKeyY = 0;
	float contentW = 0;
	float contentH = 0;
	Render2DClass* renderer = nullptr;
	FontCharsClass* boundFont = nullptr;

	~SentenceImpl()
	{
		delete renderer;
	}
};

std::unordered_map<const Render2DSentenceClass*, SentenceImpl*>& sentenceImplTable()
{
	static std::unordered_map<const Render2DSentenceClass*, SentenceImpl*> t;
	return t;
}

SentenceImpl* getSentenceImpl(const Render2DSentenceClass* owner, bool create)
{
	auto& t = sentenceImplTable();
	auto it = t.find(owner);
	if (it != t.end()) return it->second;
	if (!create) return nullptr;
	SentenceImpl* s = new SentenceImpl;
	t.emplace(owner, s);
	return s;
}

void releaseSentenceImpl(const Render2DSentenceClass* owner)
{
	auto& t = sentenceImplTable();
	auto it = t.find(owner);
	if (it != t.end()) {
		delete it->second;
		t.erase(it);
	}
}

// Lightweight metric queries that DO NOT touch the atlas or glyph cache.
// Vanilla Generals constructs DisplayStrings + sets text early, before
// the bgfx backend is up — those calls only need advance widths for
// layout, not pixels. Caching glyph entries from those calls used to
// pin the font at rasterScale=1.0 forever (the deferred re-target
// could never run because glyphs.empty() was false). Read directly from
// stb_truetype here and let actual rasterization happen lazily inside
// Build_Sentence, which is guaranteed to run after backend init.
int glyphAdvanceLogical(FontImpl* f, uint32_t codepoint)
{
	if (!f || !f->blob || !f->blob->ok) {
		return f ? (f->lineHeightPx / 3) : 6;
	}
	const int glyph = stbtt_FindGlyphIndex(&f->blob->info, static_cast<int>(codepoint));
	int adv = 0, lsb = 0;
	stbtt_GetGlyphHMetrics(&f->blob->info, glyph, &adv, &lsb);
	return static_cast<int>(std::round(adv * f->scale));
}

int glyphLogicalWidth(FontImpl* f, uint32_t codepoint)
{
	if (!f || !f->blob || !f->blob->ok) return 0;
	const int glyph = stbtt_FindGlyphIndex(&f->blob->info, static_cast<int>(codepoint));
	if (glyph == 0) return 0;
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	stbtt_GetGlyphBitmapBox(&f->blob->info, glyph, f->scale, f->scale, &x0, &y0, &x1, &y1);
	const int w = x1 - x0;
	return w > 0 ? w : 0;
}

// Idempotent. Called at the top of Build_Sentence — by which point the
// backend is provably up since drawing requires a render device. Locks
// in the real display scale and allocates the glyph atlas at the right
// dense size. Safe to call repeatedly; only does work the first time
// (and again if a backend reset changed the pixel/logical ratio).
void ensureFontDrawReady(FontImpl* f)
{
	if (!f || !f->blob || !f->blob->ok) return;

	if (!f->rasterScaleFinalized) {
		f->rasterScale = currentFontRasterScale();
		f->rasterStbScale = stbtt_ScaleForPixelHeight(&f->blob->info,
			static_cast<float>(f->pointSize) * f->rasterScale);
		// Only flip "finalized" if a backend was actually queried — the
		// 1.0 fallback should be replaced as soon as one comes up.
		f->rasterScaleFinalized = (RenderBackendRuntime::Get_Active() != nullptr);
	}

	const unsigned wantEdge = atlasEdgeForScale(f->rasterScale);
	const bool needRealloc = (f->atlasSurface == nullptr) ||
	                         (f->atlasW != wantEdge) ||
	                         (f->atlasH != wantEdge);
	if (needRealloc) {
		if (f->atlasTexture) { f->atlasTexture->Release_Ref(); f->atlasTexture = nullptr; }
		if (f->atlasSurface) { f->atlasSurface->Release_Ref(); f->atlasSurface = nullptr; }
		f->atlasW = wantEdge;
		f->atlasH = wantEdge;
		f->atlasSurface = NEW_REF(SurfaceClass, (f->atlasW, f->atlasH, WW3D_FORMAT_A8R8G8B8));
		f->atlasTexture = NEW_REF(TextureClass, (f->atlasSurface, MIP_LEVELS_1));
		f->atlasSurface->Clear();
		f->glyphs.clear();
		f->penX = static_cast<int>(kAtlasPadding);
		f->penY = static_cast<int>(kAtlasPadding);
		f->rowH = 0;
		f->atlasFull = false;
		f->dirty = false;
	}
}

bool rasterizeGlyph(FontImpl* f, uint32_t codepoint, GlyphInfo& out)
{
	if (!f || !f->blob || !f->blob->ok) {
		out.missing = true;
		out.advance = f ? (f->lineHeightPx / 3) : 6;
		return false;
	}

	// Caller (Build_Sentence) has already invoked ensureFontDrawReady so
	// rasterScale + atlas are pinned at the correct values by the time
	// we get here.

	int glyph = stbtt_FindGlyphIndex(&f->blob->info, static_cast<int>(codepoint));
	if (glyph == 0) {
		// No mapping for this codepoint.
		int adv = 0, lsb = 0;
		stbtt_GetGlyphHMetrics(&f->blob->info, 0, &adv, &lsb);
		out.missing = true;
		out.advance = static_cast<int>(std::round(adv * f->scale));
		return false;
	}

	int adv = 0, lsb = 0;
	stbtt_GetGlyphHMetrics(&f->blob->info, glyph, &adv, &lsb);
	// Advance stays in LOGICAL pixels so text layout matches the 800×600
	// design space.
	out.advance = static_cast<int>(std::round(adv * f->scale));

	// Logical-pixel bounds — drive the on-screen quad size.
	int lx0 = 0, ly0 = 0, lx1 = 0, ly1 = 0;
	stbtt_GetGlyphBitmapBox(&f->blob->info, glyph, f->scale, f->scale, &lx0, &ly0, &lx1, &ly1);
	const int lw = lx1 - lx0;
	const int lh = ly1 - ly0;
	if (lw <= 0 || lh <= 0) {
		out.bitmapW = 0;
		out.bitmapH = 0;
		out.rasterized = true;
		return true;
	}

	// Dense (atlas-pixel) bounds — drive the actual rasterization. At 1×
	// raster scale these match the logical bounds; at 2.4× they're ~2.4×
	// larger so the GPU samples a higher-resolution glyph into the same
	// logical-sized quad, eliminating the "upscale blur" that was visible
	// on small overlay text (FPS counter) in fullscreen.
	int dx0 = 0, dy0 = 0, dx1 = 0, dy1 = 0;
	stbtt_GetGlyphBitmapBox(&f->blob->info, glyph, f->rasterStbScale, f->rasterStbScale,
		&dx0, &dy0, &dx1, &dy1);
	const int dw = dx1 - dx0;
	const int dh = dy1 - dy0;
	if (dw <= 0 || dh <= 0) {
		out.bitmapW = 0;
		out.bitmapH = 0;
		out.rasterized = true;
		return true;
	}

	// Shelf-pack using DENSE dimensions — the atlas stores dense texels.
	if (f->penX + dw > static_cast<int>(f->atlasW)) {
		f->penX = static_cast<int>(kAtlasPadding);
		f->penY += f->rowH + static_cast<int>(kAtlasPadding);
		f->rowH = 0;
	}
	if (f->penY + dh > static_cast<int>(f->atlasH)) {
		f->atlasFull = true;
		out.missing = true;
		return false;
	}

	// stb rasterizes into a caller-provided grayscale buffer at DENSE
	// dimensions; composite into the A8R8G8B8 atlas (white RGB + coverage
	// in alpha) so Render2DClass's default SRC_ALPHA/INV_SRC_ALPHA blend works.
	std::vector<uint8_t> gray(static_cast<size_t>(dw * dh), 0);
	stbtt_MakeGlyphBitmap(&f->blob->info, gray.data(), dw, dh, dw,
		f->rasterStbScale, f->rasterStbScale, glyph);

	int pitch = 0;
	uint8_t* pixels = static_cast<uint8_t*>(f->atlasSurface->Lock(&pitch));
	if (!pixels) {
		out.missing = true;
		return false;
	}
	for (int y = 0; y < dh; ++y) {
		uint8_t* dstRow = pixels + (f->penY + y) * pitch + f->penX * 4;
		const uint8_t* srcRow = gray.data() + y * dw;
		for (int x = 0; x < dw; ++x) {
			dstRow[x * 4 + 0] = 0xFF; // B
			dstRow[x * 4 + 1] = 0xFF; // G
			dstRow[x * 4 + 2] = 0xFF; // R
			dstRow[x * 4 + 3] = srcRow[x];
		}
	}
	// Defer Unlock/upload until the end of Build_Sentence.

	// Quad dimensions + offsets stay LOGICAL — Build_Sentence uses these
	// directly as screen-space coords.
	out.bitmapW = lw;
	out.bitmapH = lh;
	out.offsetX = lx0;
	// stb y0/y1 are measured from the baseline with +y going down. Shift
	// by ascent so offsetY is from the top of the line box.
	out.offsetY = f->ascentPx + ly0;
	// UVs cover the DENSE region so the GPU samples every atlas texel.
	out.u0 = static_cast<float>(f->penX)      / static_cast<float>(f->atlasW);
	out.v0 = static_cast<float>(f->penY)      / static_cast<float>(f->atlasH);
	out.u1 = static_cast<float>(f->penX + dw) / static_cast<float>(f->atlasW);
	out.v1 = static_cast<float>(f->penY + dh) / static_cast<float>(f->atlasH);

	f->penX += dw + static_cast<int>(kAtlasPadding);
	if (dh > f->rowH) f->rowH = dh;
	f->dirty = true;
	out.rasterized = true;
	return true;
}

GlyphInfo* lookupOrRasterize(FontImpl* f, uint32_t codepoint)
{
	if (!f) return nullptr;
	auto it = f->glyphs.find(codepoint);
	if (it != f->glyphs.end()) return &it->second;
	GlyphInfo info;
	rasterizeGlyph(f, codepoint, info);
	auto ins = f->glyphs.emplace(codepoint, info);
	return &ins.first->second;
}

void ensureAtlasUploaded(FontImpl* f)
{
	if (!f || !f->dirty || !f->atlasSurface) return;
	f->atlasSurface->Unlock();
	f->dirty = false;
}

// Codepoint iteration over WCHAR text. WCHAR is 4 bytes on macOS/Linux,
// 2 bytes on Windows; surrogates only matter on the 2-byte side.
uint32_t nextCodepoint(const WCHAR*& p)
{
	if (!p || !*p) return 0;
	uint32_t c = static_cast<uint32_t>(*p++);
	if (sizeof(WCHAR) == 2 && c >= 0xD800 && c <= 0xDBFF && *p) {
		uint32_t lo = static_cast<uint32_t>(*p);
		if (lo >= 0xDC00 && lo <= 0xDFFF) {
			++p;
			c = 0x10000 + ((c - 0xD800) << 10) + (lo - 0xDC00);
		}
	}
	return c;
}

} // namespace

// =============================================================================
// FontCharsClass
// =============================================================================

FontCharsClass::FontCharsClass()
	: AlternateUnicodeFont(nullptr)
	, CurrPixelOffset(0)
	, CharHeight(0)
	, CharAscent(0)
	, CharOverhang(0)
	, PixelOverlap(0)
	, PointSize(0)
	, FirstUnicodeChar(0)
	, LastUnicodeChar(0)
	, IsBold(false)
	, UnicodeCharArray(nullptr)
{
	std::memset(ASCIICharArray, 0, sizeof(ASCIICharArray));
	fontImplTable().emplace(this, new FontImpl);
}

FontCharsClass::~FontCharsClass()
{
	if (AlternateUnicodeFont) {
		AlternateUnicodeFont->Release_Ref();
		AlternateUnicodeFont = nullptr;
	}
	auto& t = fontImplTable();
	auto it = t.find(this);
	if (it != t.end()) {
		delete it->second;
		t.erase(it);
	}
}

bool FontCharsClass::Initialize_GDI_Font(const char* font_name, int point_size, bool is_bold)
{
	Name = font_name ? font_name : "";
	GDIFontName = Name;

	// HiDPI readability boost — small UI fonts become unreadably tiny when
	// the 800×600 logical UI is stretched to a 6000+ pixel display. Previous
	// round used a static 1.5× for ≤10pt; now scale dynamically with the
	// real display ratio and extend the range to ≤14pt body text (column
	// headers, dropdown rows, stats labels) with a milder per-size boost so
	// they grow visibly without overflowing 800×600-design columns.
	//
	// Cap total boost at 1.8× so long labels (e.g. "USA Super Weapon
	// General") remain inside their dropdown column width.
	const float hidpiScale = currentFontRasterScale();
	if (hidpiScale >= 1.5f && point_size > 0 && point_size < 14) {
		const float density = std::min((hidpiScale - 1.0f) * 0.7f, 0.8f);          // 0.0 .. 0.8
		const float sizeWeight = 1.0f - std::min((point_size - 6) / 12.0f, 0.7f);  // small fonts get more
		float boost = 1.0f + density * (0.6f + 0.6f * sizeWeight);
		// 11–13pt fonts are typically widget labels (push buttons, dropdown
		// titles) authored to fit specific .wnd boxes at 800x600; boosting
		// them past ~1.3x makes glyphs spill outside the widget's hit-test
		// rect so users mis-aim at the visible text and miss the actual
		// click area. ≤10pt (FPS counter, exit-dialog body) keeps the
		// original 1.8x ceiling.
		const float cap = (point_size >= 11) ? 1.3f : 1.8f;
		boost = std::min(boost, cap);
		point_size = static_cast<int>(point_size * boost + 0.5f);
	}
	PointSize = point_size;
	IsBold = is_bold;

	FontImpl* f = getImpl(this);
	if (!f) return false;

	f->blob = loadTTF(font_name, is_bold);
	if (!f->blob || !f->blob->ok) {
		// Still set CharHeight so callers don't divide by zero.
		f->pointSize = point_size;
		f->bold = is_bold;
		f->ascentPx = point_size;
		f->descentPx = point_size / 4;
		f->lineHeightPx = f->ascentPx + f->descentPx;
		CharAscent = f->ascentPx;
		CharHeight = f->lineHeightPx;
		return false;
	}

	f->pointSize = point_size;
	f->bold = is_bold;
	f->scale = stbtt_ScaleForPixelHeight(&f->blob->info, static_cast<float>(point_size));

	// HiDPI raster scale is queried lazily inside ensureFontDrawReady at
	// the first Build_Sentence — by then the bgfx backend is provably up
	// (you can't draw without a device). Atlas allocation is deferred
	// for the same reason: nothing here may rasterize a glyph, so a font
	// can be initialized + measured before bgfx exists without locking
	// the atlas to a 1.0 raster scale.
	f->rasterScale = 1.0f;
	f->rasterStbScale = f->scale;
	f->rasterScaleFinalized = false;

	int asc = 0, desc = 0, gap = 0;
	stbtt_GetFontVMetrics(&f->blob->info, &asc, &desc, &gap);
	f->ascentPx    = static_cast<int>(std::ceil(asc  * f->scale));
	f->descentPx   = static_cast<int>(std::ceil(-desc * f->scale));
	f->lineGapPx   = static_cast<int>(std::ceil(gap  * f->scale));
	f->lineHeightPx = f->ascentPx + f->descentPx + f->lineGapPx;

	CharAscent = f->ascentPx;
	CharHeight = f->lineHeightPx;
	CharOverhang = 0;
	PixelOverlap = 0;

	return true;
}

bool FontCharsClass::Is_Font(const char* font_name, int point_size, bool is_bold)
{
	if (PointSize != point_size) return false;
	if (IsBold != is_bold) return false;
	if (!font_name) return Name.Is_Empty();
	return Name.Compare_No_Case(font_name) == 0;
}

int FontCharsClass::Get_Char_Width(WCHAR ch)
{
	FontImpl* f = getImpl(this);
	if (!f) return 0;
	return glyphLogicalWidth(f, static_cast<uint32_t>(ch));
}

int FontCharsClass::Get_Char_Spacing(WCHAR ch)
{
	FontImpl* f = getImpl(this);
	if (!f) return 0;
	return glyphAdvanceLogical(f, static_cast<uint32_t>(ch));
}

void FontCharsClass::Blit_Char(WCHAR /*ch*/, uint16* /*dest_ptr*/, int /*dest_stride*/, int /*x*/, int /*y*/)
{
	// Not used on BGFX: Render2DSentenceClass samples the atlas directly.
}

// =============================================================================
// Render2DSentenceClass
// =============================================================================

Render2DSentenceClass::Render2DSentenceClass()
	: Font(nullptr)
	, BaseLocation(0, 0)
	, Location(0.0f, 0.0f)
	, Cursor(0.0f, 0.0f)
	, TextureOffset(0, 0)
	, TextureStartX(0)
	, CurrTextureSize(0)
	, TextureSizeHint(0)
	, CurSurface(nullptr)
	, MonoSpaced(false)
	, WrapWidth(0.0f)
	, Centered(false)
	, ClipRect(0, 0, 0, 0)
	, DrawExtents(0, 0, 0, 0)
	, IsClippedEnabled(false)
	, ParseHotKey(false)
	, useHardWordWrap(false)
	, LockedPtr(nullptr)
	, LockedStride(0)
	, CurTexture(nullptr)
{
	Shader = Render2DClass::Get_Default_Shader();
	(void)getSentenceImpl(this, true);
}

Render2DSentenceClass::~Render2DSentenceClass()
{
	if (Font) {
		Font->Release_Ref();
		Font = nullptr;
	}
	releaseSentenceImpl(this);
}

void Render2DSentenceClass::Reset()
{
	SentenceImpl* impl = getSentenceImpl(this, true);
	impl->quads.clear();
	impl->hotKeyGlyphIndex = -1;
	impl->hotKeyX = impl->hotKeyY = 0;
	impl->contentW = impl->contentH = 0;
	if (impl->renderer) impl->renderer->Reset();
	Cursor.Set(0, 0);
	DrawExtents = RectClass(0, 0, 0, 0);
}

void Render2DSentenceClass::Reset_Polys()
{
	SentenceImpl* impl = getSentenceImpl(this, true);
	if (impl->renderer) impl->renderer->Reset();
}

void Render2DSentenceClass::Set_Font(FontCharsClass* font)
{
	Reset();
	if (Font == font) return;
	if (Font) Font->Release_Ref();
	Font = font;
	if (Font) Font->Add_Ref();

	SentenceImpl* impl = getSentenceImpl(this, true);
	FontImpl* f = getImpl(Font);
	TextureClass* atlas = f ? f->atlasTexture : nullptr;
	if (!impl->renderer) {
		impl->renderer = new Render2DClass(atlas);
	} else {
		impl->renderer->Set_Texture(atlas);
	}
	impl->boundFont = Font;
}

void Render2DSentenceClass::Set_Location(const Vector2& loc)
{
	Location = loc;
	Cursor = loc;
}

void Render2DSentenceClass::Set_Base_Location(const Vector2& loc)
{
	BaseLocation = loc;
}

void Render2DSentenceClass::Make_Additive()
{
	Shader.Set_Dst_Blend_Func(ShaderClass::DSTBLEND_ONE);
	Shader.Set_Src_Blend_Func(ShaderClass::SRCBLEND_ONE);
	Set_Shader(Shader);
}

void Render2DSentenceClass::Set_Shader(ShaderClass shader)
{
	Shader = shader;
	SentenceImpl* impl = getSentenceImpl(this, true);
	if (impl->renderer) {
		*(impl->renderer->Get_Shader()) = Shader;
	}
}

Vector2 Render2DSentenceClass::Get_Text_Extents(const WCHAR* text)
{
	if (!Font || !text) return Vector2(0, 0);
	FontImpl* f = getImpl(Font);
	if (!f) return Vector2(0, 0);

	float w = 0;
	float h = static_cast<float>(f->lineHeightPx);
	for (const WCHAR* p = text; *p; ) {
		uint32_t c = nextCodepoint(p);
		if (c == static_cast<uint32_t>('\n')) continue;
		if (c == static_cast<uint32_t>('&') && ParseHotKey) continue;
		w += static_cast<float>(glyphAdvanceLogical(f, c));
	}
	return Vector2(w, h);
}

Vector2 Render2DSentenceClass::Get_Formatted_Text_Extents(const WCHAR* text)
{
	if (!Font || !text) return Vector2(0, 0);
	FontImpl* f = getImpl(Font);
	if (!f) return Vector2(0, 0);

	float curLine = 0;
	float maxW = 0;
	int lines = 1;
	for (const WCHAR* p = text; *p; ) {
		uint32_t c = nextCodepoint(p);
		if (c == static_cast<uint32_t>('\n')) {
			maxW = std::max(maxW, curLine);
			curLine = 0;
			++lines;
			continue;
		}
		if (c == static_cast<uint32_t>('&') && ParseHotKey) continue;
		const float advance = static_cast<float>(glyphAdvanceLogical(f, c));
		if (WrapWidth > 0.0f && curLine + advance > WrapWidth) {
			maxW = std::max(maxW, curLine);
			curLine = 0;
			++lines;
		}
		curLine += advance;
	}
	maxW = std::max(maxW, curLine);
	return Vector2(maxW, static_cast<float>(f->lineHeightPx * lines));
}

void Render2DSentenceClass::Build_Sentence(const WCHAR* text, int* hkX, int* hkY)
{
	SentenceImpl* impl = getSentenceImpl(this, true);
	impl->quads.clear();
	impl->hotKeyGlyphIndex = -1;
	impl->hotKeyX = impl->hotKeyY = 0;
	impl->contentW = impl->contentH = 0;
	if (hkX) *hkX = -1;
	if (hkY) *hkY = -1;

	if (!Font || !text) return;
	FontImpl* f = getImpl(Font);
	if (!f) return;

	// Lock in the real display scale + allocate atlas now that we're at
	// draw time and the bgfx backend is up. Idempotent — does work only
	// the first call (and after a backend reset).
	ensureFontDrawReady(f);

	const float lineH = static_cast<float>(f->lineHeightPx);
	float penX = 0;
	float penY = 0;
	float maxX = 0;
	bool pendingHotkey = false;
	int lineStartIndex = 0;

	auto finishLine = [&](float lineWidth) {
		if (Centered) {
			const float shift = -lineWidth * 0.5f;
			for (int i = lineStartIndex; i < static_cast<int>(impl->quads.size()); ++i) {
				impl->quads[i].local.Left  += shift;
				impl->quads[i].local.Right += shift;
			}
		}
		if (lineWidth > maxX) maxX = lineWidth;
		lineStartIndex = static_cast<int>(impl->quads.size());
	};

	for (const WCHAR* p = text; *p; ) {
		uint32_t c = nextCodepoint(p);

		if (c == static_cast<uint32_t>('\n')) {
			finishLine(penX);
			penX = 0;
			penY += lineH;
			continue;
		}
		if (ParseHotKey && c == static_cast<uint32_t>('&')) {
			pendingHotkey = true;
			continue;
		}

		GlyphInfo* gi = lookupOrRasterize(f, c);
		if (!gi) continue;

		if (WrapWidth > 0.0f && penX + static_cast<float>(gi->advance) > WrapWidth && penX > 0) {
			finishLine(penX);
			penX = 0;
			penY += lineH;
		}

		if (!gi->missing && gi->bitmapW > 0 && gi->bitmapH > 0) {
			SentenceQuad q;
			const float x0 = penX + static_cast<float>(gi->offsetX);
			const float y0 = penY + static_cast<float>(gi->offsetY);
			q.local = RectClass(x0, y0,
			                    x0 + static_cast<float>(gi->bitmapW),
			                    y0 + static_cast<float>(gi->bitmapH));
			q.uv    = RectClass(gi->u0, gi->v0, gi->u1, gi->v1);
			impl->quads.push_back(q);

			if (pendingHotkey) {
				impl->hotKeyGlyphIndex = static_cast<int>(impl->quads.size()) - 1;
				impl->hotKeyX = penX;
				impl->hotKeyY = penY;
				if (hkX) *hkX = static_cast<int>(penX);
				if (hkY) *hkY = static_cast<int>(penY);
			}
		}
		penX += static_cast<float>(gi->advance);
		pendingHotkey = false;
	}
	finishLine(penX);

	impl->contentW = maxX;
	impl->contentH = penY + lineH;
	DrawExtents = RectClass(0, 0, impl->contentW, impl->contentH);

	ensureAtlasUploaded(f);

	// The first glyph may have triggered retargetRasterScale → new atlas
	// texture. Re-bind so Draw_Sentence samples the current atlas rather
	// than the stale one captured by Set_Font.
	if (impl->renderer && f->atlasTexture) {
		impl->renderer->Set_Texture(f->atlasTexture);
	}
}

void Render2DSentenceClass::Draw_Sentence(uint32 color)
{
	SentenceImpl* impl = getSentenceImpl(this, true);
	if (!impl->renderer) return;

	// Convert_Vert runs inside Add_Quad and uses the CoordinateScale/Offset
	// we set here. W3DDisplay keeps the *global* ScreenResolution rect up to
	// date; mirror it onto our private renderer so our glyph quads map from
	// logical pixel coords into the same NDC range the rest of the UI uses.
	impl->renderer->Set_Coordinate_Range(Render2DClass::Get_Screen_Resolution());

	for (const SentenceQuad& q : impl->quads) {
		RectClass screen(q.local.Left   + Location.X,
		                 q.local.Top    + Location.Y,
		                 q.local.Right  + Location.X,
		                 q.local.Bottom + Location.Y);

		if (IsClippedEnabled) {
			if (screen.Right  <= ClipRect.Left)   continue;
			if (screen.Left   >= ClipRect.Right)  continue;
			if (screen.Bottom <= ClipRect.Top)    continue;
			if (screen.Top    >= ClipRect.Bottom) continue;

			// Quad straddles the clip rect — shrink the screen rect to fit
			// and remap UVs proportionally so the partial glyph samples only
			// the visible slice of the atlas tile. Without this, long
			// dropdown rows (e.g. "USA Super Weapon General") bleed past
			// their column boundary into adjacent cells.
			const float screenW = screen.Right  - screen.Left;
			const float screenH = screen.Bottom - screen.Top;
			RectClass clippedScreen = screen;
			RectClass clippedUv     = q.uv;
			if (screen.Left   < ClipRect.Left   && screenW > 0.0f) {
				const float t = (ClipRect.Left - screen.Left) / screenW;
				clippedScreen.Left = ClipRect.Left;
				clippedUv.Left     = q.uv.Left + t * (q.uv.Right - q.uv.Left);
			}
			if (screen.Right  > ClipRect.Right  && screenW > 0.0f) {
				const float t = (screen.Right - ClipRect.Right) / screenW;
				clippedScreen.Right = ClipRect.Right;
				clippedUv.Right     = q.uv.Right - t * (q.uv.Right - q.uv.Left);
			}
			if (screen.Top    < ClipRect.Top    && screenH > 0.0f) {
				const float t = (ClipRect.Top - screen.Top) / screenH;
				clippedScreen.Top = ClipRect.Top;
				clippedUv.Top     = q.uv.Top + t * (q.uv.Bottom - q.uv.Top);
			}
			if (screen.Bottom > ClipRect.Bottom && screenH > 0.0f) {
				const float t = (screen.Bottom - ClipRect.Bottom) / screenH;
				clippedScreen.Bottom = ClipRect.Bottom;
				clippedUv.Bottom     = q.uv.Bottom - t * (q.uv.Bottom - q.uv.Top);
			}
			impl->renderer->Add_Quad(clippedScreen, clippedUv, color);
			continue;
		}
		impl->renderer->Add_Quad(screen, q.uv, color);
	}
}

void Render2DSentenceClass::Render()
{
	SentenceImpl* impl = getSentenceImpl(this, true);
	if (!impl->renderer) return;
	if (Font) ensureAtlasUploaded(getImpl(Font));
	impl->renderer->Render();
}

#endif // !RTS_RENDERER_DX8
