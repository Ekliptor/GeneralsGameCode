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
#include "agg_def.h"
#include "part_ldr.h"
#include "wwstring.h"
#include "simplevec.h"
#include "AudibleSound.h"
#include "persistfactory.h"
#include "WWLib/registry.h"
#include "hashtemplate.h"
#include "string_compat.h"
#include <float.h>
#include <cstring>

// ============================================================================
// RenderObjClass
// ----------------------------------------------------------------------------
// Base class for every render object. rendobj.cpp is fully gated to DX8, so
// we provide the vtable/typeinfo and stubs for every out-of-line virtual.
// ============================================================================

const float RenderObjClass::AT_MIN_LOD = FLT_MAX;
const float RenderObjClass::AT_MAX_LOD = -1.0f;

RenderObjClass::RenderObjClass()
    : Bits(DEFAULT_BITS)
    , Transform(1)
    , ObjectScale(1.0f)
    , ObjectColor(0)
    , CachedBoundingSphere(Vector3(0,0,0), 1.0f)
    , CachedBoundingBox(Vector3(0,0,0), Vector3(1,1,1))
    , NativeScreenSize(0.0f)
    , IsTransformIdentity(false)
    , Scene(nullptr)
    , Container(nullptr)
    , User_Data(nullptr)
    , RenderHook(nullptr)
{}

RenderObjClass::RenderObjClass(const RenderObjClass & src)
    : RefCountClass(src)
    , PersistClass(src)
    , MultiListObjectClass(src)
    , Bits(src.Bits)
    , Transform(src.Transform)
    , ObjectScale(src.ObjectScale)
    , ObjectColor(src.ObjectColor)
    , CachedBoundingSphere(src.CachedBoundingSphere)
    , CachedBoundingBox(src.CachedBoundingBox)
    , NativeScreenSize(src.NativeScreenSize)
    , IsTransformIdentity(src.IsTransformIdentity)
    , Scene(nullptr)
    , Container(nullptr)
    , User_Data(nullptr)
    , RenderHook(nullptr)
{}

RenderObjClass & RenderObjClass::operator=(const RenderObjClass & that)
{
    if (this != &that) {
        Set_Hidden(that.Is_Hidden());
        Set_Animation_Hidden(that.Is_Animation_Hidden());
        Set_Force_Visible(that.Is_Force_Visible());
        Set_Collision_Type(that.Get_Collision_Type());
        Set_Native_Screen_Size(that.Get_Native_Screen_Size());
        IsTransformIdentity = false;
    }
    return *this;
}

void RenderObjClass::Add(SceneClass * /*scene*/) {}
bool RenderObjClass::Remove() { return false; }
SceneClass * RenderObjClass::Get_Scene() { return nullptr; }
void RenderObjClass::Set_Container(RenderObjClass * con) { Container = con; }
void RenderObjClass::Validate_Transform() const {}
void RenderObjClass::Set_Transform(const Matrix3D & m) { Transform = m; IsTransformIdentity = false; }
void RenderObjClass::Set_Position(const Vector3 & v) { Transform.Set_Translation(v); IsTransformIdentity = false; }
Vector3 RenderObjClass::Get_Position() const { return Transform.Get_Translation(); }

void RenderObjClass::Notify_Added(SceneClass * scene) { Scene = scene; }
void RenderObjClass::Notify_Removed(SceneClass * /*scene*/) { Scene = nullptr; }

RenderObjClass * RenderObjClass::Get_Sub_Object_By_Name(const char * /*name*/, int * /*index*/) const { return nullptr; }
int RenderObjClass::Add_Sub_Object_To_Bone(RenderObjClass * /*subobj*/, const char * /*bname*/) { return 0; }
int RenderObjClass::Remove_Sub_Objects_From_Bone(int /*boneindex*/) { return 0; }
int RenderObjClass::Remove_Sub_Objects_From_Bone(const char * /*bname*/) { return 0; }
void RenderObjClass::Update_Sub_Object_Transforms() {}
void RenderObjClass::Update_Sub_Object_Bits() {}

bool RenderObjClass::Intersect(IntersectionClass * /*ic*/, IntersectionResultClass * /*r*/) { return false; }
bool RenderObjClass::Intersect_Sphere(IntersectionClass * /*ic*/, IntersectionResultClass * /*r*/) { return false; }
bool RenderObjClass::Intersect_Sphere_Quick(IntersectionClass * /*ic*/, IntersectionResultClass * /*r*/) { return false; }

void RenderObjClass::Update_Cached_Bounding_Volumes() const
{
    CachedBoundingSphere.Center = Transform.Get_Translation();
    CachedBoundingSphere.Radius = 1.0f;
    CachedBoundingBox.Center = Transform.Get_Translation();
    CachedBoundingBox.Extent.Set(1,1,1);
    Bits |= BOUNDING_VOLUMES_VALID;
}

void RenderObjClass::Get_Obj_Space_Bounding_Sphere(SphereClass & sphere) const
{
    sphere.Center.Set(0,0,0);
    sphere.Radius = 1.0f;
}

void RenderObjClass::Get_Obj_Space_Bounding_Box(AABoxClass & box) const
{
    box.Center.Set(0,0,0);
    box.Extent.Set(1,1,1);
}

void RenderObjClass::Prepare_LOD(CameraClass & /*camera*/) {}
float RenderObjClass::Get_Cost() const { return 0.0f; }
int RenderObjClass::Calculate_Cost_Value_Arrays(float /*screen_area*/, float * /*values*/, float * /*costs*/) const { return 1; }
float RenderObjClass::Get_Screen_Size(CameraClass & /*camera*/) { return 0.0f; }

bool RenderObjClass::Build_Dependency_List(DynamicVectorClass<StringClass> & /*file_list*/, bool /*recursive*/) { return false; }
bool RenderObjClass::Build_Texture_List(DynamicVectorClass<StringClass> & /*file_list*/, bool /*recursive*/) { return false; }
void RenderObjClass::Add_Dependencies_To_List(DynamicVectorClass<StringClass> & /*file_list*/, bool /*textures_only*/) {}

// ----------------------------------------------------------------------------
// Stub persist factory shared by classes whose real factories live in
// DX8-gated translation units. Returning a reference to this static ensures
// Get_Factory() virtuals can be wired to *something* at link time; they are
// never actually called in the bgfx path because Save/Load also stub out.
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

const PersistFactoryClass & RenderObjClass::Get_Factory() const { return _StubFactory; }
bool RenderObjClass::Save(ChunkSaveClass & /*csave*/) { return true; }
bool RenderObjClass::Load(ChunkLoadClass & /*cload*/) { return true; }

// ============================================================================
// WW3DAssetManager
// ============================================================================

WW3DAssetManager::WW3DAssetManager()
    : PrototypeLoaders(PROTOLOADERS_VECTOR_SIZE)
    , Prototypes(PROTOTYPES_VECTOR_SIZE)
    , PrototypeHashTable(nullptr)
    , WW3D_Load_On_Demand(false)
    , Activate_Fog_On_Load(false)
    , MetalManager(nullptr)
{
    TheInstance = this;
}

WW3DAssetManager::~WW3DAssetManager()
{
    TheInstance = nullptr;
}

bool WW3DAssetManager::Load_3D_Assets(const char * /*filename*/) { return false; }
bool WW3DAssetManager::Load_3D_Assets(FileClass & /*assetfile*/) { return false; }
void WW3DAssetManager::Free_Assets() {}
void WW3DAssetManager::Release_Unused_Assets() {}
void WW3DAssetManager::Free_Assets_With_Exclusion_List(const DynamicVectorClass<StringClass> & /*list*/) {}
void WW3DAssetManager::Create_Asset_List(DynamicVectorClass<StringClass> & /*list*/) {}
RenderObjClass * WW3DAssetManager::Create_Render_Obj(const char * /*name*/) { return nullptr; }
bool WW3DAssetManager::Render_Obj_Exists(const char * /*name*/) { return false; }
RenderObjIterator * WW3DAssetManager::Create_Render_Obj_Iterator() { return nullptr; }
void WW3DAssetManager::Release_Render_Obj_Iterator(RenderObjIterator * /*it*/) {}
AssetIterator * WW3DAssetManager::Create_HAnim_Iterator() { return nullptr; }
HAnimClass * WW3DAssetManager::Get_HAnim(const char * /*name*/) { return nullptr; }

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
HTreeClass * WW3DAssetManager::Get_HTree(const char * /*name*/) { return nullptr; }
void WW3DAssetManager::Register_Prototype_Loader(PrototypeLoaderClass * /*loader*/) {}
void WW3DAssetManager::Add_Prototype(PrototypeClass * /*newproto*/) {}
PrototypeClass * WW3DAssetManager::Find_Prototype(const char * /*name*/) { return nullptr; }
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
// Motion/Bit channel classes
// ============================================================================

MotionChannelClass::MotionChannelClass()
    : PivotIdx(0), Type(0), VectorLen(0)
    , ValueOffset(0.0f), ValueScale(0.0f)
    , CompressedData(nullptr), Data(nullptr)
    , FirstFrame(0), LastFrame(0)
{}
MotionChannelClass::~MotionChannelClass() {}
bool MotionChannelClass::Load_W3D(ChunkLoadClass & /*cload*/) { return false; }

BitChannelClass::BitChannelClass()
    : PivotIdx(0), Type(0), DefaultVal(0)
    , FirstFrame(0), LastFrame(0), Bits(nullptr)
{}
BitChannelClass::~BitChannelClass() {}
bool BitChannelClass::Load_W3D(ChunkLoadClass & /*cload*/) { return false; }

TimeCodedMotionChannelClass::TimeCodedMotionChannelClass()
    : PivotIdx(0), Type(0), VectorLen(0), PacketSize(0)
    , NumTimeCodes(0), LastTimeCodeIdx(0), CachedIdx(0)
    , Data(nullptr)
{}
TimeCodedMotionChannelClass::~TimeCodedMotionChannelClass() {}
bool TimeCodedMotionChannelClass::Load_W3D(ChunkLoadClass & /*cload*/) { return false; }
void TimeCodedMotionChannelClass::Get_Vector(float32 /*frame*/, float * setvec)
{
    if (setvec) {
        for (int i = 0; i < VectorLen && i < 4; ++i) setvec[i] = 0.0f;
    }
}
Quaternion TimeCodedMotionChannelClass::Get_QuatVector(float32 /*frame*/)
{
    return Quaternion(true); // identity
}

AdaptiveDeltaMotionChannelClass::AdaptiveDeltaMotionChannelClass()
    : PivotIdx(0), Type(0), VectorLen(0)
    , NumFrames(0), Scale(0.0f)
    , Data(nullptr), CacheFrame(0), CacheData(nullptr)
{}
AdaptiveDeltaMotionChannelClass::~AdaptiveDeltaMotionChannelClass() {}
bool AdaptiveDeltaMotionChannelClass::Load_W3D(ChunkLoadClass & /*cload*/) { return false; }
void AdaptiveDeltaMotionChannelClass::Get_Vector(float32 /*frame*/, float * setvec)
{
    if (setvec) {
        for (int i = 0; i < VectorLen && i < 4; ++i) setvec[i] = 0.0f;
    }
}
Quaternion AdaptiveDeltaMotionChannelClass::Get_QuatVector(float32 /*frame*/)
{
    return Quaternion(true);
}

TimeCodedBitChannelClass::TimeCodedBitChannelClass()
    : PivotIdx(0), Type(0), DefaultVal(0)
    , NumTimeCodes(0), CachedIdx(0), Bits(nullptr)
{}
TimeCodedBitChannelClass::~TimeCodedBitChannelClass() {}
bool TimeCodedBitChannelClass::Load_W3D(ChunkLoadClass & /*cload*/) { return false; }
int TimeCodedBitChannelClass::Get_Bit(int /*frame*/) { return DefaultVal; }

// ============================================================================
// ParticleEmitterClass::Reset
// ============================================================================

void ParticleEmitterClass::Reset() {}

// ============================================================================
// AggregateLoader / ParticleEmitterLoader globals
// ============================================================================

AggregateLoaderClass _AggregateLoader;
PrototypeClass * AggregateLoaderClass::Load_W3D(ChunkLoadClass & /*chunk_load*/) { return nullptr; }

ParticleEmitterLoaderClass _ParticleEmitterLoader;
PrototypeClass * ParticleEmitterLoaderClass::Load_W3D(ChunkLoadClass & /*chunk_load*/) { return nullptr; }

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

TextureClass * Load_Texture(ChunkLoadClass & /*cload*/) { return nullptr; }

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
void DX8MeshRendererClass::Add_To_Render_List(DecalMeshClass * /*decalmesh*/) {}

DX8MeshRendererClass TheDX8MeshRenderer;

void DX8FVFCategoryContainer::Add_Visible_Material_Pass(MaterialPassClass * /*pass*/, MeshClass * /*mesh*/) {}
void DX8FVFCategoryContainer::Change_Polygon_Renderer_Texture(
    MultiListClass<DX8PolygonRendererClass> & /*list*/,
    TextureClass * /*texture*/, TextureClass * /*new_texture*/,
    unsigned /*pass*/, unsigned /*stage*/) {}
void DX8FVFCategoryContainer::Change_Polygon_Renderer_Material(
    MultiListClass<DX8PolygonRendererClass> & /*list*/,
    VertexMaterialClass * /*vmat*/, VertexMaterialClass * /*new_vmat*/,
    unsigned /*pass*/) {}

void DX8SkinFVFCategoryContainer::Add_Visible_Skin(MeshClass * /*mesh*/) {}

void DX8TextureCategoryClass::Add_Render_Task(DX8PolygonRendererClass * /*p_renderer*/, MeshClass * /*p_mesh*/) {}
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
