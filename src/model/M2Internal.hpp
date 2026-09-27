#ifndef MODEL_M2_INTERNAL_HPP
#define MODEL_M2_INTERNAL_HPP

#include <cstdint>

extern uint32_t* g_modelPool;

class CM2Model;

// Destruct a model and give its block back to `pool`. The reference factors this out because two
// creation paths unwind through it; CM2Model::Release does the same two steps inline.
void DestroyModel(uint32_t* pool, CM2Model* model);

struct M2SkinSection;

// The element the doodad-batch merge pass (FUN_00832dd0) sorts and then walks in runs. Twelve
// bytes, and the two predicates below are its ordering and its grouping test.
//
// uint08 is NOT named because nothing establishes what it is: neither predicate reads it, and
// the pass's own callers are unported. It is carried so the element keeps the reference's size.
struct M2MergeEntry {
    CM2Model* model;
    uint32_t batchIndex;
    uint32_t uint08;
};

// The merge pass's ordering. External linkage on purpose -- see the note at the definition.
bool M2MergeEntryLess(const M2MergeEntry& a, const M2MergeEntry& b);

// A growable list of combo PAIRS, as the specialized-shader pass builds one on the stack. The count
// is in shorts rather than pairs, so it is always even, and that is the reference's own convention.
struct M2ComboPairList {
    int32_t count;
    int16_t* data;
};

// Make sure one packed pair is present in a sorted pair list, inserting it in order if it is not.
void M2EnsureComboPair(uint16_t packed, M2ComboPairList& list, int32_t unbias);

// Whether two neighbours of the sorted run belong to the same merge group.
bool M2MergeEntriesGroup(const M2MergeEntry& a, const M2MergeEntry& b);

#endif
