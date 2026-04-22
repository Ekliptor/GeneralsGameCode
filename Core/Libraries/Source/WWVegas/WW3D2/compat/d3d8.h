// d3d8.h — Portable D3D8 type shim for non-DX8 renderer builds (Phase 5d).
// Provides the same type names, enum values, and struct layouts as the real
// Direct3D 8 SDK headers so that game code compiles without modification on
// platforms where the Windows SDK is not available.
//
// This header is on the include path ONLY when RTS_RENDERER != dx8.  When
// building the DX8 backend, the real <d3d8.h> from min-dx8-sdk is used instead.

#pragma once

// ---------------------------------------------------------------------------
// Section 1 — Win32 type polyfills (only when not on Windows)
// ---------------------------------------------------------------------------
#ifndef _WIN32

#include <cstdint>
#include <cstring>

#ifndef _HRESULT_DEFINED
#define _HRESULT_DEFINED
typedef long HRESULT;
#endif

// Match bittype.h's existing DWORD definition (unsigned long, not uint32_t).
#include "bittype.h"

typedef int32_t     LONG;
typedef unsigned int UINT;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef int         BOOL;
typedef int         INT;
typedef float       FLOAT;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef CONST
#define CONST const
#endif

#ifndef S_OK
#define S_OK    ((HRESULT)0)
#endif
#ifndef S_FALSE
#define S_FALSE ((HRESULT)1)
#endif
#ifndef E_FAIL
#define E_FAIL  ((HRESULT)0x80004005L)
#endif

typedef void*       HWND;
typedef void*       HANDLE;
typedef void*       HDC;
typedef void*       HMONITOR;
typedef void*       HINSTANCE;

#ifndef WINAPI
#define WINAPI
#endif

#ifndef _RECT_DEFINED_
#define _RECT_DEFINED_
struct RECT  { LONG left, top, right, bottom; };
#endif
struct D3DLOCKED_RECT;
#ifndef _POINT_DEFINED_
#define _POINT_DEFINED_
struct POINT { LONG x, y; };
#endif

struct GUID {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
};

#ifndef _LARGE_INTEGER_DEFINED_
#define _LARGE_INTEGER_DEFINED_
typedef union _LARGE_INTEGER {
    struct { uint32_t LowPart; int32_t HighPart; };
    int64_t QuadPart;
} LARGE_INTEGER;
#endif

#ifndef MAKEFOURCC
#define MAKEFOURCC(a, b, c, d) \
    ((uint32_t)(uint8_t)(a)        | ((uint32_t)(uint8_t)(b) << 8) | \
     ((uint32_t)(uint8_t)(c) << 16) | ((uint32_t)(uint8_t)(d) << 24))
#endif

#endif // !_WIN32

// ---------------------------------------------------------------------------
// Section 2 — D3D8 COM interface stubs
// ---------------------------------------------------------------------------
// Forward-declared as minimal structs with stub AddRef/Release so that pointer
// members and inline code (Set_DX8_Texture's Release/AddRef calls) compile.
// These are never actually instantiated in bgfx builds.

#ifndef _D3D8_COM_STUBS_DEFINED
#define _D3D8_COM_STUBS_DEFINED

struct D3DADAPTER_IDENTIFIER8;

struct IDirect3D8 {
    unsigned long AddRef()  { return 1; }
    unsigned long Release() { return 0; }
    // Phase 5h — adapter-enum no-op stub. bgfx queries its own GPU identity.
    HRESULT GetAdapterIdentifier(UINT /*adapter*/, DWORD /*flags*/, D3DADAPTER_IDENTIFIER8* /*pIdent*/) { return S_OK; }
    UINT GetAdapterCount() { return 1; }
};

#ifndef D3DENUM_NO_WHQL_LEVEL
#define D3DENUM_NO_WHQL_LEVEL 0x00000002L
#endif

#ifndef D3DWRAP_U
#define D3DWRAP_U 0x00000001
#endif
#ifndef D3DWRAP_V
#define D3DWRAP_V 0x00000002
#endif
#ifndef D3DWRAP_W
#define D3DWRAP_W 0x00000004
#endif

// Forward decls for Create* stubs on IDirect3DDevice8 — the full declarations
// live later in this section.
struct IDirect3DVertexBuffer8;
struct IDirect3DIndexBuffer8;
struct IDirect3DSurface8;
struct IDirect3DBaseTexture8;
struct IDirect3DTexture8;
struct D3DSURFACE_DESC;

struct IDirect3DDevice8 {
    unsigned long AddRef()  { return 1; }
    unsigned long Release() { return 0; }
    // Phase 5h.15 — no-op resource-creation stubs so DX8VertexBufferClass +
    // DX8IndexBufferClass constructors compile in bgfx mode. Runtime-cold
    // (nothing instantiates those classes in bgfx); zero the output pointer
    // on success so any accidental caller fails loudly. Pool/format enums
    // are defined later in this header — use `UINT` here to avoid a
    // forward-declare mismatch; callers cast from the real enum via the
    // implicit promotion that D3D8's headers rely on.
    HRESULT CreateVertexBuffer(UINT /*length*/, DWORD /*usage*/, DWORD /*fvf*/,
                               UINT /*pool*/, IDirect3DVertexBuffer8** ppVB)
    {
        if (ppVB) *ppVB = nullptr;
        return S_OK;
    }
    HRESULT CreateIndexBuffer(UINT /*length*/, DWORD /*usage*/, UINT /*fmt*/,
                              UINT /*pool*/, IDirect3DIndexBuffer8** ppIB)
    {
        if (ppIB) *ppIB = nullptr;
        return S_OK;
    }
    HRESULT ResourceManagerDiscardBytes(DWORD /*bytes*/) { return S_OK; }
    // Phase 5h — scene-render no-op stubs so W3DScene's custom/extra-pass path
    // and renderStenciledPlayerColor compile in bgfx mode. Runtime-cold:
    // bgfx has its own shader / state / primitive draw path.
    HRESULT SetVertexShader(DWORD /*handle*/) { return S_OK; }
    HRESULT GetRenderState(DWORD /*state*/, DWORD* pValue) { if (pValue) *pValue = 0; return S_OK; }
    HRESULT DrawPrimitiveUP(UINT /*type*/, UINT /*primCount*/, const void* /*data*/, UINT /*stride*/) { return S_OK; }
    HRESULT TestCooperativeLevel() { return S_OK; }
    // Phase 5h — hardware cursor no-op stubs. bgfx doesn't drive a hardware
    // cursor; the W3DMouse cursor path renders via 2D/3D assets.
    BOOL ShowCursor(BOOL /*show*/) { return TRUE; }
    HRESULT SetCursorProperties(UINT /*xHotSpot*/, UINT /*yHotSpot*/, IDirect3DSurface8* /*pCursorBitmap*/) { return S_OK; }
    void SetCursorPosition(int /*x*/, int /*y*/, DWORD /*flags*/) {}
    // Phase 5h — shader / texture-stage / pixel-shader no-op stubs for the
    // legacy fixed-function + ps.1.1 paths. Runtime-cold: bgfx has its own
    // shader pipeline; these calls are dead in the bgfx build.
    HRESULT SetTexture(DWORD /*stage*/, IDirect3DBaseTexture8* /*texture*/) { return S_OK; }
    HRESULT SetTextureStageState(DWORD /*stage*/, DWORD /*type*/, DWORD /*value*/) { return S_OK; }
    HRESULT SetPixelShader(DWORD /*handle*/) { return S_OK; }
    HRESULT SetPixelShaderConstant(DWORD /*reg*/, const void* /*data*/, DWORD /*count*/) { return S_OK; }
    HRESULT DeletePixelShader(DWORD /*handle*/) { return S_OK; }
    HRESULT CreatePixelShader(const DWORD* /*function*/, DWORD* pHandle) { if (pHandle) *pHandle = 0; return S_OK; }
    HRESULT DeleteVertexShader(DWORD /*handle*/) { return S_OK; }
    HRESULT CreateVertexShader(const DWORD* /*decl*/, const DWORD* /*function*/, DWORD* pHandle, DWORD /*usage*/) { if (pHandle) *pHandle = 0; return S_OK; }
    HRESULT SetVertexShaderConstant(DWORD /*reg*/, const void* /*data*/, DWORD /*count*/) { return S_OK; }
    HRESULT SetStreamSource(UINT /*num*/, IDirect3DVertexBuffer8* /*pStream*/, UINT /*stride*/) { return S_OK; }
    HRESULT SetIndices(IDirect3DIndexBuffer8* /*pIB*/, UINT /*baseVertexIndex*/) { return S_OK; }
    HRESULT BeginScene() { return S_OK; }
    HRESULT EndScene() { return S_OK; }
    HRESULT Present(const RECT* /*src*/, const RECT* /*dst*/, HWND /*hwnd*/, const void* /*unused*/) { return S_OK; }
    HRESULT Clear(DWORD /*count*/, const void* /*rects*/, DWORD /*flags*/, DWORD /*color*/, float /*z*/, DWORD /*stencil*/) { return S_OK; }
    // Phase 5h — render-target / texture-create no-op stubs.
    HRESULT GetRenderTarget(IDirect3DSurface8** ppSurface) { if (ppSurface) *ppSurface = nullptr; return S_OK; }
    HRESULT SetRenderTarget(IDirect3DSurface8* /*pRenderTarget*/, IDirect3DSurface8* /*pZStencil*/) { return S_OK; }
    HRESULT GetDepthStencilSurface(IDirect3DSurface8** ppSurface) { if (ppSurface) *ppSurface = nullptr; return S_OK; }
    HRESULT CreateTexture(UINT /*width*/, UINT /*height*/, UINT /*levels*/, DWORD /*usage*/, UINT /*format*/, UINT /*pool*/, IDirect3DTexture8** ppTexture) { if (ppTexture) *ppTexture = nullptr; return S_OK; }
    HRESULT SetRenderState(DWORD /*state*/, DWORD /*value*/) { return S_OK; }
    HRESULT SetTransform(DWORD /*state*/, const void* /*matrix*/) { return S_OK; }
    HRESULT GetTransform(DWORD /*state*/, void* /*matrix*/) { return S_OK; }
    HRESULT SetViewport(const void* /*viewport*/) { return S_OK; }
    HRESULT GetViewport(void* /*viewport*/) { return S_OK; }
    HRESULT DrawIndexedPrimitive(DWORD /*type*/, UINT /*minVtx*/, UINT /*numVtx*/, UINT /*startIdx*/, UINT /*primCount*/) { return S_OK; }
    HRESULT DrawPrimitive(DWORD /*type*/, UINT /*startVtx*/, UINT /*primCount*/) { return S_OK; }
    HRESULT CreateImageSurface(UINT /*width*/, UINT /*height*/, UINT /*format*/, IDirect3DSurface8** ppSurface) { if (ppSurface) *ppSurface = nullptr; return S_OK; }
    HRESULT CopyRects(IDirect3DSurface8* /*src*/, const RECT* /*srcRects*/, UINT /*numRects*/, IDirect3DSurface8* /*dst*/, const POINT* /*destPoints*/) { return S_OK; }
};

#ifndef _WIN32
#define D3DCURSOR_IMMEDIATE_UPDATE 0x00000001
#endif

struct IDirect3DBaseTexture8 {
    unsigned long AddRef()  { return 1; }
    unsigned long Release() { return 0; }
    // Phase 5h.21 — priority no-op stubs. The real DX8 values govern texture
    // LRU behavior; bgfx has its own texture cache so the priority is
    // meaningless here. Fixed return (0) keeps callers deterministic.
    DWORD GetPriority() { return 0; }
    DWORD SetPriority(DWORD /*new_priority*/) { return 0; }
    DWORD SetLOD(DWORD /*new_lod*/) { return 0; }
    DWORD GetLOD() { return 0; }
};

struct IDirect3DTexture8 : IDirect3DBaseTexture8 {
    // Phase 5h — no-op surface-level accessor so TerrainTex / DX8 texture copy
    // paths compile in bgfx mode. Runtime-cold: bgfx owns its own texture data.
    inline HRESULT GetSurfaceLevel(UINT /*level*/, IDirect3DSurface8** ppSurface);
    inline HRESULT GetLevelDesc(UINT /*level*/, D3DSURFACE_DESC* pDesc);
    inline HRESULT LockRect(UINT /*level*/, D3DLOCKED_RECT* pLockedRect, const RECT* /*pRect*/, DWORD /*flags*/);
    inline HRESULT UnlockRect(UINT /*level*/);
    DWORD GetLevelCount() { return 1; }
};
struct IDirect3DCubeTexture8 : IDirect3DBaseTexture8 {};
struct IDirect3DVolumeTexture8 : IDirect3DBaseTexture8 {};

struct IDirect3DSurface8 {
    unsigned long AddRef()  { return 1; }
    unsigned long Release() { return 0; }
    // Phase 5h — no-op stubs for locked-rect access. Runtime-cold in bgfx mode;
    // shroud/snapshot paths call into these as part of D3D surface handling.
    // Defined below D3DLOCKED_RECT to keep that struct opaque here.
    inline HRESULT LockRect(D3DLOCKED_RECT* pLockedRect, const RECT* /*pRect*/, DWORD /*flags*/);
    HRESULT UnlockRect() { return S_OK; }
    inline HRESULT GetDesc(D3DSURFACE_DESC* pDesc);
};

struct IDirect3DSwapChain8 {
    unsigned long AddRef()  { return 1; }
    unsigned long Release() { return 0; }
};

struct IDirect3DVertexBuffer8 {
    unsigned long AddRef()  { return 1; }
    unsigned long Release() { return 0; }
    // Phase 5h.14 — no-op Lock/Unlock so the VertexBufferClass::WriteLockClass
    // DX8 dispatch path compiles. Runtime-cold in bgfx mode (no VBs of
    // BUFFER_TYPE_DX8 are instantiated yet); pass-through returns a null
    // payload so any accidental caller fails loudly rather than silently
    // writing into the void.
    HRESULT Lock(UINT /*OffsetToLock*/, UINT /*SizeToLock*/, BYTE** ppbData, DWORD /*Flags*/)
    {
        if (ppbData) *ppbData = nullptr;
        return S_OK;
    }
    HRESULT Unlock() { return S_OK; }
};

struct IDirect3DIndexBuffer8 {
    unsigned long AddRef()  { return 1; }
    unsigned long Release() { return 0; }
    HRESULT Lock(UINT /*OffsetToLock*/, UINT /*SizeToLock*/, BYTE** ppbData, DWORD /*Flags*/)
    {
        if (ppbData) *ppbData = nullptr;
        return S_OK;
    }
    HRESULT Unlock() { return S_OK; }
};

typedef IDirect3D8*              LPDIRECT3D8;
typedef IDirect3DDevice8*        LPDIRECT3DDEVICE8;
typedef IDirect3DTexture8*       LPDIRECT3DTEXTURE8;
typedef IDirect3DBaseTexture8*   LPDIRECT3DBASETEXTURE8;
typedef IDirect3DCubeTexture8*   LPDIRECT3DCUBETEXTURE8;
typedef IDirect3DVolumeTexture8* LPDIRECT3DVOLUMETEXTURE8;
typedef IDirect3DSurface8*       LPDIRECT3DSURFACE8;
typedef IDirect3DSwapChain8*     LPDIRECT3DSWAPCHAIN8;
typedef IDirect3DVertexBuffer8*  LPDIRECT3DVERTEXBUFFER8;
typedef IDirect3DIndexBuffer8*   LPDIRECT3DINDEXBUFFER8;

#endif // _D3D8_COM_STUBS_DEFINED

// ---------------------------------------------------------------------------
// Section 3 — D3D color type and macros
// ---------------------------------------------------------------------------

#ifndef D3DCOLOR_DEFINED
#define D3DCOLOR_DEFINED
typedef DWORD D3DCOLOR;
#endif

#ifndef D3DCOLOR_ARGB
#define D3DCOLOR_ARGB(a,r,g,b) \
    ((D3DCOLOR)((((a)&0xff)<<24)|(((r)&0xff)<<16)|(((g)&0xff)<<8)|((b)&0xff)))
#endif
#ifndef D3DCOLOR_XRGB
#define D3DCOLOR_XRGB(r,g,b) D3DCOLOR_ARGB(0xff,r,g,b)
#endif
#ifndef D3DCOLOR_RGBA
#define D3DCOLOR_RGBA(r,g,b,a) D3DCOLOR_ARGB(a,r,g,b)
#endif
#ifndef D3DCOLOR_COLORVALUE
#define D3DCOLOR_COLORVALUE(r,g,b,a) \
    D3DCOLOR_RGBA((DWORD)((r)*255.f),(DWORD)((g)*255.f),(DWORD)((b)*255.f),(DWORD)((a)*255.f))
#endif

#ifndef D3D_OK
#define D3D_OK S_OK
#endif

// ---------------------------------------------------------------------------
// Section 4 — D3D8 basic structs
// ---------------------------------------------------------------------------

#ifndef _D3D8_STRUCTS_DEFINED
#define _D3D8_STRUCTS_DEFINED

struct D3DVECTOR {
    float x, y, z;
};

struct D3DCOLORVALUE {
    float r, g, b, a;
};

#ifndef _D3DMATRIX_DEFINED
#define _D3DMATRIX_DEFINED
typedef struct _D3DMATRIX {
    union {
        struct {
            float _11, _12, _13, _14;
            float _21, _22, _23, _24;
            float _31, _32, _33, _34;
            float _41, _42, _43, _44;
        };
        float m[4][4];
    };
} D3DMATRIX;
#endif

struct D3DVIEWPORT8 {
    DWORD X, Y;
    DWORD Width, Height;
    float MinZ, MaxZ;
};

struct D3DMATERIAL8 {
    D3DCOLORVALUE Diffuse;
    D3DCOLORVALUE Ambient;
    D3DCOLORVALUE Specular;
    D3DCOLORVALUE Emissive;
    float Power;
};

// Light types
enum D3DLIGHTTYPE {
    D3DLIGHT_POINT       = 1,
    D3DLIGHT_SPOT        = 2,
    D3DLIGHT_DIRECTIONAL = 3,
};

struct D3DLIGHT8 {
    D3DLIGHTTYPE    Type;
    D3DCOLORVALUE   Diffuse;
    D3DCOLORVALUE   Specular;
    D3DCOLORVALUE   Ambient;
    D3DVECTOR       Position;
    D3DVECTOR       Direction;
    float           Range;
    float           Falloff;
    float           Attenuation0;
    float           Attenuation1;
    float           Attenuation2;
    float           Theta;
    float           Phi;
};

struct D3DLOCKED_RECT {
    INT   Pitch;
    void* pBits;
};

inline HRESULT IDirect3DSurface8::LockRect(D3DLOCKED_RECT* pLockedRect, const RECT* /*pRect*/, DWORD /*flags*/)
{
    if (pLockedRect) { pLockedRect->Pitch = 0; pLockedRect->pBits = nullptr; }
    return S_OK;
}

struct D3DCLIPSTATUS8 {
    DWORD ClipUnion;
    DWORD ClipIntersection;
};

#endif // _D3D8_STRUCTS_DEFINED

// ---------------------------------------------------------------------------
// Section 5 — Transform state types
// ---------------------------------------------------------------------------

typedef enum _D3DTRANSFORMSTATETYPE {
    D3DTS_VIEW         = 2,
    D3DTS_PROJECTION   = 3,
    D3DTS_TEXTURE0     = 16,
    D3DTS_TEXTURE1     = 17,
    D3DTS_TEXTURE2     = 18,
    D3DTS_TEXTURE3     = 19,
    D3DTS_TEXTURE4     = 20,
    D3DTS_TEXTURE5     = 21,
    D3DTS_TEXTURE6     = 22,
    D3DTS_TEXTURE7     = 23,
    D3DTS_WORLD        = 256,
    D3DTS_FORCE_DWORD  = 0x7fffffff,
} D3DTRANSFORMSTATETYPE;

#define D3DTS_WORLDMATRIX(index) (D3DTRANSFORMSTATETYPE)(index + 256)

// ---------------------------------------------------------------------------
// Section 6 — Render state types
// ---------------------------------------------------------------------------

typedef enum _D3DRENDERSTATETYPE {
    D3DRS_ZENABLE                   = 7,
    D3DRS_FILLMODE                  = 8,
    D3DRS_SHADEMODE                 = 9,
    D3DRS_LINEPATTERN               = 10,
    D3DRS_ZWRITEENABLE              = 14,
    D3DRS_ALPHATESTENABLE           = 15,
    D3DRS_LASTPIXEL                 = 16,
    D3DRS_SRCBLEND                  = 19,
    D3DRS_DESTBLEND                 = 20,
    D3DRS_CULLMODE                  = 22,
    D3DRS_ZFUNC                     = 23,
    D3DRS_ALPHAREF                  = 24,
    D3DRS_ALPHAFUNC                 = 25,
    D3DRS_DITHERENABLE              = 26,
    D3DRS_ALPHABLENDENABLE          = 27,
    D3DRS_FOGENABLE                 = 28,
    D3DRS_SPECULARENABLE            = 29,
    D3DRS_ZVISIBLE                  = 30,
    D3DRS_FOGCOLOR                  = 34,
    D3DRS_FOGTABLEMODE              = 35,
    D3DRS_FOGSTART                  = 36,
    D3DRS_FOGEND                    = 37,
    D3DRS_FOGDENSITY                = 38,
    D3DRS_EDGEANTIALIAS             = 40,
    D3DRS_ZBIAS                     = 47,
    D3DRS_RANGEFOGENABLE            = 48,
    D3DRS_STENCILENABLE             = 52,
    D3DRS_STENCILFAIL               = 53,
    D3DRS_STENCILZFAIL              = 54,
    D3DRS_STENCILPASS               = 55,
    D3DRS_STENCILFUNC               = 56,
    D3DRS_STENCILREF                = 57,
    D3DRS_STENCILMASK               = 58,
    D3DRS_STENCILWRITEMASK          = 59,
    D3DRS_TEXTUREFACTOR             = 60,
    D3DRS_WRAP0                     = 128,
    D3DRS_WRAP1                     = 129,
    D3DRS_WRAP2                     = 130,
    D3DRS_WRAP3                     = 131,
    D3DRS_WRAP4                     = 132,
    D3DRS_WRAP5                     = 133,
    D3DRS_WRAP6                     = 134,
    D3DRS_WRAP7                     = 135,
    D3DRS_CLIPPING                  = 136,
    D3DRS_LIGHTING                  = 137,
    D3DRS_AMBIENT                   = 139,
    D3DRS_FOGVERTEXMODE             = 140,
    D3DRS_COLORVERTEX               = 141,
    D3DRS_LOCALVIEWER               = 142,
    D3DRS_NORMALIZENORMALS          = 143,
    D3DRS_DIFFUSEMATERIALSOURCE     = 145,
    D3DRS_SPECULARMATERIALSOURCE    = 146,
    D3DRS_AMBIENTMATERIALSOURCE     = 147,
    D3DRS_EMISSIVEMATERIALSOURCE    = 148,
    D3DRS_VERTEXBLEND               = 151,
    D3DRS_CLIPPLANEENABLE           = 152,
    D3DRS_SOFTWAREVERTEXPROCESSING  = 153,
    D3DRS_POINTSIZE                 = 154,
    D3DRS_POINTSIZE_MIN             = 155,
    D3DRS_POINTSPRITEENABLE         = 156,
    D3DRS_POINTSCALEENABLE          = 157,
    D3DRS_POINTSCALE_A              = 158,
    D3DRS_POINTSCALE_B              = 159,
    D3DRS_POINTSCALE_C              = 160,
    D3DRS_MULTISAMPLEANTIALIAS      = 161,
    D3DRS_MULTISAMPLEMASK           = 162,
    D3DRS_PATCHEDGESTYLE            = 163,
    D3DRS_PATCHSEGMENTS             = 164,
    D3DRS_DEBUGMONITORTOKEN         = 165,
    D3DRS_POINTSIZE_MAX             = 166,
    D3DRS_INDEXEDVERTEXBLENDENABLE  = 167,
    D3DRS_COLORWRITEENABLE          = 168,
    D3DRS_TWEENFACTOR               = 170,
    D3DRS_BLENDOP                   = 171,
    D3DRS_POSITIONORDER             = 172,
    D3DRS_NORMALORDER               = 173,
} D3DRENDERSTATETYPE;

// D3DCOLORWRITEENABLE — bitmask values for D3DRS_COLORWRITEENABLE render state.
#define D3DCOLORWRITEENABLE_RED     (1L<<0)
#define D3DCOLORWRITEENABLE_GREEN   (1L<<1)
#define D3DCOLORWRITEENABLE_BLUE    (1L<<2)
#define D3DCOLORWRITEENABLE_ALPHA   (1L<<3)

// ---------------------------------------------------------------------------
// Section 7 — Texture stage state types
// ---------------------------------------------------------------------------

typedef enum _D3DTEXTURESTAGESTATETYPE {
    D3DTSS_COLOROP                  = 1,
    D3DTSS_COLORARG1                = 2,
    D3DTSS_COLORARG2                = 3,
    D3DTSS_ALPHAOP                  = 4,
    D3DTSS_ALPHAARG1                = 5,
    D3DTSS_ALPHAARG2                = 6,
    D3DTSS_BUMPENVMAT00             = 7,
    D3DTSS_BUMPENVMAT01             = 8,
    D3DTSS_BUMPENVMAT10             = 9,
    D3DTSS_BUMPENVMAT11             = 10,
    D3DTSS_TEXCOORDINDEX            = 11,
    D3DTSS_ADDRESSU                 = 13,
    D3DTSS_ADDRESSV                 = 14,
    D3DTSS_BORDERCOLOR              = 15,
    D3DTSS_MAGFILTER                = 16,
    D3DTSS_MINFILTER                = 17,
    D3DTSS_MIPFILTER                = 18,
    D3DTSS_MIPMAPLODBIAS            = 19,
    D3DTSS_MAXMIPLEVEL              = 20,
    D3DTSS_MAXANISOTROPY            = 21,
    D3DTSS_BUMPENVLSCALE            = 22,
    D3DTSS_BUMPENVLOFFSET           = 23,
    D3DTSS_TEXTURETRANSFORMFLAGS    = 24,
    D3DTSS_ADDRESSW                 = 25,
    D3DTSS_COLORARG0                = 26,
    D3DTSS_ALPHAARG0                = 27,
    D3DTSS_RESULTARG                = 28,
} D3DTEXTURESTAGESTATETYPE;

// Texture coordinate index flags
#define D3DTSS_TCI_PASSTHRU                       0x00000000
#define D3DTSS_TCI_CAMERASPACENORMAL              0x00010000
#define D3DTSS_TCI_CAMERASPACEPOSITION            0x00020000
#define D3DTSS_TCI_CAMERASPACEREFLECTIONVECTOR    0x00030000

// ---------------------------------------------------------------------------
// Section 8 — Texture operations, arguments, blend, compare, etc.
// ---------------------------------------------------------------------------

// Texture operations (D3DTOP_*)
#define D3DTOP_DISABLE                    1
#define D3DTOP_SELECTARG1                 2
#define D3DTOP_SELECTARG2                 3
#define D3DTOP_MODULATE                   4
#define D3DTOP_MODULATE2X                 5
#define D3DTOP_MODULATE4X                 6
#define D3DTOP_ADD                        7
#define D3DTOP_ADDSIGNED                  8
#define D3DTOP_ADDSIGNED2X                9
#define D3DTOP_SUBTRACT                   10
#define D3DTOP_ADDSMOOTH                  11
#define D3DTOP_BLENDDIFFUSEALPHA          12
#define D3DTOP_BLENDTEXTUREALPHA          13
#define D3DTOP_BLENDFACTORALPHA           14
#define D3DTOP_BLENDTEXTUREALPHAPM        15
#define D3DTOP_BLENDCURRENTALPHA          16
#define D3DTOP_PREMODULATE                17
#define D3DTOP_MODULATEALPHA_ADDCOLOR     18
#define D3DTOP_MODULATECOLOR_ADDALPHA     19
#define D3DTOP_MODULATEINVALPHA_ADDCOLOR  20
#define D3DTOP_MODULATEINVCOLOR_ADDALPHA  21
#define D3DTOP_BUMPENVMAP                 22
#define D3DTOP_BUMPENVMAPLUMINANCE        23
#define D3DTOP_DOTPRODUCT3                24
#define D3DTOP_MULTIPLYADD                25
#define D3DTOP_LERP                       26

// Texture arguments (D3DTA_*)
#define D3DTA_SELECTMASK        0x0000000f
#define D3DTA_DIFFUSE           0x00000000
#define D3DTA_CURRENT           0x00000001
#define D3DTA_TEXTURE           0x00000002
#define D3DTA_TFACTOR           0x00000003
#define D3DTA_SPECULAR          0x00000004
#define D3DTA_TEMP              0x00000005
#define D3DTA_COMPLEMENT        0x00000010
#define D3DTA_ALPHAREPLICATE    0x00000020

// Blend modes (D3DBLEND_*)
typedef enum _D3DBLEND {
    D3DBLEND_ZERO               = 1,
    D3DBLEND_ONE                = 2,
    D3DBLEND_SRCCOLOR           = 3,
    D3DBLEND_INVSRCCOLOR        = 4,
    D3DBLEND_SRCALPHA           = 5,
    D3DBLEND_INVSRCALPHA        = 6,
    D3DBLEND_DESTALPHA          = 7,
    D3DBLEND_INVDESTALPHA       = 8,
    D3DBLEND_DESTCOLOR          = 9,
    D3DBLEND_INVDESTCOLOR       = 10,
    D3DBLEND_SRCALPHASAT        = 11,
    D3DBLEND_BOTHSRCALPHA       = 12,
    D3DBLEND_BOTHINVSRCALPHA    = 13,
} D3DBLEND;

// Compare functions (D3DCMP_*)
typedef enum _D3DCMPFUNC {
    D3DCMP_NEVER            = 1,
    D3DCMP_LESS             = 2,
    D3DCMP_EQUAL            = 3,
    D3DCMP_LESSEQUAL        = 4,
    D3DCMP_GREATER          = 5,
    D3DCMP_NOTEQUAL         = 6,
    D3DCMP_GREATEREQUAL     = 7,
    D3DCMP_ALWAYS           = 8,
} D3DCMPFUNC;

// Stencil operations (D3DSTENCILOP_*)
typedef enum _D3DSTENCILOP {
    D3DSTENCILOP_KEEP       = 1,
    D3DSTENCILOP_ZERO       = 2,
    D3DSTENCILOP_REPLACE    = 3,
    D3DSTENCILOP_INCRSAT    = 4,
    D3DSTENCILOP_DECRSAT    = 5,
    D3DSTENCILOP_INVERT     = 6,
    D3DSTENCILOP_INCR       = 7,
    D3DSTENCILOP_DECR       = 8,
} D3DSTENCILOP;

// Cull modes
typedef enum _D3DCULL {
    D3DCULL_NONE    = 1,
    D3DCULL_CW      = 2,
    D3DCULL_CCW     = 3,
} D3DCULL;

// Fill modes
typedef enum _D3DFILLMODE {
    D3DFILL_POINT       = 1,
    D3DFILL_WIREFRAME   = 2,
    D3DFILL_SOLID       = 3,
} D3DFILLMODE;

// Shade modes
typedef enum _D3DSHADEMODE {
    D3DSHADE_FLAT       = 1,
    D3DSHADE_GOURAUD    = 2,
    D3DSHADE_PHONG      = 3,
} D3DSHADEMODE;

// Fog modes
typedef enum _D3DFOGMODE {
    D3DFOG_NONE     = 0,
    D3DFOG_EXP      = 1,
    D3DFOG_EXP2     = 2,
    D3DFOG_LINEAR   = 3,
} D3DFOGMODE;

// Zbuffer types
typedef enum _D3DZBUFFERTYPE {
    D3DZB_FALSE     = 0,
    D3DZB_TRUE      = 1,
    D3DZB_USEW      = 2,
} D3DZBUFFERTYPE;

// Material color source
typedef enum _D3DMATERIALCOLORSOURCE {
    D3DMCS_MATERIAL = 0,
    D3DMCS_COLOR1   = 1,
    D3DMCS_COLOR2   = 2,
} D3DMATERIALCOLORSOURCE;

// Vertex blend flags
typedef enum _D3DVERTEXBLENDFLAGS {
    D3DVBF_DISABLE  = 0,
    D3DVBF_1WEIGHTS = 1,
    D3DVBF_2WEIGHTS = 2,
    D3DVBF_3WEIGHTS = 3,
    D3DVBF_TWEENING = 255,
    D3DVBF_0WEIGHTS = 256,
} D3DVERTEXBLENDFLAGS;

// Blend operations
typedef enum _D3DBLENDOP {
    D3DBLENDOP_ADD          = 1,
    D3DBLENDOP_SUBTRACT     = 2,
    D3DBLENDOP_REVSUBTRACT  = 3,
    D3DBLENDOP_MIN          = 4,
    D3DBLENDOP_MAX          = 5,
} D3DBLENDOP;

// Texture address modes
typedef enum _D3DTEXTUREADDRESS {
    D3DTADDRESS_WRAP        = 1,
    D3DTADDRESS_MIRROR      = 2,
    D3DTADDRESS_CLAMP       = 3,
    D3DTADDRESS_BORDER      = 4,
    D3DTADDRESS_MIRRORONCE  = 5,
} D3DTEXTUREADDRESS;

// Texture filter types
typedef enum _D3DTEXTUREFILTERTYPE {
    D3DTEXF_NONE            = 0,
    D3DTEXF_POINT           = 1,
    D3DTEXF_LINEAR          = 2,
    D3DTEXF_ANISOTROPIC     = 3,
    D3DTEXF_FLATCUBIC       = 4,
    D3DTEXF_GAUSSIANCUBIC   = 5,
} D3DTEXTUREFILTERTYPE;

// Texture transform flags
typedef enum _D3DTEXTURETRANSFORMFLAGS {
    D3DTTFF_DISABLE     = 0,
    D3DTTFF_COUNT1      = 1,
    D3DTTFF_COUNT2      = 2,
    D3DTTFF_COUNT3      = 3,
    D3DTTFF_COUNT4      = 4,
    D3DTTFF_PROJECTED   = 256,
} D3DTEXTURETRANSFORMFLAGS;

// Patch edge style
typedef enum _D3DPATCHEDGESTYLE {
    D3DPATCHEDGE_DISCRETE   = 0,
    D3DPATCHEDGE_CONTINUOUS = 1,
} D3DPATCHEDGESTYLE;

// Debug monitor tokens
typedef enum _D3DDEBUGMONITORTOKENS {
    D3DDMT_ENABLE   = 0,
    D3DDMT_DISABLE  = 1,
} D3DDEBUGMONITORTOKENS;

// ---------------------------------------------------------------------------
// Section 9 — Primitive types
// ---------------------------------------------------------------------------

typedef enum _D3DPRIMITIVETYPE {
    D3DPT_POINTLIST     = 1,
    D3DPT_LINELIST      = 2,
    D3DPT_LINESTRIP     = 3,
    D3DPT_TRIANGLELIST  = 4,
    D3DPT_TRIANGLESTRIP = 5,
    D3DPT_TRIANGLEFAN   = 6,
} D3DPRIMITIVETYPE;

// ---------------------------------------------------------------------------
// Section 10 — Formats
// ---------------------------------------------------------------------------

typedef enum _D3DFORMAT {
    D3DFMT_UNKNOWN      = 0,
    D3DFMT_R8G8B8       = 20,
    D3DFMT_A8R8G8B8     = 21,
    D3DFMT_X8R8G8B8     = 22,
    D3DFMT_R5G6B5       = 23,
    D3DFMT_X1R5G5B5     = 24,
    D3DFMT_A1R5G5B5     = 25,
    D3DFMT_A4R4G4B4     = 26,
    D3DFMT_R3G3B2       = 27,
    D3DFMT_A8           = 28,
    D3DFMT_A8R3G3B2     = 29,
    D3DFMT_X4R4G4B4     = 30,
    D3DFMT_A8P8         = 40,
    D3DFMT_P8           = 41,
    D3DFMT_L8           = 50,
    D3DFMT_A8L8         = 51,
    D3DFMT_A4L4         = 52,
    D3DFMT_V8U8         = 60,
    D3DFMT_L6V5U5       = 61,
    D3DFMT_X8L8V8U8     = 62,
    D3DFMT_Q8W8V8U8     = 63,
    D3DFMT_V16U16       = 64,
    D3DFMT_W11V11U10    = 65,
    D3DFMT_UYVY         = MAKEFOURCC('U','Y','V','Y'),
    D3DFMT_YUY2         = MAKEFOURCC('Y','U','Y','2'),
    D3DFMT_DXT1         = MAKEFOURCC('D','X','T','1'),
    D3DFMT_DXT2         = MAKEFOURCC('D','X','T','2'),
    D3DFMT_DXT3         = MAKEFOURCC('D','X','T','3'),
    D3DFMT_DXT4         = MAKEFOURCC('D','X','T','4'),
    D3DFMT_DXT5         = MAKEFOURCC('D','X','T','5'),
    D3DFMT_D16_LOCKABLE = 70,
    D3DFMT_D32          = 71,
    D3DFMT_D15S1        = 73,
    D3DFMT_D24S8        = 75,
    D3DFMT_D16          = 80,
    D3DFMT_D24X8        = 77,
    D3DFMT_D24X4S4      = 79,
    D3DFMT_INDEX16      = 101,
    D3DFMT_INDEX32      = 102,
    D3DFMT_VERTEXDATA   = 100,
} D3DFORMAT;

// ---------------------------------------------------------------------------
// Section 11 — Pool, multisample, device type, swap effect
// ---------------------------------------------------------------------------

typedef enum _D3DPOOL {
    D3DPOOL_DEFAULT     = 0,
    D3DPOOL_MANAGED     = 1,
    D3DPOOL_SYSTEMMEM   = 2,
    D3DPOOL_SCRATCH     = 3,
} D3DPOOL;

typedef enum _D3DMULTISAMPLE_TYPE {
    D3DMULTISAMPLE_NONE         = 0,
    D3DMULTISAMPLE_2_SAMPLES    = 2,
    D3DMULTISAMPLE_3_SAMPLES    = 3,
    D3DMULTISAMPLE_4_SAMPLES    = 4,
    D3DMULTISAMPLE_5_SAMPLES    = 5,
    D3DMULTISAMPLE_6_SAMPLES    = 6,
    D3DMULTISAMPLE_7_SAMPLES    = 7,
    D3DMULTISAMPLE_8_SAMPLES    = 8,
    D3DMULTISAMPLE_9_SAMPLES    = 9,
    D3DMULTISAMPLE_10_SAMPLES   = 10,
    D3DMULTISAMPLE_11_SAMPLES   = 11,
    D3DMULTISAMPLE_12_SAMPLES   = 12,
    D3DMULTISAMPLE_13_SAMPLES   = 13,
    D3DMULTISAMPLE_14_SAMPLES   = 14,
    D3DMULTISAMPLE_15_SAMPLES   = 15,
    D3DMULTISAMPLE_16_SAMPLES   = 16,
} D3DMULTISAMPLE_TYPE;

typedef enum _D3DDEVTYPE {
    D3DDEVTYPE_HAL  = 1,
    D3DDEVTYPE_REF  = 2,
    D3DDEVTYPE_SW   = 3,
} D3DDEVTYPE;

typedef enum _D3DSWAPEFFECT {
    D3DSWAPEFFECT_DISCARD     = 1,
    D3DSWAPEFFECT_FLIP        = 2,
    D3DSWAPEFFECT_COPY        = 3,
    D3DSWAPEFFECT_COPY_VSYNC  = 4,
} D3DSWAPEFFECT;

typedef enum _D3DBACKBUFFER_TYPE {
    D3DBACKBUFFER_TYPE_MONO  = 0,
    D3DBACKBUFFER_TYPE_LEFT  = 1,
    D3DBACKBUFFER_TYPE_RIGHT = 2,
} D3DBACKBUFFER_TYPE;

typedef enum _D3DRESOURCETYPE {
    D3DRTYPE_SURFACE        = 1,
    D3DRTYPE_VOLUME         = 2,
    D3DRTYPE_TEXTURE        = 3,
    D3DRTYPE_VOLUMETEXTURE  = 4,
    D3DRTYPE_CUBETEXTURE    = 5,
    D3DRTYPE_VERTEXBUFFER   = 6,
    D3DRTYPE_INDEXBUFFER    = 7,
} D3DRESOURCETYPE;

// ---------------------------------------------------------------------------
// Section 12 — FVF flags
// ---------------------------------------------------------------------------

#define D3DFVF_XYZ              0x002
#define D3DFVF_XYZRHW           0x004
#define D3DFVF_XYZB1            0x006
#define D3DFVF_XYZB2            0x008
#define D3DFVF_XYZB3            0x00a
#define D3DFVF_XYZB4            0x00c
#define D3DFVF_XYZB5            0x00e
#define D3DFVF_NORMAL           0x010
#define D3DFVF_PSIZE            0x020
#define D3DFVF_DIFFUSE          0x040
#define D3DFVF_SPECULAR         0x080
#define D3DFVF_TEXCOUNT_MASK    0xf00
#define D3DFVF_TEXCOUNT_SHIFT   8
#define D3DFVF_TEX0             0x000
#define D3DFVF_TEX1             0x100
#define D3DFVF_TEX2             0x200
#define D3DFVF_TEX3             0x300
#define D3DFVF_TEX4             0x400
#define D3DFVF_TEX5             0x500
#define D3DFVF_TEX6             0x600
#define D3DFVF_TEX7             0x700
#define D3DFVF_TEX8             0x800
#define D3DFVF_POSITION_MASK    0x00e
#define D3DFVF_LASTBETA_UBYTE4  0x1000

#define D3DFVF_TEXTUREFORMAT1   3
#define D3DFVF_TEXTUREFORMAT2   0
#define D3DFVF_TEXTUREFORMAT3   1
#define D3DFVF_TEXTUREFORMAT4   2
#define D3DFVF_TEXCOORDSIZE1(i) (D3DFVF_TEXTUREFORMAT1 << (i*2 + 16))
#define D3DFVF_TEXCOORDSIZE2(i) (D3DFVF_TEXTUREFORMAT2 << (i*2 + 16))
#define D3DFVF_TEXCOORDSIZE3(i) (D3DFVF_TEXTUREFORMAT3 << (i*2 + 16))
#define D3DFVF_TEXCOORDSIZE4(i) (D3DFVF_TEXTUREFORMAT4 << (i*2 + 16))

// D3DFVF_POINTVERTEX is used in the codebase — define as typical point vertex format
#define D3DFVF_POINTVERTEX      (D3DFVF_XYZ | D3DFVF_DIFFUSE)

// Maximum texture coordinate sets
#define D3DDP_MAXTEXCOORD 8

// ---------------------------------------------------------------------------
// Section 13 — Lock, usage, clear, present flags
// ---------------------------------------------------------------------------

#define D3DLOCK_READONLY        0x00000010
#define D3DLOCK_DISCARD         0x00002000
#define D3DLOCK_NOOVERWRITE     0x00001000
#define D3DLOCK_NOSYSLOCK       0x00000800
#define D3DLOCK_NO_DIRTY_UPDATE 0x00008000

#define D3DUSAGE_RENDERTARGET       0x00000001
#define D3DUSAGE_DEPTHSTENCIL       0x00000002
#define D3DUSAGE_WRITEONLY          0x00000008
#define D3DUSAGE_SOFTWAREPROCESSING 0x00000010
#define D3DUSAGE_POINTS             0x00000040
#define D3DUSAGE_NPATCHES           0x00000100
#define D3DUSAGE_DYNAMIC            0x00000200

#define D3DCLEAR_TARGET     0x00000001
#define D3DCLEAR_ZBUFFER    0x00000002
#define D3DCLEAR_STENCIL    0x00000004

#define D3DPRESENT_INTERVAL_DEFAULT     0x00000000
#define D3DPRESENT_INTERVAL_ONE         0x00000001
#define D3DPRESENT_INTERVAL_TWO         0x00000002
#define D3DPRESENT_INTERVAL_THREE       0x00000004
#define D3DPRESENT_INTERVAL_IMMEDIATE   0x80000000
#define D3DPRESENT_RATE_DEFAULT         0x00000000

#define D3DPRESENTFLAG_LOCKABLE_BACKBUFFER  0x00000001

// ---------------------------------------------------------------------------
// Section 14 — Vertex shader declaration macros
// ---------------------------------------------------------------------------

#define D3DVSD_STREAM(StreamNumber)   ((DWORD)(1 << 28) | ((StreamNumber) << 0))
#define D3DVSD_REG(reg, type)         ((DWORD)(2 << 28) | ((reg) << 0) | ((type) << 16))
#define D3DVSD_END()                  0xFFFFFFFF

#define D3DVSDT_FLOAT2      0x01
#define D3DVSDT_FLOAT3      0x02
#define D3DVSDT_D3DCOLOR    0x04

// ---------------------------------------------------------------------------
// Section 15 — Error codes
// ---------------------------------------------------------------------------

#define _FACD3D 0x876
#define MAKE_D3DHRESULT(code) ((HRESULT)(1 << 31 | _FACD3D << 16 | (code)))

#define D3DERR_WRONGTEXTUREFORMAT           MAKE_D3DHRESULT(2072)
#define D3DERR_UNSUPPORTEDCOLOROPERATION    MAKE_D3DHRESULT(2073)
#define D3DERR_UNSUPPORTEDCOLORARG          MAKE_D3DHRESULT(2074)
#define D3DERR_UNSUPPORTEDALPHAOPERATION    MAKE_D3DHRESULT(2075)
#define D3DERR_UNSUPPORTEDALPHAARG          MAKE_D3DHRESULT(2076)
#define D3DERR_TOOMANYOPERATIONS            MAKE_D3DHRESULT(2077)
#define D3DERR_CONFLICTINGTEXTUREFILTER     MAKE_D3DHRESULT(2078)
#define D3DERR_UNSUPPORTEDFACTORVALUE       MAKE_D3DHRESULT(2079)
#define D3DERR_CONFLICTINGTEXTUREPALETTE    MAKE_D3DHRESULT(2086)
#define D3DERR_NOTAVAILABLE                 MAKE_D3DHRESULT(2154)
#define D3DERR_OUTOFVIDEOMEMORY             MAKE_D3DHRESULT(380)
#define D3DERR_DEVICELOST                   MAKE_D3DHRESULT(2152)
#define D3DERR_DEVICENOTRESET               MAKE_D3DHRESULT(2153)
#define D3DERR_UNSUPPORTEDTEXTUREFILTER     MAKE_D3DHRESULT(2082)
#define D3DERR_INVALIDCALL                  MAKE_D3DHRESULT(2156)
#define D3DERR_DRIVERINTERNALERROR          MAKE_D3DHRESULT(2087)

// ---------------------------------------------------------------------------
// Section 16 — Caps flags
// ---------------------------------------------------------------------------

#define D3DCAPS2_FULLSCREENGAMMA        0x00020000

#define D3DDEVCAPS_HWTRANSFORMANDLIGHT  0x00010000
#define D3DDEVCAPS_NPATCHES             0x01000000

#define D3DPMISCCAPS_COLORWRITEENABLE   0x00000800
#define D3DPRASTERCAPS_FOGRANGE         0x00010000
#define D3DPRASTERCAPS_ZBIAS            0x00004000
#define D3DPTEXTURECAPS_CUBEMAP         0x00000800

#define D3DSTENCILCAPS_KEEP       0x00000001
#define D3DSTENCILCAPS_ZERO       0x00000002
#define D3DSTENCILCAPS_REPLACE    0x00000004
#define D3DSTENCILCAPS_INCRSAT    0x00000008
#define D3DSTENCILCAPS_DECRSAT    0x00000010
#define D3DSTENCILCAPS_INVERT     0x00000020
#define D3DSTENCILCAPS_INCR       0x00000040
#define D3DSTENCILCAPS_DECR       0x00000080

#define D3DFVFCAPS_DONOTSTRIPELEMENTS   0x00080000
#define D3DFVFCAPS_TEXCOORDCOUNTMASK    0x0000ffff

#define D3DVTXPCAPS_DIRECTIONALLIGHTS   0x00000008
#define D3DVTXPCAPS_POSITIONALLIGHTS    0x00000010
#define D3DVTXPCAPS_TEXGEN              0x00010000
#define D3DVTXPCAPS_TEXGEN_SPHEREMAP    0x00000100

#define D3DLINECAPS_TEXTURE     0x00000001
#define D3DLINECAPS_ZTEST       0x00000002
#define D3DLINECAPS_BLEND       0x00000004
#define D3DLINECAPS_ALPHACMP    0x00000008
#define D3DLINECAPS_FOG         0x00000010

// Texture operation caps
#define D3DTEXOPCAPS_DISABLE                0x00000001
#define D3DTEXOPCAPS_SELECTARG1             0x00000002
#define D3DTEXOPCAPS_SELECTARG2             0x00000004
#define D3DTEXOPCAPS_MODULATE               0x00000008
#define D3DTEXOPCAPS_MODULATE2X             0x00000010
#define D3DTEXOPCAPS_MODULATE4X             0x00000020
#define D3DTEXOPCAPS_ADD                    0x00000040
#define D3DTEXOPCAPS_ADDSIGNED              0x00000080
#define D3DTEXOPCAPS_ADDSIGNED2X            0x00000100
#define D3DTEXOPCAPS_SUBTRACT               0x00000200
#define D3DTEXOPCAPS_ADDSMOOTH              0x00000400
#define D3DTEXOPCAPS_BLENDDIFFUSEALPHA      0x00000800
#define D3DTEXOPCAPS_BLENDTEXTUREALPHA      0x00001000
#define D3DTEXOPCAPS_BLENDFACTORALPHA       0x00002000
#define D3DTEXOPCAPS_BLENDTEXTUREALPHAPM    0x00004000
#define D3DTEXOPCAPS_BLENDCURRENTALPHA      0x00008000
#define D3DTEXOPCAPS_PREMODULATE            0x00010000
#define D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR 0x00020000
#define D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA 0x00040000
#define D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR 0x00080000
#define D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA 0x00100000
#define D3DTEXOPCAPS_BUMPENVMAP             0x00200000
#define D3DTEXOPCAPS_BUMPENVMAPLUMINANCE    0x00400000
#define D3DTEXOPCAPS_DOTPRODUCT3            0x00800000
#define D3DTEXOPCAPS_MULTIPLYADD            0x01000000
#define D3DTEXOPCAPS_LERP                   0x02000000

// Create/vertex processing flags
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING  0x00000020
#define D3DCREATE_HARDWARE_VERTEXPROCESSING  0x00000040
#define D3DCREATE_MIXED_VERTEXPROCESSING     0x00000080

// Pixel/vertex shader version
#define D3DVS_VERSION(major, minor) (0xFFFE0000 | ((major) << 8) | (minor))
#define D3DPS_VERSION(major, minor) (0xFFFF0000 | ((major) << 8) | (minor))
#define D3DSHADER_VERSION_MAJOR(version) (((version) >> 8) & 0xFF)
#define D3DSHADER_VERSION_MINOR(version) ((version) & 0xFF)

// ---------------------------------------------------------------------------
// Section 17 — Capability structs
// ---------------------------------------------------------------------------

#ifndef _D3D8_CAPS_STRUCTS_DEFINED
#define _D3D8_CAPS_STRUCTS_DEFINED

#define MAX_DEVICE_IDENTIFIER_STRING 512

struct D3DADAPTER_IDENTIFIER8 {
    char            Driver[MAX_DEVICE_IDENTIFIER_STRING];
    char            Description[MAX_DEVICE_IDENTIFIER_STRING];
    LARGE_INTEGER   DriverVersion;
    DWORD           VendorId;
    DWORD           DeviceId;
    DWORD           SubSysId;
    DWORD           Revision;
    GUID            DeviceIdentifier;
    DWORD           WHQLLevel;
};

struct D3DCAPS8 {
    D3DDEVTYPE          DeviceType;
    UINT                AdapterOrdinal;
    DWORD               Caps;
    DWORD               Caps2;
    DWORD               Caps3;
    DWORD               PresentationIntervals;
    DWORD               CursorCaps;
    DWORD               DevCaps;
    DWORD               PrimitiveMiscCaps;
    DWORD               RasterCaps;
    DWORD               ZCmpCaps;
    DWORD               SrcBlendCaps;
    DWORD               DestBlendCaps;
    DWORD               AlphaCmpCaps;
    DWORD               ShadeCaps;
    DWORD               TextureCaps;
    DWORD               TextureFilterCaps;
    DWORD               CubeTextureFilterCaps;
    DWORD               VolumeTextureFilterCaps;
    DWORD               TextureAddressCaps;
    DWORD               VolumeTextureAddressCaps;
    DWORD               LineCaps;
    DWORD               MaxTextureWidth;
    DWORD               MaxTextureHeight;
    DWORD               MaxVolumeExtent;
    DWORD               MaxTextureRepeat;
    DWORD               MaxTextureAspectRatio;
    DWORD               MaxAnisotropy;
    float               MaxVertexW;
    float               GuardBandLeft;
    float               GuardBandTop;
    float               GuardBandRight;
    float               GuardBandBottom;
    float               ExtentsAdjust;
    DWORD               StencilCaps;
    DWORD               FVFCaps;
    DWORD               TextureOpCaps;
    DWORD               MaxTextureBlendStages;
    DWORD               MaxSimultaneousTextures;
    DWORD               VertexProcessingCaps;
    DWORD               MaxActiveLights;
    DWORD               MaxUserClipPlanes;
    DWORD               MaxVertexBlendMatrices;
    DWORD               MaxVertexBlendMatrixIndex;
    float               MaxPointSize;
    DWORD               MaxPrimitiveCount;
    DWORD               MaxVertexIndex;
    DWORD               MaxStreams;
    DWORD               MaxStreamStride;
    DWORD               VertexShaderVersion;
    DWORD               MaxVertexShaderConst;
    DWORD               PixelShaderVersion;
    float               MaxPixelShaderValue;
};

struct D3DPRESENT_PARAMETERS {
    UINT                BackBufferWidth;
    UINT                BackBufferHeight;
    D3DFORMAT           BackBufferFormat;
    UINT                BackBufferCount;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    D3DSWAPEFFECT       SwapEffect;
    HWND                hDeviceWindow;
    BOOL                Windowed;
    BOOL                EnableAutoDepthStencil;
    D3DFORMAT           AutoDepthStencilFormat;
    DWORD               Flags;
    UINT                FullScreen_RefreshRateInHz;
    UINT                FullScreen_PresentationInterval;
};

struct D3DSURFACE_DESC {
    D3DFORMAT           Format;
    D3DRESOURCETYPE     Type;
    DWORD               Usage;
    D3DPOOL             Pool;
    UINT                Size;
    D3DMULTISAMPLE_TYPE MultiSampleType;
    UINT                Width;
    UINT                Height;
};

// Out-of-line definitions for the D3D compat interface methods that take
// D3DSURFACE_DESC (declared inline above where the struct is still opaque).
inline HRESULT IDirect3DSurface8::GetDesc(D3DSURFACE_DESC* pDesc)
{
    if (pDesc) { *pDesc = D3DSURFACE_DESC{}; }
    return S_OK;
}

inline HRESULT IDirect3DTexture8::GetSurfaceLevel(UINT /*level*/, IDirect3DSurface8** ppSurface)
{
    if (ppSurface) *ppSurface = nullptr;
    return S_OK;
}

inline HRESULT IDirect3DTexture8::GetLevelDesc(UINT /*level*/, D3DSURFACE_DESC* pDesc)
{
    if (pDesc) { *pDesc = D3DSURFACE_DESC{}; }
    return S_OK;
}

inline HRESULT IDirect3DTexture8::LockRect(UINT /*level*/, D3DLOCKED_RECT* pLockedRect, const RECT* /*pRect*/, DWORD /*flags*/)
{
    if (pLockedRect) { pLockedRect->Pitch = 0; pLockedRect->pBits = nullptr; }
    return S_OK;
}

inline HRESULT IDirect3DTexture8::UnlockRect(UINT /*level*/) { return S_OK; }

struct D3DVERTEXBUFFER_DESC {
    D3DFORMAT   Format;
    D3DRESOURCETYPE Type;
    DWORD       Usage;
    D3DPOOL     Pool;
    UINT        Size;
    DWORD       FVF;
};

struct D3DINDEXBUFFER_DESC {
    D3DFORMAT   Format;
    D3DRESOURCETYPE Type;
    DWORD       Usage;
    D3DPOOL     Pool;
    UINT        Size;
};

#endif // _D3D8_CAPS_STRUCTS_DEFINED

// ---------------------------------------------------------------------------
// Section 18 — D3DX stub types (for remaining WW3D2 internal includes)
// ---------------------------------------------------------------------------

// D3DX filter flags used by D3DXFilterTexture
#define D3DX_FILTER_NONE        0x00000001
#define D3DX_FILTER_POINT       0x00000002
#define D3DX_FILTER_LINEAR      0x00000003
#define D3DX_FILTER_TRIANGLE    0x00000004
#define D3DX_FILTER_BOX         0x00000005
#define D3DX_DEFAULT            0xFFFFFFFF
