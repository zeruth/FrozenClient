#include "model/CM2Cache.hpp"
#include "gx/Gx.hpp"
#include "model/CM2ParticleEmitter.hpp"
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

// **A stub with a trap attached.** CM2Scene::Animate takes its multithreaded branch on cache flag
// 0x4, and that branch interleaves: it expects the thread started here to animate the odd entries
// of the animate list while the caller walks the even ones two at a time. With this empty, setting
// that bit leaves every second model frozen mid-pose and logs nothing.
//
// CM2Cache::Initialize therefore refuses to propagate the M2UseThreads CVar into flag 0x4, and
// says so at the spot where the propagation would be added. Port this and WaitThread together,
// and re-enable the bit in the same change.
// Identified 2026-09-23 as FUN_0081bfa0, from CM2Scene::Animate's call order -- frozen calls it at
// exactly that position. The reference stores its two arguments at +0x1014 and +0x1018 and then
// signals a thread object at +0x1008, and it returns with `retl $0x8`, so it takes the two stack
// arguments this signature has: the callback and its context. frozen's own call site passes
// CM2Scene::AnimateThread and the scene, which is the same shape.
//
// **Still not implemented, deliberately.** The warning above has not changed: this and WaitThread
// have to land together, with the M2UseThreads bit re-enabled in the same change, or every second
// model freezes mid-pose.
// ref: FUN_0081bfa0
void CM2Cache::BeginThread(void (*callback)(void*), void* arg) {
    // TODO -- with WaitThread, and not before; see above
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

    // The reference propagates these three unmasked, as `flags & 0x1a0` (00081c211). Nothing in
    // frozen reads them yet -- M2BatchDoodads, M2BatchParticles and M2ForceAdditiveParticleSort --
    // so this is inert today and carried so the bits are right when something does read them.
    this->m_flags |= flags & 0x1a0;

    // **0x4 (M2UseThreads) is deliberately NOT propagated, and this is the place someone would
    // "finish the job" and break the client.** CM2Scene::Animate's 0x4 branch does not merely
    // start a thread: it then walks the animate list two at a time, because the thread it spawned
    // is supposed to take the odd entries. CM2Cache::BeginThread is an empty stub, so setting this
    // bit would leave every second model un-animated, frozen mid-pose, with nothing in the log.
    // Port BeginThread and WaitThread first, then set this.
    //
    // The reference also derives 0x40 here rather than taking it from the caller: it sets it when
    // 0x8 is clear and a capability global is clear too, i.e. "no shader support, use the
    // single-bone fixed-function path". frozen requires shaders, so 0x8 is set and 0x40 stays
    // clear, which is what CM2SceneRender and CM2Shared already assume when they read it.

    // One of this function's remaining TODOs, closed 2026-09-24: the particle twinkle table.
    // The reference fills it inline here (0x81c240) and it is read every frame by every emitter,
    // so this is the only place that may call it -- refilling it mid-run would make every
    // particle in the world blink at once.
    M2ParticleInitTwinkleTable();

    // And the shared index buffer every particle quad draws through, which the reference creates
    // on the next line (0x81c273). One buffer for the whole world; see its definition.
    M2ParticleIndexBufferCreate();

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
