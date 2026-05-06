// ============================================================================
// ww3d2_bgfx_stubs.cpp
// ----------------------------------------------------------------------------
// Phase 5h — link-time stubs for WW3D2 classes whose .cpp files are gated to
// the Direct3D8 renderer (#ifdef RTS_RENDERER_DX8). The bgfx-mode build still
// references these symbols from headers/vtables in other files. Each stub
// here matches the declared signature exactly and performs no real work —
// the bgfx renderer path is not yet implemented for these subsystems.
// ============================================================================

#include "always.h"

#ifndef RTS_RENDERER_DX8

#include "rendobj.h"
#include "assetmgr.h"
#include "hlod.h"
#include "hmdldef.h"
#include "motchan.h"
#include "part_emt.h"
#include "render2dsentence.h"
#include "texture.h"
#include "textureloader.h"
#include "surfaceclass.h"
#include "dx8wrapper.h"
#include "dx8renderer.h"
#include "decalmsh.h"
#include "mesh.h"
#include "meshmdl.h"
#include "agg_def.h"
#include "part_ldr.h"
#include "wwstring.h"
#include "simplevec.h"
#include "AudibleSound.h"
#include "persistfactory.h"
#include "WWLib/registry.h"
#include "hashtemplate.h"
#include "string_compat.h"
// Phase D8 / D14 — un-stub the WW3DAssetManager loader pipeline so HLOD,
// mesh, and HTree prototypes load on demand. Pulls in all the prototype-
// loader globals defined across hlod.cpp / proto.cpp / boxrobj.cpp / etc.
#include "boxrobj.h"
#include "chunkio.h"
#include "collect.h"
#include "dazzle.h"
#include "distlod.h"
#include "ffactory.h"
#include "nullrobj.h"
#include "proto.h"
#include "realcrc.h"
#include "ringobj.h"
#include "sphereobj.h"
#include "w3d_file.h"
#include "wwdebug.h"
#include "wwmemlog.h"
#include "wwprofile.h"
#include "assetstatus.h"
#include <float.h>
#include <cstdio>
#include <cstring>

// ============================================================================
// RenderObjClass — Phase 0 (BGFX scene wireup): the real device-independent
// bodies now live in Core/.../rendobj.cpp (un-gated). The previous no-op
// stubs silently dropped every render object the gameplay code added to a
// scene; removed.
// ============================================================================

// ----------------------------------------------------------------------------
// Stub persist factory shared by AudibleSoundDefinitionClass below
// (WWAudio's real .cpp is Windows+Miles-only and never compiles on macOS).
// ----------------------------------------------------------------------------
namespace {
class StubPersistFactoryClass : public PersistFactoryClass
{
public:
    uint32 Chunk_ID() const override { return 0; }
    PersistClass * Load(ChunkLoadClass & /*cload*/) const override { return nullptr; }
    void Save(ChunkSaveClass & /*csave*/, PersistClass * /*obj*/) const override {}
};
StubPersistFactoryClass _StubFactory;
}

// ============================================================================
// WW3DAssetManager
// ----------------------------------------------------------------------------
// Phase D8 — model loader pipeline un-stubbed. Mirrors the DX8 path in
// assetmgr.cpp:210-245 / 625-836 / 1522-1694: register all the built-in
// prototype loaders, then resolve `Create_Render_Obj` by walking the
// prototype hash table and falling back to load-on-demand .w3d reads.
// Phase D14 — extended to dispatch W3D_CHUNK_HIERARCHY to HTreeManager
// and W3D_CHUNK_ANIMATION* to HAnimManager so skinned units load real
// skeletons instead of falling back to HTreeClass::Init_Default (1 pivot,
// causes infantry to render as flat polygons — see D13c.2/.3 findings).
// ============================================================================

namespace { NullPrototypeClass _NullPrototype; }

WW3DAssetManager::WW3DAssetManager()
    : PrototypeLoaders(PROTOLOADERS_VECTOR_SIZE)
    , Prototypes(PROTOTYPES_VECTOR_SIZE)
    , PrototypeHashTable(nullptr)
    , WW3D_Load_On_Demand(false)
    , Activate_Fog_On_Load(false)
    , MetalManager(nullptr)
{
    TheInstance = this;

    PrototypeLoaders.Set_Growth_Step(PROTOLOADERS_GROWTH_RATE);
    Prototypes.Set_Growth_Step(PROTOTYPES_GROWTH_RATE);

    Register_Prototype_Loader(&_MeshLoader);
    Register_Prototype_Loader(&_HModelLoader);
    Register_Prototype_Loader(&_CollectionLoader);
    Register_Prototype_Loader(&_BoxLoader);
    Register_Prototype_Loader(&_HLodLoader);
    Register_Prototype_Loader(&_DistLODLoader);
    Register_Prototype_Loader(&_AggregateLoader);
    Register_Prototype_Loader(&_NullLoader);
    Register_Prototype_Loader(&_DazzleLoader);
    Register_Prototype_Loader(&_RingLoader);
    Register_Prototype_Loader(&_SphereLoader);

    PrototypeHashTable = W3DNEWARRAY PrototypeClass *[PROTOTYPE_HASH_TABLE_SIZE];
    memset(PrototypeHashTable, 0, sizeof(PrototypeClass *) * PROTOTYPE_HASH_TABLE_SIZE);
}

WW3DAssetManager::~WW3DAssetManager()
{
    Free_Assets();
    delete[] PrototypeHashTable;
    PrototypeHashTable = nullptr;
    TheInstance = nullptr;
}

bool WW3DAssetManager::Load_3D_Assets(const char * filename)
{
    bool result = false;
    FileClass * file = _TheFileFactory->Get_File(filename);
    if (file) {
        if (file->Is_Available()) {
            result = WW3DAssetManager::Load_3D_Assets(*file);
        } else {
            WWDEBUG_SAY(("Missing asset '%s'.", filename));
        }
        _TheFileFactory->Return_File(file);
    }
    return result;
}

bool WW3DAssetManager::Load_3D_Assets(FileClass & w3dfile)
{
    WWPROFILE("WW3DAssetManager::Load_3D_Assets");
    if (!w3dfile.Open()) {
        return false;
    }
    ChunkLoadClass cload(&w3dfile);
    while (cload.Open_Chunk()) {
        switch (cload.Cur_Chunk_ID()) {
            case W3D_CHUNK_HIERARCHY:
                HTreeManager.Load_Tree(cload);
                break;

            case W3D_CHUNK_ANIMATION:
            case W3D_CHUNK_COMPRESSED_ANIMATION:
            case W3D_CHUNK_MORPH_ANIMATION:
                HAnimManager.Load_Anim(cload);
                break;

            default:
                Load_Prototype(cload);
                break;
        }
        cload.Close_Chunk();
    }
    w3dfile.Close();
    return true;
}

bool WW3DAssetManager::Load_Prototype(ChunkLoadClass & cload)
{
    WWPROFILE("WW3DAssetManager::Load_Prototype");
    WWMEMLOG(MEM_GEOMETRY);
    int chunk_id = cload.Cur_Chunk_ID();
    PrototypeLoaderClass * loader = Find_Prototype_Loader(chunk_id);
    PrototypeClass * newproto = nullptr;
    if (loader != nullptr) {
        newproto = loader->Load_W3D(cload);
    } else {
        WWDEBUG_SAY(("Unknown chunk type encountered!  Chunk Id = %d", chunk_id));
        return false;
    }
    if (newproto != nullptr) {
        if (!Render_Obj_Exists(newproto->Get_Name())) {
            Add_Prototype(newproto);
        } else {
            WWDEBUG_SAY(("Render Object Name Collision: %s", newproto->Get_Name()));
            newproto->DeleteSelf();
            return false;
        }
    } else {
        WWDEBUG_SAY(("Could not generate prototype!  Chunk = %d", chunk_id));
        return false;
    }
    return true;
}

void WW3DAssetManager::Free_Assets()
{
    WWPROFILE("WW3DAssetManager::Free_Assets");

    int count = Prototypes.Count();
    while (count-- > 0) {
        PrototypeClass * proto = Prototypes[count];
        Prototypes.Delete(count);
        if (proto != nullptr) {
            proto->DeleteSelf();
        }
    }
    if (PrototypeHashTable) {
        memset(PrototypeHashTable, 0, sizeof(PrototypeClass *) * PROTOTYPE_HASH_TABLE_SIZE);
    }

    HAnimManager.Free_All_Anims();
    HTreeManager.Free_All_Trees();

    Release_All_Textures();
    Release_All_Font3DDatas();
    Release_All_FontChars();
}
void WW3DAssetManager::Release_Unused_Assets() {}
void WW3DAssetManager::Free_Assets_With_Exclusion_List(const DynamicVectorClass<StringClass> & /*list*/) {}
void WW3DAssetManager::Create_Asset_List(DynamicVectorClass<StringClass> & /*list*/) {}

RenderObjClass * WW3DAssetManager::Create_Render_Obj(const char * name)
{
    WWPROFILE("WW3DAssetManager::Create_Render_Obj");
    WWMEMLOG(MEM_GEOMETRY);

    PrototypeClass * proto = Find_Prototype(name);

    if (WW3D_Load_On_Demand && proto == nullptr) {
        AssetStatusClass::Peek_Instance()->Report_Load_On_Demand_RObj(name);
        char filename[MAX_PATH];
        const char * mesh_name = ::strchr(name, '.');
        if (mesh_name != nullptr) {
            // 64-bit safe pointer arithmetic — DX8 path's (int) casts truncate on macOS.
            const size_t prefix_len = (size_t)((uintptr_t)mesh_name - (uintptr_t)name);
            const size_t copy_len = prefix_len < (sizeof(filename) - 1) ? prefix_len : (sizeof(filename) - 1);
            memcpy(filename, name, copy_len);
            filename[copy_len] = '\0';
            strncat(filename, ".w3d", sizeof(filename) - copy_len - 1);
        } else {
            snprintf(filename, ARRAY_SIZE(filename), "%s.w3d", name);
        }
        if (Load_3D_Assets(filename) == false) {
            StringClass new_filename(StringClass("..\\"), true);
            new_filename += filename;
            Load_3D_Assets(new_filename);
        }
        proto = Find_Prototype(name);
    }

    if (proto == nullptr) {
        static int warning_count = 0;
        if (name[0] != '#') {
            if (++warning_count <= 20) {
                WWDEBUG_SAY(("WARNING: Failed to create Render Object: %s", name));
            }
            AssetStatusClass::Peek_Instance()->Report_Missing_RObj(name);
        }
        return nullptr;
    }
    return proto->Create();
}

bool WW3DAssetManager::Render_Obj_Exists(const char * name)
{
    return Find_Prototype(name) != nullptr;
}

RenderObjIterator * WW3DAssetManager::Create_Render_Obj_Iterator() { return nullptr; }
void WW3DAssetManager::Release_Render_Obj_Iterator(RenderObjIterator * /*it*/) {}
AssetIterator * WW3DAssetManager::Create_HAnim_Iterator() { return nullptr; }

// Phase D14 — mirror assetmgr.cpp:965-1003. Resolve HAnims by name; fall
// back to load-on-demand .w3d reads. The animation name format is
// "<htree>.<anim>" — the .w3d filename is everything after the dot.
HAnimClass * WW3DAssetManager::Get_HAnim(const char * name)
{
    WWPROFILE("WW3DAssetManager::Get_HAnim");
    HAnimClass * anim = HAnimManager.Get_Anim(name);

    if (WW3D_Load_On_Demand && anim == nullptr) {
        if (!HAnimManager.Is_Missing(name)) {
            AssetStatusClass::Peek_Instance()->Report_Load_On_Demand_HAnim(name);
            char filename[MAX_PATH];
            const char * animname = strchr(name, '.');
            if (animname != nullptr) {
                snprintf(filename, ARRAY_SIZE(filename), "%s.w3d", animname + 1);
            } else {
                WWDEBUG_SAY(("Animation %s has no . in the name", name));
                return nullptr;
            }
            if (Load_3D_Assets(filename) == false) {
                StringClass new_filename = StringClass("..\\") + filename;
                Load_3D_Assets(new_filename);
            }
            anim = HAnimManager.Get_Anim(name);
            if (anim == nullptr) {
                HAnimManager.Register_Missing(name);
                AssetStatusClass::Peek_Instance()->Report_Missing_HAnim(name);
            }
        }
    }
    return anim;
}

// ---------------------------------------------------------------------------
// Texture hash-cache implementations mirror the real DX8 asset manager
// (Generals/Code/Libraries/Source/WWVegas/WW3D2/assetmgr.cpp). The code is
// device-independent: TextureClass::Init() already has a real bgfx path via
// BgfxTextureCache::Get_Or_Load_File, so constructing a TextureClass here is
// safe even though the DX8 renderer isn't linked.
TextureClass * WW3DAssetManager::Get_Texture(
    const char * filename,
    MipCountType mip_level_count,
    WW3DFormat texture_format,
    bool allow_compression,
    TextureBaseClass::TexAssetType type,
    bool allow_reduction)
{
    if (texture_format == WW3D_FORMAT_U8V8) {
        mip_level_count = MIP_LEVELS_1;
    }
    if (filename == nullptr || *filename == '\0') {
        return nullptr;
    }

    StringClass lower_case_name(filename, true);
    _strlwr(lower_case_name.Peek_Buffer());

    TextureClass * tex = TextureHash.Get(lower_case_name);
    if (!tex) {
        if (type == TextureBaseClass::TEX_REGULAR) {
            tex = NEW_REF(TextureClass, (lower_case_name, nullptr, mip_level_count, texture_format, allow_compression, allow_reduction));
        } else {
            // CubeTextureClass/VolumeTextureClass ctors are DX8-only today.
            // ToDo Phase 5h+: provide bgfx cube/volume texture ctors.
            return nullptr;
        }
        TextureHash.Insert(tex->Get_Texture_Name(), tex);
    }

    tex->Add_Ref();
    return tex;
}

void WW3DAssetManager::Release_All_Textures()
{
    HashTemplateIterator<StringClass, TextureClass*> ite(TextureHash);
    for (ite.First(); !ite.Is_Done(); ite.Next()) {
        TextureClass * tex = ite.Peek_Value();
        tex->Release_Ref();
    }
    TextureHash.Remove_All();
}

void WW3DAssetManager::Release_Unused_Textures()
{
    unsigned count = 0;
    TextureClass * temp_textures[256];

    HashTemplateIterator<StringClass, TextureClass*> ite(TextureHash);
    for (ite.First(); !ite.Is_Done(); ite.Next()) {
        TextureClass * tex = ite.Peek_Value();
        if (tex->Num_Refs() == 1) {
            temp_textures[count++] = tex;
            if (count == 256) {
                for (unsigned i = 0; i < 256; ++i) {
                    TextureHash.Remove(temp_textures[i]->Get_Texture_Name());
                    temp_textures[i]->Release_Ref();
                }
                count = 0;
                ite.First();
            }
        }
    }
    for (unsigned i = 0; i < count; ++i) {
        TextureHash.Remove(temp_textures[i]->Get_Texture_Name());
        temp_textures[i]->Release_Ref();
    }
}

void WW3DAssetManager::Release_Texture(TextureClass * tex)
{
    if (tex == nullptr) {
        return;
    }
    TextureHash.Remove(tex->Get_Texture_Name());
    tex->Release_Ref();
}

void WW3DAssetManager::Load_Procedural_Textures() {}
// Font3DInstanceClass / Font3DDataClass: legacy DX8-bound 3D billboard text
// (Render2DTextClass + TextDrawClass). Audited 2026-05-06 — no shipping
// game-code caller in Generals/GeneralsMD/GameEngine[Device]; UI text uses
// the separate FontCharsClass + DisplayString pipeline. Deferred until a
// real consumer surfaces.
Font3DInstanceClass * WW3DAssetManager::Get_Font3DInstance(const char * /*name*/) { return nullptr; }

// Real FontChars resolution for the BGFX build. Mirrors the DX8 path in
// assetmgr.cpp: reuse an existing matching entry, otherwise construct and
// initialize a new FontCharsClass (which loads a TTF + allocates a glyph
// atlas in render2dsentence_bgfx.cpp). Needed so button labels render —
// see docs/ZH-MainMenu-Bugs.md §3.3.
FontCharsClass * WW3DAssetManager::Get_FontChars(const char * name, int point_size, bool is_bold)
{
    for (int i = 0; i < FontCharsList.Count(); ++i) {
        if (FontCharsList[i]->Is_Font(name, point_size, is_bold)) {
            FontCharsList[i]->Add_Ref();
            return FontCharsList[i];
        }
    }
    FontCharsClass * font = NEW_REF(FontCharsClass, ());
    if (font->Initialize_GDI_Font(name, point_size, is_bold)) {
        font->Add_Ref();
        FontCharsList.Add(font);
        return font;
    }
    font->Release_Ref();
    return nullptr;
}

AssetIterator * WW3DAssetManager::Create_HTree_Iterator() { return nullptr; }

// Phase D14 — mirror assetmgr.cpp:1019-1048. The previous nullptr stub
// caused Animatable3DObjClass to fall back to HTreeClass::Init_Default
// (NumPivots=1), which made every CPU-skinned mesh read OOB bone matrices
// and collapse to garbage flat polygons (D13c.2 root cause).
HTreeClass * WW3DAssetManager::Get_HTree(const char * name)
{
    WWPROFILE("WW3DAssetManager::Get_HTree");
    HTreeClass * htree = HTreeManager.Get_Tree(name);

    if (WW3D_Load_On_Demand && htree == nullptr) {
        AssetStatusClass::Peek_Instance()->Report_Load_On_Demand_HTree(name);
        char filename[MAX_PATH];
        snprintf(filename, ARRAY_SIZE(filename), "%s.w3d", name);
        if (Load_3D_Assets(filename) == false) {
            StringClass new_filename("..\\", true);
            new_filename += filename;
            Load_3D_Assets(new_filename);
        }
        htree = HTreeManager.Get_Tree(name);
        if (htree == nullptr) {
            AssetStatusClass::Peek_Instance()->Report_Missing_HTree(name);
        }
    }
    return htree;
}

void WW3DAssetManager::Register_Prototype_Loader(PrototypeLoaderClass * loader)
{
    WWASSERT(loader != nullptr);
    PrototypeLoaders.Add(loader);
}

PrototypeLoaderClass * WW3DAssetManager::Find_Prototype_Loader(int chunk_id)
{
    for (int i = 0; i < PrototypeLoaders.Count(); ++i) {
        PrototypeLoaderClass * loader = PrototypeLoaders[i];
        if (loader && loader->Chunk_Type() == chunk_id) {
            return loader;
        }
    }
    return nullptr;
}

void WW3DAssetManager::Add_Prototype(PrototypeClass * newproto)
{
    WWASSERT(newproto != nullptr);
    int hash = CRC_Stringi(newproto->Get_Name()) & PROTOTYPE_HASH_MASK;
    newproto->friend_setNextHash(PrototypeHashTable[hash]);
    PrototypeHashTable[hash] = newproto;
    Prototypes.Add(newproto);
}

PrototypeClass * WW3DAssetManager::Find_Prototype(const char * name)
{
    if (stricmp(name, "NULL") == 0) {
        return &_NullPrototype;
    }
    int hash = CRC_Stringi(name) & PROTOTYPE_HASH_MASK;
    PrototypeClass * test = PrototypeHashTable[hash];
    while (test != nullptr) {
        if (stricmp(test->Get_Name(), name) == 0) {
            return test;
        }
        test = test->friend_getNextHash();
    }
    return nullptr;
}
AssetIterator * WW3DAssetManager::Create_Font3DData_Iterator() { return nullptr; }
void WW3DAssetManager::Add_Font3DData(Font3DDataClass * /*font*/) {}
void WW3DAssetManager::Remove_Font3DData(Font3DDataClass * /*font*/) {}
Font3DDataClass * WW3DAssetManager::Get_Font3DData(const char * /*name*/) { return nullptr; }
void WW3DAssetManager::Release_All_Font3DDatas() {}
void WW3DAssetManager::Release_Unused_Font3DDatas() {}
void WW3DAssetManager::Release_All_FontChars()
{
    while (FontCharsList.Count()) {
        FontCharsList[0]->Release_Ref();
        FontCharsList.Delete(0);
    }
}

// ============================================================================
// HLodClass constructors now come from the real hlod.cpp (gate lifted).
// ============================================================================

// ============================================================================
// Motion/Bit channel classes — Phase D15 un-stubbed.
// The real implementations are in motchan.cpp (un-gated, fully BGFX-clean).
// Previously the duplicate stubs here were winning the static-archive symbol
// race and returning false from Load_W3D, which made HAnims load but stay
// channel-less so units rendered in bind pose. Removing these stubs lets
// the real chunk-walking loaders link in. Constructors come from motchan.cpp.
// ============================================================================

// ============================================================================
// Phase D16 — particle emitter loader / instance bodies live in
// part_ldr.cpp + part_emt.cpp now (gates lifted on the MD side, vanilla
// Generals was already ungated). The duplicate stubs here were returning
// nullptr from Load_W3D so .w3d-defined particle emitters never loaded.
// ============================================================================

// AggregateLoader still stubbed — it composes multiple render objects
// per .w3d (used for landmark / multi-mesh assets); not on the menu path.
AggregateLoaderClass _AggregateLoader;
PrototypeClass * AggregateLoaderClass::Load_W3D(ChunkLoadClass & /*chunk_load*/) { return nullptr; }

// FontCharsClass + Render2DSentenceClass implementations live in
// render2dsentence_bgfx.cpp (stb_truetype glyph rasterizer + atlas).

// ============================================================================
// TextureClass / TextureLoader / surfaceclass helpers
// ============================================================================

SurfaceClass * TextureClass::Get_Surface_Level(unsigned int /*level*/)
{
    // Allocate a CPU-backed SurfaceClass sized to match the texture and bind
    // it to the bgfx handle so Unlock uploads the written pixels back. The
    // caller owns a ref and releases when done; the surface's CPU buffer is
    // destroyed with it.
    // ToDo: honor `level` for mip levels > 0 (bgfx currently generates mips
    // internally). Startup-path callers only ask for level 0.
    const unsigned w = static_cast<unsigned>(Get_Width()  > 0 ? Get_Width()  : 1);
    const unsigned h = static_cast<unsigned>(Get_Height() > 0 ? Get_Height() : 1);
    const WW3DFormat fmt = TextureFormat != WW3D_FORMAT_UNKNOWN
                             ? TextureFormat
                             : WW3D_FORMAT_A8R8G8B8;
    SurfaceClass * surface = NEW_REF(SurfaceClass, (w, h, fmt));
    surface->Set_Associated_Texture(Peek_Bgfx_Handle());
    return surface;
}
void TextureClass::Get_Level_Description(SurfaceClass::SurfaceDescription & desc, unsigned int /*level*/)
{
    desc.Width  = static_cast<unsigned>(Get_Width()  > 0 ? Get_Width()  : 0);
    desc.Height = static_cast<unsigned>(Get_Height() > 0 ? Get_Height() : 0);
    desc.Format = TextureFormat;
}

void TextureLoader::Validate_Texture_Size(unsigned & /*width*/, unsigned & /*height*/, unsigned & /*depth*/) {}

// Load_Texture: BGFX implementation. Core/.../texture.cpp's Load_Texture
// is gated to RTS_RENDERER_DX8, and the prior stub returned nullptr,
// which left every mesh's Textures vector empty → crash inside
// Peek_Texture during read_texture_ids. Reads the W3D_CHUNK_TEXTURE
// sub-chunks (name + optional texinfo) and routes the name through
// WW3DAssetManager::Get_Texture (the same path terrain textures use).
TextureClass * Load_Texture(ChunkLoadClass & cload)
{
    if (!cload.Open_Chunk() || cload.Cur_Chunk_ID() != W3D_CHUNK_TEXTURE) {
        return nullptr;
    }

    char name[256] = {0};
    W3dTextureInfoStruct texinfo;
    bool hastexinfo = false;

    while (cload.Open_Chunk()) {
        switch (cload.Cur_Chunk_ID()) {
            case W3D_CHUNK_TEXTURE_NAME:
                cload.Read(name, cload.Cur_Chunk_Length());
                break;
            case W3D_CHUNK_TEXTURE_INFO:
                cload.Read(&texinfo, sizeof(W3dTextureInfoStruct));
                hastexinfo = true;
                break;
        }
        cload.Close_Chunk();
    }
    cload.Close_Chunk();

    TextureClass * newtex = nullptr;
    if (hastexinfo) {
        MipCountType mipcount = MIP_LEVELS_ALL;
        bool no_lod = ((texinfo.Attributes & W3DTEXTURE_NO_LOD) == W3DTEXTURE_NO_LOD);
        if (no_lod) {
            mipcount = MIP_LEVELS_1;
        } else {
            switch (texinfo.Attributes & W3DTEXTURE_MIP_LEVELS_MASK) {
                case W3DTEXTURE_MIP_LEVELS_ALL: mipcount = MIP_LEVELS_ALL; break;
                case W3DTEXTURE_MIP_LEVELS_2:   mipcount = MIP_LEVELS_2;   break;
                case W3DTEXTURE_MIP_LEVELS_3:   mipcount = MIP_LEVELS_3;   break;
                case W3DTEXTURE_MIP_LEVELS_4:   mipcount = MIP_LEVELS_4;   break;
                default:                        mipcount = MIP_LEVELS_ALL; break;
            }
        }
        newtex = WW3DAssetManager::Get_Instance()->Get_Texture(name, mipcount);
        if (newtex) {
            if (no_lod) {
                newtex->Get_Filter().Set_Mip_Mapping(TextureFilterClass::FILTER_TYPE_NONE);
            }
            const bool u_clamp = ((texinfo.Attributes & W3DTEXTURE_CLAMP_U) != 0);
            newtex->Get_Filter().Set_U_Addr_Mode(
                u_clamp ? TextureFilterClass::TEXTURE_ADDRESS_CLAMP
                        : TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
            const bool v_clamp = ((texinfo.Attributes & W3DTEXTURE_CLAMP_V) != 0);
            newtex->Get_Filter().Set_V_Addr_Mode(
                v_clamp ? TextureFilterClass::TEXTURE_ADDRESS_CLAMP
                        : TextureFilterClass::TEXTURE_ADDRESS_REPEAT);
        }
    } else {
        newtex = WW3DAssetManager::Get_Instance()->Get_Texture(name);
    }

    return newtex;
}

void Convert_Pixel(unsigned char * /*pixel*/, const SurfaceClass::SurfaceDescription & /*sd*/, const Vector3 & /*rgb*/) {}

// ============================================================================
// DX8Wrapper::Set_World_Identity / Set_View_Identity
// — real implementations live in Core/.../dx8wrapper.cpp (bgfx branch) now
// ============================================================================

// ============================================================================
// DX8MeshRenderer family + TheDX8MeshRenderer global
// ============================================================================

DX8MeshRendererClass::DX8MeshRendererClass()
    : enable_lighting(false)
    , camera(nullptr)
    , texture_category_container_list_skin(nullptr)
    , visible_decal_meshes(nullptr)
{}

DX8MeshRendererClass::~DX8MeshRendererClass() {}

void DX8MeshRendererClass::Flush() {}
void DX8MeshRendererClass::Clear_Pending_Delete_Lists() {}
void DX8MeshRendererClass::Invalidate(bool /*shutdown*/) {}
void DX8MeshRendererClass::Register_Mesh_Type(MeshModelClass * /*mmc*/) {}
void DX8MeshRendererClass::Unregister_Mesh_Type(MeshModelClass * /*mmc*/) {}
void DX8MeshRendererClass::Add_To_Render_List(DecalMeshClass * decalmesh)
{
	if (decalmesh == nullptr) return;
	static const DecalMeshClass* seen[64] = {};
	static int seenCount = 0;
	for (int i = 0; i < seenCount; ++i) {
		if (seen[i] == decalmesh) return;
	}
	if (seenCount >= 64) return;
	seen[seenCount++] = decalmesh;
	MeshClass* parent = decalmesh->Peek_Parent();
	const char* parentName = (parent && parent->Peek_Model()) ? parent->Peek_Model()->Get_Name() : "<noparent>";
	std::fprintf(stderr,
		"[PhaseD13a:stub-decal] unique=%d ptr=%p parent=%s\n",
		seenCount, (void*)decalmesh, parentName);
}

DX8MeshRendererClass TheDX8MeshRenderer;

void DX8FVFCategoryContainer::Add_Visible_Material_Pass(MaterialPassClass * /*pass*/, MeshClass * mesh)
{
	static bool s_warned = false;
	if (s_warned) return;
	s_warned = true;
	const char* name = (mesh && mesh->Peek_Model()) ? mesh->Peek_Model()->Get_Name() : "<unknown>";
	std::fprintf(stderr, "[PhaseD13a:stub-matpass] firstFire mesh=%s\n", name);
}
void DX8FVFCategoryContainer::Change_Polygon_Renderer_Texture(
    MultiListClass<DX8PolygonRendererClass> & /*list*/,
    TextureClass * /*texture*/, TextureClass * /*new_texture*/,
    unsigned /*pass*/, unsigned /*stage*/) {}
void DX8FVFCategoryContainer::Change_Polygon_Renderer_Material(
    MultiListClass<DX8PolygonRendererClass> & /*list*/,
    VertexMaterialClass * /*vmat*/, VertexMaterialClass * /*new_vmat*/,
    unsigned /*pass*/) {}

void DX8SkinFVFCategoryContainer::Add_Visible_Skin(MeshClass * mesh)
{
	static bool s_warned = false;
	if (s_warned) return;
	s_warned = true;
	const char* name = (mesh && mesh->Peek_Model()) ? mesh->Peek_Model()->Get_Name() : "<unknown>";
	std::fprintf(stderr, "[PhaseD13a:stub-skin] firstFire mesh=%s\n", name);
}

void DX8TextureCategoryClass::Add_Render_Task(DX8PolygonRendererClass * /*p_renderer*/, MeshClass * p_mesh)
{
	static bool s_warned = false;
	if (s_warned) return;
	s_warned = true;
	const char* name = (p_mesh && p_mesh->Peek_Model()) ? p_mesh->Peek_Model()->Get_Name() : "<unknown>";
	std::fprintf(stderr, "[PhaseD13a:stub-rendertask] firstFire mesh=%s\n", name);
}
void DX8TextureCategoryClass::Remove_Polygon_Renderer(DX8PolygonRendererClass * /*p_renderer*/) {}

// ============================================================================
// AudibleSoundDefinitionClass
// ----------------------------------------------------------------------------
// WWAudio is Windows+Miles-only; its .cpp never compiles on macOS. soundrobj.cpp
// in Core/WW3D2 still references this class, so we stub the virtuals enough
// to emit the vtable.
// ============================================================================

AudibleSoundDefinitionClass::AudibleSoundDefinitionClass()
    : m_Priority(0.5f)
    , m_Volume(1.0f)
    , m_Pan(0.0f)
    , m_LoopCount(1)
    , m_DropOffRadius(600.0f)
    , m_MaxVolRadius(100.0f)
    , m_Is3D(false)
    , m_Type(0)
    , m_StartOffset(0.0f)
    , m_PitchFactor(1.0f)
    , m_LogicalTypeMask(0)
    , m_LogicalNotifyDelay(0.0f)
    , m_LogicalDropOffRadius(0.0f)
    , m_CreateLogical(false)
    , m_AttenuationSphereColor(1.0f, 1.0f, 1.0f)
{}

uint32 AudibleSoundDefinitionClass::Get_Class_ID() const { return 0; }
const PersistFactoryClass & AudibleSoundDefinitionClass::Get_Factory() const { return _StubFactory; }
bool AudibleSoundDefinitionClass::Save(ChunkSaveClass & /*csave*/) { return false; }
bool AudibleSoundDefinitionClass::Load(ChunkLoadClass & /*cload*/) { return false; }
PersistClass * AudibleSoundDefinitionClass::Create() const { return nullptr; }
AudibleSoundClass * AudibleSoundDefinitionClass::Create_Sound(int /*classid_hint*/) const { return nullptr; }
void AudibleSoundDefinitionClass::Initialize_From_Sound(AudibleSoundClass * /*sound*/) {}
LogicalSoundClass * AudibleSoundDefinitionClass::Create_Logical() { return nullptr; }

// ============================================================================
// RegistryClass — WWLib registry.cpp is Windows-only. A game-startup read of
// the language key is the only caller we care about at this stage.
// ============================================================================

RegistryClass::RegistryClass(const char * /*sub_key*/, bool /*create*/) {}
RegistryClass::~RegistryClass() {}
int RegistryClass::Get_Int(const char * /*name*/, int def_value) { return def_value; }

#endif // !RTS_RENDERER_DX8
