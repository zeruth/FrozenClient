#include "model/CM2Cache.hpp"
#include "gx/Gx.hpp"
#include "model/CM2Shared.hpp"
#include "model/Model2.hpp"
#include "util/Filesystem.hpp"
#include "util/SFile.hpp"
#include <cstring>
#include <new>
#include <storm/Memory.hpp>
#include <storm/String.hpp>
#include <tempest/Box.hpp>

CM2Cache CM2Cache::s_cache;

void CM2Cache::BeginThread(void (*callback)(void*), void* arg) {
    // TODO
}

CM2Shared* CM2Cache::CreateShared(const char* path, uint32_t flags) {
    char convertedPath[STORM_MAX_PATH];
    if (!M2ConvertModelFileName(path, convertedPath, STORM_MAX_PATH, flags)) {
        return nullptr;
    }

    char* ext = OsPathFindExtensionWithDot(convertedPath);

    CAaBox v28;
    ModelBlobQuery(convertedPath, v28.b, v28.t);

    if (ext) {
        *ext = '.';
    }

    // TODO

    SFile* fileptr;

    if (SFile::OpenEx(nullptr, convertedPath, (flags >> 2) & 1, &fileptr)) {
        auto m = SMemAlloc(sizeof(CM2Shared), __FILE__, __LINE__, 0x0);
        auto shared = new (m) CM2Shared(this);

        if (shared->Load(fileptr, flags & 0x4, &v28)) {
            strcpy(shared->m_filePath, convertedPath);
            shared->ext = strrchr(shared->m_filePath, '.');;

            if (shared->ext > shared->m_filePath) {
                // TODO
            }

            // TODO

            return shared;
        }

        SFile::Close(fileptr);
        delete shared;
    }

    return nullptr;
}

void CM2Cache::GarbageCollect(int32_t a2) {
    // Nothing to collect: this cache has no hash table and CM2Shared::Release destroys on the
    // spot, so no model is ever queued. The reference pops its pending list while entries are
    // older than 9999ms, or all of them when a2 is non-zero. Porting it means porting the cache
    // itself -- see docs/ref/parity-model-cache.md, which has all three functions worked out.
    // TODO
}

int32_t CM2Cache::Initialize(uint32_t flags) {
    if (this->m_initialized) {
        // TODO

        return 1;
    }

    // TODO

    // M2RegisterCVars packs the model CVars into this word and every one of them arrives here.
    // Only 0x8 was being propagated, so the rest were registered, defaulted on, and dropped --
    // the same shape as the CWorldParam graphics CVars audited on 2026-09-23.
    //
    // 0x1 is `M2UseZFill`, "z-fill transparent objects", and it gates the alpha-tested depth
    // prepass in CM2Scene::Animate. Without this the prepass is built and never runs.
    //
    // 0x2 is `M2UseClipPlanes`, "use clip planes for sorting transparent objects", which is the
    // liquid-plane split. That is still inert: the refinement it enables sits behind a TODO in
    // Animate and nothing sets CM2Lighting's 0x40 bit, so propagating it changes nothing today.
    // It is propagated anyway so the two arrive together when that lands.
    //
    // **Diverged, deliberately.** The reference gates 0x1 on a CGxCaps field at +0xf4 and 0x2 on
    // +0xf8 with a value at +0xb4 (FUN_0081c0d0, around 0x0081c1b3). frozen's CGxCaps stops well
    // short of those offsets -- its own int130/int134/int138 sit where the reference has 0xa0 --
    // so the fields do not exist to test. Both are hardware capability checks that a D3D9 device
    // running frozen's shader path will pass, and `M2UseZFill 0` turns the prepass off at runtime
    // if a run disagrees.
    if (flags & 0x1) {
        this->m_flags |= 0x1;
    }

    if (flags & 0x2) {
        this->m_flags |= 0x2;
    }

    if (flags & 0x8) {
        if (GxCaps().m_shaderTargets[GxSh_Vertex] > GxShVS_none && GxCaps().m_shaderTargets[GxSh_Pixel] > GxShPS_none) {
            this->m_flags |= 0x8;
        }
    }

    // Still dropped, and each is its own port: 0x4 (M2UseThreads, gated on a processor count),
    // 0x20 (M2BatchDoodads), 0x80 (M2BatchParticles) and 0x100 (M2ForceAdditiveParticleSort),
    // which the reference propagates unmasked as `flags & 0x1a0`.

    // TODO

    this->m_initialized = 1;

    return 1;
}

void CM2Cache::UpdateShared() {
    // TODO
}

void CM2Cache::WaitThread() {
    // TODO
}
