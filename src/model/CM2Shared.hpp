#ifndef MODEL_C_M2_SHARED_HPP
#define MODEL_C_M2_SHARED_HPP

#include "gx/Texture.hpp"
#include "model/CM2SequenceLoad.hpp"
#include <cstdint>
#include <storm/String.hpp>
#include <tempest/Box.hpp>

class CAsyncObject;
class CGxBuf;
class CGxPool;
class CM2Cache;
class CM2Model;
class CShaderEffect;
struct M2Batch;
struct M2Data;
struct M2SkinProfile;
struct M2SkinSection;
class SFile;

// The 16-byte-aligning allocator the reference uses for the .anim buffer and for every matrix
// CM2Model keeps outside its pooled block. Alloc returns an INTERIOR pointer with the pad size
// in the byte before it, so only SequenceBufferFree can free one -- never SMemFree.
// ref: FUN_0083de50
void* SequenceBufferAlloc(uint32_t size, const char* file, int32_t line);
// ref: FUN_0083de90
void SequenceBufferFree(void* buffer);

class CM2Shared {
    public:
        // Static functions
        static void LoadCanceledCallback(CAsyncObject* object);
        static void LoadFailedCallback(void* param);
        static void LoadSucceededCallback(void* param);
        static void SkinProfileLoadedCallback(void* param);
        static void SequenceLoadedCallback(void* param);
        static void SequenceLoadFailedCallback(void* param);

        // External (.anim) sequences: M2 sequences without flag 0x20 keep their keyframes in
        // "<model>%04d-%02d.anim", read asynchronously on first use. The load record parks the
        // bone-sequence requests that arrive meanwhile (CM2Model::SetBoneSequenceDeferred).
        CM2SequenceLoad* LoadSequence(uint16_t sequenceIndex);
        int32_t InitSequence(uint16_t sequenceIndex, CAsyncObject* object);
        void DestroySequenceLoad(CM2SequenceLoad* load);

        // ref: FUN_0083d510
        // Retire every .anim read this shared data still has outstanding, then drop the records.
        // Called first thing by ~CM2Shared. Two things here that a plain drain does not do: an
        // in-flight read is CANCELLED before its record is freed, and when a cancel cannot be
        // honoured the buffer slot it would have filled is cleared rather than left holding a
        // pointer to a record that is about to go; and if this is reached from inside the
        // sequence-loaded callback (m_flag10) the drain is deferred to that callback via m_flag20
        // instead of freeing the record it is standing on.
        void CancelSequenceLoads();

        // The two halves of the specialized-shader pass's texture-combo handling. PackTextureCombos
        // rewrites each batch's combo fields into a packed intermediate form and FixUpTextureCombos
        // turns them back into indices; SubstituteSpecializedShaders runs the analysis between them.
        // Neither is meaningful without the other -- see the notes at their definitions.
        // THE COMBO ARRAYS EVERY BATCH INDEXES, and the one place to ask for them.
        //
        // The reference's specialized-shader pass REPLACES m_data->textureCombos and
        // textureTransformCombos with rebuilt arrays holding every pair its merged batches need,
        // writing raw pointers into the M2Arrays at +0x84 and +0x9c. frozen cannot do that: its
        // M2Array holds a signed 32-bit delta from its own address rather than a pointer (see
        // M2Data.hpp, and it is what makes the 64-bit build work), and a fresh allocation is not
        // guaranteed to land within reach of the model data.
        //
        // So the rebuilt arrays live HERE instead, and everything that indexes a batch's combo
        // fields asks through these two accessors. While the pass has not run they are null and the
        // accessors hand back the model data's own arrays, which is what every reader used to do
        // directly -- so this is behaviour-preserving until something fills them in.
        uint16_t* m_rebuiltTextureCombos = nullptr;
        uint32_t m_rebuiltTextureComboCount = 0;
        uint16_t* m_rebuiltTextureTransformCombos = nullptr;
        uint32_t m_rebuiltTextureTransformComboCount = 0;

        uint16_t* TextureCombos(uint32_t* count);
        uint16_t* TextureTransformCombos(uint32_t* count);

        // Rebuild the two combo arrays so they hold every entry the specialized batches need.
        void RebuildComboArrays();
        void PublishComboArray(uint16_t** target, uint32_t* targetCount,
                               const struct M2ComboPairList& built);
        void PackTextureCombos();
        void FixUpTextureCombos();
        TSList<CM2SequenceLoad, TSGetLink<CM2SequenceLoad>> m_sequenceLoads;
        void** m_sequenceBuffers = nullptr;   // one .anim buffer per loaded sequence, freed with the shared
        uint32_t m_sequenceBufferCount = 0;

        // Member variables
        // +0x198, a uint16 the reference zeroes in its constructor along with the whole run
        // from +0x16c to +0x1a4. Nothing frozen has corresponds to it and nothing here writes
        // it, so it stays zero -- which is what the reference's own default is.
        //
        // It is carried rather than dropped because CM2Model::AnimateAlphasOnly TESTS it: a
        // non-zero value there forces the full Animate instead of the alpha-only path. Leaving
        // the field out would have meant writing that condition with half its terms. What sets
        // it is not identified -- the only 16-bit write to the offset anywhere in the binary is
        // the constructor clearing it.
        uint16_t uint198 = 0;
        uint32_t m_refCount = 1;
        CM2Cache* m_cache;
        uint32_t m_m2DataLoaded : 1;
        uint32_t m_skinProfileLoaded : 1;
        uint32_t m_flag4 : 1;
        uint32_t m_flag8 : 1;
        uint32_t m_flag10 : 1;
        uint32_t m_flag20 : 1;
        uint32_t m_flag40 : 1;
        CAsyncObject* asyncObject = nullptr;
        CM2Model* m_callbackList = nullptr;
        CM2Model** m_callbackListTail = &this->m_callbackList;
        char m_filePath[STORM_MAX_PATH];
        char* ext = nullptr;
        M2Data* m_data = nullptr;
        CAaBox aaBox154;
        uint32_t m_dataSize = 0;
        M2SkinProfile* skinProfile = nullptr;
        HTEXTURE* textures = nullptr;
        CGxPool* m_indexPool = nullptr;
        CGxBuf* m_indexBuf = nullptr;
        CGxPool* m_vertexPool = nullptr;
        CGxBuf* m_vertexBuf = nullptr;
        CShaderEffect** m_batchShaders = nullptr;
        M2SkinSection* m_skinSections = nullptr;
        uint32_t uint190 = 0;
        uint32_t uint194 = 0;
        // The cache's list of shared models nobody references any more, reference +0x30 through
        // +0x38. Only the unported half of Release links a shared in; AddRef unlinks it on revival.
        CM2Shared** m_freePrev = nullptr;
        CM2Shared* m_freeNext = nullptr;
        uint32_t uint38 = 0;

        // Member functions
        CM2Shared(CM2Cache* cache)
            : m_cache(cache)
            , m_m2DataLoaded(0)
            , m_skinProfileLoaded(0)
            , m_flag4(0)
            , m_flag8(0)
            , m_flag10(0)
            , m_flag20(0)
            , m_flag40(0)
            {};
        ~CM2Shared();
        uint32_t AddRef();
        int32_t CallbackWhenLoaded(CM2Model* model);
        CShaderEffect* CreateSimpleEffect(uint32_t textureCount, uint16_t shader, uint16_t textureCoordComboIndex);
        CShaderEffect* GetEffect(M2Batch* batch);
        int32_t FinishLoadingSkinProfile(uint32_t size);
        int32_t Initialize();
        int32_t InitializeSkinProfile();
        int32_t Load(SFile* file, int32_t a3, CAaBox* a4);
        int32_t LoadSkinProfile(uint32_t profile);
        // Drop the shared index and vertex buffers and the pools behind them. Safe to call when
        // they were never built, and leaves every slot null so SetIndices and SetVertices rebuild
        // on next use -- which is exactly how the instance capacity grows.
        void ReleaseGeometryBuffers();
        // Raise the instance capacity toward `count` and return what is actually available, which
        // may be less than asked for. Callers MUST draw in chunks of the returned value.
        uint32_t ReserveInstances(uint32_t count);
        uint32_t Release();
        int32_t SetIndices();
        int32_t SetVertices(uint32_t a2);
        void SubstituteSimpleShaders();
        void SubstituteSpecializedShaders();
};

#endif
